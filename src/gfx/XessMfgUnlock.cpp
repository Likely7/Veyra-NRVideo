#include "veyra/gfx/XessMfgUnlock.h"

#include <format>
#include <cstring>
#include <iterator>
#include <mutex>
#include <vector>

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"

namespace veyra::gfx {

namespace {

// Byte tables ported from OptiScaler XeFGUnlock.h (GPL-3.0, commit 70676c5f).
// U1 removes the "XeLL too old for multi-frame" fallback to 2X, U2 keeps the
// multi-frame model from being downgraded, U3 raises the default ceiling, U4
// stops the provider from pinning the override to a single frame and U5 makes
// xefgSwapChainGetProperties report the real ceiling instead of 1.
struct PatchSpec {
    const char* name;
    uint32_t rva;
    const uint8_t* expected;
    uint32_t size;
    const uint8_t* replacement;
    int32_t immediateOffset; // -1: fixed replacement, otherwise LE imm32 slot
    bool enabled;
};

const uint8_t kU1Old[] = {0x0F, 0x85, 0xCC, 0x00, 0x00, 0x00};
const uint8_t kU1New[] = {0xE9, 0xCD, 0x00, 0x00, 0x00, 0x90};
const uint8_t kU2Old[] = {0x74, 0x09};
const uint8_t kU2New[] = {0xEB, 0x06};
const uint8_t kU3Old[] = {0xBB, 0x03, 0x00, 0x00, 0x00};
const uint8_t kU3New[] = {0xBB, 0x00, 0x00, 0x00, 0x00};
const uint8_t kU4Old[] = {0xC7, 0x87, 0x6C, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
const uint8_t kU4New[] = {0xC7, 0x87, 0x6C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kU5Old[] = {0xB8, 0x01, 0x00, 0x00, 0x00};
const uint8_t kU5New[] = {0xB8, 0x00, 0x00, 0x00, 0x00};

const PatchSpec kPatches[] = {
    {"U1/frame-count-fallback", 0x20DA4F, kU1Old, sizeof(kU1Old), kU1New, -1, true},
    {"U2/model-downgrade",      0x1A5DE4, kU2Old, sizeof(kU2Old), kU2New, -1, true},
    {"U3/default-ceiling",      0x1A517D, kU3Old, sizeof(kU3Old), kU3New,  1, true},
    {"U4/override-clamp",       0x1A45C2, kU4Old, sizeof(kU4Old), kU4New,  6, true},
    {"U5/reported-maximum",     0x20973B, kU5Old, sizeof(kU5Old), kU5New,  1, true},
};

struct AppliedPatch {
    uint8_t* address = nullptr;
    uint8_t original[16]{};
    uint32_t size = 0;
    const char* name = "";
};

std::mutex g_mutex;
XessMfgUnlock::State g_state;
std::vector<AppliedPatch> g_applied;

const IMAGE_DOS_HEADER* dosHeader(uint8_t* base) {
    if (base == nullptr) return nullptr;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return nullptr;
    return dos;
}

const IMAGE_NT_HEADERS* ntHeaders(uint8_t* base) {
    const auto* dos = dosHeader(base);
    if (dos == nullptr) return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

const IMAGE_SECTION_HEADER* findSection(uint8_t* base, const char* name) {
    const auto* nt = ntHeaders(base);
    if (nt == nullptr) return nullptr;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char sectionName[9]{};
        memcpy(sectionName, section->Name, 8);
        if (strcmp(sectionName, name) == 0) return section;
    }
    return nullptr;
}

bool insideSection(const IMAGE_SECTION_HEADER* section, uint32_t rva, uint32_t size) {
    if (section == nullptr) return false;
    const uint32_t start = section->VirtualAddress;
    const uint32_t end = start + section->Misc.VirtualSize;
    return rva >= start && rva + size <= end;
}

std::string toHex(const uint8_t* data, uint32_t size) {
    std::string text;
    text.reserve(size * 3);
    for (uint32_t i = 0; i < size; ++i) {
        if (i) text.push_back(' ');
        text += std::format("{:02X}", data[i]);
    }
    return text;
}

bool writeVerified(uint8_t* address, const uint8_t* bytes, uint32_t size) {
    DWORD previous = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    memcpy(address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    VirtualProtect(address, size, previous, &previous);
    return memcmp(address, bytes, size) == 0;
}

} // namespace

XessMfgUnlock::State XessMfgUnlock::apply(HMODULE provider, uint32_t generatedFrames) {
    std::lock_guard lock(g_mutex);
    State state = g_state;
    state.providerLoaded = provider != nullptr;
    if (generatedFrames < 1) generatedFrames = 1;
    if (generatedFrames > kMaxGeneratedFrames) generatedFrames = kMaxGeneratedFrames;

    if (provider == nullptr) {
        state.detail = L"XeSS frame-generation provider is not loaded";
        g_state = state;
        return state;
    }

    // Stock 2X: nothing to patch, and a previously applied unlock must not leak
    // into a 2X session.
    if (generatedFrames <= 1) {
        state.detail = L"2X requested; provider left untouched";
        state.generatedFrames = 1;
        g_state = state;
        return state;
    }

    if (state.applied && g_state.generatedFrames == generatedFrames) return state;

    // Provider identity: absolute path, size and SHA-256 must match the audited
    // build. A different provider version simply keeps stock 2X behaviour.
    wchar_t modulePath[MAX_PATH * 2]{};
    if (GetModuleFileNameW(provider, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0) {
        state.detail = L"provider path unavailable";
        g_state = state;
        return state;
    }
    FileIdentity identity{};
    IdentityError identityError = IdentityError::None;
    if (!computeFileIdentity(modulePath, identity, identityError)) {
        state.detail = std::format(L"provider identity failed (code {})", static_cast<int>(identityError));
        g_state = state;
        return state;
    }
    state.identityVerified = identity.sizeBytes == kKnownProviderSize && identity.sha256Upper == kKnownProviderSha256;

    auto* base = reinterpret_cast<uint8_t*>(provider);
    const auto* nt = ntHeaders(base);
    const auto* text = findSection(base, ".text");
    state.recognisedBuild = nt != nullptr && text != nullptr &&
                            nt->FileHeader.TimeDateStamp == kKnownTimeDateStamp &&
                            nt->OptionalHeader.SizeOfImage == kKnownSizeOfImage;

    if (!state.identityVerified) {
        state.detail = std::format(L"unaudited provider (size={} sha256={})", identity.sizeBytes,
                                   std::wstring(identity.sha256Upper.begin(), identity.sha256Upper.end()));
        veyra::log::warn("xess-mfg", std::format("unlock refused: unaudited provider size={} sha256={} recognisedBuild={}",
                                                 identity.sizeBytes, identity.sha256Upper, state.recognisedBuild));
        g_state = state;
        return state;
    }
    if (!state.recognisedBuild) {
        state.detail = L"provider PE identity does not match the audited build";
        veyra::log::warn("xess-mfg", "unlock refused: PE timestamp/image size mismatch");
        g_state = state;
        return state;
    }

    // Build the replacement bytes for every enabled patch first (transactional:
    // all positions are validated before the first write).
    struct Planned {
        const PatchSpec* spec;
        uint8_t bytes[16]{};
    };
    std::vector<Planned> planned;
    planned.reserve(std::size(kPatches));
    for (const auto& patch : kPatches) {
        if (!patch.enabled || patch.size > sizeof(Planned::bytes)) continue;
        if (!insideSection(text, patch.rva, patch.size)) {
            state.detail = std::format(L"{} outside .text", std::wstring(patch.name, patch.name + strlen(patch.name)));
            g_state = state;
            return state;
        }
        uint8_t* destination = base + patch.rva;
        if (memcmp(destination, patch.expected, patch.size) != 0) {
            const auto actualBytes = toHex(destination, patch.size);
            state.detail = std::format(L"{} has unexpected bytes ({})", std::wstring(patch.name, patch.name + strlen(patch.name)),
                                       std::wstring(actualBytes.begin(), actualBytes.end()));
            veyra::log::error("xess-mfg", std::format("unlock aborted: {} at 0x{:X} expected [{}] found [{}]", patch.name, patch.rva,
                                                      toHex(patch.expected, patch.size), toHex(destination, patch.size)));
            g_state = state;
            return state;
        }
        Planned plan{};
        plan.spec = &patch;
        memcpy(plan.bytes, patch.replacement, patch.size);
        if (patch.immediateOffset >= 0) {
            plan.bytes[patch.immediateOffset + 0] = static_cast<uint8_t>(generatedFrames & 0xFF);
            plan.bytes[patch.immediateOffset + 1] = static_cast<uint8_t>((generatedFrames >> 8) & 0xFF);
            plan.bytes[patch.immediateOffset + 2] = static_cast<uint8_t>((generatedFrames >> 16) & 0xFF);
            plan.bytes[patch.immediateOffset + 3] = static_cast<uint8_t>((generatedFrames >> 24) & 0xFF);
        }
        planned.push_back(plan);
    }

    size_t installed = 0;
    for (const auto& plan : planned) {
        auto* destination = reinterpret_cast<uint8_t*>(provider) + plan.spec->rva;
        if (!writeVerified(destination, plan.bytes, plan.spec->size)) {
            state.detail = std::format(L"{} write verification failed", std::wstring(plan.spec->name, plan.spec->name + strlen(plan.spec->name)));
            veyra::log::error("xess-mfg", std::format("unlock aborted at {} (0x{:X}); rolling back {} patch(es)", plan.spec->name, plan.spec->rva, installed));
            for (auto it = g_applied.rbegin(); it != g_applied.rend(); ++it) {
                writeVerified(it->address, it->original, it->size);
            }
            g_applied.clear();
            state.applied = false;
            g_state = state;
            return state;
        }
        AppliedPatch record{};
        record.address = destination;
        memcpy(record.original, plan.spec->expected, plan.spec->size);
        record.size = plan.spec->size;
        record.name = plan.spec->name;
        g_applied.push_back(record);
        ++installed;
        veyra::log::info("xess-mfg", std::format("unlock patch {} applied at 0x{:X} -> [{}]", plan.spec->name, plan.spec->rva,
                                                 toHex(plan.bytes, plan.spec->size)));
    }

    state.applied = true;
    state.generatedFrames = generatedFrames;
    state.detail = std::format(L"unlock applied: {} patches, ceiling {}X", installed, generatedFrames + 1);
    veyra::log::info("xess-mfg", std::format("unlock applied: {}/{} patches, maxInterpolatedFrames={} => {}X",
                                             installed, std::size(kPatches), generatedFrames, generatedFrames + 1));
    g_state = state;
    return state;
}

void XessMfgUnlock::reportRuntimeCeiling(uint32_t maxInterpolations) {
    std::lock_guard lock(g_mutex);
    g_state.maxInterpolations = maxInterpolations == 0 ? 1 : maxInterpolations;
    veyra::log::info("xess-mfg", std::format("runtime reports maxInterpolations={} => {}X", g_state.maxInterpolations,
                                             g_state.maxInterpolations + 1));
}

void XessMfgUnlock::release() {
    std::lock_guard lock(g_mutex);
    if (!g_applied.empty()) {
        size_t restored = 0;
        for (auto it = g_applied.rbegin(); it != g_applied.rend(); ++it) {
            if (writeVerified(it->address, it->original, it->size)) ++restored;
            else veyra::log::error("xess-mfg", std::format("rollback failed for {} at {:p}", it->name, static_cast<void*>(it->address)));
        }
        veyra::log::info("xess-mfg", std::format("unlock rolled back {}/{} patch(es)", restored, g_applied.size()));
        g_applied.clear();
    }
    g_state.applied = false;
    g_state.detail = L"released";
}

XessMfgUnlock::State XessMfgUnlock::snapshot() {
    std::lock_guard lock(g_mutex);
    return g_state;
}

bool XessMfgUnlock::applied() {
    std::lock_guard lock(g_mutex);
    return g_state.applied;
}

bool XessMfgUnlock::providerIsAudited(const std::wstring& path) {
    FileIdentity identity{};
    IdentityError error = IdentityError::None;
    if (!computeFileIdentity(path, identity, error)) return false;
    return identity.sizeBytes == kKnownProviderSize && identity.sha256Upper == kKnownProviderSha256;
}

} // namespace veyra::gfx
