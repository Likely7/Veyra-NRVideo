// SPDX-License-Identifier: GPL-3.0-only
// AMD FRC (AMF frame rate conversion) through the real enhancement graph.
//   amd     : on the first AMD adapter, a bright bar moving 16 px per frame must appear
//             in every generated frame halfway between its two real neighbours with sharp
//             edges (a blend of the two frames would show two half-bright 16 px bands),
//             history resets honoured and no D3D12 debug-layer error.
//   other   : on an NVIDIA adapter. Where an AMD driver installed the AMF runtime (this
//             PC has an AMD iGPU), AMF FRC also ran on the RTX 5070 (2026-11-14); then the
//             same checks apply. Otherwise it must fail cleanly as a frame-generation
//             failure, not crash or hang. The outcome is printed as AMF_FRC_OTHER.
//   flow-4k : measure the AMD optical-flow stage at 4K with full and half flow extents.
// usage: veyra_amf_frc_tests OUTDIR amd|other|flow-4k
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
int amdFlow4k(veyra::gfx::D3D12DeviceContext& ctx, veyra::gfx::CommandSlotRing& ring) {
    using namespace veyra;
    constexpr unsigned width=3840,height=2160,frames=16,warmup=4;
    auto freeFrame=[](AVFrame* f){av_frame_free(&f);};
    std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);
    if(!frame)return 1;
    frame->format=AV_PIX_FMT_RGBA;
    frame->width=width;
    frame->height=height;
    frame->color_range=AVCOL_RANGE_JPEG;
    frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    if(av_frame_get_buffer(frame.get(),32)<0)return 1;

    for(const bool half:{false,true}){
        pipeline::EnhanceGraphDesc desc;
        desc.sourceWidth=desc.workWidth=width;
        desc.sourceHeight=desc.workHeight=height;
        desc.rgbInput=true;
        desc.enableNr=desc.enableSr=desc.enableFg=false;
        desc.enableNvofStandalone=true;
        desc.opticalFlowBackend=engine::OpticalFlowBackend::AmdFidelityFx;
        desc.amdFlowHalfResolution=half;
        desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        pipeline::EnhanceGraph graph(ctx,ring);
        if(!graph.initialize(desc)||!graph.createViews())return 1;
        graph.recordGpuTimings();
        std::vector<double> samples;
        for(unsigned i=0;i<frames;++i){
            for(unsigned y=0;y<height;++y){
                auto* row=reinterpret_cast<uint32_t*>(frame->data[0]+size_t(y)*frame->linesize[0]);
                std::fill_n(row,width,0xFF202020u);
                if(y>=370&&y<1100)std::fill_n(row+160+i*16,420,0xFFEEE0B0u);
            }
            pipeline::EnhanceGraph::FrameOutputs out;
            if(!graph.process(frame.get(),i*(1000.0/30),i==0,out,i+1)||
               !ctx.waitForFenceValue(out.videoFenceValue))return 1;
            for(const auto& timing:graph.takeGpuTimings()){
                const auto& sample=timing.gpu[size_t(diagnostics::GpuStage::Flow)];
                if(i>=warmup&&sample.milliseconds)samples.push_back(*sample.milliseconds);
            }
        }
        if(samples.size()!=frames-warmup||graph.metrics().amdOfExecuteCount!=frames||
           graph.flowWidth()!=width/(half?2:1)||graph.flowHeight()!=height/(half?2:1))return 1;
        std::sort(samples.begin(),samples.end());
        const size_t p95=(samples.size()*95+99)/100-1;
        std::cout<<"AMD_FLOW_4K half="<<half<<" flow="<<graph.flowWidth()<<'x'<<graph.flowHeight()
                 <<" samples="<<samples.size()<<" p50Ms="<<samples[samples.size()/2]
                 <<" p95Ms="<<samples[p95]<<'\n';
        if(!ring.drainQueue())return 1;
        graph.shutdown();
    }
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace veyra;
    if (argc != 3) return 2;
    const std::wstring mode = argv[2];
    const bool measureFlow = mode == L"flow-4k";
    const bool amd = mode == L"amd" || measureFlow;
    if (!amd && mode != L"other") return 2;
    std::filesystem::create_directories(argv[1]);
    veyra::Logger::instance().openFile((std::filesystem::path(argv[1]) / L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring; Status st = Status::Ok;
    gfx::DeviceContextDesc device; device.enableDebugLayer = !measureFlow; device.requiredVendorId = amd ? 0x1002 : 0x10DE;
    if (!ctx.initialize(device, st) || !ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 6, st)) {
        std::cout << "AMF_FRC no " << (amd ? "AMD" : "NVIDIA") << " adapter" << std::endl; CoUninitialize(); return 3;
    }
    if (measureFlow) {
        const int result = amdFlow4k(ctx,ring);
        ring.shutdown(); ctx.shutdown(); CoUninitialize();
        return result;
    }
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info; ctx.device()->QueryInterface(IID_PPV_ARGS(&info));
    constexpr unsigned W = 640, H = 360, Bar = 80, Step = 16, Row = 180;
    pipeline::EnhanceGraph graph(ctx, ring);
    pipeline::EnhanceGraphDesc desc; desc.sourceWidth = desc.workWidth = W; desc.sourceHeight = desc.workHeight = H;
    desc.rgbInput = true; desc.enableNr = desc.enableSr = false; desc.enableFg = true; desc.fgMultiplier = 2;
    desc.frameGenerationBackend = engine::FrameGenerationBackend::AmdFrc;
    desc.opticalFlowBackend = engine::OpticalFlowBackend::AmdFidelityFx;
    desc.runtimeAbsPath = runtime::localRuntimeDirectory().wstring(); desc.settingsRevision = 1;
    const bool opened = graph.initialize(desc) && graph.createViews();
    int failures = 0;
    auto check = [&](bool pass, const char* name) { std::cout << "AMF_FRC " << name << " pass=" << pass << std::endl; failures += !pass; return pass; };
    if (!amd) std::cout << "AMF_FRC_OTHER " << (opened ? "runs on this adapter" : "refused") << std::endl;
    if (!amd && !opened) {
        check(graph.failedBackend() == engine::FailedBackend::Fg, "another vendor's adapter refuses AMD FRC as a frame-generation failure");
        graph.shutdown(); ring.shutdown(); ctx.shutdown(); CoUninitialize();
        return failures ? 1 : 0;
    }
    if (!check(opened, "graph opens with AMD FRC")) { graph.shutdown(); ring.shutdown(); ctx.shutdown(); CoUninitialize(); return 1; }
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_RGBA; frame->width = W; frame->height = H;
    frame->color_range = AVCOL_RANGE_JPEG; frame->color_trc = AVCOL_TRC_IEC61966_2_1;
    bool ok = av_frame_get_buffer(frame, 32) >= 0;
    unsigned generated = 0, checked = 0, midpoint = 0, sharp = 0, resetsWithoutFrame = 0; double maxError = 0; unsigned maxSoft = 0;
    for (unsigned i = 0; ok && i < 48; ++i) {
        // 24 frames per sweep; the jump back at frame 24 is a history reset.
        const unsigned left = 40 + (i % 24) * Step;
        for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x) {
            auto* p = frame->data[0] + size_t(y) * frame->linesize[0] + x * 4;
            p[0] = p[1] = p[2] = uint8_t(y > 130 && y < 230 && x >= left && x < left + Bar ? 230 : 32); p[3] = 255;
        }
        pipeline::EnhanceGraph::FrameOutputs out;
        const bool reset = i == 0 || i == 24;
        ok = graph.process(frame, double(i) * 1000.0 / 30.0, reset, out, i + 1);
        if (!ok) { std::cout << "AMF_FRC process failed frame=" << i << " failed=" << int(graph.failedBackend()) << std::endl; break; }
        ok = ctx.waitForFenceValue(std::max(out.videoFenceValue, out.genFenceValue)) && graph.resolveGeneration(out);
        if (reset) { resetsWithoutFrame += !out.hasGenerated; continue; }
        if (!out.hasGenerated) continue;
        ++generated;
        sink::RgbaImage image;
        if (!sink::readRgba8(ctx, ring, graph.generatedFrameResource(out.genSlot), image)) { ok = false; break; }
        // Half-maximum crossings give the bar's centre; pixels between dark and bright
        // count the soft edge (a two-frame blend leaves 2 x 16 of them).
        const auto value = [&](unsigned x) { return int(image.pixels[(size_t(Row) * image.width + x) * 4]); };
        int first = -1, last = -1; unsigned soft = 0;
        for (unsigned x = 0; x < image.width; ++x) {
            const int v = value(x);
            if (v > 131) { if (first < 0) first = int(x); last = int(x); }
            if (v > 80 && v < 180) ++soft;
        }
        const double center = first < 0 ? -1e9 : (first + last) * 0.5;
        const double expected = 40.0 + (double(i % 24) - 0.5) * Step + Bar * 0.5 - 0.5, error = std::abs(center - expected);
        ++checked; maxError = std::max(maxError, error); midpoint += error <= 2.0; sharp += soft <= 8; maxSoft = std::max(maxSoft, soft);
        if (i == 12) sink::saveImage((std::filesystem::path(argv[1]) / L"generated-12.png").wstring(), image);
        if (i <= 3 || i == 25 || i == 47) {
            std::cout << "AMF_FRC frame=" << i << " center=" << center << " expected=" << expected << " softPixels=" << soft << " row:";
            for (int x = int(expected - Bar * 0.5) - 12; x < int(expected - Bar * 0.5) + 12; ++x) if (x >= 0 && x < int(image.width)) std::cout << ' ' << value(unsigned(x));
            std::cout << std::endl;
        }
    }
    unsigned errors = 0;
    if (info) for (UINT64 m = 0; m < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++m) {
        SIZE_T size = 0; info->GetMessage(m, nullptr, &size); std::vector<uint8_t> data(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if (SUCCEEDED(info->GetMessage(m, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { ++errors; if (errors <= 5) std::cout << "GPU_ERROR " << message->pDescription << std::endl; }
    }
    check(ok, "48 frames processed with AMD FRC");
    check(resetsWithoutFrame == 2, "a history reset shows no generated frame");
    check(generated >= 40, "a generated frame for every pair after the first");
    check(checked > 0 && midpoint == checked, "every generated frame shows the bar halfway between its neighbours (2 px)");
    check(checked > 0 && sharp == checked, "generated frames move the bar (sharp edges), not blend the two frames");
    check(errors == 0, "no D3D12 debug-layer error");
    std::cout << "AMF_FRC generated=" << generated << " checked=" << checked << " midpoint=" << midpoint << " sharp=" << sharp << " maxSoftPixels=" << maxSoft << " maxErrorPx=" << maxError
              << " debugErrors=" << errors << " pass=" << (failures == 0) << std::endl;
    av_frame_free(&frame);
    ring.drainQueue(); graph.shutdown(); ring.shutdown(); ctx.shutdown(); CoUninitialize();
    return failures ? 1 : 0;
}
