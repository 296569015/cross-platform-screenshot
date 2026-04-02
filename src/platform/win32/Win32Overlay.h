#pragma once

#include <platform/IPlatformOverlay.h>
#include <windows.h>

namespace sst::platform::win32 {

class Win32Overlay final : public IPlatformOverlay {
public:
    Win32Overlay();
    ~Win32Overlay() override;

    bool create(const Rect& bounds) override;
    void show() override;
    void hide() override;
    void* getNativeHandle() const override;
    Size getSize() const override;
    float getDpiScale() const override;
    void setMouseCallback(std::function<void(const MouseEvent&)> cb) override;
    void setKeyCallback(std::function<void(const KeyEvent&)> cb) override;
    bool pumpMessages() override;
    void destroy() override;

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handleMessage(UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    Size size_;
    float dpiScale_ = 1.0f;
    bool quitRequested_ = false;

    std::function<void(const MouseEvent&)> mouseCallback_;
    std::function<void(const KeyEvent&)>   keyCallback_;

    static constexpr const wchar_t* kClassName = L"SSTOverlay";
    static bool classRegistered_;
};

} // namespace sst::platform::win32
