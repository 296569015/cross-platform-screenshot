#pragma once

#include <platform/EglContext.h>

// Forward-declare EGL and D3D11 types to keep them out of public headers
struct ID3D11Device;

namespace sst::platform::win32 {

class Win32EglContext final : public EglContext {
public:
    Win32EglContext();
    ~Win32EglContext() override;

    bool initialize(void* nativeWindowHandle) override;
    uint32_t importNativeTexture(void* nativeTexture, Size size) override;
    void releaseImportedTexture(uint32_t glTexture) override;
    void makeCurrent() override;
    void swapBuffers() override;
    Size getSurfaceSize() const override;
    void* getNativeDevice() const override;
    void shutdown() override;

private:
    void* eglDisplay_ = nullptr;
    void* eglContext_ = nullptr;
    void* eglSurface_ = nullptr;
    void* eglConfig_  = nullptr;
    ID3D11Device* d3dDevice_ = nullptr;
    Size surfaceSize_;
};

} // namespace sst::platform::win32
