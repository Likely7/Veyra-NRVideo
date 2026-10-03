#include "veyra/engine/PresetLibrary.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace veyra::engine {
namespace {
// Six full grades per entry (plus the legacy first-grade field), up to 64
// entries. Never remove the file-size guard as LUT names are external input.
constexpr size_t kMaxPresetBytes = 2u * 1024u * 1024u;
void writeRendering(std::ostream& out,const ChainGlobalSettings& g,bool extended=false,bool extendedTransition=false){out<<' '<<int(g.hdrOutputMode)<<' '<<int(g.fgMotion)<<' '<<int(g.srMotion)<<' '<<int(g.nrMotion);
    // Custom: per-scene HDR brightness, written from library v8 / chain session v5 on.
    if(extended)out<<' '<<(g.hdrBrightness.enabled?1:0)<<' '<<g.hdrBrightness.strength<<' '<<g.hdrBrightness.targetPeakNits<<' '<<g.hdrBrightness.response;
    if(extendedTransition)out<<' '<<g.hdrBrightness.transitionMs;}
bool readRendering(std::istream& in,ChainGlobalSettings& g,bool extended=false,bool extendedTransition=false){int hdr,fg,sr,nr;if(!(in>>hdr>>fg>>sr>>nr))return false;g.hdrOutputMode=HdrOutputMode(hdr);g.fgMotion=MotionSource(fg);g.srMotion=MotionSource(sr);g.nrMotion=MotionSource(nr);
    if(extended){int enabled,strength,peak,response;
        if(!(in>>enabled>>strength>>peak>>response)||enabled<0||enabled>1)return false;
        g.hdrBrightness.enabled=enabled!=0;g.hdrBrightness.strength=unsigned(strength);
        g.hdrBrightness.targetPeakNits=unsigned(peak);g.hdrBrightness.response=unsigned(response);}
    if(extendedTransition){int transition;if(!(in>>transition)||transition<0||transition>2000)return false;g.hdrBrightness.transitionMs=unsigned(transition);}
    EnhancementSettings s;g.apply(s);return s.validate().empty();}
void applyRenderingPreset(const PresetEntry& e,EnhancementSettings& s){
    if(!e.globals)return;
    const auto fg=s.frameGenerationBackend;const auto fgMotion=s.fgMotion;
    if(e.contents&presetContentMask(PresetContent::Chain))e.globals->apply(s);
    if(e.contents&presetContentMask(PresetContent::FrameGeneration))s.fgMotion=e.globals->fgMotion;
    else{s.frameGenerationBackend=fg;s.fgMotion=fgMotion;}
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n > 0 ? size_t(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n > 0 ? size_t(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), r.data(), n);
    return r;
}
bool nameOk(const std::wstring& s) {
    return !s.empty() && s.size() <= 48 && s.front() != L' ' && s.back() != L' ' &&
        std::none_of(s.begin(), s.end(), [](wchar_t c) { return c < 32; });
}
} // namespace

void PresetLibrary::addBuiltins() {
    if (!includeBuiltins_) return;
    auto add = [this](const wchar_t* name, const wchar_t* note, ChainMode kind, bool nr, bool sr,
                      uint32_t multiplier, bool hdr, bool color) {
        PresetEntry entry;
        entry.name = name; entry.note = note; entry.kind = kind; entry.builtin = true;
        EnhancementSettings s;
        s.nr = nr; s.sr = sr; s.multiplier = multiplier;
        s.videoHdr.enabled = hdr; s.color.enabled = color;
        entry.chain = toChain(s);
        entry.color = s.color;
        entry.fg = {s.multiplier, FrameGenerationBackend::Dlss};
        entries_.push_back(std::move(entry));
    };
    if (!entries_.empty()) return;
    // Built-ins are the same in both modes: the chain differs only by order,
    // and a list preset describes the product order by construction.
    add(L"原画", L"所有增强关闭", ChainMode::List, false, false, 1, false, false);
    add(L"流畅", L"超分 2K · 补帧 2X", ChainMode::List, false, true, 2, false, false);
    add(L"均衡", L"超分 4K · NR 1 层 · 补帧 2X", ChainMode::List, true, true, 2, false, false);
    add(L"极致", L"超分 4K · NR 1 层 · HDR · 补帧 2X", ChainMode::List, true, true, 2, true, false);
}

bool PresetLibrary::load() {
    error_.clear();
    addBuiltins();
    const auto rootRendering=std::filesystem::path(path_).concat(L".field-render");
    if(std::filesystem::exists(rootRendering))path_=rootRendering;
    const auto migratedPath = std::filesystem::path(path_).concat(L".p3-node");
    if (std::filesystem::exists(migratedPath)) path_ = migratedPath;
    const auto editorPath = std::filesystem::path(path_).concat(L".p3-editor");
    if (std::filesystem::exists(editorPath) ||
        std::filesystem::exists(std::filesystem::path(editorPath).concat(L".p3-globals"))) path_ = editorPath;
    const auto globalsPath = std::filesystem::path(path_).concat(L".p3-globals");
    if (std::filesystem::exists(globalsPath)) path_ = globalsPath;
    const auto renderingPath=std::filesystem::path(path_).concat(L".field-render");
    if(std::filesystem::exists(renderingPath))path_=renderingPath;
    if (!std::filesystem::exists(path_)) return true;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path_, ec);
    if (ec || size > kMaxPresetBytes) { corrupt_ = true; error_ = L"预设库文件过大，原文件保留"; return false; }
    std::ifstream f(path_, std::ios::binary);
    if (!f) { corrupt_ = true; error_ = L"预设库无法读取，原文件保留"; return false; }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<PresetEntry> loaded;
    std::wstring def, parseError;
    bool migrated = false;
    if (!parse(data, loaded, def, parseError, &migrated)) {
        corrupt_ = true;
        error_ = parseError.empty() ? L"预设库损坏或版本不支持；原文件已保留，禁止覆盖" : parseError;
        return false;
    }
    // The file's own presets replace the built-ins; built-ins are re-added so a
    // user who deleted nothing always sees them.
    entries_ = std::move(loaded);
    defaultName_ = def;
    if (migrated) {
        path_ = std::filesystem::path(path_).concat(L".p3-node");
        error_ = L"旧节点预设的 NR 保护区域已移除；其余参数保留，后续保存到 .p3-node 文件，原文件不覆盖";
    }
    addBuiltins();
    for (auto& entry : entries_) entry.builtin = entry.builtin || (entry.name == L"原画" || entry.name == L"流畅" || entry.name == L"均衡" || entry.name == L"极致");
    if (!includeBuiltins_) {
        const bool defaultWasBuiltin = std::any_of(entries_.begin(), entries_.end(), [this](const auto& e) {
            return e.builtin && e.name == defaultName_;
        });
        std::erase_if(entries_, [](const auto& e) { return e.builtin; });
        if (defaultWasBuiltin) defaultName_.clear();
    }
    return true;
}

bool PresetLibrary::exportEntry(size_t index, const std::filesystem::path& target) {
    error_.clear();
    if (index >= entries_.size()) { error_ = L"预设不存在"; return false; }
    auto entry = entries_[index];
    entry.builtin = false;
    const auto data = encodeEntries({entry}, L"");
    std::vector<PresetEntry> checked;
    std::wstring def;
    if (!parse(data, checked, def, error_) || checked.size() != 1) {
        if (error_.empty()) error_ = L"预设无法编码";
        return false;
    }
    std::error_code ec;
    const auto temp = std::filesystem::path(target).concat(L".tmp");
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << data) || !f.flush()) { error_ = L"导出文件无法写入"; return false; }
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) { std::filesystem::remove(temp, ec); error_ = L"导出文件无法替换"; return false; }
    return true;
}

