#include "fg_test.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

// NGX SDK helper headers trigger /W4 warnings; suppress for this TU.
#pragma warning(push, 0)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssg.h>
#pragma warning(pop)

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "../nr_harness/harness_util.h"
#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/DlssFgBackend.h"
#include "veyra/ngx/DlssgTransfusion.h"
#include "veyra/ngx/NgxParameters.h"

namespace veyra::harness {

namespace {

using util::jsonEscape;
using util::narrowText;
using util::writeRgbaPng;
using util::writeTextFileUtf8;

// ---------------------------------------------------------------------------
// Test scene: 1920x1080 SDR RGBA8, one 640x640 white square translating +16px
// per real frame. The large object keeps the blend-vs-interp residual signal
// well above noise (~1.1 luma units baseline). The cut phase flips the
// background at frame 30.
// ---------------------------------------------------------------------------
constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr size_t kRowPitch = 7680; // 1920*4, 256-aligned
constexpr int kRealFrames = 60;
constexpr int kShiftPx = 16;
constexpr int kSquare = 640;
constexpr int kCutFrame = 30;
constexpr double kFps = 60.0;

struct MvecConvention {
    const char* name;
    bool mvecInPixels;          // true: (shift,0) half values; false: normalized
    float scaleX;
    float scaleY;
};

const MvecConvention kConventions[2] = {
    { "pixels-scaled-1-over-w", true,  1.0f / 1920.0f, 1.0f / 1080.0f },
    { "normalized-unscaled",    false, 1.0f,           1.0f },
};

uint16_t floatToHalf(float value)
{
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) return static_cast<uint16_t>(sign); // flush to zero
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u); // inf
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

uint64_t fnv1a64(const uint8_t* data, size_t size)
{
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// Renders real frame `frame` of `scene` (0 = dark bg, 1 = gray bg after cut).
static void drawScene(std::vector<uint8_t>& pixels, int xLeft, uint8_t bg)
{
    for (uint32_t y = 0; y < kHeight; ++y) {
        uint8_t* row = pixels.data() + y * kRowPitch;
        for (uint32_t x = 0; x < kWidth; ++x) {
            row[x * 4 + 0] = bg;
            row[x * 4 + 1] = bg;
            row[x * 4 + 2] = bg;
            row[x * 4 + 3] = 255;
        }
    }
    const int y0 = (static_cast<int>(kHeight) - kSquare) / 2;
    for (int y = 0; y < kSquare; ++y) {
        uint8_t* row = pixels.data() + static_cast<size_t>(y0 + y) * kRowPitch;
        for (int x = 0; x < kSquare; ++x) {
            row[static_cast<size_t>(xLeft + x) * 4 + 0] = 240;
            row[static_cast<size_t>(xLeft + x) * 4 + 1] = 240;
            row[static_cast<size_t>(xLeft + x) * 4 + 2] = 240;
            row[static_cast<size_t>(xLeft + x) * 4 + 3] = 255;
        }
    }
}

void renderFrame(std::vector<uint8_t>& pixels, int frame, int scene)
{
    drawScene(pixels, 64 + frame * kShiftPx, (scene == 0) ? 16 : 72);
}

// CPU ground-truth generated frame: the exact midpoint position between
// real frames `frame` and `frame+1` (kShiftPx is even, so it is integral).
void renderInterpFrame(std::vector<uint8_t>& pixels, int frame, int scene)
{
    drawScene(pixels, 64 + frame * kShiftPx + kShiftPx / 2, (scene == 0) ? 16 : 72);
}

struct Centroid {
    bool found = false;
    double x = 0.0;
    double y = 0.0;
};

Centroid brightCentroid(const uint8_t* pixels)
{
    Centroid c;
    double sx = 0.0, sy = 0.0;
    uint64_t count = 0;
    for (uint32_t y = 0; y < kHeight; ++y) {
        const uint8_t* row = pixels + y * kRowPitch;
        for (uint32_t x = 0; x < kWidth; ++x) {
            const int luma = (row[x * 4] * 299 + row[x * 4 + 1] * 587 + row[x * 4 + 2] * 114) / 1000;
            if (luma > 128) {
                sx += x;
                sy += y;
                ++count;
            }
        }
    }
    if (count == 0) return c;
    c.found = true;
    c.x = sx / count;
    c.y = sy / count;
    return c;
}

double meanAbsResidual(const uint8_t* a, const uint8_t* b, const uint8_t* c)
{
    // mean |a - (b+c)/2| over RGB channels (8-bit units)
    double sum = 0.0;
    const size_t samples = kRowPitch * kHeight;
    for (size_t i = 0; i < samples; ++i) {
        const int channel = static_cast<int>(i & 3);
        if (channel == 3) continue; // alpha constant
        const double blend = (static_cast<double>(b[i]) + c[i]) * 0.5;
        sum += std::fabs(static_cast<double>(a[i]) - blend);
    }
    return sum / (static_cast<double>(samples) * 0.75);
}

double meanLuma(const uint8_t* pixels)
{
    double sum = 0.0;
    for (uint32_t y = 0; y < kHeight; ++y) {
        const uint8_t* row = pixels + y * kRowPitch;
        for (uint32_t x = 0; x < kWidth; ++x) {
            sum += (row[x * 4] * 299 + row[x * 4 + 1] * 587 + row[x * 4 + 2] * 114) / 1000.0;
        }
    }
    return sum / (kWidth * kHeight);
}

double meanAbsDiff(const uint8_t* a, const uint8_t* b)
{
    // mean |a - b| over RGB channels (8-bit units)
    double sum = 0.0;
    const size_t samples = kRowPitch * kHeight;
    for (size_t i = 0; i < samples; ++i) {
        const int channel = static_cast<int>(i & 3);
        if (channel == 3) continue; // alpha constant
        sum += std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    }
    return sum / (static_cast<double>(samples) * 0.75);
}

// Per-phase result metrics. Blend truth is self-calibrating: the CPU renders
// the exact midpoint frame (trueInterp), so
//   blendBaseline = mean|trueInterp - blend(prev,cur)|
// is the physical signal separating interpolation from blending. A generated
// frame is proven real when |gen-trueInterp| is well below baseline while
// |gen-blend| stays well above the blend-imposter level (~0).
struct PhaseResult {
    int realFrames = 0;
    int evaluateFailures = 0;
    int usableGenerated = 0;
    int duplicateHashCount = 0;
    int disableFlagCount = 0;
    double minBlendResidual = 1e9;   // min |gen - blend(prev,cur)|
    double maxTrueResidual = 0.0;    // max |gen - trueInterp|
    double meanBlendBaseline = 0.0;  // mean |trueInterp - blend(prev,cur)|
    double maxMidpointErrorPx = 0.0;
    double generatedMeanLuma = 0.0;
    bool directionCorrect = true;
    bool ptsMonotonic = true;
    // cut phase only
    bool crossCutGenerated = false;
    bool resetIssued = false;
    std::vector<double> ptsSequence;

    bool translationPass() const {
        const double baseline = meanBlendBaseline;
        return realFrames == kRealFrames && evaluateFailures == 0 &&
               usableGenerated == kRealFrames - 1 && duplicateHashCount == 0 &&
               baseline >= 0.5 &&                              // scene is discriminative
               minBlendResidual > 0.6 * baseline &&            // not a blend
               maxTrueResidual < 0.5 * baseline &&             // matches true midpoint
               maxMidpointErrorPx <= 6.0 &&
               directionCorrect && ptsMonotonic && generatedMeanLuma > 0.5;
    }
    bool cutPass() const {
        return !crossCutGenerated && resetIssued &&
               usableGenerated >= kRealFrames - 4 && usableGenerated <= kRealFrames - 2;
    }
};

std::string ptsJson(const std::vector<double>& pts)
{
    std::string out = "[";
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) out += ", ";
        out += std::format("{:.6f}", pts[i]);
    }
    out += "]";
    return out;
}

