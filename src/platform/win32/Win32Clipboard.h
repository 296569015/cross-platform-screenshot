#pragma once

#include <platform/IClipboard.h>
#include <windows.h>

namespace sst::platform::win32 {

class Win32Clipboard final : public IClipboard {
public:
    bool writeImage(const uint8_t* rgbaPixels, int width, int height,
                    ClipboardError* outError = nullptr) override;
    bool writePng(const std::vector<uint8_t>& pngData,
                  ClipboardError* outError = nullptr) override;
};

} // namespace sst::platform::win32
