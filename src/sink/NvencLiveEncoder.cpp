#include "veyra/sink/NvencLiveEncoder.h"
#include "veyra/Log.h"

#include <ffnvcodec/nvEncodeAPI.h>
#include <wrl/client.h>

#include <algorithm>
#include <format>

namespace veyra::sink {
static_assert(NVENCAPI_MAJOR_VERSION==13&&NVENCAPI_MINOR_VERSION==0,"NVENC requires the audited 13.0 ABI headers");
using Microsoft::WRL::ComPtr;

struct NvencLiveEncoder::Impl {
    HMODULE dll=nullptr;
    void* encoder=nullptr;
    NV_ENCODE_API_FUNCTION_LIST api{};
    Config config;
    NV_ENC_BUFFER_FORMAT format=NV_ENC_BUFFER_FORMAT_NV12;
    std::wstring error;
    std::vector<uint8_t> sequence;
    struct Input {
        NV_ENC_REGISTERED_PTR registered=nullptr;
        NV_ENC_INPUT_PTR mapped=nullptr;
        NV_ENC_INPUT_RESOURCE_D3D12 resource{};
    };
    struct Output {
        ComPtr<ID3D12Resource> buffer;
        NV_ENC_REGISTERED_PTR registered=nullptr;
        NV_ENC_INPUT_PTR mapped=nullptr;
        NV_ENC_OUTPUT_RESOURCE_D3D12 resource{};
        uint64_t value=0;
        int64_t ptsUs=0;
        unsigned input=0;
        bool pending=false;
    };
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    uint64_t submitted=0,completed=0,signal=0;
    ComPtr<ID3D12Fence> fence;
    HANDLE event=nullptr;
    unsigned successLogs=0;

