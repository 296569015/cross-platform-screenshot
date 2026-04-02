#include "renderer/ShapeRenderer.h"
#include <GLES3/gl3.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace sst::renderer {

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

ShapeRenderer::ShapeRenderer() = default;

ShapeRenderer::~ShapeRenderer() {
    shutdown();
}

bool ShapeRenderer::initialize() {
    if (!shader_.compile(kShapeVert, kShapeFrag))
        return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // Allocate enough for most shapes (dynamic)
    glBufferData(GL_ARRAY_BUFFER, 256 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
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
    shader_.setVec4("u_color", color.r, color.g, color.b, color.a);

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
    // Build a thick line as a quad (two triangles)
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float nx = -dy / len * thickness * 0.5f;
    float ny =  dx / len * thickness * 0.5f;

    float vertices[] = {
        x0 + nx, y0 + ny,
        x0 - nx, y0 - ny,
        x1 + nx, y1 + ny,
        x1 + nx, y1 + ny,
        x0 - nx, y0 - ny,
        x1 - nx, y1 - ny,
    };

    shader_.use();
    shader_.setMat4("u_projection", projection_);
    shader_.setVec4("u_color", color.r, color.g, color.b, color.a);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void ShapeRenderer::drawArrow(float x0, float y0, float x1, float y1,
                              platform::Color color, float thickness,
                              float headSize) {
    // Draw the line shaft
    drawLine(x0, y0, x1, y1, color, thickness);

    // Draw arrowhead as a filled triangle
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float ux = dx / len;
    float uy = dy / len;

    // Arrowhead base point (along the shaft, behind the tip)
    float bx = x1 - ux * headSize;
    float by = y1 - uy * headSize;

    // Perpendicular direction
    float px = -uy * headSize * 0.5f;
    float py =  ux * headSize * 0.5f;

    float vertices[] = {
        x1,       y1,        // tip
        bx + px,  by + py,   // left wing
        bx - px,  by - py,   // right wing
    };

    shader_.use();
    shader_.setMat4("u_projection", projection_);
    shader_.setVec4("u_color", color.r, color.g, color.b, color.a);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void ShapeRenderer::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

} // namespace sst::renderer
