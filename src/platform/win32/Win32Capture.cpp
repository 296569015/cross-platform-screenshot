#include "Win32Capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace sst::platform::win32 {

Win32Capture::Win32Capture(ID3D11Device* externalDevice) {
    if (externalDevice) {
        d3dDevice_ = externalDevice;
        d3dDevice_->AddRef();
        d3dDevice_->GetImmediateContext(&d3dContext_);
        ownsDevice_ = false;
    }
}

Win32Capture::~Win32Capture() {
    shutdown();
}

bool Win32Capture::initialize(int monitorIndex) {
    if (!d3dDevice_) {
        // Create our own D3D11 device
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL featureLevel;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels, 1,
            D3D11_SDK_VERSION,
            &d3dDevice_,
            &featureLevel,
            &d3dContext_
        );
        if (FAILED(hr)) return false;
        ownsDevice_ = true;
    }

    return initDxgiDuplication(monitorIndex);
}

bool Win32Capture::initDxgiDuplication(int monitorIndex) {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice_->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    // Get the specified output (monitor), default to primary (index 0)
    int targetIndex = (monitorIndex < 0) ? 0 : monitorIndex;
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(targetIndex, &output);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    hr = output1->DuplicateOutput(d3dDevice_, &duplication_);
    if (FAILED(hr)) return false;

    // Get output dimensions
    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);
    captureSize_.w = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    captureSize_.h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

    // Create a staging texture for the zero-copy bridge.
    // This texture has MISC_SHARED so ANGLE can import it.
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width              = captureSize_.w;
    texDesc.Height             = captureSize_.h;
    texDesc.MipLevels          = 1;
    texDesc.ArraySize          = 1;
    texDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count   = 1;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
    texDesc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;

    hr = d3dDevice_->CreateTexture2D(&texDesc, nullptr, &stagingTexture_);
    if (FAILED(hr)) return false;

    return true;
}

bool Win32Capture::acquireFrame(CapturedFrame& outFrame,
                                CaptureError& outError,
                                uint32_t timeoutMs) {
    if (!duplication_) {
        outError = CaptureError::NotInitialized;
        return false;
    }

    // Release any previously held frame
    if (frameAcquired_) {
        duplication_->ReleaseFrame();
        frameAcquired_ = false;
    }

    // DXGI Desktop Duplication only returns a frame when the desktop changes.
    // Nudge the cursor to guarantee at least one changed pixel.
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    SetCursorPos(cursorPos.x + 1, cursorPos.y);
    SetCursorPos(cursorPos.x, cursorPos.y);

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        outError = CaptureError::Timeout;
        return false;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        outError = CaptureError::DeviceLost;
        return false;
    }
    if (FAILED(hr)) {
        outError = CaptureError::AccessDenied;
        return false;
    }

    frameAcquired_ = true;

    // Get the captured texture
    ComPtr<ID3D11Texture2D> capturedTexture;
    hr = resource.As(&capturedTexture);
    if (FAILED(hr)) {
        outError = CaptureError::UnsupportedFormat;
        return false;
    }

    // Copy to our shared staging texture (this is a GPU→GPU copy, fast)
    d3dContext_->CopyResource(stagingTexture_, capturedTexture.Get());

    outFrame.nativeTextureHandle = stagingTexture_;
    outFrame.size = captureSize_;
    outFrame.frameIndex++;

    return true;
}

void Win32Capture::releaseFrame() {
    if (frameAcquired_ && duplication_) {
        duplication_->ReleaseFrame();
        frameAcquired_ = false;
    }
}

bool Win32Capture::readFramePixels(std::vector<uint8_t>& outPixels,
                                   int& outWidth, int& outHeight) {
    if (!stagingTexture_ || !d3dDevice_ || !d3dContext_)
        return false;

    outWidth = captureSize_.w;
    outHeight = captureSize_.h;

    // Create a CPU-readable texture if we don't have one yet
    if (!cpuTexture_) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = captureSize_.w;
        desc.Height             = captureSize_.h;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

        HRESULT hr = d3dDevice_->CreateTexture2D(&desc, nullptr, &cpuTexture_);
        if (FAILED(hr)) return false;
    }

    // Copy from GPU staging texture to CPU-readable texture
    d3dContext_->CopyResource(cpuTexture_, stagingTexture_);

    // Map and read pixels
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = d3dContext_->Map(cpuTexture_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    // Output as RGBA (swap B and R channels from BGRA)
    outPixels.resize(static_cast<size_t>(outWidth) * outHeight * 4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* dst = outPixels.data();

    for (int y = 0; y < outHeight; ++y) {
        const uint8_t* row = src + y * mapped.RowPitch;
        for (int x = 0; x < outWidth; ++x) {
            dst[0] = row[2]; // R (from B)
            dst[1] = row[1]; // G
            dst[2] = row[0]; // B (from R)
            dst[3] = row[3]; // A
            dst += 4;
            row += 4;
        }
    }

    d3dContext_->Unmap(cpuTexture_, 0);
    return true;
}

std::vector<MonitorInfo> Win32Capture::enumerateMonitors() const {
    std::vector<MonitorInfo> monitors;

    if (!d3dDevice_) return monitors;

    ComPtr<IDXGIDevice> dxgiDevice;
    d3dDevice_->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIOutput> output;
    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        MonitorInfo info;
        info.bounds.x = desc.DesktopCoordinates.left;
        info.bounds.y = desc.DesktopCoordinates.top;
        info.bounds.w = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        info.bounds.h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
        info.workArea = info.bounds; // TODO: query actual work area
        info.isPrimary = (i == 0);
        info.dpiScale = 1.0f; // TODO: query per-monitor DPI

        monitors.push_back(info);
        output.Reset();
    }

    return monitors;
}

void Win32Capture::shutdown() {
    if (frameAcquired_ && duplication_) {
        duplication_->ReleaseFrame();
        frameAcquired_ = false;
    }

    if (duplication_) {
        duplication_->Release();
        duplication_ = nullptr;
    }
    if (stagingTexture_) {
        stagingTexture_->Release();
        stagingTexture_ = nullptr;
    }
    if (cpuTexture_) {
        cpuTexture_->Release();
        cpuTexture_ = nullptr;
    }
    if (ownsDevice_) {
        if (d3dContext_) { d3dContext_->Release(); d3dContext_ = nullptr; }
        if (d3dDevice_)  { d3dDevice_->Release();  d3dDevice_ = nullptr; }
    } else {
        if (d3dContext_) { d3dContext_->Release(); d3dContext_ = nullptr; }
        if (d3dDevice_)  { d3dDevice_->Release();  d3dDevice_ = nullptr; }
    }
}

} // namespace sst::platform::win32
