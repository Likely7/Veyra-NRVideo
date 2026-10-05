// EffectChain: conversion fidelity and the ordering rules that both editing
// modes must obey. These are the guards for the multi-NR and node-mode work.
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/GraphDescription.h"
#include <type_traits>
#include <algorithm>
#include <random>
#include <memory>
#include <limits>

#include <cstdio>
#include <string>

using namespace veyra::engine;

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

EnhancementSettings populated() {
    EnhancementSettings s;
    s.revision = 7;
    s.nr = true; s.sr = true; s.lowLatency = true;
    s.model = NrSettings{0.8f, 0.7f, 0.6f, 0.2f, 1, 1, 0};
    s.residual = ResidualSettings{1.4f, 0.8f, 1.1f, 0.9f, 1.2f};
    s.nrRuntime = NrRuntime::Ampere;
    s.nrTemporal = true;
    s.protection.enabled = true;
    s.protection.featherPixels = 12;
    s.protection.regions[1] = {0.1f, 0.2f, 0.3f, 0.4f};
    s.color.enabled = true;
    s.color.exposure = 0.25f;
    s.color.setLutName(L"Kodak.cube");
    s.videoHdr.enabled = true;
    s.videoHdr.peakNits = 1200;
    s.multiplier = 4;
    s.frameGenerationBackend = FrameGenerationBackend::Dlss;
    s.videoSrQuality = 3;
    s.srTarget = veyra::pipeline::SrTarget::Uhd4K;
    // Fields that are not stages: the chain must not disturb them.
    s.audioOffsetMs = 40;
    s.audioSync = AudioSyncMode::Manual;
    s.captureCompatible = true;
    s.exportBitrateMbps = 90;
    s.flow = FlowQuality::Quality;
    return s;
}
} // namespace

__declspec(noinline) void testNodeEditorGraph() {
    auto document = std::make_unique<EffectChain>();
    document->mode = ChainMode::Node; document->nodeCount = 4;
    document->nodes[0].type = EffectType::NrEnhance; document->nodes[0].enabled = true;
    document->nodes[0].nr.model.intensity = 0.73f;
    document->nodes[0].nr.sizePolicy = veyra::pipeline::NrSizePolicy::Native;
    document->nodes[1].type = EffectType::Color; document->nodes[1].enabled = true;
    document->nodes[1].color.exposure = 0.63f;
    document->nodes[1].color.curves[0].points[1].y = 0.8f;
    document->nodes[1].color.setLutName(L"unchanged-reference.cube");
    document->nodes[2].type = EffectType::SuperResolution;
    document->nodes[3].type = EffectType::FrameGeneration;
    NodeGraphLayout graph;
    check(graph.initialize(*document).accepted, "editor: migrate clean legacy linear chain");
    auto output = std::make_unique<EffectChain>();
    check(graph.project(*document, *output).accepted && *output == *document,
          "editor: migration retains exact runtime payload and order");
    const uint32_t nr = graph.ids[0], color = graph.ids[1], sr = graph.ids[2], fg = graph.ids[3];
    auto previous = std::make_unique<EffectChain>(*output);
    uint32_t copy = 999;
    check(graph.duplicate(*document, nr, copy).accepted && copy != nr && copy != 999,
          "editor: duplicate uses new independent ID");
    const int ci = graph.indexOf(*document, copy);
    check(ci >= 0 && document->nodes[ci].enabled &&
          document->nodes[ci].nr == document->nodes[0].nr && graph.next[ci] == 0,
          "editor: NR duplicate keeps all parameters, size and enabled state while detached");
    check(graph.project(*document, *output).accepted && *output == *previous,
          "editor: enabled detached duplicate cannot enter runtime chain");
    document->nodes[ci].nr.model.intensity = 0.17f;
    check(document->nodes[0].nr.model.intensity == 0.73f, "editor: cloned NR parameters are independent");
    check(graph.insertAfter(*document, copy, nr).accepted && graph.project(*document, *output).accepted &&
          output->nodeCount == 5 && output->nodes[1].nr.model.intensity == 0.17f,
          "editor: explicit insertion alone routes detached node into execution");
    check(graph.remove(*document, copy).accepted && graph.project(*document, *output).accepted &&
          *output == *previous && graph.ids[0] == nr && graph.ids[1] == color,
          "editor: deleting active copy bypasses it and preserves original IDs and payload");
    uint32_t nextCopy = 0;
    check(graph.duplicate(*document, color, nextCopy).accepted && nextCopy > copy,
          "editor: removed IDs are never recycled");
    const int cc = graph.indexOf(*document, nextCopy);
    check(document->nodes[cc].color == document->nodes[1].color,
          "editor: Color clone copies complete curve HSL wheel and LUT payload");
    document->nodes[cc].color.exposure = -0.5f;
    check(document->nodes[1].color.exposure == 0.63f, "editor: cloned color is independent");
    const auto beforeGraph = graph;
    auto beforeDocument = std::make_unique<EffectChain>(*document);
    uint32_t untouched = 123;
    check(!graph.duplicate(*document, sr, untouched).accepted && untouched == 123 && graph == beforeGraph &&
          *document == *beforeDocument, "editor: disabled singleton duplication rejected atomically");
    check(!graph.duplicate(*document, fg, untouched).accepted &&
          !graph.duplicate(*document, NodeGraphLayout::Input, untouched).accepted &&
          !graph.remove(*document, NodeGraphLayout::Output).accepted, "editor: singleton and fixed endpoints cannot clone");
    check(!graph.connect(*document, nextCopy, nr).accepted && graph == beforeGraph,
          "editor: competing incoming edge rejected without mutations");
    check(!graph.connect(*document, nextCopy, nextCopy).accepted && graph == beforeGraph,
          "editor: detached self cycle rejected without mutations");
    check(!graph.connect(*document, nextCopy, 123456).accepted && graph == beforeGraph &&
          !graph.connect(*document, NodeGraphLayout::Output, nr).accepted &&
          !graph.connect(*document, nr, NodeGraphLayout::Input).accepted, "editor: invalid endpoint directions rejected");
    check(!graph.insertAfter(*document, nextCopy, fg).accepted && graph == beforeGraph,
          "editor: cannot insert behind even disabled FG");
    check(graph.disconnect(*document, nr).accepted && graph.validate(*document).accepted,
          "editor: partial wire edit remains a valid document");
    check(!graph.project(*document, *output).accepted && *output == *previous,
          "editor: incomplete path retains last runtime output unchanged");
    check(graph.connect(*document, nr, color).accepted && graph.project(*document, *output).accepted,
          "editor: reconnect restores valid runtime chain");
    auto corrupt = graph; corrupt.ids[1] = corrupt.ids[0];
    check(!corrupt.validate(*document).accepted, "editor: persisted duplicate IDs rejected");
    corrupt = graph; corrupt.next[cc] = 99999;
    check(!corrupt.validate(*document).accepted, "editor: persisted dangling edges rejected");
    corrupt = graph; corrupt.nextId = 2;
    check(!corrupt.validate(*document).accepted, "editor: persisted regressed ID allocator rejected");
    check(graph.remove(*document, nextCopy).accepted && graph.project(*document, *output).accepted &&
          *output == *previous, "editor: deleting detached node leaves runtime unchanged");
    for (int i = 0; i < 3; ++i) check(graph.duplicate(*document, nr, copy).accepted, "editor: NR detached copies within limit");
    check(!graph.duplicate(*document, nr, copy).accepted && document->countOf(EffectType::NrEnhance) == 4,
          "editor: all detached and disabled nodes count towards capacity");
    check(graph.project(*document, *output).accepted && *output == *previous,
          "editor: maximum detached copies still consume no runtime slots");
}

