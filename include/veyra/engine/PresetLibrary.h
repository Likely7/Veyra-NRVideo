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
    uint32_t vfgQuality = 1;
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
    // Version 4 node presets retain the complete draft and last accepted chain.
    // Optional so legacy/list presets keep their original representation.
    std::optional<ChainConfiguration> nodeConfiguration;
    // v6: chain rendering choices also travel with list presets. No capture,
    // audio or export configuration is carried by this optional snapshot.
    std::optional<ChainGlobalSettings> globals;
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
    bool clearDefault();
    const std::optional<size_t> defaultIndex() const;
    // Applies the parts the preset carries onto `settings`, leaving everything
    // else (capture, export, resolution policy) untouched.
    static void apply(const PresetEntry& entry, EnhancementSettings& settings);
    // Editor variant: retain topology/layout unless Chain is selected. Colour
    // grades replace colour slots by ordinal (extras follow the last slot);
    // all unrelated nodes remain intact. Failure changes neither argument.
    static ChainValidation applyToChain(const PresetEntry& entry, EffectChain& chain,
                                        EnhancementSettings& settings);
    // Partial presets retain stable IDs, detached nodes and wires.
    static ChainValidation applyToEditor(const PresetEntry&, NodeEditorDocument&,
                                         EnhancementSettings& settings);
    // True when the preset can be applied to a settings struct of this kind.
    static bool matchesKind(const PresetEntry& entry, ChainMode kind) { return entry.kind == kind; }
    const std::vector<PresetEntry>& entries() const { return entries_; }
    const std::wstring& error() const { return error_; }
    bool corrupt() const { return corrupt_; }
    // The 2.0 interface ships no built-in presets (user decision 2026-09-29):
    // with this off, none are added and any built-in entries a file still
    // carries are dropped on load. Other callers keep the default.
    void setIncludeBuiltins(bool include) { includeBuiltins_ = include; }
    // One preset to / from a standalone file in the library's own format. An
    // import never replaces an existing name: it gets a numbered suffix.
    bool exportEntry(size_t index, const std::filesystem::path& target);
    bool importFile(const std::filesystem::path& source, std::wstring& nameOut);
    // Imports an existing single-purpose store (the old VEYRA_PRESETS files)
    // as chain-only or colour-only presets. Never overwrites an existing name.
    bool importLegacy(const std::vector<PresetEntry>& entries);
private:
    friend class ChainSessionStore;
    static bool parse(const std::string& data, std::vector<PresetEntry>& out, std::wstring& def, std::wstring& error,
                      bool* migrated = nullptr, bool preserveLegacy = false);
    std::string serialize() const;
    static std::string encodeEntries(const std::vector<PresetEntry>& entries, const std::wstring& defaultName, int minimumVersion = 1);
    void addBuiltins();
    std::filesystem::path path_;
    std::vector<PresetEntry> entries_;
    std::wstring defaultName_, error_;
    bool corrupt_ = false;
    bool includeBuiltins_ = true;
};

// A private editor-session file, not entries in the user's preset library.
// Reuses the versioned chain codec; no second set of per-node serializers.
class ChainSessionStore {
public:
    explicit ChainSessionStore(std::filesystem::path path) : path_(std::move(path)) {}
    bool load(ChainSession& session); // missing: keep the caller's initial state
    bool save(const ChainSession& session);
    const std::wstring& error() const { return error_; }
private:
    friend class PresetLibrary;
    static std::string encode(const ChainSession&);
    static bool decode(const std::string&, ChainSession&, bool* migrated = nullptr);
    std::filesystem::path path_;
    std::wstring error_;
    bool corrupt_ = false;
};
}
