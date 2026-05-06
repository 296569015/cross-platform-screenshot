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
#include <cstring>
#include <cwchar>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <array>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

#ifndef SST_LOG_DIR
#define SST_LOG_DIR "logs"
#endif

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
static const platform::Color kLongBorder   = { 255, 80, 92, 255 };
static const platform::Color kDimColor     = { 0, 0, 0, 128 };
static const platform::Color kLongDimColor = { 0, 0, 0, 138 };
static const platform::Color kLongPanel    = { 246, 247, 249, 245 };
static const platform::Color kLongToolbar  = { 255, 255, 255, 248 };
static const platform::Color kLongHintBg   = { 98, 98, 98, 196 };
static const platform::Color kLongCancel   = { 255, 80, 92, 255 };
static const platform::Color kLongConfirm  = { 22, 185, 111, 255 };

static constexpr int kLongMaxFrames = 18;
static constexpr int kLongScrollNotches = 3;
static constexpr int kLongMaxOutputHeight = 16000;
static constexpr int kLongCaptureDelayMs = 80;
static constexpr int kLongNativePassthroughCaptureDelayMs = 45;
static constexpr int kLongTrailingCaptureDelayMs = 200;
static constexpr int kLongMinCaptureIntervalMs = 60;
static constexpr int kLongPreviewMargin = 72;
static constexpr float kLongToolbarBtnSize = 40.f;

#ifdef _WIN32
namespace {

bool envFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

long long elapsedMs(std::chrono::steady_clock::time_point start,
                    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now()) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

void writeLongScreenshotLog(const char* format, ...) {
    CreateDirectoryA(SST_LOG_DIR, nullptr);

    char path[MAX_PATH] = {};
    std::snprintf(path, sizeof(path), "%s/long-screenshot.log", SST_LOG_DIR);

    FILE* file = nullptr;
    if (fopen_s(&file, path, "ab") != 0 || !file) {
        return;
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    std::fprintf(file,
                 "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
                 now.wYear,
                 now.wMonth,
                 now.wDay,
                 now.wHour,
                 now.wMinute,
                 now.wSecond,
                 now.wMilliseconds);

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

uint8_t colorByte(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
}

COLORREF colorRef(platform::Color color) {
    return RGB(colorByte(color.r), colorByte(color.g), colorByte(color.b));
}

Gdiplus::Color gdiplusColor(platform::Color color) {
    return Gdiplus::Color(colorByte(color.a),
                          colorByte(color.r),
                          colorByte(color.g),
                          colorByte(color.b));
}

void ensureGdiplusStarted() {
    static ULONG_PTR token = 0;
    static bool attempted = false;
    if (attempted) {
        return;
    }
    attempted = true;

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        token = 0;
    }
}

void appendFreehandPoint(std::vector<platform::PointF>& points, float x, float y) {
    static constexpr float kMinDistancePx = 0.35f;
    static constexpr float kInterpolateStepPx = 0.75f;

    if (points.empty()) {
        points.push_back({ x, y });
        return;
    }

    const auto last = points.back();
    const float dx = x - last.x;
    const float dy = y - last.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < kMinDistancePx) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::floor(distance / kInterpolateStepPx)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        points.push_back({ last.x + dx * t, last.y + dy * t });
    }
}

void blendRect(std::vector<uint8_t>& bgra,
               int width,
               int height,
               int x,
               int y,
               int w,
               int h,
               platform::Color color) {
    if (width <= 0 || height <= 0 || bgra.empty()) {
        return;
    }

    const int x0 = std::clamp(x, 0, width);
    const int y0 = std::clamp(y, 0, height);
    const int x1 = std::clamp(x + w, 0, width);
    const int y1 = std::clamp(y + h, 0, height);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    const int alpha = colorByte(color.a);
    const int invAlpha = 255 - alpha;
    const int r = colorByte(color.r);
    const int g = colorByte(color.g);
    const int b = colorByte(color.b);

    for (int py = y0; py < y1; ++py) {
        uint8_t* row = bgra.data() + static_cast<size_t>(py) * width * 4;
        for (int px = x0; px < x1; ++px) {
            uint8_t* p = row + px * 4;
            p[0] = static_cast<uint8_t>((b * alpha + p[0] * invAlpha) / 255);
            p[1] = static_cast<uint8_t>((g * alpha + p[1] * invAlpha) / 255);
            p[2] = static_cast<uint8_t>((r * alpha + p[2] * invAlpha) / 255);
        }
    }
}

void drawGdiLine(HDC dc,
                 float x0,
                 float y0,
                 float x1,
                 float y1,
                 platform::Color color,
                 float thickness) {
    ensureGdiplusStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Pen pen(gdiplusColor(color), std::max(1.0f, thickness));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawLine(&pen, x0, y0, x1, y1);
}

void drawGdiPolyline(HDC dc,
                     const std::vector<platform::PointF>& points,
                     platform::Color color,
                     float thickness) {
    if (points.size() < 2) {
        return;
    }

    ensureGdiplusStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Pen pen(gdiplusColor(color), std::max(1.0f, thickness));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    std::vector<Gdiplus::PointF> gdipPoints;
    gdipPoints.reserve(points.size());
    for (const auto& point : points) {
        gdipPoints.emplace_back(point.x, point.y);
    }
    graphics.DrawLines(&pen, gdipPoints.data(), static_cast<INT>(gdipPoints.size()));
}

void drawGdiRectOutline(HDC dc,
                        float x,
                        float y,
                        float w,
                        float h,
                        platform::Color color,
                        float thickness) {
    HPEN pen = CreatePen(PS_SOLID,
                         std::max(1, static_cast<int>(std::round(thickness))),
                         colorRef(color));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc,
              static_cast<int>(std::round(x)),
              static_cast<int>(std::round(y)),
              static_cast<int>(std::round(x + w)),
              static_cast<int>(std::round(y + h)));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawGdiRectFilled(HDC dc,
                       float x,
                       float y,
                       float w,
                       float h,
                       platform::Color color) {
    HBRUSH brush = CreateSolidBrush(colorRef(color));
    RECT rect = {
        static_cast<LONG>(std::round(x)),
        static_cast<LONG>(std::round(y)),
        static_cast<LONG>(std::round(x + w)),
        static_cast<LONG>(std::round(y + h))
    };
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawGdiArrow(HDC dc,
                  float x0,
                  float y0,
                  float x1,
                  float y1,
                  platform::Color color,
                  float thickness,
                  float headSize) {
    drawGdiLine(dc, x0, y0, x1, y1, color, thickness);

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }

    const float ux = dx / len;
    const float uy = dy / len;
    const float bx = x1 - ux * headSize;
    const float by = y1 - uy * headSize;
    const float px = -uy * headSize * 0.5f;
    const float py = ux * headSize * 0.5f;

    POINT pts[3] = {
        { static_cast<LONG>(std::round(x1)), static_cast<LONG>(std::round(y1)) },
        { static_cast<LONG>(std::round(bx + px)), static_cast<LONG>(std::round(by + py)) },
        { static_cast<LONG>(std::round(bx - px)), static_cast<LONG>(std::round(by - py)) },
    };

    HBRUSH brush = CreateSolidBrush(colorRef(color));
    HPEN pen = CreatePen(PS_SOLID, 1, colorRef(color));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, pts, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

} // namespace
#endif

Application::Application() = default;

