#include "Win32WindowEnumerator.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace sst::platform::win32 {

BOOL CALLBACK Win32WindowEnumerator::enumCallback(HWND hwnd, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE; // minimized

    // Filter out cloaked windows (UWP hidden windows)
    BOOL isCloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
    if (isCloaked) return TRUE;

    // Get the actual visual bounds (excluding invisible borders on Win10+)
    RECT extFrame;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                       &extFrame, sizeof(extFrame));
    if (FAILED(hr)) {
        GetWindowRect(hwnd, &extFrame);
    }

    WindowInfo info;
    info.handle    = reinterpret_cast<uintptr_t>(hwnd);
    info.rect.x    = extFrame.left;
    info.rect.y    = extFrame.top;
    info.rect.w    = extFrame.right - extFrame.left;
    info.rect.h    = extFrame.bottom - extFrame.top;
    info.isVisible = true;

    // Get window title
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    // Convert wchar_t to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        info.title.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, title, -1, info.title.data(), len, nullptr, nullptr);
    }

    list->push_back(info);
    return TRUE;
}

std::vector<WindowInfo> Win32WindowEnumerator::enumerate() const {
    std::vector<WindowInfo> windows;
    EnumWindows(enumCallback, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

WindowInfo Win32WindowEnumerator::windowAtPoint(Point pt) const {
    auto windows = enumerate();
    // Return the first window (topmost Z-order) containing the point
    for (const auto& w : windows) {
        if (pt.x >= w.rect.x && pt.x < w.rect.x + w.rect.w &&
            pt.y >= w.rect.y && pt.y < w.rect.y + w.rect.h) {
            return w;
        }
    }
    return {}; // empty
}

} // namespace sst::platform::win32