    bool fail(std::wstring message){
        error=std::move(message);
        veyra::log::error("nvenc-live",std::string(error.begin(),error.end()));
        return false;
    }
    bool check(NVENCSTATUS code,const char* op){
        if(code==NV_ENC_SUCCESS){
            if(successLogs<16){++successLogs;veyra::log::info("nvenc-live",std::format("{} ok",op));}
            return true;
        }
        const char* detail=encoder&&api.nvEncGetLastErrorString?api.nvEncGetLastErrorString(encoder):"";
        const auto text=std::format("{} status={} {}",op,int(code),detail?detail:"");
        error.assign(text.begin(),text.end());
        veyra::log::error("nvenc-live",text);
        return false;
    }
};

NvencLiveEncoder::NvencLiveEncoder():p_(std::make_unique<Impl>()){}
NvencLiveEncoder::~NvencLiveEncoder(){close();}
std::vector<uint8_t> NvencLiveEncoder::headers() const{return p_->sequence;}
std::wstring NvencLiveEncoder::lastError() const{return p_->error;}
std::wstring NvencLiveEncoder::describe() const{
    return p_->config.hevc?(p_->config.hdr?L"NVIDIA NVENC HEVC 10bit":L"NVIDIA NVENC HEVC"):L"NVIDIA NVENC H.264";
}
size_t NvencLiveEncoder::inFlight() const{return size_t(p_->submitted-p_->completed);}
size_t NvencLiveEncoder::capacity() const{return p_->outputs.size();}

bool NvencLiveEncoder::open(ID3D12Device* device,const Config& config,const std::vector<ID3D12Resource*>& inputTextures){
    auto& p=*p_;
    p.config=config;
    if(!device||inputTextures.empty())return p.fail(L"NVENC: no device or input textures");
    if(config.hdr&&!config.hevc)return p.fail(L"HDR recording requires HEVC Main10");
    if(!config.width||!config.height||(config.width&1)||(config.height&1))
        return p.fail(std::format(L"NVENC: unsupported extent {}x{}",config.width,config.height));
    p.format=config.hdr?NV_ENC_BUFFER_FORMAT_YUV420_10BIT:NV_ENC_BUFFER_FORMAT_NV12;
    wchar_t dir[MAX_PATH]{};GetSystemDirectoryW(dir,MAX_PATH);
    const auto dllPath=std::wstring(dir)+L"\\nvEncodeAPI64.dll";
    p.dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!p.dll)return p.fail(std::format(L"LoadLibrary nvEncodeAPI64.dll win32={}",GetLastError()));
    using Create=NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
    auto create=reinterpret_cast<Create>(GetProcAddress(p.dll,"NvEncodeAPICreateInstance"));
    if(!create)return p.fail(L"NvEncodeAPICreateInstance export missing");
    p.api.version=NV_ENCODE_API_FUNCTION_LIST_VER;
    if(!p.check(create(&p.api),"CreateInstance"))return false;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session{};
    session.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;session.apiVersion=NVENCAPI_VERSION;
    session.device=device;session.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;
    if(!p.check(p.api.nvEncOpenEncodeSessionEx(&session,&p.encoder),"OpenD3D12Session"))return false;
    const GUID codec=config.hevc?NV_ENC_CODEC_HEVC_GUID:NV_ENC_CODEC_H264_GUID;
    int maxWidth=0,maxHeight=0;
    NV_ENC_CAPS_PARAM caps{};caps.version=NV_ENC_CAPS_PARAM_VER;
    caps.capsToQuery=NV_ENC_CAPS_WIDTH_MAX;p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&maxWidth);
    caps.capsToQuery=NV_ENC_CAPS_HEIGHT_MAX;p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&maxHeight);
    if(maxWidth>0&&maxHeight>0&&(int(config.width)>maxWidth||int(config.height)>maxHeight))
        return p.fail(std::format(L"NVENC {} supports at most {}x{} (requested {}x{})",config.hevc?L"HEVC":L"H.264",maxWidth,maxHeight,config.width,config.height));
    if(config.hdr){
        int tenBit=0;caps.capsToQuery=NV_ENC_CAPS_SUPPORT_10BIT_ENCODE;
        p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&tenBit);
        if(!tenBit)return p.fail(L"this GPU's NVENC has no 10-bit HEVC encode");
    }
    // Recording favours quality over latency, but B-frames would reorder the
    // output and make DTS differ from PTS; keep IPPP with a 2 s GOP.
    NV_ENC_PRESET_CONFIG preset{};preset.version=NV_ENC_PRESET_CONFIG_VER;preset.presetCfg.version=NV_ENC_CONFIG_VER;
    if(!p.check(p.api.nvEncGetEncodePresetConfigEx(p.encoder,codec,NV_ENC_PRESET_P4_GUID,NV_ENC_TUNING_INFO_HIGH_QUALITY,&preset),"GetPreset"))return false;
    auto& cfg=preset.presetCfg;
    const uint32_t fps=std::clamp(config.fps,1u,480u);
    cfg.frameIntervalP=1;cfg.gopLength=fps*2;cfg.rcParams.enableLookahead=0;
    const uint32_t bits=std::clamp(config.bitrateMbps,1u,400u)*1000000u;
    cfg.rcParams.rateControlMode=NV_ENC_PARAMS_RC_VBR;
    cfg.rcParams.averageBitRate=bits;
    cfg.rcParams.maxBitRate=bits+bits/2;
    cfg.rcParams.vbvBufferSize=bits/2;
    cfg.rcParams.vbvInitialDelay=cfg.rcParams.vbvBufferSize;
    if(config.hevc){
        auto& hevc=cfg.encodeCodecConfig.hevcConfig;
        hevc.idrPeriod=cfg.gopLength;hevc.repeatSPSPPS=1;
        auto& vui=hevc.hevcVUIParameters;
        vui.videoSignalTypePresentFlag=1;vui.videoFormat=NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;vui.videoFullRangeFlag=0;vui.colourDescriptionPresentFlag=1;
        if(config.hdr){
            cfg.profileGUID=NV_ENC_HEVC_PROFILE_MAIN10_GUID;
            hevc.inputBitDepth=hevc.outputBitDepth=NV_ENC_BIT_DEPTH_10;
            vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT2020;
            vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE2084;
            vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT2020_NCL;
        }else{
            vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT709;
            vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
            vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT709;
        }
    }else{
        auto& h264=cfg.encodeCodecConfig.h264Config;
        h264.idrPeriod=cfg.gopLength;h264.repeatSPSPPS=1;
        auto& vui=h264.h264VUIParameters;
        vui.videoSignalTypePresentFlag=1;vui.videoFormat=NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;vui.videoFullRangeFlag=0;vui.colourDescriptionPresentFlag=1;
        vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
        vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT709;
    }
    NV_ENC_INITIALIZE_PARAMS init{};init.version=NV_ENC_INITIALIZE_PARAMS_VER;
    init.encodeGUID=codec;init.presetGUID=NV_ENC_PRESET_P4_GUID;init.tuningInfo=NV_ENC_TUNING_INFO_HIGH_QUALITY;
    init.encodeWidth=config.width;init.encodeHeight=config.height;init.darWidth=config.width;init.darHeight=config.height;
    init.frameRateNum=fps;init.frameRateDen=1;init.enablePTD=1;init.encodeConfig=&cfg;
    init.maxEncodeWidth=config.width;init.maxEncodeHeight=config.height;init.bufferFormat=p.format;
    if(!p.check(p.api.nvEncInitializeEncoder(p.encoder,&init),"InitializeEncoder"))return false;
    p.sequence.resize(4096);uint32_t size=0;
    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq{};seq.version=NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;seq.inBufferSize=4096;seq.spsppsBuffer=p.sequence.data();seq.outSPSPPSPayloadSize=&size;
    if(!p.check(p.api.nvEncGetSequenceParams(p.encoder,&seq),"GetSequenceParams"))return false;
    p.sequence.resize(size);
    const HRESULT fenceResult=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&p.fence));
    if(FAILED(fenceResult))return p.fail(std::format(L"CreateFence HRESULT=0x{:08X}",unsigned(fenceResult)));
    p.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!p.event)return p.fail(std::format(L"CreateEvent win32={}",GetLastError()));
    auto registerResource=[&](ID3D12Resource* resource,bool output,uint32_t width,uint32_t height,NV_ENC_REGISTERED_PTR& registered,NV_ENC_INPUT_PTR& mapped){
        NV_ENC_REGISTER_RESOURCE r{};r.version=NV_ENC_REGISTER_RESOURCE_VER;r.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        r.resourceToRegister=resource;r.width=width;r.height=height;
        r.bufferFormat=output?NV_ENC_BUFFER_FORMAT_U8:p.format;r.bufferUsage=output?NV_ENC_OUTPUT_BITSTREAM:NV_ENC_INPUT_IMAGE;
        if(!p.check(p.api.nvEncRegisterResource(p.encoder,&r),output?"RegisterOutput":"RegisterInput"))return false;
        registered=r.registeredResource;
        NV_ENC_MAP_INPUT_RESOURCE m{};m.version=NV_ENC_MAP_INPUT_RESOURCE_VER;m.registeredResource=registered;
        if(!p.check(p.api.nvEncMapInputResource(p.encoder,&m),"MapResource"))return false;
        mapped=m.mappedResource;return true;
    };
    p.inputs.resize(inputTextures.size());
    for(size_t i=0;i<inputTextures.size();++i){
        auto& in=p.inputs[i];
        if(!registerResource(inputTextures[i],false,config.width,config.height,in.registered,in.mapped))return false;
        in.resource.version=NV_ENC_INPUT_RESOURCE_D3D12_VER;in.resource.pInputBuffer=in.mapped;
        in.resource.inputFencePoint.version=NV_ENC_FENCE_POINT_D3D12_VER;in.resource.inputFencePoint.bWait=1;
    }
    // A frame larger than its raw NV12 size does not occur at these bitrates
    // (the VBV above bounds a frame to half a second of the target).
    const uint64_t bitstreamBytes=std::max<uint64_t>(1u<<20,(uint64_t(config.width)*config.height*3/2+4095)&~4095ull);
    p.outputs.resize(std::min<size_t>(4,inputTextures.size()));
    for(auto& out:p.outputs){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bitstreamBytes;bd.Height=1;bd.DepthOrArraySize=1;
        bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr=device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out.buffer));
        if(FAILED(hr))return p.fail(std::format(L"bitstream buffer HRESULT=0x{:08X}",unsigned(hr)));
        if(!registerResource(out.buffer.Get(),true,uint32_t(bitstreamBytes),1,out.registered,out.mapped))return false;
        out.resource.version=NV_ENC_OUTPUT_RESOURCE_D3D12_VER;out.resource.pOutputBuffer=out.mapped;
        out.resource.outputFencePoint.version=NV_ENC_FENCE_POINT_D3D12_VER;out.resource.outputFencePoint.pFence=p.fence.Get();
        out.resource.outputFencePoint.bSignal=1;
    }
    veyra::log::info("nvenc-live",std::format("open codec={} hdr={} extent={}x{} fps={} vbrMbps={} inputs={} outputs={} bitstreamBytes={}",
        config.hevc?"HEVC":"H264",config.hdr,config.width,config.height,fps,bits/1000000,p.inputs.size(),p.outputs.size(),bitstreamBytes));
    p.error.clear();
    return true;
}

