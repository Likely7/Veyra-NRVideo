// SPDX-License-Identifier: GPL-3.0-only
// 性能诊断 analysis. Extends the method of tools/frame_analyzer (the private
// trace analyzer contributed by the Magpie author): one host clock for every
// event, steady-state statistics, and anomalies explained by the stage that
// took the extra time. Rules R1–R17 encode past field cases (docs/PERF_DIAGNOSTICS.md).
#include "veyra/diagnostics/FrameAnalysis.h"
#include "FrameAnalysisInternal.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace veyra::diagnostics::analysis {
using namespace detail;
using json::Array;
using json::Object;
using json::Value;
namespace fs = std::filesystem;

namespace detail {
std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
bool writeText(const fs::path& path, const std::string& text) {
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) return false;
    }
    std::error_code ec;
    fs::rename(temporary, path, ec);
    if (ec) { fs::remove(temporary, ec); return false; }
    return true;
}
}

namespace {
// ---------------------------------------------------------------- helpers
template <class T> T num(std::string_view s, T fallback = T{}) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    T v{};
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() ? v : fallback;
}
void split(std::string_view line, char separator, std::vector<std::string_view>& out) {
    out.clear();
    size_t start = 0;
    for (;;) {
        const auto at = line.find(separator, start);
        if (at == std::string_view::npos) { out.push_back(line.substr(start)); return; }
        out.push_back(line.substr(start, at - start));
        start = at + 1;
    }
}
double round3(double v) { return std::isfinite(v) ? std::round(v * 1000.0) / 1000.0 : 0; }
double round1(double v) { return std::isfinite(v) ? std::round(v * 10.0) / 10.0 : 0; }

struct Dist {
    size_t n = 0;
    double mean = 0, sd = 0, min = 0, p50 = 0, p90 = 0, p95 = 0, p99 = 0, p999 = 0, max = 0;
};
double quantileSorted(const std::vector<double>& v, double q) {
    if (v.empty()) return 0;
    const double pos = q * double(v.size() - 1);
    const size_t lo = size_t(std::floor(pos)), hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - double(lo));
}
Dist distOf(std::vector<double> v) {
    Dist d;
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    d.n = v.size();
    if (v.empty()) return d;
    std::sort(v.begin(), v.end());
    d.min = v.front(); d.max = v.back();
    d.mean = std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
    double sq = 0;
    for (double x : v) sq += (x - d.mean) * (x - d.mean);
    d.sd = v.size() > 1 ? std::sqrt(sq / double(v.size() - 1)) : 0;
    d.p50 = quantileSorted(v, 0.5); d.p90 = quantileSorted(v, 0.9); d.p95 = quantileSorted(v, 0.95);
    d.p99 = quantileSorted(v, 0.99); d.p999 = quantileSorted(v, 0.999);
    return d;
}
Value distJson(const Dist& d) {
    Object o;
    o["n"] = d.n;
    if (d.n) {
        o["mean"] = round3(d.mean); o["sd"] = round3(d.sd); o["min"] = round3(d.min); o["p50"] = round3(d.p50);
        o["p90"] = round3(d.p90); o["p95"] = round3(d.p95); o["p99"] = round3(d.p99); o["p999"] = round3(d.p999); o["max"] = round3(d.max);
    }
    return o;
}
double median(std::vector<double> v) { return distOf(std::move(v)).p50; }

// ---------------------------------------------------------------- trace
enum class K : uint8_t { Submitted, Ready, Present, Gpu, Reset, Cancelled, FrameReady, Discarded, XessSleep, XessBind, XessPresent,
                         ProviderOutput, ProviderDeadline, Source, SourceSkipped, Cpu, GpuUnavailable, Mark, Other };
K kindOf(std::string_view name) {
    static const std::pair<std::string_view, K> table[] = {
        {"Submitted", K::Submitted}, {"Ready", K::Ready}, {"Present", K::Present}, {"Gpu", K::Gpu}, {"Reset", K::Reset},
        {"Cancelled", K::Cancelled}, {"FrameReady", K::FrameReady}, {"Discarded", K::Discarded}, {"XessSleep", K::XessSleep},
        {"XessBind", K::XessBind}, {"XessPresent", K::XessPresent}, {"ProviderOutput", K::ProviderOutput},
        {"ProviderDeadline", K::ProviderDeadline}, {"Source", K::Source}, {"SourceSkipped", K::SourceSkipped}, {"Cpu", K::Cpu},
        {"GpuUnavailable", K::GpuUnavailable}, {"Mark", K::Mark}};
    for (const auto& [n, k] : table) if (n == name) return k;
    return K::Other;
}

struct Ev {
    K kind = K::Other;
    uint8_t frameKind = 0;       // 1 real, 2 generated
    uint16_t stage = 0;
    uint32_t detail = 0, count = 0, queueDepth = 0;
    int64_t host = 0, pts = 0;
    uint64_t session = 0, revision = 0, epoch = 0, source = 0, batch = 0, sourceA = 0;
    double ms = 0;
    int64_t decodedHost = 0, processHost = 0, readyHost = 0, presentBeginHost = 0, presentEndHost = 0;
    int64_t arrivalHost = 0, deadlineHost = 0, sourceAHost = 0, presentCallHost = 0;
};

struct Trace {
    std::map<std::string, std::string> meta;
    std::vector<Ev> events;
    std::vector<std::string> stages{""};
    bool complete = false, finalized = false, partial = false;
    uint64_t malformed = 0;
    int64_t started = 0, ended = 0, startedUtcMs = 0;
    uint64_t declaredRecorded = 0, dropped = 0;
    std::string termination, file;
};

bool readTrace(const fs::path& path, Trace& trace, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path.string(); return false; }
    trace.file = path.filename().string();
    trace.partial = path.extension() == ".partial";
    enum Col { cEvent, cHost, cSession, cRevision, cEpoch, cSource, cBatch, cPts, cDetail, cCount, cMs, cDecoded, cProcess, cReady,
               cBegin, cEnd, cQueue, cFrameKind, cStage, cArrival, cDeadline, cSourceA, cSourceAHost, cCall, cColumns };
    static const char* names[] = {"event", "host", "session", "revision", "epoch", "source", "batch", "pts", "detail", "count", "ms",
                                  "decodedHost", "processHost", "readyHost", "presentBeginHost", "presentEndHost", "queueDepth",
                                  "frameKind", "stage", "arrivalHost", "deadlineHost", "sourceA", "sourceAHost", "presentCallHost"};
    std::array<int, cColumns> col;
    col.fill(-1);
    size_t columnCount = 0;
    bool header = false;
    std::unordered_map<std::string, uint16_t> stageIndex{{"", 0}};
    std::vector<std::string_view> cells;
    std::string carry;
    std::vector<char> buffer(8 << 20);
    const auto line = [&](std::string_view text) {
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
        if (text.empty()) return;
        if (text.front() == '#') {
            if (text == "# complete=1") { trace.complete = true; return; }
            const auto eq = text.find('=');
            if (eq != std::string_view::npos && text.size() > 2) {
                std::string key(text.substr(2, eq - 2));
                std::string value(text.substr(eq + 1));
                while (!value.empty() && value.back() == ' ') value.pop_back();
                trace.meta[key] = value;
            }
            return;
        }
        if (!header) {
            split(text, ',', cells);
            columnCount = cells.size();
            for (size_t i = 0; i < cells.size(); ++i)
                for (int c = 0; c < cColumns; ++c) if (cells[i] == names[c]) col[size_t(c)] = int(i);
            header = true;
            return;
        }
        split(text, ',', cells);
        if (cells.size() != columnCount || col[cEvent] < 0 || col[cHost] < 0) { ++trace.malformed; return; }
        const auto get = [&](int c) -> std::string_view { return col[size_t(c)] >= 0 ? cells[size_t(col[size_t(c)])] : std::string_view(); };
        Ev e;
        e.kind = kindOf(get(cEvent));
        e.host = num<int64_t>(get(cHost));
        if (e.host <= 0) { ++trace.malformed; return; }
        e.session = num<uint64_t>(get(cSession)); e.revision = num<uint64_t>(get(cRevision)); e.epoch = num<uint64_t>(get(cEpoch));
        e.source = num<uint64_t>(get(cSource)); e.batch = num<uint64_t>(get(cBatch)); e.pts = num<int64_t>(get(cPts));
        e.detail = num<uint32_t>(get(cDetail)); e.count = num<uint32_t>(get(cCount)); e.ms = num<double>(get(cMs));
        e.decodedHost = num<int64_t>(get(cDecoded)); e.processHost = num<int64_t>(get(cProcess)); e.readyHost = num<int64_t>(get(cReady));
        e.presentBeginHost = num<int64_t>(get(cBegin)); e.presentEndHost = num<int64_t>(get(cEnd)); e.queueDepth = num<uint32_t>(get(cQueue));
        const auto fk = get(cFrameKind);
        e.frameKind = fk == "real" ? 1 : fk == "generated" ? 2 : 0;
        const std::string stage(get(cStage));
        auto [it, inserted] = stageIndex.try_emplace(stage, uint16_t(trace.stages.size()));
        if (inserted) trace.stages.push_back(stage);
        e.stage = it->second;
        e.arrivalHost = num<int64_t>(get(cArrival)); e.deadlineHost = num<int64_t>(get(cDeadline));
        e.sourceA = num<uint64_t>(get(cSourceA)); e.sourceAHost = num<int64_t>(get(cSourceAHost)); e.presentCallHost = num<int64_t>(get(cCall));
        trace.events.push_back(e);
    };
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        const size_t got = size_t(in.gcount());
        if (!got) break;
        std::string_view chunk(buffer.data(), got);
        size_t start = 0;
        for (;;) {
            const auto nl = chunk.find('\n', start);
            if (nl == std::string_view::npos) { carry.append(chunk.substr(start)); break; }
            if (!carry.empty()) { carry.append(chunk.substr(start, nl - start)); line(carry); carry.clear(); }
            else line(chunk.substr(start, nl - start));
            start = nl + 1;
        }
    }
    // A torn last line (the process died while writing) is ignored by the count check.
    if (!carry.empty()) { std::vector<std::string_view> probe; split(carry, ',', probe); if (probe.size() == columnCount) line(carry); else ++trace.malformed; }
    if (!header) { error = "no column header in " + path.string(); return false; }
    const auto metaNum = [&](const char* key) { const auto it = trace.meta.find(key); return it == trace.meta.end() ? int64_t(0) : num<int64_t>(it->second); };
    trace.started = metaNum("started_host");
    trace.ended = metaNum("ended_host");
    trace.startedUtcMs = metaNum("started_utc_ms");
    trace.declaredRecorded = uint64_t(metaNum("recorded"));
    trace.dropped = uint64_t(metaNum("dropped"));
    trace.finalized = trace.meta["finalized"] == "1" || (trace.complete && trace.meta.find("finalized") == trace.meta.end());
    trace.termination = trace.meta["termination"];
    if (trace.meta["schema"] != "veyra-frame-trace/1") { error = "unsupported trace schema '" + trace.meta["schema"] + "'"; return false; }
    std::stable_sort(trace.events.begin(), trace.events.end(), [](const Ev& a, const Ev& b) { return a.host < b.host; });
    return true;
}

