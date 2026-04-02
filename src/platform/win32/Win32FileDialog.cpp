#include "Win32FileDialog.h"
#include <shobjidl.h>
#include <commdlg.h>

namespace sst::platform::win32 {

std::string Win32FileDialog::showSave(const std::string& defaultName,
                                      const std::string& filter) {
    // Use the classic GetSaveFileName API for simplicity
    wchar_t filename[MAX_PATH] = {};

    // Convert default name to wide string
    int wLen = MultiByteToWideChar(CP_UTF8, 0, defaultName.c_str(), -1, nullptr, 0);
    if (wLen > 0 && wLen < MAX_PATH) {
        MultiByteToWideChar(CP_UTF8, 0, defaultName.c_str(), -1, filename, MAX_PATH);
    }

    // Build filter string: "PNG Files\0*.png\0All Files\0*.*\0\0"
    wchar_t filterW[256] = L"PNG Files\0*.png\0JPEG Files\0*.jpg\0All Files\0*.*\0";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = nullptr;
    ofn.lpstrFilter  = filterW;
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt  = L"png";

    if (GetSaveFileNameW(&ofn)) {
        // Convert result to UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, result.data(), len, nullptr, nullptr);
        return result;
    }

    return {};
}

} // namespace sst::platform::win32
