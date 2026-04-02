#include "renderer/SpriteBatch.h"
#include <GLES3/gl3.h>
#include <cstring>

namespace sst::renderer {

static const char* kSpriteVert = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform mat4 u_projection;

out vec2 v_texCoord;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
)";

static const char* kSpriteFrag = R"(#version 300 es
precision mediump float;

uniform sampler2D u_texture;

in vec2 v_texCoord;
out vec4 fragColor;

void main() {
    fragColor = texture(u_texture, v_texCoord);
}
)";

SpriteBatch::SpriteBatch() = default;

SpriteBatch::~SpriteBatch() {
    shutdown();
}

bool SpriteBatch::initialize() {
    if (!shader_.compile(kSpriteVert, kSpriteFrag))
        return false;

    // Create a quad VAO/VBO (2 triangles, positions + texcoords)
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // Allocate space for dynamic quad data (6 vertices * 4 floats each)
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Position attribute (location 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // TexCoord attribute (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Default identity projection
    std::memset(projection_, 0, sizeof(projection_));
    projection_[0]  = 1.0f;
    projection_[5]  = 1.0f;
    projection_[10] = 1.0f;
    projection_[15] = 1.0f;

    return true;
}

void SpriteBatch::setProjection(float left, float top, float right, float bottom) {
    // Orthographic projection matrix (column-major for GL)
    std::memset(projection_, 0, sizeof(projection_));
    projection_[0]  =  2.0f / (right - left);
    projection_[5]  =  2.0f / (top - bottom);  // flip Y
    projection_[10] = -1.0f;
    projection_[12] = -(right + left) / (right - left);
    projection_[13] = -(top + bottom) / (top - bottom);
    projection_[15] =  1.0f;
}

void SpriteBatch::drawQuad(uint32_t textureId,
                           float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1) {
    // 6 vertices (2 triangles), each: x, y, u, v
    float vertices[] = {
        x,     y,     u0, v0,  // top-left
        x + w, y,     u1, v0,  // top-right
        x,     y + h, u0, v1,  // bottom-left

        x + w, y,     u1, v0,  // top-right
        x + w, y + h, u1, v1,  // bottom-right
        x,     y + h, u0, v1,  // bottom-left
    };

    shader_.use();
    shader_.setMat4("u_projection", projection_);
    shader_.setInt("u_texture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void SpriteBatch::drawFullscreen(uint32_t textureId) {
    // Fullscreen quad in NDC (bypass projection)
    float vertices[] = {
        -1.f, -1.f,  0.f, 1.f,
         1.f, -1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 1.f,
         1.f,  1.f,  1.f, 0.f,
        -1.f,  1.f,  0.f, 0.f,
    };

    // Use identity projection for fullscreen
    float identity[16] = {};
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;

    shader_.use();
    shader_.setMat4("u_projection", identity);
    shader_.setInt("u_texture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void SpriteBatch::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

} // namespace sst::renderer