// ---------------------------------------------------------------- telemetry
struct Telemetry {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    std::unordered_map<std::string, size_t> index;
    double get(size_t row, std::string_view name, double fallback = NAN) const {
        const auto it = index.find(std::string(name));
        if (it == index.end() || row >= rows.size() || it->second >= rows[row].size()) return fallback;
        const auto& text = rows[row][it->second];
        if (text.empty()) return fallback;
        if (text.size() > 2 && text[0] == '0' && text[1] == 'x') return double(std::strtoull(text.c_str() + 2, nullptr, 16));
        return num<double>(text, fallback);
    }
    std::string text(size_t row, std::string_view name) const {
        const auto it = index.find(std::string(name));
        if (it == index.end() || row >= rows.size() || it->second >= rows[row].size()) return {};
        return rows[row][it->second];
    }
};
Telemetry readTelemetry(const fs::path& path) {
    Telemetry t;
    std::ifstream in(path, std::ios::binary);
    std::string text;
    std::vector<std::string_view> cells;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        if (text.empty()) continue;
        split(text, ',', cells);
        if (t.columns.empty()) {
            for (auto c : cells) { t.index[std::string(c)] = t.columns.size(); t.columns.emplace_back(c); }
            continue;
        }
        if (cells.size() != t.columns.size()) continue;
        std::vector<std::string> row;
        row.reserve(cells.size());
        for (auto c : cells) row.emplace_back(c);
        t.rows.push_back(std::move(row));
    }
    return t;
}

// ---------------------------------------------------------------- log
struct LogLine {
    double t = NAN;            // ms on the trace timeline (NaN when it cannot be placed)
    int64_t utcMs = 0;
    char level = 'I';
    std::string tag, text;
    std::map<std::string, std::string> kv;
    double d(const char* key, double fallback = NAN) const {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : num<double>(it->second, fallback);
    }
    std::string s(const char* key) const { const auto it = kv.find(key); return it == kv.end() ? std::string() : it->second; }
};
int64_t parseIsoUtcMs(std::string_view s) {
    // 2026-11-27T09:03:16.914Z
    if (s.size() < 23 || s[4] != '-' || s[10] != 'T') return 0;
    std::tm tm{};
    tm.tm_year = num<int>(s.substr(0, 4)) - 1900; tm.tm_mon = num<int>(s.substr(5, 2)) - 1; tm.tm_mday = num<int>(s.substr(8, 2));
    tm.tm_hour = num<int>(s.substr(11, 2)); tm.tm_min = num<int>(s.substr(14, 2)); tm.tm_sec = num<int>(s.substr(17, 2));
    const int ms = num<int>(s.substr(20, 3));
    const auto seconds = _mkgmtime(&tm);
    return seconds < 0 ? 0 : int64_t(seconds) * 1000 + ms;
}
std::vector<LogLine> readLog(const fs::path& path, int64_t startedUtcMs) {
    std::vector<LogLine> out;
    std::ifstream in(path, std::ios::binary);
    std::string text;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        // 2026-11-27T09:03:16.914Z t=32468 [INFO ] [tag] key=value …
        const auto tagOpen = text.find("] [");
        if (text.size() < 30 || tagOpen == std::string::npos) continue;
        const auto tagClose = text.find(']', tagOpen + 3);
        if (tagClose == std::string::npos) continue;
        LogLine l;
        l.utcMs = parseIsoUtcMs(text);
        if (startedUtcMs && l.utcMs) l.t = double(l.utcMs - startedUtcMs);
        const auto levelAt = text.find(" [");
        if (levelAt != std::string::npos && levelAt + 2 < text.size()) l.level = text[levelAt + 2];
        l.tag = text.substr(tagOpen + 3, tagClose - tagOpen - 3);
        l.text = text.substr(std::min(text.size(), tagClose + 2));
        // key=value tokens up to the first parenthesised legend.
        std::string_view body(l.text);
        const auto legend = body.find(" (");
        if (legend != std::string_view::npos) body = body.substr(0, legend);
        size_t start = 0;
        while (start < body.size()) {
            const auto space = body.find(' ', start);
            const auto token = body.substr(start, space == std::string_view::npos ? std::string_view::npos : space - start);
            const auto eq = token.find('=');
            if (eq != std::string_view::npos && eq > 0) l.kv[std::string(token.substr(0, eq))] = std::string(token.substr(eq + 1));
            if (space == std::string_view::npos) break;
            start = space + 1;
        }
        out.push_back(std::move(l));
    }
    return out;
}

// ---------------------------------------------------------------- analysis state
struct Inputs {
    fs::path folder, traceFile, telemetryFile, eventsFile, snapshotFile, logFile, logHeadFile;
    bool logOnly = false;
};

Inputs locate(const fs::path& input) {
    Inputs in;
    std::error_code ec;
    if (fs::is_directory(input, ec)) {
        in.folder = input;
        const fs::path raw = fs::exists(input / "raw") ? input / "raw" : input;
        for (const char* name : {"trace.csv", "trace.csv.partial"}) if (fs::exists(raw / name)) { in.traceFile = raw / name; break; }
        if (fs::exists(raw / "telemetry.csv")) in.telemetryFile = raw / "telemetry.csv";
        if (fs::exists(raw / "events.json")) in.eventsFile = raw / "events.json";
        if (fs::exists(raw / "snapshot.json")) in.snapshotFile = raw / "snapshot.json";
        if (fs::exists(raw / "veyra-log.txt")) in.logFile = raw / "veyra-log.txt";
        if (fs::exists(raw / "veyra-log-head.txt")) in.logHeadFile = raw / "veyra-log-head.txt";
    } else {
        in.folder = input.parent_path();
        const auto ext = input.extension().string();
        if (ext == ".csv" || ext == ".partial") in.traceFile = input;
        else { in.logFile = input; in.logOnly = true; }
    }
    if (in.traceFile.empty() && !in.logFile.empty()) in.logOnly = true;
    return in;
}

const char* resetReasonName(uint32_t reason) {
    static const char* names[] = {"None", "Open", "Seek", "SceneCut", "FrameDrop", "Resize", "SourceSwitch", "PauseResume", "DeviceLost",
                                  "Settings", "PtsDiscontinuity", "CadenceBreak"};
    return reason < std::size(names) ? names[reason] : "Other";
}

struct Finding {
    std::string id, severity, title, advice, evidence, developer;
    double score = 0;
    Object params;
};

struct Context {
    Options options;
    Inputs in;
    Trace trace;
    Telemetry telemetry;
    std::vector<LogLine> log, logHead;
    Value snapshot, events;
    double durationMs = 0;
    // derived
    double targetMs = 0, sourceIntervalMs = 0;
    std::vector<FrameRow> frames;
    std::vector<double> resetTimes, markTimes, hitchOnsets;
    std::vector<int> markNumbers;
    Object report;
    std::vector<Finding> findings;
    std::vector<Object> eventsOut;
};

double ms(const Context& c, int64_t host) { return host > 0 ? double(host - c.trace.started) / 10000.0 : NAN; }

// ---------------------------------------------------------------- frames & hitches
struct HitchEvent {
    double start = 0, end = 0, worst = 0, excess = 0;
    int count = 0;
    std::array<double, CauseCount> causeMs{};
    std::map<std::string, double> stageMs;
    uint64_t revision = 0;
    std::vector<int> marks;
    std::vector<std::string> tags;
};

void buildFrames(Context& c) {
    std::vector<const Ev*> presents;
    for (const auto& e : c.trace.events) if (e.kind == K::Present) presents.push_back(&e);
    const auto keyOf = [](const Ev* e) { return e->presentCallHost > 0 ? e->presentCallHost : e->presentEndHost > 0 ? e->presentEndHost : e->host; };
    std::stable_sort(presents.begin(), presents.end(), [&](const Ev* a, const Ev* b) { return keyOf(a) < keyOf(b); });
    // Session starts and resets bound the steady state.
    std::map<uint64_t, double> sessionStart;
    for (const auto& e : c.trace.events) {
        if (e.kind == K::Reset) c.resetTimes.push_back(ms(c, e.host));
        if (e.kind == K::Mark) { c.markTimes.push_back(ms(c, e.host)); c.markNumbers.push_back(int(e.detail)); }
    }
    const double warmup = c.options.warmupSeconds * 1000.0;
    const Ev* previous = nullptr;
    for (const Ev* e : presents) {
        FrameRow f;
        f.t = ms(c, keyOf(e));
        f.kind = e->frameKind;
        f.session = e->session; f.revision = e->revision; f.source = e->source; f.subframe = e->detail;
        if (e->presentBeginHost > 0 && e->presentEndHost >= e->presentBeginHost) f.callMs = double(e->presentEndHost - e->presentBeginHost) / 10000.0;
        if (e->arrivalHost > 0 && e->processHost >= e->arrivalHost) f.waitInMs = double(e->processHost - e->arrivalHost) / 10000.0;
        if (e->processHost > 0 && e->readyHost >= e->processHost) f.gpuMs = double(e->readyHost - e->processHost) / 10000.0;
        if (e->readyHost > 0 && e->presentBeginHost >= e->readyHost) f.pacingMs = double(e->presentBeginHost - e->readyHost) / 10000.0;
        if (e->deadlineHost > 0 && e->presentBeginHost > 0) f.lateMs = double(e->presentBeginHost - e->deadlineHost) / 10000.0;
        if (e->arrivalHost > 0 && keyOf(e) > e->arrivalHost) f.latencyMs = double(keyOf(e) - e->arrivalHost) / 10000.0;
        if (!sessionStart.count(e->session)) sessionStart[e->session] = f.t;
        if (previous && previous->session == e->session) f.dt = f.t - c.frames.back().t;
        f.steady = f.t - sessionStart[e->session] >= warmup;
        for (double r : c.resetTimes) if (f.t >= r && f.t - r < warmup) { f.steady = false; break; }
        c.frames.push_back(f);
        previous = e;
    }
}

// The expected interval: the median per settings revision (output rate can
// change with the settings), or the override.
std::map<uint64_t, double> expectedIntervals(Context& c) {
    std::map<uint64_t, std::vector<double>> byRevision;
    std::vector<double> all;
    for (const auto& f : c.frames) if (f.dt > 0 && f.steady) { byRevision[f.revision].push_back(f.dt); all.push_back(f.dt); }
    std::map<uint64_t, double> out;
    const double global = c.options.targetFps > 0 ? 1000.0 / c.options.targetFps : median(all);
    c.targetMs = global;
    for (auto& [revision, values] : byRevision) out[revision] = c.options.targetFps > 0 || values.size() < 60 ? global : median(values);
    return out;
}

double windowMax(const std::vector<std::pair<double, double>>& series, double from, double to) {
    double best = 0;
    auto it = std::lower_bound(series.begin(), series.end(), std::make_pair(from, -1e300));
    for (; it != series.end() && it->first <= to; ++it) best = std::max(best, it->second);
    return best;
}

