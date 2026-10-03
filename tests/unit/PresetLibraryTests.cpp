// PresetLibrary: one store for chain/colour/frame-generation/audio content,
// with per-preset contents, built-ins, and safe migration from the old stores.
#include "veyra/engine/PresetLibrary.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

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
std::string readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
// A legacy node header used the identical linear payload as list mode.
// Change only the two mode fields, leaving all original serialized parameters.
std::string legacyNodeLine(std::string line) {
    std::istringstream input(line); std::string name, note; int value;
    input >> std::quoted(name) >> std::quoted(note) >> std::ws;
    const auto kind = size_t(input.tellg());
    for (int i = 0; i < 8; ++i) input >> value;
    input >> std::ws; const auto mode = size_t(input.tellg());
    if (!input || kind >= line.size() || mode >= line.size()) return {};
    line[kind] = line[mode] = '1'; return line;
}
} // namespace

__declspec(noinline) void testNodeEditorSession() {
    const auto path = scratch(L"node-editor-session.v1");
    const auto sidecar = std::filesystem::path(path).concat(L".p3-editor");
    std::filesystem::remove(sidecar);
    auto session = std::make_unique<ChainSession>(ChainSession::initial(sample()));
    check(session->select(ChainMode::Node), "editor session: initialize node mode");
    ChainSessionStore writer(path);
    check(writer.save(*session), "editor session: save original v1 rollback file");
    const auto original = readBytes(path);
    auto& configuration = session->configurations[1];
    auto document = std::make_shared<NodeEditorDocument>();
    document->nodes = configuration.chain;
    check(document->layout.initialize(document->nodes).accepted, "editor session: initialize stable IDs");
    uint32_t sourceId = 0;
    for (uint32_t i = 0; i < document->nodes.nodeCount; ++i)
        if (document->nodes.nodes[i].type == EffectType::Color) sourceId = document->layout.ids[i];
    uint32_t copiedId = 0;
    check(document->layout.duplicate(document->nodes, sourceId, copiedId).accepted,
          "editor session: create detached enabled color copy");
    auto& color = document->nodes.nodes[document->layout.indexOf(document->nodes, copiedId)].color;
    color.exposure = 1.375f; color.mixerHue[2] = 17.5f; color.grading[1].hue = 215;
    color.grading[1].saturation = 25; color.curves[0].points[1].y = 0.75f;
    color.lutStrength = 37.5f; color.setLutName(L"saved-copy.cube");
    check(document->layout.disconnect(document->nodes, NodeGraphLayout::Input).accepted,
          "editor session: deliberately incomplete input wire");
    configuration.editor = document;
    configuration.selectedColour = document->layout.indexOf(document->nodes, copiedId);
    check(uint32_t(configuration.selectedColour) >= configuration.chain.nodeCount,
          "editor session: selected detached copy is absent from runtime");
    check(session->valid() && writer.save(*session),
          "editor session: incomplete document saves alongside last accepted runtime");
    check(readBytes(path) == original && readBytes(sidecar).starts_with("VEYRA_CHAIN_SESSION 2"),
          "editor session: new schema sidecar never overwrites v1 file");
    auto restored = std::make_unique<ChainSession>(ChainSession::initial({}));
    ChainSessionStore reopened(path);
    check(reopened.load(*restored) && *restored == *session,
          "editor session: fresh reader restores IDs edges enabled states and complete payloads");
    check(restored->configurations[1].chain == configuration.chain &&
          restored->configurations[1].editor && restored->configurations[1].editor.get() != document.get(),
          "editor session: accepted runtime retained and restored snapshot is independent");
    auto projection = std::make_unique<EffectChain>(configuration.chain);
    check(!document->layout.project(document->nodes, *projection).accepted && *projection == configuration.chain,
          "editor session: reloaded partial graph must not replace runtime");
    const auto validBytes = readBytes(sidecar);
    // Faults: duplicate ID, dangling edge, detached cycle, merge, and an
    // unsupported detached Protection node. Rejection is not migration.
    for (int fault = 0; fault < 5; ++fault) {
        auto damaged = std::make_unique<ChainSession>(*session);
        auto graph = std::make_shared<NodeEditorDocument>(*document);
        damaged->configurations[1].editor = graph;
        const auto detached = graph->layout.indexOf(graph->nodes, copiedId);
        if (fault == 0) graph->layout.ids[1] = graph->layout.ids[0];
        if (fault == 1) graph->layout.next[0] = graph->layout.nextId + 100;
        if (fault == 2) graph->layout.next[detached] = copiedId;
        if (fault == 3) {
            graph->layout.next[detached] = graph->layout.ids[0];
            graph->layout.inputNext = graph->layout.ids[0];
        }
        if (fault == 4) graph->nodes.nodes[detached].type = EffectType::Protection;
        check(!writer.save(*damaged) && readBytes(sidecar) == validBytes, __FUNCTION__);
        // Protection save refusal does not establish old sidecar migration.
        if (fault == 4) continue;
        std::istringstream source(validBytes);
        std::string headers[4], runtimeBody, editorBody;
        for (auto& line : headers) std::getline(source, line);
        source >> std::quoted(runtimeBody);
        uint32_t oldNextId, oldInput, oldCount;
        source >> oldNextId >> oldInput >> oldCount;
        for (uint32_t i = 0; i < oldCount; ++i) {
            uint32_t oldId, oldEdge; int oldFlag;
            source >> oldId >> oldEdge >> oldFlag;
        }
        source >> std::quoted(editorBody);
        check(bool(source) && oldCount == graph->nodes.nodeCount, __FUNCTION__);
        std::ostringstream bytes;
        for (const auto& line : headers) bytes << line << char(10);
        bytes << std::quoted(runtimeBody) << char(10);
        bytes << graph->layout.nextId << ' ' << graph->layout.inputNext << ' ' << oldCount << char(10);
        for (uint32_t i = 0; i < oldCount; ++i)
            bytes << graph->layout.ids[i] << ' ' << graph->layout.next[i] << ' ' << graph->nodes.nodes[i].enabled << char(10);
        bytes << std::quoted(editorBody) << char(10);
        const auto damagedBytes = bytes.str();
        auto retained = std::make_unique<ChainSession>(*session);
        { std::ofstream file(sidecar, std::ios::binary | std::ios::trunc); file << damagedBytes; }
        ChainSessionStore reader(path);
        check(!reader.load(*retained) && *retained == *session && !reader.save(*session) &&
              readBytes(sidecar) == damagedBytes && readBytes(path) == original, __FUNCTION__);
        { std::ofstream file(sidecar, std::ios::binary | std::ios::trunc); file << validBytes; }
    }
    { std::ofstream file(sidecar, std::ios::binary | std::ios::trunc); file << validBytes; }
    auto invalid = std::make_unique<ChainSession>(*session);
    invalid->configurations[1].selectedColour = int(document->nodes.nodeCount);
    check(!writer.save(*invalid) && readBytes(sidecar) == validBytes,
          "editor session: out of editor range selection rejected atomically");
    invalid->configurations[1].selectedColour = configuration.selectedNr;
    if (configuration.selectedNr >= 0)
        check(!writer.save(*invalid) && readBytes(sidecar) == validBytes,
              "editor session: wrong selected node type rejected atomically");
    *invalid = *session;
    auto invalidDocument = std::make_shared<NodeEditorDocument>(*document);
    invalidDocument->layout.ids[1] = invalidDocument->layout.ids[0];
    invalid->configurations[1].editor = invalidDocument;
    check(!writer.save(*invalid) && readBytes(sidecar) == validBytes && readBytes(path) == original,
          "editor session: duplicate-ID save rejected atomically");
    invalidDocument->layout = document->layout;
    invalidDocument->nodes.nodes[0].color.exposure = 9999;
    check(!writer.save(*invalid) && readBytes(sidecar) == validBytes,
          "editor session: invalid detached parameters cannot overwrite session");
    { std::ofstream file(sidecar, std::ios::binary | std::ios::trunc); file << validBytes << "unexpected"; }
    ChainSessionStore corrupt(path);
    check(!corrupt.load(*restored) && *restored == *session && !corrupt.save(*session),
          "editor session: corrupt sidecar retained without falling back or half-loading");
    std::filesystem::remove(sidecar);
}

