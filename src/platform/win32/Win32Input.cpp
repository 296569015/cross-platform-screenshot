#include "Win32Input.h"

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

    if (RegisterHotKey(nullptr, static_cast<int>(id), winMods, keyCode)) {
        callbacks_[id] = std::move(callback);
    }

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
            it->second();
        }
    }
}

} // namespace sst::platform::win32
