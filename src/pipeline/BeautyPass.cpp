#include "veyra/pipeline/BeautyPass.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace veyra::pipeline {
namespace {
constexpr UINT kSrvs = 6, kSet = kSrvs + 1;   // t0..t5, u0
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr UINT kRegionBytes = 64;              // shaders/Beauty.hlsli kRoiBytes
constexpr UINT kFullArgs = 0, kLowArgs = 12;
constexpr uint32_t kRestore = 1, kSmooth = 2, kRestoreSkin = 4, kAllSkin = 8, kShowMask = 16, kOriginal = 32;
uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
}

bool BeautyPass::createStage(ID3D12Device* device, Stage& stage, const char* shader, UINT sets)
{
    std::vector<uint8_t> cs;
    return stage.pass.loadShader(shader, cs) && stage.pass.create(device, cs, sets * kSet, kSrvs, 1, kConstantCount, 0, true);
}

bool BeautyPass::initialize(ID3D12Device* device, const engine::BeautySettings& settings, uint32_t width, uint32_t height,
                            uint32_t sourceWidth, uint32_t sourceHeight)
{
    if (!device || !settings.enabled || !settings.validate().empty() || !width || !height || !sourceWidth || !sourceHeight) return false;
    settings_ = settings;
    width_ = width; height_ = height; sourceWidth_ = sourceWidth; sourceHeight_ = sourceHeight;
    // About 540 statistics rows whatever the node size: 1080p uses 2x2 cells, 4K 4x4.
    scale_ = std::clamp(uint32_t(std::lround(double(height) / 540.0)), 1u, 8u);
    lowWidth_ = (width + scale_ - 1) / scale_;
    lowHeight_ = (height + scale_ - 1) / scale_;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = kRegionBytes; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&region_))))
        return false;
    lowA_ = makeTexture(device, lowWidth_, lowHeight_, kColorFormat, true);
    lowB_ = makeTexture(device, lowWidth_, lowHeight_, kColorFormat, true);
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC sd{};
    sd.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    sd.NumArgumentDescs = 1;
    sd.pArgumentDescs = &argument;
    if (!lowA_ || !lowB_ || FAILED(device->CreateCommandSignature(&sd, nullptr, IID_PPV_ARGS(&signature_)))) return false;
    return createStage(device, roi_, "BeautyRoi.dxil", 1) && createStage(device, stats_, "BeautyStats.dxil", 1) &&
           createStage(device, blur_, "BeautyBlur.dxil", 2) && createStage(device, apply_, "BeautyApply.dxil", 1);
}

