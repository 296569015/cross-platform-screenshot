#pragma once

#include "ShaderProgram.h"
#include <platform/PlatformTypes.h>
#include <cstdint>
#include <vector>

namespace sst::renderer {

/// Renders 2D shape primitives: rectangles, lines, arrows.
class ShapeRenderer {
public:
    ShapeRenderer();
    ~ShapeRenderer();

    bool initialize();

    /// Set the orthographic projection (same as SpriteBatch).
    void setProjection(float left, float top, float right, float bottom);

    /// Draw a filled rectangle.
    void drawRectFilled(float x, float y, float w, float h,
                        platform::Color color);

    /// Draw a filled rounded rectangle.
    void drawRoundedRectFilled(float x, float y, float w, float h,
                               float radius, platform::Color color);

    /// Draw a rectangle outline.
    void drawRectOutline(float x, float y, float w, float h,
                         platform::Color color, float thickness = 2.0f);

    /// Draw a rounded rectangle outline.
    void drawRoundedRectOutline(float x, float y, float w, float h,
                                float radius, platform::Color color,
                                float thickness = 2.0f);

    /// Draw a line segment.
    void drawLine(float x0, float y0, float x1, float y1,
                  platform::Color color, float thickness = 2.0f);

    /// Draw a continuous polyline by joining line segments.
    void drawPolyline(const std::vector<platform::PointF>& points,
                      platform::Color color, float thickness = 2.0f);

    /// Draw an arrow from (x0,y0) to (x1,y1) with arrowhead.
    void drawArrow(float x0, float y0, float x1, float y1,
                   platform::Color color, float thickness = 2.0f,
                   float headSize = 12.0f);

    void shutdown();

private:
    ShaderProgram shader_;      // flat shader (rects)
    ShaderProgram aaShader_;    // antialiased shader (lines/arrows)
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t aaVao_ = 0;       // VAO for aa shader (pos + edgeDist)
    uint32_t aaVbo_ = 0;
    float projection_[16] = {};

    /// Draw triangles using the AA shader (vertices: x,y,edgeDist per vertex)
    void drawAATriangles(const float* vertices, int vertexCount,
                         platform::Color color);
};

} // namespace sst::renderer
