// SPDX-License-Identifier: GPL-3.0-only
// veyra_frame_analysis_tests <empty directory>
// A synthetic 性能诊断 package with four hitches of known cause and two known
// environment problems; the analyzer has to name each of them.
#include "veyra/diagnostics/FrameAnalysis.h"
#include "../../src/diagnostics/MiniJson.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace analysis = veyra::diagnostics::analysis;
namespace json = veyra::diagnostics::json;

namespace {
constexpr int64_t H0 = 1000000000000;   // host ticks (100 ns)
constexpr int64_t U0 = 1795800000000;   // started_utc_ms
constexpr int64_t Frame = 166667;       // 60 fps

std::string read(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream s; s << in.rdbuf(); return s.str(); }

struct Row {
    std::string event, frameKind, stage;
    int64_t host = 0, process = 0, ready = 0, begin = 0, end = 0, arrival = 0, call = 0, pts = 0;
    uint64_t revision = 1, source = 0;
    uint32_t detail = 0, count = 1;
    double ms = 0;
};
std::string row(const Row& r) {
    char b[600];
    std::snprintf(b, sizeof(b), "%s,%lld,1,%llu,1,%llu,%llu,0,%lld,%u,%u,%.3f,0,%lld,%lld,%lld,%lld,0,0,1,0,0,0,0,0,0,0,0,0,0,%s,%s,%lld,0,0,0,0,0,0,%lld\n",
                  r.event.c_str(), (long long)r.host, (unsigned long long)r.revision, (unsigned long long)r.source, (unsigned long long)r.source,
                  (long long)r.pts, r.detail, r.count, r.ms, (long long)r.process, (long long)r.ready, (long long)r.begin, (long long)r.end,
                  r.frameKind.c_str(), r.stage.c_str(), (long long)r.arrival, (long long)r.call);
    return b;
}

void writePackage(const fs::path& dir, bool smoothMotion) {
    fs::create_directories(dir / "raw");
    std::vector<Row> rows;
    int64_t shift = 0;
    const int frames = 1200;
    for (int i = 0; i < frames; ++i) {
        int64_t extraGpu = 0;
        int64_t blockPrevious = 0;
        if (i == 300) { shift += 400000; extraGpu = 400000; }        // GPU stall 40 ms
        if (i == 600) { shift += 350000; blockPrevious = 350000; }   // previous Present blocked 35 ms
        if (i == 900) shift += 500000;                                 // input gap 50 ms
        if (i == 1050) {                                                // reset 80 ms
            shift += 800000;
            Row reset; reset.event = "Reset"; reset.host = H0 + int64_t(i) * Frame + shift - 300000; reset.detail = 9; reset.ms = 80;
            rows.push_back(reset);
        }
        const int64_t t = H0 + int64_t(i) * Frame + shift;
        const bool real = i % 2 == 0;
        const uint64_t source = uint64_t(i / 2 + 1);
        Row p;
        p.event = "Present"; p.frameKind = real ? "real" : "generated"; p.source = source;
        p.revision = i >= 1050 ? 2 : 1;
        p.begin = t; p.end = t + 5000; p.call = t + 2000; p.host = p.end;
        p.ready = t - 30000;
        p.process = i == 600 ? t - 350000 - 80000 : t - 80000 - extraGpu;
        if (i == 600) p.ready = t - 350000 - 30000;
        p.arrival = p.process - 10000;
        p.ms = 0.5;
        if (i == 600 && !rows.empty()) {
            // The previous Present call took 35 ms.
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) if (it->event == "Present") { it->end += blockPrevious; it->host = it->end; break; }
        }
        if (real) {
            Row s; s.event = "Source"; s.frameKind = "real"; s.stage = "CaptureCard"; s.source = source;
            s.arrival = p.arrival; s.host = s.arrival + 20000; s.pts = s.arrival - H0; s.ms = 1.2;
            s.revision = p.revision;
            rows.push_back(s);
            Row g; g.event = "Gpu"; g.stage = "Fg1"; g.source = source; g.host = p.ready + 1000; g.ms = 4.0 + double(extraGpu) / 10000.0;
            g.revision = p.revision;
            rows.push_back(g);
            Row blit = g; blit.stage = "Blit"; blit.ms = 0.2;
            rows.push_back(blit);
        }
        rows.push_back(p);
    }
    Row mark; mark.event = "Mark"; mark.stage = "User"; mark.detail = 1; mark.host = H0 + 302 * Frame + 400000;
    rows.push_back(mark);
    const int64_t end = H0 + int64_t(frames) * Frame + shift;
    std::ofstream trace(dir / "raw" / "trace.csv", std::ios::binary);
    trace << "# schema=veyra-frame-trace/1\n# pid=1\n# host_ticks_per_second=10000000\n# started_host=" << H0 << "\n# ended_host=" << end
          << "\n# requested_ms=60000\n# termination=shutdown\n# recorded=" << rows.size() << "\n# dropped=0\n# capacity=200000000\n# started_utc_ms=" << U0
          << "\n# writer=veyra-diagnostic-stream/2\n# finalized=1\n"
             "event,host,session,revision,epoch,source,batch,fence,pts,detail,count,ms,decodedHost,processHost,readyHost,presentBeginHost,"
             "presentEndHost,entryDeviationMs,returnDeviationMs,queueDepth,mediaDeviationValid,providerInstance,providerCycle,preparationCycle,"
             "providerCallerRva,providerScheduleBeginHost,providerFenceSampled,providerFenceBefore,providerFenceAtDeadline,providerFenceTarget,"
             "frameKind,stage,arrivalHost,deadlineHost,sourceA,sourceAHost,gpuBegin,gpuEnd,gpuFrequency,presentCallHost\n";
    for (const auto& r : rows) trace << row(r);
    trace << "# complete=1\n";
    std::ofstream telemetry(dir / "raw" / "telemetry.csv", std::ios::binary);
    telemetry << "host,utc_ms,cpu_total,cpu_veyra,gpu_busy_max,gpu_veyra_max,gpu_others_max,gpu_top_other_util,gpu_top_other_name,ui_gap_ms\n";
    for (int s = 1; s <= 22; ++s)
        telemetry << (H0 + int64_t(s) * 10000000) << "," << (U0 + s * 1000) << ",30,4,90,30,60,60,Game.exe,250\n";
    std::ofstream log(dir / "raw" / "veyra-log.txt", std::ios::binary);
    for (int s = 1; s <= 20; ++s) {
        char line[400];
        std::snprintf(line, sizeof(line),
                      "2026-11-27T10:00:%02d.000Z t=1 [INFO ] [display-latency] presentToScanoutP50Ms=8.000 presentToScanoutP95Ms=9.000 "
                      "presentToScanoutMaxMs=10.000 samples=60 replacedAtLeast=0 compositionMode=2 refreshHz=60.000 vrrCapable=true vrrActive=false (legend)\n", s);
        log << line;
        std::snprintf(line, sizeof(line), "2026-11-27T10:00:%02d.100Z t=1 [INFO ] [display-stats] refreshHz=60.000 presentCalls=60 dxgiPresentCount=%d refreshes=60\n",
                      s, smoothMotion ? 120 : 60);
        log << line;
    }
    std::ofstream snapshot(dir / "raw" / "snapshot.json", std::ios::binary);
    snapshot << "{\"schema\":\"veyra-diagnostic-snapshot/1\",\"system\":{\"modules\":{\"flagged\":["
             << (smoothMotion ? "{\"module\":\"NvPresent64.dll\",\"product\":\"NVIDIA Smooth Motion\"}" : "")
             << "]},\"processes\":{\"flagged\":[]},\"power\":{\"acLine\":\"online\"},\"gpus\":[{\"name\":\"Test GPU\",\"driver\":\"1.2.3.4\"}],"
                "\"displays\":[],\"os\":{\"product\":\"Windows\",\"build\":\"26200.1\"},\"graphics\":{\"hagsEnabled\":true}},"
                "\"veyra\":{\"version\":\"" << (smoothMotion ? "A" : "B") << "\",\"preferences\":{\"fgQuality\":" << (smoothMotion ? 1 : 2) << "}},"
                "\"interface\":{\"variableRefresh\":false,\"fgMultiplier\":2}}";
    std::ofstream events(dir / "raw" / "events.json", std::ios::binary);
    events << "{\"events\":[{\"type\":\"setting\",\"name\":\"fgQuality\",\"previous\":1,\"value\":2,\"host\":" << (H0 + 120000000)
           << "}],\"trace\":{\"startedUtcMs\":" << U0 << "}}";
}

