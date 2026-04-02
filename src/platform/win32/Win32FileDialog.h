#pragma once

#include <platform/IFileDialog.h>
#include <windows.h>

namespace sst::platform::win32 {

class Win32FileDialog final : public IFileDialog {
public:
    std::string showSave(const std::string& defaultName,
                         const std::string& filter) override;
};

} // namespace sst::platform::win32