__declspec(noinline) void testLegacyProtectionMigration() {
    for (int position : {1, 2, 4, 6}) for (bool enabled : {false, true}) {
        const auto path = scratch(L"legacy-protection-library.v1");
        const auto sidecar = std::filesystem::path(path).concat(L".p3-node");
        std::filesystem::remove(sidecar);
        PresetLibrary writer(path); writer.load();
        auto settings = sample(); settings.protection.enabled = enabled; settings.nrLayerCount = 4;
        PresetEntry entry; entry.name = L"legacy-protection"; entry.chain = toChain(settings); entry.color = settings.color;
        entry.chain.nodes[2].viewX = 481; entry.chain.nodes[2].nr.model.tone = .37f;
        auto& fixture = entry.chain;
        uint32_t index = 0;
        while (fixture.nodes[index].type != EffectType::Protection) ++index;
        const auto protection = fixture.nodes[index];
        for (uint32_t i = index; i > uint32_t(position); --i) fixture.nodes[i] = fixture.nodes[i - 1];
        fixture.nodes[position] = protection;
        auto expected = fixture; expected.mode = ChainMode::Node;
        uint32_t count = 0;
        for (uint32_t i = 0; i < fixture.nodeCount; ++i)
            if (fixture.nodes[i].type != EffectType::Protection) expected.nodes[count++] = fixture.nodes[i];
        for (uint32_t i = count; i < expected.nodeCount; ++i) expected.nodes[i] = {};
        expected.nodeCount = count;
        check(writer.put(entry), "create historical library fixture through list serializer");
        auto legacy = readBytes(path); const auto start = legacy.find("\"legacy-protection\"");
        const auto end = legacy.find('\n', start);
        legacy.replace(start, end - start, legacyNodeLine(legacy.substr(start, end - start)));
        { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << legacy; }
        PresetLibrary migrated(path);
        check(migrated.load() && !migrated.error().empty(), "legacy node library reports explicit protection removal notice");
        const auto& restored = migrated.entries().back().chain;
        check(restored == expected && restored.mode == ChainMode::Node && restored.countOf(EffectType::Protection) == 0 &&
              restored.firstOf(EffectType::NrEnhance)->viewX == 481 &&
              restored.firstOf(EffectType::NrEnhance)->nr.model.tone == .37f,
              "library migration preserves layout and non-protection parameters");
        check(migrated.save() && readBytes(path) == legacy && std::filesystem::exists(sidecar),
              "migrated library saves separately without rewriting original bytes");
        PresetLibrary reopened(path);
        check(reopened.load() && reopened.entries() == migrated.entries(), "restart prefers validated migrated library");
    }
    {
        const auto path = scratch(L"legacy-protection-session.v1");
        const auto sidecar = std::filesystem::path(path).concat(L".p3-node");
        std::filesystem::remove(sidecar);
        auto settings = sample(); settings.protection.enabled = true;
        auto session = ChainSession::initial(settings); session.select(ChainMode::Node);
        session.configurations[0].chain.nodes[2].viewX = 731;
        ChainSessionStore writer(path); check(writer.save(session), "create session migration fixture");
        std::istringstream input(readBytes(path));
        std::string headers[4], body; for (auto& line : headers) std::getline(input, line); input >> std::quoted(body);
        std::istringstream records(body); std::string magic, count, list, node;
        std::getline(records, magic); std::getline(records, count); std::getline(records, list); std::getline(records, node);
        node = list; node.replace(1, 4, "node"); node = legacyNodeLine(node);
        body = magic + '\n' + count + '\n' + list + '\n' + node + '\n';
        std::ostringstream bytes; for (const auto& line : headers) bytes << line << '\n'; bytes << std::quoted(body) << '\n';
        const auto legacy = bytes.str();
        { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << legacy; }
        ChainSessionStore migrated(path); auto restored = ChainSession::initial({});
        check(migrated.load(restored) && !migrated.error().empty(), "legacy session migration reports its compatibility action");
        check(restored.configurations[0].chain == session.configurations[0].chain &&
              restored.configurations[1].chain.countOf(EffectType::Protection) == 0 &&
              restored.configurations[1].chain.firstOf(EffectType::NrEnhance)->viewX == 731,
              "session migration keeps list protection and node layout independent");
        check(migrated.save(restored) && readBytes(path) == legacy && std::filesystem::exists(sidecar),
              "migrated session saves sidecar and preserves original file exactly");
        ChainSessionStore reopened(path); auto restarted = ChainSession::initial({});
        check(reopened.load(restarted) && restarted == restored, "new session process selects migrated sidecar");
    }
}


