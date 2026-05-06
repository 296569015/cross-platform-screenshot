#include "core/LongScreenshotStitcher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace sst::core {

namespace {

constexpr int kBytesPerPixel = 4;
constexpr int kFeatureBins = 6;

struct RowFeature {
    float luma = 0.0f;
    float contrast = 0.0f;
    float edge = 0.0f;
    float color = 0.0f;
    std::array<float, kFeatureBins> bins = {};
};

struct OverlapCandidate {
    int overlap = 0;
    float rowScore = 255.0f;
    float pixelScore = 255.0f;
};

size_t rowOffset(int width, int y) {
    return static_cast<size_t>(y) * width * kBytesPerPixel;
}

float pixelLuma(const uint8_t* p) {
    return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
}

std::vector<RowFeature> buildRowFeatures(const std::vector<uint8_t>& frame,
                                         int width,
                                         int height) {
    std::vector<RowFeature> features(static_cast<size_t>(std::max(0, height)));
    if (width <= 0 || height <= 0) {
        return features;
    }

    const int ignoredRight = std::min(width / 8, std::max(2, width / 24));
    const int x0 = std::min(width - 1, std::max(0, width / 80));
    int x1 = std::max(x0 + 1, width - ignoredRight);
    if (x1 > width) {
        x1 = width;
    }

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = frame.data() + rowOffset(width, y);
        double lumaSum = 0.0;
        double lumaSqSum = 0.0;
        double edgeSum = 0.0;
        double colorSum = 0.0;
        std::array<double, kFeatureBins> binSums = {};
        std::array<int, kFeatureBins> binCounts = {};
        int samples = 0;
        float previousLuma = 0.0f;
        bool hasPrevious = false;
        const int span = std::max(1, x1 - x0);

        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = row + x * kBytesPerPixel;
            const float luma = pixelLuma(p);
            const int bin = std::min(kFeatureBins - 1,
                                     ((x - x0) * kFeatureBins) / span);
            lumaSum += luma;
            lumaSqSum += static_cast<double>(luma) * luma;
            colorSum += std::max({ p[0], p[1], p[2] }) -
                        std::min({ p[0], p[1], p[2] });
            binSums[static_cast<size_t>(bin)] += luma;
            ++binCounts[static_cast<size_t>(bin)];
            if (hasPrevious) {
                edgeSum += std::abs(luma - previousLuma);
            }
            previousLuma = luma;
            hasPrevious = true;
            ++samples;
        }

        if (samples <= 0) {
            continue;
        }

        const double mean = lumaSum / samples;
        const double variance = std::max(0.0, lumaSqSum / samples - mean * mean);
        RowFeature feature;
        feature.luma = static_cast<float>(mean);
        feature.contrast = static_cast<float>(std::sqrt(variance));
        feature.edge = static_cast<float>(edgeSum / samples);
        feature.color = static_cast<float>(colorSum / samples);
        for (int i = 0; i < kFeatureBins; ++i) {
            if (binCounts[static_cast<size_t>(i)] > 0) {
                feature.bins[static_cast<size_t>(i)] =
                    static_cast<float>(binSums[static_cast<size_t>(i)] /
                                       binCounts[static_cast<size_t>(i)]);
            } else {
                feature.bins[static_cast<size_t>(i)] = feature.luma;
            }
        }
        features[static_cast<size_t>(y)] = feature;
    }

    return features;
}

float rowInformation(const RowFeature& row) {
    return row.contrast + row.edge * 2.0f + row.color * 0.35f;
}

float compareFeatureOverlap(const std::vector<RowFeature>& previous,
                            int previousHeight,
                            const std::vector<RowFeature>& next,
                            int nextHeight,
                            int overlapRows) {
    const int previousStartY = previousHeight - overlapRows;
    if (previousStartY < 0 || overlapRows <= 0 || overlapRows >= nextHeight) {
        return 255.0f;
    }

    const int yStep = std::max(1, overlapRows / 128);
    double diff = 0.0;
    double weightSum = 0.0;
    int informativeRows = 0;

    for (int y = 0; y < overlapRows; y += yStep) {
        const auto& a = previous[static_cast<size_t>(previousStartY + y)];
        const auto& b = next[static_cast<size_t>(y)];
        const float info = std::max(rowInformation(a), rowInformation(b));
        const float weight = 1.0f + std::min(info, 96.0f) / 28.0f;
        float binDiff = 0.0f;
        for (int i = 0; i < kFeatureBins; ++i) {
            binDiff += std::abs(a.bins[static_cast<size_t>(i)] -
                                b.bins[static_cast<size_t>(i)]);
        }
        binDiff /= static_cast<float>(kFeatureBins);
        diff += weight * (
            std::abs(a.luma - b.luma) * 0.35f +
            binDiff * 0.85f +
            std::abs(a.contrast - b.contrast) * 0.65f +
            std::abs(a.edge - b.edge) * 1.35f +
            std::abs(a.color - b.color) * 0.35f);
        weightSum += weight;
        if (info >= 3.0f) {
            ++informativeRows;
        }
    }

    if (informativeRows < 6 || weightSum <= 0.0) {
        return 255.0f;
    }
    return static_cast<float>(diff / weightSum);
}