void attribute(Context& c, std::vector<HitchEvent>& out) {
    const auto expected = expectedIntervals(c);
    // Baselines over steady frames.
    std::vector<double> calls, waits, gpus, pacings;
    for (const auto& f : c.frames) {
        if (!f.steady) continue;
        if (f.callMs >= 0) calls.push_back(f.callMs);
        if (f.waitInMs >= 0) waits.push_back(f.waitInMs);
        if (f.gpuMs >= 0) gpus.push_back(f.gpuMs);
        if (f.pacingMs >= 0) pacings.push_back(f.pacingMs);
    }
    const double callBase = median(calls), waitBase = median(waits), gpuBase = median(gpus), pacingBase = median(pacings);
    // Source arrival gaps and discards on the timeline.
    std::vector<std::pair<double, double>> arrivalExcess, discards;
    std::vector<double> arrivals;
    std::vector<double> sourceIntervals;
    int64_t lastArrival = 0;
    uint64_t lastSession = 0;
    for (const auto& e : c.trace.events) {
        if (e.kind == K::Source) {
            const int64_t a = e.arrivalHost > 0 ? e.arrivalHost : e.decodedHost > 0 ? e.decodedHost : e.host;
            if (lastArrival && e.session == lastSession && a > lastArrival) sourceIntervals.push_back(double(a - lastArrival) / 10000.0);
            lastArrival = a; lastSession = e.session;
            arrivals.push_back(ms(c, a));
        }
        if (e.kind == K::Discarded) discards.emplace_back(ms(c, e.host), 1.0);
        // Submitted.detail = frame-generation candidates skipped before evaluation
        // (budget rejection or a reduced multiplier): those generated frames never exist.
        if (e.kind == K::Submitted && e.detail > 0) discards.emplace_back(ms(c, e.host), 2.0);
    }
    c.sourceIntervalMs = median(sourceIntervals);
    for (size_t i = 1; i < arrivals.size(); ++i)
        arrivalExcess.emplace_back(arrivals[i], std::max(0.0, arrivals[i] - arrivals[i - 1] - c.sourceIntervalMs));
    std::sort(arrivalExcess.begin(), arrivalExcess.end());
    // Per-stage GPU baselines for the stage named in a GPU hitch.
    std::map<uint16_t, std::vector<double>> gpuStage;
    std::vector<const Ev*> gpuEvents, cpuEvents;
    for (const auto& e : c.trace.events) {
        if (e.kind == K::Gpu) { gpuStage[e.stage].push_back(e.ms); gpuEvents.push_back(&e); }
        if (e.kind == K::Cpu) cpuEvents.push_back(&e);
    }
    std::map<uint16_t, double> gpuStageBase;
    for (auto& [stage, values] : gpuStage) gpuStageBase[stage] = median(values);
    // Waits are not CPU work: the scheduler's idle wait (controlAndSchedule),
    // GPU readiness (ReadyWait) and pacing (DeadlineWait, DisplayCapacityWait).
    static const std::set<std::string> waitStages = {"controlAndSchedule", "ReadyWait", "DeadlineWait", "DisplayCapacityWait"};
    cpuEvents.erase(std::remove_if(cpuEvents.begin(), cpuEvents.end(), [&](const Ev* e) { return waitStages.count(c.trace.stages[e->stage]) > 0; }),
                    cpuEvents.end());
    std::map<uint16_t, std::vector<double>> cpuStage;
    for (const Ev* e : cpuEvents) cpuStage[e->stage].push_back(e->ms);
    std::map<uint16_t, double> cpuStageBase;
    for (auto& [stage, values] : cpuStage) cpuStageBase[stage] = median(values);

    const auto stageExcessNear = [&](const std::vector<const Ev*>& list, const std::map<uint16_t, double>& base, double from, double to) {
        std::pair<std::string, double> best{"", 0};
        const int64_t a = c.trace.started + int64_t(from * 10000.0), b = c.trace.started + int64_t(to * 10000.0);
        auto it = std::lower_bound(list.begin(), list.end(), a, [](const Ev* e, int64_t h) { return e->host < h; });
        for (; it != list.end() && (*it)->host <= b; ++it) {
            const double excess = (*it)->ms - base.at((*it)->stage);
            if (excess > best.second) best = {c.trace.stages[(*it)->stage], excess};
        }
        return best;
    };

    HitchEvent current;
    bool open = false;
    const auto close = [&] {
        if (open) out.push_back(current);
        open = false;
        current = HitchEvent{};
    };
    for (size_t i = 1; i < c.frames.size(); ++i) {
        auto& f = c.frames[i];
        if (f.dt <= 0) continue;
        const auto it = expected.find(f.revision);
        const double target = it != expected.end() ? it->second : c.targetMs;
        f.target = target;
        if (!(target > 0)) continue;
        const double limit = std::max(1.5 * target, target + 4.0);
        if (f.dt <= limit) continue;
        f.hitch = true;
        const double excess = f.dt - target;
        const auto& p = c.frames[i - 1];
        // Candidate explanations, in ms of extra time.
        std::array<double, CauseCount> share{};
        for (double r : c.resetTimes) if (r > p.t - 300 && r <= f.t + 1) share[CauseReset] = excess;
        if (std::any_of(discards.begin(), discards.end(), [&](const auto& d) { return d.second == 1.0 && d.first > p.t && d.first < f.t; }))
            share[CauseDiscard] = std::max(share[CauseDiscard], std::min(excess, target));
        // A pair skipped before evaluation explains the gap by itself.
        const bool skipped = std::any_of(discards.begin(), discards.end(), [&](const auto& d) {
            return d.second == 2.0 && d.first > p.t - 2 * target - 40 && d.first <= f.t; });
        share[CauseInput] = std::min(excess, windowMax(arrivalExcess, f.t - 2 * target - 20, f.t));
        if (f.waitInMs >= 0) share[CauseEngine] = std::max(0.0, f.waitInMs - waitBase);
        if (f.gpuMs >= 0) share[CauseGpu] = std::max(0.0, f.gpuMs - gpuBase);
        if (f.callMs >= 0) share[CausePresent] = std::max(0.0, f.callMs - callBase);
        if (p.callMs >= 0) share[CausePresent] = std::max(share[CausePresent], p.callMs - callBase);
        if (f.pacingMs >= 0) share[CausePacing] = std::max(0.0, f.pacingMs - pacingBase);
        int cause = CauseUnknown;
        double best = 0.3 * excess;
        // Reset and discard explain the gap by themselves; the timed stages
        // compete by how much of the extra time they account for.
        if (share[CauseReset] > 0) cause = CauseReset;
        else if (skipped) cause = CauseDiscard;
        else {
            for (int k : {CauseInput, CauseEngine, CauseGpu, CausePresent, CausePacing})
                if (share[size_t(k)] > best) { best = share[size_t(k)]; cause = k; }
            if (cause == CauseUnknown && share[CauseDiscard] > 0) cause = CauseDiscard;
        }
        f.cause = int16_t(cause);
        if (cause == CauseGpu) f.stage = stageExcessNear(gpuEvents, gpuStageBase, f.t - f.gpuMs - 5, f.t + 50).first;
        if (cause == CauseEngine) f.stage = stageExcessNear(cpuEvents, cpuStageBase, f.t - f.waitInMs - target - 5, f.t).first;
        if (open && (f.t - current.end > 1000 || f.t - current.start > 2000)) close();
        // Onsets: the first hitch after a quiet second; the period test uses these.
        if (!open || f.t - current.end > 1000) c.hitchOnsets.push_back(f.t);
        if (!open) { open = true; current.start = p.t; current.revision = f.revision; }
        current.end = f.t;
        current.worst = std::max(current.worst, f.dt);
        current.excess += excess;
        ++current.count;
        current.causeMs[size_t(cause)] += excess;
        if (!f.stage.empty()) current.stageMs[f.stage] += excess;
        f.event = int(out.size());
    }
    close();
}

// ---------------------------------------------------------------- metrics
Value outputMetrics(Context& c, Object& summary) {
    Object o;
    std::vector<double> steady, all;
    size_t real = 0, generated = 0;
    for (const auto& f : c.frames) {
        if (f.dt > 0) { all.push_back(f.dt); if (f.steady) steady.push_back(f.dt); }
        if (f.kind == 1) ++real; else if (f.kind == 2) ++generated;
    }
    const Dist d = distOf(steady);
    o["intervalMs"] = distJson(d);
    o["intervalMsAll"] = distJson(distOf(all));
    o["presents"] = c.frames.size();
    o["realPresents"] = real;
    o["generatedPresents"] = generated;
    double span = c.frames.size() > 1 ? c.frames.back().t - c.frames.front().t : 0;
    const double fps = d.n && d.mean > 0 ? 1000.0 / d.mean : 0;
    o["averageFps"] = round3(fps);
    // Both readings of "1% low": the P99 interval (what Veyra's interface and
    // RTSS show) and the average of the slowest 1% of frames.
    std::vector<double> sorted = steady;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());
    const auto slowAvg = [&](double share) {
        const size_t n = std::max<size_t>(1, size_t(std::ceil(double(sorted.size()) * share)));
        if (sorted.empty()) return 0.0;
        double sum = 0;
        for (size_t i = 0; i < std::min(n, sorted.size()); ++i) sum += sorted[i];
        return 1000.0 / (sum / double(std::min(n, sorted.size())));
    };
    o["low1FpsP99"] = round3(d.p99 > 0 ? 1000.0 / d.p99 : 0);
    o["low1FpsSlowestAverage"] = round3(slowAvg(0.01));
    o["low01FpsP999"] = round3(d.p999 > 0 ? 1000.0 / d.p999 : 0);
    o["low01FpsSlowestAverage"] = round3(slowAvg(0.001));
    o["expectedIntervalMs"] = round3(c.targetMs);
    o["expectedFps"] = round3(c.targetMs > 0 ? 1000.0 / c.targetMs : 0);
    // Hitches by how far they overran the expected interval.
    size_t h15 = 0, h2 = 0, h3 = 0, h50 = 0, h100 = 0, hitches = 0;
    const auto targetOf = [&](const FrameRow& f) { return f.target > 0 ? f.target : c.targetMs; };
    for (const auto& f : c.frames) {
        const double target = targetOf(f);
        if (f.dt <= 0 || target <= 0) continue;
        if (f.hitch) ++hitches;
        if (f.dt > 1.5 * target) ++h15;
        if (f.dt > 2 * target) ++h2;
        if (f.dt > 3 * target) ++h3;
        if (f.dt > 50) ++h50;
        if (f.dt > 100) ++h100;
    }
    Object h;
    h["total"] = hitches; h["over1_5x"] = h15; h["over2x"] = h2; h["over3x"] = h3; h["over50ms"] = h50; h["over100ms"] = h100;
    h["perMinute"] = round3(span > 0 ? double(hitches) / (span / 60000.0) : 0);
    o["hitches"] = h;
    // Pacing regularity: share of intervals more than 25% away from expected.
    size_t irregular = 0;
    for (const auto& f : c.frames) {
        const double target = targetOf(f);
        if (f.steady && f.dt > 0 && target > 0 && std::abs(f.dt - target) > 0.25 * target) ++irregular;
    }
    o["irregularShare"] = round3(steady.empty() ? 0 : double(irregular) / double(steady.size()));
    o["spanSeconds"] = round3(span / 1000.0);
    summary["averageFps"] = o["averageFps"];
    summary["low1FpsP99"] = o["low1FpsP99"];
    summary["low1FpsSlowestAverage"] = o["low1FpsSlowestAverage"];
    summary["expectedFps"] = o["expectedFps"];
    summary["hitches"] = hitches;
    summary["hitchesPerMinute"] = h["perMinute"];
    summary["worstIntervalMs"] = round3(distOf(all).max);
    return o;
}

// Settings change mid-recording (another multiplier, another tier): each
// revision gets its own rate, 1% low and hitch count.
Value revisionMetrics(Context& c) {
    std::map<uint64_t, std::vector<const FrameRow*>> byRevision;
    for (const auto& f : c.frames) byRevision[f.revision].push_back(&f);
    Array out;
    for (const auto& [revision, frames] : byRevision) {
        std::vector<double> steady;
        size_t real = 0, generated = 0, hitches = 0;
        double target = 0;
        for (const auto* f : frames) {
            if (f->steady && f->dt > 0) steady.push_back(f->dt);
            if (f->kind == 1) ++real; else if (f->kind == 2) ++generated;
            if (f->hitch) ++hitches;
            if (f->target > 0) target = f->target;
        }
        if (frames.size() < 2) continue;
        const Dist d = distOf(steady);
        std::vector<double> sorted = steady;
        std::sort(sorted.begin(), sorted.end(), std::greater<double>());
        const size_t n = std::min(sorted.size(), std::max<size_t>(1, sorted.size() / 100));
        const double slow = n ? std::accumulate(sorted.begin(), sorted.begin() + long(n), 0.0) / double(n) : 0;
        Object o;
        o["revision"] = revision;
        o["fromMs"] = round3(frames.front()->t);
        o["toMs"] = round3(frames.back()->t);
        o["presents"] = frames.size();
        o["averageFps"] = round3(d.mean > 0 ? 1000.0 / d.mean : 0);
        o["low1FpsP99"] = round3(d.p99 > 0 ? 1000.0 / d.p99 : 0);
        o["low1FpsSlowestAverage"] = round3(slow > 0 ? 1000.0 / slow : 0);
        o["expectedFps"] = round3(target > 0 ? 1000.0 / target : 0);
        o["observedMultiplier"] = round3(real ? double(real + generated) / double(real) : 0);
        o["hitches"] = hitches;
        out.push_back(Value(std::move(o)));
    }
    return out;
}

