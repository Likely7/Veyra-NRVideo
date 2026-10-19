// #48 network playback / server buffering checks.
//
//   veyra_network_buffer_tests selftest
//   veyra_network_buffer_tests source <path-or-url> <outdir> [key=value ...]
//   veyra_network_buffer_tests engine <path-or-url> <outdir> [key=value ...]
//   veyra_network_buffer_tests scan <file> <outdir> [key=value ...]   (subtitle scan of a server file)
//
// `source` drives MediaFileSource directly (blocking reads unless nonblocking=1) and writes a per-frame
// CSV (pts, luma hash) for A/B comparisons; `engine` plays through EngineController on a hidden window
// and samples the snapshot, including the network statistics, every 50 ms. Both finish with one
// "RESULT {json}" line the orchestrator (tests/network/run_network_tests.py) parses.
#include "veyra/source/MediaFileSource.h"
#include "veyra/engine/EngineController.h"
#include "veyra/engine/Subtitles.h"
#include "veyra/media/NetworkInput.h"
#include "veyra/media/NetworkSettings.h"
#include "veyra/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavutil/log.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

using namespace veyra;
using Clock = std::chrono::steady_clock;

namespace {

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += char(c);
    }
    return out;
}

using Options = std::map<std::string, std::string>;
Options parseOptions(int argc, wchar_t** argv, int first) {
    Options o;
    for (int i = first; i < argc; ++i) {
        const std::string a = narrow(argv[i]);
        const auto eq = a.find('=');
        o[eq == std::string::npos ? a : a.substr(0, eq)] = eq == std::string::npos ? "1" : a.substr(eq + 1);
    }
    return o;
}
double num(const Options& o, const char* key, double fallback) {
    const auto it = o.find(key);
    return it == o.end() ? fallback : std::atof(it->second.c_str());
}
std::string str(const Options& o, const char* key, const char* fallback = "") {
    const auto it = o.find(key);
    return it == o.end() ? fallback : it->second;
}

media::NetworkSettings networkSettings(const Options& o) {
    media::NetworkSettings s;
    s.level = media::networkBufferLevelFromId(str(o, "level", "auto"));
    s.startupPrebufferSeconds = int(num(o, "prebuffer", -1));
    s.autoReconnect = num(o, "reconnect", 1) != 0;
    s.corrupt = media::corruptPolicyFromId(str(o, "policy", "conceal"));
    return s;
}

const char* stateName(media::NetworkStats::State s) {
    switch (s) {
    case media::NetworkStats::State::Prebuffering: return "prebuffering";
    case media::NetworkStats::State::Playing: return "playing";
    case media::NetworkStats::State::Buffering: return "buffering";
    case media::NetworkStats::State::Reconnecting: return "reconnecting";
    case media::NetworkStats::State::Failed: return "failed";
    case media::NetworkStats::State::Ended: return "ended";
    default: return "idle";
    }
}

