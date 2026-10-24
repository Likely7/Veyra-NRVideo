// Danmaku engine implementation (see DanmakuOverlay.h for the shape and why the
// text is pre-rendered).
#include "veyra/media/DanmakuOverlay.h"

// gdiplus.h needs IStream and PROPID, which objidl.h declares.
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

namespace veyra::media {
namespace {

// One GDI+ session for the process. SubtitleOverlay does the same; the token is
// intentionally never released, because a late shutdown breaks other overlays.
ULONG_PTR gdiplusToken() {
    static ULONG_PTR token = [] {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR value = 0;
        Gdiplus::GdiplusStartup(&value, &input, nullptr);
        return value;
    }();
    return token;
}

constexpr int kFontPixels = 24;
constexpr double kScrollSeconds = 8.0;   // time to cross the full width at 1x speed
constexpr int kTrackGap = 4;             // vertical gap between tracks
constexpr int kMargin = 8;

// Text is rasterised once per distinct string; a frame only blits these.
Gdiplus::FontFamily* commentFamily() {
    static Gdiplus::FontFamily* family = new Gdiplus::FontFamily(L"Microsoft YaHei");
    return family;
}

} // namespace

DanmakuEngine::DanmakuEngine() {
    gdiplusToken();
}

DanmakuEngine::~DanmakuEngine() = default;

void DanmakuEngine::setItems(std::vector<DanmakuItem> items) {
    items_ = std::move(items);
    // The API returns them in document order, which is roughly by time but not
    // guaranteed; the draw loop assumes ascending time.
    std::stable_sort(items_.begin(), items_.end(),
                     [](const DanmakuItem& a, const DanmakuItem& b) { return a.time < b.time; });
    sprites_.clear();
    spriteUsed_.clear();
    spriteBytes_ = 0;
    active_.clear();
    trackOf_.clear();
    needRebuild_ = true;
    lastSeconds_ = -1.0;
}

void DanmakuEngine::clear() {
    items_.clear();
    sprites_.clear();
    spriteUsed_.clear();
    spriteBytes_ = 0;
    active_.clear();
    trackOf_.clear();
    needRebuild_ = true;
    lastSeconds_ = -1.0;
}

void DanmakuEngine::noteSeek() {
    needRebuild_ = true;
    active_.clear();
    trackOf_.clear();
}

void DanmakuEngine::noteFontSize(int fontSize) {
    // Sprites are rasterised at a fixed size, so a size change invalidates all of
    // them. Rebuilding is cheap (only the visible comments are re-rendered).
    if (fontSize == spriteFontSize_) return;
    spriteFontSize_ = fontSize;
    sprites_.clear();
    spriteUsed_.clear();
    spriteBytes_ = 0;
}

void DanmakuEngine::evictIfNeeded() {
    // Bounded like newBV's cache pool: past the budget, drop the least recently
    // drawn entries. A long video otherwise accumulates one bitmap per distinct
    // comment and never releases any.
    constexpr size_t kBudget = 48ull * 1024 * 1024;   // 48 MiB of text bitmaps
    if (spriteBytes_ <= kBudget) return;
    std::vector<std::pair<uint64_t, size_t>> byAge;
    byAge.reserve(spriteUsed_.size());
    for (const auto& [index, used] : spriteUsed_) byAge.emplace_back(used, index);
    std::sort(byAge.begin(), byAge.end());
    for (const auto& [used, index] : byAge) {
        if (spriteBytes_ <= kBudget / 2) break;   // evict down to half, not to the edge
        const auto found = sprites_.find(index);
        if (found == sprites_.end()) continue;
        spriteBytes_ -= size_t(found->second.width) * size_t(found->second.height) * 4;
        sprites_.erase(found);
        spriteUsed_.erase(index);
    }
}

const DanmakuEngine::Sprite* DanmakuEngine::spriteFor(size_t itemIndex) {
    const auto found = sprites_.find(itemIndex);
    if (found != sprites_.end()) {
        spriteUsed_[itemIndex] = ++spriteClock_;
        return &found->second;
    }

    const DanmakuItem& item = items_[itemIndex];
    if (item.text.empty()) return nullptr;

    auto sprite = std::make_unique<Sprite>();
    Gdiplus::GraphicsPath path;
    Gdiplus::RectF layout(0, 0, 0, 0);
    const int rasterSize = spriteFontSize_ > 0 ? spriteFontSize_ : kFontPixels;
    path.AddString(item.text.c_str(), -1, commentFamily(), Gdiplus::FontStyleRegular,
                   float(rasterSize), layout, nullptr);
    Gdiplus::RectF bounds;
    path.GetBounds(&bounds);
    const int pad = rasterSize / 4;
    sprite->width = int(std::ceil(bounds.Width)) + pad * 2;
    sprite->height = int(std::ceil(bounds.Height)) + pad * 2;
    if (sprite->width <= 0 || sprite->height <= 0) return nullptr;

    sprite->image = std::make_unique<Gdiplus::Bitmap>(sprite->width, sprite->height,
                                                      PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(sprite->image.get());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        Gdiplus::Matrix shift;
        shift.Translate(float(pad) - bounds.X, float(pad) - bounds.Y);
        path.Transform(&shift);
        // An outline is what keeps the text readable over bright picture content.
        Gdiplus::Pen outline(Gdiplus::Color(200, 0, 0, 0), 2.4f);
        outline.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&outline, &path);
        const Gdiplus::Color ink(255, (item.color >> 16) & 0xFF,
                                 (item.color >> 8) & 0xFF, item.color & 0xFF);
        Gdiplus::SolidBrush fill(ink);
        graphics.FillPath(&fill, &path);
    }
    spriteBytes_ += size_t(sprite->width) * size_t(sprite->height) * 4;
    sprites_.emplace(itemIndex, std::move(*sprite));
    spriteUsed_[itemIndex] = ++spriteClock_;
    evictIfNeeded();
    // evictIfNeeded may have just dropped this very entry, so the pointer is taken
    // by a fresh lookup rather than from the insert result: returning the old
    // iterator here handed the caller a dangling pointer (0xC0000005).
    const auto afterEviction = sprites_.find(itemIndex);
    return afterEviction == sprites_.end() ? nullptr : &afterEviction->second;
}

