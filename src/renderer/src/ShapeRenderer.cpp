#include "renderer/ShapeRenderer.h"
#include <GLES3/gl3.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace sst::renderer {

// ── Flat shader (rectangles / solid fills) ──────────────────────
static const char* kShapeVert = R"(#version 300 es
layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
}
)";

static const char* kShapeFrag = R"(#version 300 es
precision mediump float;

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
)";

// ── AA shader (lines / arrows with edge feathering) ─────────────
static const char* kAAVert = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in float a_edgeDist;  // 0.0 at center, 1.0 at edge

uniform mat4 u_projection;

out float v_edgeDist;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_edgeDist = a_edgeDist;
}
)";

static const char* kAAFrag = R"(#version 300 es
precision mediump float;

uniform vec4 u_color;

in float v_edgeDist;
out vec4 fragColor;

void main() {
    // smoothstep from 0.0 (fully opaque core) to 1.0 (fully transparent edge)
    // The feather zone is between 0.7 and 1.0 — inner 70% is solid
    float alpha = 1.0 - smoothstep(0.7, 1.0, abs(v_edgeDist));
    fragColor = vec4(u_color.rgb, u_color.a * alpha);
}
)";

// Feather margin in pixels — the quad extends this far beyond the line thickness
static constexpr float kFeatherPx = 1.5f;

ShapeRenderer::ShapeRenderer() = default;

ShapeRenderer::~ShapeRenderer() {
    shutdown();
}