Application::~Application() {
    if (screenshotTexture_) {
        glDeleteTextures(1, &screenshotTexture_);
    }
    if (longBackgroundTexture_) {
        glDeleteTextures(1, &longBackgroundTexture_);
    }
    if (longHintTextTexture_) {
        glDeleteTextures(1, &longHintTextTexture_);
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

    platform::Rect desktopBounds = { 0, 0, 1920, 1080 };
    if (!monitors.empty()) {
        int left = monitors.front().bounds.x;
        int top = monitors.front().bounds.y;
        int right = monitors.front().bounds.x + monitors.front().bounds.w;
        int bottom = monitors.front().bounds.y + monitors.front().bounds.h;
        for (const auto& m : monitors) {
            left = std::min(left, m.bounds.x);
            top = std::min(top, m.bounds.y);
            right = std::max(right, m.bounds.x + m.bounds.w);
            bottom = std::max(bottom, m.bounds.y + m.bounds.h);
        }
        desktopBounds = { left, top, right - left, bottom - top };
    }

#ifdef _WIN32
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw > 0 && vh > 0) {
        desktopBounds = { vx, vy, vw, vh };
    }
#endif

    screenSize_ = { desktopBounds.w, desktopBounds.h };
    screenBounds_ = desktopBounds;
    std::printf("[init] Virtual desktop: origin=%d,%d size=%dx%d\n",
                screenBounds_.x, screenBounds_.y, screenSize_.w, screenSize_.h);

    std::printf("[init] Creating overlay window...\n");
    if (!platform_.overlay->create(desktopBounds)) {
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

    auto registerScreenshotHotkey = [&](const char* name,
                                        uint32_t keyCode,
                                        uint8_t modifiers) {
        const auto hotkeyId = platform_.input->registerHotkey(
            keyCode,
            modifiers,
            [this]() { onHotkeyTriggered(); }
        );
        if (hotkeyId == 0) {
            std::fprintf(stderr, "[init] WARNING: %s hotkey registration failed\n", name);
            return;
        }
        std::printf("[init] Registered hotkey: %s\n", name);
    };

    const uint8_t ctrlAlt =
        static_cast<uint8_t>(platform::KeyModifier::Ctrl) |
        static_cast<uint8_t>(platform::KeyModifier::Alt);
    const uint8_t ctrlShift =
        static_cast<uint8_t>(platform::KeyModifier::Ctrl) |
        static_cast<uint8_t>(platform::KeyModifier::Shift);

    registerScreenshotHotkey("Ctrl+Alt+X", 'X', ctrlAlt);
    registerScreenshotHotkey("F8", 0x77, 0);
    registerScreenshotHotkey("Ctrl+Shift+A", 'A', ctrlShift);

    platform_.systemTray->create("Screenshot Tool", {
        { "Take Screenshot", [this]() { onHotkeyTriggered(); } },
        { "", nullptr, true },
        { "Quit", [this]() { quit(); } },
    });

    running_ = true;
    std::printf("[init] All systems ready!\n");
    std::printf("[info] Press Ctrl+Alt+X or F8 to take a screenshot\n");
    std::printf("[info] Right-click tray icon for menu\n");
    return true;
}

int Application::run() {
    while (running_) {
        platform_.input->pollHotkeys();

        if (!platform_.overlay->pumpMessages()) {
            break;
        }

        runPendingActions();

        auto state = stateMachine_.currentState();

        if (state != core::AppState::Idle) {
            if (isLongCaptureActive_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }
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
    resetCaptureSession();
#ifdef _WIN32
    if (!captureFramePixelsGdi(capturedPixels_, capturedW_, capturedH_,
                               { 0, 0, screenSize_.w, screenSize_.h }) &&
        !captureFramePixels(capturedPixels_, capturedW_, capturedH_, 500)) {
        return false;
    }
#else
    if (!captureFramePixels(capturedPixels_, capturedW_, capturedH_, 500)) {
        return false;
    }
#endif

    if (!uploadScreenshotTexture()) {
        return false;
    }

    std::printf("[capture] GL texture = %u (%dx%d) via CPU readback\n",
                screenshotTexture_, capturedW_, capturedH_);
    return true;
}

bool Application::captureFramePixels(std::vector<uint8_t>& outPixels,
                                     int& outW,
                                     int& outH,
                                     uint32_t timeoutMs) {
    platform::CapturedFrame frame;
    platform::CaptureError error;

    bool captured = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (platform_.capture->acquireFrame(frame, error, timeoutMs)) {
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
    if (!platform_.capture->readFramePixels(outPixels, outW, outH)) {
        std::fprintf(stderr, "[capture] Failed to read frame pixels\n");
        platform_.capture->releaseFrame();
        return false;
    }

    platform_.capture->releaseFrame();
    return true;
}

bool Application::captureLongFramePixels(std::vector<uint8_t>& outPixels,
                                          int& outW,
                                          int& outH,
                                          platform::Rect region) {
#ifdef _WIN32
    if (captureFramePixelsGdi(outPixels, outW, outH, region)) {
        return true;
    }
    std::fprintf(stderr, "[long] GDI visible capture failed; falling back to DXGI\n");
#endif

    std::vector<uint8_t> fullPixels;
    int fullW = 0, fullH = 0;
    if (!captureFramePixels(fullPixels, fullW, fullH, 250)) {
        return false;
    }

    outPixels = cropPixels(fullPixels, fullW, fullH, region);
    outW = std::min(std::max(0, region.w), std::max(0, fullW - std::max(0, region.x)));
    outH = std::min(std::max(0, region.h), std::max(0, fullH - std::max(0, region.y)));
    return !outPixels.empty();
}

#ifdef _WIN32
bool Application::captureLongFramePixelsFromCoveredWindow(std::vector<uint8_t>& outPixels,
                                                          int& outW,
                                                          int& outH,
                                                          platform::Rect region) {
    const auto started = std::chrono::steady_clock::now();
    const uint64_t scrollSeq = longCurrentFrameScrollSeq_;
    const auto scrollAt = longCurrentFrameScrollAt_;
    HWND overlayHwnd = static_cast<HWND>(platform_.overlay->getNativeHandle());
    const POINT screenPoint = {
        screenBounds_.x + region.x + region.w / 2,
        screenBounds_.y + region.y + region.h / 2
    };

    HWND target = WindowFromPoint(screenPoint);
    if (overlayHwnd && (target == overlayHwnd || GetAncestor(target, GA_ROOT) == overlayHwnd)) {
        target = nullptr;
        for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
            if (hwnd == overlayHwnd || GetAncestor(hwnd, GA_ROOT) == overlayHwnd) {
                continue;
            }
            if (!IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd)) {
                continue;
            }

            RECT rc = {};
            if (!GetWindowRect(hwnd, &rc) || !PtInRect(&rc, screenPoint)) {
                continue;
            }

            POINT clientPoint = screenPoint;
            ScreenToClient(hwnd, &clientPoint);
            HWND child = ChildWindowFromPointEx(
                hwnd,
                clientPoint,
                CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
            target = child ? child : hwnd;
            break;
        }
    }

    if (!target || (overlayHwnd && GetAncestor(target, GA_ROOT) == overlayHwnd)) {
        writeLongScreenshotLog("capture-covered failed seq=%llu event_to_capture_ms=%lld stage=no-target elapsed_ms=%lld region=%d,%d,%d,%d",
                               static_cast<unsigned long long>(scrollSeq),
                               scrollSeq ? elapsedMs(scrollAt) : -1,
                               elapsedMs(started), region.x, region.y, region.w, region.h);
        return false;
    }

    HWND root = GetAncestor(target, GA_ROOT);
    if (!root || !IsWindowVisible(root) || IsIconic(root)) {
        writeLongScreenshotLog("capture-covered failed seq=%llu event_to_capture_ms=%lld stage=bad-root elapsed_ms=%lld target=0x%p",
                               static_cast<unsigned long long>(scrollSeq),
                               scrollSeq ? elapsedMs(scrollAt) : -1,
                               elapsedMs(started), target);
        return false;
    }

    RECT windowRect = {};
    if (!GetWindowRect(root, &windowRect)) {
        writeLongScreenshotLog("capture-covered failed seq=%llu event_to_capture_ms=%lld stage=window-rect elapsed_ms=%lld root=0x%p",
                               static_cast<unsigned long long>(scrollSeq),
                               scrollSeq ? elapsedMs(scrollAt) : -1,
                               elapsedMs(started), root);
        return false;
    }

    const int windowW = static_cast<int>(windowRect.right - windowRect.left);
    const int windowH = static_cast<int>(windowRect.bottom - windowRect.top);
    if (windowW <= 0 || windowH <= 0) {
        return false;
    }

    const RECT captureRect = {
        screenBounds_.x + region.x,
        screenBounds_.y + region.y,
        screenBounds_.x + region.x + region.w,
        screenBounds_.y + region.y + region.h
    };
    if (captureRect.left < windowRect.left ||
        captureRect.top < windowRect.top ||
        captureRect.right > windowRect.right ||
        captureRect.bottom > windowRect.bottom) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return false;
    }

    HDC memDc = CreateCompatibleDC(screenDc);
    if (!memDc) {
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = windowW;
    bmi.bmiHeader.biHeight = -windowH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
    const BOOL printed = PrintWindow(root, memDc, PW_RENDERFULLCONTENT);
    const auto printedAt = std::chrono::steady_clock::now();

    outPixels.clear();
    outW = std::max(0, region.w);
    outH = std::max(0, region.h);
    if (printed && outW > 0 && outH > 0) {
        outPixels.resize(static_cast<size_t>(outW) * outH * 4);
        const int srcX = static_cast<int>(captureRect.left - windowRect.left);
        const int srcY = static_cast<int>(captureRect.top - windowRect.top);
        const uint8_t* srcBase = static_cast<const uint8_t*>(bits);
        for (int y = 0; y < outH; ++y) {
            const uint8_t* src = srcBase +
                (static_cast<size_t>(srcY + y) * windowW + srcX) * 4;
            uint8_t* dst = outPixels.data() + static_cast<size_t>(y) * outW * 4;
            for (int x = 0; x < outW; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
    }

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    writeLongScreenshotLog("capture-covered seq=%llu event_to_capture_ms=%lld result=%d elapsed_ms=%lld print_ms=%lld target=0x%p root=0x%p frame=%dx%d window=%dx%d",
                           static_cast<unsigned long long>(scrollSeq),
                           scrollSeq ? elapsedMs(scrollAt) : -1,
                           printed != FALSE && !outPixels.empty(),
                           elapsedMs(started),
                           elapsedMs(started, printedAt),
                           target,
                           root,
                           outW,
                           outH,
                           windowW,
                           windowH);
    return printed != FALSE && !outPixels.empty();
}

bool Application::captureFramePixelsGdi(std::vector<uint8_t>& outPixels,
                                        int& outW,
                                        int& outH,
                                        platform::Rect region) {
    const int sx = std::clamp(region.x, 0, screenSize_.w);
    const int sy = std::clamp(region.y, 0, screenSize_.h);
    outW = std::min(std::max(0, region.w), std::max(0, screenSize_.w - sx));
    outH = std::min(std::max(0, region.h), std::max(0, screenSize_.h - sy));
    if (outW <= 0 || outH <= 0) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return false;
    }

    HDC memDc = CreateCompatibleDC(screenDc);
    if (!memDc) {
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = outW;
    bmi.bmiHeader.biHeight = -outH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
    const BOOL copied = BitBlt(memDc, 0, 0, outW, outH,
                               screenDc, screenBounds_.x + sx, screenBounds_.y + sy,
                               SRCCOPY);

    outPixels.clear();
    if (copied) {
        outPixels.resize(static_cast<size_t>(outW) * outH * 4);
        const uint8_t* src = static_cast<const uint8_t*>(bits);
        uint8_t* dst = outPixels.data();
        for (int i = 0; i < outW * outH; ++i) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
            src += 4;
            dst += 4;
        }
    }

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    return copied != FALSE && !outPixels.empty();
}
#endif

bool Application::uploadScreenshotTexture() {
    return uploadScreenshotTextureFromPixels(capturedPixels_, capturedW_, capturedH_);
}

bool Application::uploadScreenshotTextureFromPixels(const std::vector<uint8_t>& pixels,
                                                    int width,
                                                    int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return false;
    }

    // Upload to GL texture
    platform_.eglContext->makeCurrent();
    if (screenshotTexture_ == 0) {
        glGenTextures(1, &screenshotTexture_);
    }
    glBindTexture(GL_TEXTURE_2D, screenshotTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

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
            resetCaptureSession();
        }
    } else if (state == core::AppState::Annotating) {
        // Update hover state on toolbar buttons
        for (auto& btn : toolButtons_) {
            btn.isHovered = (mx >= btn.x && mx <= btn.x + btn.w &&
                             my >= btn.y && my <= btn.y + btn.h);
        }

        if (isLongCaptureActive_ && evt.type == platform::MouseEvent::Type::Scroll) {
            handleLongScreenshotScroll(evt.scrollDelta,
                                       evt.position,
                                       evt.nativePassthrough);
            return;
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
            resetCaptureSession();
        }
    }
}

// ── Key Events ──────────────────────────────────────────────────
void Application::onKeyEvent(const platform::KeyEvent& evt) {
    if (evt.type != platform::KeyEvent::Type::Press) return;

    if (evt.keyCode == VK_ESCAPE) {
        stateMachine_.transition(core::AppEvent::Escape);
        platform_.overlay->hide();
        resetCaptureSession();
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

    if (isLongScreenshotResult_) {
        constexpr int numButtons = 4;
        const float totalW = numButtons * kLongToolbarBtnSize;
        const float totalH = kLongToolbarBtnSize;
        const auto anchor = longScreenshotSourceRegion_.w > 0 ? longScreenshotSourceRegion_ : sel;
        const float sw = static_cast<float>(screenSize_.w);
        const float sh = static_cast<float>(screenSize_.h);
        const float ax = static_cast<float>(anchor.x);
        const float ay = static_cast<float>(anchor.y);
        const float aw = static_cast<float>(anchor.w);
        const float ah = static_cast<float>(anchor.h);

        auto intersectsAnchor = [&](float x, float y) {
            return x < ax + aw && x + totalW > ax &&
                   y < ay + ah && y + totalH > ay;
        };
        auto fits = [&](float x, float y) {
            return x >= 8.f && y >= 8.f &&
                   x + totalW <= sw - 8.f &&
                   y + totalH <= sh - 8.f &&
                   !intersectsAnchor(x, y);
        };

        const float rightAligned = std::clamp(ax + aw - totalW, 8.f, std::max(8.f, sw - totalW - 8.f));
        const float centeredY = std::clamp(ay + (ah - totalH) * 0.5f, 8.f, std::max(8.f, sh - totalH - 8.f));
        const std::array<std::pair<float, float>, 4> candidates = {{
            { rightAligned, ay + ah + 10.f },
            { rightAligned, ay - totalH - 10.f },
            { ax + aw + 12.f, centeredY },
            { ax - totalW - 12.f, centeredY },
        }};

        float tx = candidates[0].first;
        float ty = candidates[0].second;
        bool placedOutsideSource = false;
        for (const auto& candidate : candidates) {
            if (fits(candidate.first, candidate.second)) {
                tx = candidate.first;
                ty = candidate.second;
                placedOutsideSource = true;
                break;
            }
        }
        if (!placedOutsideSource) {
            tx = rightAligned;
            ty = std::clamp(ay + ah + 10.f, 8.f, std::max(8.f, sh - totalH - 8.f));
        }

        toolbarX_ = tx;
        toolbarY_ = std::max(8.f, ty);
        toolbarW_ = totalW;
        toolbarH_ = totalH;

        float bx = toolbarX_;
        auto addBtn = [&](ToolButton::Type type) {
            toolButtons_.push_back({ bx, toolbarY_, kLongToolbarBtnSize,
                                     kLongToolbarBtnSize, type, false });
            bx += kLongToolbarBtnSize;
        };

        addBtn(ToolButton::Type::Edit);
        addBtn(ToolButton::Type::Save);
        addBtn(ToolButton::Type::Cancel);
        addBtn(ToolButton::Type::Confirm);
        return;
    }

    // 9 buttons: [Rect] [Arrow] [Line] [Brush] [Long] | [Undo] [Save] [Copy] [Cancel]
    int numButtons = 9;
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
    addBtn(ToolButton::Type::Freehand);
    addBtn(ToolButton::Type::LongScreenshot);
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
    case ToolButton::Type::Edit:
        activeTool_ = core::AnnotationTool::Rectangle;
        std::printf("[toolbar] Long screenshot edit mode\n");
        break;
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
    case ToolButton::Type::Freehand:
        activeTool_ = core::AnnotationTool::Freehand;
        std::printf("[toolbar] Tool: Freehand\n");
        break;
    case ToolButton::Type::LongScreenshot:
        pendingLongScreenshot_ = true;
        std::printf("[toolbar] Long screenshot requested\n");
        break;
    case ToolButton::Type::Undo:
        if (commandHistory_.canUndo()) {
            commandHistory_.undo();
            std::printf("[toolbar] Undo\n");
        }
        break;
    case ToolButton::Type::Save:
        if (isLongCaptureActive_) {
            finishLongScreenshotMode();
        }
        saveToFile();
        break;
    case ToolButton::Type::Copy:
        if (isLongCaptureActive_) {
            finishLongScreenshotMode();
        }
        saveToClipboard();
        break;
    case ToolButton::Type::Confirm:
        if (isLongCaptureActive_) {
            finishLongScreenshotMode();
        }
        saveToClipboard();
        break;
    case ToolButton::Type::Cancel:
        stateMachine_.transition(core::AppEvent::Escape);
        platform_.overlay->hide();
        resetCaptureSession();
        std::printf("[toolbar] Cancel\n");
        break;
    }
}

// ── Annotation Drawing ──────────────────────────────────────────
void Application::startAnnotation(float x, float y) {
    isDrawingAnnotation_ = true;
    annStartX_ = x; annStartY_ = y;
    annCurrX_ = x;  annCurrY_ = y;
    activeFreehandPoints_.clear();
    if (activeTool_ == core::AnnotationTool::Freehand) {
        appendFreehandPoint(activeFreehandPoints_, x, y);
    }
}

void Application::updateAnnotation(float x, float y) {
    annCurrX_ = x; annCurrY_ = y;
    if (activeTool_ == core::AnnotationTool::Freehand) {
        appendFreehandPoint(activeFreehandPoints_, x, y);
    }
}

void Application::finishAnnotation(float x, float y) {
    isDrawingAnnotation_ = false;
    annCurrX_ = x; annCurrY_ = y;

    float dx = annCurrX_ - annStartX_;
    float dy = annCurrY_ - annStartY_;
    if (activeTool_ != core::AnnotationTool::Freehand &&
        std::abs(dx) < 3.f && std::abs(dy) < 3.f) {
        return; // too small
    }

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
    case core::AnnotationTool::Freehand: {
        if (activeFreehandPoints_.empty()) {
            appendFreehandPoint(activeFreehandPoints_, annStartX_, annStartY_);
        }
        appendFreehandPoint(activeFreehandPoints_, annCurrX_, annCurrY_);
        if (activeFreehandPoints_.size() < 2) {
            activeFreehandPoints_.clear();
            return;
        }
        ann = core::FreehandAnnotation{
            activeFreehandPoints_, annotationColor_, annotationThickness_
        };
        activeFreehandPoints_.clear();
        break;
    }
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
                activeTool_ == core::AnnotationTool::Arrow ? "arrow" :
                activeTool_ == core::AnnotationTool::Line ? "line" : "freehand");
}

#ifdef _WIN32
bool Application::shouldUseSoftwareOverlay() const {
    return !envFlagEnabled("SST_GL_OVERLAY");
}

void Application::renderSoftwareOverlay() {
    const bool useLongBackground =
        isLongScreenshotResult_ &&
        !longBackgroundPixels_.empty() &&
        longBackgroundW_ > 0 &&
        longBackgroundH_ > 0;
    const auto& backgroundPixels = useLongBackground ? longBackgroundPixels_ : capturedPixels_;
    const int backgroundW = useLongBackground ? longBackgroundW_ : capturedW_;
    const int backgroundH = useLongBackground ? longBackgroundH_ : capturedH_;

    if (backgroundPixels.empty() || backgroundW <= 0 || backgroundH <= 0) {
        return;
    }

    const int width = screenSize_.w;
    const int height = screenSize_.h;
    if (width <= 0 || height <= 0) {
        return;
    }

    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4, 0);
    const int copyW = std::min(width, backgroundW);
    const int copyH = std::min(height, backgroundH);
    for (int y = 0; y < copyH; ++y) {
        const uint8_t* src = backgroundPixels.data() + static_cast<size_t>(y) * backgroundW * 4;
        uint8_t* dst = bgra.data() + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < copyW; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }

    auto state = stateMachine_.currentState();
    if (state == core::AppState::Selecting || state == core::AppState::Annotating) {
        float sx = 0.f;
        float sy = 0.f;
        float sw = 0.f;
        float sh = 0.f;
        if (state == core::AppState::Selecting && isDragging_) {
            const float x0 = std::min(dragStartX_, dragCurrX_);
            const float y0 = std::min(dragStartY_, dragCurrY_);
            const float x1 = std::max(dragStartX_, dragCurrX_);
            const float y1 = std::max(dragStartY_, dragCurrY_);
            sx = x0;
            sy = y0;
            sw = x1 - x0;
            sh = y1 - y0;
        } else if (state == core::AppState::Annotating) {
            auto sel = stateMachine_.selectedRegion();
            sx = static_cast<float>(sel.x);
            sy = static_cast<float>(sel.y);
            sw = static_cast<float>(sel.w);
            sh = static_cast<float>(sel.h);
        }

        const int ix = static_cast<int>(std::round(sx));
        const int iy = static_cast<int>(std::round(sy));
        const int iw = static_cast<int>(std::round(sw));
        const int ih = static_cast<int>(std::round(sh));
        blendRect(bgra, width, height, 0, 0, width, iy, kDimColor);
        blendRect(bgra, width, height, 0, iy + ih, width, height - (iy + ih), kDimColor);
        blendRect(bgra, width, height, 0, iy, ix, ih, kDimColor);
        blendRect(bgra, width, height, ix + iw, iy, width - (ix + iw), ih, kDimColor);
    }

    HWND hwnd = static_cast<HWND>(platform_.overlay->getNativeHandle());
    if (!hwnd) {
        return;
    }

    HDC windowDc = GetDC(hwnd);
    HDC memDc = CreateCompatibleDC(windowDc);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(windowDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(hwnd, windowDc);
        return;
    }

    std::memcpy(bits, bgra.data(), bgra.size());
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    SetBkMode(memDc, TRANSPARENT);
    auto drawRgbaPixelsScaled = [&](const std::vector<uint8_t>& pixels,
                                    int imageW,
                                    int imageH,
                                    int sourceY,
                                    int sourceH,
                                    float dx,
                                    float dy,
                                    float dw,
                                    float dh) {
        const int dstX = static_cast<int>(std::round(dx));
        const int dstY = static_cast<int>(std::round(dy));
        const int dstW = std::max(1, static_cast<int>(std::round(dw)));
        const int dstH = std::max(1, static_cast<int>(std::round(dh)));
        if (pixels.empty() || imageW <= 0 || imageH <= 0 || sourceH <= 0) {
            return;
        }

        const int srcY0 = std::clamp(sourceY, 0, imageH - 1);
        const int srcH = std::clamp(sourceH, 1, imageH - srcY0);
        std::vector<uint8_t> scaled(static_cast<size_t>(dstW) * dstH * 4);
        for (int y = 0; y < dstH; ++y) {
            const int sy = srcY0 + std::min(srcH - 1, (y * srcH) / dstH);
            for (int x = 0; x < dstW; ++x) {
                const int sx = std::min(imageW - 1, (x * imageW) / dstW);
                const size_t srcIdx = (static_cast<size_t>(sy) * imageW + sx) * 4;
                const size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * 4;
                scaled[dstIdx + 0] = pixels[srcIdx + 2];
                scaled[dstIdx + 1] = pixels[srcIdx + 1];
                scaled[dstIdx + 2] = pixels[srcIdx + 0];
                scaled[dstIdx + 3] = 255;
            }
        }

        BITMAPINFO scaledBmi = {};
        scaledBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        scaledBmi.bmiHeader.biWidth = dstW;
        scaledBmi.bmiHeader.biHeight = -dstH;
        scaledBmi.bmiHeader.biPlanes = 1;
        scaledBmi.bmiHeader.biBitCount = 32;
        scaledBmi.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(memDc, dstX, dstY, dstW, dstH,
                      0, 0, dstW, dstH,
                      scaled.data(), &scaledBmi, DIB_RGB_COLORS, SRCCOPY);
    };

    if (state == core::AppState::Selecting) {
        if (isDragging_) {
            const float x0 = std::min(dragStartX_, dragCurrX_);
            const float y0 = std::min(dragStartY_, dragCurrY_);
            const float x1 = std::max(dragStartX_, dragCurrX_);
            const float y1 = std::max(dragStartY_, dragCurrY_);
            drawGdiRectOutline(memDc, x0, y0, x1 - x0, y1 - y0, kSelBorder, 2.0f);
        }
    } else if (state == core::AppState::Annotating) {
        auto sel = stateMachine_.selectedRegion();
        if (isLongScreenshotResult_) {
            platform::Rect preview = longScreenshotSourceRegion_.w > 0
                ? longScreenshotSourceRegion_
                : sel;
            const float px = static_cast<float>(preview.x);
            const float py = static_cast<float>(preview.y);
            const float pw = static_cast<float>(preview.w);
            const float ph = static_cast<float>(preview.h);
            const float previewScale = capturedW_ > 0 ? pw / static_cast<float>(capturedW_) : 1.f;
            const float visibleV = capturedH_ > 0
                ? std::clamp(ph / (static_cast<float>(capturedH_) * previewScale), 0.0f, 1.0f)
                : 1.0f;
            const float thumbMaxH = std::min(static_cast<float>(height) - 32.f,
                                             std::max(ph, ph + 96.f));
            const float thumbH = std::max(160.f, thumbMaxH);
            const float thumbW = std::clamp(
                thumbH * static_cast<float>(capturedW_) / std::max(1, capturedH_),
                72.f, 132.f);
            const float gap = 18.f;
            const float thumbX = px - thumbW - gap >= 8.f
                ? px - thumbW - gap
                : std::min(static_cast<float>(width) - thumbW - 8.f, px + pw + gap);
            const float thumbY = std::clamp(
                py + (ph - thumbH) * 0.5f,
                8.f,
                std::max(8.f, static_cast<float>(height) - thumbH - 8.f));
            drawGdiRectFilled(memDc, thumbX + 3.f, thumbY + 3.f,
                              thumbW, thumbH, { 0, 0, 0, 52 });
            drawGdiRectFilled(memDc, thumbX, thumbY, thumbW, thumbH, kLongPanel);
            drawRgbaPixelsScaled(capturedPixels_, capturedW_, capturedH_,
                                 0, capturedH_, thumbX, thumbY, thumbW, thumbH);
            const float viewportH = std::max(18.f, thumbH * visibleV);
            const float viewportY = thumbY + thumbH - viewportH;
            drawGdiRectOutline(memDc, thumbX - 1.f, thumbY - 1.f,
                               thumbW + 2.f, thumbH + 2.f,
                               { 255, 255, 255, 130 }, 1.0f);
            drawGdiRectOutline(memDc, thumbX - 2.f, viewportY - 1.f,
                               thumbW + 4.f, viewportH + 2.f,
                               { 255, 255, 255, 235 }, 2.0f);

            const wchar_t* hint = L"\u6EDA\u52A8\u9875\u9762\u622A\u53D6\u66F4\u591A\u5185\u5BB9";
            const float hintW = 270.f;
            const float hintH = 34.f;
            const float hintX = px + (pw - hintW) * 0.5f;
            const bool hintAbove = py - hintH - 10.f >= 8.f;
            if (hintAbove) {
                const float hintY = py - hintH - 10.f;
                drawGdiRectFilled(memDc, hintX, hintY, hintW, hintH, kLongHintBg);
                SetTextColor(memDc, RGB(255, 255, 255));
                RECT hintRect = {
                    static_cast<LONG>(std::round(hintX + 18.f)),
                    static_cast<LONG>(std::round(hintY + 7.f)),
                    static_cast<LONG>(std::round(hintX + hintW - 18.f)),
                    static_cast<LONG>(std::round(hintY + hintH))
                };
                DrawTextW(memDc, hint, -1, &hintRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
        }

        const auto borderColor = isLongScreenshotResult_ ? kLongBorder : kSelBorder;
        drawGdiRectOutline(memDc, static_cast<float>(sel.x), static_cast<float>(sel.y),
                           static_cast<float>(sel.w), static_cast<float>(sel.h),
                           borderColor, isLongScreenshotResult_ ? 3.0f : 2.0f);

        auto drawAnnotation = [&](const core::Annotation& ann) {
            std::visit([&](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, core::RectAnnotation>) {
                    if (a.filled) {
                        drawGdiRectFilled(memDc, a.bounds.x, a.bounds.y,
                                          a.bounds.w, a.bounds.h, a.color);
                    } else {
                        drawGdiRectOutline(memDc, a.bounds.x, a.bounds.y,
                                           a.bounds.w, a.bounds.h,
                                           a.color, a.thickness);
                    }
                } else if constexpr (std::is_same_v<T, core::ArrowAnnotation>) {
                    drawGdiArrow(memDc, a.start.x, a.start.y, a.end.x, a.end.y,
                                 a.color, a.thickness, a.headSize);
                } else if constexpr (std::is_same_v<T, core::LineAnnotation>) {
                    drawGdiLine(memDc, a.start.x, a.start.y, a.end.x, a.end.y,
                                a.color, a.thickness);
                } else if constexpr (std::is_same_v<T, core::FreehandAnnotation>) {
                    drawGdiPolyline(memDc, a.points, a.color, a.thickness);
                }
            }, ann);
        };

        for (const auto& ann : annotations_.annotations()) {
            drawAnnotation(ann);
        }

        if (isDrawingAnnotation_) {
            switch (activeTool_) {
            case core::AnnotationTool::Rectangle: {
                const float rx = std::min(annStartX_, annCurrX_);
                const float ry = std::min(annStartY_, annCurrY_);
                drawGdiRectOutline(memDc, rx, ry,
                                   std::abs(annCurrX_ - annStartX_),
                                   std::abs(annCurrY_ - annStartY_),
                                   annotationColor_, annotationThickness_);
                break;
            }
            case core::AnnotationTool::Arrow:
                drawGdiArrow(memDc, annStartX_, annStartY_, annCurrX_, annCurrY_,
                             annotationColor_, annotationThickness_, 12.0f);
                break;
            case core::AnnotationTool::Line:
                drawGdiLine(memDc, annStartX_, annStartY_, annCurrX_, annCurrY_,
                            annotationColor_, annotationThickness_);
                break;
            case core::AnnotationTool::Freehand:
                drawGdiPolyline(memDc, activeFreehandPoints_,
                                annotationColor_, annotationThickness_);
                break;
            default:
                break;
            }
        }

        drawGdiRectFilled(memDc, toolbarX_, toolbarY_, toolbarW_, toolbarH_,
                          isLongScreenshotResult_ ? kLongToolbar : kToolbarBg);
        for (const auto& btn : toolButtons_) {
            const bool isSelected =
                (btn.type == ToolButton::Type::Rectangle && activeTool_ == core::AnnotationTool::Rectangle) ||
                (btn.type == ToolButton::Type::Arrow && activeTool_ == core::AnnotationTool::Arrow) ||
                (btn.type == ToolButton::Type::Line && activeTool_ == core::AnnotationTool::Line) ||
                (btn.type == ToolButton::Type::Freehand && activeTool_ == core::AnnotationTool::Freehand);
            if (isSelected) {
                drawGdiRectFilled(memDc, btn.x, btn.y, btn.w, btn.h, kBtnSelected);
            } else if (btn.isHovered) {
                drawGdiRectFilled(memDc, btn.x, btn.y, btn.w, btn.h, kBtnHover);
            }

            const float cx = btn.x + btn.w * 0.5f;
            const float cy = btn.y + btn.h * 0.5f;
            const float p = 8.f;
            const auto iconColor = isLongScreenshotResult_
                ? (btn.type == ToolButton::Type::Cancel ? kLongCancel :
                   btn.type == ToolButton::Type::Confirm ? kLongConfirm :
                   platform::Color{ 76, 82, 92, 255 })
                : kBtnIcon;
            switch (btn.type) {
            case ToolButton::Type::Edit:
                drawGdiRectOutline(memDc, btn.x + p + 2.f, btn.y + p + 2.f,
                                   btn.w - p * 2.f - 4.f, btn.h - p * 2.f - 4.f,
                                   iconColor, 1.6f);
                drawGdiLine(memDc, cx - 5.f, cy + 5.f, cx + 6.f, cy - 6.f,
                            iconColor, 2.0f);
                break;
            case ToolButton::Type::Rectangle:
                drawGdiRectOutline(memDc, btn.x + p, btn.y + p,
                                   btn.w - p * 2, btn.h - p * 2, iconColor, 2.0f);
                break;
            case ToolButton::Type::Arrow:
                drawGdiArrow(memDc, btn.x + p, btn.y + p,
                              btn.x + btn.w - p, btn.y + btn.h - p,
                              iconColor, 2.0f, 8.0f);
                break;
            case ToolButton::Type::Line:
                drawGdiLine(memDc, btn.x + p, btn.y + btn.h - p,
                            btn.x + btn.w - p, btn.y + p, iconColor, 2.0f);
                break;
            case ToolButton::Type::Freehand:
                drawGdiLine(memDc, btn.x + 7.f, btn.y + 21.f, btn.x + 12.f, btn.y + 14.f, iconColor, 2.0f);
                drawGdiLine(memDc, btn.x + 12.f, btn.y + 14.f, btn.x + 18.f, btn.y + 19.f, iconColor, 2.0f);
                drawGdiLine(memDc, btn.x + 18.f, btn.y + 19.f, btn.x + 25.f, btn.y + 10.f, iconColor, 2.0f);
                break;
            case ToolButton::Type::LongScreenshot:
                drawGdiLine(memDc, cx, btn.y + p, cx, btn.y + btn.h - p - 4, iconColor, 2.0f);
                drawGdiLine(memDc, cx, btn.y + btn.h - p, cx - 6, btn.y + btn.h - p - 6, iconColor, 2.0f);
                drawGdiLine(memDc, cx, btn.y + btn.h - p, cx + 6, btn.y + btn.h - p - 6, iconColor, 2.0f);
                break;
            case ToolButton::Type::Undo:
                drawGdiLine(memDc, cx, btn.y + p, btn.x + p, cy, iconColor, 2.0f);
                drawGdiLine(memDc, btn.x + p, cy, cx, btn.y + btn.h - p, iconColor, 2.0f);
                break;
            case ToolButton::Type::Save:
                drawGdiLine(memDc, cx, btn.y + p, cx, btn.y + btn.h - p - 7.f, iconColor, 2.0f);
                drawGdiLine(memDc, cx, btn.y + btn.h - p, cx - 6.f, btn.y + btn.h - p - 6.f, iconColor, 2.0f);
                drawGdiLine(memDc, cx, btn.y + btn.h - p, cx + 6.f, btn.y + btn.h - p - 6.f, iconColor, 2.0f);
                drawGdiLine(memDc, btn.x + p + 2.f, btn.y + btn.h - p + 1.f,
                            btn.x + btn.w - p - 2.f, btn.y + btn.h - p + 1.f,
                            iconColor, 2.0f);
                break;
            case ToolButton::Type::Copy:
                drawGdiRectOutline(memDc, btn.x + p, btn.y + p, btn.w - p * 2 - 4, btn.h - p * 2 - 4, iconColor, 1.5f);
                drawGdiRectOutline(memDc, btn.x + p + 4, btn.y + p + 4, btn.w - p * 2 - 4, btn.h - p * 2 - 4, iconColor, 1.5f);
                break;
            case ToolButton::Type::Cancel:
                drawGdiLine(memDc, btn.x + p, btn.y + p, btn.x + btn.w - p, btn.y + btn.h - p, iconColor, 2.0f);
                drawGdiLine(memDc, btn.x + btn.w - p, btn.y + p, btn.x + p, btn.y + btn.h - p, iconColor, 2.0f);
                break;
            case ToolButton::Type::Confirm:
                drawGdiLine(memDc, btn.x + p, cy + 1.f, cx - 2.f, btn.y + btn.h - p,
                            iconColor, 2.4f);
                drawGdiLine(memDc, cx - 2.f, btn.y + btn.h - p,
                            btn.x + btn.w - p, btn.y + p,
                            iconColor, 2.4f);
                break;
            default:
                break;
            }
        }
    }

    BitBlt(windowDc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(hwnd, windowDc);
}
#endif

// ── Render ──────────────────────────────────────────────────────
void Application::render() {
#ifdef _WIN32
    if (shouldUseSoftwareOverlay()) {
        renderSoftwareOverlay();
        return;
    }
#endif

    platform_.eglContext->makeCurrent();
    auto size = platform_.eglContext->getSurfaceSize();

    glViewport(0, 0, size.w, size.h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    auto state = stateMachine_.currentState();

    if (isLongScreenshotResult_ && state == core::AppState::Annotating) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderLongScreenshotUi();
        glDisable(GL_BLEND);
        platform_.eglContext->swapBuffers();
        return;
    }

    if (screenshotTexture_) {
        spriteBatch_.drawFullscreen(screenshotTexture_);
    }

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
            case core::AnnotationTool::Freehand:
                shapeRenderer_.drawPolyline(activeFreehandPoints_,
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
            } else if constexpr (std::is_same_v<T, core::FreehandAnnotation>) {
                shapeRenderer_.drawPolyline(a.points, a.color, a.thickness);
            } else if constexpr (std::is_same_v<T, core::TextAnnotation>) {
                // TODO: SDF text rendering
            }
        }, ann);
    }
}