void testExistingChainContracts() {
    // 1. Round trip through the chain preserves every stage field.
    {
        const auto before = populated();
        auto after = before;
        fromChain(toChain(before), after);
        check(after.nr == before.nr && after.sr == before.sr, "round trip keeps the stage switches");
        check(after.model == before.model, "round trip keeps the NR model parameters");
        check(after.residual == before.residual, "round trip keeps the residual parameters");
        check(after.nrRuntime == before.nrRuntime, "round trip keeps the NR runtime");
        check(after.nrTemporal == before.nrTemporal, "round trip keeps temporal anti-flicker");
        check(after.lowLatency == before.lowLatency, "round trip keeps the NR-first order");
        check(after.protection.enabled && after.protection.featherPixels == 12 &&
              after.protection.regions[1].right == 0.3f && after.protection.regions[1].bottom == 0.4f, "round trip keeps the protection regions");
        check(after.color.enabled && after.color.lutNameString() == before.color.lutNameString() &&
              after.color.exposure == before.color.exposure, "round trip keeps the colour grade");
        check(after.videoHdr.enabled && after.videoHdr.peakNits == 1200, "round trip keeps Video HDR");
        check(after.multiplier == 4, "round trip keeps the frame-generation multiplier");
        check(after.videoSrQuality == 3, "round trip keeps the SR quality");
        // Non-stage fields are the caller's: fromChain must leave them alone.
        check(after.audioOffsetMs == 40 && after.audioSync == AudioSyncMode::Manual,
              "round trip does not touch audio settings");
        check(after.captureCompatible && after.exportBitrateMbps == 90,
              "round trip does not touch capture or export settings");
        check(after.flow == FlowQuality::Quality, "round trip does not touch the flow quality");
    }

    // 2. A disabled stage stays disabled through the round trip.
    {
        auto before = populated();
        before.nr = false; before.sr = false; before.multiplier = 1;
        before.videoHdr.enabled = false; before.protection.enabled = false;
        auto after = before;
        fromChain(toChain(before), after);
        check(!after.nr && !after.sr && after.multiplier == 1 && !after.videoHdr.enabled &&
              !after.protection.enabled, "round trip keeps disabled stages disabled");
    }

    // 3. List order is the product order and NR defaults to exactly one layer.
    {
        const auto chain = toChain(populated());
        check(chain.nodeCount == 6, "list chain has one node per stage");
        check(chain.nodes[0].type == EffectType::Color &&
              chain.nodes[1].type == EffectType::SuperResolution &&
              chain.nodes[2].type == EffectType::NrEnhance &&
              chain.nodes[3].type == EffectType::Protection &&
              chain.nodes[4].type == EffectType::VideoHdr &&
              chain.nodes[5].type == EffectType::FrameGeneration,
              "list chain is colour, SR, NR, protection, HDR, frame generation");
        check(chain.countOf(EffectType::NrEnhance) == 1, "list chain has a single NR layer");
    }

    // 4. Frame generation must be last. This is what stops the UI from moving it.
    {
        EffectChain chain;
        chain.nodeCount = 2;
        chain.nodes[0] = ChainNode{}; chain.nodes[0].type = EffectType::FrameGeneration; chain.nodes[0].enabled = true;
        chain.nodes[1] = ChainNode{}; chain.nodes[1].type = EffectType::NrEnhance; chain.nodes[1].enabled = true;
        const auto verdict = validateChain(chain);
        check(!verdict.accepted && std::string(verdict.message).find("补帧") != std::string::npos,
              "frame generation before another enabled stage is rejected");

        EffectChain ok;
        ok.nodeCount = 2;
        ok.nodes[0] = ChainNode{}; ok.nodes[0].type = EffectType::NrEnhance; ok.nodes[0].enabled = true;
        ok.nodes[1] = ChainNode{}; ok.nodes[1].type = EffectType::FrameGeneration; ok.nodes[1].enabled = true;
        check(validateChain(ok).accepted, "frame generation last is accepted");

        // A disabled trailing node does not push FG out of last place.
        EffectChain trailing;
        trailing.nodeCount = 3;
        trailing.nodes[0] = ChainNode{}; trailing.nodes[0].type = EffectType::NrEnhance; trailing.nodes[0].enabled = true;
        trailing.nodes[1] = ChainNode{}; trailing.nodes[1].type = EffectType::FrameGeneration; trailing.nodes[1].enabled = true;
        trailing.nodes[2] = ChainNode{}; trailing.nodes[2].type = EffectType::VideoHdr; trailing.nodes[2].enabled = false;
        check(validateChain(trailing).accepted, "a disabled node after frame generation is allowed");
    }

    // 5. Video HDR may only be followed by frame generation.
    {
        EffectChain chain;
        chain.nodeCount = 2;
        chain.nodes[0] = ChainNode{}; chain.nodes[0].type = EffectType::VideoHdr; chain.nodes[0].enabled = true;
        chain.nodes[1] = ChainNode{}; chain.nodes[1].type = EffectType::NrEnhance; chain.nodes[1].enabled = true;
        const auto verdict = validateChain(chain);
        check(!verdict.accepted && std::string(verdict.message).find("HDR") != std::string::npos,
              "a stage after Video HDR is rejected");

        EffectChain ok;
        ok.nodeCount = 3;
        ok.nodes[0] = ChainNode{}; ok.nodes[0].type = EffectType::VideoHdr; ok.nodes[0].enabled = true;
        ok.nodes[1] = ChainNode{}; ok.nodes[1].type = EffectType::FrameGeneration; ok.nodes[1].enabled = true;
        ok.nodes[2] = ChainNode{}; ok.nodes[2].type = EffectType::NrEnhance; ok.nodes[2].enabled = false;
        check(validateChain(ok).accepted, "a disabled stage after Video HDR is allowed");

        // Video HDR is pinned in front of frame generation: an enabled stage
        // between them would have to consume HDR, which only FG can do.
        EffectChain between;
        between.nodeCount = 3;
        between.nodes[0] = ChainNode{}; between.nodes[0].type = EffectType::VideoHdr; between.nodes[0].enabled = true;
        between.nodes[1] = ChainNode{}; between.nodes[1].type = EffectType::Color; between.nodes[1].enabled = true;
        between.nodes[2] = ChainNode{}; between.nodes[2].type = EffectType::FrameGeneration; between.nodes[2].enabled = true;
        check(!validateChain(between).accepted, "an enabled stage between Video HDR and frame generation is rejected");

        // Video HDR after frame generation is also refused.
        EffectChain afterFg;
        afterFg.nodeCount = 2;
        afterFg.nodes[0] = ChainNode{}; afterFg.nodes[0].type = EffectType::FrameGeneration; afterFg.nodes[0].enabled = true;
        afterFg.nodes[1] = ChainNode{}; afterFg.nodes[1].type = EffectType::VideoHdr; afterFg.nodes[1].enabled = true;
        check(!validateChain(afterFg).accepted, "Video HDR after frame generation is rejected");
    }

    // 6. Instance limits come from the catalog.
    {
        EffectChain chain;
        chain.nodeCount = kMaxNrInstances + 1;
        for (uint32_t i = 0; i < chain.nodeCount; ++i) {
            chain.nodes[i] = ChainNode{};
            chain.nodes[i].type = EffectType::NrEnhance;
            chain.nodes[i].enabled = true;
        }
        check(!validateChain(chain).accepted, "more NR layers than the catalog allows is rejected");
        chain.nodeCount = kMaxNrInstances;
        check(validateChain(chain).accepted, "the NR layer ceiling itself is allowed");
        check(effectInfo(EffectType::NrEnhance).repeatable &&
              effectInfo(EffectType::NrEnhance).maxInstances == kMaxNrInstances,
              "the catalog marks NR as repeatable up to the ceiling");
        check(effectInfo(EffectType::FrameGeneration).mustBeLast, "the catalog marks frame generation as last");
        check(effectInfo(EffectType::VideoHdr).justBeforeLast && !effectInfo(EffectType::VideoHdr).mustBeLast,
              "the catalog pins Video HDR in front of frame generation");
        check(!effectInfo(EffectType::NrEnhance).mustBeLast && !effectInfo(EffectType::NrEnhance).justBeforeLast,
              "NR is not pinned to a fixed position");
    }

    // 7. Rebuild rules: shape changes rebuild, live parameters do not.
    {
        const auto base = populated();
        { auto next = base; next.model.intensity = 0.42f;
          check(!requiresGraphRebuild(base, next), "an NR slider does not rebuild"); }
        { auto next = base; next.color.exposure = -0.5f; next.color.saturation = 30;
          check(!requiresGraphRebuild(base, next), "colour values do not rebuild"); }
        { auto next = base; next.protection.regions[0] = {0.2f, 0.2f, 0.5f, 0.5f};
          check(!requiresGraphRebuild(base, next), "protection rectangles do not rebuild"); }
        { auto next = base; next.audioOffsetMs = -30; next.exportBitrateMbps = 12;
          check(!requiresGraphRebuild(base, next), "audio and export fields do not rebuild"); }
        { auto next = base; next.nr = false;
          check(requiresGraphRebuild(base, next), "turning NR off rebuilds"); }
        { auto next = base; next.multiplier = 2;
          check(requiresGraphRebuild(base, next), "changing the multiplier rebuilds"); }
        { auto next = base; next.nrTemporal = false;
          check(requiresGraphRebuild(base, next), "toggling temporal anti-flicker rebuilds"); }
        { auto next = base; next.videoHdr.enabled = false;
          check(requiresGraphRebuild(base, next), "toggling Video HDR rebuilds"); }
        { auto next = base; next.lowLatency = false;
          check(requiresGraphRebuild(base, next), "changing the NR/SR order rebuilds"); }
        { auto next = base; next.color.setLutName(L"Other.cube");
          check(requiresGraphRebuild(base, next), "switching the LUT rebuilds"); }
        { auto next = base; next.nrPolicy = veyra::pipeline::NrSizePolicy::Native;
          check(requiresGraphRebuild(base, next), "changing the NR size policy rebuilds"); }
    }

    {
        static_assert(std::is_trivially_copyable_v<EnhancementSettings>);
        auto settings=populated();settings.additionalColorCount=kMaxColorInstances-1;
        for(unsigned i=0;i<settings.additionalColorCount;++i){
            auto& c=settings.additionalColors[i];c.enabled=i!=2;c.exposure=float(i)*0.2f;
            c.setLutName(L"grade-"+std::to_wstring(i)+L".cube");
        }
        auto chain=toChain(settings);auto roundTrip=settings;fromChain(chain,roundTrip);
        check(validateChain(chain).accepted&&roundTrip==settings,"six color settings round-trip independently including disabled nodes");
        auto edited=settings;edited.additionalColors[3].exposure+=0.2f;
        check(!requiresGraphRebuild(settings,edited)&&settings.sameVideoConfiguration(edited),"extra color uniforms remain live");
        edited.additionalColors[3].setLutName(L"replacement.cube");
        check(requiresGraphRebuild(settings,edited)&&!settings.sameVideoConfiguration(edited),"extra LUT changes require a rebuild");
        edited=settings;edited.additionalColors[2].enabled=true;
        check(requiresGraphRebuild(settings,edited)&&!settings.sameVideoConfiguration(edited),"extra color bypass changes require a rebuild");
        edited=settings;edited.additionalColorCount=6;
        check(!edited.validate().empty(),"a seventh color instance is rejected");
    }
    // P2 regression: the QML list uses this exact chain -> settings -> chain
    // boundary. A disabled first layer must not erase it or its successors.
    {
        EffectChain chain; chain.nodeCount=4;
        for(uint32_t i=0;i<chain.nodeCount;++i){
            auto& n=chain.nodes[i]; n.type=EffectType::NrEnhance; n.enabled=i!=0;
            n.nr.model.intensity=0.2f+0.2f*i; n.nr.residual.total=0.5f+0.2f*i;
            n.nr.temporal=(i%2)!=0;
        }
        EnhancementSettings settings; fromChain(chain,settings);
        settings.protection.enabled=true;settings.protection.regions[0]={0,0,0.3f,0.3f};
        veyra::pipeline::EnhanceGraphDesc desc;
        StageRequest request;request.nr=true;request.width=1920;request.height=1080;
        describeStages(request,settings,desc);
        check(desc.enableNr&&desc.nrLayersModel.size()==3&&desc.nrLayersModel[0]==chain.nodes[1].nr.model&&
              desc.nrLayersModel[2]==chain.nodes[3].nr.model,"only enabled NR layers reach graph creation in list order");
        check(desc.nrLayersTemporal==std::vector<bool>({true,false,true}),"each NR temporal flag reaches the graph");
        check(!desc.nrLayersProtection[0].enabled&&!desc.nrLayersProtection[1].enabled&&
              desc.nrLayersProtection[2]==settings.protection,"list protection belongs to the final NR output");
        auto restored=toChain(settings);
        check(restored.countOf(EffectType::NrEnhance)==4,"four NR list layers survive the settings boundary");
        uint32_t index=0;
        for(uint32_t i=0;i<restored.nodeCount;++i)if(restored.nodes[i].type==EffectType::NrEnhance){
            check(index<4&&restored.nodes[i].nr==chain.nodes[index].nr&&
                  restored.nodes[i].enabled==chain.nodes[index].enabled,"NR payload and bypass survive independently");
            ++index;
        }
        auto editedChain=chain; editedChain.nodes[2].nr.model.tone=0.25f;
        auto edited=settings; fromChain(editedChain,edited);
        check(edited!=settings,"editing a downstream NR layer reaches settings");
        check(!requiresGraphRebuild(settings,edited),"downstream NR uniforms stay live");
        editedChain=chain; editedChain.nodes[2].enabled=false; fromChain(editedChain,edited);
        check(requiresGraphRebuild(settings,edited),"bypassing an NR layer changes the graph shape");
        editedChain=chain; editedChain.nodes[2].nr.temporal=true; fromChain(editedChain,edited);
        check(requiresGraphRebuild(settings,edited),"a downstream temporal toggle rebuilds its resources");
        editedChain=chain; editedChain.nodes[2].nr.runtime=NrRuntime::Community;
        check(!validateChain(editedChain).accepted,"mixed active runtimes cannot silently share one adapter");
        editedChain=chain; editedChain.nodes[2].nr.lowLatencyPairing=true;
        check(!validateChain(editedChain).accepted,"mixed active NR/SR orders cannot be silently flattened");
        for(uint32_t i=0;i<chain.nodeCount;++i)chain.nodes[i].enabled=false;
        fromChain(chain,settings); restored=toChain(settings);
        check(!settings.nr&&restored.countOf(EffectType::NrEnhance)==4,"all-bypassed list retains four editable layers");
        describeStages(request,settings,desc);
        check(!desc.enableNr&&desc.nrLayersModel.empty(),"all-bypassed list clears the reused descriptor");
        settings.nrLayerCount=kMaxNrInstances+1;
        check(!settings.validate().empty(),"oversized settings are rejected before allocation");
    }
    {
        using namespace veyra::pipeline;
        check(ChainNrParams{}.sizePolicy==NrSizePolicy::Realtime,"new NR instances default to 1080p");
        const NrSizePolicy policies[]={NrSizePolicy::Realtime,NrSizePolicy::Native,NrSizePolicy::P480,
            NrSizePolicy::P720,NrSizePolicy::P900,NrSizePolicy::P1440,NrSizePolicy::Auto};
        const unsigned heights[]={1080,2160,480,720,900,1440,1080};
        for(unsigned i=0;i<7;++i){
            EnhancementSettings s;s.nr=true;s.nrPolicy=policies[i];
            auto c=toChain(s);EnhancementSettings restored;fromChain(c,restored);
            check(restored.nrPolicy==policies[i],"legacy flat policy survives chain conversion");
            StageRequest request;request.nr=true;request.width=3840;request.height=2160;
            EnhanceGraphDesc d;describeStages(request,restored,d);
            check(d.nrLayersExtent.size()==1&&d.nrLayersExtent[0].height==heights[i]&&
                  d.workWidth==3840&&d.workHeight==2160,"fixed and Auto fallback policies preserve full output extent");
            request.exportJob=true;describeStages(request,restored,d);
            check(d.nrLayersExtent[0]==Extent{3840,2160}&&!d.nrAutoPoolLayer,"export ignores preview policies and Auto pool");
            request.exportJob=false;request.stillImage=true;describeStages(request,restored,d);
            check(d.nrLayersExtent[0]==Extent{3840,2160}&&!d.nrAutoPoolLayer,"image ignores preview policies and Auto pool");
        }
        EffectChain c;c.nodeCount=4;
        for(unsigned i=0;i<4;++i){c.nodes[i].type=EffectType::NrEnhance;c.nodes[i].enabled=true;
            c.nodes[i].nr.sizePolicy=policies[i+2];}
        EnhancementSettings s;fromChain(c,s);
        StageRequest request;request.nr=true;request.sr=true;request.width=1920;request.height=1080;
        EnhanceGraphDesc d;describeStages(request,s,d);
        check(d.nrLayersExtent==std::vector<Extent>({{852,480},{1280,720},{1600,900},{2560,1440}}),
              "four post-SR instances have distinct internal sizes");
        auto changed=s;changed.nrLayers[3].sizePolicy=NrSizePolicy::Native;
        check(requiresGraphRebuild(s,changed),"downstream resolution change rebuilds resources");
        for(auto& node:c.nodes)node.nr.lowLatencyPairing=true;fromChain(c,s);describeStages(request,s,d);
        check(d.nrLayersExtent.back()==Extent{1920,1080},"NR-first policy never enlarges source input");
        request.exportJob=true;describeStages(request,s,d);
        check(std::all_of(d.nrLayersExtent.begin(),d.nrLayersExtent.end(),[](auto e){return e==Extent{3840,2160};})
              &&!d.nrBeforeSr,"export preserves full-size processing in every instance");
        request.exportJob=false;request.stillImage=true;describeStages(request,s,d);
        check(d.nrLayersExtent.front()==Extent{3840,2160},"still images preserve full processing extent");
        c.nodes[1].nr.sizePolicy=static_cast<NrSizePolicy>(99);c.nodes[1].enabled=false;
        check(!validateChain(c).accepted,"even bypassed layers reject invalid resolution policies");
    }
    {
        auto s = populated();
        auto session = ChainSession::initial(s);
        check(session.valid(), "initial editor session is valid");
        const auto list = session.configurations[0];
        check(session.select(ChainMode::Node), "first node activation clones the list");
        auto& node = session.configurations[1];
        node.videoSrQuality = 3;
        node.chain.firstOf(EffectType::NrEnhance)->nr.model.tone = .123f;
        node.chain.firstOf(EffectType::Color)->color.exposure = 1.25f;
        node.chain.nodes[0].viewX = 321;
        const auto edited = node;
        bool restored = true;
        for (int i = 0; i < 50; ++i) {
            restored = restored && session.select(ChainMode::List) && session.configurations[0] == list;
            restored = restored && session.select(ChainMode::Node) && session.configurations[1] == edited;
        }
        check(restored, "50 mode switches preserve independent chain parameters and layout");
        EnhancementSettings live;
        live.captureCompatible = true; live.captureFlipVertical = true;
        live.audioOffsetMs = 73; live.exportBitrateMbps = 81; live.forceSdrPreview = true;
        edited.apply(live);
        check(live.videoSrQuality == 3 && live.model.tone == .123f && live.color.exposure == 1.25f,
              "configuration restores chain and non-payload SR parameters");
        check(live.captureCompatible && live.captureFlipVertical && live.audioOffsetMs == 73 &&
              live.exportBitrateMbps == 81 && live.forceSdrPreview, "mode change preserves global settings");
        const auto before = session;
        check(!session.select(ChainMode(2)) && session == before, "invalid mode leaves session untouched");
        node.selectedNr = 999;
        check(!session.valid(), "invalid instance selection is rejected");
    }
}

