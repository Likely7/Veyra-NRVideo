#include "veyra/engine/EffectChain.h"

#include <algorithm>

namespace veyra::engine {
namespace {
constexpr std::array<EffectInfo, effectTypeCount> kCatalog{{
    {EffectType::Color, effectTypeName(EffectType::Color), "调色", kMaxColorInstances, true, false, false, false},
    {EffectType::SuperResolution, effectTypeName(EffectType::SuperResolution), "超分辨率", 1, false, false, true, false},
    {EffectType::NrEnhance, effectTypeName(EffectType::NrEnhance), "NR 画面增强", kMaxNrInstances, true, false, false, true},
    {EffectType::Protection, effectTypeName(EffectType::Protection), "NR 保护区域", 1, false, false, false, false},
    {EffectType::VideoHdr, effectTypeName(EffectType::VideoHdr), "RTX Video HDR", 1, false, false, false, false},
    {EffectType::FrameGeneration, effectTypeName(EffectType::FrameGeneration), "补帧", 1, false, true, false, false},
}};
} // namespace

const EffectInfo& effectInfo(EffectType type) {
    const auto index = size_t(type);
    return kCatalog[index < kCatalog.size() ? index : 0];
}
const std::array<EffectInfo, effectTypeCount>& effectCatalog() { return kCatalog; }

ChainValidation validateChain(const EffectChain& chain) {
    if (chain.nodeCount > kMaxChainNodes) return {false, "链路节点过多"};
    std::array<uint32_t, effectTypeCount> seen{};
    uint32_t lastEnabled = kMaxChainNodes; // index of the last enabled node
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        const auto& node = chain.nodes[i];
        const auto index = size_t(node.type);
        if (index >= effectTypeCount) return {false, "未知效果类型"};
        ++seen[index];
        if (node.enabled) lastEnabled = i;
    }
    for (uint32_t t = 0; t < effectTypeCount; ++t) {
        const auto& info = kCatalog[t];
        if (seen[t] > info.maxInstances) return {false, "该效果数量超出上限"};
    }
    // Frame generation always runs on the chain's final output: the present
    // sink owns the swapchain and the OSD/subtitles are composited after it.
    // The UI refuses to move it, and this is the engine-side guard.
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        if (chain.nodes[i].type == EffectType::FrameGeneration && chain.nodes[i].enabled &&
            i != lastEnabled) {
            return {false, "补帧只能放在链路最后"};
        }
    }
    // Video HDR turns an SDR frame into an HDR one; the stages after it must be
    // able to consume HDR. Only frame generation is allowed to follow.
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        if (chain.nodes[i].type != EffectType::VideoHdr || !chain.nodes[i].enabled) continue;
        for (uint32_t j = i + 1; j < chain.nodeCount; ++j) {
            const auto type = chain.nodes[j].type;
            if (!chain.nodes[j].enabled) continue;
            if (type != EffectType::FrameGeneration) return {false, "RTX Video HDR 之后只能接补帧"};
        }
    }
    return {};
}

EffectChain toChain(const EnhancementSettings& settings) {
    EffectChain chain;
    chain.mode = ChainMode::List;
    chain.fgStrictAdmission = settings.fgStrictAdmission;
    chain.fgMultiplier = std::max(2u, settings.multiplier);
    auto append = [&chain](EffectType type, bool enabled) -> ChainNode& {
        ChainNode& node = chain.nodes[chain.nodeCount < kMaxChainNodes ? chain.nodeCount : kMaxChainNodes - 1];
        if (chain.nodeCount < kMaxChainNodes) ++chain.nodeCount;
        node = ChainNode{};
        node.type = type;
        node.enabled = enabled;
        return node;
    };
    // List order is the product order: colour (fused into the ingest shader),
    // SR, NR layers, protection, Video HDR, frame generation.
    append(EffectType::Color, settings.color.enabled).color = settings.color;
    {
        ChainNode& sr = append(EffectType::SuperResolution, settings.sr);
        // SR has no payload of its own yet: its inputs live in the settings
        // fields videoSrQuality / srTarget, which fromChain() fills back.
        (void)sr;
    }
    {
        ChainNode& nr = append(EffectType::NrEnhance, settings.nr);
        nr.nr.model = settings.model;
        nr.nr.residual = settings.residual;
        nr.nr.runtime = settings.nrRuntime;
        nr.nr.temporal = settings.nrTemporal;
        nr.nr.lowLatencyPairing = settings.lowLatency;
    }
    append(EffectType::Protection, settings.protection.enabled).protection = settings.protection;
    append(EffectType::VideoHdr, settings.videoHdr.enabled).videoHdr = settings.videoHdr;
    append(EffectType::FrameGeneration, settings.multiplier > 1);
    return chain;
}

