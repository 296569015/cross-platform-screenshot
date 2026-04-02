add_library(platform_win32 STATIC
    Win32EglContext.cpp
    Win32Capture.cpp
    Win32Overlay.cpp
    Win32Clipboard.cpp
    Win32Input.cpp
    Win32FileDialog.cpp
    Win32SystemTray.cpp
    Win32WindowEnumerator.cpp
    Win32PlatformFactory.cpp
)

target_include_directories(platform_win32
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(platform_win32
    PUBLIC  platform_interfaces
    PRIVATE angle_egl angle_glesv2
            d3d11 dxgi dwmapi shcore ole32 uuid comdlg32 shell32
)

# Windows-specific compile definitions
target_compile_definitions(platform_win32 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
