#pragma once

#include "PlatformTypes.h"
#include <cstdint>

namespace sst::platform {

using HotkeyId = uint32_t;

/// Global hotkey registration interface.
class IInput {
public:
    virtual ~IInput() = default;

    /// Register a system-global hotkey. Returns an ID for later unregister.
    virtual HotkeyId registerHotkey(uint32_t keyCode,
                                    uint8_t  modifiers,
                                    HotkeyCallback callback) = 0;

    /// Unregister a previously registered hotkey.
    virtual void unregisterHotkey(HotkeyId id) = 0;

    /// Must be called in the message-pump thread to dispatch hotkey events.
    virtual void pollHotkeys() = 0;
};

} // namespace sst::platform
