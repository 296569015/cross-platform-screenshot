#include "core/ScreenshotStateMachine.h"

namespace sst::core {

AppState ScreenshotStateMachine::transition(AppEvent event) {
    switch (state_) {
    case AppState::Idle:
        if (event == AppEvent::HotkeyTriggered)
            state_ = AppState::Capturing;
        break;

    case AppState::Capturing:
        if (event == AppEvent::FrameAcquired)
            state_ = AppState::Selecting;
        else if (event == AppEvent::Escape || event == AppEvent::CancelRequested)
            state_ = AppState::Idle;
        break;

    case AppState::Selecting:
        if (event == AppEvent::MouseUp)
            state_ = AppState::Annotating;
        else if (event == AppEvent::Escape || event == AppEvent::CancelRequested)
            state_ = AppState::Idle;
        break;

    case AppState::Annotating:
        if (event == AppEvent::SaveRequested || event == AppEvent::CopyRequested)
            state_ = AppState::Saving;
        else if (event == AppEvent::Escape || event == AppEvent::CancelRequested)
            state_ = AppState::Idle;
        break;

    case AppState::Saving:
        if (event == AppEvent::SaveComplete)
            state_ = AppState::Idle;
        break;
    }

    return state_;
}

} // namespace sst::core