void testExecutionPlans() {
    // Ordered execution planning is a preflight contract, not a GPU test.
    {
        const ChainExecutionRequest request{{1280, 720}, veyra::pipeline::SrTarget::Uhd4K};
        auto s = populated(); s.lowLatency = false;
        const auto fixed = toChain(s);
        ChainExecutionPlan plan;
        check(compileChainExecutionPlan(fixed, request, plan).accepted, "compile default fixed list plan");
        check(plan.stepCount == 6 && plan.steps[0].type == EffectType::Color &&
              plan.steps[1].type == EffectType::SuperResolution &&
              plan.steps[2].type == EffectType::NrEnhance &&
              plan.steps[3].type == EffectType::Protection &&
              plan.steps[4].type == EffectType::VideoHdr &&
              plan.steps[5].type == EffectType::FrameGeneration, "default list order stays Color SR NR Protection HDR FG");
        auto first = fixed; first.firstOf(EffectType::NrEnhance)->nr.lowLatencyPairing = true;
        check(compileChainExecutionPlan(first, request, plan).accepted &&
              plan.steps[1].type == EffectType::NrEnhance && plan.steps[2].type == EffectType::Protection &&
              plan.steps[3].type == EffectType::SuperResolution &&
              plan.steps[1].input == request.source, "preview NR-first list puts NR and protection before SR");
        auto offline = request; offline.exportJob = true;
        check(compileChainExecutionPlan(first, offline, plan).accepted &&
              plan.steps[1].type == EffectType::SuperResolution &&
              plan.steps[2].processing == plan.steps[2].input, "offline list stays SR-first with native NR");
        auto still = request; still.stillImage = true;
        check(compileChainExecutionPlan(first, still, plan).accepted &&
              plan.steps[1].type == EffectType::SuperResolution &&
              plan.steps[2].processing == plan.steps[2].input, "still image list preserves existing native SR-first contract");

        auto node = fixed; node.mode = ChainMode::Node;
        removeLegacyNodeProtection(node);
        std::swap(node.nodes[0], node.nodes[2]); // NR -> SR -> Color
        node.nodes[0].nr.sizePolicy = veyra::pipeline::NrSizePolicy::Native;
        check(compileChainExecutionPlan(node, request, plan).accepted &&
              plan.steps[0].type == EffectType::NrEnhance && plan.steps[0].processing == request.source &&
              plan.steps[2].type == EffectType::Color && plan.steps[2].input.width == 3840,
              "node plan preserves NR before SR and Color after SR");
        auto bypass = request; bypass.source = {3840, 2160};
        check(compileChainExecutionPlan(node, bypass, plan).accepted &&
              plan.resourceCounts[size_t(EffectType::SuperResolution)] == 0 && plan.stepCount == 4,
              "identity SR requests no execution step or resource slot");
        auto layout = node; layout.nodes[0].viewX = 731; layout.nodes[2].viewY = -300;
        ChainExecutionPlan moved;
        compileChainExecutionPlan(node, request, plan);
        check(compileChainExecutionPlan(layout, request, moved).accepted && moved == plan,
              "layout-only edits produce an identical execution plan");
        const auto before = plan;
        auto illegal = node; std::swap(illegal.nodes[0], illegal.nodes[illegal.nodeCount - 1]);
        check(!compileChainExecutionPlan(illegal, request, plan).accepted && plan == before,
              "illegal FG placement rejects without changing the accepted plan");
        auto badSize = request; badSize.source.width = 0;
        check(!compileChainExecutionPlan(node, badSize, plan).accepted && plan == before,
              "invalid source extent rejects atomically");
        node.mode = ChainMode(2);
        check(!compileChainExecutionPlan(node, request, plan).accepted && plan == before,
              "invalid chain mode rejects atomically");
    }
}