void fromChain(const EffectChain& chain, EnhancementSettings& settings) {
    // Start from the values the chain does not carry, so callers can pass a
    // settings struct that already has capture/audio/export fields filled in.
    settings.color = {};
    settings.sr = false;
    settings.nr = false;
    settings.model = NrSettings{};
    settings.residual = ResidualSettings{};
    settings.nrRuntime = NrRuntime::Original;
    settings.nrTemporal = false;
    settings.lowLatency = false;
    settings.protection = ProtectionSettings{};
    settings.videoHdr = VideoHdrSettings{};
    settings.multiplier = 1;
    settings.fgStrictAdmission = chain.fgStrictAdmission;
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        const auto& node = chain.nodes[i];
        switch (node.type) {
        case EffectType::Color:
            settings.color = node.color;
            settings.color.enabled = node.enabled;
            break;
        case EffectType::SuperResolution:
            settings.sr = node.enabled;
            break;
        case EffectType::NrEnhance:
            // Only the first NR instance maps back into today's single-NR
            // settings; extra layers need the multi-instance engine work.
            if (!settings.nr) {
                settings.model = node.nr.model;
                settings.residual = node.nr.residual;
                settings.nrRuntime = node.nr.runtime;
                settings.nrTemporal = node.nr.temporal;
                settings.lowLatency = node.nr.lowLatencyPairing;
            }
            settings.nr = settings.nr || node.enabled;
            break;
        case EffectType::Protection:
            settings.protection = node.protection;
            settings.protection.enabled = node.enabled;
            break;
        case EffectType::VideoHdr:
            settings.videoHdr = node.videoHdr;
            settings.videoHdr.enabled = node.enabled;
            break;
        case EffectType::FrameGeneration:
            settings.multiplier = node.enabled ? std::max(2u, chain.fgMultiplier) : 1;
            break;
        case EffectType::Count:
            break;
        }
    }
}

bool requiresGraphRebuild(const EnhancementSettings& previous, const EnhancementSettings& next) {
    // Everything the graph allocates or branches on. Live uniform parameters
    // (NR model/residual sliders, colour values, protection rectangles, audio,
    // export bitrate) are deliberately absent: applySettings() handles them.
    const auto planOf = [](const EnhancementSettings& s) {
        // The real sizes come from ResolutionPlan, which needs the source
        // extent; the caller compares those separately. Here we compare the
        // inputs that decide them.
        return std::array<uint64_t, 8>{
            uint64_t(s.sr), uint64_t(s.nr), uint64_t(s.multiplier),
            uint64_t(s.nrRuntime), uint64_t(s.nrTemporal), uint64_t(s.videoHdr.enabled),
            uint64_t(s.color.enabled), uint64_t(s.videoSrQuality)};
    };
    if (planOf(previous) != planOf(next)) return true;
    if (previous.lowLatency != next.lowLatency) return true;
    if (previous.nrPolicy != next.nrPolicy) return true;
    if (previous.srTarget != next.srTarget) return true;
    if (previous.frameGenerationBackend != next.frameGenerationBackend) return true;
    if (previous.opticalFlowBackend != next.opticalFlowBackend) return true;
    if (previous.amdFlowHalfResolution != next.amdFlowHalfResolution) return true;
    if (previous.flow != next.flow) return true;
    if (previous.captureCompatible != next.captureCompatible) return true;
    if (previous.forceSdrPreview != next.forceSdrPreview) return true;
    if (previous.color.lutNameString() != next.color.lutNameString()) return true;
    // The LUT's input space changes which table the colour stage binds.
    if (previous.color.lutInputSpace != next.color.lutInputSpace) return true;
    // Deeper chain edits (an extra NR layer, a reordered node) need the engine
    // work that lands with the ordered executor; until then the chain's shape
    // is fully determined by the fields above.
    return false;
}
}