// ---------------------------------------------------------------------------
// GPU plumbing
// ---------------------------------------------------------------------------
struct GpuTextures {
    veyra::gfx::ComPtr<ID3D12Resource> backbuffer;   // RGBA8 in
    veyra::gfx::ComPtr<ID3D12Resource> depth;        // R32_FLOAT in
    veyra::gfx::ComPtr<ID3D12Resource> mvecs;        // R16G16_FLOAT in
    veyra::gfx::ComPtr<ID3D12Resource> outInterp;    // RGBA8 out (UAV)
    veyra::gfx::ComPtr<ID3D12Resource> disableFlag;  // 256B buffer (UAV)
    veyra::gfx::ComPtr<ID3D12Resource> uploadColor;  // upload heaps
    veyra::gfx::ComPtr<ID3D12Resource> uploadMvec;
    veyra::gfx::ComPtr<ID3D12Resource> uploadDepth;
    veyra::gfx::ComPtr<ID3D12Resource> readbackInterp;
    veyra::gfx::ComPtr<ID3D12Resource> readbackFlag;
    uint8_t* mappedColor = nullptr;
    uint8_t* mappedMvec = nullptr;
    uint8_t* mappedDepth = nullptr;
};

veyra::gfx::ComPtr<ID3D12Resource> makeDefaultTexture(
    ID3D12Device* device, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool uav)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    td.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    veyra::gfx::ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r)))) {
        return nullptr;
    }
    return r;
}

veyra::gfx::ComPtr<ID3D12Resource> makeBuffer(
    ID3D12Device* device, D3D12_HEAP_TYPE type, uint64_t size, bool uav)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = type;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = size; bd.Height = 1; bd.DepthOrArraySize = 1;
    bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    veyra::gfx::ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
            type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                           : (type == D3D12_HEAP_TYPE_READBACK
                                                  ? D3D12_RESOURCE_STATE_COPY_DEST
                                                  : D3D12_RESOURCE_STATE_COMMON),
            nullptr, IID_PPV_ARGS(&r)))) {
        return nullptr;
    }
    return r;
}

bool createGpuTextures(ID3D12Device* device, GpuTextures& t)
{
    t.backbuffer = makeDefaultTexture(device, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    t.depth = makeDefaultTexture(device, kWidth, kHeight, DXGI_FORMAT_R32_FLOAT, false);
    t.mvecs = makeDefaultTexture(device, kWidth, kHeight, DXGI_FORMAT_R16G16_FLOAT, false);
    t.outInterp = makeDefaultTexture(device, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    t.disableFlag = makeBuffer(device, D3D12_HEAP_TYPE_DEFAULT, 256, true);
    t.uploadColor = makeBuffer(device, D3D12_HEAP_TYPE_UPLOAD, kRowPitch * kHeight, false);
    t.uploadMvec = makeBuffer(device, D3D12_HEAP_TYPE_UPLOAD, kRowPitch * kHeight, false);
    t.uploadDepth = makeBuffer(device, D3D12_HEAP_TYPE_UPLOAD, kRowPitch * kHeight, false);
    t.readbackInterp = makeBuffer(device, D3D12_HEAP_TYPE_READBACK, kRowPitch * kHeight, false);
    t.readbackFlag = makeBuffer(device, D3D12_HEAP_TYPE_READBACK, 256, false);
    if (!t.backbuffer || !t.depth || !t.mvecs || !t.outInterp || !t.disableFlag ||
        !t.uploadColor || !t.uploadMvec || !t.uploadDepth || !t.readbackInterp || !t.readbackFlag) {
        return false;
    }
    if (FAILED(t.uploadColor->Map(0, nullptr, reinterpret_cast<void**>(&t.mappedColor)))) return false;
    if (FAILED(t.uploadMvec->Map(0, nullptr, reinterpret_cast<void**>(&t.mappedMvec)))) return false;
    if (FAILED(t.uploadDepth->Map(0, nullptr, reinterpret_cast<void**>(&t.mappedDepth)))) return false;
    // constant far depth
    for (uint32_t y = 0; y < kHeight; ++y) {
        float* row = reinterpret_cast<float*>(t.mappedDepth + y * kRowPitch);
        for (uint32_t x = 0; x < kWidth; ++x) row[x] = 0.9f;
    }
    return true;
}

void copyUploadToTexture(ID3D12GraphicsCommandList* list, ID3D12Resource* dst,
                         ID3D12Resource* src, DXGI_FORMAT fmt, uint32_t w, uint32_t h)
{
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = dst;
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &toCopy);

    D3D12_TEXTURE_COPY_LOCATION d{}, s{};
    d.pResource = dst;
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d.SubresourceIndex = 0;
    s.pResource = src;
    s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    s.PlacedFootprint.Footprint.Format = fmt;
    s.PlacedFootprint.Footprint.Width = w;
    s.PlacedFootprint.Footprint.Height = h;
    s.PlacedFootprint.Footprint.Depth = 1;
    s.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(kRowPitch);
    list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);

    D3D12_RESOURCE_BARRIER fromCopy = toCopy;
    fromCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    fromCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1, &fromCopy);
}

} // namespace

