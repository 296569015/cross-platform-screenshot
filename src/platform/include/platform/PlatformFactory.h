#pragma once

#include <memory>

namespace sst::platform {

// Forward declarations
class IPlatformCapture;
class IPlatformOverlay;
class IClipboard;
class IInput;
class IWindowEnumerator;
class IFileDialog;
class ISystemTray;
class EglContext;

/// All platform services, created by the platform factory.
/// This is the single injection point — application code depends on this,
/// never on platform-specific implementation headers.
struct PlatformServices {
    std::unique_ptr<IPlatformCapture>    capture;
    std::unique_ptr<IPlatformOverlay>    overlay;
    std::unique_ptr<IClipboard>          clipboard;
    std::unique_ptr<IInput>              input;
    std::unique_ptr<IWindowEnumerator>   windowEnum;
    std::unique_ptr<IFileDialog>         fileDialog;
    std::unique_ptr<ISystemTray>         systemTray;
    std::unique_ptr<EglContext>          eglContext;
};

/// Create all platform services for the current OS.
/// On Windows: returns Win32* implementations.
PlatformServices createPlatformServices();

} // namespace sst::platform
