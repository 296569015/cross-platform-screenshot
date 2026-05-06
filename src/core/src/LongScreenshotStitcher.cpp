#include "core/LongScreenshotStitcher.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sst::core {

namespace {

constexpr int kBytesPerPixel = 4;

size_t rowOffset(int width, int y) {
    return static_cast<size_t>(y) * width * kBytesPerPixel;
}

} // namespace

LongScreenshotStitcher::LongScreenshotStitcher(
    int width,
    LongScreenshotStitchOptions options)
    : width_(width), options_(options) {}

void LongScreenshotStitcher::reset(int width) {
    width_ = width;
    height_ = 0;
    lastFrameHeight_ = 0;
    pixels_.clear();
    lastFrame_.clear();
}

void LongScreenshotStitcher::start(const std::vector<uint8_t>& firstFrame,
                                   int frameHeight) {
    if (width_ <= 0 || frameHeight <= 0) {
        height_ = 0;
        lastFrameHeight_ = 0;
        pixels_.clear();
        lastFrame_.clear();
        return;
    }

    const size_t expectedSize =
        static_cast<size_t>(frameHeight) * width_ * kBytesPerPixel;
    if (firstFrame.size() < expectedSize) {
        height_ = 0;
        lastFrameHeight_ = 0;
        pixels_.clear();
        lastFrame_.clear();
        return;
    }

    height_ = frameHeight;
    pixels_ = firstFrame;
    lastFrameHeight_ = frameHeight;
    lastFrame_ = firstFrame;
}

LongScreenshotStitchResult LongScreenshotStitcher::append(
    const std::vector<uint8_t>& nextFrame,
    int frameHeight) {
    LongScreenshotStitchResult result;
    if (width_ <= 0 || height_ <= 0 || frameHeight <= 0 ||
        lastFrame_.empty() || lastFrameHeight_ <= 0) {
        return result;
    }

    const size_t expectedSize =
        static_cast<size_t>(frameHeight) * width_ * kBytesPerPixel;
    if (nextFrame.size() < expectedSize) {
        return result;
    }

    const float duplicateScore = compareDuplicate(nextFrame, frameHeight);
    if (duplicateScore <= options_.duplicateScore) {
        result.duplicate = true;
        result.score = duplicateScore;
        result.overlapRows = frameHeight;
        return result;
    }

    const int maxOverlap = std::min({
        options_.maxOverlapRows,
        lastFrameHeight_,
        frameHeight - 1
    });
    const int minOverlap = std::min(options_.minOverlapRows, maxOverlap);

    int bestOverlap = 0;
    float bestScore = std::numeric_limits<float>::max();
    for (int overlap = minOverlap; overlap <= maxOverlap; ++overlap) {
        const float score = compareOverlap(nextFrame, frameHeight, overlap);
        const bool meaningfullyBetter = score + 0.25f < bestScore;
        const bool closeAndLargerOverlap =
            std::abs(score - bestScore) <= 0.25f && overlap > bestOverlap;
        if (meaningfullyBetter || closeAndLargerOverlap) {
            bestScore = score;
            bestOverlap = overlap;
        }
    }

    int appendFrom = 0;
    if (bestOverlap > 0 && bestScore <= options_.reliableMatchScore) {
        appendFrom = bestOverlap;
    } else if (options_.appendOnUnreliableMatch && bestOverlap > 0) {
        appendFrom = bestOverlap;
    } else {
        result.score = bestScore;
        result.overlapRows = bestOverlap;
        return result;
    }

    const int appendRows = frameHeight - appendFrom;
    if (appendRows < options_.minAppendRows) {
        result.duplicate = true;
        result.score = bestScore;
        result.overlapRows = bestOverlap;
        return result;
    }

    const size_t oldSize = pixels_.size();
    const size_t appendBytes =
        static_cast<size_t>(appendRows) * width_ * kBytesPerPixel;
    pixels_.resize(oldSize + appendBytes);

    const uint8_t* src = nextFrame.data() + rowOffset(width_, appendFrom);
    std::copy(src, src + appendBytes, pixels_.data() + oldSize);
    height_ += appendRows;

    result.appended = true;
    result.overlapRows = appendFrom;
    result.appendedRows = appendRows;
    result.score = bestScore;
    lastFrameHeight_ = frameHeight;
    lastFrame_ = nextFrame;
    return result;
}