void BeautyPass::createViews(ID3D12Device* device, DescriptorStager& stager, ID3D12Resource* input, ID3D12Resource* output,
                             ID3D12Resource* original, ID3D12Resource* faces, ID3D12Resource* shapes)
{
    input_ = input; output_ = output; original_ = original; faces_ = faces; shapes_ = shapes;
    D3D12_SHADER_RESOURCE_VIEW_DESC texture{};
    texture.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    texture.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    texture.Texture2D.MipLevels = 1;
    const auto tex = [&](Stage& stage, UINT set, UINT slot, ID3D12Resource* resource) {
        auto d = texture;
        d.Format = resource ? resource->GetDesc().Format : kColorFormat;   // the original may be RGBA32F
        stager.stageSrv(resource, &d, stage.pass.heap.Get(), set * kSet + slot);
    };
    D3D12_SHADER_RESOURCE_VIEW_DESC structured{};
    structured.Format = DXGI_FORMAT_UNKNOWN;
    structured.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    structured.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    structured.Buffer.NumElements = 9;          // FaceTracker::kFaceEntries
    structured.Buffer.StructureByteStride = 64;
    D3D12_SHADER_RESOURCE_VIEW_DESC raw{};
    raw.Format = DXGI_FORMAT_R32_TYPELESS;
    raw.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    raw.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    raw.Buffer.NumElements = kRegionBytes / 4;
    raw.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    D3D12_SHADER_RESOURCE_VIEW_DESC outlines = structured;
    outlines.Buffer.NumElements = 8 * 128;      // FaceMeshGpu::kMaxFaces x kShapeEntries
    outlines.Buffer.StructureByteStride = 8;
    const auto common = [&](Stage& stage, UINT set) {
        stager.stageSrv(faces_, &structured, stage.pass.heap.Get(), set * kSet + 2);
        stager.stageSrv(region_.Get(), &raw, stage.pass.heap.Get(), set * kSet + 3);
        stager.stageSrv(shapes_, &outlines, stage.pass.heap.Get(), set * kSet + 5);
    };
    const auto uav = [&](Stage& stage, UINT set, ID3D12Resource* resource) {
        makeUav(device, resource, kColorFormat, cpuHandleOf(stage.pass, set * kSet + kSrvs));
    };
    // Region: faces in, region buffer out.
    tex(roi_, 0, 0, nullptr); tex(roi_, 0, 1, nullptr); common(roi_, 0); tex(roi_, 0, 4, nullptr);
    D3D12_UNORDERED_ACCESS_VIEW_DESC regionUav{};
    regionUav.Format = DXGI_FORMAT_R32_TYPELESS;
    regionUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    regionUav.Buffer.NumElements = kRegionBytes / 4;
    regionUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(region_.Get(), nullptr, &regionUav, cpuHandleOf(roi_.pass, kSrvs));
    // Statistics of the restored picture.
    tex(stats_, 0, 0, input); tex(stats_, 0, 1, original); common(stats_, 0); tex(stats_, 0, 4, nullptr); uav(stats_, 0, lowA_.Get());
    // Blur sets: 0 reads A writes B, 1 reads B writes A.
    tex(blur_, 0, 0, lowA_.Get()); tex(blur_, 0, 1, nullptr); common(blur_, 0); tex(blur_, 0, 4, nullptr); uav(blur_, 0, lowB_.Get());
    tex(blur_, 1, 0, lowB_.Get()); tex(blur_, 1, 1, nullptr); common(blur_, 1); tex(blur_, 1, 4, nullptr); uav(blur_, 1, lowA_.Get());
    // Final pass: the coefficients end in A.
    tex(apply_, 0, 0, input); tex(apply_, 0, 1, original); common(apply_, 0); tex(apply_, 0, 4, lowA_.Get()); uav(apply_, 0, output);
}

float BeautyPass::sigmaPixels() const
{
    // The radius is the Gaussian sigma in 1080p pixels, scaled with the node's height.
    return std::max(settings_.radius * float(height_) / 1080.0f, 0.25f);
}

void BeautyPass::constants(uint32_t* c, bool hdrWorking, bool originalValid) const
{
    const auto& s = settings_;
    uint32_t flags = 0;
    if (s.restoreActive()) flags |= kRestore;
    if (s.smoothActive()) flags |= kSmooth;
    if (s.restoreScope == engine::BeautyRestoreScope::Skin) flags |= kRestoreSkin;
    if (s.scope == engine::BeautyScope::AllSkin) flags |= kAllSkin;
    if (s.enabled && s.showMask) flags |= kShowMask;
    if (originalValid && original_) flags |= kOriginal;
    c[0] = width_; c[1] = height_; c[2] = sourceWidth_; c[3] = sourceHeight_;
    c[4] = lowWidth_; c[5] = lowHeight_; c[6] = scale_; c[7] = flags;
    // "1.0 = white": SDR white, or the 203-nit reference white of scRGB HDR working.
    c[8] = bitsOf(hdrWorking ? 80.0f / 203.0f : 1.0f);
    c[9] = bitsOf(s.restoreStrength); c[10] = bitsOf(s.regionExpand); c[11] = bitsOf(s.feather);
    c[12] = bitsOf(s.strength); c[13] = bitsOf(s.smoothAmount); c[14] = bitsOf(s.detail); c[15] = bitsOf(s.texture);
    c[16] = bitsOf(s.brighten);
    c[17] = bitsOf(sigmaPixels() / float(scale_));
    // Guided-filter regulariser on the perceptual luma variance: what counts as an edge
    // to keep. Measured on lit faces (scripts/beauty notes): skin 0.002-0.009, eyes
    // 0.016-0.04 at a 1.5-3% face-height sigma. Detail 0 -> 0.012, 1 -> 0.002.
    c[18] = bitsOf(0.012f - 0.010f * s.detail);
    c[19] = bitsOf(s.minConfidence);
    c[20] = bitsOf(s.hueCenter); c[21] = bitsOf(s.hueRange); c[22] = bitsOf(s.satMin); c[23] = bitsOf(s.satMax);
    c[24] = bitsOf(s.lumaMin); c[25] = bitsOf(s.lumaMax); c[26] = bitsOf(s.softness); c[27] = 0;
    c[28] = 8; c[29] = c[30] = c[31] = 0;
}

