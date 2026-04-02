#include "Win32SystemTray.h"
#include <shellapi.h>

namespace sst::platform::win32 {

Win32SystemTray::Win32SystemTray() = default;

Win32SystemTray::~Win32SystemTray() {
    destroy();
}

bool Win32SystemTray::create(const std::string& tooltip,
                             const std::vector<TrayMenuItem>& menuItems) {
    menuItems_ = menuItems;

    // Create a hidden message-only window for tray icon messages
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance      = GetModuleHandle(nullptr);
    wc.lpszClassName  = L"SSTTrayMsg";
    RegisterClassW(&wc);

    messageWindow_ = CreateWindowW(L"SSTTrayMsg", L"", 0,
                                   0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr,
                                   GetModuleHandle(nullptr), nullptr);
    SetWindowLongPtr(messageWindow_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Create context menu
    contextMenu_ = CreatePopupMenu();
    for (size_t i = 0; i < menuItems_.size(); ++i) {
        if (menuItems_[i].separator) {
            AppendMenuW(contextMenu_, MF_SEPARATOR, 0, nullptr);
        } else {
            wchar_t label[128] = {};
            MultiByteToWideChar(CP_UTF8, 0, menuItems_[i].label.c_str(), -1, label, 128);
            AppendMenuW(contextMenu_, MF_STRING, static_cast<UINT_PTR>(i + 1), label);
        }
    }

    // Set up NOTIFYICONDATA
    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = messageWindow_;
    nid_.uID              = 1;
    nid_.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon            = LoadIcon(nullptr, IDI_APPLICATION); // TODO: use custom icon

    // Set tooltip
    wchar_t tipW[128] = {};
    MultiByteToWideChar(CP_UTF8, 0, tooltip.c_str(), -1, tipW, 128);
    wcscpy_s(nid_.szTip, tipW);

    return Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

void Win32SystemTray::setTooltip(const std::string& tooltip) {
    wchar_t tipW[128] = {};
    MultiByteToWideChar(CP_UTF8, 0, tooltip.c_str(), -1, tipW, 128);
    wcscpy_s(nid_.szTip, tipW);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Win32SystemTray::destroy() {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        nid_.hWnd = nullptr;
    }
    if (contextMenu_) {
        DestroyMenu(contextMenu_);
        contextMenu_ = nullptr;
    }
    if (messageWindow_) {
        DestroyWindow(messageWindow_);
        messageWindow_ = nullptr;
    }
}

LRESULT CALLBACK Win32SystemTray::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Win32SystemTray*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (msg == WM_TRAYICON && self) {
        if (LOWORD(lp) == WM_RBUTTONUP) {
            self->showContextMenu();
            return 0;
        }
    }

    if (msg == WM_COMMAND && self) {
        UINT id = LOWORD(wp);
        if (id > 0 && id <= self->menuItems_.size()) {
            auto& item = self->menuItems_[id - 1];
            if (item.callback) item.callback();
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

void Win32SystemTray::showContextMenu() {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(messageWindow_);
    TrackPopupMenu(contextMenu_, TPM_RIGHTBUTTON, pt.x, pt.y, 0, messageWindow_, nullptr);
    PostMessage(messageWindow_, WM_NULL, 0, 0);
}

} // namespace sst::platform::win32
