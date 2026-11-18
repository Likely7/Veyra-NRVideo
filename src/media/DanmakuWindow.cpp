// Danmaku window implementation.
#include "veyra/media/DanmakuWindow.h"
#include "veyra/Log.h"

// gdiplus.h needs IStream and PROPID, which objidl.h declares.
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include <format>

#pragma comment(lib, "gdiplus.lib")

namespace veyra::media {
namespace {

// The ULPARAM carries the window's own pointer so the engine can be reached from
// the window procedure.
constexpr wchar_t kClassName[] = L"VeyraDanmakuOverlay";

LRESULT CALLBACK danmakuProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;   // clicks belong to the picture
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ensureClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = danmakuProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    registered = true;
}

} // namespace

bool DanmakuWindow::create(HWND parent) {
    if (hwnd_) return true;
    ensureClass();
    // WS_EX_TRANSPARENT keeps the pointer on the video; NOACTIVATE keeps focus there.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                            kClassName, L"", WS_CHILD | WS_VISIBLE,
                            0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        log::warn("danmaku", std::format("overlay creation failed err={}", GetLastError()));
        return false;
    }
    log::info("danmaku", "overlay created");
    return true;
}

void DanmakuWindow::destroy() {
    releaseSurface();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    visible_ = false;
}

bool DanmakuWindow::ensureSurface(int width, int height) {
    if (memory_ && width == width_ && height == height_) return true;
    releaseSurface();
    width_ = width;
    height_ = height;
    if (width_ <= 0 || height_ <= 0) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width_;
    bi.bmiHeader.biHeight = -height_;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    memory_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!memory_) return false;
    bitmap_ = CreateDIBSection(memory_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!bitmap_ || !bits_) { releaseSurface(); return false; }
    previous_ = SelectObject(memory_, bitmap_);
    return true;
}

void DanmakuWindow::releaseSurface() {
    if (memory_) {
        SelectObject(memory_, previous_);
        if (bitmap_) DeleteObject(bitmap_);
        DeleteDC(memory_);
    }
    memory_ = nullptr;
    bitmap_ = nullptr;
    bits_ = nullptr;
    previous_ = nullptr;
    width_ = height_ = 0;
}

void DanmakuWindow::pushToWindow() {
    if (!hwnd_ || !memory_) return;
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    SIZE dimensions{width_, height_};
    POINT origin{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL ok = UpdateLayeredWindow(hwnd_, screen, nullptr, &dimensions, memory_,
                                        &origin, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (!ok && GetPropW(hwnd_, L"danmaku.error") == nullptr) {
        log::error("danmaku", std::format("UpdateLayeredWindow failed err={}", GetLastError()));
        SetPropW(hwnd_, L"danmaku.error", HANDLE(1));
    }
}

void DanmakuWindow::follow(HWND parent) {
    if (!hwnd_ || !parent) return;
    RECT client{};
    if (!GetClientRect(parent, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) { hide(); return; }
    if (width != width_ || height != height_) {
        if (!ensureSurface(width, height)) { hide(); return; }
    }
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, client.right, client.bottom, SWP_NOACTIVATE);
}

int DanmakuWindow::render(double seconds, const DanmakuView& view) {
    if (!hwnd_ || !memory_) return 0;
    // The DIB is reused, so every frame starts transparent.
    std::memset(bits_, 0, size_t(width_) * size_t(height_) * 4);
    const int drawn = engine_.draw(memory_, width_, height_, seconds, view);
    if (drawn > 0) {
        pushToWindow();
        if (!visible_) {
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            visible_ = true;
        }
        // Make sure it stays above the picture after an UpdateLayeredWindow.
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        hide();
    }
    return drawn;
}

void DanmakuWindow::hide() {
    if (!hwnd_ || !visible_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

} // namespace veyra::media
