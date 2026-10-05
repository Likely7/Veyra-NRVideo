#pragma once

// Full ASS/SSA rendering through libass (ISC): animation (\move, \fad, \t),
// karaoke, \clip, \blur, vector drawings, per-span styles and the fonts a
// Matroska file carries. One renderer holds one libass track; the overlay
// keeps one per subtitle slot (primary / secondary).
#include <cstdint>
#include <memory>

#include "veyra/engine/Subtitles.h"

namespace veyra::ui {
class AssSubtitleRenderer {
public:
    AssSubtitleRenderer();
    ~AssSubtitleRenderer();
    AssSubtitleRenderer(const AssSubtitleRenderer&) = delete;
    AssSubtitleRenderer& operator=(const AssSubtitleRenderer&) = delete;

    // Adopts a (possibly grown) script snapshot. Events appended to the same
    // embedded source are fed incrementally; a different source rebuilds.
    bool prepare(const std::shared_ptr<const engine::SubtitleAssData>& data);
    // Renders the frame at `timeMs` for a video area of frameW x frameH device
    // pixels. storage = the video's own pixel size (aspect correction).
    // Returns libass' change code: 0 unchanged, 1 moved, 2 new content; -1 when
    // nothing can be rendered.
    int render(int64_t timeMs, int frameW, int frameH, int storageW, int storageH, double fontScale);
    // Blends the images of the last render() into a premultiplied BGRA canvas,
    // with the video area's top-left corner at (offsetX, offsetY).
    void composite(uint32_t* canvas, int canvasW, int canvasH, int offsetX, int offsetY) const;
    bool hasImages() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace veyra::ui
