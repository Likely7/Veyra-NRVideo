#include "veyra/sink/VideoEncoder.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"

#include <windows.h>
#include <d3d12.h>
#include <strmif.h>   // ICodecAPI (codec property set)
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <cmath>
#include <format>
#include <thread>
#include <vector>

// Media Foundation hardware encoder for every non-NVIDIA adapter (and the
// fallback path when NVENC refuses a session). The MFT setup sequence -
// MFTEnumEx with MFT_ENUM_FLAG_HARDWARE, async unlock, type negotiation,
// ICodecAPI rate control, event-driven ProcessInput/ProcessOutput, drain on
// EOS - is ported from FFmpeg's libavcodec/mfenc.c (LGPL-2.1-or-later,
// FFmpeg n9.0.1, local source C:\veyra-deps\ffmpeg-ps5-slices-source). Veyra is
// GPLv3 and the port is recorded in THIRD_PARTY_NOTICES.md. Divergence: Veyra
// feeds NV12 from its own D3D12 graph (readback pack) instead of AVFrame, and
// the MFT output is written straight into the MP4 writer.
namespace veyra::sink {
namespace {
using namespace veyra::pipeline;

constexpr int64_t kTicksPerSecond=10000000; // Media Foundation 100ns units
constexpr unsigned kWaitEventTimeoutMs=30000;

// Codec property GUIDs, copied verbatim from the Windows SDK's codecapi.h
// STATIC_CODECAPI_* values (10.0.26100.0). Defining them locally avoids
// <initguid.h>, which would emit every GUID of strmif/codecapi into this
// translation unit, and avoids depending on strmiids/wmcodecdspuuid.
constexpr GUID kPropDefaultBPictureCount{0x8d390aac,0xdc5c,0x4200,{0xb5,0x7f,0x81,0x4d,0x04,0xba,0xba,0xb2}};
constexpr GUID kPropLowLatencyMode{0x9c27891a,0xed7a,0x40e1,{0x88,0xe8,0xb2,0x27,0x27,0xa0,0x24,0xee}};
constexpr GUID kPropGOPSize{0x95f31b26,0x95a4,0x41aa,{0x93,0x03,0x24,0x6a,0x7f,0xc6,0xee,0xf1}};
constexpr GUID kPropQualityVsSpeed{0x98332df8,0x03cd,0x476b,{0x89,0xfa,0x3f,0x9e,0x44,0x2d,0xec,0x9f}};
constexpr GUID kPropMeanBitRate{0xf7222374,0x2144,0x4815,{0xb5,0x50,0xa3,0x7f,0x8e,0x12,0xee,0x52}};
constexpr GUID kPropMaxBitRate{0x9651eae4,0x39b9,0x4ebf,{0x85,0xef,0xd7,0xf4,0x44,0xec,0x74,0x65}};
constexpr GUID kPropBufferSize{0x0db96574,0xb6a4,0x4c8b,{0x81,0x06,0x37,0x73,0xde,0x03,0x10,0xcd}};
constexpr GUID kPropRateControlMode{0x1c0608e9,0x370c,0x4710,{0x8a,0x58,0xcb,0x61,0x81,0xc4,0x24,0x23}};
constexpr GUID kPropCommonQuality{0xfcbf57a3,0x7ea5,0x4b0c,{0x96,0x44,0x69,0xb4,0x0c,0x39,0xc3,0x91}};
constexpr GUID kPropForceKeyFrame{0x398c1b98,0x8353,0x475a,{0x9e,0xf2,0x8f,0x26,0x5d,0x26,0x03,0x45}};

std::string hrName(HRESULT hr){return std::format("0x{:08X}",unsigned(hr));}

std::atomic<int> gMfSessions{0};
bool acquireMediaFoundation(){
    if(gMfSessions.fetch_add(1)==0){
        const HRESULT hr=MFStartup(MF_VERSION,MFSTARTUP_LITE);
        if(FAILED(hr)){
            gMfSessions.fetch_sub(1);
            log::error("mf-encoder",std::format("MFStartup failed hr={}",hrName(hr)));
            return false;
        }
    }
    return true;
}
void releaseMediaFoundation(){if(gMfSessions.fetch_sub(1)==1)MFShutdown();}

std::wstring attributeString(IMFAttributes* attributes,const GUID& key){
    LPWSTR raw=nullptr;UINT32 length=0;
    if(!attributes||FAILED(attributes->GetAllocatedString(key,&raw,&length))||!raw)return {};
    std::wstring value(raw,length);CoTaskMemFree(raw);return value;
}

struct MftCandidate {ComPtr<IMFActivate> activate;std::wstring name;};

std::wstring vendorKeyword(uint32_t vendorId){
    switch(vendorId){
    case 0x10DE:return L"NVIDIA";
    case 0x1002:case 0x1022:return L"AMD";
    case 0x8086:return L"Intel";
    default:return {};
    }
}
bool containsFold(const std::wstring& haystack,const std::wstring& needle){
    if(needle.empty()||haystack.size()<needle.size())return false;
    auto lower=[](wchar_t c){return wchar_t(towlower(c));};
    for(size_t i=0;i+needle.size()<=haystack.size();++i){
        bool match=true;
        for(size_t j=0;j<needle.size();++j)if(lower(haystack[i+j])!=lower(needle[j])){match=false;break;}
        if(match)return true;
    }
    return false;
}

// All hardware MFTs that accept NV12 input and produce the requested codec,
// ordered so the active adapter's own vendor comes first. (mfenc.c:
// MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE |
// MFT_ENUM_FLAG_SORTANDFILTER, ...); the vendor preference is Veyra's, because
// a machine can carry several vendors' MFTs and only one has the hardware.)
bool listHardwareMfts(bool hevc,uint32_t adapterVendorId,std::vector<MftCandidate>& candidates,std::wstring& detail){
    MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Video,MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO outputInfo{MFMediaType_Video,hevc?MFVideoFormat_HEVC:MFVideoFormat_H264};
    IMFActivate** activates=nullptr;UINT32 count=0;
    const HRESULT listed=MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,MFT_ENUM_FLAG_HARDWARE|MFT_ENUM_FLAG_SORTANDFILTER,&inputInfo,&outputInfo,&activates,&count);
    if(FAILED(listed)){detail=L"系统编码器枚举失败";log::error("mf-encoder",std::format("MFTEnumEx failed hr={}",hrName(listed)));return false;}
    if(count==0){
        detail=hevc?L"系统里没有可用的 HEVC 硬件编码器":L"系统里没有可用的 H.264 硬件编码器";
        log::warn("mf-encoder","no hardware MFT for the requested codec");CoTaskMemFree(activates);return false;
    }
    const auto keyword=vendorKeyword(adapterVendorId);
    for(UINT32 index=0;index<count;++index){
        const auto name=attributeString(activates[index],MFT_FRIENDLY_NAME_Attribute);
        log::info("mf-encoder",std::format("candidate[{}] {} nv12->{} vendorMatch={}",index,std::string(name.begin(),name.end()),hevc?"HEVC":"H264",containsFold(name,keyword)));
        MftCandidate candidate;candidate.name=name;
        activates[index]->QueryInterface(IID_PPV_ARGS(&candidate.activate));
        if(candidate.activate)candidates.push_back(std::move(candidate));
    }
    for(UINT32 index=0;index<count;++index)activates[index]->Release();
    CoTaskMemFree(activates);
    if(candidates.empty()){detail=L"系统硬件编码器无法激活";return false;}
    std::stable_sort(candidates.begin(),candidates.end(),[&](const MftCandidate& a,const MftCandidate& b){
        return containsFold(a.name,keyword)&&!containsFold(b.name,keyword);
    });
    return true;
}

uint32_t alignUp(uint32_t value,uint32_t alignment){return (value+alignment-1)/alignment*alignment;}

class MfVideoEncoder final : public VideoEncoder {
public:
    ~MfVideoEncoder() override {close();}

