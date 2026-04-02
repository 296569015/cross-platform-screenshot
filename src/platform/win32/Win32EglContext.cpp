#include "Win32EglContext.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <d3d11.h>
#include <cassert>

namespace sst::platform::win32 {

Win32EglContext::Win32EglContext() = default;

Win32EglContext::~Win32EglContext() {
    shutdown();
}

bool Win32EglContext::initialize(void* nativeWindowHandle) {
    // Load eglGetPlatformDisplayEXT dynamically (ANGLE extension)
    auto eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!eglGetPlatformDisplayEXT) return false;

    // Request ANGLE's D3D11 backend
    const EGLint displayAttribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,
        EGL_NONE
    };

    eglDisplay_ = eglGetPlatformDisplayEXT(
        EGL_PLATFORM_ANGLE_ANGLE,
        EGL_DEFAULT_DISPLAY,
        displayAttribs
    );
    if (eglDisplay_ == EGL_NO_DISPLAY) return false;

    EGLint major, minor;
    if (!eglInitialize(static_cast<EGLDisplay>(eglDisplay_), &major, &minor))
        return false;

    // Choose config with RGBA8 + depth
    const EGLint configAttribs[] = {
        EGL_RED_SIZE,       8,
        EGL_GREEN_SIZE,     8,
        EGL_BLUE_SIZE,      8,
        EGL_ALPHA_SIZE,     8,
        EGL_DEPTH_SIZE,     0,
        EGL_STENCIL_SIZE,   0,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    EGLConfig config;
    if (!eglChooseConfig(static_cast<EGLDisplay>(eglDisplay_),
                         configAttribs, &config, 1, &numConfigs)
        || numConfigs == 0) {
        return false;
    }
    eglConfig_ = config;

    // Create GLES 3.0 context
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };

    eglContext_ = eglCreateContext(
        static_cast<EGLDisplay>(eglDisplay_),
        config,
        EGL_NO_CONTEXT,
        contextAttribs
    );
    if (eglContext_ == EGL_NO_CONTEXT) return false;

    // Create window surface
    eglSurface_ = eglCreateWindowSurface(
        static_cast<EGLDisplay>(eglDisplay_),
        config,
        static_cast<EGLNativeWindowType>(nativeWindowHandle),
        nullptr
    );
    if (eglSurface_ == EGL_NO_SURFACE) return false;

    // Make current
    eglMakeCurrent(
        static_cast<EGLDisplay>(eglDisplay_),
        static_cast<EGLSurface>(eglSurface_),
        static_cast<EGLSurface>(eglSurface_),
        static_cast<EGLContext>(eglContext_)
    );

    // Retrieve ANGLE's internal D3D11 device for texture sharing
    EGLAttrib deviceAttrib;
    EGLDeviceEXT eglDevice;
    auto eglQueryDisplayAttribEXT =
        reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(
            eglGetProcAddress("eglQueryDisplayAttribEXT"));
    auto eglQueryDeviceAttribEXT =
        reinterpret_cast<PFNEGLQUERYDEVICEATTRIBEXTPROC>(
            eglGetProcAddress("eglQueryDeviceAttribEXT"));

    if (eglQueryDisplayAttribEXT && eglQueryDeviceAttribEXT) {
        eglQueryDisplayAttribEXT(
            static_cast<EGLDisplay>(eglDisplay_),
            EGL_DEVICE_EXT,
            reinterpret_cast<EGLAttrib*>(&eglDevice)
        );
        eglQueryDeviceAttribEXT(
            eglDevice,
            EGL_D3D11_DEVICE_ANGLE,
            &deviceAttrib
        );
        d3dDevice_ = reinterpret_cast<ID3D11Device*>(deviceAttrib);
    }

    // Query surface size
    EGLint w, h;
    eglQuerySurface(static_cast<EGLDisplay>(eglDisplay_),
                    static_cast<EGLSurface>(eglSurface_),
                    EGL_WIDTH, &w);
    eglQuerySurface(static_cast<EGLDisplay>(eglDisplay_),
                    static_cast<EGLSurface>(eglSurface_),
                    EGL_HEIGHT, &h);
    surfaceSize_ = { w, h };

    return true;
}

uint32_t Win32EglContext::importNativeTexture(void* nativeTexture, Size size) {
    // Use EGL_ANGLE_d3d11_texture to import a D3D11 texture as a GL texture.
    // This is the zero-copy bridge from DXGI capture to GL rendering.

    auto eglCreateImageKHR =
        reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            eglGetProcAddress("eglCreateImageKHR"));
    if (!eglCreateImageKHR) return 0;

    const EGLint imageAttribs[] = {
        EGL_WIDTH,  size.w,
        EGL_HEIGHT, size.h,
        EGL_NONE
    };

    EGLImageKHR image = eglCreateImageKHR(
        static_cast<EGLDisplay>(eglDisplay_),
        EGL_NO_CONTEXT,
        EGL_D3D11_TEXTURE_ANGLE,
        static_cast<EGLClientBuffer>(nativeTexture),
        imageAttribs
    );
    if (image == EGL_NO_IMAGE_KHR) return 0;

    // Create a GL texture and bind the EGL image to it
    auto glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!glEGLImageTargetTexture2DOES) return 0;

    GLuint glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(GL_TEXTURE_2D, glTexture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // TODO: store image handle for cleanup in releaseImportedTexture

    return glTexture;
}

void Win32EglContext::releaseImportedTexture(uint32_t glTexture) {
    if (glTexture != 0) {
        glDeleteTextures(1, &glTexture);
    }
    // TODO: also destroy the EGLImageKHR handle
}

void Win32EglContext::makeCurrent() {
    eglMakeCurrent(
        static_cast<EGLDisplay>(eglDisplay_),
        static_cast<EGLSurface>(eglSurface_),
        static_cast<EGLSurface>(eglSurface_),
        static_cast<EGLContext>(eglContext_)
    );
}

void Win32EglContext::swapBuffers() {
    eglSwapBuffers(
        static_cast<EGLDisplay>(eglDisplay_),
        static_cast<EGLSurface>(eglSurface_)
    );
}

Size Win32EglContext::getSurfaceSize() const {
    return surfaceSize_;
}

void* Win32EglContext::getNativeDevice() const {
    return d3dDevice_;
}

void Win32EglContext::shutdown() {
    auto display = static_cast<EGLDisplay>(eglDisplay_);
    if (display == EGL_NO_DISPLAY) return;

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (eglSurface_ != EGL_NO_SURFACE)
        eglDestroySurface(display, static_cast<EGLSurface>(eglSurface_));
    if (eglContext_ != EGL_NO_CONTEXT)
        eglDestroyContext(display, static_cast<EGLContext>(eglContext_));
    eglTerminate(display);

    eglDisplay_ = EGL_NO_DISPLAY;
    eglSurface_ = EGL_NO_SURFACE;
    eglContext_ = EGL_NO_CONTEXT;
    d3dDevice_  = nullptr;
}

} // namespace sst::platform::win32
