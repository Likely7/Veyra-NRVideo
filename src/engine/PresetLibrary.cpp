#include "veyra/engine/PresetLibrary.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace veyra::engine {
namespace {
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
    if (!std::filesystem::exists(path_)) return true;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path_, ec);
    if (ec || size > 262144) { corrupt_ = true; error_ = L"预设库文件过大，原文件保留"; return false; }
    std::ifstream f(path_, std::ios::binary);
    if (!f) { corrupt_ = true; error_ = L"预设库无法读取，原文件保留"; return false; }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<PresetEntry> loaded;
    std::wstring def, parseError;
    if (!parse(data, loaded, def, parseError)) {
        corrupt_ = true;
        error_ = parseError.empty() ? L"预设库损坏或版本不支持；原文件已保留，禁止覆盖" : parseError;
        return false;
    }
    // The file's own presets replace the built-ins; built-ins are re-added so a
    // user who deleted nothing always sees them.
    entries_ = std::move(loaded);
    defaultName_ = def;
    addBuiltins();
    for (auto& entry : entries_) entry.builtin = entry.builtin || (entry.name == L"原画" || entry.name == L"流畅" || entry.name == L"均衡" || entry.name == L"极致");
    return true;
}

bool PresetLibrary::save() {
    if (corrupt_) { error_ = L"预设库损坏，已拒绝覆盖原文件"; return false; }
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    const auto temporary = std::filesystem::path(path_).concat(L".tmp");
    { std::ofstream f(temporary, std::ios::binary | std::ios::trunc); if (!f) { error_ = L"预设库写入失败"; return false; } f << serialize(); if (!f) { error_ = L"预设库写入失败"; return false; } }
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
        *found = entry;
        return save();
    }
    if (entries_.size() >= 64) { error_ = L"预设数量已达上限"; return false; }
    entries_.push_back(entry);
    return save();
}

bool PresetLibrary::rename(size_t index, std::wstring name) {
    if (index >= entries_.size() || entries_[index].builtin) { error_ = L"内置预设不能改名"; return false; }
    if (!nameOk(name)) { error_ = L"预设名称非法"; return false; }
    if (std::any_of(entries_.begin(), entries_.end(), [&](const PresetEntry& e) { return e.name == name; })) { error_ = L"同名预设已存在"; return false; }
    if (defaultName_ == entries_[index].name) defaultName_ = name;
    entries_[index].name = std::move(name);
    return save();
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
    return save();
}

bool PresetLibrary::erase(size_t index) {
    if (index >= entries_.size()) return false;
    if (entries_[index].builtin) { error_ = L"内置预设不能删除；可以复制后修改"; return false; }
    if (defaultName_ == entries_[index].name) defaultName_.clear();
    entries_.erase(entries_.begin() + std::ptrdiff_t(index));
    return save();
}

bool PresetLibrary::setDefault(size_t index) {
    if (index >= entries_.size()) return false;
    defaultName_ = entries_[index].name;
    return save();
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
    const auto keepMultiplier = settings.multiplier;
    const auto keepBackend = settings.frameGenerationBackend;
    const auto keepAudioSync = settings.audioSync;
    const auto keepAudioOffset = settings.audioOffsetMs;
    if (entry.contents & presetContentMask(PresetContent::Chain)) fromChain(entry.chain, settings);
    if (entry.contents & presetContentMask(PresetContent::Color)) settings.color = entry.color;
    else settings.color = keepColor;
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
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(std::numeric_limits<float>::max_digits10);
    o << "VEYRA_PRESET_LIBRARY 1\n" << std::quoted(utf8(defaultName_)) << ' ' << entries_.size() << '\n';
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
            for (const auto& r : n.protection.regions) o << ' ' << r.left << ' ' << r.top << ' ' << r.right << ' ' << r.bottom;
            o << ' ' << (n.videoHdr.enabled ? 1 : 0) << ' ' << n.videoHdr.contrast << ' ' << n.videoHdr.saturation << ' '
              << n.videoHdr.middleGray << ' ' << n.videoHdr.peakNits << ' ';
        }
        o << ' ';
        writeColorSettings(o, e.color, utf8(e.color.lutNameString()));
        o << '\n';
    }
    return o.str();
}