// 2.0 (2026-09-29): the new interface ships no built-in presets, and a single
// preset round-trips through a standalone file; an import never overwrites.
void testNoBuiltinsAndSingleFile() {
    const auto path = scratch(L"no-builtins.v1");
    {
        PresetLibrary withBuiltins(path);
        check(withBuiltins.load() && withBuiltins.entries().size() == 4, "no-builtins: default library still has 4 built-ins");
        PresetEntry mine; mine.name = L"mine"; mine.chain = toChain(sample());
        check(withBuiltins.put(mine) && withBuiltins.setDefault(0) && withBuiltins.save(), "no-builtins: file with built-ins, default on a built-in");
    }
    PresetLibrary lib(path);
    lib.setIncludeBuiltins(false);
    check(lib.load(), "no-builtins: load");
    check(lib.entries().size() == 1 && lib.entries()[0].name == L"mine" && !lib.entries()[0].builtin,
          "no-builtins: built-in entries in the file are dropped, the user's own kept");
    check(!lib.defaultIndex().has_value(), "no-builtins: a default that pointed at a built-in is cleared");
    const auto file = scratch(L"single.vpreset");
    check(lib.exportEntry(0, file) && std::filesystem::file_size(file) > 0, "single file: export");
    std::wstring name;
    check(lib.importFile(file, name) && name == L"mine 2" && lib.entries().size() == 2,
          "single file: import of an existing name gets a numbered suffix");
    check(lib.entries()[1].chain == lib.entries()[0].chain, "single file: imported chain equals the exported one");
    { std::ofstream bad(file, std::ios::binary | std::ios::trunc); bad << "not a preset"; }
    check(!lib.importFile(file, name) && lib.entries().size() == 2, "single file: a damaged file is refused, nothing added");
    PresetLibrary empty(scratch(L"no-builtins-empty.v1"));
    empty.setIncludeBuiltins(false);
    check(empty.load() && empty.entries().empty(), "no-builtins: a fresh library starts empty");
}

