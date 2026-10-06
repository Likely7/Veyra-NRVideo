#pragma once
#include "veyra/engine/ColorSettings.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/VideoHdrSettings.h"
#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <string_view>

namespace veyra::engine {
// The processing chain as data, instead of a fixed set of fields.
//
// Why: the UI has to show, reorder (node mode) and stack (NR layers) the
// stages, and both the preview and the export worker need to carry that
// description across a process boundary. A fixed-capacity array keeps the
// structure trivially copyable, which the export shared memory header asserts.
//
// The structure is additive for now: EnhancementSettings remains the runtime
// input, and toChain()/fromChain() convert between the two. Later engine work
// (multi-NR, ordered executor, node mode) consumes the chain directly.
inline constexpr uint32_t kMaxChainNodes = 16;

enum class EffectType : uint8_t {
    Color = 0,          // tone/LUT grade
    SuperResolution,    // DLSS SR / RTX Video SR / AMD FSR
    NrEnhance,          // one NR instance; repeatable
    Protection,         // keeps NR out of chosen rectangles
    VideoHdr,           // SDR -> HDR conversion
    FrameGeneration,    // must be the last node
    Count
};
constexpr uint32_t effectTypeCount = uint32_t(EffectType::Count);
constexpr std::string_view effectTypeName(EffectType type) {
    switch (type) {
    case EffectType::Color: return "color";
    case EffectType::SuperResolution: return "sr";
    case EffectType::NrEnhance: return "nr";
    case EffectType::Protection: return "protection";
    case EffectType::VideoHdr: return "video-hdr";
    case EffectType::FrameGeneration: return "frame-generation";
    case EffectType::Count: break;
    }
    return "unknown";
}

// One NR instance. Every field is per-instance on purpose: stacking only makes
// sense when each layer can be tuned on its own.
struct ChainNrParams {
    NrSettings model{};
    ResidualSettings residual{};
    NrRuntime runtime = NrRuntime::Original;
    bool temporal = false;          // motion-reprojected residual stabilization
    bool lowLatencyPairing = false; // this layer may run before SR (preview only)
    // Anti-flicker tier, only meaningful when `temporal` is set. Flow is the
    // pre-tier behaviour, so an existing chain keeps doing exactly what it did.
    NrAntiFlicker antiFlicker = NrAntiFlicker::Flow;
    pipeline::NrSizePolicy sizePolicy = pipeline::NrSizePolicy::Realtime;
    bool operator==(const ChainNrParams&) const = default;
};

struct ChainNode {
    EffectType type = EffectType::NrEnhance;
    bool enabled = false;
    uint8_t reserved = 0;
    // Frame coordinates in node mode; ignored in list mode. Node mode keeps its
    // own layout, so they never affect the picture.
    float viewX = 0, viewY = 0;
    // Per-type payload; only the fields that belong to `type` are meaningful.
    ChainNrParams nr{};
    ProtectionSettings protection{};
    ColorSettings color{};
    VideoHdrSettings videoHdr{};
    bool operator==(const ChainNode&) const = default;
};

enum class ChainMode : uint8_t { List = 0, Node = 1 };

// Description of the whole chain plus the settings that are not stages.
// Trivially copyable: no pointers, no containers, no std::string.
struct EffectChain {
    std::array<ChainNode, kMaxChainNodes> nodes{};
    uint32_t nodeCount = 0;
    ChainMode mode = ChainMode::List;
    // Frame-generation detail: the node says whether it runs, this says how many
    // frames it produces. Only meaningful when a frame-generation node is on.
    uint32_t fgMultiplier = 2;
    // Non-stage settings that still belong to a job description (they were in
    // EnhancementSettings before and stay there; copied here for completeness).
    bool fgStrictAdmission = false;
    bool operator==(const EffectChain&) const = default;

    // Counts nodes of one type, regardless of enabled state.
    uint32_t countOf(EffectType type) const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < nodeCount; ++i) if (nodes[i].type == type) ++n;
        return n;
    }
    const ChainNode* firstOf(EffectType type) const {
        for (uint32_t i = 0; i < nodeCount; ++i) if (nodes[i].type == type) return &nodes[i];
        return nullptr;
    }
    ChainNode* firstOf(EffectType type) {
        for (uint32_t i = 0; i < nodeCount; ++i) if (nodes[i].type == type) return &nodes[i];
        return nullptr;
    }
};

