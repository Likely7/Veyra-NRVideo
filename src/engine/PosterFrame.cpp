#include "veyra/engine/PosterFrame.h"

// FFmpeg headers are C: they must be included inside extern "C", otherwise the
// linker looks for C++-mangled names and every sws_* symbol goes unresolved.
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>

namespace veyra::engine {
// Small RGBA8 preview of one decoded frame for the UI (minimal-mode bar).
// Uses swscale, so every pixel format the decoder can hand us is covered.
// Deliberately CPU-only and called once per open: it must never touch the
// playback path or the GPU queue. Returns false when the frame cannot be
// converted; callers keep an empty poster instead of a fake one.
bool makePosterFrame(const AVFrame* frame, uint32_t maxWidth, uint32_t maxHeight,
                     std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) {
    rgba.clear(); width = 0; height = 0;
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0 || maxWidth == 0 || maxHeight == 0) return false;
    const double scale = std::min({1.0, double(maxWidth) / frame->width, double(maxHeight) / frame->height});
    const int outWidth = std::max(2, int(std::lround(frame->width * scale)));
    const int outHeight = std::max(2, int(std::lround(frame->height * scale)));
    SwsContext* sws = sws_getCachedContext(nullptr, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), outWidth, outHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) return false;
    rgba.assign(size_t(outWidth) * outHeight * 4, 0);
    uint8_t* destination[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int stride[4] = {outWidth * 4, 0, 0, 0};
    const int rows = sws_scale(sws, frame->data, frame->linesize, 0, frame->height, destination, stride);
    sws_freeContext(sws);
    if (rows != outHeight) { rgba.clear(); return false; }
    width = uint32_t(outWidth); height = uint32_t(outHeight);
    return true;
}
}
