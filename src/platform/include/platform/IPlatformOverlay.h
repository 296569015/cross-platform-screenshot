#pragma once

#include "PlatformTypes.h"
#include <functional>
#include <optional>
#include <vector>

namespace sst::platform {

/// Fullscreen overlay window that covers the entire desktop.
/// Used for the screenshot selection UI.
/// On Windows: WS_POPUP + WS_EX_TOPMOST + WS_EX_TOOLWINDOW.
class IPlatformOverlay {
public:
    virtual ~IPlatformOverlay() = default;

    /// Create the overlay covering the given monitor bounds.
    virtual bool create(const Rect& bounds) = 0;

    /// Show/hide without destroying.
    virtual void show() = 0;
    virtual void hide() = 0;

    /// Return the native window handle (HWND on Windows).
    virtual void* getNativeHandle() const = 0;

    /// Return the overlay's pixel dimensions (DPI-aware).
    virtual Size getSize() const = 0;
    virtual float getDpiScale() const = 0;

    /// Make a region visually and interactively pass through to windows below.
    /// Coordinates are overlay-local; pass std::nullopt to restore the full overlay.
    virtual void setPassthroughRegion(std::optional<Rect> region,
                                      std::vector<Rect> overlayRegions = {}) = 0;

    /// Register callbacks for input events.
    virtual void setMouseCallback(std::function<void(const MouseEvent&)> cb) = 0;
    virtual void setKeyCallback(std::function<void(const KeyEvent&)> cb) = 0;

    /// Process pending OS messages. Returns false if quit requested.
    virtual bool pumpMessages() = 0;

    /// Destroy the overlay window.
    virtual void destroy() = 0;
};

} // namespace sst::platform