    bool open(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,EnhanceGraph& graph,const EncoderConfig& config,PacketWriter writer) override;
    std::vector<uint8_t> headers() const override {return extradata_;}
    bool encode(unsigned frameSlot,bool generated,int64_t pts) override;
    bool finish() override;
    void close() override;
    EncoderBackend backend() const override {return EncoderBackend::MediaFoundation;}
    std::wstring lastError() const override {return error_;}
    std::wstring describe() const override {
        return hevc_?std::format(L"系统硬件编码 HEVC（{}）",friendly_):std::format(L"系统硬件编码 H.264（{}）",friendly_);
    }

private:
    bool setupTypes();
    bool applyRateControl();
    bool readExtradata();
    bool createOutputSample(IMFSample** sample);
    bool waitEvents();
    bool drainOutputs(bool waitForOutput);
    bool writeSample(IMFSample* sample);
    bool convertToNv12(unsigned frameSlot,bool generated);
    bool sampleFromPacked(int64_t pts,IMFSample** sample);

    gfx::D3D12DeviceContext* ctx_=nullptr;
    gfx::CommandSlotRing* ring_=nullptr;
    EnhanceGraph* graph_=nullptr;
    PacketWriter writer_;
    ComPtr<IMFTransform> transform_;
    ComPtr<ICodecAPI> codecApi_;
    ComPtr<IMFMediaEventGenerator> events_;
    DWORD inputId_=0,outputId_=0;
    bool async_=false,needInput_=false,haveOutput_=false,marker_=false;
    bool draining_=false,drainingDone_=false,sampleSent_=false;
    bool outputProvidesSamples_=false;
    MFT_OUTPUT_STREAM_INFO outputInfo_{};
    ComPtr<IMFMediaType> inputType_,outputType_;
    std::vector<uint8_t> extradata_;
    std::wstring friendly_;
    std::wstring error_;
    bool check(HRESULT hr,const wchar_t* stage){if(SUCCEEDED(hr))return true;error_=std::format(L"{} HRESULT=0x{:08X}",stage,unsigned(hr));log::error("mf-encoder",std::string(error_.begin(),error_.end()));return false;}
    bool hevc_=false;
    uint32_t bitrateMbps_=0;
    ExportRateControl rateControl_=ExportRateControl::Cq;
    unsigned fpsNum_=1,fpsDen_=1;
    uint32_t width_=0,height_=0;
    ComputePass convert_;
    StateTracker states_;
    ComPtr<ID3D12Resource> y_,uv_,yRead_,uvRead_;
    UINT yPitch_=0,uvPitch_=0;
    std::vector<uint8_t> packed_;
    HANDLE fenceEvent_=nullptr;
    bool mfAcquired_=false;
    uint64_t submitted_=0,written_=0;
    uint64_t nextKeyframe_=0;
    uint64_t gopLength_=1;
    uint64_t needInputEvents_=0,haveOutputEvents_=0;
};

bool MfVideoEncoder::open(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,EnhanceGraph& graph,const EncoderConfig& config,PacketWriter writer){
    ctx_=&ctx;ring_=&ring;graph_=&graph;writer_=std::move(writer);
    hevc_=config.hevc;bitrateMbps_=config.bitrateMbps;rateControl_=config.rateControl;
    fpsNum_=std::max(1u,config.fpsNum);fpsDen_=std::max(1u,config.fpsDen);
    width_=graph.workWidth();height_=graph.workHeight();
    error_=L"Media Foundation startup failed; see worker log";
    if(graph.hdrOutput()){
        // The MFT path is 8-bit 4:2:0 only; producing an SDR file from an HDR
        // source without telling the user would be a silent quality lie.
        log::error("mf-encoder","HDR export requires NVENC (HEVC Main10); Media Foundation path refused");
        error_=L"HDR 10bit 不支持此系统编码路径（仅 8bit NV12）";
        return false;
    }
    if(width_<16||height_<16||(width_&1)||(height_&1)){
        log::error("mf-encoder",std::format("unsupported extent {}x{} for NV12",width_,height_));
        error_=std::format(L"Unsupported NV12 extent {}x{}",width_,height_);
        return false;
    }
    if(!acquireMediaFoundation())return false;
    mfAcquired_=true;
    std::vector<MftCandidate> candidates;std::wstring detail;
    if(!listHardwareMfts(hevc_,config.adapterVendorId,candidates,detail)){
        error_=detail;
        log::error("mf-encoder",std::format("no hardware MFT: {}",std::string(detail.begin(),detail.end())));
        return false;
    }
    // Negotiate against the candidates in vendor-preference order: an MFT from
    // another vendor's driver enumerates fine but fails with
    // MF_E_HW_MFT_FAILED_START_STREAMING (0xC00D6D76) when it has no device.
    for(auto& candidate:candidates){
        events_.Reset();codecApi_.Reset();inputType_.Reset();outputType_.Reset();extradata_.clear();
        async_=false;needInput_=false;haveOutput_=false;marker_=false;drainingDone_=false;draining_=false;sampleSent_=false;
        ComPtr<IMFTransform> transform;
        if(!check(candidate.activate->ActivateObject(IID_PPV_ARGS(&transform)),L"ActivateObject")||!transform){
            log::warn("mf-encoder",std::format("activate failed for {}",std::string(candidate.name.begin(),candidate.name.end())));
            continue;
        }
        transform_=transform;friendly_=candidate.name;inputId_=0;outputId_=0;
        if(FAILED(transform_->GetStreamIDs(1,&inputId_,1,&outputId_))){inputId_=0;outputId_=0;}
        // Async MFTs must be unlocked before the event interface is used
        // (mfenc.c mf_unlock_async); hardware encoders are async-only.
        ComPtr<IMFAttributes> attributes;
        UINT32 async=0;
        if(SUCCEEDED(transform_->GetAttributes(&attributes))&&SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC,&async))&&async){
            attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK,TRUE);
            if(SUCCEEDED(transform_->QueryInterface(IID_PPV_ARGS(&events_))))async_=true;
            else log::warn("mf-encoder","async MFT without an event generator; continuing synchronously");
        }
        if(FAILED(transform_->QueryInterface(IID_PPV_ARGS(&codecApi_)))){
            log::warn("mf-encoder","no ICodecAPI on this MFT; bitrate settings are unavailable");
        }
        error_=L"MFT type/header negotiation failed";
        if(applyRateControl()&&setupTypes()&&readExtradata()){
            // ~2 s GOP: the first sample and every GOP boundary are forced,
            // which is what makes vendor MFTs emit their first output
            // (mfenc.c forces a key frame on the first sample and on I-frames).
            gopLength_=std::max<uint64_t>(1,uint64_t(std::max(1u,fpsNum_))*2/std::max(1u,fpsDen_));
            nextKeyframe_=0;
            log::info("mf-encoder",std::format("selected MFT {} codec={} extent={}x{} fps={}/{} bitrate={}Mbps",
                std::string(friendly_.begin(),friendly_.end()),hevc_?"HEVC":"H264",width_,height_,fpsNum_,fpsDen_,bitrateMbps_));
            break;
        }
        log::warn("mf-encoder",std::format("MFT {} refused the encoder contract; trying the next candidate",std::string(friendly_.begin(),friendly_.end())));
        transform_.Reset();events_.Reset();codecApi_.Reset();friendly_.clear();
    }
    if(transform_==nullptr){
        log::error("mf-encoder",std::format("no hardware MFT accepted the contract ({} candidates)",candidates.size()));
        return false;
    }
    if(!check(transform_->GetOutputStreamInfo(outputId_,&outputInfo_),L"GetOutputStreamInfo"))return false;
    outputProvidesSamples_=(outputInfo_.dwFlags&(MFT_OUTPUT_STREAM_PROVIDES_SAMPLES|MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))!=0;
    // Same NV12 conversion the NVENC path uses; the result is read back because
    // MFTs take system-memory NV12 (no shared D3D12 texture contract).
    std::vector<uint8_t> shader;
    error_=L"RgbToNv12 shader load/create failed";
    if(!convert_.loadShader("RgbToNv12.dxil",shader)||!convert_.create(ctx.device(),shader,kOutputPoolSlots+2,1,2))return false;
    for(unsigned i=0;i<kOutputPoolSlots;++i)makeSrv(ctx.device(),i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2),graph.outputFormat(),cpuHandleOf(convert_,i));
    error_=L"NV12 conversion/readback allocation failed";
    y_=makeTexture(ctx.device(),width_,height_,DXGI_FORMAT_R8_UNORM,true);
    uv_=makeTexture(ctx.device(),width_/2,height_/2,DXGI_FORMAT_R8G8_UNORM,true);
    if(!y_||!uv_)return false;
    makeUav(ctx.device(),y_.Get(),DXGI_FORMAT_R8_UNORM,cpuHandleOf(convert_,kOutputPoolSlots));
    makeUav(ctx.device(),uv_.Get(),DXGI_FORMAT_R8G8_UNORM,cpuHandleOf(convert_,kOutputPoolSlots+1));
    auto makeReadback=[&](uint32_t rows,uint32_t rowBytes,ComPtr<ID3D12Resource>& out,UINT& pitch){
        pitch=alignUp(rowBytes,256);
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=uint64_t(pitch)*rows;
        bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)));
    };
    if(!makeReadback(height_,width_,yRead_,yPitch_)||!makeReadback(height_/2,width_,uvRead_,uvPitch_))return false;
    packed_.resize(size_t(width_)*height_+size_t(width_)*(height_/2));
    fenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!fenceEvent_){error_=std::format(L"CreateEvent win32={}",GetLastError());return false;}
    if(!check(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0),L"Flush"))return false;
    if(!check(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0),L"BeginStreaming"))return false;
    if(!check(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),L"StartOfStream"))return false;
    error_.clear();
    return true;
}

