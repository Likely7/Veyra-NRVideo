#include "veyra/gfx/XessPacing.h"

#include "veyra/Log.h"
#include "veyra/ThunkHook.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <intrin.h>
#include <mutex>
#include <d3d12.h>

namespace veyra::gfx {
namespace {

// Provider signatures. The present is a member function: `this` in rcx plus six
// arguments, two of which arrive on the stack.
using PresentFn = int64_t (*)(void*, uint32_t, uint32_t, uint64_t, void*, void*, uint64_t);
// The provider's own frame scheduler and its ring snapshot helper.
using SchedFn = bool (*)(void*, void*, uint8_t, void*, uint32_t);
using RingSnapshotFn = void* (*)(void*, void*);
using TimestampFn = void* (*)(void*, int64_t*, void*, void*, uint32_t, uint32_t);
// Same pinned provider locations documented by Magpie 3841698348bfb246.
constexpr uint32_t timestampThunkRva=0x3430,timestampFnRva=0x224B30;

// Read-only samples around our synchronous scheduler call. The provider owns
// the fence for the entire call; never retain it across calls or rebuilds.
struct FenceObservation {
    void* ring=nullptr;
    ID3D12Fence* fence=nullptr;
    uint64_t before=0;
};
thread_local FenceObservation fenceObservation;

struct OutputIntervals {
    // Quarter-millisecond bins, with a separate overflow bin at 250ms.
    std::array<uint64_t,1001> bins{};
    uint64_t samples=0, under1=0, over10=0;
    double sumMs=0, maximumMs=0;
    void add(double ms) {
        if(ms<0)return;
        ++samples;sumMs+=ms;maximumMs=(std::max)(maximumMs,ms);
        under1+=ms<1;over10+=ms>10;
        ++bins[static_cast<size_t>((std::min)(ms*4,1000.0))];
    }
    double percentileUpper(double fraction) const {
        if(!samples)return 0;
        const auto rank=static_cast<uint64_t>(std::ceil(double(samples)*fraction));
        uint64_t cumulative=0;
        for(size_t i=0;i<bins.size();++i){
            cumulative+=bins[i];
            if(cumulative>=rank)return i==1000?maximumMs:double(i+1)*0.25;
        }
        return maximumMs;
    }
};

struct PacingState {
    std::mutex mutex;
    std::mutex statsMutex;
    ThunkHook hook;
    ThunkHook timestampHook;
    TimestampFn timestampNative=nullptr;
    bool installed = false;
    const bool traceEnabled=GetEnvironmentVariableW(L"VEYRA_TEST_TRACE_XESS",nullptr,0)>0;
    uint8_t* base = nullptr;
    PresentFn native = nullptr;
    SchedFn sched = nullptr;
    RingSnapshotFn ringSnapshot = nullptr;
    uint32_t generatedFrames = 0;
    // Counters, written only from the provider's present thread.
    uint64_t scheduled = 0, refused = 0, forwarded = 0, bypassed = 0;
    LARGE_INTEGER frequency{};
    int64_t lastScheduledQpc = 0;
    uint64_t lastScheduledIndex = 0;
    uint64_t lastScheduledCount = 0;
    int64_t gapSum = 0, gapMin = 0, gapMax = 0;
    uint64_t gapSamples = 0;
    // Gaps that cross a burst boundary include the real frame the provider
    // presents itself, so they are counted but not mixed into the in-burst
    // spacing that shows whether the fix worked.
    uint64_t boundaryGaps = 0;
    int64_t statsDeadlineQpc = 0;
    int64_t lastMultiplier = 0;
    int32_t logBudget = 6;
    int64_t lastPresentQpc = 0, presentGapSum = 0, presentGapMax = 0;
    uint64_t presentGapSamples = 0;
    int64_t firstOutputQpc=0;
    OutputIntervals startupOutputs,steadyOutputs;
    std::array<int64_t,15> periods{};
    size_t periodCount=0,periodPosition=0;
    int64_t lastBurstQpc=0,periodQpc=0,intervalQpc=0,targetQpc=0;
    uint64_t fallbackFrames=0;
};

PacingState& state() {
    static PacingState instance;
    return instance;
}

double msFromQpc(const PacingState& s, int64_t qpc) {
    return s.frequency.QuadPart > 0 ? (double(qpc) * 1000.0) / double(s.frequency.QuadPart) : 0.0;
}

void logOutputIntervals(const char* phase,const OutputIntervals& stats) {
    log::info("xess-output-total",std::format(
        "phase={} samples={} meanMs={:.3f} p50UpperMs={:.3f} p95UpperMs={:.3f} p99UpperMs={:.3f} maxMs={:.3f} under1={} over10={} (cumulative per hook lifetime; 0.25ms bins; SDK returns, not scanout)",
        phase,stats.samples,stats.samples?stats.sumMs/double(stats.samples):0,
        stats.percentileUpper(0.50),stats.percentileUpper(0.95),stats.percentileUpper(0.99),
        stats.maximumMs,stats.under1,stats.over10));
}

std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string text(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), text.data(), length, nullptr, nullptr);
    return text;
}

