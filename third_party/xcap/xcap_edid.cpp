#include "xcap_edid.h"
#include "xcap.h"
#include <setupapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

namespace xcap {

// ------------------------------------------------------------- monitors ----

static const GUID kMonitor = {0xe6f07b5f, 0xee97, 0x4a90, {0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7}};

static std::vector<uint8_t> interfaceEdid(HDEVINFO set, SP_DEVICE_INTERFACE_DATA& itf) {
    std::vector<uint8_t> out;
    DWORD need = 0;
    SP_DEVINFO_DATA dev{sizeof(dev)};
    SetupDiGetDeviceInterfaceDetailW(set, &itf, nullptr, 0, &need, &dev);          // fills dev although there is no buffer
    HKEY key = SetupDiOpenDevRegKey(set, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) return out;
    DWORD size = 0;
    if (RegQueryValueExW(key, L"EDID", nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS && size >= 128) {
        out.resize(size);
        if (RegQueryValueExW(key, L"EDID", nullptr, nullptr, out.data(), &size) != ERROR_SUCCESS) out.clear();
    }
    RegCloseKey(key);
    return out;
}

static std::vector<uint8_t> registryEdid(const std::wstring& devicePath) {
    std::vector<uint8_t> out;
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA itf{sizeof(itf)};
    if (SetupDiOpenDeviceInterfaceW(set, devicePath.c_str(), 0, &itf)) out = interfaceEdid(set, itf);
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

// A process without a desktop (a service, ssh) gets no display paths: the connected monitors are still listed here.
static std::vector<TvEdid> presentMonitors() {
    std::vector<TvEdid> out;
    HDEVINFO set = SetupDiGetClassDevsW(&kMonitor, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA itf{sizeof(itf)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kMonitor, i, &itf); ++i) {
        TvEdid m;
        m.bytes = interfaceEdid(set, itf);
        if (m.bytes.size() < 128) continue;
        const auto n = parseTv(m.bytes).name;
        m.name.assign(n.begin(), n.end());
        out.push_back(std::move(m));
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::vector<TvEdid> activeMonitors() {
    std::vector<TvEdid> out;
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS || !np) return presentMonitors();
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return presentMonitors();
    for (UINT32 i = 0; i < np; ++i) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME t{};
        t.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(t), paths[i].targetInfo.adapterId, paths[i].targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&t.header) != ERROR_SUCCESS) continue;
        TvEdid m;
        m.name = t.monitorFriendlyDeviceName;
        m.path = t.monitorDevicePath;
        m.bytes = registryEdid(t.monitorDevicePath);
        DISPLAYCONFIG_SOURCE_DEVICE_NAME g{};
        g.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(g), paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&g.header) == ERROR_SUCCESS) m.gdi = g.viewGdiDeviceName;
        const UINT32 si = paths[i].sourceInfo.modeInfoIdx;
        if (si < nm && modes[si].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) m.primary = modes[si].sourceMode.position.x == 0 && modes[si].sourceMode.position.y == 0;
        if (m.bytes.size() >= 128) out.push_back(std::move(m));
    }
    std::stable_sort(out.begin(), out.end(), [](const TvEdid& a, const TvEdid& b) { return a.primary > b.primary; });
    return out;
}

// ---------------------------------------------------------------- parse ----

TvInfo parseTv(const std::vector<uint8_t>& e) {
    TvInfo t;
    if (e.size() < 128) return t;
    for (int d = 0; d < 4; ++d) {
        const uint8_t* p = &e[54 + 18 * d];
        if (!p[0] && !p[1] && p[3] == 0xFC) for (int k = 5; k < 18 && p[k] != 0x0A; ++k) t.name.push_back(char(p[k]));
    }
    const auto audio = [&](uint8_t a, uint8_t b, uint8_t c) {
        const unsigned codec = (a >> 3) & 15, ch = (a & 7) + 1;
        if (codec == 1) t.pcmChannels = std::max(t.pcmChannels, ch);
        if (codec == 10 && (c & 1)) t.atmosEac3 = true;
        if (codec == 12 && (c & 1)) t.atmosMat = true;
        if (codec && codec != 15) t.sads.push_back({a, b, c});
    };
    for (size_t base = 128; base + 128 <= e.size(); base += 128) {
        const uint8_t* x = &e[base];
        const auto blocks = [&](const uint8_t* p, const uint8_t* end) {
            while (p < end) {
                const unsigned tag = p[0] >> 5, n = p[0] & 31;
                if (p + 1 + n > end) break;
                const uint8_t* d = p + 1;
                if (tag == 1) for (unsigned i = 0; i + 2 < n; i += 3) audio(d[i], d[i + 1], d[i + 2]);
                else if (tag == 4 && n >= 3) t.speakers.assign(d, d + 3);
                else if (tag == 7 && n >= 2 && d[0] == 5) t.bt2020 = (d[1] & 0xC0) != 0;
                else if (tag == 7 && n >= 3 && d[0] == 6) {
                    t.hdr = (d[1] & 4) != 0;
                    if (n > 3) t.maxLum = d[3];
                    if (n > 4) t.avgLum = d[4];
                    if (n > 5) t.minLum = d[5];
                }
                p += 1 + n;
            }
        };
        if (x[0] == 0x02 && x[2] >= 4 && x[2] <= 127) blocks(x + 4, x + x[2]);
        else if (x[0] == 0x70) {                                     // DisplayID: CTA blocks travel inside block 0x81
            const uint8_t* p = x + 5;
            const uint8_t* end = x + 5 + std::min<unsigned>(x[2], 121);
            while (p + 3 <= end && p[2] && p + 3 + p[2] <= end) {
                if (p[0] == 0x81) blocks(p + 3, p + 3 + p[2]);
                p += 3 + p[2];
            }
        }
    }
    return t;
}