void Application::renderLongScreenshotUi() {
    const float sw = static_cast<float>(screenSize_.w);
    const float sh = static_cast<float>(screenSize_.h);

    if (longBackgroundTexture_) {
        spriteBatch_.drawFullscreen(longBackgroundTexture_);
    }
    shapeRenderer_.drawRectFilled(0.f, 0.f, sw, sh, kLongDimColor);

    auto preview = stateMachine_.selectedRegion();
    if (longScreenshotSourceRegion_.w > 0 && longScreenshotSourceRegion_.h > 0) {
        preview = longScreenshotSourceRegion_;
    }
    const float px = static_cast<float>(preview.x);
    const float py = static_cast<float>(preview.y);
    const float pw = static_cast<float>(preview.w);
    const float ph = static_cast<float>(preview.h);
    const float previewScale = capturedW_ > 0 ? pw / static_cast<float>(capturedW_) : 1.f;
    const float visibleV = capturedH_ > 0
        ? std::clamp(ph / (static_cast<float>(capturedH_) * previewScale), 0.0f, 1.0f)
        : 1.0f;

    const float thumbMaxH = std::min(sh - 32.f, std::max(ph, ph + 96.f));
    const float thumbH = std::max(160.f, thumbMaxH);
    const float thumbW = std::clamp(
        thumbH * static_cast<float>(capturedW_) / std::max(1, capturedH_),
        72.f, 132.f);
    const float gap = 18.f;
    const float thumbX = px - thumbW - gap >= 8.f
        ? px - thumbW - gap
        : std::min(sw - thumbW - 8.f, px + pw + gap);
    const float thumbY = std::clamp(
        py + (ph - thumbH) * 0.5f,
        8.f,
        std::max(8.f, sh - thumbH - 8.f));

    shapeRenderer_.drawRectFilled(thumbX + 3.f, thumbY + 3.f,
                                  thumbW, thumbH, { 0, 0, 0, 52 });
    shapeRenderer_.drawRectFilled(thumbX, thumbY, thumbW, thumbH, kLongPanel);
    spriteBatch_.drawQuad(screenshotTexture_, thumbX, thumbY, thumbW, thumbH);

    const float viewportH = std::max(18.f, thumbH * visibleV);
    const float viewportY = thumbY + thumbH - viewportH;
    shapeRenderer_.drawRectOutline(thumbX - 1.f, thumbY - 1.f,
                                   thumbW + 2.f, thumbH + 2.f,
                                   { 255, 255, 255, 130 }, 1.0f);
    shapeRenderer_.drawRectOutline(thumbX - 2.f, viewportY - 1.f,
                                   thumbW + 4.f, viewportH + 2.f,
                                   { 255, 255, 255, 235 }, 2.0f);

    shapeRenderer_.drawRectOutline(px, py, pw, ph, kLongBorder, 3.0f);

    const bool hasHint = ensureLongHintTexture();
    const float hintW = hasHint ? static_cast<float>(longHintTextW_ + 42) : 260.f;
    const float hintH = 34.f;
    const float hintX = px + (pw - hintW) * 0.5f;
    const bool hintAbove = py - hintH - 10.f >= 8.f;
    if (hintAbove) {
        const float hintY = py - hintH - 10.f;
        shapeRenderer_.drawRectFilled(hintX, hintY, hintW, hintH, kLongHintBg);
        if (hasHint) {
            spriteBatch_.drawQuad(longHintTextTexture_,
                                  hintX + 21.f,
                                  hintY + (hintH - longHintTextH_) * 0.5f,
                                  static_cast<float>(longHintTextW_),
                                  static_cast<float>(longHintTextH_));
        }
    }

    renderLongScreenshotToolbar();
}