float comparePixelOverlap(const std::vector<uint8_t>& previous,
                          int width,
                          int previousHeight,
                          const std::vector<uint8_t>& next,
                          int nextHeight,
                          int overlapRows) {
    const int previousStartY = previousHeight - overlapRows;
    if (width <= 0 || previousStartY < 0 || overlapRows <= 0 ||
        overlapRows >= nextHeight) {
        return 255.0f;
    }

    const int ignoredRight = std::min(width / 8, std::max(2, width / 24));
    const int xEnd = std::max(1, width - ignoredRight);
    const int xStep = std::max(1, width / 128);
    const int yStep = std::max(1, overlapRows / 128);

    double diff = 0.0;
    uint64_t samples = 0;
    for (int y = 0; y < overlapRows; y += yStep) {
        const uint8_t* a =
            previous.data() + rowOffset(width, previousStartY + y);
        const uint8_t* b = next.data() + rowOffset(width, y);
        for (int x = 0; x < xEnd; x += xStep) {
            const int i = x * kBytesPerPixel;
            diff += std::abs(a[i + 0] - b[i + 0]);
            diff += std::abs(a[i + 1] - b[i + 1]);
            diff += std::abs(a[i + 2] - b[i + 2]);
            samples += 3;
        }
    }

    return samples == 0 ? 255.0f : static_cast<float>(diff / samples);
}

void addCandidate(std::vector<OverlapCandidate>& candidates,
                  int overlap,
                  float rowScore) {
    auto it = std::find_if(candidates.begin(), candidates.end(),
        [overlap](const OverlapCandidate& candidate) {
            return candidate.overlap == overlap;
        });
    if (it != candidates.end()) {
        it->rowScore = std::min(it->rowScore, rowScore);
        return;
    }
    candidates.push_back({ overlap, rowScore, 255.0f });
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
    int frameHeight,
    bool allowAcceptableMatch) {
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
    const auto previousFeatures =
        buildRowFeatures(lastFrame_, width_, lastFrameHeight_);
    const auto nextFeatures =
        buildRowFeatures(nextFrame, width_, frameHeight);

    std::vector<OverlapCandidate> candidates;
    auto collectOverlap = [&](int overlap) {
        const float score = compareFeatureOverlap(previousFeatures,
                                                  lastFrameHeight_,
                                                  nextFeatures,
                                                  frameHeight,
                                                  overlap);
        addCandidate(candidates, overlap, score);
    };

    const int overlapRange = maxOverlap - minOverlap + 1;
    const int coarseStep = overlapRange > 96 ? std::max(2, overlapRange / 32) : 1;
    for (int overlap = minOverlap; overlap <= maxOverlap; overlap += coarseStep) {
        collectOverlap(overlap);
    }

    if (coarseStep > 1 && !candidates.empty()) {
        auto ranked = candidates;
        std::sort(ranked.begin(), ranked.end(),
                  [](const OverlapCandidate& a, const OverlapCandidate& b) {
                      if (std::abs(a.rowScore - b.rowScore) > 0.25f) {
                          return a.rowScore < b.rowScore;
                      }
                      return a.overlap > b.overlap;
                  });

        const int refineSeeds = std::min<int>(4, ranked.size());
        for (int i = 0; i < refineSeeds; ++i) {
            const int refineStart = std::max(minOverlap, ranked[i].overlap - coarseStep);
            const int refineEnd = std::min(maxOverlap, ranked[i].overlap + coarseStep);
            for (int overlap = refineStart; overlap <= refineEnd; ++overlap) {
                collectOverlap(overlap);
            }
        }
    }

    for (auto& candidate : candidates) {
        candidate.pixelScore = comparePixelOverlap(lastFrame_,
                                                   width_,
                                                   lastFrameHeight_,
                                                   nextFrame,
                                                   frameHeight,
                                                   candidate.overlap);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const OverlapCandidate& a, const OverlapCandidate& b) {
                  if (std::abs(a.pixelScore - b.pixelScore) > 0.25f) {
                      return a.pixelScore < b.pixelScore;
                  }
                  return a.overlap > b.overlap;
              });

    const int bestOverlap = candidates.empty() ? 0 : candidates.front().overlap;
    const float bestScore = candidates.empty()
        ? std::numeric_limits<float>::max()
        : candidates.front().pixelScore;
    float secondBestScore = std::numeric_limits<float>::max();
    for (size_t i = 1; i < candidates.size(); ++i) {
        if (std::abs(candidates[i].overlap - bestOverlap) > 2) {
            secondBestScore = candidates[i].pixelScore;
            break;
        }
    }

    int appendFrom = 0;
    const bool hasClearWinner =
        !std::isfinite(secondBestScore) ||
        secondBestScore - bestScore >= options_.ambiguousScoreGap;
    const bool reliable =
        bestOverlap > 0 &&
        bestScore <= options_.reliableMatchScore &&
        hasClearWinner;
    if (reliable) {
        appendFrom = bestOverlap;
    } else if (allowAcceptableMatch &&
               options_.appendOnUnreliableMatch &&
               bestOverlap > 0 &&
               bestScore <= options_.acceptableMatchScore) {
        appendFrom = bestOverlap;
    } else {
        result.score = bestScore;
        result.secondBestScore = secondBestScore;
        result.overlapRows = bestOverlap;
        return result;
    }

    const int appendRows = frameHeight - appendFrom;
    if (appendRows < options_.minAppendRows) {
        result.duplicate = true;
        result.score = bestScore;
        result.secondBestScore = secondBestScore;
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
    result.reliable = reliable;
    result.overlapRows = appendFrom;
    result.appendedRows = appendRows;
    result.score = bestScore;
    result.secondBestScore = secondBestScore;
    lastFrameHeight_ = frameHeight;
    lastFrame_ = nextFrame;
    return result;
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


} // namespace sst::core
