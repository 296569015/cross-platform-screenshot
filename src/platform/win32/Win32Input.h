#pragma once

#include <platform/IInput.h>
#include <windows.h>
#include <unordered_map>

namespace sst::platform::win32 {

class Win32Input final : public IInput {
public:
    Win32Input();
    ~Win32Input() override;

    HotkeyId registerHotkey(uint32_t keyCode, uint8_t modifiers,
                            HotkeyCallback callback) override;
    void unregisterHotkey(HotkeyId id) override;
    void pollHotkeys() override;
    bool scrollAt(Point screenPosition,
                  int wheelDelta,
                  void* ignoredWindow = nullptr) override;

private:
    HWND windowFromPointExcluding(Point screenPosition, HWND ignoredWindow) const;

    HotkeyId nextId_ = 1;
    std::unordered_map<HotkeyId, HotkeyCallback> callbacks_;
    HWND lastScrollTarget_ = nullptr;
};

} // namespace sst::platform::win32