bool BeautyPass::dispatch(ID3D12GraphicsCommandList* list, StateTracker& tracker, bool hdrWorking, bool originalValid)
{
    if (!list || !input_ || !output_ || input_ == output_ || !faces_ || !shapes_) return false;
    // The output starts as the input: outside this frame's region nothing changes.
    tracker.transition(list, input_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, output_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(output_, input_);
    const bool restore = settings_.restoreActive() && originalValid && original_;
    const bool smooth = settings_.smoothActive();
    const bool mask = settings_.enabled && settings_.showMask;
    if (!restore && !smooth && !mask) {
        tracker.transition(list, output_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return true;
    }
    uint32_t c[kConstantCount]{};
    constants(c, hdrWorking, originalValid);
    const auto* constants = reinterpret_cast<const float*>(c);
    const auto bind = [&](Stage& stage, UINT set) {
        stage.pass.bind(list, constants, gpuHandleOf(stage.pass, set * kSet).ptr, gpuHandleOf(stage.pass, set * kSet + kSrvs).ptr);
    };
    tracker.transition(list, input_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (original_) tracker.transition(list, original_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, faces_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, shapes_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Region of this frame, as dispatch arguments.
    tracker.transition(list, region_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(roi_, 0);
    list->Dispatch(1, 1, 1);
    tracker.uavBarrier(list, region_.Get());
    tracker.transition(list, region_.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (smooth) {
        tracker.transition(list, lowA_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        bind(stats_, 0);
        list->ExecuteIndirect(signature_.Get(), 1, region_.Get(), kLowArgs, nullptr, 0);
        tracker.uavBarrier(list, lowA_.Get());
        for (uint32_t pass = 0; pass < 4; ++pass) {
            ID3D12Resource* source = (pass & 1) ? lowB_.Get() : lowA_.Get();
            ID3D12Resource* target = (pass & 1) ? lowA_.Get() : lowB_.Get();
            tracker.transition(list, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker.transition(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            c[27] = pass;
            bind(blur_, pass & 1);
            list->ExecuteIndirect(signature_.Get(), 1, region_.Get(), kLowArgs, nullptr, 0);
            tracker.uavBarrier(list, target);
        }
        c[27] = 0;
        tracker.transition(list, lowA_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    tracker.transition(list, output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(apply_, 0);
    list->ExecuteIndirect(signature_.Get(), 1, region_.Get(), kFullArgs, nullptr, 0);
    tracker.uavBarrier(list, output_);
    tracker.transition(list, output_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}

std::string BeautyPass::describe() const
{
    const auto& s = settings_;
    std::string text = std::format("restore={} {:.0f}%{} smoothing={} {:.0f}% radius={:.1f} detail={:.0f}% texture={:.0f}% scope={}",
        s.restore, s.restoreStrength * 100.0f, s.restoreScope == engine::BeautyRestoreScope::Skin ? " skin" : "",
        s.smooth, s.smoothAmount * 100.0f, s.radius, s.detail * 100.0f, s.texture * 100.0f,
        s.scope == engine::BeautyScope::AllSkin ? "all-skin" : "faces");
    return text + std::format(" grid={}x{} cell={}", lowWidth_, lowHeight_, scale_);
}
}