bool PresetLibrary::importFile(const std::filesystem::path& source, std::wstring& nameOut) {
    error_.clear();
    if (corrupt_) { error_ = L"预设库损坏，已拒绝导入"; return false; }
    std::error_code ec;
    const auto size = std::filesystem::file_size(source, ec);
    if (ec || size == 0 || size > kMaxPresetBytes) { error_ = L"预设文件为空或过大"; return false; }
    std::ifstream f(source, std::ios::binary);
    if (!f) { error_ = L"预设文件无法读取"; return false; }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<PresetEntry> loaded;
    std::wstring def, parseError;
    if (!parse(data, loaded, def, parseError) || loaded.empty()) {
        error_ = parseError.empty() ? L"不是 Veyra 预设文件" : parseError;
        return false;
    }
    auto entry = loaded.front();
    entry.builtin = false;
    const std::wstring base = entry.name.empty() ? std::wstring(L"导入的预设") : entry.name;
    auto taken = [this](const std::wstring& n) {
        return std::any_of(entries_.begin(), entries_.end(), [&n](const auto& e) { return e.name == n; });
    };
    entry.name = base;
    for (int n = 2; taken(entry.name) && n < 1000; ++n) entry.name = base.substr(0, 40) + L" " + std::to_wstring(n);
    if (!put(entry, false)) return false;
    nameOut = entry.name;
    return true;
}

bool PresetLibrary::save() {
    if (corrupt_) { error_ = L"预设库损坏，已拒绝覆盖原文件"; return false; }
    const auto data = serialize();
    std::vector<PresetEntry> checked;
    std::wstring checkedDefault;
    // Match PresetStore: never replace a valid file with one our reader rejects.
    if (!parse(data, checked, checkedDefault, error_)) return false;
    if(data.starts_with("VEYRA_PRESET_LIBRARY 6")&&!path_.wstring().ends_with(L".field-render"))path_=std::filesystem::path(path_).concat(L".field-render");
    if (std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e.nodeConfiguration.has_value(); }) &&
        !path_.wstring().ends_with(L".field-render")&&!path_.wstring().ends_with(L".p3-editor") && !path_.wstring().ends_with(L".p3-globals"))
        path_ = std::filesystem::path(path_).concat(L".p3-editor");
    if (std::any_of(entries_.begin(), entries_.end(), [](const auto& e) {
            return e.nodeConfiguration && e.nodeConfiguration->editor && e.nodeConfiguration->editor->globals;
        }) && !path_.wstring().ends_with(L".field-render")&&!path_.wstring().ends_with(L".p3-globals"))
        path_ = std::filesystem::path(path_).concat(L".p3-globals");
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    const auto temporary = std::filesystem::path(path_).concat(L".tmp");
    { std::ofstream f(temporary, std::ios::binary | std::ios::trunc); if (!f) { error_ = L"预设库写入失败"; return false; } f << data; if (!f) { error_ = L"预设库写入失败"; return false; } }
    if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::filesystem::remove(temporary, ec);
        error_ = L"预设库替换失败";
        return false;
    }
    return true;
}

bool PresetLibrary::put(const PresetEntry& entry, bool replace) {
    if (!nameOk(entry.name)) { error_ = L"预设名称非法"; return false; }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const PresetEntry& e) { return e.name == entry.name; });
    if (found != entries_.end()) {
        if (!replace) { error_ = L"同名预设已存在"; return false; }
        if (found->builtin) { error_ = L"内置预设不能覆盖，请换一个名称"; return false; }
        const auto before = *found; *found = entry;
        if (save()) return true;
        *found = before; return false;
    }
    if (entries_.size() >= 64) { error_ = L"预设数量已达上限"; return false; }
    entries_.push_back(entry);
    if (save()) return true;
    entries_.pop_back(); return false;
}

bool PresetLibrary::rename(size_t index, std::wstring name) {
    if (index >= entries_.size() || entries_[index].builtin) { error_ = L"内置预设不能改名"; return false; }
    if (!nameOk(name)) { error_ = L"预设名称非法"; return false; }
    if (std::any_of(entries_.begin(), entries_.end(), [&](const PresetEntry& e) { return e.name == name; })) { error_ = L"同名预设已存在"; return false; }
    const auto oldDefault = defaultName_;
    const auto oldName = entries_[index].name;
    if (defaultName_ == oldName) defaultName_ = name;
    entries_[index].name = std::move(name);
    if (save()) return true;
    entries_[index].name = oldName;
    defaultName_ = oldDefault;
    return false;
}

bool PresetLibrary::duplicate(size_t index) {
    if (index >= entries_.size()) return false;
    if (entries_.size() >= 64) { error_ = L"预设数量已达上限"; return false; }
    PresetEntry copy = entries_[index];
    copy.builtin = false;
    for (int n = 2; n < 100; ++n) {
        std::wstring candidate = copy.name + L" " + std::to_wstring(n);
        if (std::none_of(entries_.begin(), entries_.end(), [&](const PresetEntry& e) { return e.name == candidate; })) { copy.name = std::move(candidate); break; }
    }
    entries_.insert(entries_.begin() + std::ptrdiff_t(index) + 1, std::move(copy));
    if (save()) return true;
    entries_.erase(entries_.begin() + std::ptrdiff_t(index) + 1);
    return false;
}

bool PresetLibrary::erase(size_t index) {
    if (index >= entries_.size()) return false;
    if (entries_[index].builtin) { error_ = L"内置预设不能删除；可以复制后修改"; return false; }
    const auto oldDefault = defaultName_;
    const auto removed = entries_[index];
    if (defaultName_ == entries_[index].name) defaultName_.clear();
    entries_.erase(entries_.begin() + std::ptrdiff_t(index));
    if (save()) return true;
    entries_.insert(entries_.begin() + std::ptrdiff_t(index), removed);
    defaultName_ = oldDefault;
    return false;
}

bool PresetLibrary::setDefault(size_t index) {
    if (index >= entries_.size()) return false;
    const auto oldDefault = defaultName_;
    defaultName_ = entries_[index].name;
    if (save()) return true;
    defaultName_ = oldDefault;
    return false;
}

bool PresetLibrary::clearDefault() {
    const auto oldDefault = defaultName_;
    defaultName_.clear();
    if (save()) return true;
    defaultName_ = oldDefault;
    return false;
}

const std::optional<size_t> PresetLibrary::defaultIndex() const {
    if (defaultName_.empty()) return std::nullopt;
    for (size_t i = 0; i < entries_.size(); ++i) if (entries_[i].name == defaultName_) return i;
    return std::nullopt;
}