void* traceDeadline(void* ctx,int64_t* out,void* lookup,void* timing,uint32_t index,uint32_t count) {
    auto& s=state();
    void* result=s.timestampNative(ctx,out,lookup,timing,index,count);
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    diagnostics::FrameTraceEvent event;event.kind=diagnostics::TraceKind::ProviderDeadline;
    event.host100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;
    event.detail=index;event.count=count;
    if(lookup&&fenceObservation.fence&&fenceObservation.ring==ctx&&index==1){
        event.providerFenceSampled=true;
        event.providerFenceBefore=fenceObservation.before;
        event.providerFenceAtDeadline=fenceObservation.fence->GetCompletedValue();
        // Scheduler lookup local+0x18 supplies the fence value at 0x21eeeb.
        std::memcpy(&event.providerFenceTarget,static_cast<uint8_t*>(lookup)+0x18,sizeof(uint64_t));
    }
    // Audited native scheduler uses QPC converted to nanoseconds, not host100ns.
    const auto frequency=s.frequency.QuadPart;
    if(out&&frequency>0){
        const auto ns=(now.QuadPart/frequency)*1000000000LL+(now.QuadPart%frequency)*1000000000LL/frequency;
        event.milliseconds=double(*out-ns)/1000000.0;
    }
    Logger::instance().recordFrame(event);
    return result;
}

// The provider's scheduler is only live when its limiter is off; calling it
// while the limiter owns the pacing is a silent no-op, which would look like a
// successful fix while changing nothing.
bool schedulerUsable(void* ctx) {
    const auto* bytes = static_cast<const uint8_t*>(ctx);
    return bytes[XessPacing::kSchedLimiterOffset] == 0 && bytes[XessPacing::kSchedEnableOffset] != 0;
}

// Adapted from OptiScaler 70676c5f XeFGPacing.h NoteFrame/PaceFrame/WaitUntil.
// The provider present thread owns this bounded median and deadline state.
void noteBurst(uint64_t index,uint64_t count,int64_t now) {
    if(index>1)return;
    auto& s=state();
    if(s.lastBurstQpc&&now>s.lastBurstQpc){
        s.periods[s.periodPosition]=(now-s.lastBurstQpc);
        s.periodPosition=(s.periodPosition+1)%s.periods.size();
        s.periodCount=(std::min)(s.periodCount+1,s.periods.size());
        auto sorted=s.periods;std::sort(sorted.begin(),sorted.begin()+s.periodCount);
        s.periodQpc=sorted[s.periodCount/2];
    }
    s.lastBurstQpc=now;
    s.intervalQpc=s.periodQpc/int64_t(count+1);
}