Value latencyMetrics(Context& c) {
    Object o;
    std::vector<double> latency, late, calls, waits, gpus, pacing;
    size_t lateCount = 0, withDeadline = 0;
    for (const auto& f : c.frames) {
        if (!f.steady) continue;
        if (f.kind == 1 && f.latencyMs >= 0) latency.push_back(f.latencyMs);
        if (std::isfinite(f.lateMs)) { ++withDeadline; late.push_back(f.lateMs); if (f.lateMs > 0.5) ++lateCount; }
        if (f.callMs >= 0) calls.push_back(f.callMs);
        if (f.waitInMs >= 0) waits.push_back(f.waitInMs);
        if (f.gpuMs >= 0) gpus.push_back(f.gpuMs);
        if (f.pacingMs >= 0) pacing.push_back(f.pacingMs);
    }
    o["arrivalToPresentMs"] = distJson(distOf(latency));
    o["presentCallMs"] = distJson(distOf(calls));
    o["arrivalToProcessMs"] = distJson(distOf(waits));
    o["processToReadyMs"] = distJson(distOf(gpus));
    o["readyToPresentMs"] = distJson(distOf(pacing));
    o["lateVsDeadlineMs"] = distJson(distOf(late));
    o["lateShare"] = round3(withDeadline ? double(lateCount) / double(withDeadline) : 0);
    return o;
}

Value sourceMetrics(Context& c, Object& summary) {
    Object o;
    std::map<std::string, size_t> skipped;
    std::vector<double> decode;
    std::string kindName;
    std::vector<const Ev*> sources;
    for (const auto& e : c.trace.events) {
        if (e.kind == K::Source) { sources.push_back(&e); decode.push_back(e.ms); kindName = c.trace.stages[e.stage]; }
        if (e.kind == K::SourceSkipped) ++skipped[c.trace.stages[e.stage]];
    }
    o["kind"] = kindName;
    o["accepted"] = sources.size();
    Object skip;
    for (const auto& [reason, n] : skipped) skip[reason] = n;
    o["skipped"] = skip;
    o["decodeMs"] = distJson(distOf(decode));
    // Arrival cadence (capture callback times; decode times for files).
    std::vector<double> intervals;
    std::vector<std::pair<double, double>> drift;   // (t, arrival - pts) in ms
    int64_t lastArrival = 0;
    uint64_t lastSession = 0;
    for (const Ev* e : sources) {
        const int64_t a = e->arrivalHost > 0 ? e->arrivalHost : e->host;
        if (lastArrival && e->session == lastSession && a > lastArrival) intervals.push_back(double(a - lastArrival) / 10000.0);
        lastArrival = a; lastSession = e->session;
        if (e->arrivalHost > 0 && e->pts > 0) drift.emplace_back(ms(c, a), double(a - e->pts) / 10000.0);
    }
    const Dist d = distOf(intervals);
    o["arrivalIntervalMs"] = distJson(d);
    o["arrivalFps"] = round3(d.p50 > 0 ? 1000.0 / d.p50 : 0);
    // Clock drift (capture only): change of (arrival − pts) between the first
    // and the last tenth of the recording, from medians so single late
    // callbacks do not count. Only when the device timestamps advance like the
    // arrivals (no seek, reset or format change in between).
    if (kindName == "CaptureCard" && drift.size() > 600) {
        size_t consistent = 0;
        for (size_t i = 1; i < drift.size(); ++i) {
            const double dArrival = drift[i].first - drift[i - 1].first;
            const double dPts = dArrival - (drift[i].second - drift[i - 1].second);
            if (dArrival > 0 && std::abs(dPts - dArrival) < 0.5 * dArrival) ++consistent;
        }
        const size_t tenth = drift.size() / 10;
        std::vector<double> head, tail;
        for (size_t i = 0; i < tenth; ++i) head.push_back(drift[i].second);
        for (size_t i = drift.size() - tenth; i < drift.size(); ++i) tail.push_back(drift[i].second);
        const double span = drift[drift.size() - tenth / 2].first - drift[tenth / 2].first;
        if (double(consistent) > 0.98 * double(drift.size() - 1) && span > 60000)
            o["clockDriftPpm"] = round1((median(tail) - median(head)) / span * 1e6);
    }
    summary["sourceKind"] = kindName;
    summary["sourceFps"] = o["arrivalFps"];
    return o;
}

Value stageMetrics(Context& c) {
    Object o, gpu, cpu;
    std::map<uint16_t, std::vector<double>> g, k;
    size_t unavailable = 0;
    for (const auto& e : c.trace.events) {
        if (e.kind == K::Gpu) g[e.stage].push_back(e.ms);
        if (e.kind == K::Cpu) k[e.stage].push_back(e.ms);
        if (e.kind == K::GpuUnavailable) ++unavailable;
    }
    for (auto& [stage, values] : g) gpu[c.trace.stages[stage].empty() ? "?" : c.trace.stages[stage]] = distJson(distOf(values));
    for (auto& [stage, values] : k) cpu[c.trace.stages[stage].empty() ? "?" : c.trace.stages[stage]] = distJson(distOf(values));
    o["gpuMs"] = gpu;
    o["cpuMs"] = cpu;
    o["gpuUnavailable"] = unavailable;
    return o;
}

Value fgMetrics(Context& c) {
    Object o;
    std::map<std::string, size_t> discarded;
    size_t discardedGenerated = 0, evaluated = 0, skippedBeforeEval = 0, cancelled = 0;
    for (const auto& e : c.trace.events) {
        if (e.kind == K::Discarded) { ++discarded[c.trace.stages[e.stage]]; if (e.frameKind == 2) ++discardedGenerated; }
        if (e.kind == K::Submitted) { evaluated += e.count; skippedBeforeEval += e.detail; }
        if (e.kind == K::Cancelled) ++cancelled;
    }
    Object d;
    for (const auto& [reason, n] : discarded) d[reason.empty() ? "?" : reason] = n;
    o["discarded"] = d;
    o["discardedGenerated"] = discardedGenerated;
    o["evaluatedPairs"] = evaluated;
    o["skippedBeforeEvaluation"] = skippedBeforeEval;
    o["cancelledBatches"] = cancelled;
    const double generated = c.report["output"]["generatedPresents"].number();
    o["discardedGeneratedShare"] = round3(generated + double(discardedGenerated) > 0 ? double(discardedGenerated) / (generated + double(discardedGenerated)) : 0);
    o["skippedShare"] = round3(evaluated + skippedBeforeEval > 0 ? double(skippedBeforeEval) / double(evaluated + skippedBeforeEval) : 0);
    const double real = c.report["output"]["realPresents"].number();
    o["observedMultiplier"] = round3(real > 0 ? (real + generated) / real : 0);
    return o;
}

// Per-second series: output fps, worst interval, hitches, the interface-style
// 1% low (P99 of the last 10 s), the slowest-1% average over the same window
// and the telemetry sample of that second.
Value perSecond(Context& c, Object& summary) {
    Array series;
    if (c.frames.empty() && c.telemetry.rows.empty()) return series;
    const double end = std::max(c.durationMs, c.frames.empty() ? 0.0 : c.frames.back().t);
    // Whole seconds only: a trailing partial second would read as a drop.
    const size_t seconds = std::max<size_t>(1, size_t(std::floor(end / 1000.0)));
    std::vector<std::vector<double>> intervals(seconds);
    std::vector<int> hitches(seconds), real(seconds), gen(seconds);
    for (const auto& f : c.frames) {
        if (f.t < 0) continue;
        const size_t s = size_t(f.t / 1000.0);
        if (s >= seconds) continue;
        if (f.dt > 0) intervals[s].push_back(f.dt);
        if (f.hitch) ++hitches[s];
        if (f.kind == 1) ++real[s]; else if (f.kind == 2) ++gen[s];
    }
    std::map<size_t, size_t> telemetryRow;
    for (size_t r = 0; r < c.telemetry.rows.size(); ++r) {
        const double host = c.telemetry.get(r, "host");
        if (!std::isfinite(host)) continue;
        const double t = (host - double(c.trace.started)) / 10000.0;
        if (t >= 0) telemetryRow[size_t(t / 1000.0)] = r;
    }
    std::vector<double> lowP99, lowAvg;
    for (size_t s = 0; s < seconds; ++s) {
        Object o;
        o["s"] = s;
        o["presents"] = intervals[s].size();
        o["real"] = real[s]; o["generated"] = gen[s];
        o["hitches"] = hitches[s];
        o["maxIntervalMs"] = round3(intervals[s].empty() ? 0 : *std::max_element(intervals[s].begin(), intervals[s].end()));
        std::vector<double> window;
        for (size_t w = s >= 9 ? s - 9 : 0; w <= s; ++w) window.insert(window.end(), intervals[w].begin(), intervals[w].end());
        if (window.size() >= 30) {
            const Dist d = distOf(window);
            std::sort(window.begin(), window.end(), std::greater<double>());
            const size_t n = std::max<size_t>(1, window.size() / 100);
            const double avg = std::accumulate(window.begin(), window.begin() + long(n), 0.0) / double(n);
            o["low1P99Fps"] = round1(1000.0 / d.p99);
            o["low1AvgFps"] = round1(1000.0 / avg);
            if (s >= 10) { lowP99.push_back(1000.0 / d.p99); lowAvg.push_back(1000.0 / avg); }
        }
        if (const auto it = telemetryRow.find(s); it != telemetryRow.end()) {
            const size_t r = it->second;
            for (const char* column : {"cpu_total", "cpu_veyra", "gpu_busy_max", "gpu_veyra_max", "gpu_others_max", "gpu_top_other_util",
                                       "nv_clock_mhz", "nv_power_w", "nv_temp_c", "nv_throttle", "gpu_veyra_dedicated_mib", "ui_gap_ms",
                                       "submit_fps", "low1_fps", "capture_fps"}) {
                const double v = c.telemetry.get(r, column);
                if (std::isfinite(v)) o[column] = round1(v);
            }
            const std::string name = c.telemetry.text(r, "gpu_top_other_name");
            if (!name.empty()) o["gpu_top_other_name"] = name;
        }
        series.push_back(Value(std::move(o)));
    }
    // R17 evidence: how much the interface-style 1% low swings compared with
    // the slowest-1% average over the same windows.
    if (lowP99.size() >= 5) {
        const auto range = [](const std::vector<double>& v) {
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            return *hi > 0 ? (*hi - *lo) / *hi : 0.0;
        };
        summary["low1P99Swing"] = round3(range(lowP99));
        summary["low1AvgSwing"] = round3(range(lowAvg));
    }
    return series;
}