bool check(bool ok, const char* what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
    return ok;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const fs::path root = fs::absolute(argv[1]);
    if (fs::exists(root / "a")) { std::cerr << "use a fresh directory\n"; return 2; }
    writePackage(root / "a", true);
    writePackage(root / "b", false);
    bool passed = true;
    std::string error;
    analysis::Options options;
    passed &= check(analysis::analyze(root / "a", root / "a", options, error) == 0, "analyze package");
    if (!error.empty()) std::cout << "  " << error << "\n";
    json::Value report;
    try { report = json::parse(read(root / "a" / "report.json")); } catch (const std::exception& ex) { std::cout << ex.what() << "\n"; return 1; }
    const double fps = report["summary"]["averageFps"].number();
    passed &= check(fps > 55 && fps < 61, "average fps near 60");
    std::set<std::string> causes, stages;
    for (const auto& e : report["events"].array()) { causes.insert(e["cause"].string()); if (e["stage"].isString()) stages.insert(e["stage"].string()); }
    for (const char* cause : {"gpu", "present", "input", "reset"})
        passed &= check(causes.count(cause) == 1, (std::string("hitch attributed: ") + cause).c_str());
    passed &= check(stages.count("Fg1") == 1, "GPU hitch names stage Fg1");
    std::set<std::string> ids;
    for (const auto& f : report["findings"].array()) ids.insert(f["id"].string());
    passed &= check(ids.count("R1") == 1, "R1 Smooth Motion");
    passed &= check(ids.count("R10") == 1, "R10 other process on the GPU");
    passed &= check(ids.count("R3") == 0, "R3 not raised at 8 ms scanout");
    passed &= check(report["marks"].array().size() == 1 && report["marks"].array()[0]["cause"].string() == "gpu", "mark tied to the GPU hitch");
    passed &= check(report["settingsChanges"].array().size() == 1, "settings change listed");
    passed &= check(report["display"]["dxgiPresentsPerSecond"]["p50"].number() == 120, "display stats from the log");
    const std::string frames = read(root / "a" / "frames.csv");
    const auto lines = size_t(std::count(frames.begin(), frames.end(), '\n'));
    passed &= check(lines == 1201, "frames.csv has every present");
    passed &= check(read(root / "a" / "report.html").find("window.R={") != std::string::npos, "report.html embeds the report");
    passed &= check(read(root / "a" / "SUMMARY.md").find("Smooth Motion") != std::string::npos, "SUMMARY.md names the finding");
    // Log-only analysis (versions without the recorder).
    fs::create_directories(root / "log");
    passed &= check(analysis::analyze(root / "a" / "raw" / "veyra-log.txt", root / "log", options, error) == 0, "log-only analysis");
    // Comparison.
    passed &= check(analysis::analyze(root / "b", root / "b", options, error) == 0, "analyze second package");
    passed &= check(analysis::compare(root / "a", root / "b", root / "cmp", options, error) == 0, "compare");
    try {
        const auto cmp = json::parse(read(root / "cmp" / "compare.json"));
        bool version = false, pref = false;
        for (const auto& d : cmp["environmentDifferences"].array()) version |= d["name"].string() == "veyraVersion";
        for (const auto& d : cmp["preferenceDifferences"].array()) pref |= d["name"].string() == "fgQuality";
        passed &= check(version && pref && !cmp["metrics"].array().empty(), "compare lists version and setting differences");
    } catch (const std::exception& ex) { std::cout << ex.what() << "\n"; passed = false; }
    return passed ? 0 : 1;
}
