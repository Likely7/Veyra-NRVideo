#pragma once
#include "veyra/engine/VideoHdrSettings.h"
#include "veyra/engine/HdrOutputTuning.h"
#include <cmath>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/source/CaptureBuffer.h"
#include "veyra/engine/ColorSettings.h"
namespace veyra::engine {
inline constexpr uint32_t kMaxColorInstances = 6;
inline constexpr uint32_t kMaxNrInstances = 4;
// Anti-flicker route for the NR temporal pass. The tiers come from Magpie's
// DLSSNRTemporal (GPL-3.0, commit 3841698): see shaders/NrTemporal.hlsl.
//   Off        - no temporal accumulation at all.
//   Static     - accumulate only where the source is not moving; motion is
//                published immediately. The safest tier.
//   Flow       - motion-reprojected accumulation with a fixed 80 ms EMA. This is
//                the behaviour the pass had before the tiers existed, so it
//                stays the value a legacy `temporal=true` maps onto.
//   FlowPlus   - as Flow, plus a 60 ms attack / 180 ms release memory and an
//                explicit rejection of a correction whose direction reverses.
//   LowFrequency - as Flow, but the accumulated signal is a half-resolution
//                residual; this frame's high frequency rides through untouched.
enum class NrAntiFlicker : uint8_t { Off, Static, Flow, FlowPlus, LowFrequency };
// Narrowing for the wire and preset formats, which store this as one integer.
inline bool validNrAntiFlicker(NrAntiFlicker v) {
    return v>=NrAntiFlicker::Off&&v<=NrAntiFlicker::LowFrequency;
}
// Preserve persisted values 0/1/2; the old Fsr value now explicitly selects
// the 3.1 provider. Fsr4 requires a real ML provider, never an auto fallback.
enum class FrameGenerationBackend { Dlss = 0, XeSS = 1, Fsr = 2, Fsr4 = 3, Vfg = 4 };
constexpr bool fsrFrameGeneration(FrameGenerationBackend backend) {
    return backend == FrameGenerationBackend::Fsr || backend == FrameGenerationBackend::Fsr4;
}
constexpr bool crossVendorFrameGeneration(FrameGenerationBackend backend) {
    return backend == FrameGenerationBackend::XeSS || fsrFrameGeneration(backend);
}
// Frame generation that runs inside the present sink (external frame
// interpolation swapchain) instead of the in-graph DLSSG backend.
constexpr bool presentSinkFrameGeneration(FrameGenerationBackend backend) {
    return backend==FrameGenerationBackend::XeSS;
}
// Multipliers the UI offers. 5X is intentionally absent: the DLSS runtime
// exposes 2/3/4/6 and MFG buyers pick from those; 1 = generation off.
inline constexpr uint32_t kFgMultiplierChoices[]={1,2,3,4,6};
inline constexpr uint32_t kVfgMultiplierChoices[]={1,2,3,4,5,6,7,8};
inline constexpr size_t kFgMultiplierChoiceCount=sizeof(kFgMultiplierChoices)/sizeof(kFgMultiplierChoices[0]);
// Export bitrate presets in Mbps; index 0 keeps the encoder's constant-quality
// default. Labels are the UI strings for the same order.
inline constexpr uint32_t kExportBitrateChoices[]={0,6,10,16,24,40,60,100,150,200};
inline constexpr size_t kExportBitrateChoiceCount=sizeof(kExportBitrateChoices)/sizeof(kExportBitrateChoices[0]);
inline constexpr const wchar_t* kExportBitrateLabels[]={L"自动 · 恒定质量",L"6 Mbps",L"10 Mbps",L"16 Mbps",L"24 Mbps",L"40 Mbps",L"60 Mbps",L"100 Mbps",L"150 Mbps",L"200 Mbps"};
inline size_t exportBitrateIndex(uint32_t mbps){for(size_t i=0;i<kExportBitrateChoiceCount;++i)if(kExportBitrateChoices[i]==mbps)return i;return 0;}
// videoSrQuality values: 0 = DLSS SR, 1..4 = RTX video SR quality steps,
// 5 = AMD FSR upscaling (vendor neutral; verified on an NVIDIA adapter).
inline constexpr uint32_t kVideoSrFsr=5;
// Capture audio ingress policy. Automatic is what the pipeline did before the
// manual selector existed; the other two exist because Dolby/DTS passthrough
// negotiation depends on the device and the console, and users reported that
// "let the capture card decide" is not reliable in practice.
enum class CaptureAudioIngress { Auto, PcmOnly, BitstreamPreferred };
constexpr std::wstring_view captureAudioIngressName(CaptureAudioIngress mode) {
    switch(mode) {
    case CaptureAudioIngress::PcmOnly: return L"强制线性 PCM";
    case CaptureAudioIngress::BitstreamPreferred: return L"位流优先（Dolby/DTS 直通解码）";
    case CaptureAudioIngress::Auto: break;
    }
    return L"自动（优先 PCM，必要时位流解码）";
}
// The original slot (0) became Lecram in 2.0.0. Give the restored NVIDIA
// binary a new value so existing presets keep selecting the same DLL.
enum class NrRuntime { Original = 0, Community = 1, Ampere = 2, NvidiaOriginal = 3, LmxxfAmd = 4 };
constexpr bool validNrRuntime(NrRuntime runtime) {
    return runtime == NrRuntime::Original || runtime == NrRuntime::Community ||
           runtime == NrRuntime::Ampere || runtime == NrRuntime::NvidiaOriginal || runtime == NrRuntime::LmxxfAmd;
}
// Persisted value 1 selected the retired RTX40 DLL. Never load that DLL in
// current builds; preserve 0 (Lecram) and 2 (SF-v2) as explicit user choices.
constexpr NrRuntime currentNrRuntime(NrRuntime runtime) {
    return runtime == NrRuntime::Community ? NrRuntime::Ampere : runtime;
}
constexpr std::string_view nrRuntimeName(NrRuntime runtime) {
    switch(runtime) {
    case NrRuntime::Original:return "community-Lecram-RTX50";
    case NrRuntime::Community:return "community-SF-v2-RTX20-RTX50-legacy40";
    case NrRuntime::Ampere:return "community-SF-v2-RTX20-RTX50";
    case NrRuntime::NvidiaOriginal:return "NVIDIA-original-RTX50-310.8.0";
    case NrRuntime::LmxxfAmd:return "lmxxf-AMD-RDNA4";
    }
    return "unknown";
}
enum class FlowQuality { Performance, Balanced, Quality };
enum class OpticalFlowBackend { Nvidia, AmdFidelityFx, GpuDis };
enum class ContentRate { Transport, Auto, Fps30, Fps50, Fps60, Capture60To30 };
enum class AudioSyncMode { Automatic, Manual, Off };
enum class HdrOutputMode { Hdr10, ScRgb };
enum class MotionSource { Zero, OpticalFlow, Automatic };
constexpr bool motionUsesFlow(MotionSource source,FrameGenerationBackend backend=FrameGenerationBackend::Dlss) {
    return backend!=FrameGenerationBackend::Vfg&&
        (source==MotionSource::OpticalFlow||(source==MotionSource::Automatic&&backend!=FrameGenerationBackend::XeSS));
}
constexpr std::string_view frameGenerationBackendName(FrameGenerationBackend backend) {
    switch(backend) {
    case FrameGenerationBackend::Dlss: return "DLSS";
    case FrameGenerationBackend::XeSS: return "XeSS";
    case FrameGenerationBackend::Fsr: return "AMD-FSR";
    case FrameGenerationBackend::Fsr4: return "AMD-FSR4-ML";
    case FrameGenerationBackend::Vfg: return "NVIDIA-VFG";
    }
    return "unknown";
}
constexpr std::string_view opticalFlowBackendName(OpticalFlowBackend backend) {
    switch(backend) {
    case OpticalFlowBackend::Nvidia: return "NVIDIA_NVOF";
    case OpticalFlowBackend::AmdFidelityFx: return "AMD_FIDELITYFX_OF";
    case OpticalFlowBackend::GpuDis: return "GPU_DIS_FAST";
    }
    return "unknown";
}
struct NrSettings {
    float intensity=1,tone=1,structure=1,skin=-1;
    int32_t style=0,autoMask=0,uiCorrection=0;
    bool operator==(const NrSettings&) const = default;
};
struct NrCorrectionSettings {
    bool enabled=false,automatic=true;
    // Manual values survive switching automatic control on and off.
    float hue=1,chroma=.75f,highlight=1,compression=.75f,stability=.8f;
    // New manual protections default to zero so schema29/schema8 keep their
    // original five-control meaning. Auto presets are explicit per style.
    float neutral=0,colorKeep=0,lumaKeep=0,shadow=0,autoAmount=1;
    bool operator==(const NrCorrectionSettings&) const = default;
    bool valid() const {
        for(float v:{hue,chroma,highlight,compression,stability,neutral,colorKeep,lumaKeep,shadow,autoAmount})
            if(!std::isfinite(v)||v<0||v>1)return false;
        return true;
    }
    bool usesHistory() const {return enabled&&(automatic?autoAmount>0:stability>0);}
    bool extendedControls() const {return neutral!=0||colorKeep!=0||lumaKeep!=0||shadow!=0||autoAmount!=1;}
    static std::array<float,9> automaticValues(int32_t style) {
        // Style 1 changes contrast/neutral saturation more; style 2 leaves
        // larger colored-area drift. Preserve source chromaticity separately
        // from texture/lightness; never replace the requested model style.
        if(style==1)return {1,1,1,1,1,1,.9f,.25f,.55f};
        if(style==2)return {1,1,1,1,1,1,.95f,.2f,.4f};
        return {1,1,1,1,1,1,0,0,0};
    }
    void useAutomaticValues(int32_t style) {
        auto v=automaticValues(style);for(auto& value:v)value*=autoAmount;
        hue=v[0];chroma=v[1];highlight=v[2];compression=v[3];stability=v[4];
        neutral=v[5];colorKeep=v[6];lumaKeep=v[7];shadow=v[8];automatic=false;
    }
    std::array<float,16> constants(bool hdr=false,int32_t style=0) const {
        auto v=automatic?automaticValues(style):std::array<float,9>{hue,chroma,highlight,compression,stability,neutral,colorKeep,lumaKeep,shadow};
        if(automatic)for(auto& value:v)value*=autoAmount;
        return {enabled?1.f:0.f,v[0],v[1],v[2],v[3],v[4],automatic?1.f:0.f,hdr?1.f:0.f,
                v[5],v[6],v[7],v[8],0,0,0,0};
    }
};
struct ResidualSettings {
    float total=1,darken=1,brighten=1,color=1,luminance=1;
    NrCorrectionSettings correction{};
    bool operator==(const ResidualSettings&) const = default;
    bool extendedSchema() const {return total>2||correction!=NrCorrectionSettings{};}
};
// Fixed storage: settings also cross the export worker's shared-memory POD.
// Runtime and NR/SR order are shared by active instances, not separate adapters.
struct NrLayerSettings {
    NrSettings model{};
    ResidualSettings residual{};
    NrRuntime runtime=NrRuntime::Original;
    bool temporal=false,lowLatencyPairing=false,enabled=false;
    // The tier only applies when `temporal` is set; `temporal=false` forces Off.
    // Kept as a separate field so an older preset that only knew the boolean
    // keeps meaning exactly what it meant (Flow).
    NrAntiFlicker antiFlicker=NrAntiFlicker::Flow;
    pipeline::NrSizePolicy sizePolicy=pipeline::NrSizePolicy::Realtime;
    bool operator==(const NrLayerSettings&) const = default;
    bool usesTemporal() const {return residual.correction.enabled?residual.correction.usesHistory():temporal;}
};
struct ProtectionRect {
    float left=0,top=0,right=0,bottom=0;
    // The ellipse inscribed in the rectangle instead of the rectangle itself.
    bool ellipse=false;
    bool empty()const{return left==right||top==bottom;}
    bool operator==(const ProtectionRect&)const=default;
};
// Shader constants for one region (NrProtection.hlsli). An ellipse is sent with
// left and right swapped: the constant layout stays the same and a rectangle's
// constants stay byte-for-byte what they were.
inline std::array<float,4> protectionConstants(const ProtectionRect& q){
    return q.ellipse?std::array<float,4>{q.right,q.top,q.left,q.bottom}:std::array<float,4>{q.left,q.top,q.right,q.bottom};
}
struct ProtectionSettings {
    bool enabled=false;
    float featherPixels=2;
    std::array<ProtectionRect,4> regions{};
    bool operator==(const ProtectionSettings&)const=default;
    std::string validate()const{
        // Upper bound is 64 px at the working extent; TiledImageProcessor keeps
        // its tile halo above this value so export tiles cannot clip the ramp.
        if(!std::isfinite(featherPixels)||featherPixels<0||featherPixels>64)return "invalid protection feather";
        // An ellipse may extend past the frame: a tiled export maps it into
        // tile space without clipping, since clipping would change its shape.
        for(auto r:regions){const float low=r.ellipse?-64.0f:0.0f,high=r.ellipse?65.0f:1.0f;
            for(float v:{r.left,r.top,r.right,r.bottom})if(!std::isfinite(v)||v<low||v>high)return "invalid protection rectangle";
            if(r.left>r.right||r.top>r.bottom)return "inverted protection rectangle";}
        return {};
    }
};
struct EnhancementSettings {
    uint64_t revision=1;
    NrSettings model;
    ResidualSettings residual;
    ProtectionSettings protection;
    bool nr=false,sr=false;
    bool lowLatency=false; // preview only: NR before SR, opt-in
    NrRuntime nrRuntime=NrRuntime::Original;
    bool nrTemporal=false; // optional motion-reprojected residual stabilization
    // Which anti-flicker tier the temporal pass runs, for the flat (non-list)
    // path. Ignored when nrTemporal is false.
    NrAntiFlicker nrAntiFlicker=NrAntiFlicker::Flow;
    // Zero keeps the original flat, single-layer API byte-for-byte in use.
    // Multi-layer lists retain disabled nodes; the flat fields mirror the first
    // enabled layer (or the first node when every layer is bypassed).
    std::array<NrLayerSettings,kMaxNrInstances> nrLayers{};
    uint32_t nrLayerCount=0;
    bool usesNrTemporal() const {
        if(!nrLayerCount)return residual.correction.enabled?residual.correction.usesHistory():nrTemporal;
        for(uint32_t i=0;i<nrLayerCount&&i<nrLayers.size();++i)
            if(nrLayers[i].enabled&&nrLayers[i].usesTemporal())return true;
        return false;
    }
    NrLayerSettings nrLayer(uint32_t index) const {
        if(nrLayerCount&&index<nrLayers.size())return nrLayers[index];
        return {model,residual,nrRuntime,nrTemporal,lowLatency,nr,nrAntiFlicker,nrPolicy};
    }
    uint32_t activeNrLayerCount() const {
        if(!nr)return 0;
        if(!nrLayerCount)return 1;
        uint32_t count=0;
        for(uint32_t i=0;i<nrLayerCount&&i<nrLayers.size();++i)if(nrLayers[i].enabled)++count;
        return count;
    }
    uint32_t activeAutoNrLayerCount() const {
        if(!nr)return 0;
        if(!nrLayerCount)return nrPolicy==pipeline::NrSizePolicy::Auto?1:0;
        uint32_t count=0;
        for(uint32_t i=0;i<nrLayerCount&&i<nrLayers.size();++i)
            count+=nrLayers[i].enabled&&nrLayers[i].sizePolicy==pipeline::NrSizePolicy::Auto;
        return count;
    }
    bool sameNrTopology(const EnhancementSettings& other) const {
        if(nrLayerCount!=other.nrLayerCount)return false;
        if(residual.correction.enabled!=other.residual.correction.enabled||usesNrTemporal()!=other.usesNrTemporal())return false;
        for(uint32_t i=0;i<nrLayerCount&&i<nrLayers.size();++i){
            const auto& a=nrLayers[i]; const auto& b=other.nrLayers[i];
            if(a.enabled!=b.enabled||a.temporal!=b.temporal||a.usesTemporal()!=b.usesTemporal()||
               a.residual.correction.enabled!=b.residual.correction.enabled||a.runtime!=b.runtime||
               a.lowLatencyPairing!=b.lowLatencyPairing||a.sizePolicy!=b.sizePolicy)return false;
        }
        return true;
    }
    // Frame-generation admission strictness.
    //   false (default): a group is admitted when the whole group can still
    //     reach its LAST deadline, which is what 1.4.0 did. More generated
    //     frames; some early outputs in a group may arrive late.
    //   true: the FIRST generated output must also reach its own deadline,
    //     which at 6X is one sixth of a source interval. Rejects whole groups
    //     whose early outputs are already doomed, so the cadence is stricter
    //     but fewer frames are generated - measured 3.75 generated per source
    //     frame at 6X against 1.4.0 hitting the full 3.0 of 3 at 4X.
    // Users reported 1.4.0 feeling better, so the looser rule is the default.
    bool fgStrictAdmission=false;
    bool captureCompatible=false;
    // Capture audio ingress; requires a reconnect to take effect (the media type
    // is negotiated when the graph is built).
    CaptureAudioIngress captureAudio=CaptureAudioIngress::Auto;
    // Capture video-pin allocator policy; requires a reconnect to take effect
    // (the allocator is created while the capture graph is built).
    source::CaptureBufferMode captureBuffer=source::CaptureBufferMode::Auto;
    // Capture ingest only: flip the incoming frame vertically. Exists because
    // some devices declare a DIB orientation that does not match their samples
    // (RGB24 upside-down reports); the capture source asks for top-down first,
    // this is the manual fallback when a driver still misreports. Applies to
    // the next sample, so it is a live edit, not a graph rebuild.
    bool captureFlipVertical=false;
    bool forceSdrPreview=false; // display only; retain actual HDR source metadata
    HdrOutputMode hdrOutputMode=HdrOutputMode::Hdr10;
    MotionSource fgMotion=MotionSource::Automatic;
    MotionSource srMotion=MotionSource::OpticalFlow;
    MotionSource nrMotion=MotionSource::OpticalFlow;
    VideoHdrSettings videoHdr;
    // Custom: static HDR output tuning (see HdrOutputTuning.h). Both blocks are
    // live: the curve reaches the present blit and the metadata the display.
    HdrCurveSettings hdrCurve;
    HdrMetadataSettings hdrMetadata;
    bool useHdrPreview(bool hdrInput,bool hdrDisplayActive)const{return (hdrInput||videoHdr.enabled)&&hdrDisplayActive&&!forceSdrPreview;}
    pipeline::SrTarget srTarget=pipeline::SrTarget::Uhd4K;
    uint32_t videoSrQuality=0; // 0 DLSS SR; 1–4 RTX Video SR
    uint32_t multiplier=1;
    FrameGenerationBackend frameGenerationBackend=FrameGenerationBackend::Dlss;
    uint32_t vfgQuality=1; // NvVFX Mode: Low=0, Medium=1 (default), High=2
    // Export target bitrate in Mbps; 0 keeps the encoder's constant-quality
    // default (NVENC CONSTQP / MFT quality mode). Only the export job consumes
    // it: preview never re-encodes.
    uint32_t exportBitrateMbps=0;
    // Lightroom-aligned colour grade. Live: the engine uploads it per frame, so
    // changing it never rebuilds the graph (see sameVideoConfiguration below).
    ColorSettings color;
    // Instance zero remains the legacy field, including for live Win32 edits.
    // Fixed capacity preserves the export shared-memory POD contract.
    std::array<ColorSettings,kMaxColorInstances-1> additionalColors{};
    uint32_t additionalColorCount=0;
    // Stage-5 output stabiliser (anti-flicker). Holds the previous output where
    // the source has not visibly changed. 0 is off, and off is the default: the
    // graph then skips the dispatch entirely, so a build with this feature
    // disabled is byte-identical to one without it. `nrHoldTolerance` is a
    // relative 3x3 box-luma delta; the default is the value the pass was
    // tuned against (see shaders/NrHold.hlsl).
    float nrHoldStrength=0.0f;
    float nrHoldTolerance=0.02f;
    pipeline::NrSizePolicy nrPolicy=pipeline::NrSizePolicy::Realtime;
    FlowQuality flow=FlowQuality::Balanced;
    OpticalFlowBackend opticalFlowBackend=OpticalFlowBackend::Nvidia;
    bool amdFlowHalfResolution=false;
    ContentRate content=ContentRate::Transport;
    AudioSyncMode audioSync=AudioSyncMode::Automatic;
    int32_t audioOffsetMs=0;
    bool operator==(const EnhancementSettings&) const = default;
    bool sameVideoConfiguration(const EnhancementSettings& other) const {
        auto video=*this;
        video.revision=other.revision;
        video.audioSync=other.audioSync;
        video.audioOffsetMs=other.audioOffsetMs;
        // Capture flip is applied per sample on the source's callback thread;
        // toggling it must not rebuild the graph.
        video.captureFlipVertical=other.captureFlipVertical;
        // Export-only fields: changing the bitrate must never invalidate the
        // running preview graph (the controller would otherwise rebuild it).
        video.exportBitrateMbps=other.exportBitrateMbps;
        // Colour grade: parameters are uniform-only (never a rebuild), but the
        // master switch changes the graph shape, so it stays in the comparison.
        video.color=other.color;
        video.color.enabled=color.enabled;
        video.color.lutName=color.lutName;
        video.color.lutInputSpace=color.lutInputSpace;
        video.additionalColors=other.additionalColors;
        for(size_t i=0;i<additionalColors.size();++i){
            video.additionalColors[i].enabled=additionalColors[i].enabled;
            video.additionalColors[i].lutName=additionalColors[i].lutName;
            video.additionalColors[i].lutInputSpace=additionalColors[i].lutInputSpace;
        }
        video.videoHdr=other.videoHdr;
        video.videoHdr.enabled=videoHdr.enabled;
        return video==other;
    }
    void rejectVideoRequest(const EnhancementSettings& attempted,const EnhancementSettings& previous) {
        if(revision!=attempted.revision)return;
        auto restored=previous;
        if(audioSync!=attempted.audioSync||audioOffsetMs!=attempted.audioOffsetMs){
            restored.audioSync=audioSync;restored.audioOffsetMs=audioOffsetMs;
        }
        *this=restored;
    }
    std::string validate() const {
        if(hdrOutputMode<HdrOutputMode::Hdr10||hdrOutputMode>HdrOutputMode::ScRgb)return "invalid HDR output mode";
        for(auto source:{fgMotion,srMotion,nrMotion})if(source<MotionSource::Zero||source>MotionSource::Automatic)return "invalid motion source";
        if(!videoHdr.valid())return "invalid RTX Video HDR parameters";
        if(!hdrCurve.valid())return "invalid HDR curve parameters";
        if(!hdrMetadata.valid())return "invalid HDR metadata parameters";
        auto range=[](float v,float hi){return std::isfinite(v)&&v>=0&&v<=hi;};
        if(auto error=protection.validate();!error.empty())return error;
        if(!revision)return "settingsRevision must be nonzero";
        if(!validNrRuntime(nrRuntime))return "invalid NR runtime";
        if(captureAudio<CaptureAudioIngress::Auto||captureAudio>CaptureAudioIngress::BitstreamPreferred)return "invalid capture audio ingress mode";
        if(captureBuffer<source::CaptureBufferMode::Auto||captureBuffer>source::CaptureBufferMode::DriverDefault)return "invalid capture buffer mode";
        if(audioSync<AudioSyncMode::Automatic||audioSync>AudioSyncMode::Off||audioOffsetMs<-250||audioOffsetMs>250)return "invalid audio sync setting";
        // Stage-5 output stabiliser: 0 is off, 1 is full hold; the tolerance is
        // a relative box-luma delta, so anything above 1 would hold every pixel.
        if(!range(nrHoldStrength,1))return "output stabiliser strength out of range";
        if(!range(nrHoldTolerance,1))return "output stabiliser tolerance out of range";
        if(!range(model.intensity,1)||!range(model.tone,1)||!range(model.structure,1))return "model parameter out of range";
        if(model.skin!=-1&&!range(model.skin,2))return "skin parameter out of range";
        if(model.style<0||model.style>2||model.autoMask<0||model.autoMask>1||model.uiCorrection<0||model.uiCorrection>1)return "invalid experimental parameter";
        if(!range(residual.total,5)||!residual.correction.valid())return "NR residual/correction parameter out of range";
        for(float v:{residual.darken,residual.brighten,residual.color,residual.luminance})if(!range(v,2))return "residual parameter out of range";
        if(nrLayerCount>nrLayers.size())return "too many NR instances";
        for(uint32_t i=0;i<nrLayerCount;++i){
            const auto& n=nrLayers[i];
            if(!pipeline::validNrSizePolicy(n.sizePolicy))return "invalid NR layer size policy";
            if(!range(n.model.intensity,1)||!range(n.model.tone,1)||!range(n.model.structure,1)||
               (n.model.skin!=-1&&!range(n.model.skin,2)))return "NR layer model parameter out of range";
            if(n.model.style<0||n.model.style>2||n.model.autoMask<0||n.model.autoMask>1||
               n.model.uiCorrection<0||n.model.uiCorrection>1)return "invalid NR layer experimental parameter";
            if(!range(n.residual.total,5)||!n.residual.correction.valid())return "NR layer residual/correction parameter out of range";
            for(float v:{n.residual.darken,n.residual.brighten,n.residual.color,n.residual.luminance})
                if(!range(v,2))return "NR layer residual parameter out of range";
            if(!validNrRuntime(n.runtime))
                return "invalid NR layer runtime";
            if(n.enabled&&(n.runtime!=nrRuntime||n.lowLatencyPairing!=lowLatency))
                return "active NR layers must share runtime and NR/SR order";
        }
        if(nr&&nrLayerCount&&!activeNrLayerCount())return "enabled NR stack has no active layer";
        if(frameGenerationBackend<FrameGenerationBackend::Dlss||frameGenerationBackend>FrameGenerationBackend::Vfg)return "invalid frame generation backend";
        if(vfgQuality>2)return "invalid VFG quality";
        // The XeSS provider reports its own generated-frame ceiling at session
        // start; the engine clamps/rejects above it, so validation only guards
        // the absolute API range here.
        if(frameGenerationBackend==FrameGenerationBackend::XeSS&&multiplier>4)return "XeSS frame generation supports up to 4X";
        // The AMD 3.1.x provider delivers one generated frame per present; the
        // probe measured the same count for 2/3/4 requested frames.
        if(fsrFrameGeneration(frameGenerationBackend)&&multiplier>2)return "AMD FSR frame generation supports up to 2X";
        if(videoSrQuality>kVideoSrFsr)return "invalid video SR quality";
        if(!pipeline::validSrTarget(srTarget))return "invalid SR target";
        if(opticalFlowBackend!=OpticalFlowBackend::Nvidia&&opticalFlowBackend!=OpticalFlowBackend::AmdFidelityFx&&opticalFlowBackend!=OpticalFlowBackend::GpuDis)return "invalid optical flow backend";
        if(multiplier<1||multiplier>(frameGenerationBackend==FrameGenerationBackend::Vfg?8u:6u))return "unsupported multiplier";
        // 0 = auto quality; explicit values are capped at 300 Mbps so a typo
        // cannot ask a driver for a nonsense rate.
        if(exportBitrateMbps>300)return "export bitrate out of range";
        if(auto error=color.validate();!error.empty())return error;
        if(additionalColorCount>additionalColors.size())return "too many color instances";
        for(uint32_t i=0;i<additionalColorCount;++i)
            if(auto error=additionalColors[i].validate();!error.empty())return error;
        if(!pipeline::validNrSizePolicy(nrPolicy))return "invalid NR size policy";
        if(flow<FlowQuality::Performance||flow>FlowQuality::Quality||content<ContentRate::Transport||content>ContentRate::Capture60To30)return "invalid flow/content mode";
        return {};
    }
};
}