int DanmakuEngine::testFreeRows(const std::vector<std::vector<RowEntry>>& table, int kind,
                                int row, double startTime, double textWidth, int rowsNeeded,
                                int rowLimit, double duration) const {
    if (kind < 0 || size_t(kind) >= table.size()) return rowsNeeded;
    const auto& column = table[size_t(kind)];
    int free = 0;
    const RowEntry* previous = nullptr;
    while (row < rowLimit && free < rowsNeeded) {
        if (size_t(row) >= column.size()) break;
        const RowEntry& entry = column[size_t(row)];
        if (previous != &entry) {
            previous = &entry;
            if (entry.taken) {
                // danmaku2ass: the holder's right edge must already be far enough
                // left that this one neither starts on top of it nor catches it.
                //   threshold = start - duration * (1 - width / (textWidth + width))
                const double width = double(lastVideoWidth_ > 0 ? lastVideoWidth_ : 1920);
                const double denominator = entry.textWidth + width;
                const double threshold = denominator > 0.0
                    ? startTime - duration * (1.0 - width / denominator)
                    : startTime - duration;
                const double holderRight = denominator > 0.0
                    ? entry.startTime + entry.textWidth * duration / denominator
                    : entry.startTime + duration;
                if (entry.startTime > threshold || holderRight > startTime) break;
            }
        }
        ++row;
        ++free;
    }
    return free;
}

int DanmakuEngine::findAlternativeRow(const std::vector<std::vector<RowEntry>>& table, int kind,
                                      int rowLimit, int rowsNeeded) const {
    if (kind < 0 || size_t(kind) >= table.size()) return 0;
    const auto& column = table[size_t(kind)];
    int leastBusy = 0;
    const int limit = std::max(1, rowLimit - rowsNeeded);
    for (int row = 0; row < limit && size_t(row) < column.size(); ++row) {
        if (!column[size_t(row)].taken) return row;
        // Among occupied rows, prefer the one whose holder started earliest.
        if (size_t(leastBusy) < column.size()
            && column[size_t(row)].startTime < column[size_t(leastBusy)].startTime)
            leastBusy = row;
    }
    return leastBusy;
}

void DanmakuEngine::markRow(std::vector<std::vector<RowEntry>>& table, int kind, int row,
                            int rowsNeeded, double startTime, double textWidth) const {
    if (kind < 0 || size_t(kind) >= table.size()) return;
    auto& column = table[size_t(kind)];
    for (int i = row; i < row + rowsNeeded; ++i) {
        if (size_t(i) >= column.size()) break;
        column[size_t(i)].taken = true;
        column[size_t(i)].startTime = startTime;
        column[size_t(i)].textWidth = textWidth;
    }
}

