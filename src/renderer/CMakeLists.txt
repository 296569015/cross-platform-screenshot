add_library(renderer STATIC
    src/ShaderProgram.cpp
    src/SpriteBatch.cpp
    src/ShapeRenderer.cpp
    src/Framebuffer.cpp
)

target_include_directories(renderer
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(renderer
    PUBLIC  platform_interfaces
    PRIVATE angle_glesv2
            stb
)
