#pragma once
#include "veyra/engine/ColorSettings.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/VideoHdrSettings.h"
#include <array>
#include <cstdint>
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
inline constexpr uint32_t kMaxNrInstances = 4;
inline constexpr uint32_t kMaxColorInstances = 6;

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
    bool mustBeLast = false;
    bool changesResolution = false;
    bool experimental = false;
};
const EffectInfo& effectInfo(EffectType type);
const std::array<EffectInfo, effectTypeCount>& effectCatalog();
}