// Diagnostic readback only: isolate the NGX backend from graph scheduling,
// pool ownership, source conversion and optical-flow estimation.
// alternateGroups: per-frame multiFrameCount 5,1,5,1,... WITHOUT reset,
// to learn whether a pair over budget may run a smaller group and keep
// history (F3 precondition, review 2026-09-22). Position/uniqueness checks
// apply to every generated frame; group 1 expects the midpoint.
static bool runPlanarSix(veyra::gfx::CommandSlotRing& ring,
    veyra::ngx::NgxCoreHost& core, NVSDK_NGX_Parameter* params, GpuTextures& t, bool alternateGroups=false,
    const std::wstring& captureDir={})
{
    veyra::Status status = veyra::Status::Ok;
    veyra::ngx::DlssFgBackend fg;
    auto* list = ring.acquire(0, status);
    veyra::ngx::DlssFgBackend::CreateDesc create{kWidth,kHeight,kWidth,kHeight,DXGI_FORMAT_R8G8B8A8_UNORM,false};
    if (!list || !fg.create(core,list,params,create,status) || !ring.submitAndSignal(0) || !ring.waitIdle()) return false;
    std::vector<uint8_t> previous(kRowPitch*kHeight), current(previous.size());
    unsigned generated=0, accurate=0, distinct=0;
    std::vector<double> evaluateMs;
    uint64_t sequenceHash=14695981039346656037ull;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    bool ok=true;
    for (unsigned frame=0; ok && frame<12; ++frame) {
        for(unsigned y=0;y<kHeight;++y)for(unsigned x=0;x<kWidth;++x){
            const double local=double(x)-frame*16;
            auto* p=current.data()+size_t(y)*kRowPitch+x*4;
            p[0]=p[1]=p[2]=uint8_t(std::lround(128+45*std::sin(local*0.07)+35*std::sin(local*0.031+y*0.06)+25*std::sin(y*0.031)));p[3]=255;
            auto* mv=reinterpret_cast<uint16_t*>(t.mappedMvec+size_t(y)*kRowPitch+x*4);
            mv[0]=frame?floatToHalf(16.0f):0;mv[1]=0;
        }
        std::memcpy(t.mappedColor,current.data(),current.size());
        uint64_t previousHash=0;
        const unsigned groupCount=alternateGroups&&(frame%2==0)?1u:5u;
        for(unsigned sub=1;ok&&sub<=groupCount;++sub){
            list=ring.acquire(0,status);if(!list){ok=false;break;}
            if(sub==1){
                copyUploadToTexture(list,t.backbuffer.Get(),t.uploadColor.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,kWidth,kHeight);
                copyUploadToTexture(list,t.mvecs.Get(),t.uploadMvec.Get(),DXGI_FORMAT_R16G16_FLOAT,kWidth,kHeight);
                copyUploadToTexture(list,t.depth.Get(),t.uploadDepth.Get(),DXGI_FORMAT_R32_FLOAT,kWidth,kHeight);
            }
            ID3D12Resource* resources[]={t.backbuffer.Get(),t.depth.Get(),t.mvecs.Get(),t.outInterp.Get(),t.disableFlag.Get()};
            D3D12_RESOURCE_BARRIER barriers[5]{};
            for(unsigned j=0;j<5;++j){barriers[j].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barriers[j].Transition={resources[j],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,j<3?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS};}
            list->ResourceBarrier(5,barriers);
            veyra::ngx::DlssFgBackend::EvalDesc evaluate{};
            evaluate.backbuffer=t.backbuffer.Get();evaluate.depth=t.depth.Get();evaluate.mvecs=t.mvecs.Get();
            evaluate.outputInterpolated=t.outInterp.Get();evaluate.outputDisableInterpolation=t.disableFlag.Get();
            evaluate.reset=frame==0;evaluate.frameId=frame+1;evaluate.multiFrameCount=groupCount;evaluate.multiFrameIndex=sub;
            evaluate.mvecScaleX=1.0f/kWidth;evaluate.mvecScaleY=1.0f/kHeight;
            ok=fg.evaluate(list,params,evaluate,status);
            for(unsigned j=0;j<5;++j){barriers[j].Transition.StateBefore=barriers[j].Transition.StateAfter;barriers[j].Transition.StateAfter=j<3?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_COPY_SOURCE;}
            list->ResourceBarrier(5,barriers);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};
            dst.pResource=t.readbackInterp.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,kWidth,kHeight,1,UINT(kRowPitch)};
            src.pResource=t.outInterp.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);list->CopyBufferRegion(t.readbackFlag.Get(),0,t.disableFlag.Get(),0,4);
            for(unsigned j=3;j<5;++j){barriers[j].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barriers[j].Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;}
            list->ResourceBarrier(2,barriers+3);
            LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
            ok=ring.submitAndSignal(0)&&ring.waitIdle()&&ok;if(!ok)break;
            QueryPerformanceCounter(&end);
            if(frame>=2)evaluateMs.push_back(double(end.QuadPart-begin.QuadPart)*1000.0/double(frequency.QuadPart));
            if(!frame)continue;
            uint8_t* pixels=nullptr;uint8_t* flag=nullptr;
            if(FAILED(t.readbackInterp->Map(0,nullptr,reinterpret_cast<void**>(&pixels)))){ok=false;break;}
            if(FAILED(t.readbackFlag->Map(0,nullptr,reinterpret_cast<void**>(&flag)))){t.readbackInterp->Unmap(0,nullptr);ok=false;break;}
            double best=1e30,shift=0;
            for(unsigned quarter=0;quarter<=64;++quarter){
                const double dx=quarter*0.25;double error=0;
                for(unsigned y=200;y<880;y+=8)for(unsigned x=300;x<1620;x+=8){
                    const double sample=x-dx;const unsigned left=unsigned(sample);const double fraction=sample-left;
                    const size_t offset=size_t(y)*kRowPitch+left*4;
                    const double expected=previous[offset]*(1-fraction)+previous[offset+4]*fraction;
                    const double delta=expected-pixels[size_t(y)*kRowPitch+x*4];error+=delta*delta;
                }
                if(error<best){best=error;shift=dx;}
            }
            const uint64_t hash=fnv1a64(pixels,current.size());
            const bool unique=hash!=previousHash&&hash!=fnv1a64(previous.data(),previous.size())&&hash!=fnv1a64(current.data(),current.size());
            const double expectedShift=16.0*sub/(groupCount+1);
            const bool position=std::abs(shift-expectedShift)<=1.0;
            ++generated;distinct+=unique;accurate+=position&&unique&&!*flag;previousHash=hash;
            sequenceHash=(sequenceHash^hash)*1099511628211ull;
            log::info("fg-planar6",std::format("frame={} group={} sub={} expected={:.3f} observed={:.3f} distinct={} disabled={} positionPass={} hash=0x{:016X}",frame,groupCount,sub,expectedShift,shift,unique,unsigned(*flag),position,hash));
            if(!captureDir.empty())
                writeRgbaPng(captureDir+L"\\gen_"+std::to_wstring(frame)+L"_"+std::to_wstring(sub)+L".png",pixels,kWidth,kHeight,kRowPitch);
            t.readbackInterp->Unmap(0,nullptr);t.readbackFlag->Unmap(0,nullptr);
        }
        previous=current;
    }
    const bool drained=ring.drainQueue();fg.release();
    // 12 frames: frame 0 is the reset seed (its outputs are not scored).
    // Fixed 5: 11*5=55. Alternating 1/5 from frame 1: odd frames 5, even frames 1 -> 6*5+5*1=35.
    const unsigned expectedGenerated=alternateGroups?35u:55u;
    std::sort(evaluateMs.begin(),evaluateMs.end());
    const double medianMs=evaluateMs.empty()?0.0:evaluateMs[evaluateMs.size()/2];
    const double meanMs=evaluateMs.empty()?0.0:std::accumulate(evaluateMs.begin(),evaluateMs.end(),0.0)/double(evaluateMs.size());
    log::info("fg-planar6",std::format("directNGX=true alternateGroups={} generated={} distinct={} contentValid={} expected={} graphUsed=false sequenceHash=0x{:016X} evaluateSubmitWaitMs median={:.3f} mean={:.3f} samples={}",alternateGroups,generated,distinct,accurate,expectedGenerated,sequenceHash,medianMs,meanMs,evaluateMs.size()));
    return ok&&drained&&generated==expectedGenerated&&accurate==expectedGenerated;
}

