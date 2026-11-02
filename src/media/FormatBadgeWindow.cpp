// Format badge window implementation.
#include "veyra/media/FormatBadgeWindow.h"

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

constexpr wchar_t kClassName[] = L"VeyraFormatBadge";

LRESULT CALLBACK badgeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;   // clicks belong to the picture
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ensureClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = badgeProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    registered = true;
}

bool ensureGdiplus() {
    static ULONG_PTR token = 0;
    static bool started = false;
    if (started) return token != 0;
    started = true;
    Gdiplus::GdiplusStartupInput input;
    return Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok && token != 0;
}

} // namespace

bool FormatBadgeWindow::create(HWND parent) {
    if (hwnd_) return true;
    ensureClass();
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                            kClassName, L"", WS_CHILD | WS_VISIBLE,
                            0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        log::warn("format-badge", std::format("overlay creation failed err={}", GetLastError()));
        return false;
    }
    return true;
}

void FormatBadgeWindow::destroy() {
    releaseSurface();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    visible_ = false;
}

bool FormatBadgeWindow::ensureSurface(int width, int height) {
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

void FormatBadgeWindow::releaseSurface() {
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

void FormatBadgeWindow::pushToWindow() {
    if (!hwnd_ || !memory_) return;
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    SIZE dimensions{width_, height_};
    POINT origin{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL ok = UpdateLayeredWindow(hwnd_, screen, nullptr, &dimensions, memory_,
                                        &origin, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (!ok) log::warn("format-badge", std::format("UpdateLayeredWindow failed err={}", GetLastError()));
}

void FormatBadgeWindow::show(HWND parent, const std::wstring& title, const std::wstring& detail) {
    if (!hwnd_ && !create(parent)) return;
    if (!parent) return;
    RECT client{};
    if (!GetClientRect(parent, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) { hide(); return; }
    if (!ensureSurface(width, height)) { hide(); return; }
    std::memset(bits_, 0, size_t(width_) * size_t(height_) * 4);
    if (ensureGdiplus()) {
        Gdiplus::Graphics graphics(memory_);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        // The safe area: far enough from the edge that a 16:9 letterbox on a taller
        // window still leaves the badge on the picture.
        const float margin = std::max(18.0f, float(width_) * 0.02f);
        Gdiplus::Font titleFont(L"Microsoft YaHei", std::max(15.0f, float(height_) * 0.026f),
                                Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font detailFont(L"Microsoft YaHei", std::max(11.0f, float(height_) * 0.018f),
                                Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat format;
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::RectF titleBox;
        graphics.MeasureString(title.c_str(), -1, &titleFont, Gdiplus::PointF(0, 0), &format, &titleBox);
        Gdiplus::RectF detailBox;
        if (!detail.empty())
            graphics.MeasureString(detail.c_str(), -1, &detailFont, Gdiplus::PointF(0, 0), &format, &detailBox);
        const float paddingX = margin * 0.6f, paddingY = margin * 0.4f;
        const float gap = detail.empty() ? 0.0f : detailBox.Height * 0.35f;
        const float boxWidth = std::max(titleBox.Width, detailBox.Width) + paddingX * 2.0f;
        const float boxHeight = titleBox.Height + gap + detailBox.Height + paddingY * 2.0f;
        const float radius = std::min(paddingY * 1.6f, boxHeight * 0.35f);
        Gdiplus::GraphicsPath path;
        const float x = margin, y = margin, r = radius;
        path.AddArc(x, y, r * 2, r * 2, 180, 90);
        path.AddArc(x + boxWidth - r * 2, y, r * 2, r * 2, 270, 90);
        path.AddArc(x + boxWidth - r * 2, y + boxHeight - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(x, y + boxHeight - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        Gdiplus::SolidBrush background(Gdiplus::Color(150, 8, 10, 16));
        Gdiplus::Pen border(Gdiplus::Color(90, 255, 255, 255), 1.0f);
        graphics.FillPath(&background, &path);
        graphics.DrawPath(&border, &path);
        Gdiplus::SolidBrush titleBrush(Gdiplus::Color(240, 255, 255, 255));
        graphics.DrawString(title.c_str(), -1, &titleFont,
                            Gdiplus::PointF(x + paddingX, y + paddingY), &format, &titleBrush);
        if (!detail.empty()) {
            Gdiplus::SolidBrush detailBrush(Gdiplus::Color(190, 210, 214, 224));
            graphics.DrawString(detail.c_str(), -1, &detailFont,
                                Gdiplus::PointF(x + paddingX, y + paddingY + titleBox.Height + gap),
                                &format, &detailBrush);
        }
    }
    pushToWindow();
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, client.right, client.bottom, SWP_NOACTIVATE);
    if (!visible_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        visible_ = true;
    }
}

void FormatBadgeWindow::hide() {
    if (!hwnd_ || !visible_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

} // namespace veyra::media
