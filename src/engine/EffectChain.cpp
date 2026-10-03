#include "veyra/engine/EffectChain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace veyra::engine {
namespace {
constexpr std::array<EffectInfo, effectTypeCount> kCatalog{{
    {EffectType::Color, effectTypeName(EffectType::Color), "调色", kMaxColorInstances, true, false, false, false, false},
    {EffectType::SuperResolution, effectTypeName(EffectType::SuperResolution), "超分辨率", 1, false, false, false, true, false},
    {EffectType::NrEnhance, effectTypeName(EffectType::NrEnhance), "NR 画面增强", kMaxNrInstances, true, false, false, false, true},
    {EffectType::Protection, effectTypeName(EffectType::Protection), "NR 保护区域", 1, false, false, false, false, false},
    {EffectType::VideoHdr, effectTypeName(EffectType::VideoHdr), "RTX Video HDR", 1, false, false, true, false, false},
    {EffectType::FrameGeneration, effectTypeName(EffectType::FrameGeneration), "补帧", 1, false, true, false, false, false},
}};
} // namespace

const EffectInfo& effectInfo(EffectType type) {
    const auto index = size_t(type);
    return kCatalog[index < kCatalog.size() ? index : 0];
}
const std::array<EffectInfo, effectTypeCount>& effectCatalog() { return kCatalog; }

int NodeGraphLayout::indexOf(const EffectChain& chain, uint32_t id) const {
    if (id < 2 || chain.nodeCount > kMaxChainNodes) return -1;
    for (uint32_t i = 0; i < chain.nodeCount; ++i)
        if (ids[i] == id) return int(i);
    return -1;
}

ChainValidation NodeGraphLayout::initialize(const EffectChain& chain) {
    if (chain.mode != ChainMode::Node) return {false, "只有节点模式使用编辑连接"};
    if (auto v = validateChain(chain); !v.accepted) return v;
    NodeGraphLayout candidate;
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        candidate.ids[i] = candidate.nextId++;
        candidate.next[i] = i + 1 < chain.nodeCount ? candidate.nextId : Output;
    }
    candidate.inputNext = chain.nodeCount ? candidate.ids[0] : Output;
    if (auto v = candidate.validate(chain); !v.accepted) return v;
    *this = candidate;
    return {};
}

ChainValidation NodeGraphLayout::validate(const EffectChain& chain) const {
    if (chain.mode != ChainMode::Node || chain.nodeCount > kMaxChainNodes)
        return {false, "节点编辑文档类型或容量无效"};
    if (nextId < 2) return {false, "节点标识计数器无效"};
    std::array<uint32_t, effectTypeCount> counts{};
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        const auto& n = chain.nodes[i];
        if (ids[i] < 2 || ids[i] >= nextId) return {false, "节点标识无效"};
        for (uint32_t j = 0; j < i; ++j)
            if (ids[i] == ids[j]) return {false, "节点标识重复"};
        if (size_t(n.type) >= effectTypeCount || n.type == EffectType::Protection)
            return {false, "节点模式不支持该效果类型"};
        if (++counts[size_t(n.type)] > effectInfo(n.type).maxInstances)
            return {false, "已存在节点（含未连接节点）超过数量上限"};
        if (!std::isfinite(n.viewX) || !std::isfinite(n.viewY) ||
            std::abs(n.viewX) > 1000000 || std::abs(n.viewY) > 1000000)
            return {false, "节点坐标无效"};
        if (n.type == EffectType::NrEnhance && !pipeline::validNrSizePolicy(n.nr.sizePolicy))
            return {false, "NR 内部处理分辨率非法"};
    }
    const auto validTarget = [&](uint32_t id) { return id <= Output || indexOf(chain, id) >= 0; };
    if (!validTarget(inputNext)) return {false, "输入节点连接目标不存在"};
    std::array<uint32_t, kMaxChainNodes> incoming{};
    uint32_t outputIncoming = 0;
    const auto admit = [&](uint32_t target) {
        if (target == Input) return true;
        if (target == Output) return ++outputIncoming <= 1;
        return ++incoming[size_t(indexOf(chain, target))] <= 1;
    };
    if (!admit(inputNext)) return {false, "输出只允许一个输入"};
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        if (!validTarget(next[i])) return {false, "节点连接目标不存在"};
        if (!admit(next[i])) return {false, "节点只允许一个输入，不支持合流"};
    }
    // Include detached components. Traversal is bounded even for corrupt files.
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        uint32_t id = ids[i], visits = 0;
        while (id > Output) {
            if (++visits > chain.nodeCount) return {false, "节点连接不能形成环"};
            id = next[size_t(indexOf(chain, id))];
        }
    }
    return {};
}