void testGeneratedExecutionPlans() {
    {
        // Deterministic varied legal chains: disabled instances, six NR sizes,
        // SR on either side of NR/Color, and optional HDR/FG tails. Verify each
        // step independently rather than merely asking validateChain() twice.
        std::mt19937 rng(0x5032026u);
        bool all = true;
        uint32_t beforeSr = 0, afterSr = 0, disabled = 0, mixedNr = 0;
        for (uint32_t sample = 0; sample < 200; ++sample) {
            EffectChain chain; chain.mode = ChainMode::Node;
            const auto add = [&](EffectType type, bool enabled) -> ChainNode& {
                auto& n = chain.nodes[chain.nodeCount++]; n.type = type; n.enabled = enabled; return n;
            };
            const auto colors = 1u + rng() % kMaxColorInstances;
            const auto nrs = 1u + rng() % kMaxNrInstances;
            for (uint32_t i = 0; i < colors; ++i) add(EffectType::Color, rng() % 4 != 0);
            for (uint32_t i = 0; i < nrs; ++i)
                add(EffectType::NrEnhance, rng() % 4 != 0).nr.sizePolicy = veyra::pipeline::NrSizePolicy(rng() % 6);
            add(EffectType::SuperResolution, sample % 3 != 0);
            std::shuffle(chain.nodes.begin(), chain.nodes.begin() + chain.nodeCount, rng);
            if (sample % 3) add(EffectType::VideoHdr, true);
            if (sample % 4) add(EffectType::FrameGeneration, true);
            const ChainExecutionRequest request{{1280, 720}, veyra::pipeline::SrTarget::Uhd4K};
            ChainExecutionPlan plan;
            bool ok = compileChainExecutionPlan(chain, request, plan).accepted;
            auto extent = request.source;
            std::array<uint32_t, effectTypeCount> parameter{}, resource{};
            uint32_t step = 0, nrBefore = 0, nrAfter = 0; bool scaled = false;
            for (uint32_t i = 0; i < chain.nodeCount; ++i) {
                const auto& n = chain.nodes[i]; const auto type = size_t(n.type);
                const auto ordinal = parameter[type]++;
                if (!n.enabled) { ++disabled; continue; }
                if (step >= plan.stepCount) { ok = false; break; }
                const auto& p = plan.steps[step++];
                ok = ok && p.nodeIndex == i && p.type == n.type && p.parameterIndex == ordinal &&
                    p.resourceIndex == resource[type]++ && p.input == extent;
                if (n.type == EffectType::SuperResolution) { extent = {3840, 2160}; scaled = true; }
                if (n.type == EffectType::NrEnhance) {
                    if (scaled) { ++afterSr; ++nrAfter; } else { ++beforeSr; ++nrBefore; }
                    const auto limit = veyra::pipeline::nrHeightLimit(n.nr.sizePolicy);
                    const auto expectedH = limit ? std::min(limit, extent.height) : extent.height;
                    ok = ok && p.processing.height == expectedH && p.processing.width == ((expectedH * 16 / 9) & ~1u);
                } else ok = ok && p.processing == extent;
                ok = ok && p.output == extent;
            }
            if (nrBefore && nrAfter) ++mixedNr;
            ok = ok && step == plan.stepCount && resource == plan.resourceCounts && plan.output == extent;
            all = all && ok;
            if (!ok) std::printf("FAIL generated chain seed=0x5032026 sample=%u\n", sample);
        }
        std::printf("plan properties: 200 chains; NR before SR=%u after SR=%u mixed=%u disabled=%u\n",
                    beforeSr, afterSr, mixedNr, disabled);
        check(all && beforeSr && afterSr && mixedNr && disabled,
              "200 fixed-seed plans preserve order, instance identity and per-stage extents (CPU only)");
    }
}

