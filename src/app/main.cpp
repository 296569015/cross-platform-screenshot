/// Cross-Platform Screenshot Tool
/// Entry point (Windows)

#include "Application.h"

#include <windows.h>
#include <shellscalingapi.h>
#include <cstdio>

#pragma comment(lib, "shcore.lib")

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // Set per-monitor DPI awareness V2 (Windows 10 1703+)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Initialize COM (needed for file dialogs)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Attach a console for debug output in debug builds
#ifdef _DEBUG
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
#endif

    sst::app::Application app;

    if (!app.initialize()) {
        std::fprintf(stderr, "Application initialization failed\n");
        return 1;
    }

    int exitCode = app.run();

    CoUninitialize();
    return exitCode;
}
