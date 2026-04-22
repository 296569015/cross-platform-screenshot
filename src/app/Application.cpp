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
#include <cstdint>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1B
#endif

namespace sst::app {

// ── Design tokens (Snipaste-style) ──────────────────────────────
static constexpr float kBtnSize    = 32.f;
static constexpr float kBtnGap     = 4.f;
static constexpr float kToolbarPad = 6.f;
static constexpr float kToolbarGap = 8.f;  // gap between toolbar and selection
static constexpr float kCornerR    = 6.f;

static const platform::Color kToolbarBg    = { 45, 45, 45, 230 };   // #2D2D2DE6
static const platform::Color kBtnHover     = { 64, 64, 64, 255 };   // #404040
static const platform::Color kBtnSelected  = { 66, 133, 244, 255 }; // #4285F4
static const platform::Color kBtnIcon      = { 220, 220, 220, 255 };// light gray icons
static const platform::Color kSelBorder    = { 66, 133, 244, 255 }; // #4285F4
static const platform::Color kDimColor     = { 0, 0, 0, 128 };

Application::Application() = default;

Application::~Application() {
    if (screenshotTexture_) {
        glDeleteTextures(1, &screenshotTexture_);
    }
    spriteBatch_.shutdown();
    shapeRenderer_.shutdown();
    if (platform_.overlay) platform_.overlay->destroy();
    if (platform_.eglContext) platform_.eglContext->shutdown();
    if (platform_.capture) platform_.capture->shutdown();
    if (platform_.systemTray) platform_.systemTray->destroy();
}

bool Application::initialize() {
    std::printf("[init] Creating platform services...\n");
    platform_ = platform::createPlatformServices();

    std::printf("[init] Initializing screen capture (D3D11)...\n");
    if (!platform_.capture->initialize(0)) {
        std::fprintf(stderr, "[init] FAILED: screen capture init\n");
        return false;
    }

    auto monitors = platform_.capture->enumerateMonitors();
    std::printf("[init] Found %zu monitor(s)\n", monitors.size());

    platform::Rect primaryBounds = { 0, 0, 1920, 1080 };
    for (const auto& m : monitors) {
        if (m.isPrimary) {
            primaryBounds = m.bounds;
            break;
        }
    }
    screenSize_ = { primaryBounds.w, primaryBounds.h };
    std::printf("[init] Primary monitor: %dx%d\n", screenSize_.w, screenSize_.h);

    std::printf("[init] Creating overlay window...\n");
    if (!platform_.overlay->create(primaryBounds)) {
        std::fprintf(stderr, "[init] FAILED: overlay window creation\n");
        return false;
    }

    std::printf("[init] Initializing EGL/ANGLE...\n");
    if (!platform_.eglContext->initialize(platform_.overlay->getNativeHandle())) {
        std::fprintf(stderr, "[init] FAILED: EGL context init\n");
        return false;
    }
    std::printf("[init] EGL/ANGLE initialized OK\n");

    platform_.capture->shutdown();
    if (!platform_.capture->initialize(0)) {
        std::fprintf(stderr, "[init] FAILED: screen capture reinit\n");
        return false;
    }

    std::printf("[init] Initializing renderers...\n");
    if (!spriteBatch_.initialize()) {
        std::fprintf(stderr, "[init] FAILED: sprite batch init\n");
        return false;
    }
    if (!shapeRenderer_.initialize()) {
        std::fprintf(stderr, "[init] FAILED: shape renderer init\n");
        return false;
    }

    spriteBatch_.setProjection(0, 0, static_cast<float>(screenSize_.w),
                               static_cast<float>(screenSize_.h));
    shapeRenderer_.setProjection(0, 0, static_cast<float>(screenSize_.w),
                                 static_cast<float>(screenSize_.h));

    platform_.overlay->setMouseCallback(
        [this](const platform::MouseEvent& evt) { onMouseEvent(evt); });
    platform_.overlay->setKeyCallback(
        [this](const platform::KeyEvent& evt) { onKeyEvent(evt); });

    platform_.input->registerHotkey(
        'A',
        static_cast<uint8_t>(platform::KeyModifier::Ctrl) |
        static_cast<uint8_t>(platform::KeyModifier::Shift),
        [this]() { onHotkeyTriggered(); }
    );

    platform_.systemTray->create("Screenshot Tool", {
        { "Take Screenshot", [this]() { onHotkeyTriggered(); } },
        { "", nullptr, true },
        { "Quit", [this]() { quit(); } },
    });

    running_ = true;
    std::printf("[init] All systems ready!\n");
    std::printf("[info] Press Ctrl+Shift+A to take a screenshot\n");
    std::printf("[info] Right-click tray icon for menu\n");
    return true;
}

int Application::run() {
    while (running_) {
        platform_.input->pollHotkeys();

        if (!platform_.overlay->pumpMessages()) {
            break;
        }

        auto state = stateMachine_.currentState();

        if (state != core::AppState::Idle) {
            // Active overlay — render at vsync rate (no manual sleep)
            render();
        } else {
            // Idle — sleep longer, only wake for messages (hotkey/tray)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}

void Application::quit() {
    running_ = false;
}

// ── Hotkey ──────────────────────────────────────────────────────
void Application::onHotkeyTriggered() {
    if (stateMachine_.currentState() != core::AppState::Idle)
        return;

    std::printf("[hotkey] Ctrl+Shift+A pressed, capturing screen...\n");
    stateMachine_.transition(core::AppEvent::HotkeyTriggered);

    if (captureScreen()) {
        std::printf("[hotkey] Capture success, showing overlay\n");
        stateMachine_.transition(core::AppEvent::FrameAcquired);
        platform_.overlay->show();
    } else {
        std::printf("[hotkey] Capture failed\n");
        stateMachine_.transition(core::AppEvent::CancelRequested);
    }
}

// ── Capture ─────────────────────────────────────────────────────
bool Application::captureScreen() {
    platform::CapturedFrame frame;
    platform::CaptureError error;

    bool captured = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (platform_.capture->acquireFrame(frame, error, 500)) {
            captured = true;
            break;
        }
        std::printf("[capture] Attempt %d failed (error=%d), retrying...\n",
                    attempt + 1, static_cast<int>(error));
    }
    if (!captured) {
        std::fprintf(stderr, "[capture] Failed to capture screen after retries\n");
        return false;
    }

    // Read frame pixels to CPU (BGRA -> RGBA)
    if (!platform_.capture->readFramePixels(capturedPixels_, capturedW_, capturedH_)) {
        std::fprintf(stderr, "[capture] Failed to read frame pixels\n");
        platform_.capture->releaseFrame();
        return false;
    }

    platform_.capture->releaseFrame();

    // Upload to GL texture
    platform_.eglContext->makeCurrent();
    if (screenshotTexture_ == 0) {
        glGenTextures(1, &screenshotTexture_);
    }
    glBindTexture(GL_TEXTURE_2D, screenshotTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, capturedW_, capturedH_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, capturedPixels_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::printf("[capture] GL texture = %u (%dx%d) via CPU readback\n",
                screenshotTexture_, capturedW_, capturedH_);
    return screenshotTexture_ != 0;
}

// ── Mouse Events ────────────────────────────────────────────────
void Application::onMouseEvent(const platform::MouseEvent& evt) {
    auto state = stateMachine_.currentState();
    float mx = static_cast<float>(evt.position.x);
    float my = static_cast<float>(evt.position.y);

    if (state == core::AppState::Selecting) {
        if (evt.type == platform::MouseEvent::Type::Press &&
            evt.button == platform::MouseButton::Left) {
            isDragging_ = true;
            dragStartX_ = mx; dragStartY_ = my;
            dragCurrX_ = mx;  dragCurrY_ = my;
        } else if (evt.type == platform::MouseEvent::Type::Move && isDragging_) {
            dragCurrX_ = mx; dragCurrY_ = my;
        } else if (evt.type == platform::MouseEvent::Type::Release &&
                   evt.button == platform::MouseButton::Left && isDragging_) {
            isDragging_ = false;
            dragCurrX_ = mx; dragCurrY_ = my;

            float x0 = std::min(dragStartX_, dragCurrX_);
            float y0 = std::min(dragStartY_, dragCurrY_);
            float x1 = std::max(dragStartX_, dragCurrX_);
            float y1 = std::max(dragStartY_, dragCurrY_);

            if ((x1 - x0) > 5.f && (y1 - y0) > 5.f) {
                stateMachine_.setSelectedRegion({
                    static_cast<int>(x0), static_cast<int>(y0),
                    static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)
                });
                stateMachine_.transition(core::AppEvent::MouseUp);
                buildToolbar();
                std::printf("[select] Region: %d,%d %dx%d\n",
                            static_cast<int>(x0), static_cast<int>(y0),
                            static_cast<int>(x1 - x0), static_cast<int>(y1 - y0));
            }
        } else if (evt.type == platform::MouseEvent::Type::Press &&
                   evt.button == platform::MouseButton::Right) {
            stateMachine_.transition(core::AppEvent::Escape);
            platform_.overlay->hide();
            isDragging_ = false;
        }
    } else if (state == core::AppState::Annotating) {
        // Update hover state on toolbar buttons
        for (auto& btn : toolButtons_) {
            btn.isHovered = (mx >= btn.x && mx <= btn.x + btn.w &&
                             my >= btn.y && my <= btn.y + btn.h);
        }

        if (evt.type == platform::MouseEvent::Type::Press &&
            evt.button == platform::MouseButton::Left) {
            // Check toolbar first
            if (hitTestToolbar(mx, my)) {
                // handled in hitTestToolbar
            } else if (activeTool_ != core::AnnotationTool::None) {
                // Start drawing annotation
                startAnnotation(mx, my);
            }
        } else if (evt.type == platform::MouseEvent::Type::Move && isDrawingAnnotation_) {
            updateAnnotation(mx, my);
        } else if (evt.type == platform::MouseEvent::Type::Release &&
                   evt.button == platform::MouseButton::Left && isDrawingAnnotation_) {
            finishAnnotation(mx, my);
        } else if (evt.type == platform::MouseEvent::Type::Press &&
                   evt.button == platform::MouseButton::Right) {
            stateMachine_.transition(core::AppEvent::Escape);
            platform_.overlay->hide();
            annotations_.clear();
            commandHistory_.clear();
            isDrawingAnnotation_ = false;
        }
    }
}

// ── Key Events ──────────────────────────────────────────────────
void Application::onKeyEvent(const platform::KeyEvent& evt) {
    if (evt.type != platform::KeyEvent::Type::Press) return;

    if (evt.keyCode == VK_ESCAPE) {
        stateMachine_.transition(core::AppEvent::Escape);
        platform_.overlay->hide();
        isDragging_ = false;
        isDrawingAnnotation_ = false;
        annotations_.clear();
        commandHistory_.clear();
        std::printf("[key] Escape pressed, returning to idle\n");
    }

    // Ctrl+Z = undo
    if (evt.keyCode == 'Z' && (evt.modifiers & static_cast<uint8_t>(platform::KeyModifier::Ctrl))) {
        if (commandHistory_.canUndo()) {
            commandHistory_.undo();
            std::printf("[key] Undo\n");
        }
    }

    // Ctrl+S = save to file
    if (evt.keyCode == 'S' && (evt.modifiers & static_cast<uint8_t>(platform::KeyModifier::Ctrl))) {
        if (stateMachine_.currentState() == core::AppState::Annotating) {
            saveToFile();
        }
    }

    // Ctrl+C = copy to clipboard
    if (evt.keyCode == 'C' && (evt.modifiers & static_cast<uint8_t>(platform::KeyModifier::Ctrl))) {
        if (stateMachine_.currentState() == core::AppState::Annotating) {
            saveToClipboard();
        }
    }
}

// ── Toolbar ─────────────────────────────────────────────────────
void Application::buildToolbar() {
    toolButtons_.clear();
    auto sel = stateMachine_.selectedRegion();

    // 7 buttons: [Rect] [Arrow] [Line] | [Undo] [Save] [Copy] [Cancel]
    int numButtons = 7;
    float totalW = kToolbarPad * 2 + numButtons * kBtnSize + (numButtons - 1) * kBtnGap;
    float totalH = kToolbarPad * 2 + kBtnSize;

    // Position: below selection, right-aligned
    float tx = static_cast<float>(sel.x + sel.w) - totalW;
    float ty = static_cast<float>(sel.y + sel.h) + kToolbarGap;

    // Clamp to screen
    float sw = static_cast<float>(screenSize_.w);
    float sh = static_cast<float>(screenSize_.h);
    if (tx < 0.f) tx = 0.f;
    if (tx + totalW > sw) tx = sw - totalW;
    if (ty + totalH > sh) {
        // Place above selection instead
        ty = static_cast<float>(sel.y) - kToolbarGap - totalH;
        if (ty < 0.f) ty = 0.f;
    }

    toolbarX_ = tx; toolbarY_ = ty;
    toolbarW_ = totalW; toolbarH_ = totalH;

    // Create buttons
    float bx = tx + kToolbarPad;
    float by = ty + kToolbarPad;

    auto addBtn = [&](ToolButton::Type type) {
        toolButtons_.push_back({ bx, by, kBtnSize, kBtnSize, type, false });
        bx += kBtnSize + kBtnGap;
    };

    addBtn(ToolButton::Type::Rectangle);
    addBtn(ToolButton::Type::Arrow);
    addBtn(ToolButton::Type::Line);
    addBtn(ToolButton::Type::Undo);
    addBtn(ToolButton::Type::Save);
    addBtn(ToolButton::Type::Copy);
    addBtn(ToolButton::Type::Cancel);
}

bool Application::hitTestToolbar(float mx, float my) {
    for (auto& btn : toolButtons_) {
        if (mx >= btn.x && mx <= btn.x + btn.w &&
            my >= btn.y && my <= btn.y + btn.h) {
            onToolbarClick(btn.type);
            return true;
        }
    }
    return false;
}

void Application::onToolbarClick(ToolButton::Type type) {
    switch (type) {
    case ToolButton::Type::Rectangle:
        activeTool_ = core::AnnotationTool::Rectangle;
        std::printf("[toolbar] Tool: Rectangle\n");
        break;
    case ToolButton::Type::Arrow:
        activeTool_ = core::AnnotationTool::Arrow;
        std::printf("[toolbar] Tool: Arrow\n");
        break;
    case ToolButton::Type::Line:
        activeTool_ = core::AnnotationTool::Line;
        std::printf("[toolbar] Tool: Line\n");
        break;
    case ToolButton::Type::Undo:
        if (commandHistory_.canUndo()) {
            commandHistory_.undo();
            std::printf("[toolbar] Undo\n");
        }
        break;
    case ToolButton::Type::Save:
        saveToFile();
        break;
    case ToolButton::Type::Copy:
        saveToClipboard();
        break;
    case ToolButton::Type::Cancel:
        stateMachine_.transition(core::AppEvent::Escape);
        platform_.overlay->hide();
        annotations_.clear();
        commandHistory_.clear();
        std::printf("[toolbar] Cancel\n");
        break;
    }
}

// ── Annotation Drawing ──────────────────────────────────────────
void Application::startAnnotation(float x, float y) {
    isDrawingAnnotation_ = true;
    annStartX_ = x; annStartY_ = y;
    annCurrX_ = x;  annCurrY_ = y;
}

void Application::updateAnnotation(float x, float y) {
    annCurrX_ = x; annCurrY_ = y;
}

void Application::finishAnnotation(float x, float y) {
    isDrawingAnnotation_ = false;
    annCurrX_ = x; annCurrY_ = y;

    float dx = annCurrX_ - annStartX_;
    float dy = annCurrY_ - annStartY_;
    if (std::abs(dx) < 3.f && std::abs(dy) < 3.f) return; // too small

    core::Annotation ann;

    switch (activeTool_) {
    case core::AnnotationTool::Rectangle: {
        float rx = std::min(annStartX_, annCurrX_);
        float ry = std::min(annStartY_, annCurrY_);
        float rw = std::abs(dx);
        float rh = std::abs(dy);
        ann = core::RectAnnotation{
            { rx, ry, rw, rh }, annotationColor_, annotationThickness_, false
        };
        break;
    }
    case core::AnnotationTool::Arrow:
        ann = core::ArrowAnnotation{
            { annStartX_, annStartY_ }, { annCurrX_, annCurrY_ },
            annotationColor_, annotationThickness_, 12.0f
        };
        break;
    case core::AnnotationTool::Line:
        ann = core::LineAnnotation{
            { annStartX_, annStartY_ }, { annCurrX_, annCurrY_ },
            annotationColor_, annotationThickness_
        };
        break;
    default:
        return;
    }

    // Use command history for undo support
    size_t idx = annotations_.count();
    commandHistory_.execute(
        ann,
        [this](core::Annotation a) { annotations_.addAnnotation(std::move(a)); },
        [this, idx]() { annotations_.removeAnnotation(idx); }
    );

    std::printf("[annotation] Added %s\n",
                activeTool_ == core::AnnotationTool::Rectangle ? "rectangle" :
                activeTool_ == core::AnnotationTool::Arrow ? "arrow" : "line");
}

// ── Render ──────────────────────────────────────────────────────
void Application::render() {
    platform_.eglContext->makeCurrent();
    auto size = platform_.eglContext->getSurfaceSize();

    glViewport(0, 0, size.w, size.h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (screenshotTexture_) {
        spriteBatch_.drawFullscreen(screenshotTexture_);
    }

    auto state = stateMachine_.currentState();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (state == core::AppState::Selecting) {
        if (isDragging_) {
            float x0 = std::min(dragStartX_, dragCurrX_);
            float y0 = std::min(dragStartY_, dragCurrY_);
            float x1 = std::max(dragStartX_, dragCurrX_);
            float y1 = std::max(dragStartY_, dragCurrY_);

            renderDimMask(x0, y0, x1 - x0, y1 - y0);
            shapeRenderer_.drawRectOutline(x0, y0, x1 - x0, y1 - y0, kSelBorder, 2.0f);
        } else {
            renderDimMask(0.f, 0.f, 0.f, 0.f);
        }
    } else if (state == core::AppState::Annotating) {
        auto sel = stateMachine_.selectedRegion();
        float sx = static_cast<float>(sel.x);
        float sy = static_cast<float>(sel.y);
        float ssw = static_cast<float>(sel.w);
        float ssh = static_cast<float>(sel.h);

        renderDimMask(sx, sy, ssw, ssh);
        shapeRenderer_.drawRectOutline(sx, sy, ssw, ssh, kSelBorder, 2.0f);

        // Draw committed annotations
        renderAnnotations();

        // Draw in-progress annotation (preview while dragging)
        if (isDrawingAnnotation_) {
            switch (activeTool_) {
            case core::AnnotationTool::Rectangle: {
                float rx = std::min(annStartX_, annCurrX_);
                float ry = std::min(annStartY_, annCurrY_);
                float rw = std::abs(annCurrX_ - annStartX_);
                float rh = std::abs(annCurrY_ - annStartY_);
                shapeRenderer_.drawRectOutline(rx, ry, rw, rh,
                                               annotationColor_, annotationThickness_);
                break;
            }
            case core::AnnotationTool::Arrow:
                shapeRenderer_.drawArrow(annStartX_, annStartY_, annCurrX_, annCurrY_,
                                          annotationColor_, annotationThickness_);
                break;
            case core::AnnotationTool::Line:
                shapeRenderer_.drawLine(annStartX_, annStartY_, annCurrX_, annCurrY_,
                                         annotationColor_, annotationThickness_);
                break;
            default: break;
            }
        }

        // Draw toolbar
        renderToolbar();
    }

    glDisable(GL_BLEND);
    platform_.eglContext->swapBuffers();
}

void Application::renderAnnotations() {
    for (const auto& ann : annotations_.annotations()) {
        std::visit([this](const auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, core::RectAnnotation>) {
                if (a.filled) {
                    shapeRenderer_.drawRectFilled(a.bounds.x, a.bounds.y,
                                                  a.bounds.w, a.bounds.h, a.color);
                } else {
                    shapeRenderer_.drawRectOutline(a.bounds.x, a.bounds.y,
                                                   a.bounds.w, a.bounds.h,
                                                   a.color, a.thickness);
                }
            } else if constexpr (std::is_same_v<T, core::ArrowAnnotation>) {
                shapeRenderer_.drawArrow(a.start.x, a.start.y,
                                          a.end.x, a.end.y,
                                          a.color, a.thickness, a.headSize);
            } else if constexpr (std::is_same_v<T, core::LineAnnotation>) {
                shapeRenderer_.drawLine(a.start.x, a.start.y,
                                         a.end.x, a.end.y,
                                         a.color, a.thickness);
            } else if constexpr (std::is_same_v<T, core::TextAnnotation>) {
                // TODO: SDF text rendering
            }
        }, ann);
    }
}

void Application::renderToolbar() {
    // Toolbar background (rounded rect approximated as filled rect)
    shapeRenderer_.drawRectFilled(toolbarX_, toolbarY_, toolbarW_, toolbarH_, kToolbarBg);

    for (const auto& btn : toolButtons_) {
        // Button background
        bool isSelected = false;
        if (btn.type == ToolButton::Type::Rectangle &&
            activeTool_ == core::AnnotationTool::Rectangle) isSelected = true;
        if (btn.type == ToolButton::Type::Arrow &&
            activeTool_ == core::AnnotationTool::Arrow) isSelected = true;
        if (btn.type == ToolButton::Type::Line &&
            activeTool_ == core::AnnotationTool::Line) isSelected = true;

        if (isSelected) {
            shapeRenderer_.drawRectFilled(btn.x, btn.y, btn.w, btn.h, kBtnSelected);
        } else if (btn.isHovered) {
            shapeRenderer_.drawRectFilled(btn.x, btn.y, btn.w, btn.h, kBtnHover);
        }

        // Draw button icons using simple geometric shapes
        float cx = btn.x + btn.w * 0.5f;
        float cy = btn.y + btn.h * 0.5f;
        float iconPad = 8.f;

        switch (btn.type) {
        case ToolButton::Type::Rectangle:
            // Small rectangle outline icon
            shapeRenderer_.drawRectOutline(btn.x + iconPad, btn.y + iconPad,
                                           btn.w - iconPad * 2, btn.h - iconPad * 2,
                                           kBtnIcon, 2.0f);
            break;
        case ToolButton::Type::Arrow:
            // Arrow icon pointing right-down
            shapeRenderer_.drawArrow(btn.x + iconPad, btn.y + iconPad,
                                      btn.x + btn.w - iconPad, btn.y + btn.h - iconPad,
                                      kBtnIcon, 2.0f, 8.0f);
            break;
        case ToolButton::Type::Line:
            // Diagonal line icon
            shapeRenderer_.drawLine(btn.x + iconPad, btn.y + btn.h - iconPad,
                                     btn.x + btn.w - iconPad, btn.y + iconPad,
                                     kBtnIcon, 2.0f);
            break;
        case ToolButton::Type::Undo:
            // Curved arrow (approximate with < shape)
            shapeRenderer_.drawLine(cx, btn.y + iconPad, btn.x + iconPad, cy, kBtnIcon, 2.0f);
            shapeRenderer_.drawLine(btn.x + iconPad, cy, cx, btn.y + btn.h - iconPad, kBtnIcon, 2.0f);
            break;
        case ToolButton::Type::Save:
            // Floppy disk icon (rectangle + inner rect)
            shapeRenderer_.drawRectOutline(btn.x + iconPad, btn.y + iconPad,
                                           btn.w - iconPad * 2, btn.h - iconPad * 2,
                                           kBtnIcon, 2.0f);
            shapeRenderer_.drawRectFilled(cx - 4, btn.y + btn.h - iconPad - 6,
                                           8, 6, kBtnIcon);
            break;
        case ToolButton::Type::Copy:
            // Two overlapping rectangles icon
            shapeRenderer_.drawRectOutline(btn.x + iconPad, btn.y + iconPad,
                                           btn.w - iconPad * 2 - 4, btn.h - iconPad * 2 - 4,
                                           kBtnIcon, 1.5f);
            shapeRenderer_.drawRectOutline(btn.x + iconPad + 4, btn.y + iconPad + 4,
                                           btn.w - iconPad * 2 - 4, btn.h - iconPad * 2 - 4,
                                           kBtnIcon, 1.5f);
            break;
        case ToolButton::Type::Cancel:
            // X icon
            shapeRenderer_.drawLine(btn.x + iconPad, btn.y + iconPad,
                                     btn.x + btn.w - iconPad, btn.y + btn.h - iconPad,
                                     kBtnIcon, 2.0f);
            shapeRenderer_.drawLine(btn.x + btn.w - iconPad, btn.y + iconPad,
                                     btn.x + iconPad, btn.y + btn.h - iconPad,
                                     kBtnIcon, 2.0f);
            break;
        }
    }
}

void Application::renderDimMask(float selX, float selY, float selW, float selH) {
    float sw = static_cast<float>(screenSize_.w);
    float sh = static_cast<float>(screenSize_.h);

    if (selW <= 0.f || selH <= 0.f) {
        shapeRenderer_.drawRectFilled(0.f, 0.f, sw, sh, kDimColor);
        return;
    }

    shapeRenderer_.drawRectFilled(0.f, 0.f, sw, selY, kDimColor);
    shapeRenderer_.drawRectFilled(0.f, selY + selH, sw, sh - selY - selH, kDimColor);
    shapeRenderer_.drawRectFilled(0.f, selY, selX, selH, kDimColor);
    shapeRenderer_.drawRectFilled(selX + selW, selY, sw - selX - selW, selH, kDimColor);
}

// ── Save / Export ───────────────────────────────────────────────
std::vector<uint8_t> Application::renderSelectionToPixels() {
    auto sel = stateMachine_.selectedRegion();
    int sx = sel.x, sy = sel.y, sw = sel.w, sh = sel.h;

    // Clamp to captured image bounds
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx + sw > capturedW_) sw = capturedW_ - sx;
    if (sy + sh > capturedH_) sh = capturedH_ - sy;

    std::vector<uint8_t> result(static_cast<size_t>(sw) * sh * 4);

    // Extract selection region from captured pixels
    for (int y = 0; y < sh; ++y) {
        const uint8_t* srcRow = capturedPixels_.data() +
                                (static_cast<size_t>(sy + y) * capturedW_ + sx) * 4;
        uint8_t* dstRow = result.data() + static_cast<size_t>(y) * sw * 4;
        std::memcpy(dstRow, srcRow, static_cast<size_t>(sw) * 4);
    }

    // Draw annotations onto the pixel buffer
    // For MVP, we render annotations by rendering to FBO and reading back.
    // But that's complex — for now, return just the screenshot region.
    // Annotations will be rendered in a future phase when FBO readback is wired.

    return result;
}

bool Application::saveToClipboard() {
    auto sel = stateMachine_.selectedRegion();
    auto pixels = renderSelectionToPixels();

    if (pixels.empty()) {
        std::fprintf(stderr, "[save] No pixels to copy\n");
        return false;
    }

    if (!platform_.clipboard->writeImage(pixels.data(), sel.w, sel.h)) {
        std::fprintf(stderr, "[save] Clipboard write failed\n");
        return false;
    }

    std::printf("[save] Copied to clipboard (%dx%d)\n", sel.w, sel.h);

    // After copy, hide overlay and return to idle
    stateMachine_.transition(core::AppEvent::Escape);
    platform_.overlay->hide();
    annotations_.clear();
    commandHistory_.clear();
    return true;
}

bool Application::saveToFile() {
    auto path = platform_.fileDialog->showSave("screenshot.png",
                                                "PNG Files (*.png)\0*.png\0All Files (*.*)\0*.*\0");
    if (path.empty()) {
        std::printf("[save] File dialog cancelled\n");
        return false;
    }

    auto sel = stateMachine_.selectedRegion();
    auto pixels = renderSelectionToPixels();

    if (pixels.empty()) {
        std::fprintf(stderr, "[save] No pixels to save\n");
        return false;
    }

    int ok = stbi_write_png(path.c_str(), sel.w, sel.h, 4,
                             pixels.data(), sel.w * 4);
    if (!ok) {
        std::fprintf(stderr, "[save] Failed to write PNG: %s\n", path.c_str());
        return false;
    }

    std::printf("[save] Saved to: %s (%dx%d)\n", path.c_str(), sel.w, sel.h);

    // After save, hide overlay and return to idle
    stateMachine_.transition(core::AppEvent::Escape);
    platform_.overlay->hide();
    annotations_.clear();
    commandHistory_.clear();
    return true;
}

} // namespace sst::app
