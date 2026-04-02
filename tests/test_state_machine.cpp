/// Minimal unit tests for ScreenshotStateMachine.
/// No test framework dependency — uses simple assert-based checks.

#include <core/ScreenshotStateMachine.h>
#include <cassert>
#include <cstdio>

using namespace sst::core;

void test_initial_state() {
    ScreenshotStateMachine sm;
    assert(sm.currentState() == AppState::Idle);
    std::printf("  PASS: initial state is Idle\n");
}

void test_hotkey_triggers_capturing() {
    ScreenshotStateMachine sm;
    sm.transition(AppEvent::HotkeyTriggered);
    assert(sm.currentState() == AppState::Capturing);
    std::printf("  PASS: HotkeyTriggered → Capturing\n");
}

void test_frame_acquired_enters_selecting() {
    ScreenshotStateMachine sm;
    sm.transition(AppEvent::HotkeyTriggered);
    sm.transition(AppEvent::FrameAcquired);
    assert(sm.currentState() == AppState::Selecting);
    std::printf("  PASS: FrameAcquired → Selecting\n");
}

void test_mouse_up_enters_annotating() {
    ScreenshotStateMachine sm;
    sm.transition(AppEvent::HotkeyTriggered);
    sm.transition(AppEvent::FrameAcquired);
    sm.transition(AppEvent::MouseUp);
    assert(sm.currentState() == AppState::Annotating);
    std::printf("  PASS: MouseUp → Annotating\n");
}

void test_escape_returns_to_idle() {
    ScreenshotStateMachine sm;
    sm.transition(AppEvent::HotkeyTriggered);
    sm.transition(AppEvent::FrameAcquired);
    sm.transition(AppEvent::Escape);
    assert(sm.currentState() == AppState::Idle);
    std::printf("  PASS: Escape → Idle (from Selecting)\n");
}

void test_save_flow() {
    ScreenshotStateMachine sm;
    sm.transition(AppEvent::HotkeyTriggered);
    sm.transition(AppEvent::FrameAcquired);
    sm.transition(AppEvent::MouseUp);
    sm.transition(AppEvent::SaveRequested);
    assert(sm.currentState() == AppState::Saving);
    sm.transition(AppEvent::SaveComplete);
    assert(sm.currentState() == AppState::Idle);
    std::printf("  PASS: SaveRequested → Saving → SaveComplete → Idle\n");
}

void test_invalid_transition_stays() {
    ScreenshotStateMachine sm;
    // MouseUp in Idle should have no effect
    sm.transition(AppEvent::MouseUp);
    assert(sm.currentState() == AppState::Idle);
    std::printf("  PASS: invalid transition stays in current state\n");
}

int main() {
    std::printf("=== ScreenshotStateMachine Tests ===\n");

    test_initial_state();
    test_hotkey_triggers_capturing();
    test_frame_acquired_enters_selecting();
    test_mouse_up_enters_annotating();
    test_escape_returns_to_idle();
    test_save_flow();
    test_invalid_transition_stays();

    std::printf("\nAll tests passed!\n");
    return 0;
}
