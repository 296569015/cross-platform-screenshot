/// Minimal unit tests for ScreenshotStateMachine.
/// No test framework dependency — uses simple assert-based checks.

#include <core/ScreenshotStateMachine.h>
#include <core/AnnotationModel.h>
#include <core/LongScreenshotStitcher.h>
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace sst::core;

std::vector<uint8_t> make_source_image(int width, int height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pixels[i + 0] = static_cast<uint8_t>((x * 17 + y * 11) & 0xFF);
            pixels[i + 1] = static_cast<uint8_t>((x * 7 + y * 23) & 0xFF);
            pixels[i + 2] = static_cast<uint8_t>((x * 13 + y * 5) & 0xFF);
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

std::vector<uint8_t> crop_rows(const std::vector<uint8_t>& source,
                               int width,
                               int startY,
                               int height) {
    std::vector<uint8_t> rows(static_cast<size_t>(width) * height * 4);
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = source.data() + static_cast<size_t>(startY + y) * rowBytes;
        uint8_t* dst = rows.data() + static_cast<size_t>(y) * rowBytes;
        std::copy(src, src + rowBytes, dst);
    }
    return rows;
}

std::vector<uint8_t> make_repeating_source_image(int width, int height, int period) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const int py = y % period;
        for (int x = 0; x < width; ++x) {
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pixels[i + 0] = static_cast<uint8_t>((py * 41 + x * 13) & 0xFF);
            pixels[i + 1] = static_cast<uint8_t>((py * 73 + x * 5) & 0xFF);
            pixels[i + 2] = static_cast<uint8_t>((py * 29 + x * 17) & 0xFF);
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

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

void test_long_screenshot_stitches_overlapping_frames() {
    constexpr int width = 8;
    constexpr int sourceHeight = 18;
    constexpr int frameHeight = 8;
    auto source = make_source_image(width, sourceHeight);

    LongScreenshotStitchOptions options;
    options.minOverlapRows = 2;
    options.maxOverlapRows = 8;
    options.minAppendRows = 1;
    options.reliableMatchScore = 0.0f;
    LongScreenshotStitcher stitcher(width, options);

    stitcher.start(crop_rows(source, width, 0, frameHeight), frameHeight);
    auto r1 = stitcher.append(crop_rows(source, width, 5, frameHeight), frameHeight);
    auto r2 = stitcher.append(crop_rows(source, width, 10, frameHeight), frameHeight);

    assert(r1.appended);
    assert(r1.reliable);
    assert(r1.overlapRows == 3);
    assert(r2.appended);
    assert(r2.reliable);
    assert(r2.overlapRows == 3);
    assert(stitcher.height() == sourceHeight);
    assert(stitcher.pixels() == source);
    std::printf("  PASS: long screenshot stitcher merges overlapping frames\n");
}

void test_long_screenshot_stitches_large_overlap_search_range() {
    constexpr int width = 20;
    constexpr int sourceHeight = 420;
    constexpr int frameHeight = 260;
    constexpr int scrollRows = 113;
    auto source = make_source_image(width, sourceHeight);

    LongScreenshotStitchOptions options;
    options.minOverlapRows = 40;
    options.maxOverlapRows = 240;
    options.minAppendRows = 1;
    options.reliableMatchScore = 0.0f;
    LongScreenshotStitcher stitcher(width, options);

    stitcher.start(crop_rows(source, width, 0, frameHeight), frameHeight);
    auto result = stitcher.append(crop_rows(source, width, scrollRows, frameHeight),
                                  frameHeight);

    assert(result.appended);
    assert(result.reliable);
    assert(result.overlapRows == frameHeight - scrollRows);
    assert(stitcher.height() == frameHeight + scrollRows);
    assert(stitcher.pixels() == crop_rows(source, width, 0, stitcher.height()));
    std::printf("  PASS: long screenshot stitcher handles large overlap search range\n");
}

void test_long_screenshot_detects_duplicate_frame() {
    constexpr int width = 6;
    constexpr int frameHeight = 7;
    auto frame = make_source_image(width, frameHeight);

    LongScreenshotStitchOptions options;
    options.minOverlapRows = 2;
    options.maxOverlapRows = 7;
    LongScreenshotStitcher stitcher(width, options);
    stitcher.start(frame, frameHeight);
    auto result = stitcher.append(frame, frameHeight);

    assert(result.duplicate);
    assert(!result.appended);
    assert(stitcher.height() == frameHeight);
    std::printf("  PASS: long screenshot stitcher ignores duplicate frame\n");
}

void test_long_screenshot_stops_on_unreliable_overlap() {
    constexpr int width = 8;
    constexpr int frameHeight = 8;
    auto first = make_source_image(width, frameHeight);
    auto unrelated = make_source_image(width, frameHeight);
    for (size_t i = 0; i < unrelated.size(); i += 4) {
        unrelated[i + 0] = static_cast<uint8_t>(255 - unrelated[i + 0]);
        unrelated[i + 1] = static_cast<uint8_t>(255 - unrelated[i + 1]);
        unrelated[i + 2] = static_cast<uint8_t>(255 - unrelated[i + 2]);
    }

    LongScreenshotStitchOptions options;
    options.minOverlapRows = 2;
    options.maxOverlapRows = 7;
    options.minAppendRows = 1;
    options.appendOnUnreliableMatch = false;
    LongScreenshotStitcher stitcher(width, options);
    stitcher.start(first, frameHeight);
    auto result = stitcher.append(unrelated, frameHeight);

    assert(!result.appended);
    assert(!result.duplicate);
    assert(stitcher.height() == frameHeight);
    std::printf("  PASS: long screenshot stitcher stops on unreliable overlap\n");
}

void test_long_screenshot_rejects_ambiguous_repeating_overlap() {
    constexpr int width = 16;
    constexpr int sourceHeight = 80;
    constexpr int frameHeight = 40;
    constexpr int scrollRows = 7;
    auto source = make_repeating_source_image(width, sourceHeight, 4);

    LongScreenshotStitchOptions options;
    options.minOverlapRows = 12;
    options.maxOverlapRows = 39;
    options.minAppendRows = 1;
    options.reliableMatchScore = 1.0f;
    options.ambiguousScoreGap = 2.0f;
    options.appendOnUnreliableMatch = false;
    LongScreenshotStitcher stitcher(width, options);

    stitcher.start(crop_rows(source, width, 0, frameHeight), frameHeight);
    auto result = stitcher.append(crop_rows(source, width, scrollRows, frameHeight),
                                  frameHeight);

    assert(!result.appended);
    assert(!result.duplicate);
    assert(!result.reliable);
    assert(stitcher.height() == frameHeight);
    std::printf("  PASS: long screenshot stitcher rejects ambiguous repeating overlap\n");
}

void test_freehand_annotation_model_stores_points() {
    AnnotationModel model;
    FreehandAnnotation brush;
    brush.points = { { 3.0f, 4.0f }, { 8.0f, 9.0f }, { 13.0f, 10.0f } };
    brush.color = { 255, 68, 68, 255 };
    brush.thickness = 3.0f;

    model.addAnnotation(brush);

    assert(model.count() == 1);
    const auto* stored = std::get_if<FreehandAnnotation>(&model.annotations().front());
    assert(stored != nullptr);
    assert(stored->points.size() == 3);
    assert(stored->points[0].x == 3.0f);
    assert(stored->points[2].y == 10.0f);
    assert(stored->thickness == 3.0f);
    std::printf("  PASS: freehand annotation stores sampled points\n");
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
    test_long_screenshot_stitches_overlapping_frames();
    test_long_screenshot_stitches_large_overlap_search_range();
    test_long_screenshot_detects_duplicate_frame();
    test_long_screenshot_stops_on_unreliable_overlap();
    test_long_screenshot_rejects_ambiguous_repeating_overlap();
    test_freehand_annotation_model_stores_points();

    std::printf("\nAll tests passed!\n");
    return 0;
}
