#include "Win32Overlay.h"
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace sst::platform::win32 {

bool Win32Overlay::classRegistered_ = false;
Win32Overlay* Win32Overlay::mouseHookOwner_ = nullptr;

Win32Overlay::Win32Overlay() = default;

Win32Overlay::~Win32Overlay() {
    destroy();
}

bool Win32Overlay::create(const Rect& bounds) {
    if (!classRegistered_) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wndProc;
        wc.hInstance      = GetModuleHandle(nullptr);
        wc.hCursor        = LoadCursor(nullptr, IDC_CROSS);
        wc.lpszClassName  = kClassName;
        if (!RegisterClassExW(&wc)) return false;
        classRegistered_ = true;
    }

    // Create a borderless, topmost, tool window covering the given bounds.
    // WS_EX_TOOLWINDOW prevents it from appearing in the taskbar.
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

    hwnd_ = CreateWindowExW(
        exStyle,
        kClassName,
        L"Screenshot",
        WS_POPUP,
        bounds.x, bounds.y, bounds.w, bounds.h,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this  // pass 'this' to WM_NCCREATE
    );

    if (!hwnd_) return false;

    // Keep the overlay capturable. Excluding it with SetWindowDisplayAffinity
    // breaks remote-control viewers and other screenshot tools, which see a
    // black fullscreen window instead of the composed overlay.
    std::printf("[overlay] Capture affinity left at default\n");


    size_ = { bounds.w, bounds.h };

    // Query DPI for this monitor
    HMONITOR hMon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpiScale_ = static_cast<float>(dpiX) / 96.0f;

    return true;
}

void Win32Overlay::show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetForegroundWindow(hwnd_);
    }
}

void Win32Overlay::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void* Win32Overlay::getNativeHandle() const {
    return hwnd_;
}

Size Win32Overlay::getSize() const {
    return size_;
}

float Win32Overlay::getDpiScale() const {
    return dpiScale_;
}

void Win32Overlay::setPassthroughRegion(std::optional<Rect> region,
                                        std::vector<Rect> overlayRegions) {
    if (region && (region->w <= 0 || region->h <= 0)) {
        region.reset();
    }

    passthroughRegion_ = region;
    overlayRegions_ = std::move(overlayRegions);
    applyWindowRegion();
    updateMouseHook();
}

void Win32Overlay::setMouseCallback(std::function<void(const MouseEvent&)> cb) {
    mouseCallback_ = std::move(cb);
}

void Win32Overlay::setKeyCallback(std::function<void(const KeyEvent&)> cb) {
    keyCallback_ = std::move(cb);
}