Value telemetryMetrics(Context& c) {
    Object o;
    const auto& t = c.telemetry;
    if (t.rows.empty()) return o;
    o["rows"] = t.rows.size();
    for (const char* column : {"cpu_total", "cpu_veyra", "gpu_busy_max", "gpu_veyra_max", "gpu_others_max", "nv_util", "nv_clock_mhz",
                               "nv_mem_clock_mhz", "nv_temp_c", "nv_power_w", "nv_power_limit_w", "gpu_veyra_dedicated_mib",
                               "gpu_adapter_dedicated_mib", "nv_mem_used_mib", "ui_gap_ms", "submit_fps", "low1_fps", "capture_fps",
                               "veyra_private_mib"}) {
        std::vector<double> v;
        for (size_t r = 0; r < t.rows.size(); ++r) { const double x = t.get(r, column); if (std::isfinite(x)) v.push_back(x); }
        if (!v.empty()) o[column] = distJson(distOf(v));
    }
    // The other processes that used the GPU most.
    std::map<std::string, std::pair<int, double>> others;
    for (size_t r = 0; r < t.rows.size(); ++r) {
        const auto name = t.text(r, "gpu_top_other_name");
        const double util = t.get(r, "gpu_top_other_util", 0);
        if (name.empty() || util < 5) continue;
        auto& entry = others[name];
        ++entry.first;
        entry.second = std::max(entry.second, util);
    }
    Array top;
    std::vector<std::pair<std::string, std::pair<int, double>>> list(others.begin(), others.end());
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    for (size_t i = 0; i < std::min<size_t>(5, list.size()); ++i)
        top.push_back(Object{{{"process", list[i].first}, {"seconds", list[i].second.first}, {"peakPercent", round1(list[i].second.second)}}});
    o["topOtherGpuProcesses"] = top;
    // NVIDIA clock-event reasons seen (bit -> seconds).
    std::map<int, int> reasons;
    for (size_t r = 0; r < t.rows.size(); ++r) {
        const double v = t.get(r, "nv_throttle");
        if (!std::isfinite(v)) continue;
        const auto bits = uint64_t(v);
        for (int b = 0; b < 12; ++b) if (bits & (1ull << b)) ++reasons[b];
    }
    static const char* reasonNames[] = {"GpuIdle", "ApplicationsClocksSetting", "SwPowerCap", "HwSlowdown", "SyncBoost", "SwThermalSlowdown",
                                        "HwThermalSlowdown", "HwPowerBrakeSlowdown", "DisplayClockSetting", "undocumented-bit9",
                                        "undocumented-bit10", "undocumented-bit11"};
    Object r;
    for (const auto& [bit, seconds] : reasons) r[reasonNames[bit]] = seconds;
    o["nvidiaClockReasonsSeconds"] = r;
    return o;
}

// Display side from the per-second log lines (DXGI frame statistics; no admin).
Value displayMetrics(Context& c) {
    Object o;
    std::vector<double> scanP50, scanP95, refresh, presentCalls, dxgiPresents, refreshes, replaced;
    std::map<int, int> composition;
    int vrrActive = 0, vrrLines = 0;
    for (const auto& l : c.log) {
        if (l.tag == "display-latency") {
            const double p50 = l.d("presentToScanoutP50Ms"), p95 = l.d("presentToScanoutP95Ms"), samples = l.d("samples", 0);
            if (samples > 0) { scanP50.push_back(p50); scanP95.push_back(p95); }
            replaced.push_back(l.d("replacedAtLeast", 0));
            ++composition[int(l.d("compositionMode", -1))];
            const double hz = l.d("refreshHz");
            if (std::isfinite(hz)) refresh.push_back(hz);
            ++vrrLines;
            if (l.s("vrrActive") == "true") ++vrrActive;
        } else if (l.tag == "display-stats") {
            presentCalls.push_back(l.d("presentCalls", 0));
            dxgiPresents.push_back(l.d("dxgiPresentCount", 0));
            refreshes.push_back(l.d("refreshes", 0));
        }
    }
    o["presentToScanoutP50Ms"] = distJson(distOf(scanP50));
    o["presentToScanoutP95Ms"] = distJson(distOf(scanP95));
    o["refreshHz"] = round3(median(refresh));
    Object comp;
    for (const auto& [mode, n] : composition) {
        static const char* names[] = {"composed", "overlay", "none", "failure"};
        comp[mode >= 0 && mode <= 3 ? names[mode] : "unknown"] = n;
    }
    o["compositionModeSeconds"] = comp;
    o["vrrActiveSeconds"] = vrrActive;
    o["vrrSampledSeconds"] = vrrLines;
    o["presentCallsPerSecond"] = distJson(distOf(presentCalls));
    o["dxgiPresentsPerSecond"] = distJson(distOf(dxgiPresents));
    o["refreshesPerSecond"] = distJson(distOf(refreshes));
    double replacedTotal = std::accumulate(replaced.begin(), replaced.end(), 0.0);
    o["replacedAtLeast"] = replacedTotal;
    return o;
}

Value logMetrics(Context& c) {
    Object o;
    std::map<std::string, int> tags;
    int errors = 0, warnings = 0;
    Array firstErrors;
    std::vector<double> stalls, vram, budget, fgBatch, gpuReady;
    std::map<std::string, int> gpuStalls;
    double admitted = 0, rejected = 0;
    double fgCandidates = 0, fgValid = 0, fgInvalid = 0;
    int captureLost = 0, captureReconnect = 0;
    std::vector<double> driverAge;
    for (const auto& l : c.log) {
        ++tags[l.tag];
        if (l.level == 'E') { ++errors; if (firstErrors.size() < 12) firstErrors.push_back("[" + l.tag + "] " + l.text.substr(0, 240)); }
        if (l.level == 'W') ++warnings;
        if (l.tag == "engine-stall") stalls.push_back(l.d("totalMs", 0));
        if (l.tag == "gpu-stage-stall") ++gpuStalls[l.s("stage")];
        if (l.tag == "player-timing") {
            if (const double v = l.d("vramMiB"); std::isfinite(v)) vram.push_back(v);
            if (const double v = l.d("budgetMiB"); std::isfinite(v)) budget.push_back(v);
            if (const double v = l.d("gpuFgBatchP95Ms"); std::isfinite(v) && v > 0) fgBatch.push_back(v);
            if (const double v = l.d("gpuReadyP95Ms"); std::isfinite(v) && v > 0) gpuReady.push_back(v);
        }
        if (l.tag == "live-fg-admission") { admitted = std::max(admitted, l.d("admittedPairs", 0)); rejected = std::max(rejected, l.d("rejectedPairs", 0)); }
        if (l.tag == "fg-validity") { fgCandidates += l.d("candidate", 0); fgValid += l.d("valid", 0); fgInvalid += l.d("invalid", 0); }
        if (l.text.find("EC_DEVICE_LOST") != std::string::npos) ++captureLost;
        if (l.tag.rfind("capture", 0) == 0 && l.text.find("reconnect") != std::string::npos) ++captureReconnect;
        if (l.tag == "capture-driver-age") { const double v = l.d("ageMs", l.d("driverAgeMs")); if (std::isfinite(v)) driverAge.push_back(v); }
    }
    o["lines"] = c.log.size();
    o["errors"] = errors;
    o["warnings"] = warnings;
    o["firstErrors"] = firstErrors;
    o["engineStalls"] = distJson(distOf(stalls));
    Object gs;
    for (const auto& [stage, n] : gpuStalls) gs[stage.empty() ? "?" : stage] = n;
    o["gpuStageStalls"] = gs;
    o["vramMiB"] = distJson(distOf(vram));
    o["vramBudgetMiB"] = distJson(distOf(budget));
    o["gpuFgBatchP95Ms"] = distJson(distOf(fgBatch));
    o["gpuReadyP95Ms"] = distJson(distOf(gpuReady));
    o["fgAdmittedPairs"] = admitted;
    o["fgRejectedPairs"] = rejected;
    o["fgValidityCandidates"] = fgCandidates;
    o["fgValidityValid"] = fgValid;
    o["fgValidityInvalid"] = fgInvalid;
    o["captureDeviceLost"] = captureLost;
    o["captureReconnects"] = captureReconnect;
    o["captureDriverAgeMs"] = distJson(distOf(driverAge));
    Object top;
    std::vector<std::pair<std::string, int>> list(tags.begin(), tags.end());
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < std::min<size_t>(25, list.size()); ++i) top[list[i].first] = list[i].second;
    o["tagCounts"] = top;
    return o;
}

// ---------------------------------------------------------------- rules
bool moduleLoaded(const Context& c, std::string_view name) {
    for (const auto& m : c.snapshot["system"]["modules"]["flagged"].array())
        if (_stricmp(m["module"].string().c_str(), std::string(name).c_str()) == 0) return true;
    return false;
}
std::vector<std::string> flaggedProcesses(const Context& c, std::initializer_list<std::string_view> products) {
    std::vector<std::string> out;
    for (const auto& p : c.snapshot["system"]["processes"]["flagged"].array())
        for (auto product : products) if (p["product"].string().find(product) != std::string::npos) out.push_back(p["process"].string());
    return out;
}
std::string fmt(double v, int digits = 1) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.*f", digits, v);
    return b;
}
void add(Context& c, Finding f) { c.findings.push_back(std::move(f)); }

double causeShare(const std::vector<HitchEvent>& events, int cause) {
    double total = 0, part = 0;
    for (const auto& e : events) for (int k = 0; k < CauseCount; ++k) { total += e.causeMs[size_t(k)]; if (k == cause) part += e.causeMs[size_t(k)]; }
    return total > 0 ? part / total : 0;
}

