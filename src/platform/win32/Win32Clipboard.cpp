#include "Win32Clipboard.h"
#include <cstring>

namespace sst::platform::win32 {

bool Win32Clipboard::writeImage(const uint8_t* rgbaPixels, int width, int height,
                                ClipboardError* outError) {
    if (!OpenClipboard(nullptr)) {
        if (outError) *outError = ClipboardError::OpenFailed;
        return false;
    }
    EmptyClipboard();

    // CF_DIB: BITMAPINFOHEADER + pixel data (BGRA, bottom-up)
    size_t headerSize = sizeof(BITMAPINFOHEADER);
    size_t rowBytes   = width * 4;
    size_t pixelSize  = rowBytes * height;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
    if (!hMem) {
        CloseClipboard();
        if (outError) *outError = ClipboardError::WriteFailed;
        return false;
    }

    auto* ptr = static_cast<uint8_t*>(GlobalLock(hMem));

    // Fill BITMAPINFOHEADER
    auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(ptr);
    std::memset(bih, 0, sizeof(BITMAPINFOHEADER));
    bih->biSize        = sizeof(BITMAPINFOHEADER);
    bih->biWidth       = width;
    bih->biHeight      = height;  // positive = bottom-up
    bih->biPlanes      = 1;
    bih->biBitCount    = 32;
    bih->biCompression = BI_RGB;
    bih->biSizeImage   = static_cast<DWORD>(pixelSize);

    // Convert RGBA top-down → BGRA bottom-up
    uint8_t* dst = ptr + headerSize;
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = rgbaPixels + (height - 1 - y) * rowBytes;
        uint8_t*       dstRow = dst + y * rowBytes;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
        }
    }

    GlobalUnlock(hMem);
    SetClipboardData(CF_DIB, hMem);

    CloseClipboard();
    return true;
}

bool Win32Clipboard::writePng(const std::vector<uint8_t>& pngData,
                              ClipboardError* outError) {
    if (!OpenClipboard(nullptr)) {
        if (outError) *outError = ClipboardError::OpenFailed;
        return false;
    }

    UINT cfPng = RegisterClipboardFormatW(L"PNG");
    if (!cfPng) {
        CloseClipboard();
        if (outError) *outError = ClipboardError::FormatNotSupported;
        return false;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, pngData.size());
    if (!hMem) {
        CloseClipboard();
        if (outError) *outError = ClipboardError::WriteFailed;
        return false;
    }

    void* ptr = GlobalLock(hMem);
    std::memcpy(ptr, pngData.data(), pngData.size());
    GlobalUnlock(hMem);

    SetClipboardData(cfPng, hMem);
    CloseClipboard();
    return true;
}

} // namespace sst::platform::win32