bool ShapeRenderer::initialize() {
    // ── Flat shader + VAO ──
    if (!shader_.compile(kShapeVert, kShapeFrag))
        return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 256 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // ── AA shader + VAO (pos.xy + edgeDist = 3 floats per vertex) ──
    if (!aaShader_.compile(kAAVert, kAAFrag))
        return false;

    glGenVertexArrays(1, &aaVao_);
    glGenBuffers(1, &aaVbo_);

    glBindVertexArray(aaVao_);
    glBindBuffer(GL_ARRAY_BUFFER, aaVbo_);
    glBufferData(GL_ARRAY_BUFFER, 512 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    // location 0: vec2 position  (stride = 3 floats)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // location 1: float edgeDist (stride = 3 floats, offset = 2 floats)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    std::memset(projection_, 0, sizeof(projection_));
    projection_[0] = projection_[5] = projection_[10] = projection_[15] = 1.0f;

    return true;
}

void ShapeRenderer::setProjection(float left, float top, float right, float bottom) {
    std::memset(projection_, 0, sizeof(projection_));
    projection_[0]  =  2.0f / (right - left);
    projection_[5]  =  2.0f / (top - bottom);
    projection_[10] = -1.0f;
    projection_[12] = -(right + left) / (right - left);
    projection_[13] = -(top + bottom) / (top - bottom);
    projection_[15] =  1.0f;
}

void ShapeRenderer::drawAATriangles(const float* vertices, int vertexCount,
                                     platform::Color color) {
    aaShader_.use();
    aaShader_.setMat4("u_projection", projection_);
    aaShader_.setVec4("u_color",
                      color.r / 255.0f, color.g / 255.0f,
                      color.b / 255.0f, color.a / 255.0f);

    glBindVertexArray(aaVao_);
    glBindBuffer(GL_ARRAY_BUFFER, aaVbo_);
    GLsizeiptr dataSize = static_cast<GLsizeiptr>(vertexCount) * 3 * sizeof(float);
    // Grow buffer if needed
    glBufferData(GL_ARRAY_BUFFER, dataSize, vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

void ShapeRenderer::drawRectFilled(float x, float y, float w, float h,
                                   platform::Color color) {
    float vertices[] = {
        x,     y,
        x + w, y,
        x,     y + h,
        x + w, y,
        x + w, y + h,
        x,     y + h,
    };

    shader_.use();
    shader_.setMat4("u_projection", projection_);
    shader_.setVec4("u_color",
                    color.r / 255.0f, color.g / 255.0f,
                    color.b / 255.0f, color.a / 255.0f);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void ShapeRenderer::drawRectOutline(float x, float y, float w, float h,
                                    platform::Color color, float thickness) {
    float t = thickness;
    // Draw 4 thin rectangles as the outline
    drawRectFilled(x, y, w, t, color);           // top
    drawRectFilled(x, y + h - t, w, t, color);   // bottom
    drawRectFilled(x, y, t, h, color);            // left
    drawRectFilled(x + w - t, y, t, h, color);   // right
}

void ShapeRenderer::drawLine(float x0, float y0, float x1, float y1,
                             platform::Color color, float thickness) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    // Half-width including feather margin
    float halfCore = thickness * 0.5f;
    float halfTotal = halfCore + kFeatherPx;

    // Normal direction (perpendicular to line)
    float nx = -dy / len;
    float ny =  dx / len;

    // edgeDist: 0.0 at core edge, 1.0 at outer feather edge
    // Inner vertices (core edge): edgeDist = halfCore / halfTotal
    // Outer vertices (feather edge): edgeDist = 1.0
    float coreD = halfCore / halfTotal;  // ~0.7 for typical values

    // 4 outer corners + 4 inner corners = 8 vertices → 4 quads → but simpler:
    // Just make the quad wider and compute edgeDist per vertex
    float oxP = nx * halfTotal;  // outer offset +
    float oyP = ny * halfTotal;
    float ixP = nx * halfCore;   // inner offset +
    float iyP = ny * halfCore;

    // 6 vertices for the core (edgeDist = 0) + 2 quads for feather strips
    // Simpler approach: one quad with edgeDist varying from -1 to +1
    float vertices[] = {
        // Triangle 1: top-left, bottom-left, top-right
        x0 + oxP, y0 + oyP,  1.0f,    // outer edge +
        x0 - oxP, y0 - oyP, -1.0f,    // outer edge -
        x1 + oxP, y1 + oyP,  1.0f,    // outer edge +
        // Triangle 2: top-right, bottom-left, bottom-right
        x1 + oxP, y1 + oyP,  1.0f,
        x0 - oxP, y0 - oyP, -1.0f,
        x1 - oxP, y1 - oyP, -1.0f,
    };

    // For this approach, we use the fragment shader to feather based on |edgeDist|
    // edgeDist goes from -1 (one edge) through 0 (center) to +1 (other edge)
    // The core region is |edgeDist| < halfCore/halfTotal
    // We smoothstep from that ratio to 1.0

    // Set the feather threshold uniform
    aaShader_.use();
    aaShader_.setMat4("u_projection", projection_);
    aaShader_.setVec4("u_color",
                      color.r / 255.0f, color.g / 255.0f,
                      color.b / 255.0f, color.a / 255.0f);

    glBindVertexArray(aaVao_);
    glBindBuffer(GL_ARRAY_BUFFER, aaVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void ShapeRenderer::drawArrow(float x0, float y0, float x1, float y1,
                              platform::Color color, float thickness,
                              float headSize) {
    // Draw the line shaft with AA
    drawLine(x0, y0, x1, y1, color, thickness);

    // Draw arrowhead as AA triangle
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float ux = dx / len;
    float uy = dy / len;

    // Arrowhead base point
    float bx = x1 - ux * headSize;
    float by = y1 - uy * headSize;

    // Perpendicular direction
    float px = -uy * headSize * 0.5f;
    float py =  ux * headSize * 0.5f;

    // For the arrowhead, use the AA shader with edgeDist = 0 at center
    // Tip vertex = 0 (center), wing vertices = 1.0 (will be slightly feathered)
    // This gives a subtle softening at the arrowhead edges
    float vertices[] = {
        x1,       y1,        0.0f,   // tip (center, fully opaque)
        bx + px,  by + py,   0.6f,   // left wing (slight feather)
        bx - px,  by - py,   0.6f,   // right wing (slight feather)
    };

    drawAATriangles(vertices, 3, color);
}

void ShapeRenderer::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (aaVbo_) { glDeleteBuffers(1, &aaVbo_); aaVbo_ = 0; }
    if (aaVao_) { glDeleteVertexArrays(1, &aaVao_); aaVao_ = 0; }
}

} // namespace sst::renderer
