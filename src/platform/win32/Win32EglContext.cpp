#include "Win32EglContext.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <d3d11.h>
#include <windows.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>

namespace sst::platform::win32 {

#ifndef EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE 0x320B
#endif

#ifndef EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE
#define EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE 0x33A4
#endif

#ifndef EGL_EXPERIMENTAL_PRESENT_PATH_COPY_ANGLE
#define EGL_EXPERIMENTAL_PRESENT_PATH_COPY_ANGLE 0x33AA
#endif

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

    auto envFlagEnabled = [](const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    };

    const bool forceHardware = envFlagEnabled("SST_ANGLE_HARDWARE");
    const bool useWarp =
        !forceHardware &&
        (GetSystemMetrics(SM_REMOTESESSION) != 0 ||
         envFlagEnabled("SST_ANGLE_WARP") ||
         envFlagEnabled("SST_REMOTE_COMPAT") ||
         !envFlagEnabled("SST_DISABLE_REMOTE_COMPAT"));

    auto getAngleDisplay = [&](EGLint deviceType, bool requestCopyPresent) {
        const EGLint displayAttribsWithCopy[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
            deviceType,
            EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE,
            EGL_EXPERIMENTAL_PRESENT_PATH_COPY_ANGLE,
            EGL_NONE
        };
        const EGLint displayAttribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
            deviceType,
            EGL_NONE
        };

        return eglGetPlatformDisplayEXT(
            EGL_PLATFORM_ANGLE_ANGLE,
            EGL_DEFAULT_DISPLAY,
            requestCopyPresent ? displayAttribsWithCopy : displayAttribs
        );
    };

    const EGLint preferredDevice =
        useWarp ? EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE
                : EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE;
    EGLint selectedDevice = preferredDevice;

    eglDisplay_ = getAngleDisplay(preferredDevice, true);
    bool requestedCopyPresent = true;
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        requestedCopyPresent = false;
        eglDisplay_ = getAngleDisplay(preferredDevice, false);
    }
    if (eglDisplay_ == EGL_NO_DISPLAY &&
        preferredDevice != EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE) {
        requestedCopyPresent = true;
        eglDisplay_ = getAngleDisplay(EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE, true);
        selectedDevice = EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE;
    }
    if (eglDisplay_ == EGL_NO_DISPLAY) return false;

    std::printf("[egl] ANGLE D3D11 device: %s, present path: %s\n",
                selectedDevice == EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE ? "WARP" : "hardware",
                requestedCopyPresent ? "copy" : "default");

    EGLint major, minor;
    if (!eglInitialize(static_cast<EGLDisplay>(eglDisplay_), &major, &minor))
        return false;

    // Choose config with RGBA8 + MSAA 4x for antialiasing
    const EGLint configAttribsMSAA[] = {
        EGL_RED_SIZE,       8,
        EGL_GREEN_SIZE,     8,
        EGL_BLUE_SIZE,      8,
        EGL_ALPHA_SIZE,     8,
        EGL_DEPTH_SIZE,     0,
        EGL_STENCIL_SIZE,   0,
        EGL_SAMPLE_BUFFERS, 1,
        EGL_SAMPLES,        4,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    // Fallback without MSAA in case driver doesn't support it
    const EGLint configAttribsNoMSAA[] = {
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

    // Try MSAA 4x first
    if (!eglChooseConfig(static_cast<EGLDisplay>(eglDisplay_),
                         configAttribsMSAA, &config, 1, &numConfigs)
        || numConfigs == 0) {
        // Fallback: no MSAA
        numConfigs = 0;
        if (!eglChooseConfig(static_cast<EGLDisplay>(eglDisplay_),
                             configAttribsNoMSAA, &config, 1, &numConfigs)
            || numConfigs == 0) {
            return false;
        }
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

    // Enable vsync — swapBuffers will block until next vblank
    eglSwapInterval(static_cast<EGLDisplay>(eglDisplay_), 1);

    // Log MSAA status
    EGLint msaaSamples = 0;
    eglGetConfigAttrib(static_cast<EGLDisplay>(eglDisplay_), config,
                       EGL_SAMPLES, &msaaSamples);
    std::printf("[egl] MSAA samples: %d\n", msaaSamples);

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