bool MfVideoEncoder::applyRateControl(){
    if(codecApi_==nullptr)return true;
    // Values must be set before SetOutputType on several MFTs (mfenc.c
    // mf_encv_output_adjust ordering).
    auto set=[&](const GUID& key,VARIANT& value,const char* label){
        const HRESULT hr=codecApi_->SetValue(&key,&value);
        if(FAILED(hr))log::info("mf-encoder",std::format("SetValue {} failed hr={}",label,hrName(hr)));
        return true;
    };
    VARIANT value;VariantInit(&value);value.vt=VT_UI4;
    value.ulVal=0;set(kPropDefaultBPictureCount,value,"BPictureCount");
    value.ulVal=1;set(kPropLowLatencyMode,value,"LowLatencyMode");
    value.ulVal=std::max(1u,fpsNum_*2);set(kPropGOPSize,value,"GOPSize");
    value.ulVal=70;set(kPropQualityVsSpeed,value,"QualityVsSpeed");
    if(rateControl_==ExportRateControl::Cbr){
        const uint32_t bitsPerSecond=bitrateMbps_*1000000u;
        value.ulVal=bitsPerSecond;set(kPropMeanBitRate,value,"MeanBitRate");
        set(kPropMaxBitRate,value,"MaxBitRate");
        value.ulVal=bitsPerSecond/2;set(kPropBufferSize,value,"BufferSize");
        value.ulVal=uint32_t(eAVEncCommonRateControlMode_CBR);set(kPropRateControlMode,value,"RateControlMode");
        log::info("mf-encoder",std::format("rate control CBR target={}Mbps",bitrateMbps_));
    }else if(rateControl_==ExportRateControl::Vbr){
        const uint32_t bitsPerSecond=bitrateMbps_*1000000u;
        value.ulVal=bitsPerSecond;set(kPropMeanBitRate,value,"MeanBitRate");
        set(kPropMaxBitRate,value,"MaxBitRate");
        value.ulVal=bitsPerSecond/2;set(kPropBufferSize,value,"BufferSize"); // 0.5 s CPB
        // Peak-constrained VBR: honours the requested average while still
        // allowing short peaks, the same tradeoff as the NVENC VBR path.
        value.ulVal=uint32_t(eAVEncCommonRateControlMode_PeakConstrainedVBR);
        set(kPropRateControlMode,value,"RateControlMode");
        log::info("mf-encoder",std::format("rate control PeakConstrainedVBR target={}Mbps",bitrateMbps_));
    }else{
        value.ulVal=uint32_t(eAVEncCommonRateControlMode_UnconstrainedVBR);
        set(kPropRateControlMode,value,"RateControlMode");
        value.ulVal=75;set(kPropCommonQuality,value,"Quality");
    }
    return true;
}