// ---------------------------------------------------------------------------------------------
int selfTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { failures += !ok; std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl; };
    check(media::networkMediaInput(L"http://example.com/a.mp4") && media::networkMediaInput(L"RTSP://cam/x") && media::networkMediaInput(L"https://x/y.m3u8"), "network schemes recognised");
    check(!media::networkMediaInput(L"C:\\video\\a.mkv") && !media::networkMediaInput(L"\\\\nas\\share\\a.mkv") && !media::networkMediaInput(L"file:///c:/a.mp4"), "paths are not network addresses");
    check(media::remoteMediaPath(L"\\\\nas\\share\\movies\\a.mkv") && media::remoteMediaPath(L"\\\\?\\UNC\\nas\\share\\a.mkv"), "UNC paths are server files");
    check(!media::remoteMediaPath(L"\\\\?\\C:\\video\\a.mkv") && !media::remoteMediaPath(L"\\\\.\\C:\\a.mkv") && !media::remoteMediaPath(L"relative\\a.mkv") && !media::remoteMediaPath(L"C:\\Windows\\a.mkv"), "local paths are not server files");
    media::NetworkSettings s;
    const auto vodAuto = media::networkBufferPlan(s, false);
    check(vodAuto.aheadSeconds == 30 && vodAuto.aheadBytes == (256ull << 20) && vodAuto.startSeconds == 3 && vodAuto.holds, "auto VOD plan: 30 s / 256 MB, 3 s start");
    s.level = media::NetworkBufferLevel::Large;
    const auto vodLarge = media::networkBufferPlan(s, false);
    check(vodLarge.aheadSeconds == 120 && vodLarge.aheadBytes == (1024ull << 20), "large VOD plan: 120 s / 1024 MB");
    s.level = media::NetworkBufferLevel::Small;
    const auto vodSmall = media::networkBufferPlan(s, false);
    check(vodSmall.aheadSeconds == 8 && vodSmall.aheadBytes == (64ull << 20), "small VOD plan: 8 s / 64 MB");
    s.level = media::NetworkBufferLevel::Off;
    const auto off = media::networkBufferPlan(s, false);
    check(!off.holds && off.startSeconds == 0 && off.lowWaterSeconds == 0, "off: never pauses for buffering");
    s = {};
    s.startupPrebufferSeconds = 20;
    const auto custom = media::networkBufferPlan(s, false);
    check(custom.startSeconds == 20 && custom.aheadSeconds >= 32, "custom pre-buffer enlarges the window");
    s = {};
    const auto live = media::networkBufferPlan(s, true);
    check(live.aheadSeconds <= 6 && live.startSeconds <= 3, "live buffers are jitter-sized");
    check(media::networkBufferLevelFromId("medium") == media::NetworkBufferLevel::Medium && media::networkBufferLevelFromId("nonsense") == media::NetworkBufferLevel::Auto, "level ids");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
uint64_t lumaHash(const AVFrame* frame) {
    uint64_t h = 1469598103934665603ull;
    if (!frame || !frame->data[0] || frame->format != AV_PIX_FMT_YUV420P) return h;
    for (int y = 0; y < frame->height; ++y) {
        const uint8_t* row = frame->data[0] + ptrdiff_t(y) * frame->linesize[0];
        for (int x = 0; x < frame->width; ++x) { h ^= row[x]; h *= 1099511628211ull; }
    }
    return h;
}

