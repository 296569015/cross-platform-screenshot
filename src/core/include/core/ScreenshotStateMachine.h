#pragma once

#include <platform/PlatformTypes.h>

namespace sst::core {

/// Application-level states for the screenshot workflow.
enum class AppState {
    Idle,        // Waiting for hotkey. Overlay hidden.
    Capturing,   // Hotkey pressed. Acquiring screen frame.
    Selecting,   // Overlay shown. User dragging to select region.
    Annotating,  // Region locked. Toolbar visible. Drawing annotations.
    Saving,      // Encoding/exporting in progress.
};

/// Events that drive state transitions.
enum class AppEvent {
    HotkeyTriggered,
    FrameAcquired,
    MouseDown,
    MouseMove,
    MouseUp,
    KeyPress,
    ToolSelected,
    SaveRequested,
    CopyRequested,
    CancelRequested,
    Escape,
    SaveComplete,
};

/// Central state machine for the screenshot workflow.
/// Pure logic — no platform code.
class ScreenshotStateMachine {
public:
    AppState currentState() const { return state_; }

    /// Feed an event. Returns the new state.
    AppState transition(AppEvent event);

    /// Region selection state
    bool isSelecting() const { return state_ == AppState::Selecting; }
    bool isAnnotating() const { return state_ == AppState::Annotating; }

    void setSelectedRegion(platform::Rect rect) { selectedRegion_ = rect; }
    platform::Rect selectedRegion() const { return selectedRegion_; }

private:
    AppState state_ = AppState::Idle;
    platform::Rect selectedRegion_;
};

} // namespace sst::core