void DanmakuEngine::rebuildIfSeeked(double seconds) {
    // A jump backwards, or a jump far ahead, means the clock was repositioned.
    if (lastSeconds_ >= 0 && (seconds < lastSeconds_ - 0.5 || seconds > lastSeconds_ + 2.0))
        needRebuild_ = true;
    if (!needRebuild_) return;
    active_.clear();
    trackOf_.clear();
    needRebuild_ = false;
}

int DanmakuEngine::draw(HDC hdc, int width, int height, double seconds,
                        const DanmakuView& view) {
    if (items_.empty() || width <= 0 || height <= 0 || seconds < 0) return 0;
    rebuildIfSeeked(seconds);
    // active_ is this frame's snapshot: it is rebuilt below and nothing reads the
    // previous frame's copy. Without this it only grew (one entry per visible comment
    // per frame, ~200 MB over an hour of continuous playback) because the only other
    // clear() calls sit on setItems/noteSeek, which a normal playback never reaches.
    active_.clear();

    const double scale = view.scale > 0 ? view.scale : 1.0;
    const double speedFactor = view.speedFactor > 0 ? view.speedFactor : 1.0;
    const double duration = kScrollSeconds / speedFactor;
    const int fontSize = int(std::lround(kFontPixels * scale));
    // A size change invalidates every cached bitmap, so it is detected here rather
    // than letting the renderer draw text at the wrong scale.
    noteFontSize(fontSize);
    const int trackHeight = fontSize + kTrackGap;
    // Comments may cover the top `area` fraction of the picture.
    const double usableHeight = height * (view.area > 0 ? view.area : 0.5);
    const int tracks = std::max(1, int(usableHeight / trackHeight));
    const BYTE alpha = BYTE(std::lround(std::clamp(view.opacity, 0.05, 1.0) * 255.0));
    const float drawScale = float(fontSize) / float(kFontPixels);

    // Pre-rasterise what is about to appear. Without this the frame that first shows
    // a comment pays its text layout cost, which is 10 ms or more when several new
    // strings land at once. newBV keeps a similar 100 ms lead.
    {
        constexpr double kLead = 0.5;
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].time > seconds + kLead) break;
            if (items_[i].time <= seconds) continue;   // already handled below
            spriteFor(i);
        }
    }

    // Everything whose time has come and that is still on screen.
    std::vector<Active> now;
    for (size_t i = 0; i < items_.size(); ++i) {
        const DanmakuItem& item = items_[i];
        if (item.time > seconds) break;
        if (seconds - item.time > duration) continue;   // already left
        if (item.text.empty()) continue;
        if (item.mode == 4 || item.mode == 5) {
            // Bottom/top comments are pinned: their life is shorter and fixed.
            if (seconds - item.time > 4.0) continue;
        }
        Active entry;
        entry.item = i;
        entry.startedAt = item.time;
        entry.duration = duration;
        entry.track = -1;
        now.push_back(entry);
    }

    // One Graphics for the frame: constructing one per comment measured noticeably
    // slower than reusing it.
    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // Row allocation follows danmaku2ass. Four independent tables, one per comment
    // kind, so a top comment never blocks a scroll comment. Each table is one entry
    // per row, and the entry keeps the identifying numbers of the comment that holds
    // it; that is what lets the test below compare speeds instead of only entry
    // times.
    //
    // The tables are rebuilt from the comments still on screen each frame: cheap
    // (a few hundred entries) and it keeps a seek honest. A comment that survived
    // keeps its row through trackOf_, which is what stops comments from hopping
    // between rows while they are visible.
    constexpr int kKindScroll = 0, kKindTop = 1, kKindBottom = 2;
    std::vector<std::vector<RowEntry>> table(3, std::vector<RowEntry>(size_t(tracks)));
    {
        std::unordered_map<size_t, int> stillVisible;
        for (const auto& entry : now) {
            const auto found = trackOf_.find(entry.item);
            if (found != trackOf_.end()) stillVisible.emplace(entry.item, found->second);
        }
        trackOf_.swap(stillVisible);
    }
    // Re-mark the rows the surviving comments already hold, so a newcomer cannot
    // take a row that is in use.
    for (const auto& [itemIndex, row] : trackOf_) {
        const DanmakuItem& item = items_[itemIndex];
        if (row < 0 || row >= tracks) continue;
        const int kind = item.mode == 5 ? kKindTop : (item.mode == 4 ? kKindBottom : kKindScroll);
        const Sprite* existing = spriteFor(itemIndex);
        const double existingWidth = existing
            ? double(existing->width) * double(fontSize) / double(kFontPixels) : 0.0;
        markRow(table, kind, row, 1, item.time, existingWidth);
    }

    int drawn = 0;
    // Oldest first, as danmaku2ass processes them: an earlier comment owns its row.
    std::sort(now.begin(), now.end(), [](const Active& a, const Active& b) {
        return a.startedAt < b.startedAt;
    });
    for (auto& entry : now) {
        const Sprite* sprite = spriteFor(entry.item);
        if (!sprite) continue;
        const DanmakuItem& item = items_[entry.item];
        const int kind = item.mode == 5 ? kKindTop : (item.mode == 4 ? kKindBottom : kKindScroll);
        const int rowsNeeded = 1;   // one text line tall
        const double textWidth = double(sprite->width) * double(fontSize) / double(kFontPixels);
        // Pinned comments live four seconds, scroll comments cross the screen.
        const double life = (kind == kKindScroll) ? entry.duration : 4.0;

        auto assigned = trackOf_.find(entry.item);
        int track = assigned == trackOf_.end() ? -1 : assigned->second;

        if (track < 0) {
            // Walk down looking for `rowsNeeded` consecutive free rows, skipping past
            // whatever the test already rejected (that is danmaku2ass's `row +=
            // freerows or 1`).
            int row = 0;
            const int rowLimit = tracks;
            while (row < rowLimit) {
                const int free = testFreeRows(table, kind, row, entry.startedAt, textWidth,
                                              rowsNeeded, rowLimit, life);
                if (free >= rowsNeeded) break;
                row += free > 0 ? free : 1;
            }
            if (row >= rowLimit) {
                // Nothing free: take the least-occupied row rather than dropping the
                // comment, which is what danmaku2ass does when it is not reducing.
                row = findAlternativeRow(table, kind, rowLimit, rowsNeeded);
            }
            track = row;
            trackOf_[entry.item] = track;
            markRow(table, kind, track, rowsNeeded, entry.startedAt, textWidth);
        }

        entry.track = track;
        active_.push_back(entry);

        // A sprite can be evicted between the lookup above and here only when a
        // deeper cache miss runs; re-checking keeps that harmless.
        sprite = spriteFor(entry.item);
        if (!sprite) continue;
        const int spriteW = std::max(1, int(std::lround(sprite->width * drawScale)));
        const int spriteH = std::max(1, int(std::lround(sprite->height * drawScale)));

        const bool pinnedTop = item.mode == 5;
        const bool pinnedBottom = item.mode == 4;
        double x = 0, y = 0;
        if (pinnedTop || pinnedBottom) {
            x = (width - spriteW) / 2.0;
            y = pinnedTop ? kMargin + track * trackHeight
                          : height - kMargin - (tracks - track) * trackHeight;
        } else {
            // Scroll: the comment travels from just off the right edge to just off
            // the left over its whole duration. The track was fixed on entry, so the
            // vertical position never changes during its life.
            const double moved = (seconds - entry.startedAt) / entry.duration;
            x = width - moved * (width + spriteW);
            y = kMargin + track * trackHeight;
        }
        if (y + spriteH > height) continue;

        Gdiplus::ImageAttributes attributes;
        Gdiplus::ColorMatrix matrix{};
        matrix.m[0][0] = 1; matrix.m[1][1] = 1; matrix.m[2][2] = 1;
        matrix.m[3][3] = float(alpha) / 255.0f;
        attributes.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault,
                                  Gdiplus::ColorAdjustTypeBitmap);
        graphics.DrawImage(sprite->image.get(),
                           Gdiplus::Rect(int(x), int(y), spriteW, spriteH),
                           0, 0, sprite->width, sprite->height,
                           Gdiplus::UnitPixel, &attributes);
        ++drawn;
    }

    lastSeconds_ = seconds;
    lastVideoWidth_ = width;
    return drawn;
}

} // namespace veyra::media