void rules(Context& c, const std::vector<HitchEvent>& events) {
    const Value report = Value(c.report);
    const auto& out = report["output"];
    const auto& tel = report["telemetry"];
    const auto& disp = report["display"];
    const auto& lg = report["log"];
    const auto& src = report["source"];
    const auto& fg = report["frameGeneration"];
    const double fps = out["averageFps"].number();
    const double hitchesPerMin = out["hitches"]["perMinute"].number();
    const double presentShare = causeShare(events, CausePresent);
    const double gpuShare = causeShare(events, CauseGpu);
    const double inputShare = causeShare(events, CauseInput);
    const double engineShare = causeShare(events, CauseEngine);

    // R1 Smooth Motion (11-18): the driver doubles presents of this process.
    if (moduleLoaded(c, "NvPresent64.dll")) {
        const double calls = disp["presentCallsPerSecond"]["p50"].number(), dxgi = disp["dxgiPresentsPerSecond"]["p50"].number();
        Finding f{"R1", "high", "NVIDIA Smooth Motion 正作用于 Veyra", "在 NVIDIA App → 图形 → 程序设置里对 Veyra（veyra_qml_ui.exe）关闭 Smooth Motion。",
                  "驱动帧生成模块 NvPresent64.dll 已加载到 Veyra 进程" + (calls > 0 ? "；每秒 Present " + fmt(calls, 0) + " 次、DXGI 计数 " + fmt(dxgi, 0) : std::string()),
                  "Driver frame generation doubles presents; VFG High then looks like ~30 fps (field 2026-11-18).", 100};
        add(c, f);
    }
    // R2 RTSS / GamePP injection (11-21).
    for (const auto& [module, product] : {std::pair{"RTSSHooks64.dll", "RivaTuner (RTSS)"}, std::pair{"GPP64.dll", "GamePP（游加加）"},
                                          std::pair{"GameTracker64.dll", "GamePP（游加加）"}}) {
        if (!moduleLoaded(c, module)) continue;
        const bool hurts = presentShare > 0.3 && hitchesPerMin > 1;
        Finding f{"R2", hurts ? "high" : "medium", std::string(product) + " 注入了 Veyra",
                  "关闭它，或在它的设置里把 veyra_qml_ui.exe 加入排除列表。",
                  std::string(module) + " 已加载" + (hurts ? "；卡顿中 " + fmt(presentShare * 100, 0) + "% 的额外时间花在送显调用上" : std::string()),
                  "Overlay hooks run inside Present; the 2026-11-21 case stalled Present ~60 s.", hurts ? 90.0 : 40.0};
        f.params["product"] = product;
        add(c, std::move(f));
        break;
    }
    // R3 Driver forced V-Sync off: immediate flips while unobstructed (11-21).
    const double scanP50 = disp["presentToScanoutP50Ms"]["p50"].number(-1);
    if (scanP50 >= 0 && scanP50 < 1.0 && disp["presentToScanoutP50Ms"]["n"].number() >= 10 && disp["vrrActiveSeconds"].number() == 0)
        add(c, Finding{"R3", "medium", "驱动可能强制关闭了垂直同步（会撕裂）", "NVIDIA 控制面板 → 管理 3D 设置 → 程序设置：veyra_qml_ui.exe 的“垂直同步”改为“使用 3D 应用程序设置”。",
                       "送显到上屏的中位时间 " + fmt(scanP50, 2) + " ms（固定刷新下正常应接近一个刷新周期）", "Present-to-scanout P50 < 1 ms at fixed refresh = immediate flips.", 45});
    // R4 Display VRR follows Veyra although Veyra's own VRR is off (11-06/11-20).
    const double vrrSeconds = disp["vrrActiveSeconds"].number(), vrrSampled = disp["vrrSampledSeconds"].number();
    const bool veyraVrr = c.snapshot["interface"]["variableRefresh"].boolean(false);
    if (vrrSampled >= 10 && vrrSeconds / vrrSampled > 0.5 && !veyraVrr)
        add(c, Finding{"R4", hitchesPerMin > 2 ? "medium" : "info", "显示器的可变刷新正在跟随 Veyra",
                       "若画面闪烁或果冻感：在 NVIDIA 控制面板对 Veyra 设“显示器技术：固定刷新”，或在 Veyra 里开启可变刷新开关让送显匹配。",
                       "可变刷新生效 " + fmt(vrrSeconds, 0) + "/" + fmt(vrrSampled, 0) + " 秒", "Driver VRR follows uneven presents; flicker/overdrive cases 11-06, 11-20.", 35});
    // R5 Periodic hitches (beat between output rate and refresh, 11-01).
    if (c.hitchOnsets.size() >= 4) {
        std::vector<double> gaps;
        for (size_t i = 1; i < c.hitchOnsets.size(); ++i) gaps.push_back(c.hitchOnsets[i] - c.hitchOnsets[i - 1]);
        const Dist g = distOf(gaps);
        // A beat shows up as pacing/present/unexplained hitches; periodic
        // frame-generation probing or rebuilds are reported by their own rules.
        const double displaySide = causeShare(events, CausePacing) + causeShare(events, CausePresent) + causeShare(events, CauseUnknown);
        if (g.mean > 1500 && g.sd / g.mean < 0.25 && displaySide > 0.5) {
            const double refresh = disp["refreshHz"].number();
            Finding f{"R5", "medium", "卡顿按固定周期出现（约每 " + fmt(g.p50 / 1000.0, 1) + " 秒）",
                           "通常是输出帧率与屏幕刷新率不成整数倍造成的拍频：让输出帧率等于刷新率或其整数分之一，或开启可变刷新。",
                           fmt(double(c.hitchOnsets.size()), 0) + " 次卡顿，间隔中位 " + fmt(g.p50 / 1000.0, 2) + " 秒（离散 " + fmt(g.sd / g.mean * 100, 0) + "%）" +
                               (refresh > 0 ? "；刷新 " + fmt(refresh, 2) + " Hz，输出 " + fmt(fps, 2) + " fps" : std::string()),
                           "Beat period ≈ 1/|f_out − f_refresh/k| (16.5 s case 2026-11-01).", 60};
            f.params["periodSeconds"] = round1(g.p50 / 1000.0);
            add(c, std::move(f));
        }
    }
    // R6 Capture arrival jitter (11-20 ITE 5G card).
    const bool capture = src["kind"].string() == "CaptureCard";
    const double sd = src["arrivalIntervalMs"]["sd"].number(), p99 = src["arrivalIntervalMs"]["p99"].number(), med = src["arrivalIntervalMs"]["p50"].number();
    if (capture && med > 0 && sd > 1.0 && p99 > 1.3 * med)
        add(c, Finding{"R6", inputShare > 0.3 ? "high" : "medium", "采集卡送帧时间不均匀", "换 USB 口（直连机身 USB 3）或换线、关闭 USB 选择性暂停；换别的采集卡对比。",
                       "到达间隔中位 " + fmt(med, 2) + " ms、标准差 " + fmt(sd, 2) + " ms、P99 " + fmt(p99, 2) + " ms", "Arrival SD > 1 ms (ITE 048d:900c case 2026-11-20).", 55});
    // R7 Capture clock drift (#268).
    const double drift = src["clockDriftPpm"].number(0);
    if (capture && std::abs(drift) > 300)
        add(c, Finding{"R7", "medium", "采集卡时钟与系统时钟漂移", "长时间运行后若帧率减半，拔插采集卡即可恢复；可向采集卡厂商反馈。",
                       "设备时间戳相对系统时钟漂移约 " + fmt(drift, 0) + " ppm", "Device PTS drift; halved fps after hours (#268).", 30});
    // R8 Dedup judging unstable: duplicates skipped only part of the time.
    const double accepted = src["accepted"].number(), dedup = src["skipped"]["ContentDedup"].number(0), half = src["skipped"]["CaptureHalfRate"].number(0);
    if (accepted + dedup > 300 && dedup > 0) {
        const double share = dedup / (accepted + dedup);
        if (share > 0.05 && share < 0.42)
            add(c, Finding{"R8", "medium", "去重判定不稳定", "30 帧游戏可改用“60→30”；或把采集格式改为 NV12/P010 再试自动去重。",
                           "去重丢掉 " + fmt(share * 100, 0) + "% 的输入帧（30 帧内容稳定时应接近 50%）", "RGB24/T911 4K60 false-new cases (11-18, 11-02).", 35});
    }
    (void)half;
    // R9 Frame generation over budget.
    const double discardShare = fg["discardedGeneratedShare"].number();
    const double skippedShare = fg["skippedShare"].number();
    const double rejected = lg["fgRejectedPairs"].number(), admitted = lg["fgAdmittedPairs"].number();
    const double fgP95 = lg["gpuFgBatchP95Ms"]["p50"].number();
    const double interval = c.sourceIntervalMs;
    const bool overBudget = discardShare > 0.05 || skippedShare > 0.05 || (admitted + rejected > 100 && rejected / (admitted + rejected) > 0.05) ||
                            (interval > 0 && fgP95 > 0.9 * interval);
    if (overBudget)
        add(c, Finding{"R9", "high", "补帧超出 GPU 预算", "降低补帧档位（VFG 高→中）或倍数，或少开一层 NR、把内部处理分辨率降到 1080p/自动。",
                       "生成帧被丢弃 " + fmt(discardShare * 100, 1) + "%，评估前跳过的补帧 " + fmt(skippedShare * 100, 1) + "%" + (admitted + rejected > 0 ? "，补帧对被拒 " + fmt(rejected, 0) + "/" + fmt(admitted + rejected, 0) : std::string()) +
                           (fgP95 > 0 ? "，补帧 GPU P95 " + fmt(fgP95, 2) + " ms（源间隔 " + fmt(interval, 2) + " ms）" : std::string()),
                       "Generated frames discarded/rejected or FG P95 near the source interval.", 85});
    if (overBudget && c.hitchOnsets.size() >= 4) {
        std::vector<double> gaps;
        for (size_t i = 1; i < c.hitchOnsets.size(); ++i) gaps.push_back(c.hitchOnsets[i] - c.hitchOnsets[i - 1]);
        const Dist g = distOf(gaps);
        if (g.mean > 1000 && g.sd / g.mean < 0.3 && !c.findings.empty() && c.findings.back().id == "R9")
            c.findings.back().evidence += "；卡顿约每 " + fmt(g.p50 / 1000.0, 1) + " 秒一串（补帧被拒后定期重试）";
    }
    else if (gpuShare > 0.5 && hitchesPerMin > 1)
        add(c, Finding{"R18", "medium", "卡顿主要发生在 GPU 处理", "降低增强档位或分辨率；看报告里“GPU 阶段”哪个耗时最高。",
                       "卡顿额外时间的 " + fmt(gpuShare * 100, 0) + "% 在 GPU 处理阶段", "GPU-bound hitches.", 50});
    // R10 GPU shared with another process.
    const double othersMax = tel["gpu_others_max"]["p90"].number(0);
    if (othersMax > 30) {
        std::string who;
        for (const auto& p : tel["topOtherGpuProcesses"].array()) { if (!who.empty()) who += "、"; who += p["process"].string(); if (who.size() > 60) break; }
        add(c, Finding{"R10", othersMax > 50 ? "high" : "medium", "别的程序在大量使用 GPU", "关闭或暂停这些程序（游戏、录屏、壁纸等）后再看；同机玩游戏时可限制游戏帧率。",
                       "其他进程 GPU 占用 P90 " + fmt(othersMax, 0) + "%" + (who.empty() ? std::string() : "，主要是 " + who), "Other processes on the GPU engines (PDH GPU Engine).", 70});
    }
    // R11 GPU clocks held down (power, thermal) or running on battery / power saving.
    const auto& reasons = tel["nvidiaClockReasonsSeconds"];
    const double thermal = reasons["SwThermalSlowdown"].number(0) + reasons["HwThermalSlowdown"].number(0);
    const double power = reasons["SwPowerCap"].number(0) + reasons["HwPowerBrakeSlowdown"].number(0) + reasons["HwSlowdown"].number(0);
    const double rows = tel["rows"].number(0);
    const auto& pw = c.snapshot["system"]["power"];
    const bool battery = pw["acLine"].string() == "battery";
    const bool saver = pw["batterySaver"].boolean(false) || pw["powerMode"].string() == "best-power-efficiency";
    if (battery || saver || (rows > 0 && (thermal / rows > 0.1 || power / rows > 0.3)))
        add(c, Finding{"R11", battery || thermal / std::max(1.0, rows) > 0.1 ? "high" : "medium", "GPU 被限制频率（电源/功耗/温度）",
                       "笔记本请接电源；Windows 电源模式选“最佳性能”；检查散热。",
                       std::string(battery ? "正在使用电池；" : "") + (saver ? "节能模式开启；" : "") +
                           (rows > 0 ? "功耗限制 " + fmt(power, 0) + " 秒、温度限制 " + fmt(thermal, 0) + " 秒（共 " + fmt(rows, 0) + " 秒）" : std::string()),
                       "NVML clock event reasons / power status.", 55});
    // R12 CPU short or engine thread delayed.
    const double cpuP90 = tel["cpu_total"]["p90"].number(0);
    const double stalls = lg["engineStalls"]["n"].number(0);
    if (cpuP90 > 90 || (engineShare > 0.4 && hitchesPerMin > 1) || stalls >= 5)
        add(c, Finding{"R12", cpuP90 > 90 ? "high" : "medium", "CPU 不足或引擎线程被拖住", "关闭后台占用 CPU 的程序；同机玩游戏时 CPU 可能被游戏占满。",
                       "系统 CPU 占用 P90 " + fmt(cpuP90, 0) + "%，引擎卡顿记录 " + fmt(stalls, 0) + " 次，卡顿额外时间的 " + fmt(engineShare * 100, 0) + "% 在引擎排队",
                       "CPU saturation / engine-stall lines.", 50});
    // R13 Frequent settings changes caused rebuild stalls.
    const double resets = double(c.resetTimes.size());
    const double minutes = std::max(1.0, out["spanSeconds"].number() / 60.0);
    if (resets >= 3 && resets / minutes >= 1.0)
        add(c, Finding{"R13", "info", "记录期间多次改设置或跳转，每次都会重建一次", "这类停顿是正常的重建过程；比较流畅度时请在不改设置的一段里看。",
                       "重建 " + fmt(resets, 0) + " 次（每分钟 " + fmt(resets / minutes, 1) + " 次）", "Reset events on the trace.", 20});
    // R14 Window composed by the system.
    const double composed = disp["compositionModeSeconds"]["composed"].number(0);
    const double compositionTotal = composed + disp["compositionModeSeconds"]["overlay"].number(0) + disp["compositionModeSeconds"]["none"].number(0);
    if (compositionTotal >= 10 && composed / compositionTotal > 0.8)
        add(c, Finding{"R14", "info", "画面由系统合成（窗口模式）", "全屏播放可减少一帧左右的显示延迟，并避免窗口下补帧被合成器丢帧。",
                       "合成方式为“composed”的时间占 " + fmt(composed / compositionTotal * 100, 0) + "%", "DXGI composition mode composed.", 15});
    // R15 First run / shader rebuild (from the log head).
    double longest = 0;
    for (const auto& l : c.logHead) for (const char* key : {"createMs", "deviceCreateMs", "pipelineMs", "compileMs"}) longest = std::max(longest, l.d(key, 0));
    if (longest > 5000)
        add(c, Finding{"R15", "info", "启动或首次开启效果时编译耗时很长", "首次运行或驱动更新后会重新编译着色器，等一次跑完即可；每次都很慢请检查驱动的着色器缓存设置。",
                       "单次创建/编译最长 " + fmt(longest / 1000.0, 1) + " 秒", "Long device/pipeline creation in the log head.", 15});
    // R16 VRAM pressure.
    const double vram = lg["vramMiB"]["max"].number(0), budget = lg["vramBudgetMiB"]["p50"].number(0);
    if (budget > 0 && vram > 0.9 * budget)
        add(c, Finding{"R16", "medium", "显存紧张", "降低处理分辨率或补帧档位，关闭同时运行的游戏或其他占显存的程序。",
                       "Veyra 显存最高 " + fmt(vram, 0) + " MiB，预算 " + fmt(budget, 0) + " MiB", "VRAM over 90% of the budget.", 45});
    // R17 The interface's 1% low steps while the frame time distribution is stable.
    const double swingP99 = report["summary"]["low1P99Swing"].number(0), swingAvg = report["summary"]["low1AvgSwing"].number(0);
    if (swingP99 > 0.2 && swingAvg < swingP99 * 0.5)
        add(c, Finding{"R17", "info", "“1% low”读数在台阶式跳动，帧时间分布本身稳定", "界面的 1% low 是最近 10 秒帧间隔的第 99 百分位；长间隔次数刚好跨过门槛时读数会成台阶跳，不代表变卡。",
                       "10 秒窗口的 P99 读数波动 " + fmt(swingP99 * 100, 0) + "%，最慢 1% 平均只波动 " + fmt(swingAvg * 100, 0) + "%", "Step artefact of the P99 readout (2026-11-26).", 10});
    // General: dropped trace events, incomplete recording.
    if (c.trace.dropped > 0 || c.trace.partial)
        add(c, Finding{"G1", "info", "记录不完整", "数据仍可分析；如反复出现，请缩短诊断时长。",
                       c.trace.partial ? "记录未正常结束（程序退出或崩溃）" : "丢失 " + fmt(double(c.trace.dropped), 0) + " 条事件", "Trace incomplete.", 5});
    // General: output below the expected rate.
    const double expectedFps = out["expectedFps"].number();
    const double low = out["low1FpsSlowestAverage"].number();
    if (c.findings.empty() && fps > 0 && hitchesPerMin < 1 && low > 0.85 * fps)
        add(c, Finding{"G0", "pass", "整体流畅", "这段记录里没有发现明显问题。",
                       "平均 " + fmt(fps, 1) + " fps，最慢 1% 平均 " + fmt(low, 1) + " fps，卡顿每分钟 " + fmt(hitchesPerMin, 1) + " 次", "", 1});
    else if (c.findings.empty() && hitchesPerMin >= 1) {
        // Name the stage when no rule matched.
        int bestCause = CauseUnknown;
        double best = 0;
        for (int k = 0; k < CauseCount; ++k) { const double s = causeShare(events, k); if (s > best) { best = s; bestCause = k; } }
        Finding f{"G2", "medium", std::string("卡顿主要发生在：") + causeZh(bestCause), "请把这个诊断包发给开发者以便定位。",
                  "卡顿每分钟 " + fmt(hitchesPerMin, 1) + " 次，" + fmt(best * 100, 0) + "% 的额外时间在这一阶段", "No rule matched.", 30};
        f.params["cause"] = causeId(bestCause);
        add(c, std::move(f));
    }
    (void)expectedFps;
    std::stable_sort(c.findings.begin(), c.findings.end(), [](const Finding& a, const Finding& b) { return a.score > b.score; });
}

