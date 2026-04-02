#pragma once

#include <platform/PlatformFactory.h>
#include <core/ScreenshotStateMachine.h>
#include <core/AnnotationModel.h>
#include <core/CommandHistory.h>
#include <renderer/SpriteBatch.h>
#include <renderer/ShapeRenderer.h>
#include <renderer/Framebuffer.h>
#include <cstdint>

namespace sst::app {

/// Main application class. Owns all subsystems and runs the main loop.
class Application {
public:
    Application();
    ~Application();

    /// Initialize all subsystems. Returns false on failure.
    bool initialize();

    /// Run the main loop (blocking). Returns exit code.
    int run();

    /// Request application exit.
    void quit();

private:
    void onHotkeyTriggered();
    void onMouseEvent(const platform::MouseEvent& evt);
    void onKeyEvent(const platform::KeyEvent& evt);
    void render();

    // Phase 1: Capture and display
    bool captureScreen();
    void renderScreenshot();

    platform::PlatformServices platform_;

    core::ScreenshotStateMachine stateMachine_;
    core::AnnotationModel        annotations_;
    core::CommandHistory         commandHistory_;

    renderer::SpriteBatch  spriteBatch_;
    renderer::ShapeRenderer shapeRenderer_;

    uint32_t screenshotTexture_ = 0; // GL texture from captured frame
    platform::Size screenSize_;

    bool running_ = false;
};

} // namespace sst::app