bool PresetLibrary::parse(const std::string& data, std::vector<PresetEntry>& out, std::wstring& def, std::wstring& error) {
    if (data.size() > 262144) { error = L"预设库超过 256 KiB"; return false; }
    std::istringstream in(data);
    in.imbue(std::locale::classic());
    std::string magic, quoted;
    int version = 0;
    size_t count = 0;
    if (!(in >> magic >> version) || magic != "VEYRA_PRESET_LIBRARY" || version < 1 || version > 1) { error = L"预设库格式或版本不支持"; return false; }
    if (!(in >> std::quoted(quoted) >> count) || count > 64) { error = L"预设库条目数非法"; return false; }
    def = wide(quoted);
    for (size_t i = 0; i < count; ++i) {
        PresetEntry e;
        std::string name, note;
        int kind = 0, builtin = 0, fgBackend = 0, audioSync = 0, chainMode = 0, strict = 0;
        uint32_t nodeCount = 0;
        if (!(in >> std::quoted(name) >> std::quoted(note) >> kind >> builtin >> e.contents >> e.fg.multiplier >> fgBackend >> audioSync >> e.audioOffsetMs >> nodeCount >> chainMode >> e.chain.fgMultiplier >> strict)) { error = L"预设库头部字段损坏"; return false; }
        if (kind < 0 || kind > 1 || nodeCount > kMaxChainNodes || e.fg.multiplier < 1 || e.fg.multiplier > 6) { error = L"预设库字段超出范围"; return false; }
        e.name = wide(name);
        e.note = wide(note);
        e.kind = static_cast<ChainMode>(kind);
        e.builtin = builtin != 0;
        e.fg.backend = static_cast<FrameGenerationBackend>(fgBackend);
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
            for (auto& r : node.protection.regions) if (!(in >> r.left >> r.top >> r.right >> r.bottom)) { error = L"预设库保护区域损坏"; return false; }
            if (!(in >> hdrEnabled >> node.videoHdr.contrast >> node.videoHdr.saturation >> node.videoHdr.middleGray >> node.videoHdr.peakNits)) { error = L"预设库 HDR 字段损坏"; return false; }
            if (type < 0 || type >= int(EffectType::Count) || enabled < 0 || enabled > 1 || runtime < 0 || runtime > 2 ||
                temporal < 0 || temporal > 1 || lowLatency < 0 || lowLatency > 1 || protEnabled < 0 || protEnabled > 1 || hdrEnabled < 0 || hdrEnabled > 1) {
                error = L"预设库节点取值超出范围"; return false;
            }
            node.type = static_cast<EffectType>(type);
            node.enabled = enabled != 0;
            node.nr.runtime = static_cast<NrRuntime>(runtime);
            node.nr.temporal = temporal != 0;
            node.nr.lowLatencyPairing = lowLatency != 0;
            node.protection.enabled = protEnabled != 0;
            node.videoHdr.enabled = hdrEnabled != 0;
        }
        in >> std::ws;
        std::string lut;
        // 19 is the current colour-block schema: it includes the per-section
        // bypass mask that the writer always emits.
        if (!readColorSettings(in, e.color, lut, 19)) { error = L"预设库调色字段损坏"; return false; }
        if (!lut.empty() && !e.color.setLutName(wide(lut))) { error = L"预设库 LUT 名称非法"; return false; }
        if (auto problem = e.color.validate(); !problem.empty()) { error = L"预设库调色参数超出范围"; return false; }
        if (auto verdict = validateChain(e.chain); !verdict.accepted) { error = L"预设库链路违反顺序规则"; return false; }
        out.push_back(std::move(e));
    }
    in >> std::ws;
    if (!in.eof()) { error = L"预设库尾部有多余数据"; return false; }
    return true;
}
}
