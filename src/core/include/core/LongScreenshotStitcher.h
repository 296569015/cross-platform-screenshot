#pragma once

#include <cstdint>
#include <vector>

namespace sst::core {

struct LongScreenshotStitchResult {
    bool  appended     = false;
    bool  duplicate    = false;
    bool  reliable     = false;
    int   overlapRows  = 0;
    int   appendedRows = 0;
    float score        = 0.0f;
    float secondBestScore = 0.0f;
};

struct LongScreenshotStitchOptions {
    int   minOverlapRows    = 24;
    int   maxOverlapRows    = 900;
    int   minAppendRows     = 6;
    float duplicateScore    = 1.0f;
    float reliableMatchScore = 16.0f;
    float acceptableMatchScore = 30.0f;
    float ambiguousScoreGap = 1.5f;
    float acceptableScoreGap = 0.0f;
    bool  appendOnUnreliableMatch = true;
};

class LongScreenshotStitcher {
public:
    explicit LongScreenshotStitcher(int width = 0,
                                    LongScreenshotStitchOptions options = {});

    void reset(int width);
    void start(const std::vector<uint8_t>& firstFrame, int frameHeight);
    LongScreenshotStitchResult append(const std::vector<uint8_t>& nextFrame,
                                      int frameHeight,
                                      bool allowAcceptableMatch = true);

    bool empty() const { return pixels_.empty(); }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }

private:
    float compareDuplicate(const std::vector<uint8_t>& nextFrame,
                           int frameHeight) const;

    int width_ = 0;
    int height_ = 0;
    int lastFrameHeight_ = 0;
    LongScreenshotStitchOptions options_;
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> lastFrame_;
};

} // namespace sst::core