int sourceTest(const std::wstring& path, const std::filesystem::path& out, const Options& o) {
    std::filesystem::create_directories(out);
    Logger::instance().openFile((out / L"source.log").wstring());
    Logger::instance().setConsoleEnabled(false);
    std::atomic<bool> cancel = false;
    source::SourceOpenDesc desc;
    desc.path = path;
    desc.cancelled = &cancel;
    desc.preferHardwareDecode = num(o, "hw", 0) != 0;
    desc.deinterlace = num(o, "deinterlace", 0) != 0;   // playback default is on; offline readers keep it off
    desc.network = networkSettings(o);
    desc.nonBlocking = num(o, "nonblocking", 0) != 0;
    desc.tolerateCorruption = num(o, "tolerant", 0) != 0;
    const double maxFrames = num(o, "frames", 1e12), maxSeconds = num(o, "seconds", 1e9);
    const bool hash = num(o, "hash", 0) != 0;
    const bool pace = num(o, "pace", 0) != 0;   // consume at the content rate, like playback does
    // seek=<targetSeconds>@<afterFrames>
    double seekTarget = -1;
    uint64_t seekAfter = 0;
    if (const auto s = str(o, "seek"); !s.empty()) {
        seekTarget = std::atof(s.c_str());
        seekAfter = uint64_t(std::atof(s.c_str() + s.find('@') + 1));
    }
    const auto openStart = Clock::now();
    source::MediaFileSource input;
    if (!input.open(desc)) {
        std::cout << "RESULT {\"opened\":false,\"error\":\"" << jsonEscape(narrow(input.errorMessage())) << "\",\"openMs\":"
                  << std::chrono::duration<double, std::milli>(Clock::now() - openStart).count() << "}" << std::endl;
        return 2;
    }
    const double openMs = std::chrono::duration<double, std::milli>(Clock::now() - openStart).count();
    std::ofstream csv(out / "frames.csv");
    csv << "index,ptsSeconds,flags,hash\n";
    uint64_t frames = 0, waits = 0, discontinuities = 0, bigGaps = 0, nonMonotonic = 0;
    double firstPts = -1, lastPts = -1, maxGapMs = 0, waitedMs = 0;
    uint64_t overall = 1469598103934665603ull;
    source::SourceReadStatus status = source::SourceReadStatus::Frame;
    const AVFrame* decoded = nullptr;
    pipeline::FramePacket packet;
    auto start = Clock::now(), lastFrame = start, lastWait = start;
    bool seeked = false;
    double paceBase = -1;
    Clock::time_point paceStart{};
    while (true) {
        if (std::chrono::duration<double>(Clock::now() - start).count() > maxSeconds) { status = source::SourceReadStatus::Eos; break; }
        status = input.read(packet, &decoded);
        if (status == source::SourceReadStatus::Waiting) {
            ++waits;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waitedMs += 10;
            continue;
        }
        if (status != source::SourceReadStatus::Frame) break;
        const auto now = Clock::now();
        const double gap = std::chrono::duration<double, std::milli>(now - lastFrame).count();
        if (frames) { maxGapMs = std::max(maxGapMs, gap); bigGaps += gap > 500; }
        lastFrame = now;
        const double pts = packet.pts.toDouble();
        if (firstPts < 0) firstPts = pts;
        if (lastPts >= 0 && pts <= lastPts && !(packet.flags & (uint32_t(pipeline::FrameFlagBits::Seek) | uint32_t(pipeline::FrameFlagBits::Discontinuity)))) ++nonMonotonic;
        if (packet.flags & uint32_t(pipeline::FrameFlagBits::Discontinuity)) ++discontinuities;
        lastPts = pts;
        const uint64_t h = hash ? lumaHash(decoded) : 0;
        overall = (overall ^ h) * 1099511628211ull;
        csv << frames << ',' << pts << ',' << packet.flags << ',' << h << '\n';
        ++frames;
        if (pace) {
            if (paceBase < 0) { paceBase = pts; paceStart = Clock::now(); }
            const auto due = paceStart + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>((pts - paceBase)));
            std::this_thread::sleep_until(due);
        }
        if (seekTarget >= 0 && !seeked && frames >= seekAfter) {
            seeked = true;
            const auto seekStart = Clock::now();
            const bool ok = input.seek({int64_t(seekTarget * 1e6), 1000000});
            std::cout << "SEEK ok=" << ok << " ms=" << std::chrono::duration<double, std::milli>(Clock::now() - seekStart).count() << std::endl;
            if (!ok) { status = source::SourceReadStatus::Error; break; }
            paceBase = -1;
        }
        if (double(frames) >= maxFrames) break;
    }
    const auto stats = input.networkStats();
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    std::ostringstream json;
    json << "{\"opened\":true,\"openMs\":" << openMs << ",\"frames\":" << frames << ",\"firstPts\":" << firstPts << ",\"lastPts\":" << lastPts
         << ",\"status\":\"" << (status == source::SourceReadStatus::Eos ? "eos" : status == source::SourceReadStatus::Frame ? "limit" : status == source::SourceReadStatus::Error ? "error" : "waiting")
         << "\",\"error\":\"" << jsonEscape(narrow(input.errorMessage())) << "\",\"elapsedSeconds\":" << elapsed
         << ",\"maxFrameGapMs\":" << maxGapMs << ",\"gapsOver500ms\":" << bigGaps << ",\"waits\":" << waits << ",\"nonMonotonic\":" << nonMonotonic
         << ",\"discontinuities\":" << discontinuities << ",\"hash\":\"" << std::hex << overall << std::dec << "\""
         << ",\"deinterlaced\":" << (input.info().deinterlaced ? "true" : "false")
         << ",\"session\":" << (stats.active ? "true" : "false") << ",\"live\":" << (stats.live ? "true" : "false")
         << ",\"corruptPackets\":" << stats.corruptPackets << ",\"corruptFrames\":" << stats.corruptFrames << ",\"skippedFrames\":" << stats.skippedFrames
         << ",\"reconnects\":" << stats.reconnects << ",\"connections\":" << stats.connections << ",\"stalls\":" << stats.stalls
         << ",\"sessionBytes\":" << stats.sessionBytes << ",\"bandwidthLow\":" << (stats.bandwidthLow ? "true" : "false")
         << ",\"state\":\"" << stateName(stats.state) << "\",\"message\":\"" << jsonEscape(narrow(stats.message)) << "\"}";
    std::cout << "RESULT " << json.str() << std::endl;
    input.close();
    return status == source::SourceReadStatus::Error ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