Value eventsJson(Context& c, const std::vector<HitchEvent>& events) {
    std::vector<size_t> order(events.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return events[a].excess > events[b].excess; });
    Array out;
    for (size_t rank = 0; rank < std::min<size_t>(60, order.size()); ++rank) {
        const auto& e = events[order[rank]];
        int cause = CauseUnknown;
        double best = 0;
        for (int k = 0; k < CauseCount; ++k) if (e.causeMs[size_t(k)] > best) { best = e.causeMs[size_t(k)]; cause = k; }
        Object o;
        o["index"] = order[rank];
        o["startMs"] = round3(e.start);
        o["endMs"] = round3(e.end);
        o["hitches"] = e.count;
        o["worstIntervalMs"] = round3(e.worst);
        o["extraMs"] = round3(e.excess);
        o["cause"] = causeId(cause);
        o["causeText"] = causeZh(cause);
        o["revision"] = e.revision;
        std::string stage;
        double stageBest = 0;
        for (const auto& [name, value] : e.stageMs) if (value > stageBest) { stageBest = value; stage = name; }
        if (!stage.empty()) o["stage"] = stage;
        Object shares;
        for (int k = 0; k < CauseCount; ++k) if (e.causeMs[size_t(k)] > 0) shares[causeId(k)] = round3(e.causeMs[size_t(k)]);
        o["extraMsByCause"] = shares;
        Array marks;
        for (size_t m = 0; m < c.markTimes.size(); ++m) if (c.markTimes[m] >= e.start - 3000 && c.markTimes[m] <= e.end + 5000) marks.push_back(c.markNumbers[m]);
        if (!marks.empty()) o["marks"] = marks;
        out.push_back(Value(std::move(o)));
    }
    return out;
}

Value marksJson(Context& c) {
    Array out;
    for (size_t m = 0; m < c.markTimes.size(); ++m) {
        const double t = c.markTimes[m];
        double worst = 0;
        int hitches = 0;
        std::array<double, CauseCount> causes{};
        for (const auto& f : c.frames) {
            if (f.t < t - 5000 || f.t > t + 1000) continue;
            worst = std::max(worst, f.dt);
            if (f.hitch) { ++hitches; if (f.cause >= 0) causes[size_t(f.cause)] += f.dt; }
        }
        int cause = -1;
        double best = 0;
        for (int k = 0; k < CauseCount; ++k) if (causes[size_t(k)] > best) { best = causes[size_t(k)]; cause = k; }
        Object o{{{"number", c.markNumbers[m]}, {"tMs", round3(t)}, {"worstIntervalMs5s", round3(worst)}, {"hitches5s", hitches}}};
        if (cause >= 0) { o["cause"] = causeId(cause); o["causeText"] = causeZh(cause); }
        out.push_back(Value(std::move(o)));
    }
    return out;
}

Value settingsChanges(const Context& c) {
    Array out;
    for (const auto& e : c.events["events"].array()) {
        if (e["type"].string() != "setting" || e["volatile"].boolean(false)) continue;
        Object o;
        o["name"] = e["name"];
        o["previous"] = e["previous"];
        o["value"] = e["value"];
        o["tMs"] = round3((e["host"].number() - double(c.trace.started)) / 10000.0);
        out.push_back(Value(std::move(o)));
    }
    return out;
}

Value environment(const Context& c) {
    Object o;
    const auto& s = c.snapshot["system"];
    const auto& v = c.snapshot["veyra"];
    o["veyraVersion"] = v["version"];
    o["os"] = s["os"]["product"].string() + " " + s["os"]["displayVersion"].string() + " (" + s["os"]["build"].string() + ")";
    o["cpu"] = s["cpu"]["name"];
    Array gpus;
    for (const auto& g : s["gpus"].array())
        if (!g["software"].boolean()) gpus.push_back(g["name"].string() + " · " + (g["nvidiaDriver"].isString() ? g["nvidiaDriver"].string() : g["driver"].string()));
    o["gpus"] = gpus;
    Array displays;
    for (const auto& d : s["displays"].array())
        displays.push_back(d["monitor"].string() + " " + fmt(d["width"].number(), 0) + "×" + fmt(d["height"].number(), 0) + " @" + fmt(d["refreshHz"].number(), 2) +
                           "Hz " + d["output"].string() + (d["hdrEnabled"].boolean() ? " HDR" : "") + (d["primary"].boolean() ? " (主)" : ""));
    o["displays"] = displays;
    o["hags"] = s["graphics"]["hagsEnabled"];
    o["gameMode"] = s["graphics"]["gameModeEnabled"];
    o["directxUserGlobalSettings"] = s["graphics"]["directxUserGlobalSettings"];
    o["powerScheme"] = s["power"]["scheme"];
    o["powerMode"] = s["power"]["powerMode"];
    o["acLine"] = s["power"]["acLine"];
    Array modules, processes;
    for (const auto& m : s["modules"]["flagged"].array()) modules.push_back(m["module"].string() + " (" + m["product"].string() + ")");
    for (const auto& p : s["processes"]["flagged"].array()) processes.push_back(p["process"].string() + " (" + p["product"].string() + ")");
    o["injectedModules"] = modules;
    o["overlayProcesses"] = processes;
    o["otherVeyraInstances"] = s["processes"]["otherVeyraInstances"];
    o["telemetrySources"] = c.events["trace"]["telemetrySources"];
    return o;
}

std::string headline(const Object& summary) {
    const Value s = Value(summary);
    std::string text = "平均 " + fmt(s["averageFps"].number(), 1) + " fps · 最慢1%平均 " + fmt(s["low1FpsSlowestAverage"].number(), 1) +
                       " · 1% low(P99) " + fmt(s["low1FpsP99"].number(), 1) + " · 卡顿 " + fmt(s["hitches"].number(), 0) + " 次";
    return text;
}

void analyzeLogOnly(Context& c, Object& summary) {
    // Versions without the recorder: per-second log statistics only.
    std::vector<double> calls;
    for (const auto& l : c.log) if (l.tag == "display-stats") calls.push_back(l.d("presentCalls", 0));
    const Dist d = distOf(calls);
    summary["averageFps"] = round3(d.mean);
    summary["minSecondFps"] = round3(d.min);
    summary["logOnly"] = true;
    Object out;
    out["presentCallsPerSecond"] = distJson(d);
    c.report["output"] = out;
}
} // namespace

