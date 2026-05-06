#include "Win32Overlay.h"
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <cstdio>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace sst::platform::win32 {

bool Win32Overlay::classRegistered_ = false;

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
    if (hwnd_) {
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

LRESULT Win32Overlay::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!mouseCallback_) break;

        MouseEvent evt;
        evt.position.x = GET_X_LPARAM(lp);
        evt.position.y = GET_Y_LPARAM(lp);
        evt.type = MouseEvent::Type::Move;
        if (wp & MK_SHIFT)   evt.modifiers |= static_cast<uint8_t>(KeyModifier::Shift);
        if (wp & MK_CONTROL) evt.modifiers |= static_cast<uint8_t>(KeyModifier::Ctrl);
        mouseCallback_(evt);
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
        case WM_LBUTTONDOWN: evt.type = MouseEvent::Type::Press;   evt.button = MouseButton::Left;   break;
        case WM_LBUTTONUP:   evt.type = MouseEvent::Type::Release; evt.button = MouseButton::Left;   break;
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