__declspec(noinline) int testExistingPresets() {
    // An editor preset must not canonicalise a hand-ordered chain. Exercise
    // every contents combination, not just full presets or single grades.
    {
        auto baseSettings = sample();
        baseSettings.additionalColorCount = 1;
        baseSettings.additionalColors[0].enabled = true;
        baseSettings.additionalColors[0].exposure = -0.75f;
        auto base = toChain(baseSettings); base.mode = ChainMode::Node;
        removeLegacyNodeProtection(base);
        std::rotate(base.nodes.begin() + 1, base.nodes.begin() + 2, base.nodes.begin() + 4);
        for (uint32_t i = 0; i < base.nodeCount; ++i) {
            base.nodes[i].viewX = 37.0f + i * 113.0f;
            base.nodes[i].viewY = 19.0f + i * 7.0f;
        }
        fromChain(base, baseSettings);
        auto saved = sample(); saved.additionalColorCount = 5;
        for (unsigned i = 0; i < 5; ++i) {
            saved.additionalColors[i].enabled = i != 2;
            saved.additionalColors[i].exposure = 0.2f * (i + 1);
        }
        saved.model.intensity = 0.33f; saved.color.exposure = 1.2f;
        PresetEntry entry; entry.kind = ChainMode::Node;
        entry.chain = toChain(saved); entry.chain.mode = ChainMode::Node;
        removeLegacyNodeProtection(entry.chain);
        entry.color = saved.color; entry.fg = {2, FrameGenerationBackend::XeSS};
        entry.audioSync = AudioSyncMode::Manual; entry.audioOffsetMs = 73;
        for (uint32_t i = 0; i < entry.chain.nodeCount; ++i) entry.chain.nodes[i].viewX = 800 + i * 31.0f;
        bool masks = true, geometry = true, unrelated = true;
        for (uint32_t mask = 0; mask <= kPresetAllContent; ++mask) {
            entry.contents = mask; auto chain = base; auto settings = baseSettings;
            auto expected = settings; PresetLibrary::apply(entry, expected);
            const auto result = PresetLibrary::applyToChain(entry, chain, settings);
            masks = masks && result.accepted && settings.color == expected.color &&
                settings.additionalColorCount == expected.additionalColorCount &&
                settings.additionalColors == expected.additionalColors &&
                settings.multiplier == expected.multiplier &&
                settings.frameGenerationBackend == expected.frameGenerationBackend &&
                settings.audioOffsetMs == expected.audioOffsetMs && settings.audioSync == expected.audioSync;
            unrelated = unrelated && settings.exportBitrateMbps == baseSettings.exportBitrateMbps &&
                settings.captureCompatible == baseSettings.captureCompatible;
            if (!(mask & presetContentMask(PresetContent::Chain))) {
                for (uint32_t i = 0; i < base.nodeCount; ++i) {
                    if (base.nodes[i].type == EffectType::Color) continue;
                    const auto* actual = chain.firstOf(base.nodes[i].type);
                    auto want = base.nodes[i];
                    if (want.type == EffectType::FrameGeneration &&
                        (mask & presetContentMask(PresetContent::FrameGeneration))) want.enabled = entry.fg.multiplier > 1;
                    geometry = geometry && actual && *actual == want;
                }
                geometry = geometry && chain.nodes[0].viewX == base.nodes[0].viewX &&
                    chain.nodes[3].type == EffectType::Color && chain.nodes[3].viewX == base.nodes[3].viewX;
            } else if (mask & presetContentMask(PresetContent::Color)) {
                for (uint32_t i = 0; i < chain.nodeCount; ++i)
                    geometry = geometry && chain.nodes[i].type == entry.chain.nodes[i].type &&
                        chain.nodes[i].viewX == entry.chain.nodes[i].viewX;
            }
            if (mask == presetContentMask(PresetContent::Audio))
                geometry = geometry && chain == base && settings == expected;
        }
        check(masks, "all 16 editor content masks preserve legacy settings semantics and six grades");
        check(geometry, "partial presets preserve hand order/layout and full presets restore saved layout");
        check(unrelated, "editor presets do not overwrite capture or export settings");

        entry.contents = presetContentMask(PresetContent::Color);
        auto chain = base; auto settings = baseSettings;
        check(PresetLibrary::applyToChain(entry, chain, settings).accepted && chain.countOf(EffectType::Color) == 6,
              "colour-only can expand existing interleaved slots to six");
        entry.chain = toChain(sample()); entry.chain.mode = ChainMode::Node;
        removeLegacyNodeProtection(entry.chain);
        check(PresetLibrary::applyToChain(entry, chain, settings).accepted && chain.countOf(EffectType::Color) == 1 &&
              chain.firstOf(EffectType::NrEnhance)->viewX == base.firstOf(EffectType::NrEnhance)->viewX,
              "colour-only can remove surplus grades without moving unrelated nodes");
        entry.contents = presetContentMask(PresetContent::Chain);
        chain = base; settings = baseSettings;
        check(PresetLibrary::applyToChain(entry, chain, settings).accepted && chain.countOf(EffectType::Color) == 2 &&
              settings.additionalColors == baseSettings.additionalColors,
              "chain-only keeps every current grade even when saved topology has fewer slots");

        entry.contents = kPresetAllContent; entry.kind = ChainMode::List;
        chain = base; settings = baseSettings;
        check(!PresetLibrary::applyToChain(entry, chain, settings).accepted && chain == base && settings == baseSettings,
              "wrong preset mode rejects without mutation");
        entry.kind = ChainMode::Node; entry.contents = presetContentMask(PresetContent::Color);
        // Enabling a currently disabled colour slot after HDR is illegal.
        entry.chain = toChain(baseSettings); entry.chain.mode = ChainMode::Node;
        removeLegacyNodeProtection(entry.chain);
        auto invalidBase = base;
        std::swap(invalidBase.nodes[3], invalidBase.nodes[4]);
        invalidBase.nodes[4].enabled = false;
        fromChain(invalidBase, settings); const auto beforeSettings = settings; chain = invalidBase;
        check(validateChain(chain).accepted && !PresetLibrary::applyToChain(entry, chain, settings).accepted &&
              chain == invalidBase && settings == beforeSettings,
              "colour activation that violates HDR tail order rejects atomically");
    }
    {
        const auto path = scratch(L"legacy-single-color.v1");
        auto settings = sample();
        PresetEntry entry; entry.name = L"legacy-color";
        entry.chain = toChain(settings); entry.color = settings.color;
        entry.chain.nodes[0].viewX = 213;
        PresetLibrary writer(path);
        check(writer.load() && writer.put(entry), "legacy single-colour preset saves");
        std::ifstream file(path); std::string magic; int version = 0; file >> magic >> version;
        check(version == 1, "compatibility fixture really uses codec v1");
        PresetLibrary reader(path);
        check(reader.load(), "fresh library reads legacy single-colour preset");
        const auto& restored = reader.entries().back();
        const auto* color = restored.chain.firstOf(EffectType::Color);
        check(color && color->color == entry.color, "v1 chain carries full legacy colour payload");
        auto applied = EnhancementSettings{}; fromChain(restored.chain, applied);
        check(applied.color == entry.color && restored.chain.nodes[0].viewX == 213,
              "direct legacy chain projection preserves colour and layout");
    }
    // 1. Built-ins exist before anything is saved, and are read-only.
    {
        const auto path = scratch(L"builtins.v1");
        PresetLibrary library(path);
        check(library.load(), "load succeeds when the file is missing");
        check(library.entries().size() == 4, "four built-in presets are present");
        bool allBuiltin = true;
        for (const auto& e : library.entries()) allBuiltin = allBuiltin && e.builtin;
        check(allBuiltin, "built-ins are flagged read-only");
        auto original = sample();
        PresetLibrary::apply(library.entries().front(), original);
        check(original.multiplier == 1 && !original.nr && !original.sr,
              "original built-in disables frame generation and enhancement");
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
        target.additionalColorCount = 1;
        target.additionalColors[0].exposure = 1.25f;
        target.audioOffsetMs = -50;          // must survive too
        PresetLibrary::apply(library.entries().back(), target);
        check(target.nr == settings.nr && target.sr == settings.sr && target.multiplier == settings.multiplier,
              "chain-only apply restores the stages");
        check(target.color.exposure == -1.0f, "chain-only apply leaves the colour grade alone");
        check(target.additionalColorCount == 1 && target.additionalColors[0].exposure == 1.25f,
              "chain-only apply leaves additional colour grades alone");
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
        removeLegacyNodeProtection(entry.chain);
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

    {
        PresetLibrary library(scratch(L"six-colors.v2"));
        check(library.load(), "multi-colour library initializes");
        auto settings = sample();
        settings.additionalColorCount = 5;
        for (unsigned i = 0; i < 5; ++i) {
            auto& color = settings.additionalColors[i];
            color.enabled = i != 2;
            color.exposure = float(i) * 0.25f;
            color.setLutName(L"independent-" + std::to_wstring(i) + L".cube");
        }
        PresetEntry entry; entry.name = L"六调色";
        entry.chain = toChain(settings); entry.color = settings.color;
        check(library.put(entry) && library.load(), "six-colour preset reloads");
        const auto& loaded = library.entries().back();
        check(loaded.chain == entry.chain, "every colour node and LUT survive serialization");
        auto target = EnhancementSettings{};
        PresetLibrary::apply(loaded, target);
        check(target.color == settings.color && target.additionalColorCount == 5 &&
              target.additionalColors == settings.additionalColors, "all six colour settings apply");
        auto colorOnly = loaded; colorOnly.contents = presetContentMask(PresetContent::Color);
        target = {}; target.exportBitrateMbps = 12;
        PresetLibrary::apply(colorOnly, target);
        check(target.additionalColors == settings.additionalColors && target.additionalColorCount == 5 &&
              !target.nr && !target.sr && target.exportBitrateMbps == 12, "colour-only restores six grades without other stages");
    }

    // Every colour node carries a full grade in v2. The file guard must allow
    // all 64 entries even with dense curves and long UTF-8 LUT references.
    {
        const auto path = scratch(L"six-colors-capacity.v2");
        PresetLibrary library(path);
        bool capacity = library.load();
        auto settings = sample(); settings.additionalColorCount = 5;
        auto fill = [](ColorSettings& color) {
            color.enabled = true; color.setLutName(std::wstring(220, L'色') + L".cube");
            for (auto& curve : color.curves) {
                curve.count = kColorCurvePoints;
                for (int j = 0; j < kColorCurvePoints; ++j)
                    curve.points[j] = {float(j) / 7.0f, float(j * j) / 49.0f};
            }
        };
        fill(settings.color); for (auto& color : settings.additionalColors) fill(color);
        PresetEntry entry; entry.chain = toChain(settings); entry.color = settings.color;
        for (unsigned i = unsigned(library.entries().size()); i < 64 && capacity; ++i) {
            entry.name = L"six-grade-" + std::to_wstring(i);
            capacity = library.put(entry);
        }
        PresetLibrary loaded(path);
        capacity = capacity && loaded.load() && loaded.entries().size() == 64;
        for (const auto& preset : loaded.entries())
            if (!preset.builtin) capacity = capacity && preset.chain == entry.chain && preset.color == entry.color;
        check(capacity, "64 full-curve/Unicode six-grade library entries reload");
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

    {
        const auto path = scratch(L"default-rollback.v1");
        PresetLibrary library(path);
        check(library.load(), "default rollback library initializes");
        PresetEntry entry; entry.name = L"rollback"; entry.chain = toChain(sample());
        check(library.put(entry), "default rollback fixture saves");
        const auto index = library.entries().size() - 1;
        check(library.setDefault(index), "default rollback fixture selects default");
        const auto bytes = readBytes(path);
        const auto temp = std::filesystem::path(path).concat(L".tmp");
        std::filesystem::create_directory(temp);
        { std::ofstream file(temp / L"sentinel"); file << "occupied"; }
        check(!library.clearDefault() && library.defaultIndex() == index && readBytes(path) == bytes,
              "failed default clear keeps memory and original file");
        check(!library.setDefault(0) && library.defaultIndex() == index && readBytes(path) == bytes,
              "failed default replacement keeps memory and original file");
        std::filesystem::remove(temp / L"sentinel"); std::filesystem::remove(temp);
        check(library.clearDefault() && !library.defaultIndex().has_value(), "default clear persists");
        PresetLibrary reopened(path);
        check(reopened.load() && !reopened.defaultIndex().has_value(), "cleared default survives restart");
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

    {
        const auto path = scratch(L"oversized.v2");
        const size_t bytes = 2u * 1024u * 1024u + 1;
        { std::ofstream f(path, std::ios::binary); f << std::string(bytes, 'x'); }
        PresetLibrary library(path);
        check(!library.load() && library.corrupt() && !library.save(), "oversized library is rejected and protected");
        check(std::filesystem::file_size(path) == bytes, "oversized source remains unchanged");
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

    {
        auto s=sample();s.nrLayerCount=4;s.additionalColorCount=1;s.additionalColors[0].exposure=.7f;
        for(unsigned i=0;i<4;++i){auto& n=s.nrLayers[i];n.enabled=i!=1;
            n.sizePolicy=static_cast<veyra::pipeline::NrSizePolicy>(i);n.model.tone=.2f*float(i+1);}
        PresetEntry entry;entry.name=L"NR四层尺寸";entry.contents=kPresetAllContent;entry.chain=toChain(s);entry.color=s.color;
        PresetLibrary library(scratch(L"nr-size.v3"));
        check(library.load()&&library.put(entry)&&library.load(),"NR per-instance library v3 reloads");
        check(library.entries().back().chain==entry.chain,"v3 preserves every node including multiple grades");
        EnhancementSettings restored;PresetLibrary::apply(library.entries().back(),restored);
        check(restored.nrLayerCount==4&&restored.nrLayers==s.nrLayers,"applying NR preset preserves disabled layers and size policies");
    }
    {
        const auto path = scratch(L"chain-session.v1");
        ChainSessionStore store(path);
        auto session = ChainSession::initial(sample());
        const auto initial = session;
        check(store.load(session) && session == initial, "missing session preserves initial settings");
        check(session.select(ChainMode::Node), "session initializes node configuration");
        auto& node = session.configurations[1];
        node.videoSrQuality = 4;
        node.chain.firstOf(EffectType::NrEnhance)->nr.sizePolicy = veyra::pipeline::NrSizePolicy::Native;
        node.chain.firstOf(EffectType::Color)->color.exposure = 1.7f;
        node.chain.nodes[0].viewX = 341.25f;
        node.selectedColour = 0;
        check(store.save(session), "mode session writes validated snapshot");
        auto restored = ChainSession::initial({});
        ChainSessionStore reopened(path);
        check(reopened.load(restored) && restored == session, "fresh store restores both chains parameters layout and active mode");
        auto invalid = session; invalid.configurations[1].videoSrQuality = 99;
        check(!reopened.save(invalid), "invalid session cannot replace valid file");
        check(reopened.load(restored) && restored == session, "invalid save leaves last valid session intact");
        const auto read = [&] { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
        const auto validBytes = read();
        const auto temporary = std::filesystem::path(path).concat(L".tmp");
        std::filesystem::create_directory(temporary);
        { std::ofstream keep(temporary / L"sentinel"); keep << "occupied"; }
        check(!reopened.save(session) && read() == validBytes, "write failure preserves original session bytes");
        std::filesystem::remove(temporary / L"sentinel"); std::filesystem::remove(temporary);
        { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << "VEYRA_CHAIN_SESSION 999\ncorrupt"; }
        ChainSessionStore corrupt(path);
        const auto before = restored;
        check(!corrupt.load(restored) && restored == before, "corrupt session does not partially mutate live state");
        check(!corrupt.save(session) && read().find("999") != std::string::npos, "corrupt source is never overwritten");
        { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << validBytes << "trailing"; }
        ChainSessionStore trailing(path);
        check(!trailing.load(restored) && restored == before, "trailing session data is rejected transactionally");
    }
    std::printf(failures ? "preset library: %d FAILURES\n" : "preset library: all checks passed\n", failures);
    return failures ? 1 : 0;
}

void testEditorPresetStorage() {
    const auto path = scratch(L"editor-presets.v1");
    const auto sidecar = std::filesystem::path(path).concat(L".p3-editor");
    std::filesystem::remove(sidecar);
    PresetLibrary library(path); check(library.load() && library.save(), "editor preset store: legacy baseline");
    const auto oldBytes = readBytes(path);
    auto settings = sample(); auto runtime = toChain(settings); runtime.mode = ChainMode::Node;
    removeLegacyNodeProtection(runtime);
    auto doc = std::make_shared<NodeEditorDocument>(); doc->nodes = runtime;
    check(doc->layout.initialize(doc->nodes).accepted, "editor preset store: init");
    uint32_t colorId = 0, duplicate = 0;
    for (uint32_t i = 0; i < doc->nodes.nodeCount; ++i)
        if (doc->nodes.nodes[i].type == EffectType::Color) colorId = doc->layout.ids[i];
    check(doc->layout.duplicate(doc->nodes, colorId, duplicate).accepted &&
          doc->layout.disconnect(doc->nodes, NodeGraphLayout::Input).accepted, "editor preset store: detached incomplete document");
    doc->nodes.nodes[doc->layout.indexOf(doc->nodes, duplicate)].color.exposure = 2.25f;
    PresetEntry entry; entry.name = L"editor-complete"; entry.kind = ChainMode::Node;
    entry.chain = runtime; entry.color = settings.color; entry.fg = {settings.multiplier, settings.frameGenerationBackend};
    entry.nodeConfiguration = ChainConfiguration::capture(runtime, settings); entry.nodeConfiguration->editor = doc;
    check(library.put(entry) && readBytes(path) == oldBytes && readBytes(sidecar).starts_with("VEYRA_PRESET_LIBRARY 4"),
          "editor preset store: sidecar saves without overwriting legacy");
    PresetLibrary reopened(path); check(reopened.load(), "editor preset store: reopen");
    const auto found = std::find_if(reopened.entries().begin(), reopened.entries().end(), [&](const auto& e) {return e.name == entry.name;});
    check(found != reopened.entries().end() && *found == entry, "editor preset store: full configuration round trip");
    NodeEditorDocument applied; applied.nodes.mode = ChainMode::Node;
    check(applied.layout.initialize(applied.nodes).accepted && PresetLibrary::applyToEditor(entry, applied, settings).accepted &&
          applied == *doc, "editor preset store: apply restores detached IDs payloads and edges");
    const auto before = reopened.entries(); const auto bytes = readBytes(sidecar);
    auto invalid = entry; invalid.name = L"invalid-editor"; auto badDoc = std::make_shared<NodeEditorDocument>(*doc);
    badDoc->layout.ids[0] = 0; invalid.nodeConfiguration->editor = badDoc;
    check(!reopened.put(invalid) && reopened.entries() == before && readBytes(sidecar) == bytes,
          "editor preset store: failed save cannot poison memory or disk");
}

void testEditorPresetParts() {
    auto values = sample();
    auto document = std::make_unique<NodeEditorDocument>();
    document->nodes = toChain(values); document->nodes.mode = ChainMode::Node;
    removeLegacyNodeProtection(document->nodes);
    check(document->layout.initialize(document->nodes).accepted, "editor preset: initialize");
    uint32_t colorId = 0, nrId = 0, copied = 0;
    for (uint32_t i = 0; i < document->nodes.nodeCount; ++i) {
        if (document->nodes.nodes[i].type == EffectType::Color) colorId = document->layout.ids[i];
        if (document->nodes.nodes[i].type == EffectType::NrEnhance) nrId = document->layout.ids[i];
    }
    check(document->layout.duplicate(document->nodes, colorId, copied).accepted, "editor preset: detached color after FG");
    const auto original = std::make_unique<NodeEditorDocument>(*document);
    PresetEntry audio; audio.kind = ChainMode::Node; audio.chain.mode = ChainMode::Node;
    audio.contents = presetContentMask(PresetContent::Audio); audio.audioOffsetMs = 73;
    check(PresetLibrary::applyToEditor(audio, *document, values).accepted && *document == *original &&
          values.audioOffsetMs == 73, "editor preset: audio applies without flattening detached tail");
    PresetEntry color; color.kind = ChainMode::Node; color.chain.mode = ChainMode::Node;
    color.contents = presetContentMask(PresetContent::Color); color.color.enabled = true; color.color.exposure = 1.25f;
    color.chain.nodeCount = 3;
    for (uint32_t i = 0; i < 3; ++i) {
        auto& n = color.chain.nodes[i]; n.type = EffectType::Color; n.enabled = true;
        n.color.enabled = true; n.color.exposure = float(i + 1) / 4;
    }
    check(PresetLibrary::applyToEditor(color, *document, values).accepted &&
          document->nodes.countOf(EffectType::Color) == 3, "editor preset: grow color slots");
    const int nr = document->layout.indexOf(document->nodes, nrId);
    check(nr >= 0 && document->nodes.nodes[nr] == original->nodes.nodes[original->layout.indexOf(original->nodes, nrId)] &&
          document->layout.inputNext == original->layout.inputNext, "editor preset: unrelated NR identity payload and source edge retained");
    auto runtime = std::make_unique<EffectChain>();
    check(document->layout.project(document->nodes, *runtime).accepted && runtime->countOf(EffectType::Color) == 1 &&
          document->layout.indexOf(document->nodes, copied) >= 0, "editor preset: added color follows detached slot without entering runtime");
    color.chain.nodeCount = 1;
    check(PresetLibrary::applyToEditor(color, *document, values).accepted && document->nodes.countOf(EffectType::Color) == 1 &&
          document->layout.indexOf(document->nodes, colorId) >= 0 && document->layout.indexOf(document->nodes, copied) < 0,
          "editor preset: shrink keeps surviving stable ID");
    check(document->layout.disconnect(document->nodes, NodeGraphLayout::Input).accepted, "editor preset: disconnect source");
    const auto accepted = values; color.color.exposure = 2.5f;
    check(PresetLibrary::applyToEditor(color, *document, values).accepted && values == accepted &&
          document->nodes.nodes[document->layout.indexOf(document->nodes, colorId)].color.exposure == 2.5f &&
          document->layout.inputNext == NodeGraphLayout::Input, "editor preset: disconnected color changes draft only");
    const auto beforeBad = std::make_unique<NodeEditorDocument>(*document);
    color.color.exposure = 1000;
    check(!PresetLibrary::applyToEditor(color, *document, values).accepted && *document == *beforeBad && values == accepted,
          "editor preset: invalid detached parameter rolls back document and settings");
    PresetEntry fg; fg.kind = ChainMode::Node; fg.chain.mode = ChainMode::Node;
    fg.contents = presetContentMask(PresetContent::FrameGeneration); fg.fg.multiplier = 9;
    check(!PresetLibrary::applyToEditor(fg, *document, values).accepted && *document == *beforeBad && values == accepted,
          "editor preset: invalid detached FG multiplier rolls back");
    PresetEntry whole; whole.kind = ChainMode::Node; whole.chain = original->nodes;
    --whole.chain.nodeCount; whole.contents = kPresetAllContent; whole.color = accepted.color;
    whole.fg.multiplier = accepted.multiplier;
    check(PresetLibrary::applyToEditor(whole, *document, values).accepted &&
          document->layout.project(document->nodes, *runtime).accepted, "editor preset: full replacement accepts incomplete prior editor");
    auto noFg = std::make_unique<NodeEditorDocument>(); noFg->nodes.mode = ChainMode::Node;
    check(noFg->layout.initialize(noFg->nodes).accepted, "editor preset: empty graph");
    fg.fg.multiplier = 2; values = EnhancementSettings{};
    check(PresetLibrary::applyToEditor(fg, *noFg, values).accepted && values.multiplier == 2 &&
          noFg->nodes.countOf(EffectType::FrameGeneration) == 1 && noFg->layout.project(noFg->nodes, *runtime).accepted,
          "editor preset: new FG attaches at complete tail");
}

__declspec(noinline) void testEditorGlobalSettings() {
    const auto path = scratch(L"editor-global-settings.v1");
    const auto editorPath = std::filesystem::path(path).concat(L".p3-editor");
    const auto draftPath = std::filesystem::path(editorPath).concat(L".p3-globals");
    std::filesystem::remove(editorPath); std::filesystem::remove(draftPath);
    auto initial = sample(); initial.multiplier = 6;
    auto session = std::make_unique<ChainSession>(ChainSession::initial(initial));
    check(session->select(ChainMode::Node), "globals: initialize session");
    ChainSessionStore writer(path);
    check(writer.save(*session), "globals: write v1");
    const auto v1 = readBytes(path);
    auto document = std::make_shared<NodeEditorDocument>();
    auto& c = session->configurations[1];
    document->nodes = c.chain; document->layout.initialize(document->nodes);
    document->layout.disconnect(document->nodes, NodeGraphLayout::Input);
    c.editor = document;
    check(writer.save(*session), "globals: write legacy editor v2");
    const auto v2 = readBytes(editorPath);
    document->globals = static_cast<const ChainGlobalSettings&>(c);
    document->globals->srTarget = veyra::pipeline::SrTarget::Qhd;
    document->globals->videoSrQuality = 3;
    document->nodes.fgMultiplier = 4;
    document->globals->fgBackend = FrameGenerationBackend::XeSS;
    document->globals->opticalFlowBackend = OpticalFlowBackend(1);
    const auto runtimeGlobals = static_cast<const ChainGlobalSettings&>(c);
    check(session->valid() && writer.save(*session), "globals: write disconnected draft v3");
    check(readBytes(path) == v1 && readBytes(editorPath) == v2 &&
          readBytes(draftPath).starts_with("VEYRA_CHAIN_SESSION 3"), "globals: preserve both legacy files");
    auto restored = std::make_unique<ChainSession>();
    ChainSessionStore reader(path);
    check(reader.load(*restored) && *restored == *session, "globals: reopen exact runtime and draft");
    check(static_cast<const ChainGlobalSettings&>(restored->configurations[1]) == runtimeGlobals &&
          *restored->configurations[1].editor->globals != runtimeGlobals, "globals: draft never replaces accepted runtime");
    const auto bytes = readBytes(draftPath);
    document->globals->videoSrQuality = 99;
    check(!writer.save(*session) && readBytes(draftPath) == bytes, "globals: invalid detached quality rejected atomically");
    document->globals->videoSrQuality = 3;
    document->nodes.fgMultiplier = 6;
    check(!session->valid(), "globals: XeSS draft rejects six times even while disconnected");
    document->nodes.fgMultiplier = 4;
    auto entry = std::make_unique<PresetEntry>();
    entry->name = L"globals"; entry->kind = ChainMode::Node; entry->contents = kPresetAllContent;
    entry->chain = c.chain; entry->nodeConfiguration = c; entry->fg = {4, FrameGenerationBackend::XeSS};
    const auto presetPath = scratch(L"editor-global-preset.v1");
    const auto presetEditor = std::filesystem::path(presetPath).concat(L".p3-editor");
    const auto presetDraft = std::filesystem::path(presetEditor).concat(L".p3-globals");
    std::filesystem::remove(presetEditor); std::filesystem::remove(presetDraft);
    PresetLibrary library(presetPath); library.load();
    check(library.put(*entry) && readBytes(presetDraft).starts_with("VEYRA_PRESET_LIBRARY 5"),
          "globals: preset v5 writes independent file");
    PresetLibrary reopened(presetPath);
    check(reopened.load(), "globals: reload preset v5");
    const auto found = std::find_if(reopened.entries().begin(), reopened.entries().end(),
        [](const auto& e) { return e.name == L"globals"; });
    check(found != reopened.entries().end() && found->nodeConfiguration == entry->nodeConfiguration,
          "globals: preset v5 keeps runtime and draft separately");
    auto target = std::make_unique<NodeEditorDocument>(); target->nodes.mode = ChainMode::Node;
    target->layout.initialize(target->nodes);
    auto values = initial;
    check(PresetLibrary::applyToEditor(*entry, *target, values).accepted &&
          target->globals == document->globals && target->layout == document->layout,
          "globals: complete preset restores disconnected draft globals");
    auto invalid = std::make_unique<ChainSession>(*restored);
    const auto retained = std::make_unique<ChainSession>(*invalid);
    { std::ofstream file(draftPath, std::ios::binary | std::ios::trunc); file << bytes << "garbage"; }
    ChainSessionStore corrupt(path);
    check(!corrupt.load(*invalid) && *invalid == *retained && !corrupt.save(*session),
          "globals: corrupt v3 decode does not partly mutate output");
    std::filesystem::remove(draftPath); std::filesystem::remove(editorPath);
    std::filesystem::remove(presetDraft); std::filesystem::remove(presetEditor);
}

void testFsrBackendStorage() {
    for(const auto backend : {FrameGenerationBackend::Fsr, FrameGenerationBackend::Fsr4}) {
        auto settings=sample();settings.multiplier=2;settings.frameGenerationBackend=backend;
        const auto path=scratch(backend==FrameGenerationBackend::Fsr?L"fsr3-library.v1":L"fsr4-library.v1");
        PresetLibrary writer(path);writer.setIncludeBuiltins(false);
        PresetEntry entry;entry.name=L"FSR FG";entry.contents=presetContentMask(PresetContent::FrameGeneration);
        entry.chain=toChain(settings);entry.fg={2,backend};
        check(writer.load()&&writer.put(entry), "FSR preset: save distinct backend");
        PresetLibrary reader(path);reader.setIncludeBuiltins(false);
        check(reader.load()&&reader.entries().size()==1&&reader.entries()[0].fg==entry.fg,
              "FSR preset: reopen preserves backend and multiplier");
        auto destination=sample();
        if(!reader.entries().empty())PresetLibrary::apply(reader.entries()[0],destination);
        check(destination.frameGenerationBackend==backend&&destination.multiplier==2&&destination.validate().empty(),
              "FSR preset: apply validates without DLSS relabeling");
        entry.name=L"invalid FSR 4X";entry.fg.multiplier=4;
        const auto retained=readBytes(path);
        check(!writer.put(entry)&&readBytes(path)==retained, "FSR preset: illegal multiplier rejected atomically");

        const auto sessionPath=scratch(backend==FrameGenerationBackend::Fsr?L"fsr3-session.v1":L"fsr4-session.v1");
        const auto session=std::make_unique<ChainSession>(ChainSession::initial(settings));
        check(session->select(ChainMode::Node), "FSR session: select node mode");
        ChainSessionStore sessionWriter(sessionPath);
        auto restored=std::make_unique<ChainSession>(ChainSession::initial({}));
        ChainSessionStore sessionReader(sessionPath);
        check(sessionWriter.save(*session)&&sessionReader.load(*restored)&&*restored==*session,
              "FSR session: list and node configurations reopen independently");
    }
}

__declspec(noinline) void testRenderingChoices(){
    auto s=sample();s.videoHdr.enabled=false;s.srTarget=veyra::pipeline::SrTarget::Uhd6K;
    s.hdrOutputMode=HdrOutputMode::ScRgb;s.fgMotion=s.srMotion=s.nrMotion=MotionSource::Zero;
    const auto path=scratch(L"field-rendering.v1");
    std::filesystem::remove(std::filesystem::path(path).concat(L".field-render"));
    PresetLibrary writer(path);writer.setIncludeBuiltins(false);
    PresetEntry e;e.name=L"rendering";e.kind=ChainMode::List;e.chain=toChain(s);e.globals=ChainGlobalSettings::capture(s);e.fg={s.multiplier,s.frameGenerationBackend};
    check(writer.load()&&writer.put(e),"rendering: save shared globals in list preset");
    PresetLibrary reader(path);reader.setIncludeBuiltins(false);
    check(reader.load()&&reader.entries().size()==1&&reader.entries()[0]==e,"rendering: library roundtrip");
    auto applied=sample();PresetLibrary::apply(e,applied);
    check(applied.srTarget==s.srTarget&&applied.hdrOutputMode==s.hdrOutputMode&&applied.nrMotion==s.nrMotion&&applied.srMotion==s.srMotion&&applied.fgMotion==s.fgMotion,"rendering: list applies resolution HDR and motion");
    e.contents=presetContentMask(PresetContent::Color);auto untouched=sample();auto before=untouched;PresetLibrary::apply(e,untouched);
    check(ChainGlobalSettings::capture(untouched)==ChainGlobalSettings::capture(before),"rendering: color-only preset leaves shared choices intact");
    const auto sessionPath=scratch(L"field-rendering-session.v1");
    std::filesystem::remove(std::filesystem::path(sessionPath).concat(L".field-render"));
    std::filesystem::remove(std::filesystem::path(sessionPath).concat(L".field-render.p3-globals"));
    auto session=std::make_unique<ChainSession>(ChainSession::initial(s));
    auto restored=std::make_unique<ChainSession>();ChainSessionStore sw(sessionPath),sr(sessionPath);
    check(sw.save(*session)&&sr.load(*restored)&&*session==*restored,"rendering: v4 session without editor restores");
    check(session->select(ChainMode::Node),"rendering: select node mode");
    auto doc=std::make_shared<NodeEditorDocument>();doc->nodes=session->configurations[1].chain;doc->layout.initialize(doc->nodes);doc->globals=ChainGlobalSettings::capture(s);
    session->configurations[1].editor=doc;
    check(sw.save(*session),"rendering: v4 session with editor saves");
    ChainSessionStore nr(sessionPath);check(nr.load(*restored)&&*session==*restored,"rendering: v4 editor globals restore exactly");
}
// Field regressions around the editor payload. It is written by the same encoder
// as the runtime payload, so it needs the same version cap when it is read back;
// without it save() rejected its own output and nothing was ever stored. And a
// brightness change made in the node editor's own globals has to raise the session
// version, or the block is written nowhere and silently reverts.
__declspec(noinline) void testEditorPayloadVersions() {
    const auto path = scratch(L"editor-payload-version.v1.field-render");
    auto session = std::make_unique<ChainSession>(ChainSession::initial(sample()));
    check(session->select(ChainMode::Node), "payload version: initialize node session");
    auto document = std::make_shared<NodeEditorDocument>();
    document->nodes = session->configurations[1].chain;
    check(document->layout.initialize(document->nodes).accepted, "payload version: initialize stable IDs");
    bool movedHdr = false;
    for (uint32_t i = 0; i < document->nodes.nodeCount; ++i) {
        if (document->nodes.nodes[i].type != EffectType::VideoHdr) continue;
        auto& hdr = document->nodes.nodes[i].videoHdr;
        hdr.sourcePeakNits = 1000; hdr.exposureEv100 = 25;
        movedHdr = true;
    }
    check(movedHdr, "payload version: sample chain carries RTX Video HDR");
    session->configurations[1].editor = document;
    ChainSessionStore writer(path);
    check(writer.save(*session), "payload version: v7 editor payload saves");
    check(readBytes(path).find("VEYRA_PRESET_LIBRARY 7") != std::string::npos,
          "payload version: editor payload declares the version it needs");
    auto restored = std::make_unique<ChainSession>(ChainSession::initial({}));
    ChainSessionStore reader(path);
    check(reader.load(*restored) && *restored == *session,
          "payload version: HDR-source route survives save and reload");
    // Only the editor's globals copy is changed here; the runtime configuration
    // keeps the defaults, which is exactly the case the version test used to miss.
    document->globals = static_cast<const ChainGlobalSettings&>(session->configurations[1]);
    document->globals->hdrBrightness.enabled = true;
    document->globals->hdrBrightness.strength = 44;
    document->globals->hdrBrightness.targetPeakNits = 1500;
    document->globals->hdrBrightness.response = 12;
    document->globals->hdrBrightness.transitionMs = 1200;
    check(session->valid() && writer.save(*session), "payload version: editor-only brightness saves");
    check(readBytes(path).starts_with("VEYRA_CHAIN_SESSION 6"),
          "payload version: editor-only transition raises the session version");
    auto reopened = std::make_unique<ChainSession>(ChainSession::initial({}));
    ChainSessionStore rereader(path);
    check(rereader.load(*reopened) && *reopened == *session,
          "payload version: editor-only brightness survives reload");
    check(reopened->configurations[1].editor && reopened->configurations[1].editor->globals &&
          reopened->configurations[1].editor->globals->hdrBrightness == document->globals->hdrBrightness &&
          reopened->configurations[1].hdrBrightness == session->configurations[1].hdrBrightness,
          "payload version: the two globals copies stay separate and exact");
}

void testNrVariants() {
    for (const auto runtime : {NrRuntime::Original, NrRuntime::Ampere, NrRuntime::NvidiaOriginal}) {
        auto settings=sample(); settings.nrRuntime=runtime;
        const auto path=scratch((L"nr-"+std::to_wstring(int(runtime))+L".v1").c_str());
        PresetLibrary writer(path); writer.setIncludeBuiltins(false);
        PresetEntry entry; entry.name=L"NR variant"; entry.chain=toChain(settings);
        entry.color=settings.color; // v1 stores the shared colour payload outside the chain.
        entry.fg={settings.multiplier,settings.frameGenerationBackend};
        check(writer.load()&&writer.put(entry), "NR variant: save preset");
        PresetLibrary reader(path); reader.setIncludeBuiltins(false);
        check(reader.load()&&reader.entries().size()==1&&reader.entries()[0]==entry,
              "NR variant: preset retains exact DLL selection");
        const auto sessionPath=scratch((L"nr-session-"+std::to_wstring(int(runtime))+L".v1").c_str());
        auto session=std::make_unique<ChainSession>(ChainSession::initial(settings));
        check(session->select(ChainMode::Node), "NR variant: initialize node session");
        ChainSessionStore store(sessionPath);
        auto restored=std::make_unique<ChainSession>(ChainSession::initial({}));
        check(store.save(*session)&&store.load(*restored)&&*restored==*session,
              "NR variant: list and node sessions retain exact DLL selection");
    }
}
int main() {
    testNrVariants();
    testRenderingChoices();
    testEditorPayloadVersions();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testLegacyProtectionMigration();
    testNodeEditorSession();
    testEditorPresetParts();
    testEditorPresetStorage();
    testEditorGlobalSettings();
    testNoBuiltinsAndSingleFile();
    testFsrBackendStorage();
    return testExistingPresets();
}