void PresetLibrary::apply(const PresetEntry& entry, EnhancementSettings& settings) {
    // fromChain() rewrites every stage field, including the ones this preset
    // does not claim (a colour grade travels inside the chain because the
    // colour stage is part of it). Keep the unclaimed parts exactly as the
    // caller had them, so "chain only" never silently resets a colour look.
    const auto keepColor = settings.color;
    const auto keepAdditionalColors = settings.additionalColors;
    const auto keepAdditionalColorCount = settings.additionalColorCount;
    const auto keepMultiplier = settings.multiplier;
    const auto keepBackend = settings.frameGenerationBackend;
    applyRenderingPreset(entry,settings);
    const auto keepAudioSync = settings.audioSync;
    const auto keepAudioOffset = settings.audioOffsetMs;
    if (entry.contents & presetContentMask(PresetContent::Chain)) fromChain(entry.chain, settings);
    if (entry.contents & presetContentMask(PresetContent::Color)) {
        auto colors = settings;
        fromChain(entry.chain, colors);
        settings.color = entry.color; // legacy single-grade compatibility field
        settings.additionalColors = colors.additionalColors;
        settings.additionalColorCount = colors.additionalColorCount;
    } else {
        settings.color = keepColor;
        settings.additionalColors = keepAdditionalColors;
        settings.additionalColorCount = keepAdditionalColorCount;
    }
    if (entry.contents & presetContentMask(PresetContent::FrameGeneration)) {
        settings.multiplier = std::max(1u, entry.fg.multiplier);
        settings.frameGenerationBackend = entry.fg.backend;
    } else {
        settings.multiplier = keepMultiplier;
        settings.frameGenerationBackend = keepBackend;
    }
    if (entry.contents & presetContentMask(PresetContent::Audio)) {
        settings.audioSync = entry.audioSync;
        settings.audioOffsetMs = entry.audioOffsetMs;
    } else {
        settings.audioSync = keepAudioSync;
        settings.audioOffsetMs = keepAudioOffset;
    }
}

ChainValidation PresetLibrary::applyToChain(const PresetEntry& entry, EffectChain& chain,
                                           EnhancementSettings& settings) {
    if (entry.kind != chain.mode) return {false, "预设编辑模式不匹配"};
    if (entry.contents & ~kPresetAllContent) return {false, "预设内容类型无效"};
    if (const auto v = validateChain(chain); !v.accepted) return v;
    if (const auto v = validateChain(entry.chain); !v.accepted) return v;
    const bool hasChain = (entry.contents & presetContentMask(PresetContent::Chain)) != 0;
    const bool hasColor = (entry.contents & presetContentMask(PresetContent::Color)) != 0;
    const bool hasFg = (entry.contents & presetContentMask(PresetContent::FrameGeneration)) != 0;
    auto nextSettings = settings;
    apply(entry, nextSettings);
    auto next = hasChain ? entry.chain : chain;
    next.mode = chain.mode;

    if (hasChain || hasColor) {
        // Do not round-trip through toChain(): that canonicalises node order
        // and erases positions, even for presets that only carry audio.
        const auto& source = hasColor ? entry.chain : chain;
        std::vector<ChainNode> colors;
        for (uint32_t i = 0; i < source.nodeCount; ++i)
            if (source.nodes[i].type == EffectType::Color) colors.push_back(source.nodes[i]);
        if (hasColor) {
            // Legacy colour-only entries can carry the grade outside the chain.
            if (colors.empty() && (!hasChain || entry.color.enabled)) {
                ChainNode color; color.type = EffectType::Color; colors.push_back(color);
            }
            if (!colors.empty()) {
                colors.front().color = entry.color;
                colors.front().enabled = entry.color.enabled;
            }
        }
        const auto needed = next.nodeCount - next.countOf(EffectType::Color) + colors.size();
        if (needed > kMaxChainNodes) return {false, "合并后的节点数量超出上限"};
        auto arranged = next;
        arranged.nodes = {}; arranged.nodeCount = 0;
        size_t colorIndex = 0;
        uint32_t insertAt = 0;
        for (uint32_t i = 0; i < next.nodeCount; ++i) {
            auto node = next.nodes[i];
            if (node.type == EffectType::Color) {
                if (colorIndex == colors.size()) continue;
                node.color = colors[colorIndex].color;
                node.enabled = colors[colorIndex++].enabled;
                arranged.nodes[arranged.nodeCount++] = node;
                insertAt = arranged.nodeCount;
            } else arranged.nodes[arranged.nodeCount++] = node;
        }
        // New instances have the saved positions; existing slots keep theirs.
        while (colorIndex < colors.size()) {
            for (uint32_t i = arranged.nodeCount; i > insertAt; --i)
                arranged.nodes[i] = arranged.nodes[i - 1];
            arranged.nodes[insertAt++] = colors[colorIndex++];
            ++arranged.nodeCount;
        }
        next = arranged;
    }
    if (hasChain || hasFg) {
        if (!hasFg) next.fgMultiplier = chain.fgMultiplier;
        auto* fg = next.firstOf(EffectType::FrameGeneration);
        if (!fg && nextSettings.multiplier > 1) {
            if (next.nodeCount == kMaxChainNodes) return {false, "合并后无法添加补帧节点"};
            fg = &next.nodes[next.nodeCount++];
            *fg = {}; fg->type = EffectType::FrameGeneration;
        }
        if (fg) fg->enabled = nextSettings.multiplier > 1;
        if (nextSettings.multiplier > 1) next.fgMultiplier = nextSettings.multiplier;
    }
    if (const auto v = validateChain(next); !v.accepted) return v;
    if (hasChain || hasColor) fromChain(next, nextSettings);
    if (!nextSettings.validate().empty()) return {false, "预设参数无效"};
    chain = next; settings = nextSettings;
    return {};
}

