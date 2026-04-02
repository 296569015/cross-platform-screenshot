#pragma once

#include <platform/PlatformTypes.h>
#include <cstdint>
#include <string>

namespace sst::renderer {

/// Compile, link, and manage a GLSL ES shader program.
class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    /// Compile and link from source strings.
    bool compile(const char* vertexSrc, const char* fragmentSrc);

    /// Activate this program for rendering.
    void use() const;

    /// Uniform setters.
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setMat4(const char* name, const float* matrix) const;

    uint32_t id() const { return programId_; }

private:
    uint32_t programId_ = 0;

    static uint32_t compileShader(uint32_t type, const char* source);
};

} // namespace sst::renderer
