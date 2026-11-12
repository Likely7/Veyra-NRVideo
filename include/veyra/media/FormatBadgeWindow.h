// The start-up format badge drawn over the picture.
//
// The video is a native child window, so a QML item cannot cover it: this uses the
// same layered-window construction as the danmaku and subtitle overlays (a
// WS_EX_LAYERED | WS_EX_TRANSPARENT child receiving a 32-bit DIB through
// UpdateLayeredWindow). Nothing here runs per frame: it draws once, stays up until
// hidden, and never takes a click.
#pragma once
#include <windows.h>

#include <string>

namespace veyra::media {

class FormatBadgeWindow {
public:
    ~FormatBadgeWindow() { destroy(); }

    bool create(HWND parent);
    void destroy();
    bool valid() const { return hwnd_ != nullptr; }

    // Redraws and shows the badge in the top-left safe area of `parent`.
    void show(HWND parent, const std::wstring& title, const std::wstring& detail);
    void hide();

    HWND handle() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    void* bits_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool visible_ = false;

    bool ensureSurface(int width, int height);
    void releaseSurface();
    void pushToWindow();
};

} // namespace veyra::media