ChainValidation PresetLibrary::applyToEditor(const PresetEntry& entry, NodeEditorDocument& document,
                                            EnhancementSettings& settings) {
    if (entry.kind != ChainMode::Node || document.nodes.mode != ChainMode::Node)
        return {false, "预设编辑模式不匹配"};
    if (entry.contents & ~kPresetAllContent) return {false, "预设内容类型无效"};
    if (auto v = document.layout.validate(document.nodes); !v.accepted) return v;
    if (auto v = validateChain(entry.chain); !v.accepted) return v;
    if (entry.nodeConfiguration && (!entry.nodeConfiguration->valid() || entry.nodeConfiguration->chain != entry.chain))
        return {false, "预设编辑快照无效"};
    auto next = std::make_unique<NodeEditorDocument>(document);
    auto& nodes = next->nodes; auto& layout = next->layout;
    auto values = settings;
    if (document.globals) document.globals->apply(values);
    if ((entry.contents & presetContentMask(PresetContent::Chain)) && entry.nodeConfiguration) {
        const auto& saved = *entry.nodeConfiguration;
        if (saved.editor) *next = *saved.editor;
        else { nodes = saved.chain; if (auto v = layout.initialize(nodes); !v.accepted) return v; }
        saved.apply(values);
        if (saved.editor && saved.editor->globals) saved.editor->globals->apply(values);
        if (!(entry.contents & presetContentMask(PresetContent::Color))) {
            PresetEntry keep; keep.kind = ChainMode::Node; keep.chain.mode = ChainMode::Node;
            keep.contents = presetContentMask(PresetContent::Color);
            for (uint32_t i = 0; i < document.nodes.nodeCount; ++i)
                if (document.nodes.nodes[i].type == EffectType::Color)
                    keep.chain.nodes[keep.chain.nodeCount++] = document.nodes.nodes[i];
            if (keep.chain.nodeCount) {
                keep.color = keep.chain.nodes[0].color; keep.color.enabled = keep.chain.nodes[0].enabled;
                if (auto v = applyToEditor(keep, *next, values); !v.accepted) return v;
            } else {
                for (uint32_t i = nodes.nodeCount; i > 0; --i)
                    if (nodes.nodes[i-1].type == EffectType::Color)
                        if (auto v = layout.remove(nodes, layout.ids[i-1]); !v.accepted) return v;
            }
        }
        if (!(entry.contents & presetContentMask(PresetContent::FrameGeneration))) {
            PresetEntry keep; keep.kind = ChainMode::Node; keep.chain.mode = ChainMode::Node;
            keep.contents = presetContentMask(PresetContent::FrameGeneration);
            const auto* old = document.nodes.firstOf(EffectType::FrameGeneration);
            keep.fg = {old && old->enabled ? document.nodes.fgMultiplier : 1, settings.frameGenerationBackend};
            if (auto v = applyToEditor(keep, *next, values); !v.accepted) return v;
            nodes.fgStrictAdmission = document.nodes.fgStrictAdmission;
        }
        if (entry.contents & presetContentMask(PresetContent::Audio)) {
            values.audioSync = entry.audioSync; values.audioOffsetMs = entry.audioOffsetMs;
        }
    } else if (entry.contents & presetContentMask(PresetContent::Chain)) {
        // Whole-chain replacement discards the old order, not saved colours.
        // Sort only the helper INPUT, so detached tail nodes do not reject it.
        const auto rank = [](const ChainNode& n) {
            switch (n.type) {
            case EffectType::Color: return 0;
            case EffectType::SuperResolution: return 1;
            case EffectType::NrEnhance: return 2;
            case EffectType::VideoHdr: return 3;
            default: return 4;
            }
        };
        std::stable_sort(nodes.nodes.begin(), nodes.nodes.begin() + nodes.nodeCount,
                         [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
        if (auto v = applyToChain(entry, nodes, values); !v.accepted) return v;
        if (auto v = layout.initialize(nodes); !v.accepted) return v;
    } else {
        const auto append = [&](ChainNode node, uint32_t after, bool attach) -> ChainValidation {
            if (nodes.nodeCount >= kMaxChainNodes || layout.nextId == (std::numeric_limits<uint32_t>::max)())
                return {false, "节点容量或标识已耗尽"};
            const uint32_t index = nodes.nodeCount++, id = layout.nextId++;
            // New slots are placed below existing cards, not on top of them.
            for (uint32_t i = 0; i < index; ++i)
                node.viewY = (std::max)(node.viewY, nodes.nodes[i].viewY + 300);
            nodes.nodes[index] = node; layout.ids[index] = id; layout.next[index] = 0;
            if (attach) {
                auto& edge = after == NodeGraphLayout::Input ? layout.inputNext :
                    layout.next[size_t(layout.indexOf(nodes, after))];
                layout.next[index] = edge; edge = id;
            }
            return layout.validate(nodes);
        };
        if (entry.contents & presetContentMask(PresetContent::Color)) {
            std::vector<ChainNode> incoming; std::vector<uint32_t> existing;
            const auto& source = entry.nodeConfiguration && entry.nodeConfiguration->editor ?
                entry.nodeConfiguration->editor->nodes : entry.chain;
            for (uint32_t i = 0; i < source.nodeCount; ++i)
                if (source.nodes[i].type == EffectType::Color) incoming.push_back(source.nodes[i]);
            if (incoming.empty()) { ChainNode n; n.type = EffectType::Color; incoming.push_back(n); }
            incoming.front().color = entry.color; incoming.front().enabled = entry.color.enabled;
            for (uint32_t i = 0; i < nodes.nodeCount; ++i)
                if (nodes.nodes[i].type == EffectType::Color) existing.push_back(layout.ids[i]);
            const size_t kept = (std::min)(incoming.size(), existing.size());
            for (size_t i = 0; i < kept; ++i) {
                auto& node = nodes.nodes[size_t(layout.indexOf(nodes, existing[i]))];
                node.color = incoming[i].color; node.enabled = incoming[i].enabled;
            }
            for (size_t i = existing.size(); i > kept; --i)
                if (auto v = layout.remove(nodes, existing[i - 1]); !v.accepted) return v;
            uint32_t after = kept ? existing[kept - 1] : NodeGraphLayout::Input;
            for (size_t i = kept; i < incoming.size(); ++i) {
                if (auto v = append(incoming[i], after, true); !v.accepted) return v;
                after = layout.ids[nodes.nodeCount - 1];
            }
        }
        if (entry.contents & presetContentMask(PresetContent::FrameGeneration)) {
            auto* node = nodes.firstOf(EffectType::FrameGeneration);
            if (!node && entry.fg.multiplier > 1) {
                uint32_t after = NodeGraphLayout::Input, end = layout.inputNext;
                while (end > NodeGraphLayout::Output) {
                    after = end; end = layout.next[size_t(layout.indexOf(nodes, end))];
                }
                ChainNode added; added.type = EffectType::FrameGeneration;
                if (auto v = append(added, after, end == NodeGraphLayout::Output); !v.accepted) return v;
                node = nodes.firstOf(EffectType::FrameGeneration);
            }
            if (node) node->enabled = entry.fg.multiplier > 1;
            if (entry.fg.multiplier > 1) nodes.fgMultiplier = entry.fg.multiplier;
            values.frameGenerationBackend = entry.fg.backend;
        }
        if (entry.contents & presetContentMask(PresetContent::Audio)) {
            values.audioSync = entry.audioSync; values.audioOffsetMs = entry.audioOffsetMs;
        }
    }
    if (auto v = layout.validate(nodes); !v.accepted) return v;
    auto runtime = std::make_unique<EffectChain>(); runtime->mode = ChainMode::Node;
    const auto projected = layout.project(nodes, *runtime);
    if (projected.accepted) fromChain(*runtime, values);
    else {
        uint32_t end = layout.inputNext;
        while (end > NodeGraphLayout::Output) end = layout.next[size_t(layout.indexOf(nodes, end))];
        if (end != NodeGraphLayout::Input) return projected;
    }
    applyRenderingPreset(entry,values);
    if (next->globals || ChainGlobalSettings::capture(values) != ChainGlobalSettings::capture(settings))
        next->globals = ChainGlobalSettings::capture(values);
    auto contract = std::make_unique<ChainConfiguration>(ChainConfiguration::capture(*runtime, values));
    contract->editor = std::make_shared<NodeEditorDocument>(*next);
    // An incomplete draft may choose XeSS 2X while the accepted runtime is
    // still DLSS 6X. Never validate that accidental flat-settings combination.
    // The document contract checks every detached node against draft globals.
    auto projectedValues = values; fromChain(*runtime, projectedValues);
    if (!contract->valid() || !projectedValues.validate().empty()) return {false, "预设节点参数无效"};
    document = *next; settings = values; return {};
}

bool PresetLibrary::importLegacy(const std::vector<PresetEntry>& incoming) {
    bool changed = false;
    for (const auto& entry : incoming) {
        if (!nameOk(entry.name)) continue;
        const bool duplicateName = std::any_of(entries_.begin(), entries_.end(), [&](const PresetEntry& e) { return e.name == entry.name; });
        if (duplicateName) continue;   // never overwrite what the user already has
        if (entries_.size() >= 64) break;
        entries_.push_back(entry);
        changed = true;
    }
    return changed ? save() : true;
}

std::string PresetLibrary::serialize() const {
    return encodeEntries(entries_, defaultName_);
}

std::string PresetLibrary::encodeEntries(const std::vector<PresetEntry>& entries_, const std::wstring& defaultName_, int minimumVersion) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(std::numeric_limits<float>::max_digits10);
    const bool multi = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) {
        return std::count_if(e.chain.nodes.begin(), e.chain.nodes.begin() + e.chain.nodeCount,
                            [](const auto& n) { return n.type == EffectType::Color; }) > 1;
    });
    const bool layerSizes=std::any_of(entries_.begin(),entries_.end(),[](const auto& e){
        return std::any_of(e.chain.nodes.begin(),e.chain.nodes.begin()+e.chain.nodeCount,[](const auto& n){
            return n.type==EffectType::NrEnhance&&n.nr.sizePolicy!=pipeline::NrSizePolicy::Realtime;
        });
    });
    const bool editor = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e.nodeConfiguration.has_value(); });
    const bool globals = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) {
        return e.nodeConfiguration && e.nodeConfiguration->editor && e.nodeConfiguration->editor->globals;
    });
    const bool rendering=std::any_of(entries_.begin(),entries_.end(),[](const auto& e){return e.globals.has_value();});
    // Custom v7: RTX Video HDR's HDR-source route and its HDR->SDR tuning.
    const bool hdrMap = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) {
        return std::any_of(e.chain.nodes.begin(), e.chain.nodes.begin() + e.chain.nodeCount, [](const auto& n) {
            return n.type == EffectType::VideoHdr && (n.videoHdr.convertHdrSource || n.videoHdr.sourcePeakNits ||
                n.videoHdr.sdrWhiteNits != 203 || n.videoHdr.exposureEv100 || n.videoHdr.shoulderPercent != 100);
        });
    });
    // Custom v8: per-scene HDR brightness rides in the globals block.
    const auto brightOf=[](const PresetEntry& e)->HdrBrightnessSettings{
        if(e.globals)return e.globals->hdrBrightness;
        if(e.nodeConfiguration&&e.nodeConfiguration->editor&&e.nodeConfiguration->editor->globals)
            return e.nodeConfiguration->editor->globals->hdrBrightness;
        return {};
    };
    const bool hdrBright=std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){
        return brightOf(e)!=HdrBrightnessSettings{};});
    const bool hdrTransition=hdrBright&&std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return brightOf(e).transitionMs!=1000;});
    const int version=std::max(minimumVersion, hdrTransition?9:hdrBright?8:hdrMap?7:rendering?6:globals?5:editor?4:layerSizes?3:multi?2:1);
    o << "VEYRA_PRESET_LIBRARY " << version << '\n' << std::quoted(utf8(defaultName_)) << ' ' << entries_.size() << '\n';
    for (const auto& e : entries_) {
        o << std::quoted(utf8(e.name)) << ' ' << std::quoted(utf8(e.note)) << ' '
          << int(e.kind) << ' ' << (e.builtin ? 1 : 0) << ' ' << e.contents << ' '
          << e.fg.multiplier << ' ' << int(e.fg.backend) << ' ' << int(e.audioSync) << ' ' << e.audioOffsetMs << ' '
          << e.chain.nodeCount << ' ' << int(e.chain.mode) << ' ' << e.chain.fgMultiplier << ' '
          << (e.chain.fgStrictAdmission ? 1 : 0) << ' ';
        for (uint32_t i = 0; i < e.chain.nodeCount; ++i) {
            const auto& n = e.chain.nodes[i];
            o << int(n.type) << ' ' << (n.enabled ? 1 : 0) << ' ' << n.viewX << ' ' << n.viewY << ' '
              << n.nr.model.intensity << ' ' << n.nr.model.tone << ' ' << n.nr.model.structure << ' '
              << n.nr.model.skin << ' ' << n.nr.model.style << ' ' << n.nr.model.autoMask << ' ' << n.nr.model.uiCorrection << ' '
              << n.nr.residual.total << ' ' << n.nr.residual.darken << ' ' << n.nr.residual.brighten << ' '
              << n.nr.residual.color << ' ' << n.nr.residual.luminance << ' '
              << int(n.nr.runtime) << ' ' << (n.nr.temporal ? 1 : 0) << ' ' << (n.nr.lowLatencyPairing ? 1 : 0) << ' '
              << (n.protection.enabled ? 1 : 0) << ' ' << n.protection.featherPixels;
            // An ellipse is stored with left and right swapped (no format change;
            // earlier files never hold an inverted box).
            for (const auto& r : n.protection.regions)
                o << ' ' << (r.ellipse ? r.right : r.left) << ' ' << r.top << ' ' << (r.ellipse ? r.left : r.right) << ' ' << r.bottom;
            o << ' ' << (n.videoHdr.enabled ? 1 : 0) << ' ' << n.videoHdr.contrast << ' ' << n.videoHdr.saturation << ' '
              << n.videoHdr.middleGray << ' ' << n.videoHdr.peakNits;
            // v7 appends RTX Video HDR's HDR-source route and its HDR->SDR
            // tuning. Written for every node once the file is v7, so the rows
            // stay aligned; the reader only takes them at version>=7.
            if (version >= 7) o << ' ' << (n.videoHdr.convertHdrSource ? 1 : 0) << ' ' << n.videoHdr.sourcePeakNits << ' '
                << n.videoHdr.sdrWhiteNits << ' ' << n.videoHdr.exposureEv100 << ' ' << n.videoHdr.shoulderPercent;
            o << ' ';
            if(version>=3)o<<int(n.nr.sizePolicy)<<' ';
            if (version>=2 && n.type == EffectType::Color) {
                writeColorSettings(o, n.color, utf8(n.color.lutNameString()));
                o << ' ';
            }
        }
        o << ' ';
        writeColorSettings(o, e.color, utf8(e.color.lutNameString()));
        if (version >= 4) {
            o << ' ' << e.nodeConfiguration.has_value();
            if (e.nodeConfiguration) {
                auto session = std::make_unique<ChainSession>(ChainSession::initial({}));
                session->active = ChainMode::Node; session->initialized[1] = true;
                session->configurations[1] = *e.nodeConfiguration;
                o << ' ' << std::quoted(ChainSessionStore::encode(*session));
            }
        }
        if(version>=6){
            o<<' '<<e.globals.has_value();
            if(e.globals){const auto& g=*e.globals;o<<' '<<int(g.srTarget)<<' '<<g.videoSrQuality<<' '<<int(g.fgBackend)<<' '<<int(g.flow)<<' '<<int(g.opticalFlowBackend)<<' '<<g.amdFlowHalfResolution<<' '<<int(g.nrPolicy);writeRendering(o,g,version>=8,version>=9);}
        }
        o << '\n';
    }
    return o.str();
}

