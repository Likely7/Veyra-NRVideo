// Danmaku (bullet-comment) engine and overlay for the Bilibili module.
//
// The site hands back one <d> entry per comment: a timestamp, a mode (1-3 scroll,
// 4 bottom, 5 top), a colour and the text. This turns that list into a screen: it
// assigns tracks, drives the motion from the player's own position so a seek or a
// pause stays in sync, and draws into the same kind of layered window the subtitle
// overlay uses.
//
// Rendering is cached per distinct string. Laying text out every frame measured
// 12 ms at 1080p; blitting pre-rendered images costs about 1 ms for 40 lines
// (measured, see the probe used during development), which is what makes 60 fps
// possible at 4K.
#pragma once
// windows.h defines min/max as macros, which breaks std::min/std::max in this
// header's inline code and in the engine.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Only needed for the opaque pointer below; the .cpp includes the real header.
namespace Gdiplus { class Bitmap; }

namespace veyra::media {

// One comment, as the API reports it.
struct DanmakuItem {
    double time = 0.0;      // seconds into the video
    int mode = 1;           // 1-3 scroll right-to-left, 4 bottom, 5 top
    int color = 0xFFFFFF;
    std::wstring text;
};

// Drawing and behaviour knobs, mirroring what newBV exposes.
struct DanmakuView {
    double scale = 1.0;        // font size multiplier
    double opacity = 0.7;      // 0..1
    double speedFactor = 1.0;  // higher = faster
    double area = 0.5;         // fraction of the picture height comments may cover
    bool mask = false;         // reserved: keep comments off the lower area
    int videoWidth = 0;
    int videoHeight = 0;
};

class DanmakuEngine {
public:
    DanmakuEngine();
    ~DanmakuEngine();

    // Replaces the script. `items` may be unsorted.
    void setItems(std::vector<DanmakuItem> items);
    void clear();
    size_t itemCount() const { return items_.size(); }

    // Draws the comments that should be on screen at `seconds`. Returns the number
    // of entries drawn. `hdc` is the overlay's memory DC, `width`/`height` its size.
    // A backward jump past the current time is treated as a seek and the tracks are
    // rebuilt, so comments do not arrive from stale positions.
    int draw(HDC hdc, int width, int height, double seconds, const DanmakuView& view);

    // Tells the engine the clock jumped (seek). The next draw rebuilds its state.
    void noteSeek();

private:
    struct Sprite {
        std::unique_ptr<Gdiplus::Bitmap> image;
        int width = 0;
        int height = 0;
    };

    // A comment currently on screen.
    struct Active {
        size_t item = 0;
        int track = 0;
        double startedAt = 0.0;   // video seconds when it entered
        double duration = 0.0;    // how long it takes to cross the screen
    };

    const Sprite* spriteFor(size_t itemIndex);
    // Drops cached sprites when the font size changes: their size is baked in.
    void noteFontSize(int fontSize);
    // Evicts the least recently used sprites once the cache passes its byte budget.
    // newBV bounds its pool the same way; without a bound a long video accumulates
    // one bitmap per distinct comment.
    void evictIfNeeded();
    void rebuildIfSeeked(double seconds);
    // Row occupancy, ported from danmaku2ass (m13253, GPL-3), whose allocation
    // avoids overlaps by comparing speeds rather than only entry times: a faster
    // comment that would catch a slower one ahead of it does not get that row.
    // One entry per row per comment kind; `taken` is the comment that holds it.
    struct RowEntry {
        bool taken = false;
        double startTime = 0.0;   // when the holder appeared
        double textWidth = 0.0;   // its width in pixels
    };
    // How many consecutive rows from `row` are free for `entry`. Returns 0 when the
    // row immediately conflicts, otherwise the count checked (capped at `rowsNeeded`).
    int testFreeRows(const std::vector<std::vector<RowEntry>>& table, int kind, int row,
                     double startTime, double textWidth, int rowsNeeded, int rowLimit,
                     double duration) const;
    // Least-occupied row, used when nothing is free (danmaku2ass's
    // FindAlternativeRow). Never fails.
    int findAlternativeRow(const std::vector<std::vector<RowEntry>>& table, int kind,
                           int rowLimit, int rowsNeeded) const;
    void markRow(std::vector<std::vector<RowEntry>>& table, int kind, int row, int rowsNeeded,
                 double startTime, double textWidth) const;

    std::vector<DanmakuItem> items_;
    std::vector<Active> active_;
    // Track per comment, decided once when it enters and kept for its whole life.
    // Recomputing every frame made comments jump tracks (they "climbed" upward).
    std::unordered_map<size_t, int> trackOf_;
    std::unordered_map<size_t, Sprite> sprites_;
    // Bytes held by sprites_, and a monotonic tick per entry for LRU order.
    size_t spriteBytes_ = 0;
    std::unordered_map<size_t, uint64_t> spriteUsed_;
    uint64_t spriteClock_ = 0;
    int spriteFontSize_ = 0;
    double lastSeconds_ = -1.0;
    double lastVideoWidth_ = 0;
    bool needRebuild_ = true;
};

} // namespace veyra::media