bool MfVideoEncoder::setupTypes(){
    const auto frameRate=[&](IMFMediaType* type){
        return type->SetUINT64(MF_MT_FRAME_SIZE,(uint64_t(width_)<<32)|height_)==S_OK
            &&type->SetUINT64(MF_MT_FRAME_RATE,(uint64_t(fpsNum_)<<32)|fpsDen_)==S_OK
            &&type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive)==S_OK
            &&type->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO,(uint64_t(1)<<32)|1)==S_OK;
    };
    bool needInput=true,needOutput=true;
    for(int round=0;round<3&&(needInput||needOutput);++round){
        if(needOutput){
            ComPtr<IMFMediaType> output;
            if(SUCCEEDED(MFCreateMediaType(&output))&&output){
                output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
                output->SetGUID(MF_MT_SUBTYPE,hevc_?MFVideoFormat_HEVC:MFVideoFormat_H264);
                output->SetUINT32(MF_MT_AVG_BITRATE,bitrateMbps_>0?bitrateMbps_*1000000u:(hevc_?20000000u:16000000u));
                output->SetUINT32(MF_MT_MPEG2_PROFILE,hevc_?eAVEncH265VProfile_Main_420_8:eAVEncH264VProfile_High);
                frameRate(output.Get());
                const HRESULT hr=transform_->SetOutputType(outputId_,output.Get(),0);
                if(SUCCEEDED(hr)){outputType_=output;needOutput=false;}
                else if(hr==MF_E_TRANSFORM_TYPE_NOT_SET){log::info("mf-encoder","output type needs the input type first");}
                else{
                    // Vendor MFTs often insist on their own type set; adopt the
                    // first advertised type and stamp our geometry onto it.
                    ComPtr<IMFMediaType> advertised;
                    if(SUCCEEDED(transform_->GetOutputAvailableType(outputId_,0,&advertised))&&advertised){
                        advertised->SetUINT32(MF_MT_AVG_BITRATE,bitrateMbps_>0?bitrateMbps_*1000000u:(hevc_?20000000u:16000000u));
                        frameRate(advertised.Get());
                        const HRESULT retry=transform_->SetOutputType(outputId_,advertised.Get(),0);
                        if(SUCCEEDED(retry)){outputType_=advertised;needOutput=false;}
                        else log::error("mf-encoder",std::format("SetOutputType failed hr={} retry={}",hrName(hr),hrName(retry)));
                    }else log::error("mf-encoder",std::format("SetOutputType failed hr={} and no advertised type",hrName(hr)));
                }
            }
        }
        if(needInput){
            ComPtr<IMFMediaType> input;
            if(SUCCEEDED(MFCreateMediaType(&input))&&input){
                input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
                input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12);
                frameRate(input.Get());
                const HRESULT hr=transform_->SetInputType(inputId_,input.Get(),0);
                if(SUCCEEDED(hr)){inputType_=input;needInput=false;}
                else if(hr==MF_E_TRANSFORM_TYPE_NOT_SET){log::info("mf-encoder","input type needs the output type first");}
                else{
                    ComPtr<IMFMediaType> advertised;
                    if(SUCCEEDED(transform_->GetInputAvailableType(inputId_,0,&advertised))&&advertised){
                        frameRate(advertised.Get());
                        const HRESULT retry=transform_->SetInputType(inputId_,advertised.Get(),0);
                        if(SUCCEEDED(retry)){inputType_=advertised;needInput=false;}
                        else log::error("mf-encoder",std::format("SetInputType failed hr={} retry={}",hrName(hr),hrName(retry)));
                    }else log::error("mf-encoder",std::format("SetInputType failed hr={} and no advertised type",hrName(hr)));
                }
            }
        }
    }
    if(needInput||needOutput){
        log::error("mf-encoder",std::format("format negotiation failed input={} output={}",needInput,needOutput));
        return false;
    }
    return true;
}

