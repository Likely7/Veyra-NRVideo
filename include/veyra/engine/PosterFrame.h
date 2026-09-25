#pragma once
#include <cstdint>
#include <vector>
struct AVFrame;
namespace veyra::engine {
// Small RGBA8 preview of one decoded frame for the UI (minimal-mode bar).
// CPU-only, called once per open: never on the playback path. Returns false
// when the frame cannot be converted; callers keep an empty poster rather
// than fabricating one.
bool makePosterFrame(const AVFrame* frame,uint32_t maxWidth,uint32_t maxHeight,std::vector<uint8_t>& rgba,uint32_t& width,uint32_t& height);
}
