#pragma once

#include <platform/IPlatformCapture.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGIOutputDuplication;
struct ID3D11Texture2D;

namespace sst::platform::win32 {

/// DXGI Desktop Duplication based screen capture.
/// Requires Windows 10+ and an interactive desktop session.
class Win32Capture final : public IPlatformCapture {
public:
    /// If externalDevice is non-null, use it (to share ANGLE's D3D11 device).
    /// Otherwise, create a new D3D11 device internally.
    explicit Win32Capture(ID3D11Device* externalDevice = nullptr);
    ~Win32Capture() override;

    bool initialize(int monitorIndex = -1) override;
    bool acquireFrame(CapturedFrame& outFrame,
                      CaptureError& outError,
                      uint32_t timeoutMs = 100) override;
    void releaseFrame() override;
    bool readFramePixels(std::vector<uint8_t>& outPixels,
                         int& outWidth, int& outHeight) override;
    std::vector<MonitorInfo> enumerateMonitors() const override;
    void shutdown() override;

private:
    bool initDxgiDuplication(int monitorIndex);

    ID3D11Device*           d3dDevice_       = nullptr;
    ID3D11DeviceContext*    d3dContext_       = nullptr;
    IDXGIOutputDuplication* duplication_      = nullptr;
    ID3D11Texture2D*        stagingTexture_  = nullptr;
    ID3D11Texture2D*        cpuTexture_      = nullptr;  // CPU-readable copy
    bool                    ownsDevice_      = false;
    bool                    frameAcquired_   = false;
    Size                    captureSize_;
};

} // namespace sst::platform::win32
