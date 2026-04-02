#pragma once

#include "PlatformTypes.h"
#include <vector>

namespace sst::platform {

/// Enumerate visible top-level windows (for window detection / snap).
class IWindowEnumerator {
public:
    virtual ~IWindowEnumerator() = default;

    /// Snapshot of all visible, non-minimized top-level windows.
    virtual std::vector<WindowInfo> enumerate() const = 0;

    /// Find the topmost window whose rect contains the given point.
    virtual WindowInfo windowAtPoint(Point pt) const = 0;
};

} // namespace sst::platform
