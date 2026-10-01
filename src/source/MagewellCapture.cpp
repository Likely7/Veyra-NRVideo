#include "veyra/source/MagewellCapture.h"

#include <windows.h>
#include <avrt.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

#include "veyra/Log.h"

#ifdef VEYRA_HAS_MAGEWELL
// Magewell's own headers (MWCapture SDK 3.3.1.1596, kept outside the source tree under
// deps/magewell). Their licence lets the header files and library be used, modified and
// redistributed with Magewell's copyright notice and disclaimer; see THIRD_PARTY_NOTICES.md.
#include <LibMWCapture/MWCapture.h>
#endif

namespace veyra::source::magewell {

namespace {

std::atomic<bool> g_preference{false};
// What the capture dialog shows (statusText()).
std::mutex g_statusMutex;
std::wstring g_lastError;
std::atomic<bool> g_active{false};
std::atomic<double> g_latencyMs{0};

void setLastError(std::wstring text) {
    std::lock_guard lock(g_statusMutex);
    g_lastError = std::move(text);
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) c = wchar_t(std::towlower(c));
    return out;
}

// "\\?\pci#ven_1cd7&dev_0010&...#4&2b0f...&0&00e0#{guid}\global" -> "pci#ven_1cd7&dev_0010&...#4&2b0f...&0&00e0"
std::wstring instanceOf(std::wstring_view path) {
    std::wstring text = lower(path);
    if (text.starts_with(L"\\\\?\\")) text.erase(0, 4);
    if (const auto guid = text.find(L"#{"); guid != std::wstring::npos) text.erase(guid);
    return text;
}

std::filesystem::path programDirectory() {
    wchar_t buffer[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

#ifdef VEYRA_HAS_MAGEWELL
struct Api {
    HMODULE module = nullptr;
    std::wstring where;
    bool ready = false;
    decltype(&MWCaptureInitInstance) initInstance = nullptr;
    decltype(&MWRefreshDevice) refreshDevice = nullptr;
    decltype(&MWGetChannelCount) channelCount = nullptr;
    decltype(&MWGetChannelInfoByIndex) channelInfo = nullptr;
    decltype(&MWGetDevicePath) devicePath = nullptr;
    decltype(&MWOpenChannelByPath) openChannel = nullptr;
    decltype(&MWCloseChannel) closeChannel = nullptr;
    decltype(&MWPinVideoBuffer) pinBuffer = nullptr;
    decltype(&MWUnpinVideoBuffer) unpinBuffer = nullptr;
    decltype(&MWStartVideoCapture) startCapture = nullptr;
    decltype(&MWStopVideoCapture) stopCapture = nullptr;
    decltype(&MWGetVideoBufferInfo) bufferInfo = nullptr;
    decltype(&MWGetVideoFrameInfo) frameInfo = nullptr;
    decltype(&MWGetVideoSignalStatus) signalStatus = nullptr;
    decltype(&MWRegisterNotify) registerNotify = nullptr;
    decltype(&MWUnregisterNotify) unregisterNotify = nullptr;
    decltype(&MWGetNotifyStatus) notifyStatus = nullptr;
    decltype(&MWGetDeviceTime) deviceTime = nullptr;
    decltype(&MWCaptureVideoFrameToVirtualAddressEx) captureFrame = nullptr;
    decltype(&MWGetVideoCaptureStatus) captureStatus = nullptr;
};

// Loaded once and kept for the life of the process (the SDK instance is never torn down).
Api& api() {
    static Api a = [] {
        Api x;
        const auto bundled = programDirectory() / L"runtime" / L"magewell" / L"LibMWCapture.dll";
        if (std::filesystem::exists(bundled)) {
            x.module = LoadLibraryExW(bundled.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (x.module) x.where = bundled.wstring();
        }
        if (!x.module) {
            // Installed by Magewell's own software (driver / SDK runtime).
            x.module = LoadLibraryExW(L"LibMWCapture.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
            if (x.module) x.where = L"LibMWCapture.dll (system)";
        }
        if (!x.module) { x.where = L"LibMWCapture.dll not found"; return x; }
        bool all = true;
        const auto bind = [&](auto& slot, const char* name) {
            slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(GetProcAddress(x.module, name));
            if (!slot) { all = false; log::error("magewell", std::format("missing export {}", name)); }
        };
        bind(x.initInstance, "MWCaptureInitInstance");
        bind(x.refreshDevice, "MWRefreshDevice");
        bind(x.channelCount, "MWGetChannelCount");
        bind(x.channelInfo, "MWGetChannelInfoByIndex");
        bind(x.devicePath, "MWGetDevicePath");
        bind(x.openChannel, "MWOpenChannelByPath");
        bind(x.closeChannel, "MWCloseChannel");
        bind(x.pinBuffer, "MWPinVideoBuffer");
        bind(x.unpinBuffer, "MWUnpinVideoBuffer");
        bind(x.startCapture, "MWStartVideoCapture");
        bind(x.stopCapture, "MWStopVideoCapture");
        bind(x.bufferInfo, "MWGetVideoBufferInfo");
        bind(x.frameInfo, "MWGetVideoFrameInfo");
        bind(x.signalStatus, "MWGetVideoSignalStatus");
        bind(x.registerNotify, "MWRegisterNotify");
        bind(x.unregisterNotify, "MWUnregisterNotify");
        bind(x.notifyStatus, "MWGetNotifyStatus");
        bind(x.deviceTime, "MWGetDeviceTime");
        bind(x.captureFrame, "MWCaptureVideoFrameToVirtualAddressEx");
        bind(x.captureStatus, "MWGetVideoCaptureStatus");
        if (!all) { x.where += L" (incomplete exports)"; return x; }
        x.ready = x.initInstance() != FALSE;
        if (!x.ready) x.where += L" (MWCaptureInitInstance failed)";
        log::info("magewell", std::format("runtime {} ready={}", narrow(x.where), x.ready));
        return x;
    }();
    return a;
}

DWORD fourccOf(Format format) {
    switch (format) {
    case Format::Nv12: return MWFOURCC_NV12;
    case Format::Yuy2: return MWFOURCC_YUY2;
    case Format::P010: return MWFOURCC_P010;
    case Format::Bgra: return MWFOURCC_BGRA;
    }
    return 0;
}
#endif

} // namespace

bool isProCaptureDevicePath(std::wstring_view directShowPath) {
    return lower(directShowPath).find(L"ven_1cd7") != std::wstring::npos;
}

bool runtimeAvailable(std::wstring* detail) {
#ifdef VEYRA_HAS_MAGEWELL
    auto& a = api();
    if (detail) *detail = a.where;
    return a.ready;
#else
    if (detail) *detail = L"this build has no Magewell SDK support";
    return false;
#endif
}

void setLowLatencyPreference(bool enabled) {
    if (g_preference.exchange(enabled) != enabled)
        log::info("magewell", std::format("low-latency preference={} (takes effect on the next connect)", enabled));
}
bool lowLatencyPreference() { return g_preference.load(); }

std::wstring statusText() {
    if (g_active.load()) {
        const double ms = g_latencyMs.load();
        return ms > 0 ? std::format(L"低延迟采集中 · 一帧从开始进卡到完整进内存 {:.1f} ms", ms) : std::wstring(L"低延迟采集中");
    }
    std::lock_guard lock(g_statusMutex);
    return g_lastError.empty() ? std::wstring() : L"未启用：" + g_lastError;
}

struct LowLatencyVideo::Impl {
    VideoRequest request;
    Deliver deliver;
    std::thread thread;
    HANDLE stopEvent = nullptr;
    std::atomic<bool> running{false};
    mutable std::mutex statsMutex;
    VideoStats stats;
#ifdef VEYRA_HAS_MAGEWELL
    HCHANNEL channel = nullptr;
    void loop();
#endif
};

LowLatencyVideo::LowLatencyVideo() : p_(std::make_unique<Impl>()) {}
LowLatencyVideo::~LowLatencyVideo() { stop(); }
bool LowLatencyVideo::running() const { return p_->running.load(); }
VideoStats LowLatencyVideo::stats() const { std::lock_guard lock(p_->statsMutex); return p_->stats; }

bool LowLatencyVideo::start(const VideoRequest& request, Deliver deliver, std::wstring& error) {
    stop();
    const bool ok = startImpl(request, std::move(deliver), error);
    setLastError(ok ? std::wstring() : error);
    return ok;
}

bool LowLatencyVideo::startImpl(const VideoRequest& request, Deliver deliver, std::wstring& error) {
#ifdef VEYRA_HAS_MAGEWELL
    auto& a = api();
    if (!a.ready) { error = L"没有可用的美乐威运行库（" + a.where + L"）"; return false; }
    if (!request.width || !request.height || !request.stride || !request.frameBytes) { error = L"采集格式无效"; return false; }
    a.refreshDevice();
    const int count = a.channelCount();
    const std::wstring wantedPath = lower(request.directShowPath), wantedInstance = instanceOf(request.directShowPath);
    int exact = -1, byInstance = -1, proCount = 0, onlyPro = -1, instanceMatches = 0;
    for (int i = 0; i < count; ++i) {
        MWCAP_CHANNEL_INFO info{};
        if (a.channelInfo(i, &info) != MW_SUCCEEDED) continue;
        wchar_t path[MAX_PATH * 2]{};
        if (a.devicePath(i, path) != MW_SUCCEEDED) continue;
        const bool pro = info.wFamilyID == MW_FAMILY_ID_PRO_CAPTURE;
        log::info("magewell", std::format("channel {} family={} product={} board={} channel={} path={}", i, info.wFamilyID,
            info.szProductName, int(info.byBoardIndex), int(info.byChannelIndex), narrow(path)));
        if (!pro) continue;
        ++proCount; onlyPro = i;
        if (lower(path) == wantedPath) exact = i;
        if (instanceOf(path) == wantedInstance) { byInstance = i; ++instanceMatches; }
    }
    int chosen = exact >= 0 ? exact : instanceMatches == 1 ? byInstance : -1;
    if (chosen < 0 && proCount == 1) {
        chosen = onlyPro;
        log::warn("magewell", "no channel path matched the DirectShow device; using the only Pro Capture channel");
    }
    log::info("magewell", std::format("directshow path={} chosen channel={} (exact={} instance={}x{} pro={})",
        narrow(request.directShowPath), chosen, exact, byInstance, instanceMatches, proCount));
    if (chosen < 0) { error = L"找不到与所选设备对应的美乐威 Pro Capture 通道"; return false; }
    wchar_t path[MAX_PATH * 2]{};
    a.devicePath(chosen, path);
    p_->channel = a.openChannel(path);
    if (!p_->channel) { error = L"打开美乐威采集通道失败"; return false; }
    p_->request = request;
    p_->deliver = std::move(deliver);
    p_->stats = {};
    p_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p_->running = true;
    g_active = true;
    g_latencyMs = 0;
    p_->thread = std::thread([impl = p_.get()] { impl->loop(); });
    log::info("magewell", std::format("low-latency video started {}x{} fourcc=0x{:08X} stride={} bytes={} partialLines={}",
        request.width, request.height, fourccOf(request.format), request.stride, request.frameBytes, request.partialLines));
    return true;
#else
    (void)request; (void)deliver;
    error = L"此版本未包含美乐威 SDK 支持";
    return false;
#endif
}

void LowLatencyVideo::stop() {
    if (p_->stopEvent) SetEvent(p_->stopEvent);
    if (p_->thread.joinable()) p_->thread.join();
#ifdef VEYRA_HAS_MAGEWELL
    if (p_->channel) { api().closeChannel(p_->channel); p_->channel = nullptr; }
#endif
    if (p_->stopEvent) { CloseHandle(p_->stopEvent); p_->stopEvent = nullptr; }
    g_active = false;
    if (p_->running.exchange(false)) {
        const auto s = stats();
        log::info("magewell", std::format("low-latency video stopped frames={} timeouts={} signalChanges={} frameStartToHostMs={:.2f}",
            s.frames, s.timeouts, s.signalChanges, s.frameStartToHostMs));
    }
}

#ifdef VEYRA_HAS_MAGEWELL
void LowLatencyVideo::Impl::loop() {
    auto& a = api();
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);
    // Page-aligned and pinned: the card writes the frame straight into it by DMA.
    const size_t allocation = (request.frameBytes + 4095) & ~size_t(4095);
    auto* buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, allocation, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    HANDLE captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE notifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HNOTIFY notify = 0;
    bool pinned = false, capturing = false;
    const DWORD fourcc = fourccOf(request.format);
    do {
        if (!buffer || !captureEvent || !notifyEvent) { log::error("magewell", "buffer/event allocation failed"); break; }
        if (a.pinBuffer(channel, buffer, DWORD(request.frameBytes)) != MW_SUCCEEDED) { log::error("magewell", "MWPinVideoBuffer failed"); break; }
        pinned = true;
        if (a.startCapture(channel, captureEvent) != MW_SUCCEEDED) { log::error("magewell", "MWStartVideoCapture failed"); break; }
        capturing = true;
        notify = a.registerNotify(channel, notifyEvent,
            MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING | MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE | MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE);
        if (!notify) { log::error("magewell", "MWRegisterNotify failed"); break; }
        bool discontinuity = true;
        double latencyAverage = 0;
        for (;;) {
            HANDLE waitOn[] = {stopEvent, notifyEvent};
            const DWORD woken = WaitForMultipleObjects(2, waitOn, FALSE, 1000);
            if (woken == WAIT_OBJECT_0) break;
            if (woken == WAIT_TIMEOUT) { std::lock_guard lock(statsMutex); ++stats.timeouts; discontinuity = true; continue; }
            ULONGLONG bits = 0;
            if (a.notifyStatus(channel, notify, &bits) != MW_SUCCEEDED) continue;
            if (bits & (MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE | MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE)) {
                discontinuity = true;
                std::lock_guard lock(statsMutex); ++stats.signalChanges;
            }
            if (!(bits & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING)) continue;
            MWCAP_VIDEO_SIGNAL_STATUS signal{};
            if (a.signalStatus(channel, &signal) != MW_SUCCEEDED || signal.state != MWCAP_VIDEO_SIGNAL_LOCKED) { discontinuity = true; continue; }
            MWCAP_VIDEO_BUFFER_INFO bufferInfo{};
            if (a.bufferInfo(channel, &bufferInfo) != MW_SUCCEEDED) continue;
            MWCAP_VIDEO_FRAME_INFO frameInfo{};
            if (a.frameInfo(channel, bufferInfo.iNewestBuffering, &frameInfo) != MW_SUCCEEDED) continue;
            // Low latency: capture the frame that is still arriving; the DMA starts after
            // `partialLines` lines and the capture event fires per completed part.
            const MW_RESULT started = a.captureFrame(channel, bufferInfo.iNewestBuffering, buffer, DWORD(request.frameBytes), DWORD(request.stride),
                request.bottomUp ? TRUE : FALSE, 0, fourcc, int(request.width), int(request.height), 0, request.partialLines,
                0, nullptr, 0, 100, 0, 100, 0, MWCAP_VIDEO_DEINTERLACE_BLEND, MWCAP_VIDEO_ASPECT_RATIO_IGNORE, nullptr, nullptr, 0, 0,
                MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN, MWCAP_VIDEO_QUANTIZATION_UNKNOWN, MWCAP_VIDEO_SATURATION_UNKNOWN);
            if (started != MW_SUCCEEDED) { discontinuity = true; continue; }
            bool completed = false, stopping = false;
            while (!completed) {
                HANDLE partWait[] = {stopEvent, captureEvent};
                const DWORD part = WaitForMultipleObjects(2, partWait, FALSE, 200);
                if (part == WAIT_OBJECT_0) { stopping = true; break; }
                if (part == WAIT_TIMEOUT) break;
                MWCAP_VIDEO_CAPTURE_STATUS status{};
                if (a.captureStatus(channel, &status) != MW_SUCCEEDED) break;
                completed = status.bFrameCompleted != FALSE;
            }
            if (stopping) break;
            if (!completed) { discontinuity = true; std::lock_guard lock(statsMutex); ++stats.timeouts; continue; }
            LONGLONG now = 0;
            a.deviceTime(channel, &now);
            const LONGLONG frameStart = frameInfo.allFieldStartTimes[0];
            if (frameStart > 0 && now >= frameStart) {
                const double ms = double(now - frameStart) / 10000.0;
                latencyAverage = latencyAverage > 0 ? latencyAverage * 0.97 + ms * 0.03 : ms;
            }
            deliver(buffer, request.frameBytes, double(frameStart) / 1e7, discontinuity);
            discontinuity = false;
            uint64_t frames = 0;
            {
                std::lock_guard lock(statsMutex);
                frames = ++stats.frames;
                stats.frameStartToHostMs = latencyAverage;
            }
            g_latencyMs = latencyAverage;
            if (frames % 600 == 0)
                log::info("magewell", std::format("frames={} frameStartToHostMs={:.2f} (frame started arriving -> whole frame in host memory)", frames, latencyAverage));
        }
    } while (false);
    if (notify) a.unregisterNotify(channel, notify);
    if (capturing) a.stopCapture(channel);
    if (pinned) a.unpinBuffer(channel, buffer);
    if (buffer) VirtualFree(buffer, 0, MEM_RELEASE);
    if (captureEvent) CloseHandle(captureEvent);
    if (notifyEvent) CloseHandle(notifyEvent);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    // An early exit (pin/start failed) hands video back to DirectShow (see CaptureCardSource).
    if (!stats.frames) setLastError(L"美乐威采集启动失败，已回到普通采集（详见日志 magewell）");
    g_active = false;
    running = false;
}
#endif

} // namespace veyra::source::magewell