void testLegacyPlanDimensions() {
    bool all = true; uint32_t cases = 0;
    const std::array<veyra::pipeline::Extent, 4> sources{{{1280, 720}, {3440, 1440}, {1080, 1920}, {3840, 2160}}};
    for (const auto source : sources)
    for (uint32_t sr = 0; sr < 2; ++sr)
    for (uint32_t first = 0; first < 2; ++first)
    for (uint32_t context = 0; context < 3; ++context)
    for (uint32_t policy = 0; policy < 7; ++policy)
    for (uint32_t target = 0; target < 3; ++target) {
        auto s = populated();
        s.sr = sr != 0; s.lowLatency = first != 0;
        s.nrPolicy = veyra::pipeline::NrSizePolicy(policy);
        s.srTarget = veyra::pipeline::SrTarget(target);
        const auto chain = toChain(s);
        ChainExecutionPlan plan;
        const ChainExecutionRequest request{source, s.srTarget, context == 1, context == 2};
        bool ok = compileChainExecutionPlan(chain, request, plan).accepted;
        StageRequest legacyRequest;
        legacyRequest.width = source.width; legacyRequest.height = source.height;
        legacyRequest.nr = s.nr; legacyRequest.sr = s.sr; legacyRequest.fg = true;
        legacyRequest.stillImage = request.stillImage; legacyRequest.exportJob = request.exportJob;
        veyra::pipeline::EnhanceGraphDesc legacy;
        describeStages(legacyRequest, s, legacy);
        // Independent pre-integration formula: describeStages now consumes the
        // compiler, so comparing only those two would be a circular test.
        const auto oldPlan=veyra::pipeline::ResolutionPlan::make(source,s.sr,
            request.exportJob?veyra::pipeline::NrSizePolicy::Native:s.nrPolicy,
            request.stillImage||request.exportJob,s.revision,s.srTarget,
            !request.exportJob&&s.lowLatency&&s.nr);
        const bool oldNrFirst=!request.stillImage&&!request.exportJob&&s.lowLatency&&s.nr&&oldPlan.srApplied;
        ok = ok && legacy.fixedExecutionPlan.has_value() && legacy.validateFixedExecutionPlan().empty() &&
             plan.output == oldPlan.base && plan.output == veyra::pipeline::Extent{legacy.workWidth, legacy.workHeight} &&
             legacy.nrBeforeSr==oldNrFirst && legacy.nrWidth==oldPlan.nr.width && legacy.nrHeight==oldPlan.nr.height &&
             plan.resourceCounts[size_t(EffectType::SuperResolution)] == uint32_t(oldPlan.srApplied);
        bool checkedNr = false;
        for (uint32_t i = 0; i < plan.stepCount; ++i) {
            const auto& step = plan.steps[i];
            if (step.type != EffectType::NrEnhance) continue;
            const auto expectedInput = oldNrFirst ? source : oldPlan.base;
            ok = ok && step.input == expectedInput && step.output == expectedInput &&
                 step.processing==oldPlan.nr && !legacy.nrLayersExtent.empty() && step.processing == legacy.nrLayersExtent.front();
            checkedNr = true;
        }
        all = all && ok && checkedNr; ++cases;
        if (!ok || !checkedNr) std::printf("FAIL legacy plan dimensions case=%u source=%ux%u sr=%u nrFirst=%u context=%u policy=%u target=%u\n",
            cases, source.width, source.height, sr, first, context, policy, target);
    }
    std::printf("legacy dimension parity: %u cases (CPU descriptions only)\n", cases);
    check(all && cases == 1008, "production fixed plans match independent legacy formulas across 1008 dimension/context combinations");
}