bool MfVideoEncoder::readExtradata(){
    ComPtr<IMFMediaType> current;
    if(FAILED(transform_->GetOutputCurrentType(outputId_,&current))||!current){
        log::error("mf-encoder","GetOutputCurrentType failed");
        return false;
    }
    UINT32 size=0;
    if(FAILED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER,&size))||size==0){
        log::error("mf-encoder","the MFT did not publish MF_MT_MPEG_SEQUENCE_HEADER (SPS/PPS)");
        return false;
    }
    extradata_.resize(size);
    if(FAILED(current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER,extradata_.data(),size,nullptr))){
        log::error("mf-encoder","GetBlob(sequence header) failed");
        return false;
    }
    log::info("mf-encoder",std::format("sequence header bytes={}",extradata_.size()));
    return true;
}

bool MfVideoEncoder::createOutputSample(IMFSample** sample){
    *sample=nullptr;
    if(outputProvidesSamples_)return true;
    const DWORD size=outputInfo_.cbSize?outputInfo_.cbSize:size_t(width_)*height_*2;
    ComPtr<IMFMediaBuffer> buffer;
    if(FAILED(MFCreateMemoryBuffer(size,&buffer))){
        log::error("mf-encoder",std::format("MFCreateMemoryBuffer failed size={}",size));
        return false;
    }
    ComPtr<IMFSample> created;
    if(FAILED(MFCreateSample(&created))||FAILED(created->AddBuffer(buffer.Get()))){
        log::error("mf-encoder","MFCreateSample/AddBuffer failed");
        return false;
    }
    *sample=created.Detach();
    return true;
}

