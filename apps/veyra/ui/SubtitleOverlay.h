#pragma once

// Layered subtitle overlay. One cue can carry several lines and a full style
// (font/size/colour/outline/shadow/background/alignment) coming from ASS/SSA or
// from the viewer's global preferences. Primary and secondary (dual language)
// lines are stacked bottom-up by the caller.
#include <windows.h>

#include <string>
#include <vector>

#include "veyra/engine/Subtitles.h"
#include "veyra/engine/PreviewView.h"

namespace veyra::ui {
struct SubtitleLine {
    std::wstring text;                 // '\n' separated
    engine::SubtitleStyle style;
    int alignOverride=0;               // {\anN}, 0 = style alignment
    double posX=-1,posY=-1;            // {\pos(x,y)} normalised to 0..1 (or -1)
    bool secondary=false;
    std::shared_ptr<const engine::SubtitleBitmapFrame> bitmap;
    // ASS/SSA track drawn by libass (builds that link it): the whole script is
    // rendered at assTimeMs inside the video area; `text` is only diagnostics.
    std::shared_ptr<const engine::SubtitleAssData> ass;
    double assTimeMs=0;
};
struct SubtitleView {
    double scale=1.0;                  // viewer size multiplier
    std::wstring fontOverride;         // empty = keep the style font
    bool outline=true;
    bool background=false;
    int bottomMargin=0;                // extra logical pixels above the bar
    int blockGap=6;                    // gap between the two languages
    int targetLines=2;                 // used only by the explicit fit-to-lines option
    bool fitToLines=false;             // otherwise preserve authored/user font size
    engine::PreviewView preview;
    double videoWidth=0,videoHeight=0;
};
HWND createSubtitleOverlay(HWND parent);
void updateSubtitleOverlay(HWND,const std::vector<SubtitleLine>&,const SubtitleView&);
} // namespace veyra::ui