void testProductionFixedPlanDescription() {
    using namespace veyra::pipeline;
    EnhancementSettings s;s.nr=true;s.sr=true;s.nrLayerCount=3;
    s.nrLayers[0].enabled=false;
    s.nrLayers[1].enabled=true;s.nrLayers[1].sizePolicy=NrSizePolicy::Native;
    s.nrLayers[2].enabled=true;s.nrLayers[2].sizePolicy=NrSizePolicy::Realtime;
    StageRequest request;request.width=1280;request.height=720;request.nr=true;request.sr=true;
    EnhanceGraphDesc desc;describeStages(request,s,desc);
    check(desc.fixedExecutionPlan.has_value()&&desc.validateFixedExecutionPlan().empty(),
        "production builder supplies a validated fixed execution plan");
    std::vector<ChainExecutionStep> nr;
    if(desc.fixedExecutionPlan)for(uint32_t i=0;i<desc.fixedExecutionPlan->stepCount;++i)
        if(desc.fixedExecutionPlan->steps[i].type==EffectType::NrEnhance)nr.push_back(desc.fixedExecutionPlan->steps[i]);
    check(nr.size()==2&&nr[0].parameterIndex==1&&nr[0].resourceIndex==0&&
          nr[1].parameterIndex==2&&nr[1].resourceIndex==1,
        "production plan keeps disabled parameter slots distinct from active resources");
    check(desc.nrLayersExtent==std::vector<Extent>{{3840,2160},{1920,1080}},
        "production NR allocation extents come from each compiled active layer");
    desc.nrLayersExtent[0]={640,360};
    check(!desc.validateFixedExecutionPlan().empty(),
        "resource dimensions tampering is rejected before GPU allocation");
    s.lowLatency=true;for(auto& n:s.nrLayers)n.lowLatencyPairing=true;
    describeStages(request,s,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.nrBeforeSr&&
          desc.nrLayersExtent==std::vector<Extent>{{1280,720},{1280,720}},
        "NR-first fixed plan uses source-space input for all active layers");
    request.exportJob=true;describeStages(request,s,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.nrBeforeSr&&
          desc.nrLayersExtent==std::vector<Extent>{{3840,2160},{3840,2160}},
        "offline description retains native NR and default order");
    request.exportJob=false;request.nvidiaAdapter=false;describeStages(request,s,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.enableNr&&!desc.enableSr&&
          desc.fixedExecutionPlan&&desc.fixedExecutionPlan->resourceCounts[size_t(EffectType::NrEnhance)]==0&&
          desc.fixedExecutionPlan->output==Extent{3840,2160},
        "non-NVIDIA fallback retains ordinary resize extent without NR resources");
    // As the engine sends it after AMD normalization: DLSS SR removed, NR kept.
    request.amdNr=true;request.sr=false;describeStages(request,s,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.enableNr&&!desc.enableSr&&desc.fixedExecutionPlan&&
          desc.fixedExecutionPlan->resourceCounts[size_t(EffectType::NrEnhance)]==2,
        "AMD adapter description keeps NR layers for the lmxxf runtime");
    request.amdNr=false;request.sr=true;
    request.nvidiaAdapter=true;request.nr=false;describeStages(request,s,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.enableNr&&desc.fixedExecutionPlan&&
          desc.fixedExecutionPlan->resourceCounts[size_t(EffectType::NrEnhance)]==0,
        "request bypass clears active plan resources on a reused descriptor");
    s.nrLayerCount=0;s.nr=false;request.nr=true;request.sr=false;describeStages(request,s,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.enableNr&&desc.fixedExecutionPlan&&
          desc.fixedExecutionPlan->resourceCounts[size_t(EffectType::NrEnhance)]==1,
        "flat caller request override is preserved rather than silently dropping NR");
    s.nrLayerCount=kMaxNrInstances+1;describeStages(request,s,desc);
    check(!desc.fixedExecutionPlan&&!desc.validateFixedExecutionPlan().empty(),
        "oversized runtime input fails instead of truncating into an apparently valid plan");
    s.nrLayerCount=0;request.width=0;describeStages(request,s,desc);
    check(!desc.fixedExecutionPlan&&!desc.validateFixedExecutionPlan().empty(),
        "invalid source dimensions leave an explicit construction error");
    request.width=1280;describeStages(request,s,desc);
    check(desc.fixedExecutionPlan&&desc.validateFixedExecutionPlan().empty(),
        "valid rebuild clears the preceding plan error");
    s.nr=true;s.color.enabled=true;s.sr=false;s.lowLatency=false;
    describeStages(request,s,desc);
    auto chain=toChain(s);chain.mode=ChainMode::Node;
    removeLegacyNodeProtection(chain);
    std::swap(chain.nodes[0],chain.nodes[2]);
    ChainExecutionPlan ordered;
    check(compileChainExecutionPlan(chain,{{1280,720},s.srTarget,false,false},ordered).accepted,
        "NR-before-color is a legal requested node plan");
    desc.fixedExecutionPlan=ordered;
    check(!desc.validateFixedExecutionPlan().empty(),
        "fixed dispatcher refuses legal but unsupported node order instead of pretending to execute it");
}

void testRuntimeOrderTransport() {
    namespace pipeline = veyra::pipeline;
    EnhancementSettings settings;
    settings.nr=true;settings.sr=true;settings.srTarget=pipeline::SrTarget::Qhd;
    settings.color.enabled=true;settings.nrLayerCount=2;
    settings.nrLayers[0].enabled=false;settings.nrLayers[0].model.intensity=.17f;
    settings.nrLayers[1].enabled=true;settings.nrLayers[1].model.intensity=.73f;
    settings.nrLayers[1].sizePolicy=pipeline::NrSizePolicy::P480;
    auto chain=toChain(settings);
    check(!runtimeOrder(chain),"legacy list keeps the original runtime request contract");
    chain.mode=ChainMode::Node;
    removeLegacyNodeProtection(chain);
    std::rotate(chain.nodes.begin()+1,chain.nodes.begin()+2,chain.nodes.begin()+4);
    fromChain(chain,settings);
    const auto order=runtimeOrder(chain);
    chain.nodes[0].viewX=519;chain.nodes[2].viewY=87;
    check(order==runtimeOrder(chain),"layout is excluded from runtime topology and cannot trigger a rebuild");
    EffectChain restored;
    check(order&&restoreRuntimeOrder(settings,*order,restored).accepted&&
          restored.mode==ChainMode::Node&&restored.nodes[1].type==EffectType::NrEnhance&&
          !restored.nodes[1].enabled&&restored.nodes[1].nr.model.intensity==.17f&&
          restored.nodes[2].enabled&&restored.nodes[2].nr.model.intensity==.73f&&
          restored.nodes[3].type==EffectType::SuperResolution,
          "runtime envelope restores actual order and disabled per-instance parameter slots");
    StageRequest request;request.width=640;request.height=360;request.nr=true;request.sr=true;
    request.nodeOrder=&*order;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
    check(desc.runtimeNodeOrder&&desc.fixedExecutionPlan&&desc.validateFixedExecutionPlan().empty()&&
          desc.nrBeforeSr&&desc.fixedExecutionPlan->steps[1].type==EffectType::NrEnhance&&
          desc.fixedExecutionPlan->steps[1].parameterIndex==1&&
          desc.fixedExecutionPlan->steps[1].resourceIndex==0&&
          desc.fixedExecutionPlan->steps[1].input==pipeline::Extent{640,360},
          "production builder consumes requested node order instead of flattening to SR-first");
    const auto preserved=restored;
    auto broken=*order;broken.types[0]=EffectType::Count;
    check(!restoreRuntimeOrder(settings,broken,restored).accepted&&restored==preserved,
          "invalid runtime order leaves previously accepted chain untouched");
    broken=*order;broken.nodeCount=kMaxChainNodes+1;
    check(!restoreRuntimeOrder(settings,broken,restored).accepted&&restored==preserved,
          "oversized runtime order fails atomically");
    broken=*order;broken.nodeCount=0;
    check(!restoreRuntimeOrder(settings,broken,restored).accepted&&restored==preserved,
          "runtime order cannot silently omit enabled stages or disabled parameter slots");
    std::swap(chain.nodes[0],chain.nodes[2]);fromChain(chain,settings);
    const auto interleaved=runtimeOrder(chain);request.nodeOrder=&*interleaved;
    describeStages(request,settings,desc);
    check(desc.fixedExecutionPlan&&desc.fixedExecutionPlan->steps[0].type==EffectType::NrEnhance&&
          desc.preSrColor(0)&&desc.validateFixedExecutionPlan().empty(),
          "NR -> Color -> SR reaches admission intact and consumes the new pre-SR boundary");
    EffectChain empty;empty.mode=ChainMode::Node;settings={};fromChain(empty,settings);
    check(restoreRuntimeOrder(settings,*runtimeOrder(empty),restored).accepted&&restored.nodeCount==0,
          "empty node configuration does not resurrect synthetic list stages");
}