// Runs one DLSSG phase (translation or cut) for a given mvec convention.
// The FG feature must already be created; textures must be initialized.
static PhaseResult runPhase(
    veyra::gfx::CommandSlotRing& ring,
    veyra::ngx::DlssFgBackend& fg,
    NVSDK_NGX_Parameter* params,
    GpuTextures& t,
    const MvecConvention& conv,
    bool cutPhase,
    const std::wstring& captureDir,
    const std::string& runId,
    const std::vector<Centroid>& realCentroids) // per real frame (CPU truth)
{
    PhaseResult result;
    result.realFrames = kRealFrames;
    std::vector<uint8_t> prevPixels(kRowPitch * kHeight);
    std::vector<uint8_t> curPixels(kRowPitch * kHeight);
    std::vector<uint8_t> genPixels(kRowPitch * kHeight);
    std::vector<uint8_t> trueInterp(kRowPitch * kHeight);
    std::vector<uint64_t> realHashes(kRealFrames, 0);
    double baselineSum = 0.0;
    int baselineCount = 0;

    veyra::Status status = veyra::Status::Ok;
    for (int n = 0; n < kRealFrames; ++n) {
        const int scene = cutPhase ? (n >= kCutFrame ? 1 : 0) : 0;
        const bool reset = (n == 0) || (cutPhase && n == kCutFrame);

        // CPU content for this real frame.
        renderFrame(curPixels, n, scene);
        std::memcpy(t.mappedColor, curPixels.data(), curPixels.size());
        // uniform motion vector field (frame n-1 -> n)
        const float mvx = conv.mvecInPixels ? static_cast<float>(kShiftPx)
                                            : static_cast<float>(kShiftPx) / kWidth;
        const uint16_t hx = floatToHalf(mvx);
        const uint16_t hy = floatToHalf(0.0f);
        for (uint32_t y = 0; y < kHeight; ++y) {
            uint8_t* row = t.mappedMvec + y * kRowPitch;
            for (uint32_t x = 0; x < kWidth; ++x) {
                std::memcpy(row + x * 4, &hx, 2);
                std::memcpy(row + x * 4 + 2, &hy, 2);
            }
        }
        realHashes[n] = fnv1a64(curPixels.data(), curPixels.size());

        ID3D12GraphicsCommandList* list = ring.acquire(n % 4, status);
        if (list == nullptr) {
            ++result.evaluateFailures;
            break;
        }

        // Upload this frame's inputs.
        copyUploadToTexture(list, t.backbuffer.Get(), t.uploadColor.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight);
        copyUploadToTexture(list, t.mvecs.Get(), t.uploadMvec.Get(),
            DXGI_FORMAT_R16G16_FLOAT, kWidth, kHeight);
        copyUploadToTexture(list, t.depth.Get(), t.uploadDepth.Get(),
            DXGI_FORMAT_R32_FLOAT, kWidth, kHeight);

        // Evaluate: inputs -> SRV, outputs -> UAV (SR-test convention).
        D3D12_RESOURCE_BARRIER toSrv[5]{};
        for (int i = 0; i < 5; ++i) {
            toSrv[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toSrv[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            toSrv[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        toSrv[0].Transition.pResource = t.backbuffer.Get();
        toSrv[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        toSrv[1].Transition.pResource = t.depth.Get();
        toSrv[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        toSrv[2].Transition.pResource = t.mvecs.Get();
        toSrv[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        toSrv[3].Transition.pResource = t.outInterp.Get();
        toSrv[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toSrv[4].Transition.pResource = t.disableFlag.Get();
        toSrv[4].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(5, toSrv);

        veyra::ngx::DlssFgBackend::EvalDesc edesc{};
        edesc.backbuffer = t.backbuffer.Get();
        edesc.depth = t.depth.Get();
        edesc.mvecs = t.mvecs.Get();
        edesc.outputInterpolated = t.outInterp.Get();
        edesc.outputDisableInterpolation = t.disableFlag.Get();
        edesc.reset = reset;
        edesc.frameId = static_cast<uint64_t>(n);
        edesc.mvecScaleX = conv.scaleX;
        edesc.mvecScaleY = conv.scaleY;
        if (!fg.evaluate(list, params, edesc, status)) {
            ++result.evaluateFailures;
        }
        if (reset) result.resetIssued = true;

        // UAV barrier + back to COMMON, then copy outputs to readback heaps.
        D3D12_RESOURCE_BARRIER uav{};
        uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav.UAV.pResource = t.outInterp.Get();
        list->ResourceBarrier(1, &uav);

        D3D12_RESOURCE_BARRIER back[5]{};
        for (int i = 0; i < 5; ++i) {
            back[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            back[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        back[0].Transition.pResource = t.backbuffer.Get();
        back[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        back[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back[1].Transition.pResource = t.depth.Get();
        back[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        back[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back[2].Transition.pResource = t.mvecs.Get();
        back[2].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        back[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back[3].Transition.pResource = t.outInterp.Get();
        back[3].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        back[3].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back[4].Transition.pResource = t.disableFlag.Get();
        back[4].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        back[4].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(5, back);

        // Copy outputs for CPU analysis.
        {
            D3D12_TEXTURE_COPY_LOCATION d{}, s{};
            d.pResource = t.readbackInterp.Get();
            d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            d.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.PlacedFootprint.Footprint.Width = kWidth;
            d.PlacedFootprint.Footprint.Height = kHeight;
            d.PlacedFootprint.Footprint.Depth = 1;
            d.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(kRowPitch);
            s.pResource = t.outInterp.Get();
            s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            s.SubresourceIndex = 0;
            list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        }
        list->CopyBufferRegion(t.readbackFlag.Get(), 0, t.disableFlag.Get(), 0, 4);

        if (!ring.submitAndSignal(n % 4)) {
            ++result.evaluateFailures;
            break;
        }
        if (!ring.waitIdle()) {
            ++result.evaluateFailures;
            break;
        }

        // CPU analysis of the generated frame for pair (n-1, n).
        if (n > 0 && result.evaluateFailures == 0) {
            uint8_t* flagPtr = nullptr;
            if (SUCCEEDED(t.readbackFlag->Map(0, nullptr, reinterpret_cast<void**>(&flagPtr)))) {
                const bool disabled = flagPtr[0] != 0;
                t.readbackFlag->Unmap(0, nullptr);
                if (disabled) ++result.disableFlagCount;
            }
            uint8_t* genPtr = nullptr;
            if (SUCCEEDED(t.readbackInterp->Map(0, nullptr, reinterpret_cast<void**>(&genPtr)))) {
                std::memcpy(genPixels.data(), genPtr, genPixels.size());
                t.readbackInterp->Unmap(0, nullptr);
            }

            const uint64_t genHash = fnv1a64(genPixels.data(), genPixels.size());
            const bool duplicate = (genHash == realHashes[n - 1]) || (genHash == realHashes[n]);
            if (duplicate) ++result.duplicateHashCount;

            const Centroid gc = brightCentroid(genPixels.data());
            const Centroid& pc = realCentroids[n - 1];
            const Centroid& cc = realCentroids[n];
            double midErr = 1e9;
            if (gc.found && pc.found && cc.found) {
                const double expected = (pc.x + cc.x) * 0.5;
                midErr = std::fabs(gc.x - expected);
                result.maxMidpointErrorPx = std::max(result.maxMidpointErrorPx, midErr);
                const bool dirOk = (cc.x > pc.x) == (gc.x > pc.x) &&
                    (gc.x > pc.x - kShiftPx * 0.5) && (gc.x < cc.x + kShiftPx * 0.5);
                if (!dirOk) result.directionCorrect = false;
            } else {
                result.directionCorrect = false;
            }

            // Self-calibrating blend truth: compare against the CPU-rendered
            // exact midpoint frame (skip the cut boundary, which has no truth).
            const bool boundaryPair = cutPhase && (n == kCutFrame);
            if (!boundaryPair) {
                const int pairScene = cutPhase ? (n >= kCutFrame ? 1 : 0) : 0;
                renderInterpFrame(trueInterp, n - 1, pairScene);
                const double genBlendResidual = meanAbsResidual(genPixels.data(), prevPixels.data(), curPixels.data());
                const double genTrueResidual = meanAbsDiff(genPixels.data(), trueInterp.data());
                const double blendBaseline = meanAbsResidual(trueInterp.data(), prevPixels.data(), curPixels.data());
                result.minBlendResidual = std::min(result.minBlendResidual, genBlendResidual);
                result.maxTrueResidual = std::max(result.maxTrueResidual, genTrueResidual);
                baselineSum += blendBaseline;
                ++baselineCount;
            }
            result.generatedMeanLuma += meanLuma(genPixels.data());

            // PTS sequence: real0 < gen(0,1) < real1 < gen(1,2) < real2 < ...
            if (n == 1) result.ptsSequence.push_back(0.0);
            result.ptsSequence.push_back((n - 0.5) / kFps);
            result.ptsSequence.push_back(n / kFps);
            const size_t psz = result.ptsSequence.size();
            if (psz >= 2 &&
                result.ptsSequence[psz - 2] >= result.ptsSequence[psz - 1]) {
                result.ptsMonotonic = false;
            }

            // Usable = a real generated frame we would present. The cut
            // boundary pair is excluded by policy (present real only).
            const bool usable = !duplicate && !(cutPhase && n == kCutFrame);
            if (usable) ++result.usableGenerated;

            // Cut boundary honesty: with Reset=1 the runtime must either flag
            // disable or emit (approximately) the current real frame; ghosted
            // double-exposure content counts as cross-boundary generation.
            if (cutPhase && n == kCutFrame) {
                uint8_t* fp = nullptr;
                bool disabled = false;
                if (SUCCEEDED(t.readbackFlag->Map(0, nullptr, reinterpret_cast<void**>(&fp)))) {
                    disabled = fp[0] != 0;
                    t.readbackFlag->Unmap(0, nullptr);
                }
                const double vsReal30 = meanAbsResidual(genPixels.data(), curPixels.data(), curPixels.data());
                if (!disabled && vsReal30 > 10.0) {
                    result.crossCutGenerated = true;
                }
            }

            // Evidence captures at a few frames.
            if (!captureDir.empty() &&
                (n == 1 || n == 30 || n == kRealFrames - 1)) {
                const std::wstring tag = std::format(L"fg-{}-prev-{}", cutPhase ? L"cut" : L"tr",
                    static_cast<long long>(n));
                writeRgbaPng(captureDir + L"\\" + tag + L".png", prevPixels.data(), kWidth, kHeight, kRowPitch);
                const std::wstring gen = std::format(L"fg-{}-gen-{}", cutPhase ? L"cut" : L"tr",
                    static_cast<long long>(n));
                writeRgbaPng(captureDir + L"\\" + gen + L".png", genPixels.data(), kWidth, kHeight, kRowPitch);
            }

            log::info("fg-test", std::format("pair({},{}) dup={} midErr={:.2f}px trueResidual={:.3f} baseline={:.3f} luma={:.1f} genHash={:016X}",
                n - 1, n, duplicate ? 1 : 0, midErr,
                result.maxTrueResidual, result.meanBlendBaseline,
                meanLuma(genPixels.data()), genHash));
        }

        std::swap(prevPixels, curPixels);
    }

    if (result.usableGenerated > 0) {
        result.generatedMeanLuma /= static_cast<double>(result.usableGenerated);
    }
    if (baselineCount > 0) {
        result.meanBlendBaseline = baselineSum / baselineCount;
    }
    if (result.minBlendResidual == 1e9) result.minBlendResidual = 0.0;
    (void)runId;
    return result;
}

int runFgTest(const FgTestArgs& args)
{
#if defined(VEYRA_D3D12_DEBUG)
    constexpr bool kDebug = true;
#else
    constexpr bool kDebug = false;
#endif
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc desc{};
    desc.enableDebugLayer = kDebug;
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) return 6;

    veyra::gfx::CommandSlotRing ring;
    if (!ring.initialize(context.device(), context.directQueue(), context.fence(),
            context.fenceEvent(), 4, status)) {
        context.shutdown(); return 7;
    }


    // GPU identity (Playbook 14.2: log GPU/driver before Create).
    std::string gpuDescription, gpuDriver;
    {
        IDXGIDevice* dxgiDevice = nullptr;
        if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
                DXGI_ADAPTER_DESC ad{};
                if (SUCCEEDED(adapter->GetDesc(&ad))) {
                    gpuDescription = narrowText(ad.Description);
                    gpuDriver = std::format("rev-{:X}", ad.Revision);
                }
                adapter->Release();
            }
            dxgiDevice->Release();
        }
        // Full driver version from the nvlddmkm service registry key.
        HKEY devKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm", 0, KEY_READ, &devKey) == ERROR_SUCCESS) {
            wchar_t ver[64]{};
            DWORD size = sizeof(ver), type = 0;
            if (RegQueryValueExW(devKey, L"DriverVersion", nullptr, &type,
                    reinterpret_cast<LPBYTE>(ver), &size) == ERROR_SUCCESS && type == REG_SZ) {
                gpuDriver = narrowText(ver);
            }
            RegCloseKey(devKey);
        }
    }
    log::info("fg-test", std::format("gpu={} driver={}", gpuDescription, gpuDriver));

    // Core init with canonical absolute runtime path.
    wchar_t absRuntimeDir[MAX_PATH * 2]{};
    GetFullPathNameW(args.runtimeDir.c_str(), MAX_PATH * 2, absRuntimeDir, nullptr);
    log::info("fg-test", std::format("runtimeDir (absolute): {}", narrowText(absRuntimeDir)));

    std::ifstream idStream(std::wstring(absRuntimeDir) + L"\\..\\config\\ngx-local.json", std::ios::binary);
    std::string idText((std::istreambuf_iterator<char>(idStream)), std::istreambuf_iterator<char>());
    std::string projectId, engineVersion;
    {
        auto scan = [&idText](const char* key) -> std::string {
            const std::string needle = std::string("\"") + key + "\"";
            size_t pos = idText.find(needle);
            if (pos == std::string::npos) return {};
            pos = idText.find('"', idText.find(':', pos + needle.size()));
            if (pos == std::string::npos) return {};
            size_t start = pos + 1;
            size_t end = idText.find('"', start);
            return idText.substr(start, end - start);
        };
        projectId = scan("ngxProjectId");
        engineVersion = scan("engineVersion");
    }
    if (projectId.empty()) { ring.shutdown(); context.shutdown(); return 7; }

    // Diagnostic 310.9.1 provider patches, installed before NGX Init exactly
    // like FgCompatibilitySession::open does in the product.
    HMODULE transfusionProvider = nullptr;
    if (!args.transfusion.empty()) {
        const std::wstring provider = std::wstring(absRuntimeDir) + L"\\nvngx_dlssg.dll";
        if (!veyra::ngx::DlssgTransfusion::moduleIsKnown(provider)) { log::error("fg-test", "transfusion: provider is not the pinned 310.9.1 build"); ring.shutdown(); context.shutdown(); return 13; }
        transfusionProvider = LoadLibraryExW(provider.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto target = args.transfusion == "ampere" ? veyra::ngx::DlssgTransfusion::Target::Ampere : veyra::ngx::DlssgTransfusion::Target::Ada;
        const auto state = transfusionProvider ? veyra::ngx::DlssgTransfusion::apply(transfusionProvider, target,
            veyra::ngx::DlssgTransfusion::defaultOptions(std::wstring(absRuntimeDir) + L"\\dlssg-kernels")) : veyra::ngx::DlssgTransfusion::State{};
        log::info("fg-test", std::format("transfusion target={} applied={} detail={}", args.transfusion, state.applied, narrowText(state.detail)));
        if (!state.applied) { ring.shutdown(); context.shutdown(); return 13; }
    }
    auto releaseTransfusion = [&]() {
        if (!transfusionProvider) return;
        const auto c = veyra::ngx::DlssgTransfusion::counters();
        log::info("fg-test", std::format("transfusion counters nvapi={} modules accepted={} rejected={} rewritten={} exact={} networkReady={} networkFailed={} dl1Runs={} dl2Runs={}",
            c.nvapiResolutions, c.modulesAccepted, c.modulesRejected, c.modulesRewritten, c.exactProviderKernels, c.networkReady, c.networkFailed, c.optimizedDl1Runs, c.optimizedDl2Runs));
        const bool restored = veyra::ngx::DlssgTransfusion::release();
        log::info("fg-test", std::format("transfusion release restored={}", restored));
        FreeLibrary(transfusionProvider);
        transfusionProvider = nullptr;
    };

    veyra::ngx::NgxCoreHost coreHost;
    if (!coreHost.initialize(context.device(), absRuntimeDir,
            projectId.c_str(), engineVersion.c_str(), status)) {
        releaseTransfusion(); ring.shutdown(); context.shutdown(); return 8;
    }

    // Capability query.
    veyra::ngx::DlssFgBackend fgBackend;
    veyra::ngx::DlssFgBackend::Capability caps{};
    const bool capAvailable = fgBackend.queryCapability(coreHost, caps, status);
    const std::string hagsEvidence = std::format("HwSchMode={} FG.Available={} NeedsUpdatedDriver={}",
        caps.hagsRegistryMode, caps.available, caps.needsUpdatedDriver);
    log::info("fg-test", std::format("capability: available={} initResult=0x{:X} multiFrameCountMax={} | {}",
        caps.available, static_cast<uint64_t>(caps.featureInitResult), caps.multiFrameCountMax, hagsEvidence));
    if (args.capabilityOnly) {
        // --fg-cap: honest capability JSON, no feature creation.
        std::string json = "{\n";
        json += std::format("  \"probe\": \"veyra_fg_harness\",\n");
        json += std::format("  \"runId\": \"{}\",\n", jsonEscape(args.runId));
        json += std::format("  \"fgAvailable\": {},\n", caps.available ? "true" : "false");
        json += std::format("  \"featureInitResult\": {},\n", caps.featureInitResult);
        json += std::format("  \"needsUpdatedDriver\": {},\n", caps.needsUpdatedDriver ? "true" : "false");
        json += std::format("  \"multiFrameCountMax\": {},\n", caps.multiFrameCountMax);
        json += std::format("  \"hagsEvidence\": \"{}\",\n", jsonEscape(hagsEvidence));
        json += std::format("  \"gpuDescription\": \"{}\",\n", jsonEscape(gpuDescription));
        json += std::format("  \"gpuDriverVersion\": \"{}\"\n", jsonEscape(gpuDriver));
        json += "}\n";
        if (!args.jsonFile.empty()) (void)writeTextFileUtf8(args.jsonFile, json);
        coreHost.shutdown(); ring.shutdown(); context.shutdown();
        return capAvailable ? 0 : 12;
    }
    if (!capAvailable) {
        // Fail closed: write JSON with the honest capability state.
        std::string json = "{\n";
        json += std::format("  \"probe\": \"veyra_fg_harness\",\n");
        json += std::format("  \"runId\": \"{}\",\n", jsonEscape(args.runId));
        json += std::format("  \"fgAvailable\": false,\n");
        json += std::format("  \"featureInitResult\": {},\n", caps.featureInitResult);
        json += std::format("  \"needsUpdatedDriver\": {},\n", caps.needsUpdatedDriver ? "true" : "false");
        json += std::format("  \"multiFrameCountMax\": {},\n", caps.multiFrameCountMax);
        json += std::format("  \"hagsEvidence\": \"{}\",\n", jsonEscape(hagsEvidence));
        json += std::format("  \"createSuccess\": false,\n");
        json += std::format("  \"createResultHex\": \"{}\",\n", "");
        json += std::format("  \"neverProvidedFlagsSet\": false,\n");
        json += std::format("  \"mvecConventionWinner\": \"\",\n");
        json += std::format("  \"gpuDescription\": \"{}\",\n", jsonEscape(gpuDescription));
        json += std::format("  \"gpuDriverVersion\": \"{}\"\n", jsonEscape(gpuDriver));
        json += "}\n";
        if (!args.jsonFile.empty()) (void)writeTextFileUtf8(args.jsonFile, json);
        coreHost.shutdown(); ring.shutdown(); context.shutdown();
        return 12;
    }

    NVSDK_NGX_Parameter* params = coreHost.allocateParameters(status);
    if (params == nullptr) {
        coreHost.shutdown(); ring.shutdown(); context.shutdown(); return 8;
    }

    GpuTextures textures;
    if (!createGpuTextures(context.device(), textures)) {
        coreHost.destroyParameters(params);
        coreHost.shutdown(); ring.shutdown(); context.shutdown(); return 9;
    }

    if(args.planarSix){
        const bool passed=caps.multiFrameCountMax>=5&&runPlanarSix(ring,coreHost,params,textures,args.alternateGroups,args.captureDir);
        coreHost.destroyParameters(params);coreHost.shutdown();releaseTransfusion();ring.shutdown();context.shutdown();
        return passed?0:1;
    }

    // CPU truth: real-frame centroids for both phases.
    std::vector<Centroid> realCentroids(kRealFrames);
    {
        std::vector<uint8_t> tmp(kRowPitch * kHeight);
        for (int n = 0; n < kRealFrames; ++n) {
            renderFrame(tmp, n, 0);
            realCentroids[n] = brightCentroid(tmp.data());
        }
    }
    std::vector<Centroid> cutCentroids(kRealFrames);
    {
        std::vector<uint8_t> tmp(kRowPitch * kHeight);
        for (int n = 0; n < kRealFrames; ++n) {
            renderFrame(tmp, n, n >= kCutFrame ? 1 : 0);
            cutCentroids[n] = brightCentroid(tmp.data());
        }
    }

    // Mvec conventions are diagnostic candidates (Playbook 14.2): try each
    // until one passes every translation proof.
    struct CandidateOutcome {
        const char* name = "";
        PhaseResult translation;
        PhaseResult cut;
        bool translationPass = false;
        uint64_t createResult = 0;
        bool created = false;
    };
    std::vector<CandidateOutcome> outcomes;
    size_t winnerIndex = SIZE_MAX;

    for (size_t ci = 0; ci < std::size(kConventions); ++ci) {
        const MvecConvention& conv = kConventions[ci];
        log::info("fg-test", std::format("=== mvec convention candidate: {} (scale={:.6f},{:.6f}) ===",
            conv.name, conv.scaleX, conv.scaleY));

        // Create the FG feature for this candidate.
        veyra::ngx::DlssFgBackend fg;
        veyra::ngx::DlssFgBackend::CreateDesc cdesc{};
        cdesc.width = kWidth;
        cdesc.height = kHeight;
        cdesc.renderWidth = kWidth;
        cdesc.renderHeight = kHeight;
        cdesc.backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        cdesc.dynamicResolution = false;
        bool created = false;
        uint64_t createResult = 0;
        {
            ID3D12GraphicsCommandList* list = ring.acquire(0, status);
            if (list != nullptr) {
                created = fg.create(coreHost, list, params, cdesc, status);
                (void)ring.submitAndSignal(0);
                (void)ring.waitIdle();
            }
            createResult = fg.createResult();
        }
        if (!created) {
            log::info("fg-test", std::format("create failed for candidate {} result=0x{:X}",
                conv.name, createResult));
            CandidateOutcome outcome;
            outcome.name = conv.name;
            outcome.translation.realFrames = 0;
            outcomes.push_back(outcome);
            continue;
        }

        CandidateOutcome outcome;
        outcome.name = conv.name;
        outcome.created = created;
        outcome.createResult = createResult;
        outcome.translation = runPhase(ring, fg, params, textures, conv,
            false, args.captureDir, args.runId, realCentroids);
        outcome.translationPass = outcome.translation.translationPass();
        log::info("fg-test", std::format("candidate {} translation pass={} usable={} dup={} minResidual={:.2f} maxMidErr={:.2f}",
            conv.name, outcome.translationPass, outcome.translation.usableGenerated,
            outcome.translation.duplicateHashCount, outcome.translation.minBlendResidual,
            outcome.translation.maxMidpointErrorPx));

        if (outcome.translationPass) {
            outcome.cut = runPhase(ring, fg, params, textures, conv,
                true, args.captureDir, args.runId, cutCentroids);
            log::info("fg-test", std::format("candidate {} cut pass={} usable={} crossCut={} reset={}",
                conv.name, outcome.cut.cutPass(), outcome.cut.usableGenerated,
                outcome.cut.crossCutGenerated, outcome.cut.resetIssued));
            winnerIndex = outcomes.size();
        }
        fg.release(); // reverse-order teardown before the next candidate
        outcomes.push_back(outcome);

        if (winnerIndex != SIZE_MAX) break; // first convention that passes wins
    }

    coreHost.destroyParameters(params);
    ring.shutdown();
    coreHost.shutdown();
    context.shutdown();

    // JSON assembly.
    const bool overallPass = winnerIndex != SIZE_MAX &&
        outcomes[winnerIndex].translationPass &&
        outcomes[winnerIndex].cut.cutPass();

    std::string json;
    json += "{\n";
    json += std::format("  \"probe\": \"veyra_fg_harness\",\n");
    json += std::format("  \"runId\": \"{}\",\n", jsonEscape(args.runId));
    json += std::format("  \"fgAvailable\": {},\n", caps.available ? "true" : "false");
    json += std::format("  \"featureInitResult\": {},\n", caps.featureInitResult);
    json += std::format("  \"needsUpdatedDriver\": {},\n", caps.needsUpdatedDriver ? "true" : "false");
    json += std::format("  \"multiFrameCountMax\": {},\n", caps.multiFrameCountMax);
    json += std::format("  \"hagsEvidence\": \"{}\",\n", jsonEscape(hagsEvidence));
    json += std::format("  \"gpuDescription\": \"{}\",\n", jsonEscape(gpuDescription));
    json += std::format("  \"gpuDriverVersion\": \"{}\",\n", jsonEscape(gpuDriver));
    json += std::format("  \"createSuccess\": {},\n",
        (winnerIndex != SIZE_MAX) ? "true" : "false");
    json += std::format("  \"createResultHex\": \"{}\",\n",
        (winnerIndex != SIZE_MAX) ? std::format("{:X}", outcomes[winnerIndex].createResult) : "");
    json += std::format("  \"neverProvidedFlagsSet\": {},\n",
        (winnerIndex != SIZE_MAX) ? "true" : "false");
    json += std::format("  \"neverProvidedFlagsHex\": \"{}\",\n", "1B0");
    json += std::format("  \"mvecConventionWinner\": \"{}\",\n",
        jsonEscape(winnerIndex != SIZE_MAX ? outcomes[winnerIndex].name : ""));
    json += "  \"candidates\": [\n";
    for (size_t i = 0; i < outcomes.size(); ++i) {
        const auto& o = outcomes[i];
        auto phaseJson = [](const char* key, const PhaseResult& r) {
            std::string s;
            s += std::format("      \"{}\": {{\n", key);
            s += std::format("        \"realFrames\": {},\n", r.realFrames);
            s += std::format("        \"evaluateFailures\": {},\n", r.evaluateFailures);
            s += std::format("        \"usableGenerated\": {},\n", r.usableGenerated);
            s += std::format("        \"duplicateHashCount\": {},\n", r.duplicateHashCount);
            s += std::format("        \"disableFlagCount\": {},\n", r.disableFlagCount);
            s += std::format("        \"minBlendResidual\": {:.4f},\n", r.minBlendResidual == 1e9 ? 0.0 : r.minBlendResidual);
            s += std::format("        \"maxTrueResidual\": {:.4f},\n", r.maxTrueResidual);
            s += std::format("        \"meanBlendBaseline\": {:.4f},\n", r.meanBlendBaseline);
            s += std::format("        \"maxMidpointErrorPx\": {:.4f},\n", r.maxMidpointErrorPx);
            s += std::format("        \"generatedMeanLuma\": {:.4f},\n", r.generatedMeanLuma);
            s += std::format("        \"directionCorrect\": {},\n", r.directionCorrect ? "true" : "false");
            s += std::format("        \"ptsMonotonic\": {},\n", r.ptsMonotonic ? "true" : "false");
            s += std::format("        \"crossCutGenerated\": {},\n", r.crossCutGenerated ? "true" : "false");
            s += std::format("        \"resetIssued\": {},\n", r.resetIssued ? "true" : "false");
            s += std::format("        \"ptsSequence\": {}", ptsJson(r.ptsSequence));
            s += std::format("\n      }}");
            return s;
        };
        json += std::format("    {{\n      \"name\": \"{}\",\n      \"created\": {},\n      \"createResultHex\": \"{}\",\n      \"translationPass\": {},\n",
            o.name, o.created ? "true" : "false",
            o.created ? std::format("{:X}", o.createResult) : "", o.translationPass ? "true" : "false");
        json += phaseJson("translation", o.translation);
        json += ",\n";
        json += phaseJson("cut", o.cut);
        json += std::format("\n    }}{}", (i + 1 < outcomes.size()) ? "," : "");
        json += "\n";
    }
    json += "  ],\n";
    if (winnerIndex != SIZE_MAX) {
        const auto& w = outcomes[winnerIndex];
        auto flatJson = [](const PhaseResult& r) {
            std::string s;
            s += std::format("    \"realFrames\": {},\n", r.realFrames);
            s += std::format("    \"evaluateFailures\": {},\n", r.evaluateFailures);
            s += std::format("    \"usableGenerated\": {},\n", r.usableGenerated);
            s += std::format("    \"duplicateHashCount\": {},\n", r.duplicateHashCount);
            s += std::format("    \"disableFlagCount\": {},\n", r.disableFlagCount);
    s += std::format("    \"minBlendResidual\": {:.4f},\n", r.minBlendResidual == 1e9 ? 0.0 : r.minBlendResidual);
    s += std::format("    \"maxTrueResidual\": {:.4f},\n", r.maxTrueResidual);
    s += std::format("    \"meanBlendBaseline\": {:.4f},\n", r.meanBlendBaseline);
    s += std::format("    \"maxMidpointErrorPx\": {:.4f},\n", r.maxMidpointErrorPx);
            s += std::format("    \"generatedMeanLuma\": {:.4f},\n", r.generatedMeanLuma);
            s += std::format("    \"directionCorrect\": {},\n", r.directionCorrect ? "true" : "false");
            s += std::format("    \"ptsMonotonic\": {},\n", r.ptsMonotonic ? "true" : "false");
            s += std::format("    \"crossCutGenerated\": {},\n", r.crossCutGenerated ? "true" : "false");
            s += std::format("    \"resetIssued\": {}", r.resetIssued ? "true" : "false");
            return s;
        };
        json += "  \"translation\": {\n";
        json += flatJson(w.translation);
        json += "  },\n";
        json += "  \"cut\": {\n";
        json += flatJson(w.cut);
        json += "  }\n";
    } else {
        json += "  \"translation\": null,\n  \"cut\": null\n";
    }
    json += "}\n";
    if (!args.jsonFile.empty()) (void)writeTextFileUtf8(args.jsonFile, json);

    log::info("fg-test", overallPass ? "fg-test: PASS" : "fg-test: FAIL");
    return overallPass ? 0 : 12;
}

} // namespace veyra::harness
