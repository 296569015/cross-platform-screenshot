#pragma once

#include "PlatformTypes.h"
#include <vector>

namespace sst::platform {

enum class CaptureError {
    NotInitialized,
    AccessDenied,
    DeviceLost,
    Timeout,
    UnsupportedFormat,
};

/// Screen capture interface.
/// Windows implementation uses DXGI Desktop Duplication.
class IPlatformCapture {
public:
    virtual ~IPlatformCapture() = default;

    /// Initialize capture for a specific monitor.
    /// monitorIndex = -1 means virtual desktop (all monitors).
    /// Returns true on success.
    virtual bool initialize(int monitorIndex = -1) = 0;

    /// Acquire the next frame. The CapturedFrame::nativeTextureHandle
    /// points to a GPU texture owned by the capture subsystem.
    /// Valid until the next acquireFrame() or releaseFrame() call.
    /// Returns true on success, sets outError on failure.
    virtual bool acquireFrame(CapturedFrame& outFrame,
                              CaptureError& outError,
                              uint32_t timeoutMs = 100) = 0;

    /// Release the current frame back to the capture pipeline.
    virtual void releaseFrame() = 0;

    /// Query available monitors.
    virtual std::vector<MonitorInfo> enumerateMonitors() const = 0;

    /// Shutdown and release all resources.
    virtual void shutdown() = 0;
};

} // namespace sst::platform