void paceFallback(void* ctx,uint8_t* burst,uint64_t index,uint64_t count,bool isLast) {
    // Match the provider's tail-frame limiter condition; never wait twice.
    if(isLast&&count>2&&reinterpret_cast<uint8_t*>(ctx)[XessPacing::kSchedLimiterOffset]!=0&&
       *reinterpret_cast<uint32_t*>(burst+0x28)==0)return;
    auto& s=state();LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    if(s.intervalQpc<=0)return;
    if(index<=1)s.targetQpc=now.QuadPart+s.intervalQpc;
    else s.targetQpc+=s.intervalQpc;
    s.targetQpc=(std::min)(s.targetQpc,now.QuadPart+s.periodQpc);
    const auto yieldBelow=s.frequency.QuadPart/5000;
    while(now.QuadPart<s.targetQpc){
        if(s.targetQpc-now.QuadPart>yieldBelow)Sleep(0);else YieldProcessor();
        QueryPerformanceCounter(&now);
    }
    std::lock_guard statsLock(s.statsMutex);++s.fallbackFrames;
}

// One schedule call per generated frame, mirroring the provider's own order:
// snapshot the ring first, then schedule. Every argument is in hand because we
// sit inside the present call the provider itself made.
void scheduleFrame(void* ctx, uint8_t* burst, uint64_t index) {
    auto& s = state();
    if (s.sched == nullptr || s.ringSnapshot == nullptr) return;
    alignas(16) uint8_t timing[0x20]{};
    s.ringSnapshot(reinterpret_cast<uint8_t*>(ctx) + XessPacing::kRingOffset, timing);
    const uint8_t gate = burst[XessPacing::kBurstGateOffset] & 1;
    const uint64_t count = *reinterpret_cast<uint64_t*>(burst + 8);

    LARGE_INTEGER before{}, after{};
    if(s.traceEnabled&&gate&&index==1){
        ID3D12Fence* fence=nullptr;
        // Audited 0x21eecf reads this pointer before GetCompletedValue.
        std::memcpy(&fence,static_cast<uint8_t*>(ctx)+0x328,sizeof(fence));
        if(fence)fenceObservation={static_cast<uint8_t*>(ctx)+XessPacing::kRingOffset,fence,fence->GetCompletedValue()};
    }
    QueryPerformanceCounter(&before);
    const bool ok = s.sched(ctx, burst, gate, timing, static_cast<uint32_t>(index));
    QueryPerformanceCounter(&after);
    fenceObservation={};

    std::lock_guard statsLock(s.statsMutex);
    ++s.scheduled;
    if (!ok) ++s.refused;

    const bool sameBurst = s.lastScheduledQpc != 0 && count == s.lastScheduledCount && index == s.lastScheduledIndex + 1;
    if (s.lastScheduledQpc != 0 && !sameBurst) ++s.boundaryGaps;
    if (sameBurst) {
        const int64_t gap = after.QuadPart - s.lastScheduledQpc;
        s.gapSum += gap;
        s.gapMin = s.gapSamples == 0 ? gap : (std::min)(s.gapMin, gap);
        s.gapMax = (std::max)(s.gapMax, gap);
        ++s.gapSamples;
    }
    s.lastScheduledQpc = after.QuadPart;
    s.lastScheduledIndex = index;
    s.lastScheduledCount = count;
    s.lastMultiplier = static_cast<int64_t>(count) + 1;

    // Bounded logging: the schedule must be visible going out, but this runs on
    // the present thread.
    if (s.logBudget > 0 && index == 1) {
        --s.logBudget;
        log::info("xess-pacing", std::format("frame {}/{} -> {} in {:.3f} ms (ctx+0x340={} ctx+0x341={} ring samples={} median={})",
                                             index, count + 1, ok ? "scheduled" : "REFUSED",
                                             msFromQpc(s, after.QuadPart - before.QuadPart),
                                             reinterpret_cast<uint8_t*>(ctx)[XessPacing::kSchedLimiterOffset],
                                             reinterpret_cast<uint8_t*>(ctx)[XessPacing::kSchedEnableOffset],
                                             *reinterpret_cast<uint64_t*>(timing),
                                             *reinterpret_cast<uint64_t*>(timing + 8)));
    }
    if (s.statsDeadlineQpc != 0 && after.QuadPart >= s.statsDeadlineQpc && s.gapSamples > 0) {
        // The measured gap is the only direct evidence that frames really come
        // out spread; report it instead of claiming the intent.
        log::info("xess-pacing", std::format("multiplier={}X scheduled={} refused={} bypassed={} forwarded={} inBurstGaps={} mean={:.3f}ms min={:.3f}ms max={:.3f}ms burstBoundaries={}",
                                             s.lastMultiplier, s.scheduled, s.refused, s.bypassed, s.forwarded,
                                             s.gapSamples, msFromQpc(s, s.gapSum / int64_t(s.gapSamples)),
                                             msFromQpc(s, s.gapMin), msFromQpc(s, s.gapMax), s.boundaryGaps));
        s.gapSum = 0; s.gapSamples = 0; s.gapMin = 0; s.gapMax = 0;
        s.statsDeadlineQpc = after.QuadPart + (s.frequency.QuadPart * 2);
    }
}

