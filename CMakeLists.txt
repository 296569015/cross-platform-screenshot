cmake_minimum_required(VERSION 3.25)
project(CrossPlatformScreenshot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ──────────────────────────────────────────────────────────
option(SST_BUILD_TESTS "Build unit tests" ON)

# ── ANGLE (OpenGL ES via EGL) ────────────────────────────────────────
# Look for pre-built ANGLE first, then vcpkg
set(ANGLE_DIR "${CMAKE_SOURCE_DIR}/third_party/angle" CACHE PATH "Path to pre-built ANGLE")

if(EXISTS "${ANGLE_DIR}/include/EGL/egl.h")
    message(STATUS "Using pre-built ANGLE from: ${ANGLE_DIR}")
    add_library(angle_egl SHARED IMPORTED)
    set_target_properties(angle_egl PROPERTIES
        IMPORTED_LOCATION "${ANGLE_DIR}/bin/libEGL.dll"
        IMPORTED_IMPLIB   "${ANGLE_DIR}/lib/libEGL.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${ANGLE_DIR}/include"
    )
    add_library(angle_glesv2 SHARED IMPORTED)
    set_target_properties(angle_glesv2 PROPERTIES
        IMPORTED_LOCATION "${ANGLE_DIR}/bin/libGLESv2.dll"
        IMPORTED_IMPLIB   "${ANGLE_DIR}/lib/libGLESv2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${ANGLE_DIR}/include"
    )
else()
    # Try vcpkg
    find_package(unofficial-angle CONFIG QUIET)
    if(unofficial-angle_FOUND)
        message(STATUS "Using ANGLE from vcpkg")
        add_library(angle_egl ALIAS unofficial::angle::libEGL)
        add_library(angle_glesv2 ALIAS unofficial::angle::libGLESv2)
    else()
        message(FATAL_ERROR
            "ANGLE not found! Either:\n"
            "  1. Place pre-built ANGLE in third_party/angle/ (include/ lib/ bin/)\n"
            "  2. Install via vcpkg: vcpkg install angle\n"
        )
    endif()
endif()

# ── Subdirectories ───────────────────────────────────────────────────
add_subdirectory(third_party/stb)
add_subdirectory(src)

if(SST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