bool Win32Overlay::pumpMessages() {
    MSG msg;
    bool processed = false;

    do {
        processed = false;

        while (PeekMessage(&msg, nullptr, 0, WM_HOTKEY - 1, PM_REMOVE)) {
            processed = true;
            if (msg.message == WM_QUIT) {
                quitRequested_ = true;
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        while (PeekMessage(&msg, nullptr, WM_HOTKEY + 1, static_cast<UINT>(-1), PM_REMOVE)) {
            processed = true;
            if (msg.message == WM_QUIT) {
                quitRequested_ = true;
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    } while (processed);

    return !quitRequested_;
}

void Win32Overlay::destroy() {
    passthroughRegion_.reset();
    updateMouseHook();

    if (hwnd_) {
        SetWindowRgn(hwnd_, nullptr, FALSE);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK Win32Overlay::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Win32Overlay* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<Win32Overlay*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Win32Overlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handleMessage(msg, wp, lp);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Win32Overlay::lowLevelMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (mouseHookOwner_) {
        return mouseHookOwner_->handleLowLevelMouse(code, wp, lp);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool Win32Overlay::pointInPassthroughRegion(int x, int y) const {
    if (!passthroughRegion_) {
        return false;
    }

    constexpr int kVisibleBorder = 2;
    const Rect& rect = *passthroughRegion_;
    const int left = rect.x + kVisibleBorder;
    const int top = rect.y + kVisibleBorder;
    const int right = rect.x + rect.w - kVisibleBorder;
    const int bottom = rect.y + rect.h - kVisibleBorder;
    return x >= left && y >= top &&
           x < right &&
           y < bottom;
}

void Win32Overlay::applyWindowRegion() {
    if (!hwnd_) {
        return;
    }

    if (!passthroughRegion_) {
        overlayRegions_.clear();
        SetWindowRgn(hwnd_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    constexpr int kVisibleBorder = 2;
    HRGN compactRegion = CreateRectRgn(0, 0, 0, 0);
    if (!compactRegion) {
        return;
    }

    auto addRect = [&](int x, int y, int w, int h) {
        const int left = std::clamp(x, 0, size_.w);
        const int top = std::clamp(y, 0, size_.h);
        const int right = std::clamp(x + w, 0, size_.w);
        const int bottom = std::clamp(y + h, 0, size_.h);
        if (right <= left || bottom <= top) {
            return;
        }

        HRGN rectRegion = CreateRectRgn(left, top, right, bottom);
        if (!rectRegion) {
            return;
        }
        CombineRgn(compactRegion, compactRegion, rectRegion, RGN_OR);
        DeleteObject(rectRegion);
    };

    const Rect& hole = *passthroughRegion_;
    addRect(hole.x - kVisibleBorder,
            hole.y - kVisibleBorder,
            hole.w + kVisibleBorder * 2,
            kVisibleBorder);
    addRect(hole.x - kVisibleBorder,
            hole.y + hole.h,
            hole.w + kVisibleBorder * 2,
            kVisibleBorder);
    addRect(hole.x - kVisibleBorder,
            hole.y,
            kVisibleBorder,
            hole.h);
    addRect(hole.x + hole.w,
            hole.y,
            kVisibleBorder,
            hole.h);

    for (const Rect& rect : overlayRegions_) {
        addRect(rect.x, rect.y, rect.w, rect.h);
    }

    if (SetWindowRgn(hwnd_, compactRegion, TRUE) == 0) {
        DeleteObject(compactRegion);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void Win32Overlay::updateMouseHook() {
    if (passthroughRegion_ && !mouseHook_) {
        mouseHookOwner_ = this;
        mouseHook_ = SetWindowsHookExW(
            WH_MOUSE_LL,
            lowLevelMouseProc,
            GetModuleHandle(nullptr),
            0);
        if (!mouseHook_) {
            std::fprintf(stderr, "[overlay] Failed to install mouse hook for passthrough region\n");
            if (mouseHookOwner_ == this) {
                mouseHookOwner_ = nullptr;
            }
        }
        return;
    }

    if (!passthroughRegion_ && mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
        if (mouseHookOwner_ == this) {
            mouseHookOwner_ = nullptr;
        }
    }
}

LRESULT Win32Overlay::handleLowLevelMouse(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION &&
        wp == WM_MOUSEWHEEL &&
        mouseCallback_ &&
        passthroughRegion_) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        POINT clientPoint = mouse->pt;
        ScreenToClient(hwnd_, &clientPoint);

        if (pointInPassthroughRegion(clientPoint.x, clientPoint.y)) {
            MouseEvent evt;
            evt.type = MouseEvent::Type::Scroll;
            evt.position.x = clientPoint.x;
            evt.position.y = clientPoint.y;
            evt.scrollDelta = static_cast<SHORT>(HIWORD(mouse->mouseData)) / 120.0f;
            evt.nativePassthrough = true;
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                evt.modifiers |= static_cast<uint8_t>(KeyModifier::Shift);
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                evt.modifiers |= static_cast<uint8_t>(KeyModifier::Ctrl);
            }
            mouseCallback_(evt);
        }
    }

    return CallNextHookEx(mouseHook_, code, wp, lp);
}

void Win32Overlay::rememberMouseMovePoint(int clientX, int clientY, DWORD time) {
    POINT screenPoint = { clientX, clientY };
    ClientToScreen(hwnd_, &screenPoint);
    lastMouseMoveScreen_ = screenPoint;
    lastMouseMoveTime_ = time;
    hasLastMouseMovePoint_ = true;
}

void Win32Overlay::dispatchMouseMoveWithHistory(WPARAM wp, LPARAM lp) {
    if (!mouseCallback_) {
        return;
    }

    const int clientX = GET_X_LPARAM(lp);
    const int clientY = GET_Y_LPARAM(lp);
    const DWORD messageTime = GetMessageTime();

    MouseEvent baseEvent;
    baseEvent.type = MouseEvent::Type::Move;
    baseEvent.position.x = clientX;
    baseEvent.position.y = clientY;
    if (wp & MK_SHIFT)   baseEvent.modifiers |= static_cast<uint8_t>(KeyModifier::Shift);
    if (wp & MK_CONTROL) baseEvent.modifiers |= static_cast<uint8_t>(KeyModifier::Ctrl);

    if (!(wp & MK_LBUTTON) || !hasLastMouseMovePoint_ || !hasHandledDragMove_) {
        mouseCallback_(baseEvent);
        rememberMouseMovePoint(clientX, clientY, messageTime);
        hasHandledDragMove_ = (wp & MK_LBUTTON) != 0;
        return;
    }

    POINT screenPoint = { clientX, clientY };
    ClientToScreen(hwnd_, &screenPoint);

    MOUSEMOVEPOINT query = {};
    query.x = screenPoint.x;
    query.y = screenPoint.y;
    query.time = messageTime;

    std::array<MOUSEMOVEPOINT, 64> history = {};
    const int count = GetMouseMovePointsEx(sizeof(MOUSEMOVEPOINT),
                                           &query,
                                           history.data(),
                                           static_cast<int>(history.size()),
                                           GMMP_USE_DISPLAY_POINTS);
    if (count <= 0) {
        mouseCallback_(baseEvent);
        rememberMouseMovePoint(clientX, clientY, messageTime);
        return;
    }

    std::vector<MOUSEMOVEPOINT> points;
    points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (history[i].time < lastMouseMoveTime_) {
            break;
        }
        const bool reachedLast =
            history[i].time == lastMouseMoveTime_ &&
            history[i].x == lastMouseMoveScreen_.x &&
            history[i].y == lastMouseMoveScreen_.y;
        if (reachedLast) {
            break;
        }
        points.push_back(history[i]);
    }

    if (points.empty()) {
        mouseCallback_(baseEvent);
        rememberMouseMovePoint(clientX, clientY, messageTime);
        return;
    }

    for (auto it = points.rbegin(); it != points.rend(); ++it) {
        POINT clientPoint = { it->x, it->y };
        ScreenToClient(hwnd_, &clientPoint);

        MouseEvent evt = baseEvent;
        evt.position.x = clientPoint.x;
        evt.position.y = clientPoint.y;
        mouseCallback_(evt);
    }

    lastMouseMoveScreen_ = screenPoint;
    lastMouseMoveTime_ = messageTime;
    hasLastMouseMovePoint_ = true;
    hasHandledDragMove_ = true;
}

LRESULT Win32Overlay::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        dispatchMouseMoveWithHistory(wp, lp);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL: {
        if (!mouseCallback_) break;

        MouseEvent evt;
        evt.position.x = GET_X_LPARAM(lp);
        evt.position.y = GET_Y_LPARAM(lp);

        if (wp & MK_SHIFT)   evt.modifiers |= static_cast<uint8_t>(KeyModifier::Shift);
        if (wp & MK_CONTROL) evt.modifiers |= static_cast<uint8_t>(KeyModifier::Ctrl);

        switch (msg) {
        case WM_LBUTTONDOWN:
            evt.type = MouseEvent::Type::Press;
            evt.button = MouseButton::Left;
            SetCapture(hwnd_);
            rememberMouseMovePoint(evt.position.x, evt.position.y, GetMessageTime());
            hasHandledDragMove_ = false;
            break;
        case WM_LBUTTONUP:
            evt.type = MouseEvent::Type::Release;
            evt.button = MouseButton::Left;
            if (GetCapture() == hwnd_) {
                ReleaseCapture();
            }
            hasLastMouseMovePoint_ = false;
            hasHandledDragMove_ = false;
            break;
        case WM_RBUTTONDOWN: evt.type = MouseEvent::Type::Press;   evt.button = MouseButton::Right;  break;
        case WM_RBUTTONUP:   evt.type = MouseEvent::Type::Release; evt.button = MouseButton::Right;  break;
        case WM_MBUTTONDOWN: evt.type = MouseEvent::Type::Press;   evt.button = MouseButton::Middle; break;
        case WM_MBUTTONUP:   evt.type = MouseEvent::Type::Release; evt.button = MouseButton::Middle; break;
        case WM_MOUSEWHEEL:
            evt.type = MouseEvent::Type::Scroll;
            evt.scrollDelta = GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
            // WM_MOUSEWHEEL coords are screen-relative; convert to client
            POINT pt = { evt.position.x, evt.position.y };
            ScreenToClient(hwnd_, &pt);
            evt.position.x = pt.x;
            evt.position.y = pt.y;
            break;
        }

        mouseCallback_(evt);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_KEYUP: {
        if (!keyCallback_) break;

        KeyEvent evt;
        evt.type     = (msg == WM_KEYDOWN) ? KeyEvent::Type::Press : KeyEvent::Type::Release;
        evt.keyCode  = static_cast<uint32_t>(wp);
        evt.scanCode = (lp >> 16) & 0xFF;

        if (GetKeyState(VK_SHIFT)   & 0x8000) evt.modifiers |= static_cast<uint8_t>(KeyModifier::Shift);
        if (GetKeyState(VK_CONTROL) & 0x8000) evt.modifiers |= static_cast<uint8_t>(KeyModifier::Ctrl);
        if (GetKeyState(VK_MENU)    & 0x8000) evt.modifiers |= static_cast<uint8_t>(KeyModifier::Alt);

        keyCallback_(evt);
        return 0;
    }

    case WM_CHAR: {
        if (!keyCallback_) break;

        KeyEvent evt;
        evt.type      = KeyEvent::Type::Press;
        evt.character = static_cast<char32_t>(wp);
        keyCallback_(evt);
        return 0;
    }

    case WM_CLOSE:
        quitRequested_ = true;
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProc(hwnd_, msg, wp, lp);
}

} // namespace sst::platform::win32