void testNodeTailColorDescription() {
    using namespace veyra;
    using namespace veyra::engine;
    EffectChain chain;chain.mode=ChainMode::Node;chain.nodeCount=4;
    chain.nodes[0].type=EffectType::Color;chain.nodes[0].enabled=true;chain.nodes[0].color.enabled=true;
    chain.nodes[1].type=EffectType::SuperResolution;chain.nodes[1].enabled=true;
    chain.nodes[2].type=EffectType::NrEnhance;chain.nodes[2].enabled=true;
    chain.nodes[3].type=EffectType::Color;chain.nodes[3].enabled=true;chain.nodes[3].color.enabled=true;
    EnhancementSettings settings;fromChain(chain,settings);
    auto order=runtimeOrder(chain);
    StageRequest request;request.width=640;request.height=360;request.sr=true;request.nr=true;request.nodeOrder=&*order;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.tailColor(0)&&desc.tailColor(1),
          "node Color -> SR -> NR -> Color admits an independently bound tail grade");
    chain.nodes[0].enabled=false;fromChain(chain,settings);order=runtimeOrder(chain);request.nodeOrder=&*order;
    describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.tailColor(0)&&desc.tailColor(1)&&
          desc.fixedExecutionPlan->steps[2].parameterIndex==1&&desc.fixedExecutionPlan->steps[2].resourceIndex==0,
          "disabled ingress Color retains the tail parameter identity without a GPU slot");
    auto broken=desc;broken.fixedExecutionPlan->steps[2].parameterIndex=engine::kMaxColorInstances;
    check(!broken.validateFixedExecutionPlan().empty(),
          "out-of-range tail Color parameter is rejected before GPU allocation");
    std::swap(chain.nodes[2],chain.nodes[3]);fromChain(chain,settings);order=runtimeOrder(chain);request.nodeOrder=&*order;
    describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.preNrColor(1)&&!desc.tailColor(1)&&!desc.nodeColor(0),
          "SR -> Color -> NR uses a distinct pre-NR grade without flattening to ingress or tail");
    chain.nodes[1].type=EffectType::NrEnhance;fromChain(chain,settings);order=runtimeOrder(chain);
    request.sr=false;request.nodeOrder=&*order;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.nodeColor(1)&&
          desc.interNrColorTarget(1)==1&&!desc.preNrColor(1)&&!desc.tailColor(1),
          "NR -> Color -> NR binds the grade to the next compact NR resource, not ingress or tail");
    chain.nodeCount=5;chain.nodes[4]=chain.nodes[3];chain.nodes[3]=chain.nodes[2];
    fromChain(chain,settings);order=runtimeOrder(chain);request.nodeOrder=&*order;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.interNrColorTarget(1)==1&&desc.interNrColorTarget(2)==1,
          "consecutive grades between NR layers keep distinct parameter slots with one downstream target");
    chain.nodeCount=6;chain.nodes[5].type=EffectType::SuperResolution;chain.nodes[5].enabled=true;
    fromChain(chain,settings);order=runtimeOrder(chain);request.sr=true;request.nodeOrder=&*order;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.nrBeforeSr&&desc.interNrColorTarget(1)==1&&
          desc.fixedExecutionPlan->steps[1].output==pipeline::Extent{640,360},
          "NR -> Color -> Color -> NR -> SR keeps inter-NR grades at source extent");
    std::swap(chain.nodes[4],chain.nodes[5]);fromChain(chain,settings);order=runtimeOrder(chain);
    request.nodeOrder=&*order;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.splitNrAcrossSr()&&desc.preSrColor(1)&&desc.preSrColor(2),
          "NR -> Color -> Color -> SR -> NR uses the split NR boundary, not the inter-NR boundary");
}

void testNodePreSrColorDescription() {
    using namespace veyra;
    using namespace veyra::engine;
    EffectChain chain;chain.mode=ChainMode::Node;chain.nodeCount=6;
    chain.nodes[0].type=EffectType::Color;chain.nodes[0].enabled=false;chain.nodes[0].color.enabled=true;
    chain.nodes[1].type=EffectType::NrEnhance;chain.nodes[1].enabled=true;
    chain.nodes[2].type=EffectType::Color;chain.nodes[2].enabled=true;chain.nodes[2].color.enabled=true;
    chain.nodes[3]=chain.nodes[2];
    chain.nodes[4].type=EffectType::SuperResolution;chain.nodes[4].enabled=true;
    chain.nodes[5]=chain.nodes[2];
    EnhancementSettings settings;fromChain(chain,settings);
    auto order=runtimeOrder(chain);
    StageRequest request;request.width=640;request.height=360;request.sr=true;request.nr=true;request.nodeOrder=&*order;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.nrBeforeSr&&desc.preSrColor(1)&&desc.preSrColor(2)&&
          !desc.preSrColor(0)&&!desc.preSrColor(3)&&desc.tailColor(3)&&
          desc.fixedExecutionPlan->steps[1].output==pipeline::Extent{640,360}&&
          desc.fixedExecutionPlan->steps[1].parameterIndex==1&&desc.fixedExecutionPlan->steps[1].resourceIndex==0,
          "NR -> Color -> Color -> SR -> Color preserves source-size pre-SR grades and disabled parameter slots");
    check(!desc.preNrColor(1)&&desc.interNrColorTarget(1)==kMaxNrInstances,
          "pre-SR grade is not mistaken for pre-NR or inter-NR");
    chain.nodeCount=7;chain.nodes[6]=chain.nodes[5];chain.nodes[5]=chain.nodes[4];
    chain.nodes[4]=chain.nodes[3];chain.nodes[3]=chain.nodes[2];
    chain.nodes[2].type=EffectType::Protection;chain.nodes[2].enabled=true;
    check(removeLegacyNodeProtection(chain)==1, "legacy pre-SR protection migrates before execution");
    fromChain(chain,settings);order=runtimeOrder(chain);request.nodeOrder=&*order;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.preSrColor(1)&&desc.preSrColor(2),
          "NR -> Color -> Color -> SR retains its grades after protection migration");
    auto fixed=desc;fixed.runtimeNodeOrder=false;
    check(!fixed.preSrColor(1)&&!fixed.validateFixedExecutionPlan().empty(),
          "pre-SR node admission does not change fixed-list execution");
    request.stillImage=true;describeStages(request,settings,desc);
    check(!desc.validateFixedExecutionPlan().empty(),
          "pre-SR support retains the offline NR-before-SR boundary");
}

void testNodeSplitNrDescription() {
    using namespace veyra;using namespace veyra::engine;
    EffectChain chain;chain.mode=ChainMode::Node;chain.nodeCount=7;
    const EffectType types[]={EffectType::NrEnhance,EffectType::NrEnhance,EffectType::Color,
        EffectType::SuperResolution,EffectType::Color,EffectType::NrEnhance,EffectType::Color};
    for(uint32_t i=0;i<chain.nodeCount;++i){
        chain.nodes[i].type=types[i];chain.nodes[i].enabled=i!=0;
        chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Native;
        chain.nodes[i].color.enabled=true;
    }
    EnhancementSettings settings;fromChain(chain,settings);settings.srTarget=pipeline::SrTarget::Qhd;
    auto order=runtimeOrder(chain);StageRequest request;
    request.width=640;request.height=360;request.sr=true;request.nr=true;request.nodeOrder=&*order;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&desc.splitNrAcrossSr()&&desc.nrBeforeSrLayerCount()==1&&
          desc.nrBeforeSr&&desc.finalNrAfterSr(),
          "split NR has a source-side prefix and a work-side final output");
    check(desc.nrFullExtent(0)==pipeline::Extent{640,360}&&desc.nrFullExtent(1)==pipeline::Extent{2560,1440}&&
          desc.nrLayersExtent==std::vector<pipeline::Extent>{{640,360},{2560,1440}},
          "each native NR allocates the declared input extent on its own side of SR");
    check(desc.preSrColor(0)&&desc.preNrColor(1)&&desc.tailColor(2)&&
          desc.fixedExecutionPlan->steps[0].parameterIndex==1&&desc.fixedExecutionPlan->steps[4].parameterIndex==2,
          "disabled NR preserves parameter identity; boundary grades are neither ingress nor duplicated");
    auto broken=desc;broken.nrLayersExtent[1]={640,360};
    check(!broken.validateFixedExecutionPlan().empty(),
          "a downstream NR cannot reuse the source-side allocation");
    broken=desc;broken.protection.enabled=true;
    check(!broken.validateFixedExecutionPlan().empty(),
          "split protection stays explicitly incomplete until an original-reference stage is supplied");
    broken=desc;broken.noNgx=true;
    check(!broken.validateFixedExecutionPlan().empty(),
          "split graph cannot read unallocated NR intermediates with NGX disabled");
    broken=desc;broken.noFeatures=true;
    check(!broken.validateFixedExecutionPlan().empty(),
          "split graph cannot claim successful execution with all features bypassed");
    request.width=2560;request.height=1440;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.splitNrAcrossSr()&&!desc.enableSr&&
          desc.nrLayersExtent.size()==2&&desc.nrFullExtent(0)==desc.nrFullExtent(1),
          "same-size SR folds away while both NR stages remain at the same declared extent");
    request.stillImage=true;describeStages(request,settings,desc);
    check(!desc.validateFixedExecutionPlan().empty(),
          "split preview does not bypass offline NR-before-SR guard");
}

