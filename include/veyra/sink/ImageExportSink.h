#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::sink {
struct RgbaImage { uint32_t width=0, height=0; std::vector<uint8_t> pixels; };
enum class ImageAlpha { Preserve, Opaque };
// Image export / explicit diagnostics only. Never used by playback or video export.
bool readRgba8(gfx::D3D12DeviceContext&, gfx::CommandSlotRing&, ID3D12Resource*, RgbaImage&);
// Straight RGBA input. Video screenshots are opaque even if an intermediate
// texture's alpha is not; omit it before viewers can premultiply and quantize RGB.
// Generic image export preserves real transparency by default.
bool saveImage(const std::wstring& path, const RgbaImage&, bool jpeg=false, ImageAlpha alpha=ImageAlpha::Preserve);
bool saveHdrScreenshot(const std::wstring& path,gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,ID3D12Resource*);
bool loadImage(const std::wstring& path, RgbaImage&);
}
