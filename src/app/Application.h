#pragma once

#include <platform/PlatformFactory.h>
#include <core/ScreenshotStateMachine.h>
#include <core/AnnotationModel.h>
#include <core/CommandHistory.h>
#include <core/Types.h>
#include <renderer/SpriteBatch.h>
#include <renderer/ShapeRenderer.h>
#include <renderer/Framebuffer.h>
#include <cstdint>

namespace sst::app {

/// Toolbar button definition
struct ToolButton {
    float x, y, w, h;
    enum class Type { Rectangle, Arrow, Line, Undo, Save, Copy, Cancel } type;
    bool isHovered = false;
};

/// Main application class. Owns all subsystems and runs the main loop.
class Application {
public:
    Application();
    ~Application();

    bool initialize();
    int run();
    void quit();

private:
    void onHotkeyTriggered();
    void onMouseEvent(const platform::MouseEvent& evt);
    void onKeyEvent(const platform::KeyEvent& evt);
    void render();

    // Phase 1: Capture and display
    bool captureScreen();

    // Phase 2: Region selection
    void renderDimMask(float selX, float selY, float selW, float selH);

    // Phase 3+4: Toolbar and annotations
    void buildToolbar();
    void renderToolbar();
    void renderAnnotations();
    bool hitTestToolbar(float mx, float my);
    void onToolbarClick(ToolButton::Type type);

    // Annotation drawing
    void startAnnotation(float x, float y);
    void updateAnnotation(float x, float y);
    void finishAnnotation(float x, float y);

    // Save/Export
    bool saveToClipboard();
    bool saveToFile();
    std::vector<uint8_t> renderSelectionToPixels();

    platform::PlatformServices platform_;

    core::ScreenshotStateMachine stateMachine_;
    core::AnnotationModel        annotations_;
    core::CommandHistory         commandHistory_;

    renderer::SpriteBatch   spriteBatch_;
    renderer::ShapeRenderer shapeRenderer_;

    uint32_t screenshotTexture_ = 0;
    platform::Size screenSize_;

    // Selection drag state
    bool   isDragging_  = false;
    float  dragStartX_  = 0.f;
    float  dragStartY_  = 0.f;
    float  dragCurrX_   = 0.f;
    float  dragCurrY_   = 0.f;

    // Toolbar state
    std::vector<ToolButton> toolButtons_;
    float toolbarX_ = 0.f, toolbarY_ = 0.f;
    float toolbarW_ = 0.f, toolbarH_ = 0.f;

    // Annotation drawing state
    core::AnnotationTool activeTool_ = core::AnnotationTool::Rectangle;
    platform::Color annotationColor_ = { 255, 68, 68, 255 }; // #FF4444
    float annotationThickness_ = 2.0f;
    bool isDrawingAnnotation_ = false;
    float annStartX_ = 0.f, annStartY_ = 0.f;
    float annCurrX_  = 0.f, annCurrY_  = 0.f;

    // Pixel data for save (stored after capture for reuse)
    std::vector<uint8_t> capturedPixels_;
    int capturedW_ = 0, capturedH_ = 0;

    bool running_ = false;
};

} // namespace sst::app
