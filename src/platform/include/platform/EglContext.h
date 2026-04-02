#pragma once

#include "PlatformTypes.h"
#include <cstdint>

namespace sst::platform {

/// EGL context management via ANGLE.
/// Handles GL context lifecycle and native texture import (zero-copy bridge).
class EglContext {
public:
    virtual ~EglContext() = default;

    /// Initialize EGL display + context using the given native window handle.
    virtual bool initialize(void* nativeWindowHandle) = 0;

    /// Import a platform-native GPU texture as a GL texture (zero-copy).
    /// On Windows: nativeTexture is an ID3D11Texture2D*.
    /// Returns the GL texture name (GLuint), or 0 on failure.
    virtual uint32_t importNativeTexture(void* nativeTexture, Size size) = 0;

    /// Release a previously imported texture.
    virtual void releaseImportedTexture(uint32_t glTexture) = 0;

    /// Make this context current on the calling thread.
    virtual void makeCurrent() = 0;

    /// Swap the front and back buffers.
    virtual void swapBuffers() = 0;

    /// Get the current surface size in pixels.
    virtual Size getSurfaceSize() const = 0;

    /// Get the underlying native graphics device handle.
    /// On Windows: returns the ID3D11Device* used by ANGLE.
    /// Used by the capture layer to share the same D3D11 device.
    virtual void* getNativeDevice() const = 0;

    /// Shutdown EGL and release all resources.
    virtual void shutdown() = 0;
};

} // namespace sst::platform
