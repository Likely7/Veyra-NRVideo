#pragma once

// Subtitle engine: external text formats (SRT/ASS/SSA/WebVTT), subtitle tracks
// embedded in the media container, including FFmpeg-decoded PGS/DVD/DVB images.
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <array>
#include <cstdint>

namespace veyra::engine {

// Colors are 0xAARRGGBB. Sizes are in 96-DPI logical pixels; 0 means "follow
// the viewer's global subtitle size".
struct SubtitleStyle {
    std::wstring font;
    double size=0;
    uint32_t primary=0xFFFFFFFFu;
    uint32_t outline=0xC0000000u;
    uint32_t back=0x80000000u;
    double outlineWidth=0;
    double shadow=0;
    int alignment=2;              // numpad layout: 1..9, 2 = bottom centre
    double marginL=0,marginR=0,marginV=0;
    bool bold=false,italic=false,background=false;
};

struct SubtitleBitmap {
    int x=0,y=0,width=0,height=0;
    std::array<uint32_t,256> palette{}; // premultiplied ARGB
    std::vector<uint32_t> runs;        // upper 24 bits = count, low 8 = palette index
    std::vector<uint32_t> pixels()const;
};
struct SubtitleBitmapFrame {
    int width=0,height=0;             // authored display canvas
    std::vector<SubtitleBitmap> images;
};
struct SubtitleCue {
    double begin=0,end=0;
    std::wstring text;            // '\n' separated, override tags already removed
    int style=0;                  // index into SubtitleTrack::styles
    int alignOverride=0;          // 1..9 from {\anN}; 0 = keep the style value
    double posX=-1,posY=-1;       // {\pos(x,y)} in script pixels; -1 = unset
    std::shared_ptr<const SubtitleBitmapFrame> bitmap;
};

// Original ASS/SSA script for full-fidelity rendering (libass): animation,
// karaoke, \move/	/\clip/lur, drawings and container fonts. The parsed
// cues below stay as the plain-text fallback and for diagnostics.
struct SubtitleAssData {
    std::string script;                 // complete external .ass/.ssa (UTF-8)
    std::string header;                 // embedded track: codec private ASS header (UTF-8)
    struct Event { int64_t startMs=0,durationMs=0; std::string chunk; }; // "ReadOrder,Layer,Style,...,Text"
    std::vector<Event> events;          // embedded track: decoded in file order
    struct Font { std::string name; std::vector<char> data; };
    std::vector<Font> fonts;            // container attachments (Matroska fonts)
    std::wstring source;                // identity of the script source (path + stream)
};

struct SubtitleTrack {
    std::wstring name,language,codec,note;
    // Set for ASS/SSA tracks; snapshots are immutable once published.
    std::shared_ptr<const SubtitleAssData> ass;
    bool embedded=false;
    int streamIndex=-1;
    std::vector<SubtitleStyle> styles;
    std::vector<SubtitleCue> cues;
    std::vector<double> starts;   // parallel to cues; sorted, for binary search
    int offsetMs=0;               // viewer offset (per track, not persisted)
    double scriptWidth=1920,scriptHeight=1080; // ASS PlayRes, for \pos scaling
    bool usable() const {return !cues.empty();}
    void rebuildIndex();
};

// Format is chosen from the file extension first, then sniffed from content.
SubtitleTrack loadSubtitleFile(const std::wstring& path);

// Subtitle tracks inside the container (Matroska/MP4/...).
std::vector<SubtitleTrack> loadEmbeddedSubtitleTracks(const std::wstring& path,std::stop_token stop={},
    const std::function<void(const std::vector<SubtitleTrack>&)>& metadata={});

// One worker, one pending source and one published snapshot. Replacing a source
// interrupts its demuxer and prevents stale results from reaching the UI.
class SubtitleLoader {
public:
    struct Result {uint64_t generation=0;bool complete=false;std::vector<SubtitleTrack> tracks;};
    SubtitleLoader();
    ~SubtitleLoader();
    uint64_t request(std::wstring path);
    std::optional<Result> poll();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Cues overlapping `seconds`, newest first, at most `limit`; binary searched.
std::vector<const SubtitleCue*> cuesAt(const SubtitleTrack&,double seconds,size_t limit=4);
std::wstring textAt(const SubtitleTrack&,double seconds);

// Constant-offset alignment: correlates the file's speech activity with the
// track's cue activity. Returns the shift to ADD to cue timestamps.
struct SubtitleAlignResult { bool ok=false; int offsetMs=0; double score=0; std::wstring detail; };
SubtitleAlignResult alignSubtitleToAudio(const std::wstring& mediaPath,const SubtitleTrack&,int maxShiftSeconds=30,std::stop_token stop={});

// Legacy helpers kept for the existing call sites/tests.
std::vector<SubtitleCue> loadSrt(const std::wstring& path);
std::wstring subtitleAt(const std::vector<SubtitleCue>&,double seconds);

// True when the codec name is a text subtitle format we can turn into cues.
bool isTextSubtitleCodec(const std::wstring& codec);
} // namespace veyra::engine
