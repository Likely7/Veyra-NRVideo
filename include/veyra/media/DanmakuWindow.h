// The layered window danmaku is drawn into.
//
// This is deliberately the same construction as SubtitleOverlay: a child window
// with WS_EX_LAYERED | WS_EX_TRANSPARENT that receives a 32-bit DIB through
// UpdateLayeredWindow. Nothing here paints text; it owns the surface and the
// timing, and hands each frame to the engine.
#pragma once
#include <windows.h>

#include <memory>

#include "veyra/media/DanmakuOverlay.h"

namespace veyra::media {

class DanmakuWindow {
public:
    // `parent` is the video window. Returns false when the window cannot be made.
    bool create(HWND parent);
    void destroy();
    bool valid() const { return hwnd_ != nullptr; }

    // Places it over the parent's client area. Cheap enough to call per frame.
    void follow(HWND parent);

    // Draws one frame at `seconds`. `opacity` is applied to the whole surface.
    // Returns the number of comments drawn (0 when nothing is on screen, in which
    // case the window is hidden so it cannot interfere with input).
    int render(double seconds, const DanmakuView& view);

    void hide();
    HWND handle() const { return hwnd_; }

    // Replaces the comment script, e.g. when a new part or video is opened.
    void setItems(std::vector<DanmakuItem> items) { engine_.setItems(std::move(items)); }
    void clearItems() { engine_.clear(); }
    size_t itemCount() const { return engine_.itemCount(); }
    // Tells the engine the clock jumped; call it on every seek.
    void noteSeek() { engine_.noteSeek(); }

private:
    HWND hwnd_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    void* bits_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool visible_ = false;
    DanmakuEngine engine_;

    bool ensureSurface(int width, int height);
    void releaseSurface();
    void pushToWindow();
};

} // namespace veyra::media