const TvEdid* chooseTv(const std::vector<TvEdid>& monitors, const std::wstring& name) {
    if (!name.empty()) for (const auto& m : monitors) if (m.name.find(name) != std::wstring::npos) return &m;
    for (const auto& m : monitors) { const auto t = parseTv(m.bytes); if (t.atmosMat || t.atmosEac3) return &m; }
    return monitors.empty() ? nullptr : &monitors.front();
}

// -------------------------------------------------------------- compose ----

static void dtd(uint8_t* o, unsigned khz, unsigned ha, unsigned hb, unsigned hso, unsigned hsw, unsigned va, unsigned vb, unsigned vso, unsigned vsw) {
    const unsigned c = khz / 10, w = 1600, h = 900;
    const unsigned v[18] = {c & 255, c >> 8, ha & 255, hb & 255, (ha >> 8) << 4 | hb >> 8, va & 255, vb & 255, (va >> 8) << 4 | vb >> 8,
        hso & 255, hsw & 255, (vso & 15) << 4 | (vsw & 15), (hso >> 8) << 6 | (hsw >> 8) << 4 | (vso >> 4) << 2 | vsw >> 4,
        w & 255, h & 255, (w >> 8) << 4 | h >> 8, 0, 0, 0x1E};
    for (int i = 0; i < 18; ++i) o[i] = uint8_t(v[i]);
}
static void seal(uint8_t* b) {
    unsigned s = 0;
    for (int i = 0; i < 127; ++i) s += b[i];
    b[127] = uint8_t(0u - s);
}

std::array<uint8_t, 256> composeEdid(const TvInfo& tv, const AudioCaps& pc) {
    std::array<uint8_t, 256> e{};
    uint8_t* b = e.data();
    const uint8_t head[] = {0, 255, 255, 255, 255, 255, 255, 0, 0x60, 0x70, 0x5C, 0xA1, 2, 0, 0, 0, 47, 36, 1, 3, 0x80, 160, 90, 120, 0x0A};
    std::memcpy(b, head, sizeof(head));                              // "XCP", digital, gamma 2.2
    const double xy[8] = {.708, .292, .170, .797, .131, .046, .3127, .3290};
    unsigned q[8];
    for (int i = 0; i < 8; ++i) { q[i] = unsigned(std::lround(xy[i] * 1024)); b[27 + i] = uint8_t(q[i] >> 2); }
    b[25] = uint8_t((q[0] & 3) << 6 | (q[1] & 3) << 4 | (q[2] & 3) << 2 | (q[3] & 3));
    b[26] = uint8_t((q[4] & 3) << 6 | (q[5] & 3) << 4 | (q[6] & 3) << 2 | (q[7] & 3));
    b[35] = 0x21; b[36] = 0x08;
    for (int i = 38; i < 54; ++i) b[i] = 1;
    dtd(b + 54, 594000, 3840, 560, 176, 88, 2160, 90, 8, 10);
    dtd(b + 72, 148500, 1920, 280, 88, 44, 1080, 45, 4, 5);
    const uint8_t name[18] = {0, 0, 0, 0xFC, 0}, range[18] = {0, 0, 0, 0xFD, 0, 23, 120, 15, 160, 60, 0, 10, 32, 32, 32, 32, 32, 32};
    std::memcpy(b + 90, name, 18);
    std::string n = "XCAP " + (tv.name.empty() ? std::string("AXAU15") : tv.name);
    n.resize(std::min<size_t>(n.size(), 13));
    for (size_t i = 0; i < 13; ++i) b[95 + i] = i < n.size() ? uint8_t(n[i]) : i == n.size() ? 0x0A : 0x20;
    std::memcpy(b + 108, range, 18);
    b[126] = 1;
    seal(b);

    uint8_t* x = b + 128;
    unsigned at = 4;
    const auto put = [&](unsigned tag, const std::vector<uint8_t>& d) {
        if (at + 1 + d.size() > 109) return;                         // keep room for the detailed timing
        x[at++] = uint8_t(tag << 5 | d.size());
        for (uint8_t v : d) x[at++] = v;
    };
    put(2, {97, 96, 95, 93, 63, 16, 31, 32, 4});
    // ---- audio ----
    std::vector<uint8_t> a{0x09, 0x07, 0x07};                        // stereo 32/44.1/48 kHz 16/20/24 bit, always
    if (pc.pcm8 && tv.pcmChannels >= 6) { a.push_back(uint8_t(0x08 | (tv.pcmChannels - 1))); a.push_back(0x07); a.push_back(0x07); }
    for (const auto& s : tv.sads) {
        const unsigned codec = (s[0] >> 3) & 15;
        const bool ok = codec == 2 ? pc.ac3 : codec == 10 ? pc.eac3 : codec == 12 ? pc.mat : codec == 7 ? pc.dts : codec == 11 ? pc.dtsHd : false;
        if (ok && a.size() + 3 <= 27) a.insert(a.end(), s.begin(), s.end());
    }
    put(1, a);
    put(4, pc.pcm8 && tv.pcmChannels >= 6 && tv.speakers.size() >= 3 ? tv.speakers : std::vector<uint8_t>{uint8_t(pc.pcm8 && tv.pcmChannels >= 6 ? 0x4F : 0x01), 0, 0});
    // ---- HDMI 2.0, colour, HDR10 ----
    put(3, {0x03, 0x0C, 0x00, 0x10, 0x00, 0xB8, 60});
    put(3, {0xD8, 0x5D, 0xC4, 1, 120, 0x80, 0x00});
    put(7, {0, 0xCA});
    put(7, {5, 0xC0, 0x00});
    // the luminance is the TV's; many TVs state none, and then the source must see none either
    if (tv.maxLum) put(7, {6, 0x05, 0x01, uint8_t(tv.maxLum), uint8_t(tv.avgLum), uint8_t(tv.minLum)});
    else put(7, {6, 0x05, 0x01});
    x[0] = 2; x[1] = 3; x[2] = uint8_t(at); x[3] = 0xF1;
    dtd(x + at, 148500, 1920, 280, 88, 44, 1080, 45, 4, 5);
    seal(x);
    return e;
}