ChainValidation NodeGraphLayout::project(const EffectChain& chain, EffectChain& output) const {
    if (auto v = validate(chain); !v.accepted) return v;
    auto candidate = std::make_unique<EffectChain>();
    candidate->mode = ChainMode::Node;
    candidate->fgMultiplier = chain.fgMultiplier;
    candidate->fgStrictAdmission = chain.fgStrictAdmission;
    uint32_t id = inputNext;
    while (id > Output) {
        const auto i = size_t(indexOf(chain, id));
        candidate->nodes[candidate->nodeCount++] = chain.nodes[i];
        id = next[i];
    }
    if (id != Output) return {false, "输入至输出尚未连接完整，保留上个运行链"};
    if (auto v = validateChain(*candidate); !v.accepted) return v;
    for (uint32_t i = 0; i + 1 < candidate->nodeCount; ++i)
        if (candidate->nodes[i].type == EffectType::FrameGeneration)
            return {false, "补帧节点固定在输出前，禁用时也不能移到链路中间"};
    output = *candidate;
    return {};
}

ChainValidation NodeGraphLayout::connect(const EffectChain& chain, uint32_t from, uint32_t to) {
    if (auto v = validate(chain); !v.accepted) return v;
    const int index = indexOf(chain, from);
    if (from == Output || (from != Input && index < 0) || to == Input ||
        (to != Output && indexOf(chain, to) < 0)) return {false, "连接端点无效"};
    auto candidate = *this;
    if (from == Input) candidate.inputNext = to;
    else candidate.next[size_t(index)] = to;
    if (auto v = candidate.validate(chain); !v.accepted) return v;
    *this = candidate;
    return {};
}

ChainValidation NodeGraphLayout::disconnect(const EffectChain& chain, uint32_t from) {
    if (auto v = validate(chain); !v.accepted) return v;
    if (from == Input) { inputNext = Input; return {}; }
    const int index = indexOf(chain, from);
    if (index < 0) return {false, "固定输出或不存在节点不能断开"};
    next[size_t(index)] = Input;
    return {};
}

ChainValidation NodeGraphLayout::duplicate(EffectChain& chain, uint32_t id, uint32_t& createdId) {
    if (auto v = validate(chain); !v.accepted) return v;
    const int index = indexOf(chain, id);
    if (index < 0) return {false, "固定节点不能复制"};
    const auto type = chain.nodes[size_t(index)].type;
    if (effectInfo(type).maxInstances <= 1 || chain.countOf(type) >= effectInfo(type).maxInstances ||
        chain.nodeCount == kMaxChainNodes) return {false, "单例节点或数量已达上限，不能复制"};
    if (nextId == std::numeric_limits<uint32_t>::max()) return {false, "节点标识已耗尽"};
    auto candidate = std::make_unique<EffectChain>(chain);
    auto layout = *this;
    auto& copy = candidate->nodes[candidate->nodeCount];
    copy = chain.nodes[size_t(index)];
    bool placed = false;
    for (uint32_t attempt = 1; attempt <= kMaxChainNodes + 1; ++attempt) {
        copy.viewX = chain.nodes[size_t(index)].viewX + 280.0f;
        copy.viewY = chain.nodes[size_t(index)].viewY + 300.0f * float(attempt - 1);
        placed = true;
        for (uint32_t i = 0; i < chain.nodeCount; ++i)
            if (std::abs(chain.nodes[i].viewX - copy.viewX) < 260 &&
                std::abs(chain.nodes[i].viewY - copy.viewY) < 300) placed = false;
        if (placed) break;
    }
    if (!placed) return {false, "副本附近没有可用位置"};
    const uint32_t newId = layout.nextId++;
    layout.ids[candidate->nodeCount] = newId;
    layout.next[candidate->nodeCount++] = Input; // detached, with enabled state preserved
    if (auto v = layout.validate(*candidate); !v.accepted) return v;
    chain = *candidate; *this = layout; createdId = newId;
    return {};
}

