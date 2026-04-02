#include "renderer/Framebuffer.h"
#include <GLES3/gl3.h>

namespace sst::renderer {

Framebuffer::~Framebuffer() {
    destroy();
}

bool Framebuffer::create(int width, int height) {
    width_  = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Create color attachment texture
    glGenTextures(1, &colorTexture_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTexture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return status == GL_FRAMEBUFFER_COMPLETE;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void Framebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::readPixels(uint8_t* outPixels) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, outPixels);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::destroy() {
    if (colorTexture_) { glDeleteTextures(1, &colorTexture_); colorTexture_ = 0; }
    if (fbo_)          { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
}

} // namespace sst::renderer
