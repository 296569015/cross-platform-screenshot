#include "Win32Input.h"
#include <cstdio>

namespace sst::platform::win32 {

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
            return false;
        }
    }

    lastScrollTarget_ = target;

    HWND root = GetAncestor(target, GA_ROOT);
    if (root) {
        SetForegroundWindow(root);
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
            return true;
        }
    }

    const WPARAM wp = MAKEWPARAM(0, static_cast<SHORT>(wheelDelta));
    const LPARAM lp = MAKELPARAM(
        static_cast<SHORT>(screenPosition.x),
        static_cast<SHORT>(screenPosition.y));
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
        return true;
    }

    return PostMessageW(target, WM_MOUSEWHEEL, wp, lp) != FALSE;
}

} // namespace sst::platform::win32
