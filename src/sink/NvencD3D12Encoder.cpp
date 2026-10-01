#include "veyra/sink/NvencD3D12Encoder.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <ffnvcodec/nvEncodeAPI.h>
#include <array>
#include <bit>
#include <format>
#include <algorithm>
namespace veyra::sink {
// Use a complete, pinned ABI. Lowering only open.apiVersion does not make
// newer structure layouts compatible with an older driver.
static_assert(NVENCAPI_MAJOR_VERSION==13&&NVENCAPI_MINOR_VERSION==0,"NVENC requires the audited 13.0 ABI headers");
using namespace veyra::pipeline;
struct NvencD3D12Encoder::Impl {
    HMODULE dll=nullptr;void* encoder=nullptr;NV_ENCODE_API_FUNCTION_LIST api{};
    gfx::D3D12DeviceContext* ctx=nullptr;gfx::CommandSlotRing* ring=nullptr;EnhanceGraph* graph=nullptr;
    bool hdr=false;bool hevc=false;NV_ENC_BUFFER_FORMAT inputFormat=NV_ENC_BUFFER_FORMAT_NV12;
    unsigned w=0,h=0;uint64_t submitted=0,completed=0;
    uint32_t bitrateMbps=0;
    std::wstring error;
    bool fail(std::wstring message){error=std::move(message);veyra::log::error("nvenc",std::string(error.begin(),error.end()));return false;}
    ComPtr<ID3D12Fence> fence;HANDLE event=nullptr;
    ComputePass convert;StateTracker states;ComPtr<ID3D12Resource> y,uv;
    PacketWriter writer;std::vector<uint8_t> sequence;
    struct Slot {ComPtr<ID3D12Resource> input,output;NV_ENC_REGISTERED_PTR registeredIn=nullptr,registeredOut=nullptr;NV_ENC_INPUT_PTR mappedIn=nullptr,mappedOut=nullptr;NV_ENC_INPUT_RESOURCE_D3D12 in{};NV_ENC_OUTPUT_RESOURCE_D3D12 out{};uint64_t value=0;bool pending=false;};
    std::array<Slot,4> slots;
    unsigned successLogs=0;
    bool check(NVENCSTATUS code,const char* op){const char* detail=code!=NV_ENC_SUCCESS&&encoder&&api.nvEncGetLastErrorString?api.nvEncGetLastErrorString(encoder):"";if(!detail)detail="";
        // Success is logged for the first calls only; per-frame encode/lock/unlock
        // at 4X 60 fps produced 720 lines/s (sweep 2026-09-22 E1). Failures always log.
        if(code!=NV_ENC_SUCCESS||successLogs<24||veyra::log::verboseFrameLogs()){if(code==NV_ENC_SUCCESS)++successLogs;veyra::log::info("nvenc",std::format("{} status={} detail={}",op,int(code),detail));}if(code!=NV_ENC_SUCCESS){const auto text=std::format("{} status={} {}",op,int(code),detail);error.assign(text.begin(),text.end());}return code==NV_ENC_SUCCESS;}
    bool drain(Slot& s){
        if(!s.pending)return true;
        if(fence->GetCompletedValue()<s.value){if(FAILED(fence->SetEventOnCompletion(s.value,event))||WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)return false;}
        NV_ENC_LOCK_BITSTREAM lock{};lock.version=NV_ENC_LOCK_BITSTREAM_VER;lock.outputBitstream=&s.out;lock.doNotWait=0;
        if(!check(api.nvEncLockBitstream(encoder,&lock),"LockBitstream"))return false;
        const bool ok=writer(static_cast<uint8_t*>(lock.bitstreamBufferPtr),lock.bitstreamSizeInBytes,static_cast<int64_t>(lock.outputTimeStamp),lock.pictureType==NV_ENC_PIC_TYPE_IDR);
        const bool unlock=check(api.nvEncUnlockBitstream(encoder,&s.out),"UnlockBitstream");
        s.pending=false;++completed;return ok&&unlock;
    }
};
NvencD3D12Encoder::NvencD3D12Encoder():p_(std::make_unique<Impl>()){}
NvencD3D12Encoder::~NvencD3D12Encoder(){close();}
std::vector<uint8_t> NvencD3D12Encoder::headers()const{return p_->sequence;}
std::wstring NvencD3D12Encoder::lastError()const{return p_->error;}
std::wstring NvencD3D12Encoder::describe()const{
    return p_->hevc?L"NVIDIA NVENC HEVC (D3D12)":L"NVIDIA NVENC H.264 (D3D12)";
}
bool NvencD3D12Encoder::open(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,EnhanceGraph& graph,const EncoderConfig& config,PacketWriter writer){
    const bool hevc=config.hevc;const unsigned fpsNum=config.fpsNum,fpsDen=config.fpsDen;
    auto& p=*p_;p.ctx=&ctx;p.ring=&ring;p.graph=&graph;p.w=graph.workWidth();p.h=graph.workHeight();p.writer=std::move(writer);
    p.hevc=hevc;p.bitrateMbps=config.bitrateMbps;
    p.error=L"NVENC initialization incomplete";
    wchar_t dir[MAX_PATH]{};GetSystemDirectoryW(dir,MAX_PATH);const auto dllPath=std::wstring(dir)+L"\\nvEncodeAPI64.dll";
    p.dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!p.dll)return p.fail(std::format(L"LoadLibrary nvEncodeAPI64.dll win32={}",GetLastError()));
    using MaxVersion=NVENCSTATUS(NVENCAPI*)(uint32_t*);
    auto maxVersion=reinterpret_cast<MaxVersion>(GetProcAddress(p.dll,"NvEncodeAPIGetMaxSupportedVersion"));
    uint32_t supported=0;
    if(maxVersion)p.check(maxVersion(&supported),"GetMaxSupportedVersion");
    log::info("nvenc",std::format("DLL={} driverApi={}.{} compiledAbi={}.{} codec={} extent={}x{} fps={}/{}",std::string(dllPath.begin(),dllPath.end()),supported>>4,supported&15,NVENCAPI_MAJOR_VERSION,NVENCAPI_MINOR_VERSION,hevc?"HEVC":"H264",p.w,p.h,fpsNum,fpsDen));
    // Version/capability queries are diagnostics. The real API calls decide
    // whether the driver can open and initialize this exact encoder request.
    using Create=NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);auto create=reinterpret_cast<Create>(GetProcAddress(p.dll,"NvEncodeAPICreateInstance"));
    if(!create)return p.fail(L"NvEncodeAPICreateInstance export missing");
    p.api.version=NV_ENCODE_API_FUNCTION_LIST_VER;if(!p.check(create(&p.api),"CreateInstance"))return false;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};open.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;open.apiVersion=NVENCAPI_VERSION;open.device=ctx.device();open.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;
    // Test injection goes through the same error handling as the real status.
    wchar_t forcedText[8]{};GetEnvironmentVariableW(L"VEYRA_TEST_NVENC_FIRST_OPEN_FAILS",forcedText,8);
    const int forcedMode=_wtoi(forcedText);
    if(forcedMode>0)log::warn("nvenc","test-only: inject OpenD3D12Session failure");
    const auto opened=forcedMode==1?NV_ENC_ERR_INVALID_VERSION:forcedMode>=2?NV_ENC_ERR_UNSUPPORTED_DEVICE:p.api.nvEncOpenEncodeSessionEx(&open,&p.encoder);
    if(!p.check(opened,"OpenD3D12Session"))return false;
    p.hdr=graph.hdrOutput();if(p.hdr&&!hevc)return p.fail(L"HDR export requires HEVC Main10");
    p.inputFormat=p.hdr?NV_ENC_BUFFER_FORMAT_YUV420_10BIT:NV_ENC_BUFFER_FORMAT_NV12;
    const GUID codec=hevc?NV_ENC_CODEC_HEVC_GUID:NV_ENC_CODEC_H264_GUID;
    int maxWidth=0,maxHeight=0;
    NV_ENC_CAPS_PARAM caps{};caps.version=NV_ENC_CAPS_PARAM_VER;caps.capsToQuery=NV_ENC_CAPS_WIDTH_MAX;
    p.check(p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&maxWidth),"WidthMax");
    caps.capsToQuery=NV_ENC_CAPS_HEIGHT_MAX;
    p.check(p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&maxHeight),"HeightMax");
    veyra::log::info("nvenc",std::format("codec={} requested={}x{} maximum={}x{}",hevc?"HEVC":"H264",p.w,p.h,maxWidth,maxHeight));
    if((p.w&1)||(p.h&1)){
        return p.fail(std::format(L"NV12 plane allocation requires even dimensions: {}x{}",p.w,p.h));
    }
    if(p.hdr){int supported=0;caps.capsToQuery=NV_ENC_CAPS_SUPPORT_10BIT_ENCODE;p.check(p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&supported),"10bit support");}
    NV_ENC_PRESET_CONFIG preset{};preset.version=NV_ENC_PRESET_CONFIG_VER;preset.presetCfg.version=NV_ENC_CONFIG_VER;
    if(!p.check(p.api.nvEncGetEncodePresetConfigEx(p.encoder,codec,NV_ENC_PRESET_P4_GUID,NV_ENC_TUNING_INFO_LOW_LATENCY,&preset),"GetPreset"))return false;
    preset.presetCfg.frameIntervalP=1;preset.presetCfg.gopLength=120;preset.presetCfg.rcParams.enableLookahead=0;
    if(p.bitrateMbps>0){
        // Explicit user bitrate: VBR with the target as both average and peak,
        // which is what "encoding bitrate" means to users (the VBV cap keeps peaks
        // bounded so the average is actually met).
        const uint32_t bitsPerSecond=p.bitrateMbps*1000000u;
        preset.presetCfg.rcParams.rateControlMode=NV_ENC_PARAMS_RC_VBR;
        preset.presetCfg.rcParams.averageBitRate=bitsPerSecond;
        preset.presetCfg.rcParams.maxBitRate=bitsPerSecond;
        // Half-second VBV: long enough for VBR to actually spend the requested
        // budget on complex scenes, short enough that the peak stays bounded.
        preset.presetCfg.rcParams.vbvBufferSize=bitsPerSecond/2;
        preset.presetCfg.rcParams.vbvInitialDelay=preset.presetCfg.rcParams.vbvBufferSize;
        veyra::log::info("nvenc",std::format("rate control VBR target={}Mbps bufferBits={}",p.bitrateMbps,preset.presetCfg.rcParams.vbvBufferSize));
    }else{
        preset.presetCfg.rcParams.rateControlMode=NV_ENC_PARAMS_RC_CONSTQP;
        preset.presetCfg.rcParams.constQP={20,22,22};
    }
    if(p.hdr){
        preset.presetCfg.profileGUID=NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        auto& config=preset.presetCfg.encodeCodecConfig.hevcConfig;
        config.inputBitDepth=config.outputBitDepth=NV_ENC_BIT_DEPTH_10;
        auto& vui=config.hevcVUIParameters;
        vui.videoSignalTypePresentFlag=1;vui.videoFormat=NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;vui.videoFullRangeFlag=0;vui.colourDescriptionPresentFlag=1;
        vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT2020;
        vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE2084;
        vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT2020_NCL;
    }
    NV_ENC_INITIALIZE_PARAMS init{};init.version=NV_ENC_INITIALIZE_PARAMS_VER;init.encodeGUID=codec;init.presetGUID=NV_ENC_PRESET_P4_GUID;init.tuningInfo=NV_ENC_TUNING_INFO_LOW_LATENCY;
    init.encodeWidth=p.w;init.encodeHeight=p.h;init.darWidth=p.w;init.darHeight=p.h;init.frameRateNum=fpsNum;init.frameRateDen=fpsDen;init.enablePTD=1;init.encodeConfig=&preset.presetCfg;init.maxEncodeWidth=p.w;init.maxEncodeHeight=p.h;
    init.bufferFormat=p.inputFormat;
    if(!p.check(p.api.nvEncInitializeEncoder(p.encoder,&init),"InitializeEncoder"))return false;
    p.sequence.resize(4096);uint32_t size=0;NV_ENC_SEQUENCE_PARAM_PAYLOAD seq{};seq.version=NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;seq.inBufferSize=4096;seq.spsppsBuffer=p.sequence.data();seq.outSPSPPSPayloadSize=&size;
    if(!p.check(p.api.nvEncGetSequenceParams(p.encoder,&seq),"GetSequenceParams"))return false;p.sequence.resize(size);
    const HRESULT fenceResult=ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&p.fence));
    if(FAILED(fenceResult))return p.fail(std::format(L"CreateFence HRESULT=0x{:08X}",unsigned(fenceResult)));
    p.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!p.event)return p.fail(std::format(L"CreateEvent win32={}",GetLastError()));
    p.error=L"NV12/P010 plane allocation failed";
    p.y=makeTexture(ctx.device(),p.w,p.h,p.hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,true);p.uv=makeTexture(ctx.device(),p.w/2,p.h/2,p.hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,true);if(!p.y||!p.uv)return false;
    p.error=L"RgbToNv12 shader load/create failed";
    std::vector<uint8_t> cs;if(!p.convert.loadShader("RgbToNv12.dxil",cs)||!p.convert.create(ctx.device(),cs,kOutputPoolSlots+2,1,2))return false;
    for(unsigned i=0;i<kOutputPoolSlots;++i)makeSrv(ctx.device(),i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2),graph.outputFormat(),cpuHandleOf(p.convert,i));
    makeUav(ctx.device(),p.y.Get(),p.hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,cpuHandleOf(p.convert,kOutputPoolSlots));makeUav(ctx.device(),p.uv.Get(),p.hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,cpuHandleOf(p.convert,kOutputPoolSlots+1));
    for(auto& s:p.slots){
        p.error=L"NVENC input/output slot allocation failed";
        s.input=makeTexture(ctx.device(),p.w,p.h,p.hdr?DXGI_FORMAT_P010:DXGI_FORMAT_NV12,false);if(!s.input)return false;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=(uint64_t(p.w)*p.h*4+4095)&~4095ull;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT resourceResult=ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s.output));
        if(FAILED(resourceResult))return p.fail(std::format(L"CreateBitstreamResource HRESULT=0x{:08X}",unsigned(resourceResult)));
        auto reg=[&](ID3D12Resource* resource,bool output,NV_ENC_REGISTERED_PTR& registered,NV_ENC_INPUT_PTR& mapped){NV_ENC_REGISTER_RESOURCE r{};r.version=NV_ENC_REGISTER_RESOURCE_VER;r.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;r.resourceToRegister=resource;r.width=output?static_cast<uint32_t>(bd.Width):p.w;r.height=output?1:p.h;r.bufferFormat=output?NV_ENC_BUFFER_FORMAT_U8:p.inputFormat;r.bufferUsage=output?NV_ENC_OUTPUT_BITSTREAM:NV_ENC_INPUT_IMAGE;
            if(!p.check(p.api.nvEncRegisterResource(p.encoder,&r),output?"RegisterOutput":p.hdr?"RegisterP010":"RegisterNV12"))return false;registered=r.registeredResource;
            NV_ENC_MAP_INPUT_RESOURCE m{};m.version=NV_ENC_MAP_INPUT_RESOURCE_VER;m.registeredResource=registered;if(!p.check(p.api.nvEncMapInputResource(p.encoder,&m),"MapResource"))return false;mapped=m.mappedResource;return true;};
        if(!reg(s.input.Get(),false,s.registeredIn,s.mappedIn)||!reg(s.output.Get(),true,s.registeredOut,s.mappedOut))return false;
        s.in.version=NV_ENC_INPUT_RESOURCE_D3D12_VER;s.in.pInputBuffer=s.mappedIn;s.in.inputFencePoint.version=NV_ENC_FENCE_POINT_D3D12_VER;s.in.inputFencePoint.pFence=ctx.fence();s.in.inputFencePoint.bWait=1;
        s.out.version=NV_ENC_OUTPUT_RESOURCE_D3D12_VER;s.out.pOutputBuffer=s.mappedOut;s.out.outputFencePoint.version=NV_ENC_FENCE_POINT_D3D12_VER;s.out.outputFencePoint.pFence=p.fence.Get();s.out.outputFencePoint.bSignal=1;
    }p.error.clear();return true;
}
bool NvencD3D12Encoder::encode(unsigned frameSlot,bool generated,int64_t pts){auto& p=*p_;auto& s=p.slots[p.submitted%4];if(!p.drain(s))return false;
    if(frameSlot>=(generated?kGeneratedPoolSlots:2))return p.fail(L"Encoder frame slot out of range");
    Status st=Status::Ok;uint32_t slot=0;auto* list=p.ring->acquireNext(slot,st);if(!list)return false;
    auto* color=generated?p.graph->generatedFrameResource(frameSlot):p.graph->videoFrameResource(frameSlot);
    p.states.transition(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);p.states.transition(list,p.y.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);p.states.transition(list,p.uv.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // reserved.y carries the output dither step so a graded export does not add
    // banding at the 8-bit (or 10-bit HDR) conversion; ungraded exports keep the
    // exact legacy bytes because the graph reports a zero step.
    const float dither=p.graph?p.graph->outputDitherStep():0.0f;
    const float dims[8]={std::bit_cast<float>(p.w),std::bit_cast<float>(p.h),std::bit_cast<float>(p.hdr?(p.graph->hdr10Output()?2u:1u):0u),std::bit_cast<float>(dither),0,0,0,0};p.convert.bind(list,dims,gpuHandleOf(p.convert,frameSlot+(generated?2:0)).ptr,gpuHandleOf(p.convert,kOutputPoolSlots).ptr);list->Dispatch((p.w+15)/16,(p.h+15)/16,1);
    p.states.uavBarrier(list,p.y.Get());p.states.uavBarrier(list,p.uv.Get());p.states.transition(list,p.y.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);p.states.transition(list,p.uv.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);p.states.transition(list,s.input.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    for(unsigned plane=0;plane<2;++plane){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=plane?p.uv.Get():p.y.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=s.input.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.SubresourceIndex=plane;list->CopyTextureRegion(&b,0,0,0,&a,nullptr);}
    p.states.transition(list,s.input.Get(),D3D12_RESOURCE_STATE_COMMON);p.states.transition(list,color,D3D12_RESOURCE_STATE_COMMON);if(!p.ring->submitAndSignal(slot))return false;
    s.in.inputFencePoint.waitValue=p.ring->lastSignaledValue();s.value=p.submitted+1;s.out.outputFencePoint.signalValue=s.value;
    NV_ENC_PIC_PARAMS pic{};pic.version=NV_ENC_PIC_PARAMS_VER;pic.inputWidth=p.w;pic.inputHeight=p.h;pic.inputBuffer=&s.in;pic.outputBitstream=&s.out;pic.bufferFmt=p.inputFormat;pic.pictureStruct=NV_ENC_PIC_STRUCT_FRAME;pic.inputTimeStamp=pts;pic.inputDuration=1;
    if(!p.check(p.api.nvEncEncodePicture(p.encoder,&pic),"EncodePicture"))return false;s.pending=true;++p.submitted;return true;
}
bool NvencD3D12Encoder::finish(){auto& p=*p_;while(p.completed<p.submitted){if(!p.drain(p.slots[p.completed%4]))return false;}return true;}
void NvencD3D12Encoder::close(){if(!p_)return;auto& p=*p_;if(p.encoder){finish();for(auto& s:p.slots){if(s.mappedOut)p.api.nvEncUnmapInputResource(p.encoder,s.mappedOut);if(s.mappedIn)p.api.nvEncUnmapInputResource(p.encoder,s.mappedIn);if(s.registeredOut)p.api.nvEncUnregisterResource(p.encoder,s.registeredOut);if(s.registeredIn)p.api.nvEncUnregisterResource(p.encoder,s.registeredIn);}p.api.nvEncDestroyEncoder(p.encoder);p.encoder=nullptr;}if(p.event){CloseHandle(p.event);p.event=nullptr;}if(p.dll){FreeLibrary(p.dll);p.dll=nullptr;}}
}