int analyze(const fs::path& input, const fs::path& outDir, const Options& options, std::string& error) {
    Context c;
    c.options = options;
    c.in = locate(input);
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (!c.in.traceFile.empty() && !readTrace(c.in.traceFile, c.trace, error)) return 2;
    try {
        if (!c.in.snapshotFile.empty()) c.snapshot = json::parse(readText(c.in.snapshotFile));
        if (!c.in.eventsFile.empty()) c.events = json::parse(readText(c.in.eventsFile));
    } catch (const std::exception& ex) {
        error = std::string("snapshot/events: ") + ex.what();
        return 3;
    }
    if (!c.in.telemetryFile.empty()) c.telemetry = readTelemetry(c.in.telemetryFile);
    // Without the trace header, place log lines from the events file or the first line.
    int64_t utc = c.trace.startedUtcMs ? c.trace.startedUtcMs : int64_t(c.events["trace"]["startedUtcMs"].number(0));
    if (!c.in.logFile.empty()) {
        c.log = readLog(c.in.logFile, utc);
        if (!utc && !c.log.empty()) {
            utc = c.log.front().utcMs;
            for (auto& l : c.log) l.t = l.utcMs ? double(l.utcMs - utc) : NAN;
        }
    }
    if (!c.in.logHeadFile.empty()) c.logHead = readLog(c.in.logHeadFile, utc);
    if (!c.trace.started && !c.trace.events.empty()) c.trace.started = c.trace.events.front().host;
    c.durationMs = c.trace.ended > c.trace.started ? double(c.trace.ended - c.trace.started) / 10000.0
                 : c.trace.events.empty() ? 0 : ms(c, c.trace.events.back().host);

    Object summary;
    c.report["schema"] = "veyra-diagnostic-report/1";
    c.report["analyzer"] = "veyra_frame_analyzer (C++)";
    {
        Object trace;
        trace["file"] = c.trace.file;
        trace["events"] = c.trace.events.size();
        trace["declaredRecorded"] = c.trace.declaredRecorded;
        trace["dropped"] = c.trace.dropped;
        trace["malformedRows"] = c.trace.malformed;
        trace["complete"] = c.trace.complete;
        trace["finalized"] = c.trace.finalized;
        trace["partial"] = c.trace.partial;
        trace["termination"] = c.trace.termination;
        trace["durationSeconds"] = round3(c.durationMs / 1000.0);
        trace["writer"] = c.trace.meta["writer"];
        c.report["trace"] = trace;
    }
    std::vector<HitchEvent> events;
    if (c.in.logOnly) {
        analyzeLogOnly(c, summary);
    } else {
        buildFrames(c);
        attribute(c, events);
        c.report["output"] = outputMetrics(c, summary);
        c.report["byRevision"] = revisionMetrics(c);
        c.report["latency"] = latencyMetrics(c);
        c.report["source"] = sourceMetrics(c, summary);
        c.report["stages"] = stageMetrics(c);
        c.report["frameGeneration"] = fgMetrics(c);
    }
    c.report["display"] = displayMetrics(c);
    c.report["telemetry"] = telemetryMetrics(c);
    c.report["log"] = logMetrics(c);
    c.report["perSecond"] = perSecond(c, summary);
    c.report["summary"] = summary;   // R17 reads the swing values
    {
        Array resets;
        for (const auto& e : c.trace.events)
            if (e.kind == K::Reset)
                resets.push_back(Object{{{"tMs", round3(ms(c, e.host))}, {"reason", resetReasonName(e.detail)}, {"outcome", e.count}, {"totalMs", round3(e.ms)}}});
        c.report["resets"] = resets;
    }
    c.report["marks"] = marksJson(c);
    c.report["settingsChanges"] = settingsChanges(c);
    c.report["environment"] = environment(c);
    rules(c, events);
    Array findings;
    for (const auto& f : c.findings)
        findings.push_back(Object{{{"id", f.id}, {"severity", f.severity}, {"title", f.title}, {"advice", f.advice}, {"evidence", f.evidence},
                                   {"developer", f.developer}, {"score", f.score}, {"params", f.params}}});
    c.report["findings"] = findings;
    c.report["events"] = eventsJson(c, events);
    // Stage attribution totals over all hitches.
    {
        Object shares;
        for (int k = 0; k < CauseCount; ++k) { const double s = causeShare(events, k); if (s > 0) shares[causeId(k)] = round3(s); }
        c.report["hitchCauseShare"] = shares;
    }
    summary["headline"] = headline(summary);
    if (!c.findings.empty()) summary["topFinding"] = c.findings.front().title;
    summary["durationSeconds"] = round3(c.durationMs / 1000.0);
    summary["marks"] = c.markTimes.size();
    c.report["summary"] = summary;

    Analysis a;
    a.report = Value(std::move(c.report));
    a.frames = std::move(c.frames);
    // Map frame -> report.events entry (events are re-ordered by severity).
    std::map<int, int> rankOf;
    const auto& list = a.report["events"].array();
    for (size_t r = 0; r < list.size(); ++r) rankOf[int(list[r]["index"].number())] = int(r);
    for (auto& f : a.frames) if (f.event >= 0) { const auto it = rankOf.find(f.event); f.event = it == rankOf.end() ? -1 : it->second; }
    if (!writeText(outDir / "report.json", json::dump(a.report))) { error = "cannot write report.json"; return 4; }
    writeFramesCsv(a, outDir / "frames.csv");
    writeSummary(a, outDir / "SUMMARY.md");
    writeHtml(a, outDir / "report.html");
    return 0;
}

// ---------------------------------------------------------------- compare
namespace {
Value loadReport(const fs::path& folder, Value& snapshot) {
    Value report = json::parse(readText(folder / "report.json"));
    const fs::path snap = fs::exists(folder / "raw" / "snapshot.json") ? folder / "raw" / "snapshot.json" : folder / "snapshot.json";
    if (fs::exists(snap)) snapshot = json::parse(readText(snap));
    return report;
}
void diffObjects(const Value& a, const Value& b, const std::string& prefix, Array& out, size_t limit = 400) {
    std::set<std::string> keys;
    for (const auto& [k, v] : a.object().items) keys.insert(k);
    for (const auto& [k, v] : b.object().items) keys.insert(k);
    for (const auto& k : keys) {
        if (out.size() >= limit) return;
        const Value& x = a[k];
        const Value& y = b[k];
        if (x.isObject() || y.isObject()) { diffObjects(x, y, prefix + k + ".", out, limit); continue; }
        if (json::dump(x, -1) == json::dump(y, -1)) continue;
        out.push_back(Object{{{"name", prefix + k}, {"a", x}, {"b", y}}});
    }
}
}

int compare(const fs::path& a, const fs::path& b, const fs::path& outDir, const Options& options, std::string& error) {
    for (const auto& folder : {a, b}) {
        if (fs::exists(folder / "report.json")) continue;
        std::string why;
        if (analyze(folder, folder, options, why) != 0) { error = "analyse " + folder.string() + ": " + why; return 2; }
    }
    Value snapA, snapB, ra, rb;
    try {
        ra = loadReport(a, snapA);
        rb = loadReport(b, snapB);
    } catch (const std::exception& ex) { error = ex.what(); return 3; }
    Object out;
    out["schema"] = "veyra-diagnostic-compare/1";
    out["a"] = Object{{{"folder", a.filename().string()}, {"version", snapA["veyra"]["version"]}, {"summary", ra["summary"]}}};
    out["b"] = Object{{{"folder", b.filename().string()}, {"version", snapB["veyra"]["version"]}, {"summary", rb["summary"]}}};
    Array metrics;
    const auto metric = [&](const char* name, const char* label, const Value& x, const Value& y, bool higherIsBetter) {
        if (!x.isNumber() && !y.isNumber()) return;
        const double va = x.number(NAN), vb = y.number(NAN);
        Object m{{{"name", name}, {"label", label}, {"a", x}, {"b", y}, {"higherIsBetter", higherIsBetter}}};
        if (std::isfinite(va) && std::isfinite(vb)) {
            m["delta"] = round3(vb - va);
            m["deltaPercent"] = round1(va != 0 ? (vb - va) / std::abs(va) * 100.0 : 0);
            const bool better = higherIsBetter ? vb > va : vb < va;
            m["verdict"] = std::abs(vb - va) <= 0.03 * std::max(std::abs(va), 1e-9) ? "same" : better ? "b-better" : "b-worse";
        }
        metrics.push_back(Value(std::move(m)));
    };
    metric("averageFps", "平均帧率", ra["summary"]["averageFps"], rb["summary"]["averageFps"], true);
    metric("low1FpsSlowestAverage", "最慢 1% 平均", ra["summary"]["low1FpsSlowestAverage"], rb["summary"]["low1FpsSlowestAverage"], true);
    metric("low1FpsP99", "1% low（P99）", ra["summary"]["low1FpsP99"], rb["summary"]["low1FpsP99"], true);
    metric("hitchesPerMinute", "卡顿/分钟", ra["summary"]["hitchesPerMinute"], rb["summary"]["hitchesPerMinute"], false);
    metric("worstIntervalMs", "最长帧间隔 ms", ra["summary"]["worstIntervalMs"], rb["summary"]["worstIntervalMs"], false);
    metric("intervalP99", "帧间隔 P99 ms", ra["output"]["intervalMs"]["p99"], rb["output"]["intervalMs"]["p99"], false);
    metric("irregularShare", "节拍不均比例", ra["output"]["irregularShare"], rb["output"]["irregularShare"], false);
    metric("latencyP50", "到达→送显 P50 ms", ra["latency"]["arrivalToPresentMs"]["p50"], rb["latency"]["arrivalToPresentMs"]["p50"], false);
    metric("latencyP95", "到达→送显 P95 ms", ra["latency"]["arrivalToPresentMs"]["p95"], rb["latency"]["arrivalToPresentMs"]["p95"], false);
    metric("discardedGeneratedShare", "生成帧丢弃比例", ra["frameGeneration"]["discardedGeneratedShare"], rb["frameGeneration"]["discardedGeneratedShare"], false);
    metric("scanoutP50", "送显→上屏 P50 ms", ra["display"]["presentToScanoutP50Ms"]["p50"], rb["display"]["presentToScanoutP50Ms"]["p50"], false);
    metric("cpuTotalMean", "系统 CPU 平均 %", ra["telemetry"]["cpu_total"]["mean"], rb["telemetry"]["cpu_total"]["mean"], false);
    metric("gpuVeyraMean", "Veyra GPU 占用平均 %", ra["telemetry"]["gpu_veyra_max"]["mean"], rb["telemetry"]["gpu_veyra_max"]["mean"], false);
    // Stage P95s present on either side.
    for (const char* group : {"gpuMs", "cpuMs"}) {
        std::set<std::string> stages;
        for (const auto& [k, v] : ra["stages"][group].object().items) stages.insert(k);
        for (const auto& [k, v] : rb["stages"][group].object().items) stages.insert(k);
        for (const auto& stage : stages) {
            const std::string name = std::string(group) + "." + stage + ".p95";
            metric(name.c_str(), name.c_str(), ra["stages"][group][stage]["p95"], rb["stages"][group][stage]["p95"], false);
        }
    }
    out["metrics"] = metrics;
    Array settings, prefs, env;
    diffObjects(snapA["interface"], snapB["interface"], "", settings);
    diffObjects(snapA["veyra"]["preferences"], snapB["veyra"]["preferences"], "", prefs);
    diffObjects(ra["environment"], rb["environment"], "", env);
    out["interfaceDifferences"] = settings;
    out["preferenceDifferences"] = prefs;
    out["environmentDifferences"] = env;
    Array fa, fb;
    for (const auto& f : ra["findings"].array()) fa.push_back(f["id"].string() + " " + f["title"].string());
    for (const auto& f : rb["findings"].array()) fb.push_back(f["id"].string() + " " + f["title"].string());
    out["findingsA"] = fa;
    out["findingsB"] = fb;
    fs::create_directories(outDir);
    if (!writeText(outDir / "compare.json", json::dump(Value(out)))) { error = "cannot write compare.json"; return 4; }
    writeCompare(Value(out), outDir);
    return 0;
}

}
