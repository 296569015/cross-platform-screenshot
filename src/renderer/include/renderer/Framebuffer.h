#pragma once

#include "ShaderProgram.h"
#include <platform/PlatformTypes.h>
#include <cstdint>

namespace sst::renderer {

/// Framebuffer Object wrapper for offscreen rendering and readback.
class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();

    /// Create an FBO with the given dimensions.
    bool create(int width, int height);

    /// Bind this FBO as the render target.
    void bind() const;

    /// Unbind (restore default framebuffer).
    static void unbind();

    /// Read back pixels from this FBO as RGBA.
    /// outPixels must have space for width*height*4 bytes.
    void readPixels(uint8_t* outPixels) const;

    /// Get the color attachment texture ID.
    uint32_t textureId() const { return colorTexture_; }

    int width()  const { return width_; }
    int height() const { return height_; }

    void destroy();

private:
    uint32_t fbo_          = 0;
    uint32_t colorTexture_ = 0;
    int width_  = 0;
    int height_ = 0;
};

} // namespace sst::renderer