bool NvencLiveEncoder::submit(unsigned index,ID3D12Fence* waitFence,uint64_t value,int64_t ptsUs,bool forceKey){
    auto& p=*p_;
    if(!p.encoder||index>=p.inputs.size()||!waitFence)return p.fail(L"NVENC submit without a session, input or fence");
    if(p.submitted-p.completed>=p.outputs.size())return p.fail(L"NVENC submit with every output pending");
    auto& in=p.inputs[index];
    auto& out=p.outputs[p.submitted%p.outputs.size()];
    in.resource.inputFencePoint.pFence=waitFence;in.resource.inputFencePoint.waitValue=value;
    out.value=++p.signal;out.resource.outputFencePoint.signalValue=out.value;
    NV_ENC_PIC_PARAMS pic{};pic.version=NV_ENC_PIC_PARAMS_VER;
    pic.inputWidth=p.config.width;pic.inputHeight=p.config.height;
    pic.inputBuffer=&in.resource;pic.outputBitstream=&out.resource;pic.bufferFmt=p.format;
    pic.pictureStruct=NV_ENC_PIC_STRUCT_FRAME;pic.inputTimeStamp=uint64_t(ptsUs);pic.inputDuration=1;
    if(forceKey)pic.encodePicFlags=NV_ENC_PIC_FLAG_FORCEIDR|NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    if(!p.check(p.api.nvEncEncodePicture(p.encoder,&pic),"EncodePicture"))return false;
    out.pending=true;out.ptsUs=ptsUs;out.input=index;++p.submitted;
    return true;
}

