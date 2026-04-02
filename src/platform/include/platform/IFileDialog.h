#pragma once

#include <string>

namespace sst::platform {

/// Native file save dialog.
class IFileDialog {
public:
    virtual ~IFileDialog() = default;

    /// Show a save file dialog. Returns the chosen path, or empty on cancel.
    /// defaultName: suggested filename (e.g., "screenshot_20260402.png")
    /// filter: file type filter description (e.g., "PNG Files|*.png")
    virtual std::string showSave(const std::string& defaultName,
                                 const std::string& filter) = 0;
};

} // namespace sst::platform