ChainValidation NodeGraphLayout::remove(EffectChain& chain, uint32_t id) {
    if (auto v = validate(chain); !v.accepted) return v;
    const int index = indexOf(chain, id);
    if (index < 0) return {false, "固定节点或不存在节点不能删除"};
    auto candidate = std::make_unique<EffectChain>(chain);
    auto layout = *this;
    const uint32_t successor = layout.next[size_t(index)];
    if (layout.inputNext == id) layout.inputNext = successor;
    for (uint32_t i = 0; i < chain.nodeCount; ++i)
        if (layout.next[i] == id) layout.next[i] = successor;
    for (uint32_t i = uint32_t(index); i + 1 < chain.nodeCount; ++i) {
        candidate->nodes[i] = candidate->nodes[i + 1];
        layout.ids[i] = layout.ids[i + 1]; layout.next[i] = layout.next[i + 1];
    }
    const uint32_t tail = --candidate->nodeCount;
    candidate->nodes[tail] = ChainNode{}; layout.ids[tail] = layout.next[tail] = 0;
    if (auto v = layout.validate(*candidate); !v.accepted) return v;
    chain = *candidate; *this = layout;
    return {};
}

ChainValidation NodeGraphLayout::insertAfter(const EffectChain& chain, uint32_t id, uint32_t after) {
    if (auto v = validate(chain); !v.accepted) return v;
    const int index = indexOf(chain, id), anchor = indexOf(chain, after);
    if (index < 0 || id == after || after == Output || (after != Input && anchor < 0))
        return {false, "插入端点无效"};
    if (chain.nodes[size_t(index)].type == EffectType::FrameGeneration ||
        (anchor >= 0 && chain.nodes[size_t(anchor)].type == EffectType::FrameGeneration))
        return {false, "补帧固定末位，不能在其后插入"};
    auto candidate = *this;
    const uint32_t oldNext = candidate.next[size_t(index)];
    if (candidate.inputNext == id) candidate.inputNext = oldNext;
    for (uint32_t i = 0; i < chain.nodeCount; ++i)
        if (candidate.next[i] == id) candidate.next[i] = oldNext;
    auto& edge = after == Input ? candidate.inputNext : candidate.next[size_t(anchor)];
    candidate.next[size_t(index)] = edge; edge = id;
    if (auto v = candidate.validate(chain); !v.accepted) return v;
    auto projected = std::make_unique<EffectChain>();
    if (auto v = candidate.project(chain, *projected); !v.accepted) return v;
    *this = candidate;
    return {};
}

uint32_t removeLegacyNodeProtection(EffectChain& chain, int* selectedNr, int* selectedColour) {
    if (chain.mode != ChainMode::Node || chain.nodeCount > kMaxChainNodes) return 0;
    const int oldNr = selectedNr ? *selectedNr : -1;
    const int oldColour = selectedColour ? *selectedColour : -1;
    uint32_t count = 0;
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        if (chain.nodes[i].type == EffectType::Protection) {
            if (selectedNr && oldNr == int(i)) *selectedNr = -1;
            if (selectedColour && oldColour == int(i)) *selectedColour = -1;
            continue;
        }
        if (selectedNr && oldNr == int(i)) *selectedNr = int(count);
        if (selectedColour && oldColour == int(i)) *selectedColour = int(count);
        chain.nodes[count++] = chain.nodes[i];
    }
    const auto removed = chain.nodeCount - count;
    for (uint32_t i = count; i < chain.nodeCount; ++i) chain.nodes[i] = {};
    chain.nodeCount = count;
    return removed;
}

