// ANGLE-specific EGL extension definitions.
// These constants are defined by ANGLE but not in the standard Khronos headers.
// See: https://chromium.googlesource.com/angle/angle/+/HEAD/include/EGL/eglext_angle.h

#ifndef EGL_EGLEXT_ANGLE_H_
#define EGL_EGLEXT_ANGLE_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>

// EGL_ANGLE_platform_angle
#ifndef EGL_ANGLE_platform_angle
#define EGL_ANGLE_platform_angle 1
#define EGL_PLATFORM_ANGLE_ANGLE                            0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE                       0x3203
#define EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE          0x3204
#define EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE          0x3205
#define EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE               0x3206
#define EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE                  0x3207
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE                 0x3208
#define EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE                0x320D
#define EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE              0x320E
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE                0x3450
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE                 0x3489
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE                0x3209
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE       0x320A
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_NULL_ANGLE           0x345E
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE    0x3487
#endif

// EGL_ANGLE_device_d3d
#ifndef EGL_ANGLE_device_d3d
#define EGL_ANGLE_device_d3d 1
#define EGL_D3D9_DEVICE_ANGLE                               0x33A0
#define EGL_D3D11_DEVICE_ANGLE                              0x33A1
#endif

// EGL_ANGLE_d3d_texture_client_buffer
#ifndef EGL_ANGLE_d3d_texture_client_buffer
#define EGL_ANGLE_d3d_texture_client_buffer 1
#define EGL_D3D_TEXTURE_ANGLE                               0x33A3
#define EGL_D3D11_TEXTURE_ANGLE                             0x33A3
#define EGL_D3D_TEXTURE_SUBRESOURCE_ID_ANGLE                0x33AB
#endif

// EGL_ANGLE_platform_angle_d3d11_device
#ifndef EGL_ANGLE_platform_angle_d3d11_device
#define EGL_ANGLE_platform_angle_d3d11_device 1
#define EGL_PLATFORM_ANGLE_D3D11_DEVICE_ANGLE               0x33A1
#endif

#endif // EGL_EGLEXT_ANGLE_H_
