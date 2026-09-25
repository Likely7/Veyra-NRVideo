// EffectChain: conversion fidelity and the ordering rules that both editing
// modes must obey. These are the guards for the multi-NR and node-mode work.
#include "veyra/engine/EffectChain.h"

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
    s.nrRuntime = NrRuntime::Community;
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

int main() {
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

    std::printf(failures ? "effect chain: %d FAILURES\n" : "effect chain: all checks passed\n", failures);
    return failures ? 1 : 0;
}