// Only the two call sites that present *generated* frames are paced; the other
// presenters of this thunk are forwarded byte-for-byte untouched.
void tryPace(void* ctx, void* arg5, void* arg6, uint64_t arg7, bool isLast) {
    if (arg5 == nullptr) return;
    const uint8_t flag = static_cast<uint8_t>(arg7);
    if ((flag == 1) == isLast) return;
    auto* burst = reinterpret_cast<uint8_t*>(arg5) - 0x38;
    const uint64_t count = *reinterpret_cast<uint64_t*>(burst + 8);
    if (count < 2 || count > 5) return;
    uint64_t index = count;
    if (!isLast) {
        auto* frames = *reinterpret_cast<uint8_t**>(burst);
        if (frames == nullptr || reinterpret_cast<uint8_t*>(arg6) < frames) return;
        const uint64_t offset = reinterpret_cast<uint8_t*>(arg6) - frames;
        if ((offset % 8) != 0) return;
        index = offset / 8;
        if (index < 1 || index >= count) return;
    }
    auto& s = state();
    LARGE_INTEGER burstNow{};QueryPerformanceCounter(&burstNow);noteBurst(index,count,burstNow.QuadPart);
    if (s.sched != nullptr && schedulerUsable(ctx)) {
        // The provider keeps the last frame of the burst; scheduling it twice
        // would put two timestamps on one frame.
        if (!isLast) scheduleFrame(ctx, burst, index);
        return;
    }
    paceFallback(ctx,burst,index,count,isLast);
    std::lock_guard statsLock(s.statsMutex);
    ++s.bypassed;
}

