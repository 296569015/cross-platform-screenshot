#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace sst::platform {

// ── Basic geometry types ──────────────────────────────────────────────

struct Point  { int32_t x = 0, y = 0; };
struct PointF { float   x = 0, y = 0; };
struct Size   { int32_t w = 0, h = 0; };
struct SizeF  { float   w = 0, h = 0; };
struct Rect   { int32_t x = 0, y = 0, w = 0, h = 0; };
struct RectF  { float   x = 0, y = 0, w = 0, h = 0; };

struct Color {
    float r = 0, g = 0, b = 0, a = 1.0f;

    static Color fromHex(uint32_t hex) {
        return {
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >>  8) & 0xFF) / 255.0f,
            ((hex >>  0) & 0xFF) / 255.0f,
            1.0f
        };
    }
};

// ── Monitor info ──────────────────────────────────────────────────────

struct MonitorInfo {
    Rect        workArea;
    Rect        bounds;
    float       dpiScale  = 1.0f;
    bool        isPrimary = false;
    std::string name;
};

// ── Window info (for window detection / snap-to-window) ───────────────

struct WindowInfo {
    uintptr_t   handle    = 0;   // opaque native handle (HWND on Windows)
    Rect        rect;
    std::string title;
    bool        isVisible = true;
};

// ── Input events ──────────────────────────────────────────────────────

enum class MouseButton : uint8_t { Left, Right, Middle };

enum class KeyModifier : uint8_t {
    None  = 0,
    Shift = 1 << 0,
    Ctrl  = 1 << 1,
    Alt   = 1 << 2,
};

inline uint8_t operator|(KeyModifier a, KeyModifier b) {
    return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}

struct MouseEvent {
    enum class Type { Move, Press, Release, Scroll } type;
    Point       position;        // overlay-local coordinates
    MouseButton button = MouseButton::Left;
    float       scrollDelta = 0;
    uint8_t     modifiers   = 0; // bitmask of KeyModifier
};

struct KeyEvent {
    enum class Type { Press, Release } type;
    uint32_t    keyCode   = 0;   // platform-normalized virtual key
    uint32_t    scanCode  = 0;
    uint8_t     modifiers = 0;
    char32_t    character = 0;   // Unicode codepoint (for text input)
};

// ── Captured frame (passed from capture → renderer) ───────────────────

struct CapturedFrame {
    void*    nativeTextureHandle = nullptr; // ID3D11Texture2D* on Windows
    Size     size;
    uint64_t frameIndex = 0;
};

// ── Callback types ────────────────────────────────────────────────────

using HotkeyCallback = std::function<void()>;

} // namespace sst::platform
