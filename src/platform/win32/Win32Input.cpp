#include "Win32Input.h"
#include <cstdio>
#include <cstdarg>

#ifndef SST_LOG_DIR
#define SST_LOG_DIR "logs"
#endif

namespace sst::platform::win32 {

namespace {

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

}

Win32Input::Win32Input() = default;

Win32Input::~Win32Input() {
    for (auto& [id, _] : callbacks_) {
        UnregisterHotKey(nullptr, static_cast<int>(id));
    }
}

HotkeyId Win32Input::registerHotkey(uint32_t keyCode, uint8_t modifiers,
                                    HotkeyCallback callback) {
    HotkeyId id = nextId_++;

    // Map our KeyModifier flags to Win32 MOD_ flags
    UINT winMods = 0;
    if (modifiers & static_cast<uint8_t>(KeyModifier::Shift)) winMods |= MOD_SHIFT;
    if (modifiers & static_cast<uint8_t>(KeyModifier::Ctrl))  winMods |= MOD_CONTROL;
    if (modifiers & static_cast<uint8_t>(KeyModifier::Alt))   winMods |= MOD_ALT;
    winMods |= MOD_NOREPEAT;

    if (!RegisterHotKey(nullptr, static_cast<int>(id), winMods, keyCode)) {
        std::fprintf(stderr, "[input] RegisterHotKey failed (key=%u mods=0x%X error=%lu)\n",
                     keyCode, winMods, GetLastError());
        return 0;
    }

    callbacks_[id] = std::move(callback);
    return id;
}

void Win32Input::unregisterHotkey(HotkeyId id) {
    UnregisterHotKey(nullptr, static_cast<int>(id));
    callbacks_.erase(id);
}

void Win32Input::pollHotkeys() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
        auto it = callbacks_.find(static_cast<HotkeyId>(msg.wParam));
        if (it != callbacks_.end()) {
            std::printf("[input] Hotkey triggered (id=%u)\n",
                        static_cast<unsigned>(msg.wParam));
            it->second();
        } else {
            std::fprintf(stderr, "[input] Ignoring unknown hotkey id=%u\n",
                         static_cast<unsigned>(msg.wParam));
        }
    }
}

HWND Win32Input::windowFromPointExcluding(Point screenPosition,
                                          HWND ignoredWindow) const {
    POINT pt = { screenPosition.x, screenPosition.y };

    for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        if (hwnd == ignoredWindow || GetAncestor(hwnd, GA_ROOT) == ignoredWindow) {
            continue;
        }
        if (!IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd)) {
            continue;
        }

        RECT rc = {};
        if (!GetWindowRect(hwnd, &rc) || !PtInRect(&rc, pt)) {
            continue;
        }

        POINT clientPt = pt;
        ScreenToClient(hwnd, &clientPt);
        HWND child = ChildWindowFromPointEx(
            hwnd,
            clientPt,
            CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        return child ? child : hwnd;
    }

    return nullptr;
}

bool Win32Input::scrollAt(Point screenPosition,
                          int wheelDelta,
                          void* ignoredWindow) {
    const ULONGLONG started = GetTickCount64();
    POINT pt = { screenPosition.x, screenPosition.y };
    HWND target = WindowFromPoint(pt);
    HWND ignored = static_cast<HWND>(ignoredWindow);
    if (ignored && (target == ignored || GetAncestor(target, GA_ROOT) == ignored)) {
        target = windowFromPointExcluding(screenPosition, ignored);
    }
    if (!target) {
        if (lastScrollTarget_ && IsWindow(lastScrollTarget_)) {
            target = lastScrollTarget_;
        } else {
            std::fprintf(stderr, "[input] scrollAt failed: no window at %d,%d\n",
                         screenPosition.x, screenPosition.y);
            writeLongScreenshotLog("scrollAt failed stage=no-target elapsed_ms=%llu point=%d,%d delta=%d",
                                   GetTickCount64() - started,
                                   screenPosition.x,
                                   screenPosition.y,
                                   wheelDelta);
            return false;
        }
    }

    lastScrollTarget_ = target;

    HWND root = GetAncestor(target, GA_ROOT);
    if (root) {
        SetForegroundWindow(root);
    }

    const WPARAM wp = MAKEWPARAM(0, static_cast<SHORT>(wheelDelta));
    const LPARAM lp = MAKELPARAM(
        static_cast<SHORT>(screenPosition.x),
        static_cast<SHORT>(screenPosition.y));

    if (ignored) {
        const BOOL posted = PostMessageW(target, WM_MOUSEWHEEL, wp, lp);
        writeLongScreenshotLog("scrollAt post result=%d elapsed_ms=%llu point=%d,%d delta=%d target=0x%p root=0x%p ignored=0x%p",
                               posted != FALSE,
                               GetTickCount64() - started,
                               screenPosition.x,
                               screenPosition.y,
                               wheelDelta,
                               target,
                               root,
                               ignored);
        return posted != FALSE;
    }

    if (!ignored) {
        POINT oldPos;
        GetCursorPos(&oldPos);
        SetCursorPos(screenPosition.x, screenPosition.y);

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(wheelDelta);

        const bool sent = SendInput(1, &input, sizeof(INPUT)) == 1;
        SetCursorPos(oldPos.x, oldPos.y);
        if (sent) {
            writeLongScreenshotLog("scrollAt sendinput result=1 elapsed_ms=%llu point=%d,%d delta=%d target=0x%p root=0x%p",
                                   GetTickCount64() - started,
                                   screenPosition.x,
                                   screenPosition.y,
                                   wheelDelta,
                                   target,
                                   root);
            return true;
        }
    }

    DWORD_PTR messageResult = 0;
    const LRESULT delivered = SendMessageTimeoutW(
        target,
        WM_MOUSEWHEEL,
        wp,
        lp,
        SMTO_ABORTIFHUNG | SMTO_NORMAL,
        80,
        &messageResult);
    if (delivered != 0) {
        writeLongScreenshotLog("scrollAt sendmessage result=1 elapsed_ms=%llu point=%d,%d delta=%d target=0x%p root=0x%p",
                               GetTickCount64() - started,
                               screenPosition.x,
                               screenPosition.y,
                               wheelDelta,
                               target,
                               root);
        return true;
    }

    const BOOL posted = PostMessageW(target, WM_MOUSEWHEEL, wp, lp);
    writeLongScreenshotLog("scrollAt fallback-post result=%d elapsed_ms=%llu point=%d,%d delta=%d target=0x%p root=0x%p",
                           posted != FALSE,
                           GetTickCount64() - started,
                           screenPosition.x,
                           screenPosition.y,
                           wheelDelta,
                           target,
                           root);
    return posted != FALSE;
}

} // namespace sst::platform::win32