int64_t detour(void* ctx, uint32_t a2, uint32_t a3, uint64_t a4, void* arg5, void* arg6, uint64_t arg7) {
    auto& s = state();
    const auto host=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;};
    // Observe the existing output hook without enabling the test-only
    // deadline hook or changing the provider's pacing.
    const bool traceOutput=s.traceEnabled||diagnostics::privateFrameTraceActive();
    const auto scheduleBegin=traceOutput?host():0;
    auto* caller = static_cast<uint8_t*>(_ReturnAddress());
    if (s.installed) {
        if (caller == s.base + XessPacing::kPacedCallerRva) tryPace(ctx, arg5, arg6, arg7, false);
        else if (caller == s.base + XessPacing::kLastFrameCallerRva) tryPace(ctx, arg5, arg6, arg7, true);
        else {std::lock_guard statsLock(s.statsMutex);++s.forwarded;}
    }
    const auto nativeBegin=traceOutput?host():0;
    const auto result=s.native(ctx, a2, a3, a4, arg5, arg6, arg7);
    if(traceOutput){
        diagnostics::FrameTraceEvent event;event.kind=diagnostics::TraceKind::ProviderOutput;
        event.host100ns=event.presentEndHost=host();event.presentBeginHost=nativeBegin;
        event.milliseconds=double(event.presentEndHost-nativeBegin)/10000;
        event.providerScheduleBeginHost=scheduleBegin;
        const auto address=reinterpret_cast<uintptr_t>(caller),base=reinterpret_cast<uintptr_t>(s.base);
        if(address>=base&&address-base<XessPacing::kKnownSizeOfImage)event.providerCallerRva=uint32_t(address-base);
        if(s.traceEnabled)Logger::instance().recordFrame(event);
        else diagnostics::recordPrivateFrame(event);
    }
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    std::lock_guard statsLock(s.statsMutex);
    if(s.lastPresentQpc!=0){
        const auto gap=now.QuadPart-s.lastPresentQpc;
        if(s.traceEnabled){
            auto& totals=now.QuadPart-s.firstOutputQpc<s.frequency.QuadPart*5?s.startupOutputs:s.steadyOutputs;
            totals.add(msFromQpc(s,gap));
        }
        s.presentGapSum+=gap;s.presentGapMax=(std::max)(s.presentGapMax,gap);
        if(++s.presentGapSamples==240){
            log::info("xess-present-gaps",std::format("samples={} meanMs={:.3f} maxMs={:.3f} (all hooked present returns, includes burst boundaries; not scanout)",s.presentGapSamples,msFromQpc(s,s.presentGapSum/int64_t(s.presentGapSamples)),msFromQpc(s,s.presentGapMax)));
            s.presentGapSamples=0;s.presentGapSum=s.presentGapMax=0;
        }
    }
    if(!s.firstOutputQpc)s.firstOutputQpc=now.QuadPart;
    s.lastPresentQpc=now.QuadPart;
    return result;
}

// A thunk is `E9 rel32` followed by 0xCC padding; the rel32 must reach
// `expectedTarget`. Anything else means this is not the build the RVAs were
// derived from, and the hook refuses.
bool thunkTargets(const uint8_t* base, uint32_t rva, uint32_t expectedTargetRva) {
    const uint8_t* thunk = base + rva;
    if (thunk[0] != 0xE9) return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, thunk + 1, sizeof(displacement));
    const auto target = reinterpret_cast<uintptr_t>(thunk) + 5 + displacement;
    return target == reinterpret_cast<uintptr_t>(base + expectedTargetRva);
}

} // namespace