float LongScreenshotStitcher::compareOverlap(
    const std::vector<uint8_t>& nextFrame,
    int frameHeight,
    int overlapRows) const {
    const int xStep = std::max(1, width_ / 128);
    const int yStep = std::max(1, overlapRows / 160);
    const int previousStartY = lastFrameHeight_ - overlapRows;

    double diff = 0.0;
    double weightSum = 0.0;
    int informativeSamples = 0;
    for (int y = 0; y < overlapRows; y += yStep) {
        const int previousY = previousStartY + y;
        const uint8_t* a = lastFrame_.data() + rowOffset(width_, previousY);
        const uint8_t* b = nextFrame.data() + rowOffset(width_, y);
        for (int x = 0; x < width_; x += xStep) {
            const float info = std::max(
                contentWeight(lastFrame_, lastFrameHeight_, x, previousY),
                contentWeight(nextFrame, frameHeight, x, y));
            if (info < 8.0f) {
                continue;
            }

            const int i = x * kBytesPerPixel;
            const float weight = 1.0f + std::min(info, 96.0f) / 32.0f;
            diff += weight * std::abs(a[i + 0] - b[i + 0]);
            diff += weight * std::abs(a[i + 1] - b[i + 1]);
            diff += weight * std::abs(a[i + 2] - b[i + 2]);
            weightSum += weight * 3.0;
            ++informativeSamples;
        }
    }

    if (informativeSamples < 8 || weightSum <= 0.0) {
        return 255.0f;
    }
    return static_cast<float>(diff / weightSum);
}

float LongScreenshotStitcher::compareDuplicate(
    const std::vector<uint8_t>& nextFrame,
    int frameHeight) const {
    if (lastFrameHeight_ != frameHeight || lastFrame_.empty()) {
        return 255.0f;
    }

    const int xStep = std::max(1, width_ / 96);
    const int yStep = std::max(1, frameHeight / 96);

    uint64_t diff = 0;
    uint64_t samples = 0;
    for (int y = 0; y < frameHeight; y += yStep) {
        const uint8_t* a = lastFrame_.data() + rowOffset(width_, y);
        const uint8_t* b = nextFrame.data() + rowOffset(width_, y);
        for (int x = 0; x < width_; x += xStep) {
            const int i = x * kBytesPerPixel;
            diff += static_cast<uint64_t>(std::abs(a[i + 0] - b[i + 0]));
            diff += static_cast<uint64_t>(std::abs(a[i + 1] - b[i + 1]));
            diff += static_cast<uint64_t>(std::abs(a[i + 2] - b[i + 2]));
            samples += 3;
        }
    }

    return samples == 0 ? 255.0f : static_cast<float>(diff) / samples;
}

float LongScreenshotStitcher::contentWeight(const std::vector<uint8_t>& frame,
                                            int frameHeight,
                                            int x,
                                            int y) const {
    auto lumaAt = [&](int px, int py) {
        px = std::clamp(px, 0, width_ - 1);
        py = std::clamp(py, 0, frameHeight - 1);
        const uint8_t* p = frame.data() + rowOffset(width_, py) + px * kBytesPerPixel;
        return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
    };

    const float c = lumaAt(x, y);
    const float horizontal = std::abs(c - lumaAt(x - 1, y)) +
                             std::abs(c - lumaAt(x + 1, y));
    const float vertical = std::abs(c - lumaAt(x, y - 1)) +
                           std::abs(c - lumaAt(x, y + 1));
    return horizontal + vertical;
}

} // namespace sst::core
