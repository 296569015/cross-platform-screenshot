#pragma once

#include <platform/ISystemTray.h>
#include <windows.h>
#include <shellapi.h>

namespace sst::platform::win32 {

class Win32SystemTray final : public ISystemTray {
public:
    Win32SystemTray();
    ~Win32SystemTray() override;

    bool create(const std::string& tooltip,
                const std::vector<TrayMenuItem>& menuItems) override;
    void setTooltip(const std::string& tooltip) override;
    void destroy() override;

private:
    NOTIFYICONDATAW nid_ = {};
    HWND            messageWindow_ = nullptr;
    HMENU           contextMenu_   = nullptr;
    std::vector<TrayMenuItem> menuItems_;

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void showContextMenu();

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
};

} // namespace sst::platform::win32
