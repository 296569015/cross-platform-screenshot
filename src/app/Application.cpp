#include "Application.h"

#include <platform/IPlatformCapture.h>
#include <platform/IPlatformOverlay.h>
#include <platform/IClipboard.h>
#include <platform/IInput.h>
#include <platform/IFileDialog.h>
#include <platform/IWindowEnumerator.h>
#include <platform/ISystemTray.h>
#include <platform/EglContext.h>

#include <GLES3/gl3.h>
#include <cstdio>
#include <thread>
#include <chrono>

#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1B
#endif

namespace sst::app {

Application::Application() = default;

Application::~Application() {
    if (screenshotTexture_) {
        platform_.eglContext->releaseImportedTexture(screenshotTexture_);
    }
    spriteBatch_.shutdown();
    shapeRenderer_.shutdown();
    platform_.overlay->destroy();
    platform_.eglContext->shutdown();
    platform_.capture->shutdown();
    platform_.systemTray->destroy();
}

bool Application::initialize() {
    // Create all platform services
    platform_ = platform::createPlatformServices();

    // Get primary monitor bounds
    auto monitors = platform_.capture->enumerateMonitors();
    if (monitors.empty()) {
        // Fallback: initialize capture first to get monitor info
        if (!platform_.capture->initialize(0)) {
            std::fprintf(stderr, "Failed to initialize screen capture\n");
            return false;
        }
        monitors = platform_.capture->enumerateMonitors();
    }

    platform::Rect primaryBounds = { 0, 0, 1920, 1080 }; // fallback
    for (const auto& m : monitors) {
        if (m.isPrimary) {
            primaryBounds = m.bounds;
            break;
        }
    }
    screenSize_ = { primaryBounds.w, primaryBounds.h };

    // Create the overlay window (initially hidden)
    if (!platform_.overlay->create(primaryBounds)) {
        std::fprintf(stderr, "Failed to create overlay window\n");
        return false;
    }

    // Initialize EGL/ANGLE on the overlay window
    if (!platform_.eglContext->initialize(platform_.overlay->getNativeHandle())) {
        std::fprintf(stderr, "Failed to initialize EGL context\n");
        return false;
    }

    // Initialize capture with ANGLE's D3D11 device for zero-copy sharing
    auto* d3dDevice = platform_.eglContext->getNativeDevice();
    // If capture wasn't initialized yet with the shared device, reinitialize
    platform_.capture->shutdown();
    // TODO: pass d3dDevice to capture for zero-copy. For now, capture uses its own.
    if (!platform_.capture->initialize(0)) {
        std::fprintf(stderr, "Failed to initialize screen capture\n");
        return false;
    }

    // Initialize renderers
    if (!spriteBatch_.initialize()) {
        std::fprintf(stderr, "Failed to initialize sprite batch\n");
        return false;
    }
    if (!shapeRenderer_.initialize()) {
        std::fprintf(stderr, "Failed to initialize shape renderer\n");
        return false;
    }

    // Set up orthographic projection
    spriteBatch_.setProjection(0, 0, static_cast<float>(screenSize_.w),
                               static_cast<float>(screenSize_.h));
    shapeRenderer_.setProjection(0, 0, static_cast<float>(screenSize_.w),
                                 static_cast<float>(screenSize_.h));

    // Register input callbacks
    platform_.overlay->setMouseCallback(
        [this](const platform::MouseEvent& evt) { onMouseEvent(evt); });
    platform_.overlay->setKeyCallback(
        [this](const platform::KeyEvent& evt) { onKeyEvent(evt); });

    // Register global hotkey: Ctrl+Shift+A
    platform_.input->registerHotkey(
        'A',
        static_cast<uint8_t>(platform::KeyModifier::Ctrl) |
        static_cast<uint8_t>(platform::KeyModifier::Shift),
        [this]() { onHotkeyTriggered(); }
    );

    // Set up system tray
    platform_.systemTray->create("Screenshot Tool", {
        { "Take Screenshot", [this]() { onHotkeyTriggered(); } },
        { "", nullptr, true }, // separator
        { "Quit", [this]() { quit(); } },
    });

    running_ = true;
    return true;
}

int Application::run() {
    while (running_) {
        // Process OS messages
        if (!platform_.overlay->pumpMessages()) {
            break;
        }

        // Poll for global hotkeys
        platform_.input->pollHotkeys();

        // Render if overlay is visible
        if (stateMachine_.currentState() != core::AppState::Idle) {
            render();
        }

        // Sleep briefly to avoid spinning (we're not a game)
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
    }

    return 0;
}

void Application::quit() {
    running_ = false;
}

void Application::onHotkeyTriggered() {
    if (stateMachine_.currentState() != core::AppState::Idle)
        return;

    stateMachine_.transition(core::AppEvent::HotkeyTriggered);

    // Capture the screen
    if (captureScreen()) {
        stateMachine_.transition(core::AppEvent::FrameAcquired);
        platform_.overlay->show();
    } else {
        stateMachine_.transition(core::AppEvent::CancelRequested);
    }
}

bool Application::captureScreen() {
    platform::CapturedFrame frame;
    platform::CaptureError error;

    if (!platform_.capture->acquireFrame(frame, error, 1000)) {
        std::fprintf(stderr, "Failed to capture screen frame\n");
        return false;
    }

    // Import the captured texture into GL (zero-copy)
    if (screenshotTexture_) {
        platform_.eglContext->releaseImportedTexture(screenshotTexture_);
        screenshotTexture_ = 0;
    }

    screenshotTexture_ = platform_.eglContext->importNativeTexture(
        frame.nativeTextureHandle, frame.size);

    platform_.capture->releaseFrame();

    return screenshotTexture_ != 0;
}

void Application::onMouseEvent(const platform::MouseEvent& evt) {
    auto state = stateMachine_.currentState();

    if (state == core::AppState::Selecting) {
        // TODO: RegionSelector handles drag logic
        if (evt.type == platform::MouseEvent::Type::Release) {
            stateMachine_.transition(core::AppEvent::MouseUp);
        }
    } else if (state == core::AppState::Annotating) {
        // TODO: handle annotation drawing
    }
}

void Application::onKeyEvent(const platform::KeyEvent& evt) {
    if (evt.type != platform::KeyEvent::Type::Press) return;

    // Escape cancels at any stage
    if (evt.keyCode == VK_ESCAPE) {
        stateMachine_.transition(core::AppEvent::Escape);
        platform_.overlay->hide();
        annotations_.clear();
        commandHistory_.clear();
    }
}

void Application::render() {
    platform_.eglContext->makeCurrent();
    auto size = platform_.eglContext->getSurfaceSize();

    glViewport(0, 0, size.w, size.h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw the captured screenshot as a fullscreen quad
    if (screenshotTexture_) {
        spriteBatch_.drawFullscreen(screenshotTexture_);
    }

    // TODO: Phase 2 - draw dimming overlay + selection rectangle
    // TODO: Phase 3 - draw annotations
    // TODO: Phase 4 - draw toolbar UI

    platform_.eglContext->swapBuffers();
}

} // namespace sst::app
