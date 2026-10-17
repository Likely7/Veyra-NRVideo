#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <cstdint>
#include <string>

namespace veyra::source {
enum class DolbyBaseLayer : uint8_t { None, Unsupported, Hdr10, Sdr, Hlg };
struct DolbyVisionInfo {
    bool present=false, baseLayer=false, enhancementLayer=false, rpuDeclared=false, rpuObserved=false;
    // Profile-5 conversion bookkeeping (custom). `profile5Converted` means the
    // RPU supplied usable conversion parameters for a frame, i.e. the conversion
    // is really running; the other three only keep their log line to one
    // occurrence per open.
    bool profile5Converted=false, profile5MissingColor=false, profile5UnsupportedMapping=false, profile5Placeholder=false;
    uint8_t profile=0, level=0, compatibility=0;
    DolbyBaseLayer route() const {
        if(!present)return DolbyBaseLayer::None;
        if(!baseLayer)return DolbyBaseLayer::Unsupported;
        if(profile==7)return DolbyBaseLayer::Hdr10;
        // Profile 5: 12-bit HEVC IPT-PQ with no HDR10/SDR compatibility layer.
        // The HEVC base layer is still PQ/BT.2020-flagged and can be watched as
        // HDR10 (RPU/enhancement layer is not applied, no native DV output).
        if(profile==5)return DolbyBaseLayer::Hdr10;
        if(profile!=8&&profile!=10)return DolbyBaseLayer::Unsupported;
        switch(compatibility){
        case 1:return DolbyBaseLayer::Hdr10;
        case 2:return DolbyBaseLayer::Sdr;
        case 4:return DolbyBaseLayer::Hlg;
        default:return DolbyBaseLayer::Unsupported;
        }
    }
    std::wstring description() const {
        if(!present)return {};
        const auto layer=route();
        const wchar_t* name=layer==DolbyBaseLayer::Hdr10?L"HDR10":layer==DolbyBaseLayer::Hlg?L"HLG":L"SDR";
        if(layer==DolbyBaseLayer::Unsupported)return L"Dolby Vision P"+std::to_wstring(profile)+L"：此类型需要 RPU 重建，当前不能正确显示";
        // Profile 5 has no compatible base layer at all: its colour exists only
        // inside the RPU, so say what is actually happening rather than
        // claiming a base-layer compatibility that does not exist - and only
        // claim the conversion once its parameters are really in effect.
        if(profile==5)return profile5Converted
            ? L"Dolby Vision P5 · IPT-PQ-C2 基础层按 RPU 转换（"+std::wstring(name)+L" 输出，增强层/RPU 动态元数据不参与输出）"
            : L"Dolby Vision P5：还没有可用的 RPU 转换参数，无法正确转换（不会退回普通 YUV 解码）";
        return L"Dolby Vision P"+std::to_wstring(profile)+L" · "+name+L" 基础层兼容（不应用 RPU/增强层，不输出原生 DV）";
    }
    bool matches(const pipeline::ColorDescription& c) const {
        using namespace pipeline;
        switch(route()){
        case DolbyBaseLayer::Hdr10:return c.transfer==TransferFunction::PQ&&c.matrix==YuvMatrix::BT2020NCL&&c.primaries==ColorPrimaries::BT2020;
        case DolbyBaseLayer::Hlg:return c.transfer==TransferFunction::HLG&&c.matrix==YuvMatrix::BT2020NCL&&c.primaries==ColorPrimaries::BT2020;
        case DolbyBaseLayer::Sdr:return !c.isHdrPath()&&c.matrix==YuvMatrix::BT709&&c.primaries==ColorPrimaries::BT709;
        case DolbyBaseLayer::None:return true;
        default:return false;
        }
    }
};
}