int engineTest(const std::wstring& path, const std::filesystem::path& out, const Options& o) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::filesystem::create_directories(out);
    Logger::instance().openFile((out / L"player.log").wstring());
    Logger::instance().setConsoleEnabled(false);
    HWND window = CreateWindowExW(0, L"STATIC", L"Network buffer playback test", WS_POPUP, 0, 0, 960, 540, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    auto playerOwner = std::make_unique<engine::EngineController>();
    auto& player = *playerOwner;
    player.setVolume(0, true);
    engine::PlayerOptions options;
    options.settings.nrRuntime = engine::NrRuntime::NvidiaOriginal;
    options.settings.nrPolicy = pipeline::NrSizePolicy::Realtime;
    options.network = networkSettings(o);
    options.softwareDecode = num(o, "software", 0) != 0;
    const double seconds = num(o, "seconds", 30);
    // events: seek=<target>@<atWallSeconds>, pause=<seconds>@<atWallSeconds>
    double seekTarget = -1, seekAt = 0, pauseFor = 0, pauseAt = 0;
    if (const auto s = str(o, "seek"); !s.empty()) { seekTarget = std::atof(s.c_str()); seekAt = std::atof(s.c_str() + s.find('@') + 1); }
    if (const auto s = str(o, "pause"); !s.empty()) { pauseFor = std::atof(s.c_str()); pauseAt = std::atof(s.c_str() + s.find('@') + 1); }
    std::ofstream timeline(out / "timeline.csv");
    timeline << "t,transport,position,realPresented,state,bufferedSec,targetSec,downloadMBps,serverMBps,bitrateMBps,reconnects,attempt,stalls,corruptPackets,corruptFrames,connections,lateMs,lateP95Ms,failed,status\n";
    const auto begin = Clock::now();
    player.open(window, path, options);
    uint64_t maxFrames = 0, samples = 0;
    double firstFrameAt = -1, maxLate = 0, maxP95 = 0, lastPosition = -1, positionStall = 0, stallStart = -1, maxStall = 0;
    double bufferingSeconds = 0, reconnectingSeconds = 0, playingSeconds = 0, prebufferSeconds = 0;
    std::map<std::string, int> seen;
    uint64_t maxBufferedBytes = 0, capBytes = 0;
    std::vector<std::string> messages;   // distinct status messages with the numbers masked
    auto masked = [](const std::string& text) {
        std::string out;
        for (const char c : text) {
            if ((c >= '0' && c <= '9') || c == '.') { if (out.empty() || out.back() != '#') out += '#'; }
            else out += c;
        }
        return out;
    };
    bool failed = false, seekDone = false, pauseDone = false, resumed = false;
    std::wstring lastStatus, failure;
    double clockAtBufferingStart = -1, maxClockAdvanceWhileBuffering = 0;
    bool inBuffering = false;
    auto lastSample = begin;
    engine::PlayerSnapshot s;
    while (true) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        const double t = std::chrono::duration<double>(Clock::now() - begin).count();
        if (t > seconds) break;
        s = player.snapshot();
        if (s.failed) { failed = true; failure = s.status; break; }
        const double dt = std::chrono::duration<double>(Clock::now() - lastSample).count();
        lastSample = Clock::now();
        const auto& n = s.network;
        const std::string state = n.active ? stateName(n.state) : "local";
        ++seen[state];
        if (n.active) {
            maxBufferedBytes = std::max(maxBufferedBytes, n.bufferedBytes);
            capBytes = std::max(capBytes, n.capBytes);
            if (!n.message.empty()) {
                const auto m = masked(narrow(n.message));
                if (messages.size() < 12 && std::find(messages.begin(), messages.end(), m) == messages.end()) messages.push_back(m);
            }
        }
        maxFrames = std::max(maxFrames, s.metrics.flow.counters.realPresented);
        if (firstFrameAt < 0 && s.metrics.flow.counters.realPresented > 0) firstFrameAt = t;
        if (n.active) {
            if (n.state == media::NetworkStats::State::Buffering) bufferingSeconds += dt;
            else if (n.state == media::NetworkStats::State::Reconnecting) reconnectingSeconds += dt;
            else if (n.state == media::NetworkStats::State::Prebuffering) prebufferSeconds += dt;
            else if (n.state == media::NetworkStats::State::Playing) playingSeconds += dt;
        }
        const bool buffering = n.active && (n.state == media::NetworkStats::State::Buffering);
        if (buffering && !inBuffering) { inBuffering = true; clockAtBufferingStart = s.position; }
        if (!buffering && inBuffering) { inBuffering = false; }
        if (inBuffering) maxClockAdvanceWhileBuffering = std::max(maxClockAdvanceWhileBuffering, s.position - clockAtBufferingStart);
        // The picture must keep moving while playing: measure the longest time the position stood still.
        if (s.position != lastPosition) { lastPosition = s.position; stallStart = -1; }
        else if (stallStart < 0) stallStart = t;
        if (stallStart >= 0) maxStall = std::max(maxStall, t - stallStart);
        // The engine's lateness percentile needs a window: the first second of pictures is not judged.
        if (s.transport == engine::TransportState::Playing && firstFrameAt >= 0 && s.metrics.flow.counters.realPresented >= 30) { maxLate = std::max(maxLate, std::abs(s.lateMs)); maxP95 = std::max(maxP95, s.lateP95Ms); }
        if (!seekDone && seekTarget >= 0 && t >= seekAt) { seekDone = true; player.seek(seekTarget); }
        if (!pauseDone && pauseFor > 0 && t >= pauseAt) { pauseDone = true; player.pause(true); }
        if (pauseDone && !resumed && t >= pauseAt + pauseFor) { resumed = true; player.pause(false); }
        if ((samples++ % 1) == 0) {
            timeline << t << ',' << int(s.transport) << ',' << s.position << ',' << s.metrics.flow.counters.realPresented << ',' << state << ',' << n.bufferedSeconds << ','
                     << n.targetSeconds << ',' << n.downloadBytesPerSecond / 1048576.0 << ',' << n.serverBytesPerSecond / 1048576.0 << ',' << n.bitrateBytesPerSecond / 1048576.0 << ','
                     << n.reconnects << ',' << n.reconnectAttempt << ',' << n.stalls << ',' << n.corruptPackets << ',' << n.corruptFrames << ',' << n.connections << ','
                     << s.lateMs << ',' << s.lateP95Ms << ',' << s.failed << ",\"" << jsonEscape(narrow(s.status)) << "\"\n";
        }
        if (s.transport == engine::TransportState::Ended) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const auto stopAt = Clock::now();
    const double finalPosition = s.position;
    const auto finalNet = s.network;
    player.stop();
    const double stopMs = std::chrono::duration<double, std::milli>(Clock::now() - stopAt).count();
    // stop() only posts the command; the engine thread (and the session under it) is joined here, so this is
    // how long a user stop / source switch really takes to get out of a blocked connect, read or wait.
    playerOwner.reset();
    const double teardownMs = std::chrono::duration<double, std::milli>(Clock::now() - stopAt).count();
    DestroyWindow(window);
    std::ostringstream json;
    json << "{\"failed\":" << (failed ? "true" : "false") << ",\"failure\":\"" << jsonEscape(narrow(failure)) << "\",\"realPresented\":" << maxFrames
         << ",\"firstFrameSeconds\":" << firstFrameAt << ",\"finalPosition\":" << finalPosition << ",\"maxPositionStallSeconds\":" << maxStall
         << ",\"bufferingSeconds\":" << bufferingSeconds << ",\"reconnectingSeconds\":" << reconnectingSeconds << ",\"prebufferSeconds\":" << prebufferSeconds
         << ",\"playingSeconds\":" << playingSeconds << ",\"maxClockAdvanceWhileBuffering\":" << maxClockAdvanceWhileBuffering
         << ",\"maxLateMs\":" << maxLate << ",\"maxLateP95Ms\":" << maxP95 << ",\"stopMs\":" << stopMs << ",\"teardownMs\":" << teardownMs
         << ",\"endState\":\"" << (finalNet.active ? stateName(finalNet.state) : "local") << "\",\"states\":{";
    bool first = true;
    for (const auto& [name, count] : seen) { json << (first ? "" : ",") << "\"" << name << "\":" << count; first = false; }
    json << "},\"reconnects\":" << finalNet.reconnects << ",\"stalls\":" << finalNet.stalls << ",\"connections\":" << finalNet.connections
         << ",\"corruptPackets\":" << finalNet.corruptPackets << ",\"corruptFrames\":" << finalNet.corruptFrames << ",\"skippedFrames\":" << finalNet.skippedFrames
         << ",\"sessionBytes\":" << finalNet.sessionBytes << ",\"bandwidthLow\":" << (finalNet.bandwidthLow ? "true" : "false")
         << ",\"bitrateMBps\":" << finalNet.bitrateBytesPerSecond / 1048576.0 << ",\"serverMBps\":" << finalNet.serverBytesPerSecond / 1048576.0
         << ",\"maxBufferedMiB\":" << maxBufferedBytes / 1048576.0 << ",\"capMiB\":" << capBytes / 1048576.0
         << ",\"message\":\"" << jsonEscape(narrow(finalNet.message)) << "\",\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) json << (i ? "," : "") << "\"" << jsonEscape(messages[i]) << "\"";
    json << "]}";
    std::cout << "RESULT " << json.str() << std::endl;
    return failed ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// scan: the embedded-subtitle scan of a file on a server (C5). Run with VEYRA_TEST_FORCE_REMOTE_PATH=1
// (a local file stands in for the NAS) and, to pace it, VEYRA_TEST_SLOW_IO=<MB/s>.
//   - gate closed: nothing may be read or published (closedSeconds, default 3)
//   - gate open: the scan completes; bytes / seconds is the pace
//   - toggle=1: the gate closes closeAfter seconds into the scan; reading must stop and then resume
int scanTest(const std::wstring& path, const std::filesystem::path& out, const Options& o) {
    std::filesystem::create_directories(out);
    Logger::instance().openFile((out / L"scan.log").wstring());
    Logger::instance().setConsoleEnabled(false);
    engine::SubtitleLoader loader;
    auto gate = std::make_shared<std::atomic<bool>>(false);
    loader.setReadGate(gate);
    const bool server = engine::subtitleServerFile(path);
    const uint64_t bytesBefore = engine::subtitleServerScanBytes();
    loader.request(path);
    const auto begin = Clock::now();
    auto since = [&] { return std::chrono::duration<double>(Clock::now() - begin).count(); };
    uint64_t publishedWhileClosed = 0;
    const double closedFor = num(o, "closedSeconds", 3);
    bool complete = false;
    double completeAt = -1;
    std::vector<engine::SubtitleTrack> tracks;
    while (since() < closedFor) {
        // A local file is not gated: its scan finishes in this window (the control case).
        if (auto r = loader.poll()) {
            ++publishedWhileClosed;
            tracks = std::move(r->tracks);
            if (r->complete) { complete = true; completeAt = since(); break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const uint64_t bytesWhileClosed = engine::subtitleServerScanBytes() - bytesBefore;
    const double openAt = since();
    gate->store(true);
    const bool toggle = num(o, "toggle", 0) != 0;
    const double closeAfter = num(o, "closeAfter", 1.0), closedAgainFor = num(o, "closedAgainSeconds", 2.0);
    bool closedAgain = false, reopened = false;
    double closeStart = 0;
    uint64_t bytesAtClose = 0, bytesWhileClosedAgain = 0, partials = 0;
    while (!complete && since() < openAt + num(o, "timeout", 120)) {
        if (auto r = loader.poll()) {
            tracks = std::move(r->tracks);
            if (r->complete) { complete = true; completeAt = since(); break; }
            ++partials;
        }
        if (toggle) {
            if (!closedAgain && since() - openAt >= closeAfter) {
                closedAgain = true;
                gate->store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(400));   // a read already in flight finishes
                bytesAtClose = engine::subtitleServerScanBytes();
                closeStart = since();
            }
            if (closedAgain && !reopened && since() - closeStart >= closedAgainFor) {
                bytesWhileClosedAgain = engine::subtitleServerScanBytes() - bytesAtClose;
                reopened = true;
                gate->store(true);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const uint64_t bytesTotal = engine::subtitleServerScanBytes() - bytesBefore;
    std::error_code ec;
    const auto fileBytes = std::filesystem::file_size(std::filesystem::path(path), ec);
    const double scanSeconds = complete ? std::max(0.0, completeAt - (publishedWhileClosed && !server ? 0.0 : openAt)) : -1;
    std::ostringstream json;
    json << "{\"serverFile\":" << (server ? "true" : "false") << ",\"publishedWhileClosed\":" << publishedWhileClosed
         << ",\"bytesWhileClosed\":" << bytesWhileClosed << ",\"complete\":" << (complete ? "true" : "false")
         << ",\"scanSeconds\":" << scanSeconds << ",\"partialPublishes\":" << partials << ",\"bytesRead\":" << bytesTotal
         << ",\"fileBytes\":" << (ec ? 0 : fileBytes) << ",\"rateMBps\":" << (scanSeconds > 0 ? bytesTotal / 1048576.0 / scanSeconds : 0.0)
         << ",\"toggled\":" << (toggle ? "true" : "false") << ",\"bytesWhileClosedAgain\":" << bytesWhileClosedAgain
         << ",\"tracks\":" << tracks.size() << ",\"cues\":[";
    for (size_t i = 0; i < tracks.size(); ++i) json << (i ? "," : "") << tracks[i].cues.size();
    json << "]}";
    std::cout << "RESULT " << json.str() << std::endl;
    return complete ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && std::wstring_view(argv[1]) == L"selftest") return selfTest();
    if (argc < 4) {
        std::cerr << "usage: selftest | source|engine|scan <path-or-url> <outdir> [k=v...]" << std::endl;
        return 2;
    }
    const Options options = parseOptions(argc, argv, 4);
    // ffmpeglog=<quiet|warning|verbose|debug|trace>: FFmpeg's own messages to stderr (a diagnosis aid; they
    // include URLs, so runs against signed addresses must never use it).
    if (const auto level = str(options, "ffmpeglog"); !level.empty())
        av_log_set_level(level == "quiet" ? AV_LOG_QUIET : level == "verbose" ? AV_LOG_VERBOSE : level == "debug" ? AV_LOG_DEBUG : level == "trace" ? AV_LOG_TRACE : AV_LOG_WARNING);
    if (std::wstring_view(argv[1]) == L"source") return sourceTest(argv[2], argv[3], options);
    if (std::wstring_view(argv[1]) == L"engine") return engineTest(argv[2], argv[3], options);
    if (std::wstring_view(argv[1]) == L"scan") return scanTest(argv[2], argv[3], options);
    return 2;
}
