#pragma once

#include <cstdint>
#include <vector>

namespace sst::platform {

enum class ClipboardError {
    OpenFailed,
    FormatNotSupported,
    WriteFailed,
};

/// Clipboard write interface.
class IClipboard {
public:
    virtual ~IClipboard() = default;

    /// Write RGBA pixel data as CF_DIB + PNG to clipboard.
    /// Returns true on success.
    virtual bool writeImage(const uint8_t* rgbaPixels,
                            int width, int height,
                            ClipboardError* outError = nullptr) = 0;

    /// Write pre-encoded PNG bytes to clipboard.
    virtual bool writePng(const std::vector<uint8_t>& pngData,
                          ClipboardError* outError = nullptr) = 0;
};

} // namespace sst::platform
