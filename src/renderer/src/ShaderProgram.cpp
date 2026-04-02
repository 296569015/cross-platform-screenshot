#include "renderer/ShaderProgram.h"

#include <GLES3/gl3.h>
#include <cstdio>

namespace sst::renderer {

ShaderProgram::~ShaderProgram() {
    if (programId_) glDeleteProgram(programId_);
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : programId_(other.programId_) {
    other.programId_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (programId_) glDeleteProgram(programId_);
        programId_ = other.programId_;
        other.programId_ = 0;
    }
    return *this;
}

uint32_t ShaderProgram::compileShader(uint32_t type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[ShaderProgram] Compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::compile(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc);
    if (!vs) return false;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    programId_ = glCreateProgram();
    glAttachShader(programId_, vs);
    glAttachShader(programId_, fs);
    glLinkProgram(programId_);

    GLint success;
    glGetProgramiv(programId_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(programId_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[ShaderProgram] Link error: %s\n", log);
        glDeleteProgram(programId_);
        programId_ = 0;
    }

    // Shaders can be detached and deleted after linking
    glDeleteShader(vs);
    glDeleteShader(fs);

    return programId_ != 0;
}

void ShaderProgram::use() const {
    glUseProgram(programId_);
}

void ShaderProgram::setInt(const char* name, int value) const {
    glUniform1i(glGetUniformLocation(programId_, name), value);
}

void ShaderProgram::setFloat(const char* name, float value) const {
    glUniform1f(glGetUniformLocation(programId_, name), value);
}

void ShaderProgram::setVec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(programId_, name), x, y);
}

void ShaderProgram::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(programId_, name), x, y, z, w);
}

void ShaderProgram::setMat4(const char* name, const float* matrix) const {
    glUniformMatrix4fv(glGetUniformLocation(programId_, name), 1, GL_FALSE, matrix);
}

} // namespace sst::renderer
