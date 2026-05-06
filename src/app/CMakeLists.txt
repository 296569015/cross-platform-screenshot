add_executable(screenshot WIN32
    main.cpp
    Application.cpp
)

target_link_libraries(screenshot PRIVATE
    core
    renderer
    platform_win32
    angle_egl
    angle_glesv2
    stb
    gdi32
)

target_compile_definitions(screenshot PRIVATE
    SST_LOG_DIR="${CMAKE_SOURCE_DIR}/logs"
)

# Embed the DPI-awareness manifest
if(MSVC)
    set_target_properties(screenshot PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
    )
endif()

# Copy ANGLE DLLs to output directory
if(EXISTS "${ANGLE_DIR}/bin/libEGL.dll")
    add_custom_command(TARGET screenshot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANGLE_DIR}/bin/libEGL.dll"
            "${ANGLE_DIR}/bin/libGLESv2.dll"
            $<TARGET_FILE_DIR:screenshot>
        COMMENT "Copying ANGLE DLLs"
    )
endif()