bool PresetLibrary::parse(const std::string& data, std::vector<PresetEntry>& out, std::wstring& def, std::wstring& error,
                          bool* migrated, bool preserveLegacy, int maxVersion) {
    if (migrated) *migrated = false;
    if (data.size() > kMaxPresetBytes) { error = L"预设库超过 2 MiB"; return false; }
    std::istringstream in(data);
    in.imbue(std::locale::classic());
    std::string magic, quoted;
    int version = 0;
    size_t count = 0;
    // maxVersion 0 keeps the original per-context cap; the chain session passes 7
    // explicitly so it can hold the HDR-source fields it writes itself.
    const int allowedVersion = maxVersion > 0 ? maxVersion : (preserveLegacy ? 3 : 9);
    if (!(in >> magic >> version) || magic != "VEYRA_PRESET_LIBRARY" || version < 1 || version > allowedVersion) { error = L"预设库格式或版本不支持"; return false; }
    if (!(in >> std::quoted(quoted) >> count) || count > 64) { error = L"预设库条目数非法"; return false; }
    def = wide(quoted);
    for (size_t i = 0; i < count; ++i) {
        PresetEntry e;
        std::string name, note;
        int kind = 0, builtin = 0, fgBackend = 0, audioSync = 0, chainMode = 0, strict = 0;
        uint32_t nodeCount = 0;
        if (!(in >> std::quoted(name) >> std::quoted(note) >> kind >> builtin >> e.contents >> e.fg.multiplier >> fgBackend >> audioSync >> e.audioOffsetMs >> nodeCount >> chainMode >> e.chain.fgMultiplier >> strict)) { error = L"预设库头部字段损坏"; return false; }
        if (kind < 0 || kind > 1 || chainMode < 0 || chainMode > 1 || strict < 0 || strict > 1 ||
            nodeCount > kMaxChainNodes || e.fg.multiplier < 1 || e.fg.multiplier > 6) { error = L"预设库字段超出范围"; return false; }
        e.name = wide(name);
        e.note = wide(note);
        e.kind = static_cast<ChainMode>(kind);
        e.builtin = builtin != 0;
        e.fg.backend = static_cast<FrameGenerationBackend>(fgBackend);
        EnhancementSettings fgContract;
        fgContract.multiplier=e.fg.multiplier;fgContract.frameGenerationBackend=e.fg.backend;
        if(!fgContract.validate().empty()){error=L"预设库补帧后端或倍率无效";return false;}
        e.audioSync = static_cast<AudioSyncMode>(audioSync);
        e.chain.nodeCount = nodeCount;
        e.chain.mode = static_cast<ChainMode>(chainMode);
        e.chain.fgStrictAdmission = strict != 0;
        if (!nameOk(e.name)) { error = L"预设库里的名称非法"; return false; }
        if (e.audioOffsetMs < -250 || e.audioOffsetMs > 250) { error = L"预设库音频偏移超出范围"; return false; }
        for (uint32_t n = 0; n < nodeCount; ++n) {
            ChainNode& node = e.chain.nodes[n];
            int type = 0, enabled = 0, runtime = 0, temporal = 0, lowLatency = 0, protEnabled = 0, hdrEnabled = 0;
            if (!(in >> type >> enabled >> node.viewX >> node.viewY
                  >> node.nr.model.intensity >> node.nr.model.tone >> node.nr.model.structure >> node.nr.model.skin
                  >> node.nr.model.style >> node.nr.model.autoMask >> node.nr.model.uiCorrection
                  >> node.nr.residual.total >> node.nr.residual.darken >> node.nr.residual.brighten
                  >> node.nr.residual.color >> node.nr.residual.luminance
                  >> runtime >> temporal >> lowLatency >> protEnabled >> node.protection.featherPixels)) { error = L"预设库节点字段损坏"; return false; }
            for (auto& r : node.protection.regions) {
                if (!(in >> r.left >> r.top >> r.right >> r.bottom)) { error = L"预设库保护区域损坏"; return false; }
                if (r.left > r.right) { std::swap(r.left, r.right); r.ellipse = true; }
            }
            if (!(in >> hdrEnabled >> node.videoHdr.contrast >> node.videoHdr.saturation >> node.videoHdr.middleGray >> node.videoHdr.peakNits)) { error = L"预设库 HDR 字段损坏"; return false; }
            // Custom v7: RTX Video HDR's HDR-source route and HDR->SDR tuning.
            if (version >= 7) {
                int convert = 0, srcPeak = 0, whiteNits = 203, ev = 0, shoulder = 100;
                if (!(in >> convert >> srcPeak >> whiteNits >> ev >> shoulder)) { error = L"预设库 HDR 色调映射字段损坏"; return false; }
                if (convert < 0 || convert > 1 || srcPeak < 0 || srcPeak > 4000 || whiteNits < 80 || whiteNits > 400 ||
                    ev < -200 || ev > 200 || shoulder < 50 || shoulder > 150) { error = L"预设库 HDR 色调映射取值超出范围"; return false; }
                node.videoHdr.convertHdrSource = convert != 0;
                node.videoHdr.sourcePeakNits = unsigned(srcPeak);
                node.videoHdr.sdrWhiteNits = unsigned(whiteNits);
                node.videoHdr.exposureEv100 = ev;
                node.videoHdr.shoulderPercent = unsigned(shoulder);
            }
            if (type < 0 || type >= int(EffectType::Count) || enabled < 0 || enabled > 1 || !validNrRuntime(static_cast<NrRuntime>(runtime)) ||
                temporal < 0 || temporal > 1 || lowLatency < 0 || lowLatency > 1 || protEnabled < 0 || protEnabled > 1 || hdrEnabled < 0 || hdrEnabled > 1) {
                error = L"预设库节点取值超出范围"; return false;
            }
            node.type = static_cast<EffectType>(type);
            node.enabled = enabled != 0;
            node.nr.runtime = currentNrRuntime(static_cast<NrRuntime>(runtime));
            node.nr.temporal = temporal != 0;
            node.nr.lowLatencyPairing = lowLatency != 0;
            node.protection.enabled = protEnabled != 0;
            node.videoHdr.enabled = hdrEnabled != 0;
            if(version>=3){int size;
                if(!(in>>size)||!pipeline::validNrSizePolicy(static_cast<pipeline::NrSizePolicy>(size))){error=L"预设库 NR 分辨率非法";return false;}
                node.nr.sizePolicy=static_cast<pipeline::NrSizePolicy>(size);
            }
            if (version >= 2 && node.type == EffectType::Color) {
                std::string lut;
                if (!readColorSettings(in, node.color, lut, 19) ||
                    (!lut.empty() && !node.color.setLutName(wide(lut))) || !node.color.validate().empty()) {
                    error = L"预设库节点调色参数损坏"; return false;
                }
            }
        }
        in >> std::ws;
        std::string lut;
        // 19 is the current colour-block schema: it includes the per-section
        // bypass mask that the writer always emits.
        if (!readColorSettings(in, e.color, lut, 19)) { error = L"预设库调色字段损坏"; return false; }
        if (!lut.empty() && !e.color.setLutName(wide(lut))) { error = L"预设库 LUT 名称非法"; return false; }
        if (auto problem = e.color.validate(); !problem.empty()) { error = L"预设库调色参数超出范围"; return false; }
        // v1 stores its only grade after the chain, not in the Color node.
        // Restore that payload before a node-mode caller uses the chain itself.
        if (version == 1) {
            if (auto* color = e.chain.firstOf(EffectType::Color)) color->color = e.color;
        }
        auto compatible = e.chain;
        if (removeLegacyNodeProtection(compatible) && migrated) *migrated = true;
        if (auto verdict = validateChain(compatible); !verdict.accepted) { error = L"预设库链路违反顺序规则"; return false; }
        if (!preserveLegacy) e.chain = compatible;
        if (version >= 4) {
            int hasEditor = 0;
            if (!(in >> hasEditor) || hasEditor < 0 || hasEditor > 1) return false;
            if (hasEditor) {
                std::string body; auto session = std::make_unique<ChainSession>();
                if (!(in >> std::quoted(body)) || e.kind != ChainMode::Node ||
                    !ChainSessionStore::decode(body, *session) || !session->valid() ||
                    session->active != ChainMode::Node || session->configurations[1].chain != e.chain) {
                    error = L"预设编辑文档无效，原状态保留"; return false;
                }
                e.nodeConfiguration = session->configurations[1];
            }
        }
        if(version>=6){
            int present;if(!(in>>present)||present<0||present>1)return false;
            if(present){ChainGlobalSettings g;int target,backend,flow,optical,half,policy;
                if(!(in>>target>>g.videoSrQuality>>backend>>flow>>optical>>half>>policy)||half<0||half>1)return false;
                g.srTarget=pipeline::SrTarget(target);g.fgBackend=FrameGenerationBackend(backend);g.flow=FlowQuality(flow);g.opticalFlowBackend=OpticalFlowBackend(optical);g.amdFlowHalfResolution=half!=0;g.nrPolicy=pipeline::NrSizePolicy(policy);
                if(!readRendering(in,g,version>=8,version>=9))return false;e.globals=g;
            }
        }
        out.push_back(std::move(e));
    }
    in >> std::ws;
    if (!in.eof()) { error = L"预设库尾部有多余数据"; return false; }
    return true;
}

