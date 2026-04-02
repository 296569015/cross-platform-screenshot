# Platform abstraction layer
# platform_interfaces: header-only, no platform code
# platform_win32: Windows-specific implementations

add_library(platform_interfaces INTERFACE)
target_include_directories(platform_interfaces INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    add_subdirectory(win32)
endif()
