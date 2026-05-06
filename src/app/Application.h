#pragma once

#include <platform/PlatformFactory.h>
#include <core/ScreenshotStateMachine.h>
#include <core/AnnotationModel.h>
#include <core/CommandHistory.h>
#include <core/Types.h>
#include <core/LongScreenshotStitcher.h>
#include <renderer/SpriteBatch.h>
#include <renderer/ShapeRenderer.h>
#include <renderer/Framebuffer.h>
#include <chrono>
#include <cstdint>
#include <vector>

namespace sst::app {

/// Toolbar button definition
struct ToolButton {
    float x, y, w, h;
    enum class Type {
        Rectangle,
        Arrow,
        Line,
        Freehand,
        LongScreenshot,
        Edit,
        AutoScroll,
        Undo,
        Save,
        Copy,
        Cancel,
        Confirm
    } type;
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
#ifdef _WIN32
    bool shouldUseSoftwareOverlay() const;
    void renderSoftwareOverlay();
#endif

    // Phase 1: Capture and display
    bool captureScreen();
    bool captureFramePixels(std::vector<uint8_t>& outPixels, int& outW, int& outH,
                            uint32_t timeoutMs = 500);
    bool captureLongFramePixels(std::vector<uint8_t>& outPixels,
                                int& outW,
                                int& outH,
                                platform::Rect region);
#ifdef _WIN32
    bool captureFramePixelsGdi(std::vector<uint8_t>& outPixels,
                               int& outW,
                               int& outH,
                               platform::Rect region);
    bool captureLongFramePixelsFromCoveredWindow(std::vector<uint8_t>& outPixels,
                                                 int& outW,
                                                 int& outH,
                                                 platform::Rect region);
#endif
    bool uploadScreenshotTexture();
    bool uploadScreenshotTextureFromPixels(const std::vector<uint8_t>& pixels,
                                           int width,
                                           int height);

    // Phase 2: Region selection
    void renderDimMask(float selX, float selY, float selW, float selH);

    // Phase 3+4: Toolbar and annotations
    void buildToolbar();
    void renderToolbar();
    void renderLongScreenshotUi();
    void renderLongScreenshotToolbar();
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
    std::vector<uint8_t> cropPixels(const std::vector<uint8_t>& source,
                                    int sourceW, int sourceH,
                                    platform::Rect region) const;
    int trimTrailingCaptureDropout(std::vector<uint8_t>& pixels,
                                   int width,
                                   int height) const;
    bool captureLongScreenshot();
    std::chrono::steady_clock::time_point scheduleLongFrameCapture(
        uint64_t scrollSeq,
        std::chrono::steady_clock::time_point scrollAt,
        std::chrono::steady_clock::time_point now,
        int captureDelayMs,
        int trailingDelayMs,
        int minCaptureIntervalMs);
    void handleLongScreenshotScroll(float scrollDelta,
                                    platform::Point cursorPosition,
                                    bool nativePassthrough);
    bool appendLongScreenshotFrame();
    void finishLongScreenshotMode();
    void runPendingActions();
    void toggleLongAutoScroll();
    void stopLongAutoScroll(const char* reason);
    void runLongAutoScroll();
    platform::Point longScreenshotScrollPoint() const;
    platform::Rect fitLongPreviewRect(int imageW, int imageH) const;
    std::vector<platform::Rect> longScreenshotOverlayRegions() const;
    bool ensureLongHintTexture();
    void resetCaptureSession();

    platform::PlatformServices platform_;

    core::ScreenshotStateMachine stateMachine_;
    core::AnnotationModel        annotations_;
    core::CommandHistory         commandHistory_;

    renderer::SpriteBatch   spriteBatch_;
    renderer::ShapeRenderer shapeRenderer_;

    uint32_t screenshotTexture_ = 0;
    uint32_t longBackgroundTexture_ = 0;
    uint32_t longHintTextTexture_ = 0;
    int longHintTextW_ = 0;
    int longHintTextH_ = 0;
    platform::Size screenSize_;
    platform::Rect screenBounds_;

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
    platform::Color annotationColor_ = { 255, 92, 92, 255 };
    float annotationThickness_ = 2.0f;
    bool isDrawingAnnotation_ = false;
    float annStartX_ = 0.f, annStartY_ = 0.f;
    float annCurrX_  = 0.f, annCurrY_  = 0.f;
    std::vector<platform::PointF> activeFreehandPoints_;

    // Pixel data for save (stored after capture for reuse)
    std::vector<uint8_t> capturedPixels_;
    int capturedW_ = 0, capturedH_ = 0;
    std::vector<uint8_t> longBackgroundPixels_;
    int longBackgroundW_ = 0, longBackgroundH_ = 0;
    bool isLongScreenshotResult_ = false;
    bool isLongCaptureActive_ = false;
    bool pendingLongScreenshot_ = false;
    bool pendingLongFrameCapture_ = false;
    bool longNeedsTrailingFrameCapture_ = false;
    bool longAutoScrollActive_ = false;
    int longAutoScrollStallCount_ = 0;
    std::chrono::steady_clock::time_point longNextAutoScroll_;
    std::chrono::steady_clock::time_point longNextAutoPreviewRender_;
    std::chrono::steady_clock::time_point longFrameCaptureDue_;
    std::chrono::steady_clock::time_point longTrailingFrameCaptureDue_;
    std::chrono::steady_clock::time_point longLastFrameCapture_;
    uint64_t longScrollEventSeq_ = 0;
    uint64_t longPendingFrameScrollSeq_ = 0;
    std::chrono::steady_clock::time_point longPendingFrameScrollAt_;
    uint64_t longCurrentFrameScrollSeq_ = 0;
    std::chrono::steady_clock::time_point longCurrentFrameScrollAt_;
    uint64_t longLastAppendedScrollSeq_ = 0;
    platform::Rect longScreenshotSourceRegion_;
    core::LongScreenshotStitcher longStitcher_;

    bool running_ = false;
};

} // namespace sst::app
