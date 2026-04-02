#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sst::platform {

struct TrayMenuItem {
    std::string label;
    std::function<void()> callback;
    bool separator = false; // true = render as separator, ignore label/callback
};

/// System tray icon interface.
class ISystemTray {
public:
    virtual ~ISystemTray() = default;

    /// Create the tray icon with a tooltip and context menu items.
    virtual bool create(const std::string& tooltip,
                        const std::vector<TrayMenuItem>& menuItems) = 0;

    /// Update the tooltip text.
    virtual void setTooltip(const std::string& tooltip) = 0;

    /// Remove the tray icon.
    virtual void destroy() = 0;
};

} // namespace sst::platform
