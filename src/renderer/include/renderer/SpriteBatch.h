#pragma once

#include "ShaderProgram.h"
#include <platform/PlatformTypes.h>
#include <cstdint>

namespace sst::renderer {

/// Batched 2D quad renderer for textured sprites.
/// Used for rendering the screenshot texture and UI elements.
class SpriteBatch {
public:
    SpriteBatch();
    ~SpriteBatch();

    /// Initialize GPU resources (VAO, VBO).
    bool initialize();

    /// Draw a textured quad covering the given rectangle.
    /// texCoords: { u0, v0, u1, v1 } in normalized texture space.
    void drawQuad(uint32_t textureId,
                  float x, float y, float w, float h,
                  float u0 = 0.f, float v0 = 0.f,
                  float u1 = 1.f, float v1 = 1.f);

    /// Draw a fullscreen quad (covers entire viewport).
    void drawFullscreen(uint32_t textureId);

    /// Set the orthographic projection matrix for 2D rendering.
    /// Typically: left=0, top=0, right=screenW, bottom=screenH.
    void setProjection(float left, float top, float right, float bottom);

    ShaderProgram& shader() { return shader_; }

    void shutdown();

private:
    ShaderProgram shader_;
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    float projection_[16] = {};
};

} // namespace sst::renderer
