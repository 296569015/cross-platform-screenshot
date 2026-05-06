add_library(core STATIC
    src/ScreenshotStateMachine.cpp
    src/AnnotationModel.cpp
    src/CommandHistory.cpp
    src/LongScreenshotStitcher.cpp
)

target_include_directories(core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(core
    PUBLIC  platform_interfaces
)