void Application::renderToolbar() {
    if (isLongScreenshotResult_) {
        renderLongScreenshotToolbar();
        return;
    }

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
        if (btn.type == ToolButton::Type::Freehand &&
            activeTool_ == core::AnnotationTool::Freehand) isSelected = true;

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
        case ToolButton::Type::Freehand:
            // Freehand brush icon
            shapeRenderer_.drawLine(btn.x + 7.f, btn.y + 21.f,
                                     btn.x + 12.f, btn.y + 14.f,
                                     kBtnIcon, 2.0f);
            shapeRenderer_.drawLine(btn.x + 12.f, btn.y + 14.f,
                                     btn.x + 18.f, btn.y + 19.f,
                                     kBtnIcon, 2.0f);
            shapeRenderer_.drawLine(btn.x + 18.f, btn.y + 19.f,
                                     btn.x + 25.f, btn.y + 10.f,
                                     kBtnIcon, 2.0f);
            break;
        case ToolButton::Type::LongScreenshot:
            // Downward scroll capture icon
            shapeRenderer_.drawLine(cx, btn.y + iconPad,
                                     cx, btn.y + btn.h - iconPad - 4,
                                     kBtnIcon, 2.0f);
            shapeRenderer_.drawLine(cx, btn.y + btn.h - iconPad,
                                     cx - 6, btn.y + btn.h - iconPad - 6,
                                     kBtnIcon, 2.0f);
            shapeRenderer_.drawLine(cx, btn.y + btn.h - iconPad,
                                     cx + 6, btn.y + btn.h - iconPad - 6,
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

void Application::renderLongScreenshotToolbar() {
    shapeRenderer_.drawRectFilled(toolbarX_ + 2.f, toolbarY_ + 3.f,
                                  toolbarW_, toolbarH_, { 0, 0, 0, 42 });
    shapeRenderer_.drawRectFilled(toolbarX_, toolbarY_,
                                  toolbarW_, toolbarH_, kLongToolbar);

    for (const auto& btn : toolButtons_) {
        if (btn.isHovered) {
            shapeRenderer_.drawRectFilled(btn.x + 2.f, btn.y + 2.f,
                                          btn.w - 4.f, btn.h - 4.f,
                                          { 232, 235, 240, 255 });
        }

        const float cx = btn.x + btn.w * 0.5f;
        const float cy = btn.y + btn.h * 0.5f;
        const float iconPad = 11.f;
        const auto iconColor = btn.type == ToolButton::Type::Cancel ? kLongCancel :
                               btn.type == ToolButton::Type::Confirm ? kLongConfirm :
                               platform::Color{ 76, 82, 92, 255 };

        switch (btn.type) {
        case ToolButton::Type::Edit:
            shapeRenderer_.drawRectOutline(btn.x + iconPad, btn.y + iconPad + 1.f,
                                           btn.w - iconPad * 2.f - 2.f,
                                           btn.h - iconPad * 2.f - 1.f,
                                           iconColor, 1.6f);
            shapeRenderer_.drawLine(cx - 4.f, cy + 5.f,
                                    cx + 6.f, cy - 5.f,
                                    iconColor, 2.0f);
            break;
        case ToolButton::Type::Save:
            shapeRenderer_.drawLine(cx, btn.y + iconPad,
                                    cx, btn.y + btn.h - iconPad - 5.f,
                                    iconColor, 2.0f);
            shapeRenderer_.drawLine(cx, btn.y + btn.h - iconPad - 4.f,
                                    cx - 5.f, btn.y + btn.h - iconPad - 9.f,
                                    iconColor, 2.0f);
            shapeRenderer_.drawLine(cx, btn.y + btn.h - iconPad - 4.f,
                                    cx + 5.f, btn.y + btn.h - iconPad - 9.f,
                                    iconColor, 2.0f);
            shapeRenderer_.drawLine(btn.x + iconPad, btn.y + btn.h - iconPad,
                                    btn.x + btn.w - iconPad, btn.y + btn.h - iconPad,
                                    iconColor, 2.0f);
            break;
        case ToolButton::Type::Cancel:
            shapeRenderer_.drawLine(btn.x + iconPad, btn.y + iconPad,
                                    btn.x + btn.w - iconPad, btn.y + btn.h - iconPad,
                                    iconColor, 2.2f);
            shapeRenderer_.drawLine(btn.x + btn.w - iconPad, btn.y + iconPad,
                                    btn.x + iconPad, btn.y + btn.h - iconPad,
                                    iconColor, 2.2f);
            break;
        case ToolButton::Type::Confirm:
            shapeRenderer_.drawLine(cx - 8.f, cy + 1.f,
                                    cx - 2.f, cy + 7.f,
                                    iconColor, 2.4f);
            shapeRenderer_.drawLine(cx - 2.f, cy + 7.f,
                                    cx + 10.f, cy - 7.f,
                                    iconColor, 2.4f);
            break;
        default:
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
// ── Long Screenshot ─────────────────────────────────────────────
std::vector<uint8_t> Application::cropPixels(const std::vector<uint8_t>& source,
                                             int sourceW,
                                             int sourceH,
                                             platform::Rect region) const {
    int sx = std::max(0, region.x);
    int sy = std::max(0, region.y);
    int sw = std::max(0, region.w);
    int sh = std::max(0, region.h);

    if (sx + sw > sourceW) sw = sourceW - sx;
    if (sy + sh > sourceH) sh = sourceH - sy;
    if (source.empty() || sw <= 0 || sh <= 0) {
        return {};
    }

    std::vector<uint8_t> result(static_cast<size_t>(sw) * sh * 4);
    for (int y = 0; y < sh; ++y) {
        const uint8_t* srcRow = source.data() +
            (static_cast<size_t>(sy + y) * sourceW + sx) * 4;
        uint8_t* dstRow = result.data() + static_cast<size_t>(y) * sw * 4;
        std::memcpy(dstRow, srcRow, static_cast<size_t>(sw) * 4);
    }
    return result;
}

int Application::trimTrailingCaptureDropout(std::vector<uint8_t>& pixels,
                                            int width,
                                            int height) const {
    if (width <= 0 || height <= 0 ||
        pixels.size() != static_cast<size_t>(width) * height * 4) {
        return height;
    }

    auto isDropoutRow = [&](int y) {
        const uint8_t* row = pixels.data() + static_cast<size_t>(y) * width * 4;
        int nearBlack = 0;
        int dark = 0;
        int colored = 0;
        int minLuma = 255;
        int maxLuma = 0;
        for (int x = 0; x < width; ++x) {
            const uint8_t r = row[x * 4 + 0];
            const uint8_t g = row[x * 4 + 1];
            const uint8_t b = row[x * 4 + 2];
            const int luma = (static_cast<int>(r) * 299 +
                              static_cast<int>(g) * 587 +
                              static_cast<int>(b) * 114) / 1000;
            if (r <= 8 && g <= 8 && b <= 8) {
                ++nearBlack;
            }
            if (luma <= 42) {
                ++dark;
            }
            if (r > 70 || g > 70 || b > 70) {
                ++colored;
            }
            minLuma = std::min(minLuma, luma);
            maxLuma = std::max(maxLuma, luma);
        }
        const bool almostBlack =
            nearBlack >= width * 95 / 100 && colored <= std::max(1, width / 200);
        const bool uniformDark =
            dark >= width * 96 / 100 &&
            (maxLuma - minLuma) <= 18 &&
            colored <= std::max(1, width / 100);
        return almostBlack || uniformDark;
    };

    int dropoutRows = 0;
    for (int y = height - 1; y >= 0 && isDropoutRow(y); --y) {
        ++dropoutRows;
    }

    const bool significantBand =
        dropoutRows >= 24 && dropoutRows >= std::max(1, height / 12);
    if (!significantBand || dropoutRows >= height - 32) {
        return height;
    }

    const int trimmedHeight = height - dropoutRows;
    pixels.resize(static_cast<size_t>(width) * trimmedHeight * 4);
    std::printf("[long] Trimmed %d trailing black capture rows (%dx%d -> %dx%d)\n",
                dropoutRows, width, height, width, trimmedHeight);
    return trimmedHeight;
}

platform::Rect Application::fitLongPreviewRect(int imageW, int imageH) const {
    if (longScreenshotSourceRegion_.w > 0 && longScreenshotSourceRegion_.h > 0) {
        return longScreenshotSourceRegion_;
    }

    const int viewportH = std::min(imageH, screenSize_.h - 148);
    return {
        (screenSize_.w - imageW) / 2,
        std::max(56, (screenSize_.h - viewportH) / 2 - 10),
        imageW,
        viewportH
    };
}

std::vector<platform::Rect> Application::longScreenshotOverlayRegions() const {
    std::vector<platform::Rect> regions;
    auto addPadded = [&](float x, float y, float w, float h, int pad) {
        const int ix = static_cast<int>(std::floor(x)) - pad;
        const int iy = static_cast<int>(std::floor(y)) - pad;
        const int iw = static_cast<int>(std::ceil(w)) + pad * 2;
        const int ih = static_cast<int>(std::ceil(h)) + pad * 2;
        if (iw > 0 && ih > 0) {
            regions.push_back({ ix, iy, iw, ih });
        }
    };

    if (toolbarW_ > 0.f && toolbarH_ > 0.f) {
        addPadded(toolbarX_, toolbarY_, toolbarW_, toolbarH_, 4);
    }

    const auto preview = longScreenshotSourceRegion_.w > 0 && longScreenshotSourceRegion_.h > 0
        ? longScreenshotSourceRegion_
        : stateMachine_.selectedRegion();
    if (preview.w <= 0 || preview.h <= 0 || capturedW_ <= 0 || capturedH_ <= 0) {
        return regions;
    }

    const float sw = static_cast<float>(screenSize_.w);
    const float sh = static_cast<float>(screenSize_.h);
    const float px = static_cast<float>(preview.x);
    const float py = static_cast<float>(preview.y);
    const float pw = static_cast<float>(preview.w);
    const float ph = static_cast<float>(preview.h);

    const float thumbMaxH = std::min(sh - 32.f, std::max(ph, ph + 96.f));
    const float thumbH = std::max(160.f, thumbMaxH);
    const float thumbW = std::clamp(
        thumbH * static_cast<float>(capturedW_) / std::max(1, capturedH_),
        72.f, 132.f);
    const float gap = 18.f;
    const float thumbX = px - thumbW - gap >= 8.f
        ? px - thumbW - gap
        : std::min(sw - thumbW - 8.f, px + pw + gap);
    const float thumbY = std::clamp(
        py + (ph - thumbH) * 0.5f,
        8.f,
        std::max(8.f, sh - thumbH - 8.f));
    addPadded(thumbX, thumbY, thumbW, thumbH, 6);

    const float hintH = 34.f;
    const bool hintAbove = py - hintH - 10.f >= 8.f;
    if (hintAbove) {
        const float hintW = longHintTextW_ > 0
            ? static_cast<float>(longHintTextW_ + 42)
            : 302.f;
        const float hintX = px + (pw - hintW) * 0.5f;
        const float hintY = py - hintH - 10.f;
        addPadded(hintX, hintY, hintW, hintH, 3);
    }

    return regions;
}

bool Application::ensureLongHintTexture() {
    if (longHintTextTexture_) {
        return true;
    }

#ifdef _WIN32
    const wchar_t* text = L"\u6EDA\u52A8\u9875\u9762\u622A\u53D6\u66F4\u591A\u5185\u5BB9";
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return false;

    HFONT font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    SIZE textSize = {};
    GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &textSize);
    longHintTextW_ = std::max(1, static_cast<int>(textSize.cx));
    longHintTextH_ = std::max(1, static_cast<int>(textSize.cy) + 2);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = longHintTextW_;
    bmi.bmiHeader.biHeight = -longHintTextH_;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        SelectObject(hdc, oldFont);
        DeleteObject(font);
        DeleteDC(hdc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);
    std::memset(bits, 0, static_cast<size_t>(longHintTextW_) * longHintTextH_ * 4);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    TextOutW(hdc, 0, 1, text, static_cast<int>(wcslen(text)));

    std::vector<uint8_t> rgba(static_cast<size_t>(longHintTextW_) * longHintTextH_ * 4);
    const uint8_t* bgra = static_cast<const uint8_t*>(bits);
    for (int y = 0; y < longHintTextH_; ++y) {
        for (int x = 0; x < longHintTextW_; ++x) {
            const size_t i = (static_cast<size_t>(y) * longHintTextW_ + x) * 4;
            const uint8_t alpha = std::max({ bgra[i + 0], bgra[i + 1], bgra[i + 2] });
            rgba[i + 0] = 255;
            rgba[i + 1] = 255;
            rgba[i + 2] = 255;
            rgba[i + 3] = alpha;
        }
    }

    SelectObject(hdc, oldBitmap);
    SelectObject(hdc, oldFont);
    DeleteObject(bitmap);
    DeleteObject(font);
    DeleteDC(hdc);

    glGenTextures(1, &longHintTextTexture_);
    glBindTexture(GL_TEXTURE_2D, longHintTextTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, longHintTextW_, longHintTextH_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return longHintTextTexture_ != 0;
#else
    return false;
#endif
}

bool Application::captureLongScreenshot() {
    writeLongScreenshotLog("session-start");
    if (isLongCaptureActive_) {
        std::printf("[long] Already in long screenshot mode\n");
        return false;
    }
    if (isLongScreenshotResult_) {
        std::printf("[long] Current image is already a long screenshot result\n");
        return false;
    }

    const auto selected = stateMachine_.selectedRegion();
    if (selected.w <= 0 || selected.h <= 0) {
        std::fprintf(stderr, "[long] Invalid selected region\n");
        return false;
    }

    auto firstFrame = cropPixels(capturedPixels_, capturedW_, capturedH_, selected);
    if (firstFrame.empty()) {
        std::fprintf(stderr, "[long] Failed to crop initial frame\n");
        return false;
    }

    longBackgroundPixels_ = capturedPixels_;
    longBackgroundW_ = capturedW_;
    longBackgroundH_ = capturedH_;

    platform_.eglContext->makeCurrent();
    if (capturedW_ > 0 && capturedH_ > 0 && !capturedPixels_.empty()) {
        if (longBackgroundTexture_ == 0) {
            glGenTextures(1, &longBackgroundTexture_);
        }
        glBindTexture(GL_TEXTURE_2D, longBackgroundTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, capturedW_, capturedH_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, capturedPixels_.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    core::LongScreenshotStitchOptions stitchOptions;
    stitchOptions.minAppendRows = std::max(24, selected.h / 20);
    stitchOptions.minOverlapRows = std::min(selected.h - 1, std::max(64, selected.h / 4));
    stitchOptions.maxOverlapRows = std::min(
        900,
        std::max(1, selected.h - stitchOptions.minAppendRows));
    stitchOptions.reliableMatchScore = 24.0f;
    stitchOptions.acceptableMatchScore = 15.5f;
    stitchOptions.ambiguousScoreGap = 2.0f;
    stitchOptions.acceptableScoreGap = 0.4f;
    stitchOptions.appendOnUnreliableMatch = true;
    longStitcher_ = core::LongScreenshotStitcher(selected.w, stitchOptions);
    longStitcher_.start(firstFrame, selected.h);

    capturedPixels_ = longStitcher_.pixels();
    capturedW_ = longStitcher_.width();
    capturedH_ = longStitcher_.height();
    isLongCaptureActive_ = true;
    isLongScreenshotResult_ = true;
    pendingLongFrameCapture_ = false;
    longNeedsTrailingFrameCapture_ = false;
    longLastAppendedScrollSeq_ = 0;
    longLastFrameCapture_ = std::chrono::steady_clock::now();
    longScreenshotSourceRegion_ = selected;
    annotations_.clear();
    commandHistory_.clear();

    if (!uploadScreenshotTextureFromPixels(longStitcher_.pixels(), capturedW_, capturedH_)) {
        std::fprintf(stderr, "[long] Failed to upload stitched screenshot texture\n");
        return false;
    }

    stateMachine_.setSelectedRegion(fitLongPreviewRect(capturedW_, capturedH_));
    buildToolbar();
    platform_.overlay->setPassthroughRegion(longScreenshotSourceRegion_,
                                            longScreenshotOverlayRegions());
    std::printf("[long] Entered manual long screenshot mode (%dx%d). Scroll to capture; click check to finish.\n",
                capturedW_, capturedH_);
    return true;
}

void Application::handleLongScreenshotScroll(float scrollDelta,
                                             platform::Point cursorPosition,
                                             bool nativePassthrough) {
    const auto started = std::chrono::steady_clock::now();
    if (!isLongCaptureActive_) {
        return;
    }

    const int wheelDelta = static_cast<int>(scrollDelta * 120.0f);
    if (wheelDelta >= 0) {
        return;
    }
    const uint64_t scrollSeq = ++longScrollEventSeq_;

    platform::Point scrollPoint = {
        screenBounds_.x + cursorPosition.x,
        screenBounds_.y + cursorPosition.y
    };
    const bool cursorInSource =
        cursorPosition.x >= longScreenshotSourceRegion_.x &&
        cursorPosition.x <= longScreenshotSourceRegion_.x + longScreenshotSourceRegion_.w &&
        cursorPosition.y >= longScreenshotSourceRegion_.y &&
        cursorPosition.y <= longScreenshotSourceRegion_.y + longScreenshotSourceRegion_.h;
    if (!cursorInSource) {
        scrollPoint = {
            screenBounds_.x + longScreenshotSourceRegion_.x + longScreenshotSourceRegion_.w / 2,
            screenBounds_.y + longScreenshotSourceRegion_.y + longScreenshotSourceRegion_.h / 2
        };
    }

    if (!nativePassthrough &&
        !platform_.input->scrollAt(scrollPoint,
                                   wheelDelta,
                                   platform_.overlay->getNativeHandle())) {
        std::fprintf(stderr, "[long] Manual scroll forwarding failed\n");
        writeLongScreenshotLog("scroll-forward failed elapsed_ms=%lld delta=%d point=%d,%d cursor=%d,%d",
                               elapsedMs(started), wheelDelta, scrollPoint.x, scrollPoint.y,
                               cursorPosition.x, cursorPosition.y);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const int captureDelayMs = nativePassthrough
        ? kLongNativePassthroughCaptureDelayMs
        : kLongCaptureDelayMs;
    const auto requestedDue = now + std::chrono::milliseconds(captureDelayMs);
    const auto trailingDue = now + std::chrono::milliseconds(kLongTrailingCaptureDelayMs);
    const auto intervalDue = longLastFrameCapture_ +
        std::chrono::milliseconds(kLongMinCaptureIntervalMs);
    const auto nextDue = std::max(requestedDue, intervalDue);
    const bool hadPendingCapture = pendingLongFrameCapture_;
    const auto scheduledDue = hadPendingCapture
        ? std::min(longFrameCaptureDue_, nextDue)
        : nextDue;

    longNeedsTrailingFrameCapture_ = true;
    longTrailingFrameCaptureDue_ = trailingDue;
    longPendingFrameScrollSeq_ = scrollSeq;
    longPendingFrameScrollAt_ = started;
    longFrameCaptureDue_ = scheduledDue;
    pendingLongFrameCapture_ = true;
    writeLongScreenshotLog("%s ok seq=%llu elapsed_ms=%lld delta=%d point=%d,%d cursor=%d,%d capture_due_ms=%lld trailing_due_ms=%d",
                           nativePassthrough ? "scroll-observed" : "scroll-forward",
                           static_cast<unsigned long long>(scrollSeq),
                           elapsedMs(started),
                           wheelDelta,
                           scrollPoint.x,
                           scrollPoint.y,
                           cursorPosition.x,
                           cursorPosition.y,
                           std::max(0LL, elapsedMs(now, scheduledDue)),
                           kLongTrailingCaptureDelayMs);
}

bool Application::appendLongScreenshotFrame() {
    const auto started = std::chrono::steady_clock::now();
    const uint64_t scrollSeq = longCurrentFrameScrollSeq_;
    const auto scrollAt = longCurrentFrameScrollAt_;
    if (!isLongCaptureActive_) {
        return false;
    }

    GLint glMaxTextureSize = kLongMaxOutputHeight;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glMaxTextureSize);
    const int maxOutputHeight = std::max(
        longScreenshotSourceRegion_.h,
        std::min(kLongMaxOutputHeight, static_cast<int>(glMaxTextureSize)));
    if (longStitcher_.height() + longScreenshotSourceRegion_.h >= maxOutputHeight) {
        std::printf("[long] Reached max output height (%d px)\n", maxOutputHeight);
        return false;
    }

    auto tryAppendFrame = [&](const std::vector<uint8_t>& framePixels,
                              int frameW,
                              int frameH,
                              const char* sourceLabel) {
        if (frameW != longScreenshotSourceRegion_.w ||
            framePixels.size() != static_cast<size_t>(frameW) * frameH * 4) {
            std::fprintf(stderr, "[long] Invalid %s frame size (%dx%d, expected %dx%d)\n",
                         sourceLabel, frameW, frameH,
                         longScreenshotSourceRegion_.w, longScreenshotSourceRegion_.h);
            return false;
        }
        if (frameH < std::max(64, longScreenshotSourceRegion_.h / 3)) {
            std::fprintf(stderr, "[long] Ignoring too-short %s frame (%dx%d)\n",
                         sourceLabel, frameW, frameH);
            return false;
        }
        if (scrollSeq != 0 && longLastAppendedScrollSeq_ == scrollSeq) {
            writeLongScreenshotLog("append skipped seq=%llu event_to_skip_ms=%lld source=%s reason=same-scroll-already-appended total_ms=%lld frame=%dx%d",
                                   static_cast<unsigned long long>(scrollSeq),
                                   elapsedMs(scrollAt),
                                   sourceLabel,
                                   elapsedMs(started),
                                   frameW,
                                   frameH);
            return false;
        }

        const auto stitchStarted = std::chrono::steady_clock::now();
        const bool allowAcceptableMatch = scrollSeq == 0 ||
            longLastAppendedScrollSeq_ != scrollSeq;
        auto stitch = longStitcher_.append(framePixels, frameH, allowAcceptableMatch);
        const auto stitchMs = elapsedMs(stitchStarted);

        if (!stitch.appended) {
            if (!stitch.duplicate) {
                std::printf("[long] Ignored %s frame overlap=%d score=%.2f\n",
                            sourceLabel, stitch.overlapRows, stitch.score);
            }
            writeLongScreenshotLog("append ignored seq=%llu event_to_append_ms=%lld source=%s duplicate=%d reliable=%d allow_acceptable=%d overlap=%d score=%.2f second=%.2f gap=%.2f stitch_ms=%lld total_ms=%lld frame=%dx%d",
                                   static_cast<unsigned long long>(scrollSeq),
                                   scrollSeq ? elapsedMs(scrollAt) : -1,
                                   sourceLabel,
                                   stitch.duplicate,
                                   stitch.reliable,
                                   allowAcceptableMatch,
                                   stitch.overlapRows,
                                   stitch.score,
                                   stitch.secondBestScore,
                                   stitch.secondBestScore - stitch.score,
                                   stitchMs,
                                   elapsedMs(started),
                                   frameW,
                                   frameH);
            return false;
        }

        capturedW_ = longStitcher_.width();
        capturedH_ = longStitcher_.height();
        capturedPixels_ = longStitcher_.pixels();
        longLastAppendedScrollSeq_ = scrollSeq;
        const auto uploadStarted = std::chrono::steady_clock::now();
        uploadScreenshotTextureFromPixels(longStitcher_.pixels(), capturedW_, capturedH_);
        const auto uploadMs = elapsedMs(uploadStarted);
        stateMachine_.setSelectedRegion(fitLongPreviewRect(capturedW_, capturedH_));
        buildToolbar();
        platform_.overlay->setPassthroughRegion(longScreenshotSourceRegion_,
                                                longScreenshotOverlayRegions());
        writeLongScreenshotLog("append ok seq=%llu event_to_append_ms=%lld source=%s reliable=%d allow_acceptable=%d appended_rows=%d overlap=%d score=%.2f second=%.2f gap=%.2f stitch_ms=%lld upload_ms=%lld total_ms=%lld output=%dx%d frame=%dx%d",
                               static_cast<unsigned long long>(scrollSeq),
                               scrollSeq ? elapsedMs(scrollAt) : -1,
                               sourceLabel,
                               stitch.reliable,
                               allowAcceptableMatch,
                               stitch.appendedRows,
                               stitch.overlapRows,
                               stitch.score,
                               stitch.secondBestScore,
                               stitch.secondBestScore - stitch.score,
                               stitchMs,
                               uploadMs,
                               elapsedMs(started),
                               capturedW_,
                               capturedH_,
                               frameW,
                               frameH);
        return true;
    };

    std::vector<uint8_t> framePixels;
    int frameW = 0, frameH = 0;

    bool captured = false;
#ifdef _WIN32
    captured = captureLongFramePixelsFromCoveredWindow(
        framePixels, frameW, frameH, longScreenshotSourceRegion_);
    if (!captured) {
        std::fprintf(stderr, "[long] Covered-window capture failed; keeping overlay visible\n");
    }
#endif

    if (!captured) {
        std::fprintf(stderr, "[long] Failed to capture user-scrolled frame\n");
        return false;
    }
    frameH = trimTrailingCaptureDropout(framePixels, frameW, frameH);

    return tryAppendFrame(framePixels, frameW, frameH, "hidden-overlay");
}

void Application::finishLongScreenshotMode() {
    if (!isLongCaptureActive_) {
        return;
    }

    isLongCaptureActive_ = false;
    pendingLongFrameCapture_ = false;
    longNeedsTrailingFrameCapture_ = false;
    platform_.overlay->setPassthroughRegion(std::nullopt);
    buildToolbar();
    std::printf("[long] Manual long screenshot finished (%dx%d)\n",
                capturedW_, capturedH_);
}

void Application::runPendingActions() {
    if (pendingLongScreenshot_) {
        pendingLongScreenshot_ = false;
        if (stateMachine_.currentState() != core::AppState::Annotating) {
            std::printf("[long] Ignoring pending long screenshot outside annotating state\n");
            return;
        }

        if (captureLongScreenshot()) {
            render();
        }
    }

    if (!pendingLongFrameCapture_) {
        return;
    }
    if (!isLongCaptureActive_ ||
        stateMachine_.currentState() != core::AppState::Annotating) {
        pendingLongFrameCapture_ = false;
        longNeedsTrailingFrameCapture_ = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < longFrameCaptureDue_) {
        return;
    }

#ifdef _WIN32
    POINT cursor = {};
    if (GetCursorPos(&cursor)) {
        const float cx = static_cast<float>(cursor.x - screenBounds_.x);
        const float cy = static_cast<float>(cursor.y - screenBounds_.y);
        constexpr float toolbarGuard = 8.f;
        if (cx >= toolbarX_ - toolbarGuard &&
            cx <= toolbarX_ + toolbarW_ + toolbarGuard &&
            cy >= toolbarY_ - toolbarGuard &&
            cy <= toolbarY_ + toolbarH_ + toolbarGuard) {
            longFrameCaptureDue_ = now + std::chrono::milliseconds(80);
            return;
        }
    }
#endif

    pendingLongFrameCapture_ = false;
    const bool scheduleTrailingCapture = longNeedsTrailingFrameCapture_;
    const auto trailingDue = longTrailingFrameCaptureDue_;
    longNeedsTrailingFrameCapture_ = false;
    longLastFrameCapture_ = now;
    const uint64_t scrollSeq = longPendingFrameScrollSeq_;
    const auto scrollAt = longPendingFrameScrollAt_;
    longCurrentFrameScrollSeq_ = scrollSeq;
    longCurrentFrameScrollAt_ = scrollAt;
    writeLongScreenshotLog("capture-due seq=%llu event_to_due_ms=%lld schedule_trailing=%d",
                           static_cast<unsigned long long>(scrollSeq),
                           scrollSeq ? elapsedMs(scrollAt, now) : -1,
                           scheduleTrailingCapture);
    const bool appended = appendLongScreenshotFrame();
    if (appended) {
        const auto renderStarted = std::chrono::steady_clock::now();
        render();
        writeLongScreenshotLog("render-after-append seq=%llu event_to_render_ms=%lld render_ms=%lld",
                               static_cast<unsigned long long>(scrollSeq),
                               scrollSeq ? elapsedMs(scrollAt) : -1,
                               elapsedMs(renderStarted));
    }

    if (isLongCaptureActive_ && scheduleTrailingCapture && !appended) {
        const auto afterCapture = std::chrono::steady_clock::now();
        const auto requestedDue = afterCapture + std::chrono::milliseconds(kLongCaptureDelayMs);
        const auto intervalDue = longLastFrameCapture_ +
            std::chrono::milliseconds(kLongMinCaptureIntervalMs);
        longFrameCaptureDue_ = std::max({ requestedDue, intervalDue, trailingDue });
        pendingLongFrameCapture_ = true;
    } else if (isLongCaptureActive_ && scheduleTrailingCapture && appended) {
        writeLongScreenshotLog("trailing-skip seq=%llu reason=already-appended",
                               static_cast<unsigned long long>(scrollSeq));
    }
}

void Application::resetCaptureSession() {
    if (platform_.overlay) {
        platform_.overlay->setPassthroughRegion(std::nullopt);
    }
    if (platform_.eglContext) {
        platform_.eglContext->makeCurrent();
    }
    if (screenshotTexture_) {
        glDeleteTextures(1, &screenshotTexture_);
        screenshotTexture_ = 0;
    }
    if (longBackgroundTexture_) {
        glDeleteTextures(1, &longBackgroundTexture_);
        longBackgroundTexture_ = 0;
    }

    isLongScreenshotResult_ = false;
    isLongCaptureActive_ = false;
    pendingLongScreenshot_ = false;
    pendingLongFrameCapture_ = false;
    longNeedsTrailingFrameCapture_ = false;
    longScrollEventSeq_ = 0;
    longPendingFrameScrollSeq_ = 0;
    longCurrentFrameScrollSeq_ = 0;
    longLastAppendedScrollSeq_ = 0;
    longScreenshotSourceRegion_ = {};
    longStitcher_.reset(0);
    longBackgroundPixels_.clear();
    longBackgroundW_ = 0;
    longBackgroundH_ = 0;
    capturedPixels_.clear();
    capturedW_ = 0;
    capturedH_ = 0;
    annotations_.clear();
    commandHistory_.clear();
    isDragging_ = false;
    isDrawingAnnotation_ = false;
    activeFreehandPoints_.clear();
    toolButtons_.clear();
}

std::vector<uint8_t> Application::renderSelectionToPixels() {
    if (isLongScreenshotResult_) {
        return longStitcher_.pixels();
    }

    auto sel = stateMachine_.selectedRegion();
    auto result = cropPixels(capturedPixels_, capturedW_, capturedH_, sel);

    // Draw annotations onto the pixel buffer
    // For MVP, we render annotations by rendering to FBO and reading back.
    // But that's complex — for now, return just the screenshot region.
    // Annotations will be rendered in a future phase when FBO readback is wired.

    return result;
}

bool Application::saveToClipboard() {
    auto sel = stateMachine_.selectedRegion();
    auto pixels = renderSelectionToPixels();
    int outW = isLongScreenshotResult_ ? capturedW_ : sel.w;
    int outH = isLongScreenshotResult_ ? capturedH_ : sel.h;

    if (pixels.empty()) {
        std::fprintf(stderr, "[save] No pixels to copy\n");
        return false;
    }

    stateMachine_.transition(core::AppEvent::Escape);
    platform_.overlay->hide();
    resetCaptureSession();

    if (!platform_.clipboard->writeImage(pixels.data(), outW, outH)) {
        std::fprintf(stderr, "[save] Clipboard write failed\n");
        return false;
    }

    std::printf("[save] Copied to clipboard (%dx%d)\n", outW, outH);
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
    int outW = isLongScreenshotResult_ ? capturedW_ : sel.w;
    int outH = isLongScreenshotResult_ ? capturedH_ : sel.h;

    if (pixels.empty()) {
        std::fprintf(stderr, "[save] No pixels to save\n");
        return false;
    }

    int ok = stbi_write_png(path.c_str(), outW, outH, 4,
                             pixels.data(), outW * 4);
    if (!ok) {
        std::fprintf(stderr, "[save] Failed to write PNG: %s\n", path.c_str());
        return false;
    }

    std::printf("[save] Saved to: %s (%dx%d)\n", path.c_str(), outW, outH);

    // After save, hide overlay and return to idle
    stateMachine_.transition(core::AppEvent::Escape);
    platform_.overlay->hide();
    resetCaptureSession();
    return true;
}

} // namespace sst::app
