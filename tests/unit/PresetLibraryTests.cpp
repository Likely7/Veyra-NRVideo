// PresetLibrary: one store for chain/colour/frame-generation/audio content,
// with per-preset contents, built-ins, and safe migration from the old stores.
#include "veyra/engine/PresetLibrary.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace veyra::engine;

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
std::filesystem::path scratch(const wchar_t* name) {
    auto dir = std::filesystem::temp_directory_path() / L"veyra-preset-library-tests";
    std::filesystem::create_directories(dir);
    auto path = dir / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}
EnhancementSettings sample() {
    EnhancementSettings s;
    s.nr = true; s.sr = true; s.multiplier = 4; s.color.enabled = true; s.color.exposure = 0.5f;
    s.videoHdr.enabled = true; s.model.intensity = 0.6f; s.audioOffsetMs = 25;
    s.audioSync = AudioSyncMode::Manual; s.exportBitrateMbps = 77; s.captureCompatible = true;
    return s;
}
} // namespace

int main() {
    // 1. Built-ins exist before anything is saved, and are read-only.
    {
        const auto path = scratch(L"builtins.v1");
        PresetLibrary library(path);
        check(library.load(), "load succeeds when the file is missing");
        check(library.entries().size() == 4, "four built-in presets are present");
        bool allBuiltin = true;
        for (const auto& e : library.entries()) allBuiltin = allBuiltin && e.builtin;
        check(allBuiltin, "built-ins are flagged read-only");
        check(!library.erase(0), "a built-in cannot be erased");
        check(!library.rename(0, L"改个名"), "a built-in cannot be renamed");
        check(library.duplicate(0), "a built-in can be duplicated");
        check(library.entries().size() == 5 && !library.entries()[1].builtin, "the copy is a normal preset");
    }

    // 2. A preset keeps only the parts it claims.
    {
        const auto path = scratch(L"contents.v1");
        PresetLibrary library(path);
        library.load();
        auto settings = sample();
        PresetEntry chainOnly;
        chainOnly.name = L"只存链路";
        chainOnly.contents = presetContentMask(PresetContent::Chain);
        chainOnly.chain = toChain(settings);
        check(library.put(chainOnly), "saving a chain-only preset succeeds");
        if (!library.load()) { std::printf("      parse error: %ls\n", library.error().c_str()); }
        check(library.load(), "reload succeeds");
        auto target = sample();
        target.color.exposure = -1.0f;      // must survive: colour is not in the preset
        target.audioOffsetMs = -50;          // must survive too
        PresetLibrary::apply(library.entries().back(), target);
        check(target.nr == settings.nr && target.sr == settings.sr && target.multiplier == settings.multiplier,
              "chain-only apply restores the stages");
        check(target.color.exposure == -1.0f, "chain-only apply leaves the colour grade alone");
        check(target.audioOffsetMs == -50, "chain-only apply leaves the audio offset alone");
    }

    // 3. Full preset round trip through the file.
    {
        const auto path = scratch(L"full.v1");
        PresetLibrary library(path);
        library.load();
        auto settings = sample();
        PresetEntry entry;
        entry.name = L"全部";
        entry.note = L"测试";
        entry.kind = ChainMode::Node;
        entry.contents = kPresetAllContent;
        entry.chain = toChain(settings);
        entry.chain.mode = ChainMode::Node;
        entry.chain.nodes[2].nr.model.style = 2;
        entry.chain.nodes[2].nr.temporal = true;
        entry.color = settings.color;
        entry.fg = {4, FrameGenerationBackend::Dlss};
        entry.audioSync = AudioSyncMode::Manual;
        entry.audioOffsetMs = 25;
        check(library.put(entry), "saving a full preset succeeds");
        check(library.load(), "reload succeeds");
        const auto& loaded = library.entries().back();
        check(loaded.name == L"全部" && loaded.note == L"测试", "name and note survive");
        check(loaded.kind == ChainMode::Node && loaded.chain.mode == ChainMode::Node, "the kind survives");
        check(loaded.contents == kPresetAllContent, "the content mask survives");
        check(loaded.chain.nodeCount == entry.chain.nodeCount, "the node count survives");
        check(loaded.chain.nodes[2].nr.model.style == 2 && loaded.chain.nodes[2].nr.temporal,
              "per-layer NR parameters survive");
        check(loaded.color.exposure == entry.color.exposure, "the colour grade survives");
        check(loaded.fg.multiplier == 4, "the frame-generation multiplier survives");
        check(loaded.audioOffsetMs == 25 && loaded.audioSync == AudioSyncMode::Manual, "the audio fields survive");
        auto target = EnhancementSettings{};
        target.exportBitrateMbps = 12;
        PresetLibrary::apply(loaded, target);
        check(target.exportBitrateMbps == 12, "applying a preset never touches export settings");
        check(target.captureCompatible == false, "applying a preset never touches capture settings");
    }

    // 4. Management: rename, duplicate, erase, default.
    {
        const auto path = scratch(L"manage.v1");
        PresetLibrary library(path);
        library.load();
        PresetEntry entry; entry.name = L"甲"; entry.contents = presetContentMask(PresetContent::Chain);
        entry.chain = toChain(sample());
        check(library.put(entry), "put 甲");
        entry.name = L"乙";
        check(library.put(entry), "put 乙");
        const auto index = library.entries().size() - 1;
        check(library.rename(index, L"丙"), "rename 乙 to 丙");
        check(!library.rename(index, L"甲"), "rename refuses a duplicate name");
        check(library.setDefault(index), "set default");
        PresetLibrary reloaded(path);
        check(reloaded.load(), "reload for management checks");
        check(reloaded.defaultIndex().has_value() && reloaded.entries()[*reloaded.defaultIndex()].name == L"丙",
              "the default preset survives a save/load cycle");
        const auto before = reloaded.entries().size();
        check(reloaded.duplicate(*reloaded.defaultIndex()), "duplicate the default");
        check(reloaded.entries().size() == before + 1, "duplicate adds an entry");
        check(reloaded.erase(*reloaded.defaultIndex()), "erase the default");
        check(!reloaded.defaultIndex().has_value(), "erasing the default clears it");
    }

    // 5. Legacy import never overwrites and keeps the chain.
    {
        const auto path = scratch(L"legacy.v1");
        PresetLibrary library(path);
        library.load();
        std::vector<PresetEntry> incoming;
        PresetEntry nrOnly; nrOnly.name = L"夜间游戏"; nrOnly.contents = presetContentMask(PresetContent::Chain);
        nrOnly.chain = toChain(sample());
        incoming.push_back(nrOnly);
        PresetEntry clashing; clashing.name = L"极致"; clashing.builtin = true;
        clashing.contents = presetContentMask(PresetContent::Chain);
        clashing.chain = toChain(EnhancementSettings{});
        incoming.push_back(clashing);
        check(library.importLegacy(incoming), "importing legacy presets succeeds");
        const auto found = std::find_if(library.entries().begin(), library.entries().end(),
            [](const PresetEntry& e) { return e.name == L"极致"; });
        check(found != library.entries().end() && found->builtin, "a name clash keeps the existing preset");
        check(std::any_of(library.entries().begin(), library.entries().end(),
              [](const PresetEntry& e) { return e.name == L"夜间游戏"; }), "the imported preset is present");
    }

    // 6. A corrupt file is preserved, never overwritten.
    {
        const auto path = scratch(L"corrupt.v1");
        { std::ofstream f(path, std::ios::binary); f << "VEYRA_PRESET_LIBRARY 99 nonsense\n"; }
        PresetLibrary library(path);
        check(!library.load(), "a corrupt library reports failure");
        check(library.corrupt(), "the corruption flag is set");
        check(!library.save(), "a corrupt library refuses to overwrite the file");
        std::ifstream f(path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(content.find("nonsense") != std::string::npos, "the original file is untouched");
    }

    std::printf(failures ? "preset library: %d FAILURES\n" : "preset library: all checks passed\n", failures);
    return failures ? 1 : 0;
}