// Result of a chain edit. The UI shows `message`; the engine refuses
// `!accepted` edits instead of silently dropping a stage.
struct ChainValidation {
    bool accepted = true;
    const char* message = "";
};

// Rules that apply to both editing modes. Node mode can order stages freely,
// but the physics of the pipeline does not change with the UI.
ChainValidation validateChain(const EffectChain& chain);

// Editor-only topology. Deliberately NOT a field of EffectChain: that structure
// crosses the export worker ABI. Node payloads stay in the existing chain; IDs
// and edges never become GPU resources. 0 is the input/source (and no edge),
// 1 is the output/sink; effect IDs start at 2 and are never recycled.
struct NodeGraphLayout {
    static constexpr uint32_t Input = 0, Output = 1;
    std::array<uint32_t, kMaxChainNodes> ids{}, next{};
    uint32_t inputNext = Output, nextId = 2;
    bool operator==(const NodeGraphLayout&) const = default;

    // Explicit migration from an already cleaned, valid legacy linear chain.
    ChainValidation initialize(const EffectChain&);
    int indexOf(const EffectChain&, uint32_t id) const;
    // Partial/disconnected documents are legal, but duplicate IDs, merges,
    // cycles, invalid endpoints and singleton/capacity violations are not.
    ChainValidation validate(const EffectChain&) const;
    // Only an input-to-output path can be submitted. Failure leaves `output`
    // unchanged so callers can keep the last accepted runtime chain.
    ChainValidation project(const EffectChain&, EffectChain& output) const;
    ChainValidation connect(const EffectChain&, uint32_t from, uint32_t to);
    ChainValidation disconnect(const EffectChain&, uint32_t from);
    ChainValidation duplicate(EffectChain&, uint32_t id, uint32_t& createdId);
    ChainValidation remove(EffectChain&, uint32_t id);
    ChainValidation insertAfter(const EffectChain&, uint32_t id, uint32_t after);
};
// Legacy node chains are ordered arrays, so removing a stage unambiguously
// bypasses it. List chains and all remaining payload/layout fields are retained.
uint32_t removeLegacyNodeProtection(EffectChain&, int* selectedNr = nullptr, int* selectedColour = nullptr);

// In-process topology envelope. Parameters remain in EnhancementSettings;
// editor coordinates and persistent/export-worker layouts are unchanged.
struct ChainRuntimeOrder {
    std::array<EffectType, kMaxChainNodes> types{};
    uint32_t nodeCount = 0;
    bool operator==(const ChainRuntimeOrder&) const = default;
};
std::optional<ChainRuntimeOrder> runtimeOrder(const EffectChain&);
ChainValidation restoreRuntimeOrder(const EnhancementSettings&, const ChainRuntimeOrder&, EffectChain&);

// CPU-side execution contract, not proof that a GPU executor supports it.
// Kept separate from EffectChain/EnhancementSettings so compiling a plan does
// not change their persistent or export-worker shared-memory representation.
struct ChainExecutionRequest {
    pipeline::Extent source{};
    pipeline::SrTarget srTarget = pipeline::SrTarget::Uhd4K;
    bool stillImage = false;
    bool exportJob = false;
    bool amdNrExport = false; // bounded internal model input, full-size output
};
struct ChainExecutionStep {
    EffectType type = EffectType::NrEnhance;
    uint32_t nodeIndex = 0;       // source editor node, never a compacted index
    uint32_t parameterIndex = 0; // ordinal among ALL nodes of this type
    uint32_t resourceIndex = 0;  // ordinal among executing nodes of this type
    pipeline::Extent input{}, processing{}, output{};
    bool operator==(const ChainExecutionStep&) const = default;
};
struct ChainExecutionPlan {
    std::array<ChainExecutionStep, kMaxChainNodes> steps{};
    uint32_t stepCount = 0;
    std::array<uint32_t, effectTypeCount> resourceCounts{};
    pipeline::Extent output{};
    bool operator==(const ChainExecutionPlan&) const = default;
};
// Node mode preserves inter-effect order; list mode uses the existing fixed
// default/NR-first schedules. Each NR size is relative to its actual input,
// not a global nrBeforeSr flag. Disabled nodes and identity SR need no step.
// Failure is atomic. Hardware admission and port connectivity are not inferred
// here: callers must establish those before allocating/executing this plan.
ChainValidation compileChainExecutionPlan(const EffectChain&, const ChainExecutionRequest&, ChainExecutionPlan&);

