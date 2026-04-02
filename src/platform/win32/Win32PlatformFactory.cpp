#include <platform/PlatformFactory.h>

#include "Win32EglContext.h"
#include "Win32Capture.h"
#include "Win32Overlay.h"
#include "Win32Clipboard.h"
#include "Win32Input.h"
#include "Win32FileDialog.h"
#include "Win32SystemTray.h"
#include "Win32WindowEnumerator.h"

namespace sst::platform {

PlatformServices createPlatformServices() {
    PlatformServices services;
    services.eglContext  = std::make_unique<win32::Win32EglContext>();
    services.capture     = std::make_unique<win32::Win32Capture>();
    services.overlay     = std::make_unique<win32::Win32Overlay>();
    services.clipboard   = std::make_unique<win32::Win32Clipboard>();
    services.input       = std::make_unique<win32::Win32Input>();
    services.fileDialog  = std::make_unique<win32::Win32FileDialog>();
    services.systemTray  = std::make_unique<win32::Win32SystemTray>();
    services.windowEnum  = std::make_unique<win32::Win32WindowEnumerator>();
    return services;
}

} // namespace sst::platform