bool MfVideoEncoder::waitEvents(){
    const auto start=GetTickCount64();
    for(;;){
        if(needInput_||haveOutput_||drainingDone_||marker_)return true;
        IMFMediaEvent* event=nullptr;
        const HRESULT hr=events_->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event);
        if(hr==MF_E_NO_EVENTS_AVAILABLE){
            if(GetTickCount64()-start>kWaitEventTimeoutMs){
                log::error("mf-encoder","timeout waiting for an encoder MFT event");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if(FAILED(hr)){
            log::error("mf-encoder",std::format("GetEvent failed hr={}",hrName(hr)));
            return false;
        }
        MediaEventType type=0;
        if(SUCCEEDED(event->GetType(&type))){
            switch(type){
            case METransformNeedInput: if(!draining_)needInput_=true; ++needInputEvents_; break;
            case METransformHaveOutput: haveOutput_=true; ++haveOutputEvents_; break;
            case METransformDrainComplete: drainingDone_=true; break;
            case METransformMarker: marker_=true; break;
            default: break;
            }
        }
        event->Release();
    }
}

bool MfVideoEncoder::writeSample(IMFSample* sample){
    DWORD length=0;
    if(FAILED(sample->GetTotalLength(&length))||length==0)return length==0;
    ComPtr<IMFMediaBuffer> buffer;
    if(FAILED(sample->ConvertToContiguousBuffer(&buffer))){
        log::error("mf-encoder","ConvertToContiguousBuffer failed");
        return false;
    }
    BYTE* data=nullptr;
    if(FAILED(buffer->Lock(&data,nullptr,nullptr))){
        log::error("mf-encoder","IMFMediaBuffer::Lock failed");
        return false;
    }
    LONGLONG time=0;
    if(FAILED(sample->GetSampleTime(&time)))time=0;
    const int64_t index=llround(double(time)*double(fpsNum_)/double(fpsDen_)/double(kTicksPerSecond));
    UINT32 clean=0;
    const bool key=SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint,&clean))&&clean!=0;
    const bool accepted=writer_(data,length,index,key);
    buffer->Unlock();
    if(!accepted)log::error("mf-encoder",std::format("mux rejected the encoded frame index={} bytes={}",index,length));
    else ++written_;
    return accepted;
}

bool MfVideoEncoder::drainOutputs(bool waitForOutput){
    for(;;){
        if(async_){
            if(!haveOutput_){
                if(!waitForOutput)return true;
                if(!waitEvents())return false;
                if(!haveOutput_)return true;
            }
        }
        // Synchronous MFTs produce one output per input; ProcessOutput is tried
        // once and "need more input" is treated as normal there.
        // MFTs that allocate their own output samples (NVIDIA/AMD hardware MFTs
        // do) replace out.pSample on success; taking our own empty pointer here
        // silently dropped every encoded frame.
        IMFSample* provided=nullptr;
        if(!createOutputSample(&provided))return false;
        MFT_OUTPUT_DATA_BUFFER out{};
        out.dwStreamID=outputId_;
        out.pSample=provided;
        DWORD status=0;
        const HRESULT hr=transform_->ProcessOutput(0,1,&out,&status);
        if(out.pEvents)out.pEvents->Release();
        haveOutput_=false;
        if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT){
            if(provided)provided->Release();
            if(draining_)drainingDone_=true;
            return true;
        }
        if(hr==MF_E_TRANSFORM_STREAM_CHANGE){
            if(provided)provided->Release();
            log::info("mf-encoder","stream change requested; renegotiating types");
            if(!setupTypes()||!readExtradata())return false;
            continue;
        }
        if(FAILED(hr)){
            if(provided)provided->Release();
            log::error("mf-encoder",std::format("ProcessOutput failed hr={}",hrName(hr)));
            return false;
        }
        IMFSample* result=out.pSample?out.pSample:provided;
        if(result!=provided&&provided)provided->Release();
        const bool accepted=result?writeSample(result):true;
        if(result)result->Release();
        if(!accepted)return false;
        if(!async_)return true;
    }
}

