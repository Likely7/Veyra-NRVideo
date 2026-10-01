#include "veyra/sink/VideoEncoder.h"
#include "veyra/sink/NvencD3D12Encoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/Log.h"

#include <format>

namespace veyra::sink {
// Defined in MfVideoEncoder.cpp (kept out of the header: the MFT details are an
// implementation concern of this sink library).
std::unique_ptr<VideoEncoder> createMediaFoundationEncoder();

std::unique_ptr<VideoEncoder> openVideoEncoder(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,pipeline::EnhanceGraph& graph,
                                              const EncoderConfig& config,PacketWriter writer,std::wstring& detail){
    detail.clear();
    const bool nvidia=ctx.adapter().isNvidia;
    EncoderConfig request=config;request.adapterVendorId=ctx.adapter().vendorId;
    // HDR (HEVC Main10) has no Media Foundation representation: the MFT path is
    // 8-bit 4:2:0 only, so an HDR export without NVENC fails loudly instead of
    // writing a silently tone-mapped file.
    if(graph.hdrOutput()&&!nvidia){
        detail=L"HDR (10-bit) export requires NVIDIA NVENC; this GPU only has an 8-bit system encoder. Turn off HDR or use an NVIDIA card to export";
        log::error("export","HDR export refused: the media foundation path is 8-bit only");
        return nullptr;
    }
    // Test-only: force the Media Foundation path on an NVIDIA host so the
    // fallback encoder can be exercised without non-NVIDIA hardware. Never set
    // by the product UI.
    const bool forceMediaFoundation=GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_MF_ENCODER",nullptr,0)>0;
    std::wstring nvencError;
    if(nvidia&&!forceMediaFoundation){
        auto nvenc=std::make_unique<NvencD3D12Encoder>();
        if(nvenc->open(ctx,ring,graph,request,writer)){
            log::info("export",std::format("encoder selected={}",std::string(encoderBackendName(nvenc->backend()))));
            return nvenc;
        }
        nvencError=nvenc->lastError();
        // A refused NVENC session (mixed/old nvEncodeAPI64.dll, codec the GPU
        // cannot encode) must not cost the user the whole export while the OS
        // exposes a hardware MFT for the same codec.
        log::warn("export","NVENC session unavailable; falling back to the Media Foundation hardware encoder");
    }
    auto mf=createMediaFoundationEncoder();
    if(mf->open(ctx,ring,graph,request,writer)){
        log::info("export",std::format("encoder selected={}",std::string(encoderBackendName(mf->backend()))));
        return mf;
    }
    detail=nvencError.empty()?std::format(L"System encoder initialization failed: {}",mf->lastError())
        :std::format(L"Encoder initialization failed; NVENC: {}; system encoder: {}",nvencError,mf->lastError());
    return nullptr;
}
} // namespace veyra::sink