XessPacing::State XessPacing::install(HMODULE provider, uint32_t generatedFrames) {
    auto& s = state();
    std::unique_lock lock(s.mutex);
    State result{};
    result.generatedFrames = generatedFrames;
    if (provider == nullptr) {
        result.detail = L"no provider module";
        return result;
    }
    result.providerLoaded = true;
    if (generatedFrames < 2) {
        result.detail = L"2X: pacing is not needed (the provider presents one generated frame per real frame)";
        return result;
    }
    if (s.installed) {
        lock.unlock();
        result = snapshot();
        return result;
    }
    auto* base = reinterpret_cast<uint8_t*>(provider);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        result.detail = L"provider is not a PE image";
        return result;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage != kKnownSizeOfImage ||
        nt->FileHeader.TimeDateStamp != kKnownTimeDateStamp) {
        result.detail = L"provider build does not match the audited identity";
        return result;
    }
    // All three thunks must point where the port expects, in this exact build.
    const bool presentOk = thunkTargets(base, kPresentThunkRva, kNativePresentRva);
    const bool schedOk = thunkTargets(base, kSchedThunkRva, kSchedFnRva);
    const bool ringOk = thunkTargets(base, kRingSnapshotThunkRva, kRingSnapshotFnRva);
    if (!presentOk || !schedOk || !ringOk) {
        result.detail = std::format(L"thunk verification failed (present={} scheduler={} ring={})",
                                    presentOk ? 1 : 0, schedOk ? 1 : 0, ringOk ? 1 : 0);
        return result;
    }
    result.structureVerified = true;
    QueryPerformanceFrequency(&s.frequency);
    s.base = base;
    s.sched = reinterpret_cast<SchedFn>(base + kSchedFnRva);
    s.ringSnapshot = reinterpret_cast<RingSnapshotFn>(base + kRingSnapshotFnRva);
    s.generatedFrames = generatedFrames;
    s.scheduled = s.refused = s.forwarded = s.bypassed = 0;
    s.lastScheduledQpc = 0;
    s.lastPresentQpc=s.presentGapSum=s.presentGapMax=0;s.presentGapSamples=0;
    s.firstOutputQpc=0;s.startupOutputs={};s.steadyOutputs={};
    s.periods={};s.periodCount=s.periodPosition=0;
    s.lastBurstQpc=s.periodQpc=s.intervalQpc=s.targetQpc=0;s.fallbackFrames=0;
    s.gapSum = s.gapSamples = 0;
    s.gapMin = s.gapMax = 0;
    s.lastMultiplier = 0;
    s.logBudget = 6;
    s.statsDeadlineQpc = s.frequency.QuadPart > 0 ? 0 : 0;
    auto status = s.hook.install(base + kPresentThunkRva, reinterpret_cast<void*>(&detour), 11);
    if (!status.installed) {
        s.sched = nullptr;
        s.ringSnapshot = nullptr;
        result.detail = std::format(L"thunk hook refused: {}", std::wstring(status.error.begin(), status.error.end()));
        return result;
    }
    s.native = reinterpret_cast<PresentFn>(status.trampoline != nullptr ? status.trampoline : base + kNativePresentRva);
    s.installed = true;
    if(s.traceEnabled&&thunkTargets(base,timestampThunkRva,timestampFnRva)){
        s.timestampNative=reinterpret_cast<TimestampFn>(base+timestampFnRva);
        const auto traceStatus=s.timestampHook.install(base+timestampThunkRva,reinterpret_cast<void*>(&traceDeadline),11);
        log::info("xess-pacing",std::format("read-only deadline trace installed={}",traceStatus.installed));
    }
    // Arm the periodic stats window only once frame times are being measured.
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    s.statsDeadlineQpc = now.QuadPart + (s.frequency.QuadPart * 2);
    result.installed = true;
    result.detail = std::format(L"present thunk {} hooked, scheduler at +0x{:X} (original target +0x{:X})",
                                kPresentThunkRva, kSchedFnRva, kNativePresentRva);
    log::info("xess-pacing", std::format("installed for {}X: present thunk hooked, scheduler wired, {}",
                                         generatedFrames + 1, narrow(result.detail)));
    return result;
}

void XessPacing::release() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.installed) return;
    s.installed = false;
    (void)s.hook.remove();
    (void)s.timestampHook.remove();
    s.timestampNative=nullptr;
    s.native = nullptr;
    s.sched = nullptr;
    s.ringSnapshot = nullptr;
    if(s.traceEnabled){
        std::lock_guard statsLock(s.statsMutex);
        logOutputIntervals("startup-first5s",s.startupOutputs);
        logOutputIntervals("steady-after5s",s.steadyOutputs);
    }
    log::info("xess-pacing", std::format("removed (scheduled={} refused={} bypassed={} forwarded={} fallbackFrames={})",
                                         s.scheduled, s.refused, s.bypassed, s.forwarded,s.fallbackFrames));
}

XessPacing::State XessPacing::snapshot() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    std::lock_guard statsLock(s.statsMutex);
    State result{};
    result.providerLoaded = s.base != nullptr;
    result.structureVerified = s.sched != nullptr;
    result.installed = s.installed;
    result.generatedFrames = s.generatedFrames;
    result.scheduled = s.scheduled;
    result.refused = s.refused;
    result.forwarded = s.forwarded;
    result.bypassed = s.bypassed;
    result.gapSamples = s.gapSamples;
    if (s.gapSamples > 0) {
        result.meanGapMs = msFromQpc(s, s.gapSum / int64_t(s.gapSamples));
        result.minGapMs = msFromQpc(s, s.gapMin);
        result.maxGapMs = msFromQpc(s, s.gapMax);
    }
    return result;
}

bool XessPacing::installed() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return s.installed;
}

bool XessPacing::providerIsAudited(const std::wstring& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size != kKnownProviderSize) return false;
    return true;
}

} // namespace veyra::gfx