bool NvencLiveEncoder::takeOldest(Packet& packet,bool wait,uint32_t timeoutMs,bool& ready){
    auto& p=*p_;
    ready=false;
    if(p.completed==p.submitted)return true;
    auto& out=p.outputs[p.completed%p.outputs.size()];
    if(!out.pending)return p.fail(L"NVENC output bookkeeping lost a frame");
    if(p.fence->GetCompletedValue()<out.value){
        if(!wait)return true;
        if(FAILED(p.fence->SetEventOnCompletion(out.value,p.event)))return p.fail(L"NVENC output fence event failed");
        if(WaitForSingleObject(p.event,timeoutMs)!=WAIT_OBJECT_0)
            return p.fail(std::format(L"NVENC output not ready after {} ms",timeoutMs));
    }
    NV_ENC_LOCK_BITSTREAM lock{};lock.version=NV_ENC_LOCK_BITSTREAM_VER;lock.outputBitstream=&out.resource;lock.doNotWait=0;
    if(!p.check(p.api.nvEncLockBitstream(p.encoder,&lock),"LockBitstream"))return false;
    const auto* bytes=static_cast<const uint8_t*>(lock.bitstreamBufferPtr);
    packet.data.assign(bytes,bytes+lock.bitstreamSizeInBytes);
    packet.ptsUs=int64_t(lock.outputTimeStamp);
    packet.key=lock.pictureType==NV_ENC_PIC_TYPE_IDR||lock.pictureType==NV_ENC_PIC_TYPE_I;
    packet.input=out.input;
    if(!p.check(p.api.nvEncUnlockBitstream(p.encoder,&out.resource),"UnlockBitstream"))return false;
    out.pending=false;++p.completed;ready=true;
    return true;
}

void NvencLiveEncoder::close(){
    if(!p_)return;
    auto& p=*p_;
    if(p.encoder){
        // Outputs that never completed are abandoned with the session.
        for(auto& out:p.outputs){
            if(out.mapped)p.api.nvEncUnmapInputResource(p.encoder,out.mapped);
            if(out.registered)p.api.nvEncUnregisterResource(p.encoder,out.registered);
        }
        for(auto& in:p.inputs){
            if(in.mapped)p.api.nvEncUnmapInputResource(p.encoder,in.mapped);
            if(in.registered)p.api.nvEncUnregisterResource(p.encoder,in.registered);
        }
        p.api.nvEncDestroyEncoder(p.encoder);p.encoder=nullptr;
    }
    p.outputs.clear();p.inputs.clear();
    if(p.event){CloseHandle(p.event);p.event=nullptr;}
    p.fence.Reset();
    if(p.dll){FreeLibrary(p.dll);p.dll=nullptr;}
}

} // namespace veyra::sink