std::string ChainSessionStore::encode(const ChainSession& session) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto& editor = session.configurations[1].editor;
    const auto extended=[](const ChainGlobalSettings& g){return g.srTarget>pipeline::SrTarget::Uhd8K||g.hdrOutputMode!=HdrOutputMode::Hdr10||g.fgMotion!=MotionSource::Automatic||g.srMotion!=MotionSource::OpticalFlow||g.nrMotion!=MotionSource::OpticalFlow;};
    const bool rendering=std::any_of(session.configurations.begin(),session.configurations.end(),[&](const auto& c){return extended(c)||(c.editor&&c.editor->globals&&extended(*c.editor->globals));});
    // Custom: the session's own version 5 carries the HDR brightness globals.
    // Compare against the settings' own defaults: the old literals (60/50) came
    // from defaults this fork changed, so every session looked customised.
    // The node editor keeps its own globals copy, and that copy is written too,
    // so either one being off-default has to raise the version.
    const auto brightCustom=[](const ChainConfiguration& c){
        return c.hdrBrightness!=HdrBrightnessSettings{} ||
            (c.editor&&c.editor->globals&&c.editor->globals->hdrBrightness!=HdrBrightnessSettings{});};
    const auto transitionCustom=[](const ChainConfiguration& c){
        return c.hdrBrightness.transitionMs!=1000 ||
            (c.editor&&c.editor->globals&&c.editor->globals->hdrBrightness.transitionMs!=1000);};
    const bool hdrBright=std::any_of(session.configurations.begin(),session.configurations.end(),brightCustom);
    const bool hdrTransition=hdrBright&&std::any_of(session.configurations.begin(),session.configurations.end(),transitionCustom);
    const int version = hdrTransition?6:hdrBright?5:rendering?4:editor ? (editor->globals ? 3 : 2) : 1;
    out << "VEYRA_CHAIN_SESSION " << version << '\n' << int(session.active) << ' ' << session.initialized[0] << ' ' << session.initialized[1];
    if(version>=4)out<<' '<<bool(editor);out<<'\n';
    std::vector<PresetEntry> entries;
    for (size_t i = 0; i < session.configurations.size(); ++i) {
        const auto& c = session.configurations[i];
        out << int(c.srTarget) << ' ' << c.videoSrQuality << ' ' << int(c.fgBackend) << ' '
            << int(c.flow) << ' ' << int(c.opticalFlowBackend) << ' ' << c.amdFlowHalfResolution << ' '
            << int(c.nrPolicy) << ' ' << c.selectedNr << ' ' << c.selectedColour;
        if(version>=4)writeRendering(out,c,version>=5,version>=6);out<<'\n';
        PresetEntry entry;
        entry.name = i == 0 ? L"list" : L"node";
        entry.kind = ChainMode(i); entry.chain = c.chain;
        entry.fg = {c.chain.fgMultiplier, c.fgBackend};
        if (const auto color = c.chain.firstOf(EffectType::Color)) entry.color = color->color;
        entries.push_back(std::move(entry));
    }
    // v3 always carries full per-node colours, even with just one Color node.
    out << std::quoted(PresetLibrary::encodeEntries(entries, {}, 3)) << '\n';
    if (editor) {
        const auto& document = *session.configurations[1].editor;
        const auto& graph = document.layout;
        if(version>=4)out<<bool(document.globals)<<'\n';
        if (version>=3&&document.globals) {
            const auto& g = *document.globals;
            out << int(g.srTarget) << ' ' << g.videoSrQuality << ' ' << int(g.fgBackend) << ' '
                << int(g.flow) << ' ' << int(g.opticalFlowBackend) << ' ' << g.amdFlowHalfResolution << ' '
                << int(g.nrPolicy);if(version>=4)writeRendering(out,g,version>=5,version>=6);out<<'\n';
        }
        out << graph.nextId << ' ' << graph.inputNext << ' ' << document.nodes.nodeCount << '\n';
        // The ordinary preset codec validates a linear runtime chain. Use a
        // disabled envelope for the editor-only unordered payload, with the
        // exact enabled flags stored alongside IDs/edges and restored below.
        // This does not weaken ordinary preset import or runtime validation.
        auto entry = std::make_unique<PresetEntry>();
        entry->name = L"editor"; entry->kind = ChainMode::Node; entry->chain = document.nodes;
        entry->fg = {document.nodes.fgMultiplier, session.configurations[1].fgBackend};
        for (uint32_t i = 0; i < document.nodes.nodeCount; ++i) {
            out << graph.ids[i] << ' ' << graph.next[i] << ' ' << document.nodes.nodes[i].enabled << '\n';
            entry->chain.nodes[i].enabled = false;
        }
        std::vector<PresetEntry> payload; payload.push_back(std::move(*entry));
        out << std::quoted(PresetLibrary::encodeEntries(payload, {}, 3)) << '\n';
    }
    return out.str();
}