bool MfVideoEncoder::convertToNv12(unsigned frameSlot,bool generated){
    if(frameSlot>=(generated?kGeneratedPoolSlots:2)){error_=L"Encoder frame slot out of range";return false;}
    Status status=Status::Ok;
    uint32_t slot=0;
    auto* list=ring_->acquireNext(slot,status);
    if(!list)return false;
    auto* color=generated?graph_->generatedFrameResource(frameSlot):graph_->videoFrameResource(frameSlot);
    states_.transition(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    states_.transition(list,y_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    states_.transition(list,uv_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // reserved.y = output dither step (see NvencD3D12Encoder): the Media
    // Foundation encoder must dither a graded frame exactly like the NVENC path
    // so preview/export stay consistent.
    const float dims[8]={std::bit_cast<float>(width_),std::bit_cast<float>(height_),0,std::bit_cast<float>(graph_?graph_->outputDitherStep():0.0f),0,0,0,0};
    convert_.bind(list,dims,gpuHandleOf(convert_,frameSlot+(generated?2:0)).ptr,gpuHandleOf(convert_,kOutputPoolSlots).ptr);
    list->Dispatch((width_+15)/16,(height_+15)/16,1);
    states_.uavBarrier(list,y_.Get());
    states_.uavBarrier(list,uv_.Get());
    states_.transition(list,y_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    states_.transition(list,uv_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    states_.transition(list,yRead_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    states_.transition(list,uvRead_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    auto copyPlane=[&](ID3D12Resource* source,ID3D12Resource* destination,UINT pitch,UINT rows,UINT planeWidth){
        D3D12_TEXTURE_COPY_LOCATION from{},to{};
        from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;
        to.pResource=destination;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Footprint.Format=source->GetDesc().Format;
        to.PlacedFootprint.Footprint.Width=planeWidth;
        to.PlacedFootprint.Footprint.Height=rows;
        to.PlacedFootprint.Footprint.Depth=1;
        to.PlacedFootprint.Footprint.RowPitch=pitch;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    };
    copyPlane(y_.Get(),yRead_.Get(),yPitch_,height_,width_);
    copyPlane(uv_.Get(),uvRead_.Get(),uvPitch_,height_/2,width_/2);
    states_.transition(list,color,D3D12_RESOURCE_STATE_COMMON);
    if(!ring_->submitAndSignal(slot))return false;
    // The slot ring signals the device context's queue fence; waiting on our own
    // (never-signalled) fence here was the first-frame timeout.
    const uint64_t value=ring_->lastSignaledValue();
    if(ctx_->fence()->GetCompletedValue()<value){
        if(FAILED(ctx_->fence()->SetEventOnCompletion(value,fenceEvent_))||WaitForSingleObject(fenceEvent_,10000)!=WAIT_OBJECT_0){
            log::error("mf-encoder",std::format("readback fence wait failed value={}",value));
            return false;
        }
    }
    for(unsigned plane=0;plane<2;++plane){
        void* mapped=nullptr;
        ID3D12Resource* resource=plane?uvRead_.Get():yRead_.Get();
        const UINT pitch=plane?uvPitch_:yPitch_;
        const uint32_t rows=plane?height_/2:height_;
        if(FAILED(resource->Map(0,nullptr,&mapped)))return false;
        uint8_t* destination=plane?packed_.data()+size_t(width_)*height_:packed_.data();
        for(uint32_t row=0;row<rows;++row){
            std::memcpy(destination+size_t(row)*width_,static_cast<const uint8_t*>(mapped)+size_t(row)*pitch,width_);
        }
        resource->Unmap(0,nullptr);
    }
    return true;
}

bool MfVideoEncoder::sampleFromPacked(int64_t pts,IMFSample** sample){
    ComPtr<IMFMediaBuffer> buffer;
    if(FAILED(MFCreateMemoryBuffer(DWORD(packed_.size()),&buffer)))return false;
    BYTE* data=nullptr;
    if(FAILED(buffer->Lock(&data,nullptr,nullptr)))return false;
    std::memcpy(data,packed_.data(),packed_.size());
    buffer->Unlock();
    buffer->SetCurrentLength(DWORD(packed_.size()));
    ComPtr<IMFSample> created;
    if(FAILED(MFCreateSample(&created))||FAILED(created->AddBuffer(buffer.Get())))return false;
    const LONGLONG time=pts*kTicksPerSecond*fpsDen_/fpsNum_;
    const LONGLONG duration=kTicksPerSecond*fpsDen_/fpsNum_;
    created->SetSampleTime(time);
    created->SetSampleDuration(duration);
    if(!sampleSent_)created->SetUINT32(MFSampleExtension_Discontinuity,TRUE);
    *sample=created.Detach();
    return true;
}

bool MfVideoEncoder::encode(unsigned frameSlot,bool generated,int64_t pts){
    if(transform_==nullptr)return false;
    if(uint64_t(std::max<int64_t>(0,pts))>=nextKeyframe_&&codecApi_!=nullptr){
        VARIANT value;VariantInit(&value);value.vt=VT_UI4;value.ulVal=1;
        const HRESULT forced=codecApi_->SetValue(&kPropForceKeyFrame,&value);
        if(FAILED(forced))log::info("mf-encoder",std::format("ForceKeyFrame failed hr={} index={}",hrName(forced),pts));
        nextKeyframe_=uint64_t(std::max<int64_t>(0,pts))+gopLength_;
    }
    if(!convertToNv12(frameSlot,generated))return false;
    IMFSample* sample=nullptr;
    if(!sampleFromPacked(pts,&sample))return false;
    for(;;){
        if(async_){
            if(!waitEvents()){sample->Release();return false;}
            if(!drainOutputs(false)){sample->Release();return false;}
            if(!needInput_)continue;
        }
        const HRESULT hr=transform_->ProcessInput(inputId_,sample,0);
        if(hr==MF_E_NOTACCEPTING){
            if(!drainOutputs(true)){sample->Release();return false;}
            continue;
        }
        if(FAILED(hr)){
            log::error("mf-encoder",std::format("ProcessInput failed hr={}",hrName(hr)));
            sample->Release();
            return false;
        }
        needInput_=false;sampleSent_=true;++submitted_;
        break;
    }
    sample->Release();
    return async_?drainOutputs(false):drainOutputs(true);
}

bool MfVideoEncoder::finish(){
    if(transform_==nullptr)return true;
    if(!draining_){
        const HRESULT hr=transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0);
        if(FAILED(hr))log::warn("mf-encoder",std::format("MFT_MESSAGE_COMMAND_DRAIN failed hr={}",hrName(hr)));
        draining_=true;needInput_=false;
    }
    const auto start=GetTickCount64();
    while(!drainingDone_){
        if(async_){
            if(!waitEvents()){
                log::error("mf-encoder",std::format("drain event wait failed submitted={} written={}",submitted_,written_));
                return false;
            }
        }
        if(!drainOutputs(true)){
            log::error("mf-encoder",std::format("drain output failed submitted={} written={}",submitted_,written_));
            return false;
        }
        if(!async_&&!haveOutput_)drainingDone_=true;
        if(GetTickCount64()-start>kWaitEventTimeoutMs*2){
            log::error("mf-encoder",std::format("drain timeout submitted={}",submitted_));
            return false;
        }
    }
    log::info("mf-encoder",std::format("drained submitted={} written={} needInputEvents={} haveOutputEvents={}",submitted_,written_,needInputEvents_,haveOutputEvents_));
    return true;
}

void MfVideoEncoder::close(){
    if(transform_)transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0);
    events_.Reset();
    codecApi_.Reset();
    transform_.Reset();
    inputType_.Reset();outputType_.Reset();
    convert_=ComputePass{};
    y_.Reset();uv_.Reset();yRead_.Reset();uvRead_.Reset();
    if(fenceEvent_){CloseHandle(fenceEvent_);fenceEvent_=nullptr;}
    if(mfAcquired_){releaseMediaFoundation();mfAcquired_=false;}
}
} // namespace

std::unique_ptr<VideoEncoder> createMediaFoundationEncoder(){return std::make_unique<MfVideoEncoder>();}

EncoderCapability queryMediaFoundationEncoder(bool hevc){
    EncoderCapability capability;
    MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Video,MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO outputInfo{MFMediaType_Video,hevc?MFVideoFormat_HEVC:MFVideoFormat_H264};
    IMFActivate** activates=nullptr;UINT32 count=0;
    const bool started=SUCCEEDED(MFStartup(MF_VERSION,MFSTARTUP_LITE));
    if(SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,MFT_ENUM_FLAG_HARDWARE|MFT_ENUM_FLAG_SORTANDFILTER,&inputInfo,&outputInfo,&activates,&count))&&count>0){
        capability.mediaFoundation=true;
        capability.mediaFoundationName=attributeString(activates[0],MFT_FRIENDLY_NAME_Attribute);
        for(UINT32 index=0;index<count;++index)activates[index]->Release();
        CoTaskMemFree(activates);
    }
    if(started)MFShutdown();
    return capability;
}
} // namespace veyra::sink
