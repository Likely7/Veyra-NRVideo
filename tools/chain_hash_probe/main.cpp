// Deterministic frame-hash probe for the UI migration (S0.3).
//
// Runs one fixed clip through EnhanceGraph with a named configuration and
// prints a SHA-256 of every output frame (RGBA8 readback of the video slot).
// The engine refactors in S1/S2 must keep these hashes identical wherever the
// processing is meant to stay unchanged. Diagnostic tool: readback is allowed
// here and never happens on the playback path.
//
// Usage: veyra_chain_hash_probe --input <clip> --case <name> [--frames N] [--out <json>]
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/source/MediaFileSource.h"

using namespace veyra;

namespace {
std::wstring widen(const std::string& utf8) {
    const int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(chars > 0 ? chars - 1 : 0, L'\0');
    if (chars > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), chars);
    return wide;
}
std::vector<std::string> utf8Arguments() {
    std::vector<std::string> args;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int n = 0; wide && n < count; ++n) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[n], -1, nullptr, 0, nullptr, nullptr);
        std::string s(bytes > 0 ? bytes - 1 : 0, '\0');
        if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, wide[n], -1, s.data(), bytes, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    LocalFree(wide);
    return args;
}

// Named configurations. Each maps to the same desc fields the engine sets, so
// the probe exercises exactly the product stages (NR, SR, order, colour,
// residual, protection, temporal) without the player's scheduling.
bool configure(const std::string& name, uint32_t w, uint32_t h, pipeline::EnhanceGraphDesc& gd) {
    gd.sourceWidth = w; gd.sourceHeight = h;
    gd.workWidth = w; gd.workHeight = h;
    gd.nrWidth = w; gd.nrHeight = h;
    gd.enableFg = false; gd.enableNr = false; gd.enableSr = false;
    gd.enableNvofStandalone = false;
    auto sr = [&](uint32_t quality) {
        gd.enableSr = true; gd.videoSrQuality = quality;
        gd.workWidth = w * 2; gd.workHeight = h * 2;
        gd.nrWidth = gd.workWidth; gd.nrHeight = gd.workHeight;
    };
    auto nr = [&] { gd.enableNr = true; gd.enableNvofStandalone = true; };
    if (name == "passthrough") return true;
    if (name == "nr") { nr(); return true; }
    if (name == "nr-style2") { nr(); gd.model.style = 2; gd.model.intensity = .6f; return true; }
    if (name == "nr-residual") { nr(); gd.residual = {1.4f, .7f, 1.2f, .8f, 1.1f}; return true; }
    if (name == "nr-protect") {
        nr(); gd.protection.enabled = true; gd.protection.featherPixels = 8;
        gd.protection.regions[0] = {.05f, .05f, .30f, .20f}; return true;
    }
    if (name == "nr-temporal") { nr(); gd.nrTemporal = true; return true; }
    if (name == "dlss-sr") { sr(0); return true; }
    if (name == "vsr") { sr(3); return true; }
    if (name == "sr-nr") { sr(0); nr(); return true; }
    if (name == "nr-sr") { sr(0); nr(); gd.nrBeforeSr = true; gd.nrWidth = w; gd.nrHeight = h; return true; }
    if (name == "vsr-nr") { sr(3); nr(); return true; }
    if (name == "color") {
        gd.color.enabled = true; gd.color.exposure = .3f; gd.color.contrast = 20; gd.color.temperature = 15;
        gd.color.saturation = -10; gd.color.shadows = 25; return true;
    }
    if (name == "color-nr") {
        nr(); gd.color.enabled = true; gd.color.exposure = -.2f; gd.color.vibrance = 30; return true;
    }
    return false;
}
} // namespace

int main() {
    const auto args = utf8Arguments();
    std::string input, caseName, outPath;
    int frames = 24;
    for (size_t i = 1; i < args.size(); ++i) {
        auto next = [&] { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (args[i] == "--input") input = next();
        else if (args[i] == "--case") caseName = next();
        else if (args[i] == "--frames") frames = std::stoi(next());
        else if (args[i] == "--out") outPath = next();
        else { std::fprintf(stderr, "unknown arg %s\n", args[i].c_str()); return 2; }
    }
    if (input.empty() || caseName.empty() || frames <= 0) {
        std::fprintf(stderr, "usage: --input <clip> --case <name> [--frames N] [--out json]\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    gfx::D3D12DeviceContext ctx;
    gfx::DeviceContextDesc ddesc;
    ddesc.commandSlotCount = 4;
    Status st = Status::Ok;
    if (!ctx.initialize(ddesc, st)) { std::fprintf(stderr, "device init failed\n"); return 1; }
    gfx::CommandSlotRing ring;
    if (!ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, st)) return 1;

    source::MediaFileSource src;
    source::SourceOpenDesc od;
    od.path = widen(input);
    od.preferHardwareDecode = false;  // CPU decode: bit-exact input every run
    if (!src.open(od)) { std::fprintf(stderr, "open failed\n"); return 1; }

    pipeline::EnhanceGraphDesc gd{};
    if (!configure(caseName, src.info().width, src.info().height, gd)) {
        std::fprintf(stderr, "unknown case %s\n", caseName.c_str());
        return 2;
    }
    gd.runtimeAbsPath = runtime::localRuntimeDirectory().wstring();
    pipeline::EnhanceGraph graph(ctx, ring);
    if (!graph.initialize(gd) || !graph.createViews()) { std::fprintf(stderr, "graph init failed\n"); return 1; }

    std::vector<std::string> hashes;
    bool reset = true;
    for (int n = 0; n < frames; ++n) {
        pipeline::FramePacket packet;
        const AVFrame* frame = nullptr;
        if (src.read(packet, &frame) != source::SourceReadStatus::Frame || !frame) break;
        pipeline::EnhanceGraph::FrameOutputs out;
        if (!graph.process(frame, 1000.0 * packet.pts.toDouble(), reset, out, uint64_t(n + 1))) {
            std::fprintf(stderr, "process failed at frame %d\n", n);
            return 1;
        }
        reset = false;
        sink::RgbaImage image;
        if (!sink::readRgba8(ctx, ring, graph.videoFrameResource(out.videoSlot), image)) {
            std::fprintf(stderr, "readback failed at frame %d\n", n);
            return 1;
        }
        hashes.push_back(sha256Hex(image.pixels.data(), image.pixels.size()));
    }
    (void)ring.waitIdle();
    graph.shutdown();

    std::string json = std::format("{{\"case\":\"{}\",\"frames\":{},\"work\":\"{}x{}\",\"nr\":\"{}x{}\",\"hashes\":[",
                                   caseName, hashes.size(), graph.workWidth(), graph.workHeight(), graph.nrWidth(), graph.nrHeight());
    for (size_t n = 0; n < hashes.size(); ++n) json += (n ? ",\"" : "\"") + hashes[n] + "\"";
    json += "]}\n";
    std::fputs(json.c_str(), stdout);
    if (!outPath.empty()) {
        std::ofstream f(widen(outPath), std::ios::binary);
        f << json;
        if (!f) return 1;
    }
    return hashes.size() == size_t(frames) ? 0 : 3;
}