bool ChainSessionStore::decode(const std::string& data, ChainSession& session, bool* migrated) {
    if (migrated) *migrated = false;
    if (data.size() > kMaxPresetBytes) return false;
    std::istringstream in(data); in.imbue(std::locale::classic());
    std::string magic, body; int version = 0, active = 0, list = 0, node = 0;
    if (!(in >> magic >> version >> active >> list >> node) || magic != "VEYRA_CHAIN_SESSION" ||
        (version < 1 || version > 6) || active < 0 || active > 1 || list != 1 || node < 0 || node > 1) return false;
    int hasEditor=version>=2?1:0;
    if(version>=4&&(!(in>>hasEditor)||hasEditor<0||hasEditor>1))return false;
    ChainSession parsed; parsed.active = ChainMode(active); parsed.initialized = {true, node != 0};
    for (auto& c : parsed.configurations) {
        int target, backend, flow, optical, half, policy;
        if (!(in >> target >> c.videoSrQuality >> backend >> flow >> optical >> half >> policy >> c.selectedNr >> c.selectedColour) ||
            half < 0 || half > 1) return false;
        c.srTarget = pipeline::SrTarget(target); c.fgBackend = FrameGenerationBackend(backend);
        c.flow = FlowQuality(flow); c.opticalFlowBackend = OpticalFlowBackend(optical);
        c.amdFlowHalfResolution = half != 0; c.nrPolicy = pipeline::NrSizePolicy(policy);
        if(version>=4&&!readRendering(in,c,version>=5,version>=6))return false;
    }
    if (!(in >> std::quoted(body))) return false;
    std::vector<PresetEntry> entries; std::wstring def, error;
    if (!PresetLibrary::parse(body, entries, def, error, nullptr, true, 9) || entries.size() != 2 || !def.empty()) return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].kind != ChainMode(i)) return false;
        parsed.configurations[i].chain = entries[i].chain;
        auto& configuration = parsed.configurations[i];
        if (removeLegacyNodeProtection(configuration.chain, &configuration.selectedNr, &configuration.selectedColour) && migrated)
            *migrated = true;
    }
    if (hasEditor) {
        if (!parsed.initialized[1]) return false;
        auto editor = std::make_shared<NodeEditorDocument>();
        int hasGlobals=version>=3?1:0;
        if(version>=4&&(!(in>>hasGlobals)||hasGlobals<0||hasGlobals>1))return false;
        if (hasGlobals) {
            ChainGlobalSettings g;
            int target, backend, flow, optical, half, policy;
            if (!(in >> target >> g.videoSrQuality >> backend >> flow >> optical >> half >> policy) ||
                half < 0 || half > 1) return false;
            g.srTarget = pipeline::SrTarget(target); g.fgBackend = FrameGenerationBackend(backend);
            g.flow = FlowQuality(flow); g.opticalFlowBackend = OpticalFlowBackend(optical);
            g.amdFlowHalfResolution = half != 0; g.nrPolicy = pipeline::NrSizePolicy(policy);
            if(version>=4&&!readRendering(in,g,version>=5,version>=6))return false;
            editor->globals = g;
        }
        uint32_t count = 0;
        if (!(in >> editor->layout.nextId >> editor->layout.inputNext >> count) || count > kMaxChainNodes) return false;
        std::array<bool, kMaxChainNodes> enabled{};
        for (uint32_t i = 0; i < count; ++i) {
            int flag = 0;
            if (!(in >> editor->layout.ids[i] >> editor->layout.next[i] >> flag) || flag < 0 || flag > 1) return false;
            enabled[i] = flag != 0;
        }
        if (!(in >> std::quoted(body))) return false;
        entries.clear(); def.clear(); error.clear();
        // The editor payload is written by the same encoder as the runtime one,
        // so it must be read with the same version cap. Leaving it at the
        // legacy default (3) rejected every document whose payload needed a
        // newer version, and save() then refused to write anything at all.
        if (!PresetLibrary::parse(body, entries, def, error, nullptr, true, 9) || entries.size() != 1 ||
            !def.empty() || entries[0].kind != ChainMode::Node || entries[0].chain.nodeCount != count) return false;
        editor->nodes = std::move(entries[0].chain);
        for (uint32_t i = 0; i < count; ++i) {
            if (editor->nodes.nodes[i].enabled) return false;
            editor->nodes.nodes[i].enabled = enabled[i];
        }
        if (!editor->layout.validate(editor->nodes).accepted) return false;
        parsed.configurations[1].editor = std::move(editor);
    }
    in >> std::ws; if (!in.eof()) return false;
    if (!parsed.valid()) return false;
    session = std::move(parsed);
    return true;
}