static void testNodePreSrProtectionDescription(){
    using namespace veyra;using namespace veyra::engine;
    EffectChain chain;chain.mode=ChainMode::Node;chain.nodeCount=7;
    const EffectType types[]={EffectType::NrEnhance,EffectType::NrEnhance,EffectType::Protection,
        EffectType::Color,EffectType::SuperResolution,EffectType::NrEnhance,EffectType::NrEnhance};
    for(uint32_t i=0;i<chain.nodeCount;++i){
        chain.nodes[i].type=types[i];chain.nodes[i].enabled=true;
        chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Native;
        chain.nodes[i].color.enabled=true;
    }
    const auto original = chain;
    int selectedNr = 6, selectedColour = 3;
    check(!validateChain(chain).accepted, "new node chains refuse Protection even at a formerly legal boundary");
    check(removeLegacyNodeProtection(chain, &selectedNr, &selectedColour) == 1 &&
          selectedNr == 5 && selectedColour == 2 && chain.nodes[5] == original.nodes[6] &&
          chain.nodes[2] == original.nodes[3], "legacy cleanup retains payload and remaps selected instances");
    EnhancementSettings settings;fromChain(chain,settings);settings.srTarget=pipeline::SrTarget::Qhd;
    auto order=runtimeOrder(chain);StageRequest request;
    request.width=640;request.height=360;request.sr=true;request.nr=true;request.nodeOrder=&*order;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
    check(desc.validateFixedExecutionPlan().empty()&&!desc.protectionBeforeSr()&&!desc.protection.enabled&&
          desc.nrBeforeSrLayerCount()==2&&desc.preSrColor(0),
          "migrated split chain retains Color and SR without any protection execution");
    check(desc.fixedExecutionPlan->steps[2].input==pipeline::Extent{640,360}&&
          desc.nrFullExtent(2)==pipeline::Extent{2560,1440},
          "migrated Color and post-SR NR retain their declared extents");
    auto legacyDisabled = original; legacyDisabled.nodes[2].enabled = false;
    check(!validateChain(legacyDisabled).accepted && removeLegacyNodeProtection(legacyDisabled) == 1 &&
          legacyDisabled == chain, "disabled legacy Protection is removed too, not hidden or retained");
    auto list = original; list.mode = ChainMode::List; const auto listBefore = list;
    check(removeLegacyNodeProtection(list) == 0 && list == listBefore, "node cleanup never edits list protection");
}

static void testNodeInterNrProtectionDescription(){
    using namespace veyra;using namespace veyra::engine;
    const auto nr=EffectType::NrEnhance,sr=EffectType::SuperResolution;
    const auto mask=EffectType::Protection,color=EffectType::Color;
    for(const auto& types:std::vector<std::vector<EffectType>>{
            {nr,mask,nr},{sr,nr,mask,color,nr},{nr,mask,color,nr,sr,nr},
            {nr,nr,mask,color,nr,sr,nr}}){
        EffectChain chain;chain.mode=ChainMode::Node;chain.nodeCount=uint32_t(types.size());
        uint32_t maskIndex=0,nrPrefix=0;
        for(uint32_t i=0;i<chain.nodeCount;++i){
            chain.nodes[i].type=types[i];chain.nodes[i].enabled=true;
            chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Native;
            chain.nodes[i].color.enabled=true;
            if(types[i]==mask)maskIndex=i;
            if(types[i]==nr&&!maskIndex)++nrPrefix;
        }
        const auto original = chain;
        check(!validateChain(chain).accepted && removeLegacyNodeProtection(chain) == 1,
              "legacy inter-NR Protection must migrate before it can be submitted");
        EnhancementSettings settings;fromChain(chain,settings);settings.srTarget=pipeline::SrTarget::Qhd;
        auto order=runtimeOrder(chain);StageRequest request;
        request.width=640;request.height=360;request.nr=true;
        request.sr=std::find(types.begin(),types.end(),sr)!=types.end();request.nodeOrder=&*order;
        pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
        check(desc.validateFixedExecutionPlan().empty()&&!desc.protection.enabled&&
              desc.interNrProtectionTarget()==kMaxNrInstances,
              "migrated inter-NR chains execute with no Protection resource");
        if(std::find(types.begin(),types.end(),color)!=types.end())
            check(desc.interNrColorTarget(0)==nrPrefix,
                  "surviving inter-NR Color still feeds the next NR");
        check(desc.nrFullExtent(nrPrefix)==desc.fixedExecutionPlan->steps[maskIndex].output,
              "migrated downstream consumer retains its extent");
        auto disabled = original; disabled.nodes[maskIndex].enabled = false;
        check(removeLegacyNodeProtection(disabled) == 1 && disabled == chain,
              "disabled protection migration preserves all other node data");
    }
    EffectChain split;split.mode=ChainMode::Node;split.nodeCount=5;
    const EffectType unsafe[]={nr,sr,nr,mask,nr};
    for(uint32_t i=0;i<split.nodeCount;++i){split.nodes[i].type=unsafe[i];split.nodes[i].enabled=true;}
    EnhancementSettings settings;fromChain(split,settings);auto order=runtimeOrder(split);
    StageRequest request;request.width=640;request.height=360;request.nr=request.sr=true;request.nodeOrder=&*order;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);
    check(!desc.validateFixedExecutionPlan().empty()&&!settings.protection.enabled,
          "unmigrated cross-SR Protection cannot enter production or inherit a list mask");
}

void testNrCorrectionDescription(){
    EnhancementSettings s;s.nr=true;s.nrMotion=MotionSource::Zero;
    veyra::pipeline::EnhanceGraphDesc d;StageRequest request;request.nr=true;request.width=1280;request.height=720;
    describeStages(request,s,d);
    check(!d.nrTemporal&&!d.enableNvofStandalone,"NR correction off retains zero-motion/no-history topology");
    auto corrected=s;corrected.residual.correction.enabled=true;
    check(requiresGraphRebuild(s,corrected),"NR correction switch rebuilds history topology");
    describeStages(request,corrected,d);
    check(d.nrTemporal&&d.enableNvofStandalone,"NR auto requests real flow even with zero model motion");
    auto manual=corrected;manual.residual.correction.automatic=false;manual.residual.correction.stability=.5f;
    check(!requiresGraphRebuild(corrected,manual),"NR manual/auto with history is a live parameter update");
    manual.residual.correction.stability=0;
    check(requiresGraphRebuild(corrected,manual),"NR stability zero releases history via rebuild");
    describeStages(request,manual,d);
    check(!d.nrTemporal&&!d.enableNvofStandalone,"NR manual stability zero needs no history or flow");
    manual.nrTemporal=true;describeStages(request,manual,d);
    check(!d.nrTemporal,"manual correction owns temporal setting while enabled");
    manual.residual.correction.enabled=false;describeStages(request,manual,d);
    check(d.nrTemporal,"switch off restores saved legacy temporal option");
    manual.nrLayerCount=2;manual.nrLayers[0].enabled=true;manual.nrLayers[1].enabled=true;
    manual.nrLayers[1].residual.correction.enabled=true;
    describeStages(request,manual,d);
    check(d.nrLayersTemporal.size()==2&&!d.nrLayersTemporal[0]&&d.nrLayersTemporal[1],
          "controlled history is allocated per active layer");
}
int main() {
    testNrCorrectionDescription();
    std::setvbuf(stdout,nullptr,_IONBF,0);
    testNodeEditorGraph();
    testExistingChainContracts();
    testExecutionPlans();
    testGeneratedExecutionPlans();
    testLegacyPlanDimensions();
    testProductionFixedPlanDescription();
    testRuntimeOrderTransport();
    testNodeTailColorDescription();
    testNodePreSrColorDescription();
    testNodeSplitNrDescription();
    testNodePreSrProtectionDescription();
    testNodeInterNrProtectionDescription();
    std::printf(failures ? "effect chain: %d FAILURES\n" : "effect chain: all checks passed\n", failures);
    return failures ? 1 : 0;
}