// Editor state is separate from presets and from source/audio/export settings.
// Only options belonging to the effect chain travel when the mode changes.
struct ChainGlobalSettings {
    pipeline::SrTarget srTarget = pipeline::SrTarget::Uhd4K;
    uint32_t videoSrQuality = 0;
    FrameGenerationBackend fgBackend = FrameGenerationBackend::Dlss;
    uint32_t vfgQuality=1;
    FlowQuality flow = FlowQuality::Balanced;
    OpticalFlowBackend opticalFlowBackend = OpticalFlowBackend::Nvidia;
    bool amdFlowHalfResolution = false;
    pipeline::NrSizePolicy nrPolicy = pipeline::NrSizePolicy::Realtime;
    HdrOutputMode hdrOutputMode=HdrOutputMode::Hdr10;
    MotionSource fgMotion=MotionSource::Automatic,srMotion=MotionSource::OpticalFlow,nrMotion=MotionSource::OpticalFlow;
    // Custom: static HDR output tuning, carried so a session can persist it.
    HdrCurveSettings hdrCurve;
    HdrMetadataSettings hdrMetadata;
    static ChainGlobalSettings capture(const EnhancementSettings&);
    void apply(EnhancementSettings&) const;
    bool operator==(const ChainGlobalSettings&) const = default;
};

struct NodeEditorDocument {
    EffectChain nodes{};
    NodeGraphLayout layout{};
    // Absent in legacy documents: inherit the enclosing runtime configuration.
    std::optional<ChainGlobalSettings> globals;
    bool operator==(const NodeEditorDocument&) const = default;
};

struct ChainConfiguration : ChainGlobalSettings {
    EffectChain chain{};
    // Runtime parameters remain accepted values; an incomplete editor may hold
    // a different immutable draft without changing the running graph.
    std::shared_ptr<const NodeEditorDocument> editor;
    int selectedNr = -1, selectedColour = -1;
    static ChainConfiguration capture(const EffectChain&, const EnhancementSettings&, int nr = -1, int colour = -1);
    void apply(EnhancementSettings&) const;
    bool valid() const;
    bool operator==(const ChainConfiguration&) const;
};

struct ChainSession {
    std::array<ChainConfiguration, 2> configurations{};
    std::array<bool, 2> initialized{true, false};
    ChainMode active = ChainMode::List;
    static ChainSession initial(const EnhancementSettings&);
    // Work on a copy until the caller has accepted the engine transaction.
    bool select(ChainMode);
    bool valid() const;
    bool operator==(const ChainSession&) const = default;
};

// Converts between the chain description and the settings struct the engine
// consumes today. `fromChain` keeps every non-stage field of `base`, so a
// round trip through the chain never loses capture/audio/export settings.
EffectChain toChain(const EnhancementSettings& settings);
void fromChain(const EffectChain& chain, EnhancementSettings& settings);

// Rebuild requirement: true when the difference between two settings structs
// changes the shape of the graph (features, sizes, order, master switches)
// rather than a live uniform. This is the single rule the controller and the
// export job use; it replaces the hand-written field list.
bool requiresGraphRebuild(const EnhancementSettings& previous, const EnhancementSettings& next);

// Effect metadata the UI generates its controls from. Adding an effect means
// registering it here; no UI code changes.
struct EffectParameterInfo {
    const char* id = "";
    const char* label = "";
    enum class Kind : uint8_t { Toggle, Slider, Choice, Text } kind = Kind::Toggle;
    float minimum = 0, maximum = 1, step = 0.01f, defaultValue = 0;
    uint8_t group = 0;      // 0 = common, >0 = collapsible advanced group
    bool rebuild = false;   // changing it needs a graph rebuild, not a live update
    bool experimental = false;
};
struct EffectInfo {
    EffectType type = EffectType::NrEnhance;
    std::string_view id;
    std::string_view label;
    uint32_t maxInstances = 1;
    bool repeatable = false;
    // mustBeLast: the stage runs after the whole chain (frame generation).
    // justBeforeLast: the stage must sit immediately in front of it, because
    // what follows would change the colour domain it just produced (Video HDR).
    // The UI keeps both pinned; neither can be dragged.
    bool mustBeLast = false;
    bool justBeforeLast = false;
    bool changesResolution = false;
    bool experimental = false;
};
const EffectInfo& effectInfo(EffectType type);
const std::array<EffectInfo, effectTypeCount>& effectCatalog();
}