bool ChainSessionStore::load(ChainSession& session) {
    error_.clear();
    std::error_code ec;
    const auto rootRendering=std::filesystem::path(path_).concat(L".field-render");
    if(std::filesystem::exists(rootRendering,ec))path_=rootRendering;
    const auto migratedPath = std::filesystem::path(path_).concat(L".p3-node");
    if (std::filesystem::exists(migratedPath, ec)) path_ = migratedPath;
    const auto editorPath = std::filesystem::path(path_).concat(L".p3-editor");
    if (std::filesystem::exists(editorPath, ec) ||
        std::filesystem::exists(std::filesystem::path(editorPath).concat(L".p3-globals"), ec)) path_ = editorPath;
    const auto globalsPath = std::filesystem::path(path_).concat(L".p3-globals");
    if (std::filesystem::exists(globalsPath, ec)) path_ = globalsPath;
    const auto renderingPath=std::filesystem::path(path_).concat(L".field-render");
    if(std::filesystem::exists(renderingPath,ec))path_=renderingPath;
    const bool exists = std::filesystem::exists(path_, ec);
    if (!exists && !ec) return true;
    const auto size = std::filesystem::file_size(path_, ec);
    if (!ec && size <= kMaxPresetBytes) {
        std::ifstream file(path_, std::ios::binary);
        if (file) {
            const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            bool migrated = false;
            if (!file.bad() && decode(data, session, &migrated)) {
                if (migrated) {
                    path_ = std::filesystem::path(path_).concat(L".p3-node");
                    error_ = L"旧节点配置的 NR 保护区域已移除；列表保护及其他节点参数保留，后续保存到 .p3-node 文件，原文件不覆盖";
                }
                return true;
            }
        }
    }
    corrupt_ = true;
    error_ = L"模式会话损坏、无法读取或版本不支持；原文件保留，禁止覆盖";
    return false;
}

bool ChainSessionStore::save(const ChainSession& session) {
    error_.clear();
    if (corrupt_) { error_ = L"模式会话损坏，已拒绝覆盖原文件"; return false; }
    if (!session.valid()) { error_ = L"模式会话包含无效链或参数，未保存"; return false; }
    const auto data = encode(session);
    ChainSession checked;
    if (!decode(data, checked)) { error_ = L"模式会话编码校验失败，未保存"; return false; }
    if(data.starts_with("VEYRA_CHAIN_SESSION 4")&&!path_.wstring().ends_with(L".field-render"))path_=std::filesystem::path(path_).concat(L".field-render");
    // A v1 reader must never reinterpret or overwrite the new editor graph.
    // Preserve the original session as a rollback point during this opt-in.
    if (session.configurations[1].editor && !path_.wstring().ends_with(L".field-render")&&!path_.wstring().ends_with(L".p3-editor") &&
        !path_.wstring().ends_with(L".field-render")&&!path_.wstring().ends_with(L".p3-globals"))
        path_ = std::filesystem::path(path_).concat(L".p3-editor");
    if (session.configurations[1].editor && session.configurations[1].editor->globals &&
        !path_.wstring().ends_with(L".field-render")&&!path_.wstring().ends_with(L".p3-globals"))
        path_ = std::filesystem::path(path_).concat(L".p3-globals");
    std::error_code ec;
    if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) { error_ = L"模式会话目录无法创建"; return false; }
    const auto temporary = std::filesystem::path(path_).concat(L".tmp");
    bool written = false;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (file) { file << data; file.flush(); written = bool(file); file.close(); written = written && !file.fail(); }
    }
    if (!written || !MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
        error_ = L"模式会话写入或替换失败，原存档保留"; return false;
    }
    return true;
}
}