ChainValidation validateChain(const EffectChain& chain) {
    if (chain.nodeCount > kMaxChainNodes) return {false, "链路节点过多"};
    std::array<uint32_t, effectTypeCount> seen{};
    uint32_t lastEnabled = kMaxChainNodes; // index of the last enabled node
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        const auto& node = chain.nodes[i];
        const auto index = size_t(node.type);
        if (index >= effectTypeCount) return {false, "未知效果类型"};
        if (chain.mode == ChainMode::Node && node.type == EffectType::Protection)
            return {false, "节点模式已取消 NR 保护区域，请迁移旧节点配置"};
        if(node.type==EffectType::NrEnhance&&!pipeline::validNrSizePolicy(node.nr.sizePolicy))
            return {false, "NR 内部处理分辨率非法"};
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
    // Video HDR turns an SDR frame into an HDR one, and the stages after it
    // would need HDR-aware handling that only frame generation has. It is
    // therefore pinned immediately in front of frame generation: no other
    // enabled stage may sit between them, and nothing else may follow it.
    {
        int32_t hdrIndex = -1, fgIndex = -1;
        for (uint32_t i = 0; i < chain.nodeCount; ++i) {
            if (!chain.nodes[i].enabled) continue;
            if (chain.nodes[i].type == EffectType::VideoHdr) hdrIndex = int32_t(i);
            if (chain.nodes[i].type == EffectType::FrameGeneration) fgIndex = int32_t(i);
        }
        if (hdrIndex >= 0) {
            if (fgIndex >= 0 && hdrIndex > fgIndex) return {false, "RTX Video HDR 只能放在补帧前面"};
            for (uint32_t j = uint32_t(hdrIndex) + 1; j < chain.nodeCount; ++j) {
                if (!chain.nodes[j].enabled) continue;
                if (chain.nodes[j].type != EffectType::FrameGeneration) return {false, "RTX Video HDR 之后只能接补帧"};
            }
        }
    }
    const ChainNrParams* firstNr=nullptr;
    for(uint32_t i=0;i<chain.nodeCount;++i){
        const auto& node=chain.nodes[i];
        if(node.type!=EffectType::NrEnhance||!node.enabled)continue;
        if(firstNr&&(node.nr.runtime!=firstNr->runtime||node.nr.lowLatencyPairing!=firstNr->lowLatencyPairing))
            return {false, "启用的 NR 层必须使用同一运行时和 NR/SR 顺序"};
        firstNr=&node.nr;
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
    for(uint32_t i=0;i<std::min(settings.additionalColorCount,kMaxColorInstances-1);++i)
        append(EffectType::Color,settings.additionalColors[i].enabled).color=settings.additionalColors[i];
    {
        ChainNode& sr = append(EffectType::SuperResolution, settings.sr);
        // SR has no payload of its own yet: its inputs live in the settings
        // fields videoSrQuality / srTarget, which fromChain() fills back.
        (void)sr;
    }
    for(uint32_t i=0;i<std::min(settings.nrLayerCount?settings.nrLayerCount:1u,kMaxNrInstances);++i){
        const auto layer=settings.nrLayer(i);
        ChainNode& nr = append(EffectType::NrEnhance, layer.enabled);
        nr.nr.model = layer.model;
        nr.nr.residual = layer.residual;
        nr.nr.runtime = layer.runtime;
        nr.nr.temporal = layer.temporal;
        nr.nr.lowLatencyPairing = layer.lowLatencyPairing;
        nr.nr.sizePolicy = layer.sizePolicy;
    }
    append(EffectType::Protection, settings.protection.enabled).protection = settings.protection;
    append(EffectType::VideoHdr, settings.videoHdr.enabled).videoHdr = settings.videoHdr;
    append(EffectType::FrameGeneration, settings.multiplier > 1);
    return chain;
}

std::optional<ChainRuntimeOrder> runtimeOrder(const EffectChain& chain) {
    if (chain.mode == ChainMode::List) return std::nullopt;
    ChainRuntimeOrder order;
    order.nodeCount = chain.nodeCount;
    for (uint32_t i = 0; i < std::min(chain.nodeCount, kMaxChainNodes); ++i)
        order.types[i] = chain.nodes[i].type;
    return order;
}

ChainValidation restoreRuntimeOrder(const EnhancementSettings& settings, const ChainRuntimeOrder& order,
                                    EffectChain& output) {
    if (order.nodeCount > kMaxChainNodes || settings.nrLayerCount > kMaxNrInstances ||
        settings.additionalColorCount >= kMaxColorInstances)
        return {false, "运行节点顺序或实例数量无效"};
    const auto source = toChain(settings);
    EffectChain restored;
    restored.mode = ChainMode::Node;
    restored.nodeCount = order.nodeCount;
    restored.fgMultiplier = source.fgMultiplier;
    restored.fgStrictAdmission = source.fgStrictAdmission;
    std::array<bool, kMaxChainNodes> used{};
    for (uint32_t i = 0; i < order.nodeCount; ++i) {
        if (uint32_t(order.types[i]) >= effectTypeCount) return {false, "运行节点类型无效"};
        uint32_t index = 0;
        while (index < source.nodeCount && (used[index] || source.nodes[index].type != order.types[i])) ++index;
        if (index == source.nodeCount) return {false, "运行节点顺序与实例参数数量不匹配"};
        used[index] = true;
        restored.nodes[i] = source.nodes[index];
    }
    for (uint32_t i = 0; i < source.nodeCount; ++i) {
        if (used[i]) continue;
        const auto& node = source.nodes[i];
        if (node.enabled || (node.type == EffectType::NrEnhance && settings.nrLayerCount) ||
            (node.type == EffectType::Color && settings.additionalColorCount))
            return {false, "运行节点顺序遗漏实例参数"};
    }
    const auto valid = validateChain(restored);
    if (!valid.accepted) return valid;
    output = restored;
    return {};
}

void fromChain(const EffectChain& chain, EnhancementSettings& settings) {
    // Start from the values the chain does not carry, so callers can pass a
    // settings struct that already has capture/audio/export fields filled in.
    settings.color = {};
    settings.additionalColors = {};
    settings.additionalColorCount = 0;
    bool firstColor = true;
    settings.sr = false;
    settings.nr = false;
    settings.nrLayers = {};
    settings.nrLayerCount = 0;
    settings.model = NrSettings{};
    settings.residual = ResidualSettings{};
    // An empty node graph must retain the shared runtime choice. Otherwise a
    // fresh RTX40 SF-v2 default becomes Lecram when its first NR is added.
    settings.nrTemporal = false;
    settings.lowLatency = false;
    settings.protection = ProtectionSettings{};
    settings.videoHdr = VideoHdrSettings{};
    settings.multiplier = 1;
    settings.fgStrictAdmission = chain.fgStrictAdmission;
    for (uint32_t i = 0; i < std::min(chain.nodeCount,kMaxChainNodes); ++i) {
        const auto& node = chain.nodes[i];
        switch (node.type) {
        case EffectType::Color:
            if(firstColor){
                settings.color=node.color;
                settings.color.enabled=node.enabled;
                firstColor=false;
            }else if(settings.additionalColorCount<settings.additionalColors.size()){
                auto& color=settings.additionalColors[settings.additionalColorCount++];
                color=node.color;
                color.enabled=node.enabled;
            }
            break;
        case EffectType::SuperResolution:
            settings.sr = node.enabled;
            break;
        case EffectType::NrEnhance:
            if(settings.nrLayerCount>=kMaxNrInstances)break;
            settings.nrLayers[settings.nrLayerCount]={node.nr.model,node.nr.residual,
                node.nr.runtime,node.nr.temporal,node.nr.lowLatencyPairing,node.enabled,
                node.nr.antiFlicker,node.nr.sizePolicy};
            if (settings.nrLayerCount==0||(!settings.nr&&node.enabled)) {
                settings.model = node.nr.model;
                settings.residual = node.nr.residual;
                settings.nrRuntime = node.nr.runtime;
                settings.nrTemporal = node.nr.temporal;
                settings.lowLatency = node.nr.lowLatencyPairing;
                settings.nrPolicy = node.nr.sizePolicy;
            }
            settings.nr = settings.nr || node.enabled;
            ++settings.nrLayerCount;
            break;
        case EffectType::Protection:
            if (chain.mode == ChainMode::List) {
                settings.protection = node.protection;
                settings.protection.enabled = node.enabled;
            }
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
    if(settings.nrLayerCount<=1){settings.nrLayers={};settings.nrLayerCount=0;}
}

ChainValidation compileChainExecutionPlan(const EffectChain& chain, const ChainExecutionRequest& request,
                                         ChainExecutionPlan& result) {
    if (chain.mode != ChainMode::List && chain.mode != ChainMode::Node) return {false, "未知链路模式"};
    const auto validation = validateChain(chain);
    if (!validation.accepted) return validation;
    if (!request.source.valid() || !pipeline::validSrTarget(request.srTarget))
        return {false, "执行计划尺寸或超分目标非法"};

    // Retain storage ordinals even when an earlier instance is disabled. This
    // is how the existing settings arrays address parameters; GPU resources
    // instead use compact active ordinals. Confusing them grades the wrong
    // Color or applies the wrong NR model after bypassing the first instance.
    std::array<uint32_t, kMaxChainNodes> parameterIndices{};
    std::array<uint32_t, effectTypeCount> parameters{};
    bool nrFirst = false;
    for (uint32_t i = 0; i < chain.nodeCount; ++i) {
        const auto& node = chain.nodes[i];
        parameterIndices[i] = parameters[size_t(node.type)]++;
        if (node.enabled && node.type == EffectType::NrEnhance) nrFirst = node.nr.lowLatencyPairing;
    }
    nrFirst = nrFirst && !request.stillImage && !request.exportJob;
    const auto srExtent = pipeline::ResolutionPlan::make(request.source, true,
        pipeline::NrSizePolicy::Native, true, 0, request.srTarget).base;
    ChainExecutionPlan plan;
    auto cursor = request.source;
    const auto append = [&](uint32_t index) {
        const auto& node = chain.nodes[index];
        if (!node.enabled) return;
        if (node.type == EffectType::SuperResolution && cursor == srExtent) return;
        ChainExecutionStep step;
        step.type = node.type; step.nodeIndex = index;
        step.parameterIndex = parameterIndices[index];
        step.resourceIndex = plan.resourceCounts[size_t(node.type)]++;
        step.input = step.processing = step.output = cursor;
        if (node.type == EffectType::SuperResolution) step.processing = step.output = srExtent;
        if (node.type == EffectType::NrEnhance) {
            const auto policy = request.exportJob ? pipeline::NrSizePolicy::Native : node.nr.sizePolicy;
            step.processing = pipeline::ResolutionPlan::make(cursor, false, policy,
                request.stillImage || request.exportJob, 0, request.srTarget).nr;
        }
        plan.steps[plan.stepCount++] = step;
        cursor = step.output;
    };
    if (chain.mode == ChainMode::Node) {
        for (uint32_t i = 0; i < chain.nodeCount; ++i) append(i);
    } else {
        const std::array<EffectType, effectTypeCount> order = nrFirst
            ? std::array{EffectType::Color, EffectType::NrEnhance, EffectType::Protection,
                EffectType::SuperResolution, EffectType::VideoHdr, EffectType::FrameGeneration}
            : std::array{EffectType::Color, EffectType::SuperResolution, EffectType::NrEnhance,
                EffectType::Protection, EffectType::VideoHdr, EffectType::FrameGeneration};
        for (const auto type : order)
            for (uint32_t i = 0; i < chain.nodeCount; ++i)
                if (chain.nodes[i].type == type) append(i);
    }
    plan.output = cursor;
    result = plan;
    return {};
}

ChainGlobalSettings ChainGlobalSettings::capture(const EnhancementSettings& s) {
    ChainGlobalSettings c;
    c.srTarget = s.srTarget; c.videoSrQuality = s.videoSrQuality;
    c.fgBackend = s.frameGenerationBackend; c.flow = s.flow;
    c.opticalFlowBackend = s.opticalFlowBackend; c.amdFlowHalfResolution = s.amdFlowHalfResolution;
    c.nrPolicy = s.nrPolicy;
    c.hdrOutputMode=s.hdrOutputMode;c.fgMotion=s.fgMotion;c.srMotion=s.srMotion;c.nrMotion=s.nrMotion;
    c.hdrBrightness=s.hdrBrightness;
    return c;
}

void ChainGlobalSettings::apply(EnhancementSettings& s) const {
    s.nrPolicy = nrPolicy;
    s.hdrOutputMode=hdrOutputMode;s.fgMotion=fgMotion;s.srMotion=srMotion;s.nrMotion=nrMotion;
    s.hdrBrightness=hdrBrightness;
    s.srTarget = srTarget; s.videoSrQuality = videoSrQuality;
    s.frameGenerationBackend = fgBackend; s.flow = flow;
    s.opticalFlowBackend = opticalFlowBackend; s.amdFlowHalfResolution = amdFlowHalfResolution;
}

ChainConfiguration ChainConfiguration::capture(const EffectChain& chain, const EnhancementSettings& s, int nr, int colour) {
    ChainConfiguration c;
    static_cast<ChainGlobalSettings&>(c) = ChainGlobalSettings::capture(s);
    c.chain = chain; c.selectedNr = nr; c.selectedColour = colour;
    return c;
}

void ChainConfiguration::apply(EnhancementSettings& s) const {
    s.nrPolicy = nrPolicy;
    fromChain(chain, s);
    ChainGlobalSettings::apply(s);
}

bool ChainConfiguration::operator==(const ChainConfiguration& other) const {
    return chain == other.chain && srTarget == other.srTarget && videoSrQuality == other.videoSrQuality &&
        fgBackend == other.fgBackend && flow == other.flow && opticalFlowBackend == other.opticalFlowBackend &&
        amdFlowHalfResolution == other.amdFlowHalfResolution && nrPolicy == other.nrPolicy &&
        // The author's own rendering globals had the same blind spot: a change to
        // the HDR output mode or to a motion source compared equal, so
        // persistSession() took its early return and the choice was lost on the
        // next start (field report: those two never came back).
        hdrOutputMode == other.hdrOutputMode && fgMotion == other.fgMotion &&
        srMotion == other.srMotion && nrMotion == other.nrMotion &&
        selectedNr == other.selectedNr && selectedColour == other.selectedColour &&
        // Custom: the globals are not compared by the original, so a change to one of
        // them looked like "no change" and persistSession() skipped the write
        // (field report: HDR brightness values were never remembered). Compare the
        // block this fork added; the author's own globals have the same blind spot.
        hdrBrightness.enabled == other.hdrBrightness.enabled && hdrBrightness.strength == other.hdrBrightness.strength &&
        hdrBrightness.targetPeakNits == other.hdrBrightness.targetPeakNits && hdrBrightness.response == other.hdrBrightness.response &&
        hdrBrightness.transitionMs == other.hdrBrightness.transitionMs &&
        ((!editor && !other.editor) || (editor && other.editor && *editor == *other.editor));
}

bool ChainConfiguration::valid() const {
    if (chain.mode != ChainMode::List && chain.mode != ChainMode::Node) return false;
    if (editor) {
        if (chain.mode != ChainMode::Node || !editor->layout.validate(editor->nodes).accepted) return false;
        const auto globals = editor->globals.value_or(static_cast<const ChainGlobalSettings&>(*this));
        EnhancementSettings globalCheck;
        globals.apply(globalCheck);
        if (!globalCheck.validate().empty()) return false;
        // Detached parameters must still be valid. Validate them independently
        // of ordering and without mutating enabled flags in the document.
        for (uint32_t i = 0; i < editor->nodes.nodeCount; ++i) {
            auto single = std::make_unique<EffectChain>();
            single->mode = ChainMode::Node; single->nodeCount = 1;
            single->nodes[0] = editor->nodes.nodes[i]; single->fgMultiplier = editor->nodes.fgMultiplier;
            if (!validateChain(*single).accepted) return false;
            auto parameters = std::make_unique<EnhancementSettings>();
            fromChain(*single, *parameters);
            globals.apply(*parameters);
            if (!parameters->validate().empty()) return false;
        }
    }
    if (!validateChain(chain).accepted || chain.fgMultiplier < 2 || chain.fgMultiplier > 6 ||
        !pipeline::validNrSizePolicy(nrPolicy)) return false;
    const auto& selectable = editor ? editor->nodes : chain;
    const auto selectionOk = [&](int index, EffectType type) {
        return index == -1 || (index >= 0 && uint32_t(index) < selectable.nodeCount && selectable.nodes[index].type == type);
    };
    if (!selectionOk(selectedNr, EffectType::NrEnhance) || !selectionOk(selectedColour, EffectType::Color)) return false;
    for (uint32_t i = 0; i < chain.nodeCount; ++i)
        if (!std::isfinite(chain.nodes[i].viewX) || !std::isfinite(chain.nodes[i].viewY)) return false;
    EnhancementSettings s;
    apply(s);
    return s.validate().empty();
}

ChainSession ChainSession::initial(const EnhancementSettings& settings) {
    ChainSession result;
    result.configurations[0] = ChainConfiguration::capture(toChain(settings), settings);
    result.configurations[1].chain.mode = ChainMode::Node;
    return result;
}

bool ChainSession::valid() const {
    if (active != ChainMode::List && active != ChainMode::Node) return false;
    if (!initialized[size_t(active)] || !initialized[0]) return false;
    for (size_t i = 0; i < configurations.size(); ++i)
        if (configurations[i].chain.mode != ChainMode(i) || !configurations[i].valid()) return false;
    return true;
}

bool ChainSession::select(ChainMode mode) {
    if ((mode != ChainMode::List && mode != ChainMode::Node) || !valid()) return false;
    const auto target = size_t(mode);
    if (!initialized[target]) {
        configurations[target] = configurations[size_t(active)];
        configurations[target].chain.mode = mode;
        removeLegacyNodeProtection(configurations[target].chain,
            &configurations[target].selectedNr, &configurations[target].selectedColour);
        initialized[target] = true;
    }
    active = mode;
    return true;
}

bool requiresGraphRebuild(const EnhancementSettings& previous, const EnhancementSettings& next) {
    if(!previous.sameNrTopology(next))return true;
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
    // The output stabiliser's pass exists only when it is on (EnhanceGraph::createNrHoldPass).
    if ((previous.nrHoldStrength > 0.0f) != (next.nrHoldStrength > 0.0f)) return true;
    if (previous.nrPolicy != next.nrPolicy) return true;
    if (previous.srTarget != next.srTarget) return true;
    if (previous.frameGenerationBackend != next.frameGenerationBackend) return true;
    if (previous.opticalFlowBackend != next.opticalFlowBackend) return true;
    if (previous.amdFlowHalfResolution != next.amdFlowHalfResolution) return true;
    if (previous.flow != next.flow) return true;
    if (previous.captureCompatible != next.captureCompatible) return true;
    if (previous.forceSdrPreview != next.forceSdrPreview) return true;
    // Custom: switching HDR sources onto the RTX Video HDR route changes the
    // working space and whether the TrueHDR feature is created at all, so it has
    // to rebuild the graph rather than be applied live (same as videoHdr.enabled
    // in planOf above).
    if (previous.videoHdr.convertHdrSource != next.videoHdr.convertHdrSource) return true;
    if (previous.hdrOutputMode != next.hdrOutputMode || previous.fgMotion != next.fgMotion ||
        previous.srMotion != next.srMotion || previous.nrMotion != next.nrMotion) return true;
    if (previous.color.lutNameString() != next.color.lutNameString()) return true;
    // The LUT's input space changes which table the colour stage binds.
    if (previous.color.lutInputSpace != next.color.lutInputSpace) return true;
    if(previous.additionalColorCount!=next.additionalColorCount)return true;
    for(uint32_t i=0;i<std::min(next.additionalColorCount,kMaxColorInstances-1);++i){
        const auto& a=previous.additionalColors[i];
        const auto& b=next.additionalColors[i];
        if(a.enabled!=b.enabled||a.lutName!=b.lutName||a.lutInputSpace!=b.lutInputSpace)return true;
    }
    // Arbitrary inter-effect node ordering remains the separate P3 executor.
    return false;
}
}
