#pragma once
#include "veyra/engine/ColorSettings.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/EnhancementSettings.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace veyra::engine {
// One preset library for everything the user can save: the processing chain,
// the colour grade, frame generation and the audio offset. Which of those a
// preset carries is the user's choice at save time (the UI shows checkboxes),
// so an old "NR preset" is simply a preset whose contents are the chain only.
//
// Two kinds are kept apart: list presets describe the fixed product order, node
// presets a hand-built graph. They are never mixed, because switching modes
// rebuilds the pipeline.
enum class PresetContent : uint32_t {
    None = 0,
    Chain = 1u << 0,          // stages + their parameters
    Color = 1u << 1,          // colour grade
    FrameGeneration = 1u << 2, // multiplier and backend
    Audio = 1u << 3,          // sync mode and offset
};
constexpr uint32_t presetContentMask(PresetContent value) { return uint32_t(value); }
inline constexpr uint32_t kPresetAllContent =
    presetContentMask(PresetContent::Chain) | presetContentMask(PresetContent::Color) |
    presetContentMask(PresetContent::FrameGeneration) | presetContentMask(PresetContent::Audio);

// The frame-generation pair a preset can carry. Deliberately smaller than the
// full EnhancementSettings: a preset must never smuggle in capture or export
// fields.
struct PresetFrameGeneration {
    uint32_t multiplier = 1;
    FrameGenerationBackend backend = FrameGenerationBackend::Dlss;
    bool operator==(const PresetFrameGeneration&) const = default;
};

struct PresetEntry {
    std::wstring name;
    std::wstring note;
    ChainMode kind = ChainMode::List;
    bool builtin = false;        // read-only: can be copied, never renamed or erased
    uint32_t contents = kPresetAllContent;
    EffectChain chain{};
    ColorSettings color{};
    PresetFrameGeneration fg{};
    AudioSyncMode audioSync = AudioSyncMode::Automatic;
    int32_t audioOffsetMs = 0;
    bool operator==(const PresetEntry&) const = default;
};

class PresetLibrary {
public:
    explicit PresetLibrary(std::filesystem::path path) : path_(std::move(path)) {}
    // Missing file is not an error: the library starts with the built-ins.
    bool load();
    bool save();
    bool put(const PresetEntry& entry, bool replace = false);
    bool rename(size_t index, std::wstring name);
    bool duplicate(size_t index);
    bool erase(size_t index);
    bool setDefault(size_t index);
    const std::optional<size_t> defaultIndex() const;
    // Applies the parts the preset carries onto `settings`, leaving everything
    // else (capture, export, resolution policy) untouched.
    static void apply(const PresetEntry& entry, EnhancementSettings& settings);
    // True when the preset can be applied to a settings struct of this kind.
    static bool matchesKind(const PresetEntry& entry, ChainMode kind) { return entry.kind == kind; }
    const std::vector<PresetEntry>& entries() const { return entries_; }
    const std::wstring& error() const { return error_; }
    bool corrupt() const { return corrupt_; }
    // Imports an existing single-purpose store (the old VEYRA_PRESETS files)
    // as chain-only or colour-only presets. Never overwrites an existing name.
    bool importLegacy(const std::vector<PresetEntry>& entries);
private:
    static bool parse(const std::string& data, std::vector<PresetEntry>& out, std::wstring& def, std::wstring& error);
    std::string serialize() const;
    void addBuiltins();
    std::filesystem::path path_;
    std::vector<PresetEntry> entries_;
    std::wstring defaultName_, error_;
    bool corrupt_ = false;
};
}