uint8_t edidNumber(const std::array<uint8_t, 256>& edid) {
    uint32_t h = 2166136261u;
    for (uint8_t v : edid) h = (h ^ v) * 16777619u;
    return uint8_t((h ^ h >> 8 ^ h >> 16 ^ h >> 24) % 255u + 1u);
}

// A second, independent hash of the contents: 1..65534 (0 = none stored, 65535 = contents unknown).
static uint32_t edidCheck(const std::array<uint8_t, 256>& edid) {
    uint32_t h = 0x9E3779B9u;
    for (uint8_t v : edid) { h ^= v; h *= 0x85EBCA6Bu; h ^= h >> 13; }
    return (h ^ h >> 16) % 65534u + 1u;
}

bool applyEdid(Device& device, const std::array<uint8_t, 256>& edid, bool* changed) {
    uint32_t hw = 0, state = 0, host = 0;
    if (changed) *changed = false;
    if (!device.read(XCAP_REG_VERSION, hw) || hw < 0x00030100u) return false;
    const uint32_t number = edidNumber(edid), other = number % 255u + 1u, check = edidCheck(edid);
    // the number of the host EDID the firmware shows right now, 0: one of the built-in ones
    const auto shown = [&]() -> uint32_t { return device.read(XCAP_REG_HDMI0 + 4 * XCAP_META_STATE, state) && (state & 0x20u) ? state >> 24 : 0u; };
    if (!device.read(XCAP_REG_HOST_CTRL, host)) return false;
    const uint32_t asked = (host & 0x20u) ? (host >> 8) & 255u : 0u, stored = host >> 16;
    // A change somebody else asked for a moment ago is still on its way (the firmware takes 0.6 s): let it
    // arrive, so that the words are not rewritten while the firmware copies them.
    uint32_t now = shown();
    for (int i = 0; i < 50 && asked && now != asked && !(host & 1u); ++i) { Sleep(20); now = shown(); }
    // The same contents, not just the same 8-bit number (two different EDIDs share one in 255 cases).
    if (now && now == asked && (stored ? stored == check && (now == number || now == other) : now == number)) return true;
    // The firmware copies the 64 words when the number changes, checks them and shows the copy: nothing
    // half-written reaches the source. Write, then commit.
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t v = edid[4 * i] | uint32_t(edid[4 * i + 1]) << 8 | uint32_t(edid[4 * i + 2]) << 16 | uint32_t(edid[4 * i + 3]) << 24;
        if (!device.write(XCAP_REG_EDID + 4 * i, v)) return false;
    }
    // Commit. A different EDID under the same number is shown already: the firmware only reloads on another number.
    const uint32_t use = now == number ? other : number;
    if (!device.write(XCAP_REG_HOST_CTRL, (host & 1u) | 0x20u | use << 8 | check << 16)) return false;
    if (changed) *changed = true;
    for (int i = 0; i < 150; ++i) { if (shown() == use) return true; Sleep(20); }
    device.write(XCAP_REG_HOST_CTRL, host);
    return false;
}

} // namespace xcap
