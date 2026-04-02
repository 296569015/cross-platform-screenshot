#pragma once

#include <platform/IWindowEnumerator.h>
#include <windows.h>

namespace sst::platform::win32 {

class Win32WindowEnumerator final : public IWindowEnumerator {
public:
    std::vector<WindowInfo> enumerate() const override;
    WindowInfo windowAtPoint(Point pt) const override;

private:
    static BOOL CALLBACK enumCallback(HWND hwnd, LPARAM lParam);
};

} // namespace sst::platform::win32
