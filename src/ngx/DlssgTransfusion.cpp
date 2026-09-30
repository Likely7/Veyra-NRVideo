// DLSS-G 310.9.1 provider patches for RTX 40 / RTX 30. Ported from
// SilyNoMeta/DLSSG-Transfusion (MIT), tag v1.4.5.3-rtx20-30-40, commit
// b56bd2deed114507ad2c88f986d90ed50ffb4639, itself a fork of
// TonyJoaca/DLSSG-Transfusion (MIT, Copyright (c) 2026 Michael Robles).
// Function names follow upstream so each part can be traced back:
//   patcher.cpp        SafeScanDlssgArchSites/PatchDlssgArchGates,
//                      SafeFindUniqueImmediate/PatchDlssgMinimumArchitecture,
//                      kNgxPatch + PatchUniqueExecutablePattern
//   midpoint_fix.cpp   Lz4BlockDecompress, Find*PtxEntry, BuildTemporalFatbin,
//                      BuildBlackwellTransfusionFatbin, RetargetContainers,
//                      PatchProvider, PrepareModuleImage
//   image_kernels.h    SourceHash, VectorizeBlendStores
//   cu_module_hook.h   Replacement, Hook
//   network_optimizer.h  rules, CreateVariants, HookFunction, HookLaunch
// Veyra changes (see DlssgTransfusion.h): recorded writes with verified
// rollback, NvAPI through the provider's GetProcAddress import slot, kernels
// from runtime files, logging through veyra::log, Turing/Vulkan/Streamline/HUD
// and the scatter/input-motion experiments removed.
#include "veyra/ngx/DlssgTransfusion.h"

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "compat/RestoreMemory.h"
#include "transfusion/quality_fix.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace veyra::ngx {
namespace {

constexpr const char* kTag = "dlssg-tf";

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
struct Write {
    uint8_t* address = nullptr;
    std::vector<uint8_t> original;
};

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using QueryInterfaceFn = void*(__cdecl*)(uint32_t);
using CreateModuleFn = int(__cdecl*)(void* device, const void* blob, uint32_t size, void** module);
using CreateFunctionFn = int(__cdecl*)(void* device, void* module, const char* name, void** function);
using LaunchFn = int(__cdecl*)(void* commandList, const void* entries, uint32_t count);

struct Global {
    std::recursive_mutex mutex;
    HMODULE module = nullptr;
    std::vector<Write> writes;
    std::vector<void*> allocations;     // rebuilt fatbins referenced by redirected descriptors
    DlssgTransfusion::State state;
    DlssgTransfusion::Options options;
    std::atomic<bool> active{false};    // hooks pass through when false
    std::atomic<uint32_t> targetSm{89};
    // Kernel payloads (runtime files), set before `active` and not modified while it is.
    std::unordered_map<uint16_t, std::string> networkPtx;
    std::string outputPull, outputPushFine;
    bool imageReplacements = false;
    // NvAPI originals: driver functions, valid for the process lifetime. Never
    // cleared because the provider may cache our wrappers.
    std::atomic<GetProcAddressFn> realGetProc{nullptr};
    std::atomic<QueryInterfaceFn> realQuery{nullptr};
    std::atomic<CreateModuleFn> createModule{nullptr};
    std::atomic<CreateFunctionFn> createFunction{nullptr};
    std::atomic<LaunchFn> launch{nullptr};
    std::atomic<uint32_t> resolutions{0}, accepted{0}, rejected{0}, rewritten{0};
    std::atomic<bool> exactProviderKernels{false};
    bool importHooked = false;
};

Global& global() {
    static Global value;
    return value;
}

bool env(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) > 0; }
// UTF-8 conversion for log messages (paths and detail strings may hold
// non-ASCII characters; narrowing them directly would lose them).
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), char{0});
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// Records the original bytes, then writes through a temporary protection
// change. Every write is restored in reverse order by release().
bool patchBytes(void* destination, const void* source, size_t bytes) {
    auto& g = global();
    auto* p = static_cast<uint8_t*>(destination);
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    constexpr DWORD kExecute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const bool executable = (info.Protect & kExecute) != 0;
    DWORD old = 0;
    if (!VirtualProtect(p, bytes, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old)) return false;
    g.writes.push_back({p, std::vector<uint8_t>(p, p + bytes)});
    std::memcpy(p, source, bytes);
    if (executable) FlushInstructionCache(GetCurrentProcess(), p, bytes);
    DWORD ignored = 0;
    if (!VirtualProtect(p, bytes, old, &ignored)) {
        compat::memoryProtectionUncertain = true;
        log::error(kTag, std::format("protection restore failed address=0x{:X} win32={}", uintptr_t(p), GetLastError()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// PE helpers
// ---------------------------------------------------------------------------
const IMAGE_NT_HEADERS64* imageHeaders(HMODULE module) {
    if (!module) return nullptr;
    __try {
        const auto* base = reinterpret_cast<const uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 1024 * 1024) return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
        return nt;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

size_t sectionSpan(const IMAGE_NT_HEADERS64* nt, const IMAGE_SECTION_HEADER& section) {
    if (section.VirtualAddress >= nt->OptionalHeader.SizeOfImage) return 0;
    return std::min<size_t>(nt->OptionalHeader.SizeOfImage - section.VirtualAddress, section.Misc.VirtualSize);
}

// Import slot of `function` in the provider's own IAT.
void** findImportSlot(HMODULE module, const char* function) {
    const auto* nt = imageHeaders(module);
    if (!nt) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    void** found = nullptr;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (!d->OriginalFirstThunk || !d->FirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0) continue;
            if (found) return nullptr; // ambiguous: never guess
            found = reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// Code patches (patcher.cpp)
// ---------------------------------------------------------------------------
bool SafeScanDlssgArchSites(const uint8_t* base, const IMAGE_NT_HEADERS64* nt, uint8_t archNew,
    uint8_t** outSites, size_t maxSites, size_t* outFound, size_t* outAlreadyPatched) {
    __try {
        constexpr uint8_t kArchOld = 0xB0;
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const uint8_t* start = base + section->VirtualAddress;
            const size_t size = sectionSpan(nt, *section);
            if (size < 7) continue;
            for (size_t off = 0; off + 7 <= size; ++off) {
                if (start[off] == 0x3D && start[off + 2] == 0x01 && start[off + 3] == 0x00 && start[off + 4] == 0x00) {
                    if (start[off + 1] == kArchOld && *outFound < maxSites) outSites[(*outFound)++] = const_cast<uint8_t*>(start + off + 1);
                    else if (start[off + 1] == archNew) ++(*outAlreadyPatched);
                    continue;
                }
                if (start[off] == 0x81 && start[off + 1] >= 0xF8 && start[off + 3] == 0x01 && start[off + 4] == 0x00 && start[off + 5] == 0x00) {
                    if (start[off + 2] == kArchOld && *outFound < maxSites) outSites[(*outFound)++] = const_cast<uint8_t*>(start + off + 2);
                    else if (start[off + 2] == archNew) ++(*outAlreadyPatched);
                    continue;
                }
                if ((start[off] >= 0x40 && start[off] <= 0x4F) && start[off + 1] == 0x81 && start[off + 2] >= 0xF8 &&
                    start[off + 4] == 0x01 && start[off + 5] == 0x00 && start[off + 6] == 0x00) {
                    if (start[off + 3] == kArchOld && *outFound < maxSites) outSites[(*outFound)++] = const_cast<uint8_t*>(start + off + 3);
                    else if (start[off + 3] == archNew) ++(*outAlreadyPatched);
                }
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Blackwell-only gates (0x1b0) are lowered to the architecture actually
// present: 0x190 on Ada, 0x170 on Ampere.
size_t PatchDlssgArchGates(HMODULE module, uint32_t target) {
    const auto* nt = imageHeaders(module);
    if (!nt) return 0;
    uint8_t* sites[64]{};
    size_t found = 0, already = 0;
    const auto archNew = static_cast<uint8_t>(target & 0xFF);
    if (!SafeScanDlssgArchSites(reinterpret_cast<uint8_t*>(module), nt, archNew, sites, std::size(sites), &found, &already)) return 0;
    if (found != DlssgTransfusion::kExpectedArchGateSites || already != 0) {
        log::error(kTag, std::format("arch gates: found {} site(s) ({} already patched), expected {}", found, already,
                                     DlssgTransfusion::kExpectedArchGateSites));
        return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < found; ++i) written += patchBytes(sites[i], &archNew, 1) ? 1 : 0;
    log::info(kTag, std::format("arch gates: {} site(s) rewrote 0x1b0 -> 0x{:x}", written, target));
    return written;
}

// Below Ada the provider itself refuses to start: NVSDK_NGX_GetGPUArchitecture
// returns Ada (400) and the three *_GetFeatureRequirements report it as the
// minimum. The exports are found by name and the unique immediate 400 in their
// first bytes is lowered to the real architecture. A hook would not do:
// GetFeatureRequirements checks that its caller is nvngx.dll.
uint32_t* SafeFindUniqueImmediate(uint8_t* function, size_t span, uint32_t value) {
    __try {
        uint32_t* hit = nullptr;
        for (size_t i = 0; i + sizeof(uint32_t) <= span; ++i) {
            uint32_t candidate = 0;
            std::memcpy(&candidate, function + i, sizeof(candidate));
            if (candidate != value) continue;
            if (hit) return nullptr; // ambiguous: never guess
            hit = reinterpret_cast<uint32_t*>(function + i);
        }
        return hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

size_t PatchDlssgMinimumArchitecture(HMODULE module, uint32_t target) {
    const auto* nt = imageHeaders(module);
    if (!nt) return 0;
    auto* base = reinterpret_cast<uint8_t*>(module);
    static constexpr const char* kExports[] = {
        "NVSDK_NGX_GetGPUArchitecture",
        "NVSDK_NGX_D3D11_GetFeatureRequirements",
        "NVSDK_NGX_D3D12_GetFeatureRequirements",
        "NVSDK_NGX_VULKAN_GetFeatureRequirements",
    };
    size_t patched = 0;
    for (const char* name : kExports) {
        auto* function = reinterpret_cast<uint8_t*>(GetProcAddress(module, name));
        if (!function) continue;
        const auto offset = static_cast<size_t>(function - base);
        if (offset >= nt->OptionalHeader.SizeOfImage) continue;
        const size_t span = std::min<size_t>(400, nt->OptionalHeader.SizeOfImage - offset);
        uint32_t* immediate = SafeFindUniqueImmediate(function, span, DlssgTransfusion::kAdaArchId);
        if (!immediate) {
            log::error(kTag, std::format("minimum architecture: {} has no unique Ada immediate", name));
            continue;
        }
        patched += patchBytes(immediate, &target, sizeof(target)) ? 1 : 0;
    }
    log::info(kTag, std::format("minimum architecture: {} of {} export(s) lowered 0x190 -> 0x{:x}", patched,
                                std::size(kExports), target));
    return patched;
}

// DLSS-G provider count/index validator ("NGX device support"): the jz that
// refuses multi-frame counts on non-Blackwell is removed.
constexpr std::array<uint8_t, 13> kNgxPattern{0x84, 0xD2, 0x0F, 0x84, 0x03, 0x01, 0x00, 0x00, 0xBE, 0x05, 0x00, 0x00, 0x00};
constexpr size_t kNgxPatchOffset = 2;
constexpr std::array<uint8_t, 6> kNgxReplacement{0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

uint8_t* SafeFindUniquePattern(const uint8_t* base, const IMAGE_NT_HEADERS64* nt, const uint8_t* pattern, size_t size, size_t* count) {
    __try {
        uint8_t* match = nullptr;
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const uint8_t* start = base + section->VirtualAddress;
            const size_t span = sectionSpan(nt, *section);
            for (size_t off = 0; off + size <= span; ++off) {
                if (std::memcmp(start + off, pattern, size) != 0) continue;
                match = const_cast<uint8_t*>(start + off);
                ++*count;
            }
        }
        return match;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool PatchValidator(HMODULE module) {
    const auto* nt = imageHeaders(module);
    if (!nt) return false;
    size_t count = 0;
    uint8_t* match = SafeFindUniquePattern(reinterpret_cast<uint8_t*>(module), nt, kNgxPattern.data(), kNgxPattern.size(), &count);
    if (count != 1 || !match) {
        log::error(kTag, std::format("NGX device support: expected one code pattern, found {}", count));
        return false;
    }
    const bool ok = patchBytes(match + kNgxPatchOffset, kNgxReplacement.data(), kNgxReplacement.size());
    log::info(kTag, std::format("NGX device support: patched={} rva=0x{:X}", ok, uintptr_t(match + kNgxPatchOffset - reinterpret_cast<uint8_t*>(module))));
    return ok;
}

// ---------------------------------------------------------------------------
// Fatbin handling (midpoint_fix.cpp)
// ---------------------------------------------------------------------------
constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kAdaArch = 89;
constexpr uint32_t kBlackwellArch = 120;
constexpr uint32_t kArchParked = 122;
constexpr uint64_t kUncompressedFlags = 0x41;

uint16_t ReadU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, sizeof(v)); return v; }
uint32_t ReadU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, sizeof(v)); return v; }
uint64_t ReadU64(const uint8_t* p) { uint64_t v = 0; std::memcpy(&v, p, sizeof(v)); return v; }

std::string GetPtxEntryName(std::string_view ptx) {
    size_t pos = ptx.find(".entry ");
    if (pos == std::string_view::npos) return {};
    pos += sizeof(".entry ") - 1;
    size_t end = pos;
    while (end < ptx.size() && ptx[end] != '(' && ptx[end] != ' ' && ptx[end] != '\r' && ptx[end] != '\n') ++end;
    return std::string(ptx.substr(pos, end - pos));
}

// Rewrites the PTX .target directive to the target SM (uncompressed entries
// may change length).
bool RetargetPtxText(std::string& text, uint32_t sm) {
    const size_t directive = text.find(".target sm_");
    if (directive == std::string::npos) return false;
    size_t end = directive + sizeof(".target ") - 1;
    while (end < text.size() && text[end] != '\r' && text[end] != '\n' && text[end] != ' ' && text[end] != ',') ++end;
    text.replace(directive, end - directive, ".target sm_" + std::to_string(sm));
    return true;
}

bool Lz4BlockDecompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    size_t in = 0, out = 0;
    while (in < srcSize) {
        const uint8_t token = src[in++];
        size_t literals = token >> 4;
        if (literals == 15) {
            uint8_t ext = 0;
            do {
                if (in >= srcSize) return false;
                ext = src[in++];
                literals += ext;
            } while (ext == 0xFF);
        }
        if (literals > srcSize - in || literals > dstSize - out) return false;
        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;
        if (in == srcSize) break;
        if (srcSize - in < 2) return false;
        const size_t back = static_cast<size_t>(src[in]) | (static_cast<size_t>(src[in + 1]) << 8);
        in += 2;
        if (back == 0 || back > out) return false;
        size_t match = 4 + (token & 0x0F);
        if ((token & 0x0F) == 15) {
            uint8_t ext = 0;
            do {
                if (in >= srcSize) return false;
                ext = src[in++];
                match += ext;
            } while (ext == 0xFF);
        }
        if (match > dstSize - out) return false;
        for (size_t i = 0; i < match; ++i) dst[out + i] = dst[out + i - back];
        out += match;
    }
    return in == srcSize && out == dstSize;
}

bool FindPtxEntry(const uint8_t* fat, size_t fatSize, uint32_t arch, size_t& entryOffset) {
    if (fatSize < kOuterHeader || ReadU32(fat) != kFatbinMagic) return false;
    if (ReadU16(fat + 6) != kOuterHeader) return false;
    if (ReadU64(fat + 8) + kOuterHeader != fatSize) return false;
    size_t p = kOuterHeader;
    while (p + 64 <= fatSize) {
        const uint32_t kind = ReadU16(fat + p);
        const uint32_t hdr = ReadU32(fat + p + 4);
        const uint64_t payload = ReadU64(fat + p + 8);
        const size_t remain = fatSize - p;
        if (hdr < 64 || payload == 0 || hdr > remain || payload > remain - hdr) return false;
        if (kind == kPtxKind && ReadU32(fat + p + 28) == arch) {
            entryOffset = p;
            return true;
        }
        p += hdr + static_cast<size_t>(payload);
    }
    return false;
}

bool DecompressEntry(const uint8_t* fat, size_t entry, std::string& text) {
    const uint32_t hdr = ReadU32(fat + entry + 4);
    const uint32_t compressed = ReadU32(fat + entry + 16);
    const uint64_t raw = ReadU64(fat + entry + 56);
    if (compressed == 0 || raw == 0 || raw > (8u << 20)) return false;
    text.resize(static_cast<size_t>(raw));
    return Lz4BlockDecompress(fat + entry + hdr, compressed, reinterpret_cast<uint8_t*>(text.data()), text.size());
}

// Replaces one PTX entry (at `entry`) by an uncompressed payload for `sm`,
// truncating the fatbin after it so the driver JITs that PTX.
void EmitUncompressedEntry(const uint8_t* fat, size_t entry, const std::string& ptx, uint32_t sm, std::vector<uint8_t>& out) {
    const uint32_t hdr = ReadU32(fat + entry + 4);
    const size_t padded = (ptx.size() + 7) & ~size_t{7};
    const size_t finalSize = entry + hdr + padded;
    out.assign(fat, fat + entry + hdr);
    out.resize(finalSize, 0);
    std::memcpy(out.data() + entry + hdr, ptx.data(), ptx.size());
    const uint64_t payload64 = padded;
    const uint32_t zero32 = 0;
    const uint64_t zero64 = 0;
    std::memcpy(out.data() + entry + 8, &payload64, sizeof(payload64));
    std::memcpy(out.data() + entry + 16, &zero32, sizeof(zero32));
    std::memcpy(out.data() + entry + 40, &kUncompressedFlags, sizeof(kUncompressedFlags));
    std::memcpy(out.data() + entry + 56, &zero64, sizeof(zero64));
    std::memcpy(out.data() + entry + 28, &sm, sizeof(sm));
    const uint64_t outer = finalSize - kOuterHeader;
    std::memcpy(out.data() + 8, &outer, sizeof(outer));
}

// DLSS-G 310.9+ temporal profile (upstream kTemporalProfiles[1]): used only
// when the Blackwell kernels are disabled, where the Ada scatter kernel still
// blends every generated frame at the compiled-in midpoint.
constexpr size_t kScatterPtxBytes = 99626;
constexpr char kScatterEntry[] = "Kernel_EstimateIntermMvecsScatter";
constexpr char kScatterParam[] = ".param .align 8 .b8 Kernel_EstimateIntermMvecsScatter_param_0[144]";
constexpr char kScatterRegs[] = ".reg .f32 %f<1362>;";
constexpr char kScatterTemporalInput[] =
    "ld.param.f32 %f134, [Kernel_EstimateIntermMvecsScatter_param_0+32];\r\n"
    "mov.f32 %f135, 0f3F800000;\r\n"
    "sub.ftz.f32 %f136, %f135, %f134;\r\n";
constexpr char kCurrToPrev[] = "%f136";
constexpr char kPrevToCurr[] = "%f134";
constexpr size_t kExpectedMidpoints = 104;
constexpr char kJoinLabel[] = "$L__BB0_3:";
constexpr char kMidpointBits[] = "0f3F000000";
constexpr char kMulPrefix[] = "mul.ftz.f32 ";

bool BuildTemporalFatbin(const uint8_t* fat, size_t fatSize, uint32_t targetSm, std::vector<uint8_t>& out, std::string& why) {
    size_t entry = 0;
    if (!FindPtxEntry(fat, fatSize, kAdaArch, entry)) { why = "no sm_89 PTX entry"; return false; }
    if (ReadU64(fat + entry + 56) != kScatterPtxBytes) { why = "PTX size differs from the 310.9 profile"; return false; }
    std::string ptx;
    if (!DecompressEntry(fat, entry, ptx)) { why = "LZ4 decompression failed"; return false; }
    if (ptx.find(std::string(".entry ") + kScatterEntry + "(") == std::string::npos || ptx.find(kScatterParam) == std::string::npos ||
        ptx.find(kScatterRegs) == std::string::npos) {
        why = "temporal kernel signature changed";
        return false;
    }
    const size_t label = ptx.find(kJoinLabel);
    if (label == std::string::npos || ptx.find(kJoinLabel, label + 1) != std::string::npos) { why = "join label not unique"; return false; }
    size_t insertion = ptx.find('\n', label);
    if (insertion == std::string::npos) { why = "join label has no line end"; return false; }
    ++insertion;
    std::vector<size_t> marks;
    const size_t midLen = sizeof(kMidpointBits) - 1, mulLen = sizeof(kMulPrefix) - 1;
    for (size_t i = ptx.find(kMidpointBits); i != std::string::npos; i = ptx.find(kMidpointBits, i + 1)) {
        if (i + midLen >= ptx.size() || ptx[i + midLen] != ';') continue;
        size_t line = i;
        while (line > 0 && ptx[line - 1] != '\n') --line;
        if (i - line < mulLen || ptx.compare(line, mulLen, kMulPrefix) != 0) continue;
        marks.push_back(i);
    }
    if (marks.size() != kExpectedMidpoints) { why = std::format("found {} midpoint multiplies, expected {}", marks.size(), kExpectedMidpoints); return false; }
    if (marks.front() <= insertion) { why = "first midpoint precedes the insertion point"; return false; }
    std::string patched = ptx.substr(0, insertion) + kScatterTemporalInput;
    size_t src = insertion;
    for (size_t i = 0; i < marks.size(); ++i) {
        patched.append(ptx, src, marks[i] - src);
        patched += i < kExpectedMidpoints / 2 ? kCurrToPrev : kPrevToCurr;
        src = marks[i] + midLen;
    }
    patched.append(ptx, src, std::string::npos);
    if (targetSm != kAdaArch && !RetargetPtxText(patched, targetSm)) { why = "temporal PTX has no .target directive"; return false; }
    EmitUncompressedEntry(fat, entry, patched, targetSm, out);
    return true;
}

bool BuildBlackwellTransfusionFatbin(const uint8_t* fat, size_t fatSize, const char* kernelName, uint32_t targetSm,
    bool quality, bool explainedWarp, std::vector<uint8_t>& out, std::string& why) {
    size_t adaEntry = 0, bwEntry = 0;
    if (!FindPtxEntry(fat, fatSize, kAdaArch, adaEntry)) { why = "no sm_89 Ada PTX entry in fatbin"; return false; }
    if (!FindPtxEntry(fat, fatSize, kBlackwellArch, bwEntry)) { why = "no sm_120 Blackwell PTX entry in fatbin"; return false; }
    std::string ptx;
    if (!DecompressEntry(fat, bwEntry, ptx)) { why = "Blackwell LZ4 decompression failed"; return false; }
    if (std::strcmp(kernelName, "Kernel_BlendCandidatesFused") == 0 && quality) {
        std::string status;
        const bool patched = quality_fix::Patch(ptx, status, explainedWarp ? quality_fix::Policy::ExplainedWarp : quality_fix::Policy::Transfusion);
        log::info(kTag, std::format("quality PTX preparation: {} (patched={})", status, patched));
    }
    const size_t targetPos = ptx.find(".target sm_120");
    if (targetPos == std::string::npos) { why = "could not find .target sm_120 in Blackwell PTX"; return false; }
    size_t after = targetPos + sizeof(".target sm_120") - 1;
    while (after < ptx.size() && (ptx[after] == '\r' || ptx[after] == '\n' || ptx[after] == ' ')) ++after;
    ptx.replace(targetPos, after - targetPos, ".target sm_" + std::to_string(targetSm) + "\n");
    // The rewritten entry replaces the Ada PTX; label it for the target SM.
    EmitUncompressedEntry(fat, adaEntry, ptx, targetSm, out);
    return true;
}

std::string GetFatbinKernelName(const uint8_t* fat, size_t fatSize) {
    size_t entry = 0;
    if (!FindPtxEntry(fat, fatSize, kBlackwellArch, entry)) return {};
    std::string ptx;
    if (!DecompressEntry(fat, entry, ptx)) return {};
    return GetPtxEntryName(ptx);
}

// Retargets kernel containers in place so the driver compiles their PTX for
// the target SM. The .target directive sits in an LZ4 literal run in every
// container of the supported provider, so a same-length rewrite is enough.
// Ada: every container holding a Blackwell sm_120 PTX is relabelled sm_89 and
// its Ada images are parked at arch 122. Below Ada every container gets one
// PTX relabelled to the target SM (the Blackwell build when preferred and
// present, else the Ada PTX) and every other image parked.
unsigned RetargetContainers(uint8_t* base, const IMAGE_NT_HEADERS64* nt, bool preferBlackwell, uint32_t targetSm, bool& writeFailed) {
    unsigned rewritten = 0;
    const bool preAda = targetSm < kAdaArch;
    char target[16]{};
    std::snprintf(target, sizeof(target), "sm_%u", targetSm);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0 || (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        const size_t size = sectionSpan(nt, *section);
        if (size < kOuterHeader) continue;
        uint8_t* start = base + section->VirtualAddress;
        for (uint8_t* c = start; c + kOuterHeader <= start + size;) {
            if (ReadU32(c) != kFatbinMagic) { ++c; continue; }
            const uint16_t headerSize = ReadU16(c + 6);
            const uint64_t fatSize = ReadU64(c + 8);
            const auto remain = static_cast<size_t>((start + size) - c);
            if (headerSize != 0x10 || fatSize == 0 || remain < 16 || fatSize > remain - 16) { c += 4; continue; }
            const size_t total = static_cast<size_t>(fatSize) + 16;
            struct Image { uint8_t* header; size_t hdr; size_t payload; uint16_t kind; uint32_t arch; };
            std::vector<Image> images;
            for (uint8_t* img = c + 16; img + 32 <= c + total;) {
                const uint32_t imgHeader = ReadU32(img + 4);
                const uint64_t payload = ReadU64(img + 8);
                const auto imgRemain = static_cast<size_t>((c + total) - img);
                if (imgHeader < 32 || payload == 0 || imgHeader > imgRemain || payload > imgRemain - imgHeader) break;
                images.push_back({img, imgHeader, static_cast<size_t>(payload), ReadU16(img), ReadU32(img + 28)});
                img += imgHeader + static_cast<size_t>(payload);
            }
            const Image* blackwell = nullptr;
            const Image* ada = nullptr;
            bool hasAdaImage = false;
            for (const auto& image : images) {
                if (image.kind == kPtxKind && image.arch == kBlackwellArch) blackwell = &image;
                if (image.kind == kPtxKind && image.arch == kAdaArch) ada = &image;
                if (image.arch == kAdaArch) hasAdaImage = true;
            }
            const Image* chosen = !preAda ? (hasAdaImage ? blackwell : nullptr) : ((preferBlackwell && blackwell) ? blackwell : ada);
            if (chosen) {
                // Same-length directive rewrite: "sm_120" -> "sm_89 ", "sm_89" -> "sm_86".
                char from[24]{}, to[24]{};
                std::snprintf(from, sizeof(from), ".target sm_%u", chosen->arch);
                std::snprintf(to, sizeof(to), ".target %-*s", static_cast<int>(std::strlen(from) - 8), target);
                uint8_t* body = chosen->header + chosen->hdr;
                uint8_t* bodyEnd = body + chosen->payload;
                const size_t fromLength = std::strlen(from);
                auto* at = std::search(body, bodyEnd, reinterpret_cast<const uint8_t*>(from), reinterpret_cast<const uint8_t*>(from) + fromLength);
                if (at != bodyEnd && std::strlen(to) == fromLength) {
                    bool ok = patchBytes(at, to, fromLength);
                    ok = patchBytes(chosen->header + 28, &targetSm, sizeof(targetSm)) && ok;
                    for (const auto& image : images) {
                        if (&image == chosen) continue;
                        if (preAda || image.arch == kAdaArch) ok = patchBytes(image.header + 28, &kArchParked, sizeof(kArchParked)) && ok;
                    }
                    writeFailed |= !ok;
                    ++rewritten;
                    c += total;
                    continue;
                }
            }
            c += 4;
        }
    }
    return rewritten;
}

bool SafeValidateFatbinCandidate(const uint8_t* candidate, size_t maxBytes, size_t& outTotal) {
    __try {
        if (ReadU32(candidate) != kFatbinMagic || ReadU16(candidate + 6) != kOuterHeader) return false;
        const uint64_t declared = ReadU64(candidate + 8);
        if (declared > (16u << 20) || declared < (1024 - kOuterHeader)) return false;
        const size_t total = static_cast<size_t>(declared) + kOuterHeader;
        if (total > maxBytes) return false;
        outTotal = total;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct ProviderResult {
    bool ok = false, blackwell = false, writeFailed = false;
    size_t descriptors = 0;
    unsigned retargeted = 0;
};

// PatchProvider: Blackwell kernels (the temporal scatter and the candidate
// blend rebuilt from their sm_120 PTX, every other container retargeted in
// place) or, when disabled, the Ada temporal midpoint fix.
ProviderResult PatchProvider(HMODULE module, uint32_t targetSm, const DlssgTransfusion::Options& options) {
    auto& g = global();
    ProviderResult result;
    const auto* nt = imageHeaders(module);
    if (!nt) return result;
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto start = reinterpret_cast<uintptr_t>(base);
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    std::map<const uint8_t*, std::vector<uint64_t*>> fatbinSlots;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0 || (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        uint8_t* sec = base + section->VirtualAddress;
        const size_t size = sectionSpan(nt, *section);
        for (size_t off = 0; off + sizeof(uint64_t) <= size; off += sizeof(uint64_t)) {
            const uint64_t value = ReadU64(sec + off);
            if (value < start || value >= start + imageSize) continue;
            const auto remaining = static_cast<size_t>((start + imageSize) - value);
            if (remaining < 16) continue;
            const auto* candidate = reinterpret_cast<const uint8_t*>(value);
            size_t total = 0;
            if (!SafeValidateFatbinCandidate(candidate, remaining, total)) continue;
            fatbinSlots[candidate].push_back(reinterpret_cast<uint64_t*>(sec + off));
        }
    }
    struct TargetFatbin {
        const char* kernelName;
        const uint8_t* fat = nullptr;
        size_t fatSize = 0;
        std::vector<uint64_t*> slots;
    };
    TargetFatbin targets[] = {{"Kernel_EstimateIntermMvecsScatter"}, {"Kernel_BlendCandidatesFused"}};
    const uint8_t* temporalFat = nullptr;
    size_t temporalFatSize = 0;
    std::vector<uint64_t*> temporalSlots;
    for (const auto& [candidate, slots] : fatbinSlots) {
        const size_t total = static_cast<size_t>(ReadU64(candidate + 8)) + kOuterHeader;
        size_t entry = 0;
        if (!temporalFat && FindPtxEntry(candidate, total, kAdaArch, entry) && ReadU64(candidate + entry + 56) == kScatterPtxBytes) {
            temporalFat = candidate;
            temporalFatSize = total;
            temporalSlots = slots;
        }
        const std::string name = GetFatbinKernelName(candidate, total);
        for (auto& target : targets) {
            if (!target.fat && !name.empty() && name == target.kernelName) {
                target.fat = candidate;
                target.fatSize = total;
                target.slots = slots;
                break;
            }
        }
    }
    auto redirect = [&](const std::vector<uint8_t>& rebuilt, const std::vector<uint64_t*>& slots) {
        void* memory = VirtualAlloc(nullptr, rebuilt.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!memory) return size_t{0};
        std::memcpy(memory, rebuilt.data(), rebuilt.size());
        g.allocations.push_back(memory);
        size_t written = 0;
        const auto value = reinterpret_cast<uint64_t>(memory);
        for (uint64_t* slot : slots) written += patchBytes(slot, &value, sizeof(value)) ? 1 : 0;
        result.writeFailed |= written != slots.size();
        return written;
    };
    if (options.blackwellKernels && targets[0].fat) {
        for (const auto& target : targets) {
            if (std::strcmp(target.kernelName, "Kernel_BlendCandidatesFused") == 0 && !options.qualityValidWarp) continue;
            if (&target != &targets[0] && result.descriptors == 0) break; // never ready on an incomplete temporal patch
            if (!target.fat || target.slots.empty()) continue;
            std::vector<uint8_t> rebuilt;
            std::string why;
            if (!BuildBlackwellTransfusionFatbin(target.fat, target.fatSize, target.kernelName, targetSm, options.qualityValidWarp,
                    options.explainedWarp, rebuilt, why)) {
                log::error(kTag, std::format("Blackwell kernels for {} failed ({})", target.kernelName, why));
                continue;
            }
            const size_t written = redirect(rebuilt, target.slots);
            result.descriptors += written;
            log::info(kTag, std::format("redirected {} of {} {} descriptor(s) ({} bytes)", written, target.slots.size(), target.kernelName, rebuilt.size()));
        }
        if (result.descriptors) {
            result.retargeted = RetargetContainers(base, nt, true, targetSm, result.writeFailed);
            result.blackwell = true;
            result.ok = !result.writeFailed;
            log::info(kTag, std::format("{} container(s) retargeted in place (Blackwell preferred -> sm_{}), {} descriptor(s) redirected",
                                        result.retargeted, targetSm, result.descriptors));
            return result;
        }
    }
    // Ada temporal midpoint fix on EstimateIntermMvecsScatter.
    std::vector<uint8_t> rebuilt;
    std::string why;
    const bool built = temporalFat && !temporalSlots.empty() && BuildTemporalFatbin(temporalFat, temporalFatSize, targetSm, rebuilt, why);
    if (targetSm < kAdaArch) {
        result.retargeted = RetargetContainers(base, nt, false, targetSm, result.writeFailed);
        log::info(kTag, std::format("{} container(s) retargeted in place (Ada PTX -> sm_{})", result.retargeted, targetSm));
    }
    if (!built) {
        log::error(kTag, std::format("temporal midpoint rebuild failed ({})", temporalFat ? why : std::string("no temporal descriptor")));
        return result;
    }
    result.descriptors = redirect(rebuilt, temporalSlots);
    result.ok = result.descriptors > 0 && !result.writeFailed;
    log::info(kTag, std::format("redirected {} scatter descriptor(s) to the temporal-corrected rebuild ({} bytes)", result.descriptors, rebuilt.size()));
    return result;
}

// ---------------------------------------------------------------------------
// Exact image kernels (image_kernels.h, PrepareModuleImage)
// ---------------------------------------------------------------------------
constexpr uint64_t kOutputPullSourceHash = 0xf4194a9d30dea518ull;     // NVIDIA 310.9.1 sm_120
constexpr uint64_t kOutputPushFineSourceHash = 0x3a728f2af0a08fc8ull; // NVIDIA 310.9.1 sm_120

// FNV-1a over the PTX with CR, trailing NULs and the first .target line
// removed, so a copy retargeted in place hashes like the original.
uint64_t SourceHash(std::string_view ptx) {
    std::string text;
    text.reserve(ptx.size());
    for (char c : ptx) if (c != '\r') text.push_back(c);
    while (!text.empty() && text.back() == '\0') text.pop_back();
    const size_t target = text.find(".target ");
    if (target != std::string::npos) {
        const size_t end = text.find('\n', target);
        text.erase(target, end == std::string::npos ? std::string::npos : end - target + 1);
    }
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : text) hash = (hash ^ c) * 1099511628211ull;
    return hash;
}

struct StorePair { const char* base; const char* first; const char* second; };
constexpr StorePair kBlendStorePairs[] = {
    {"%rd83", "%r553", "%r554"}, {"%rd84", "%r555", "%r556"}, {"%rd72", "%r543", "%r544"}, {"%rd73", "%r545", "%r546"},
    {"%rd61", "%r491", "%r492"}, {"%rd62", "%r493", "%r494"}, {"%rd50", "%r382", "%r383"}, {"%rd51", "%r384", "%r385"},
    {"%rd39", "%r234", "%r235"}, {"%rd40", "%r236", "%r237"}};

// Merges the ten store pairs; all ten must be found exactly once, in order,
// or the text is left unchanged.
bool VectorizeBlendStores(std::string& ptx) {
    std::string text = ptx;
    for (const auto& pair : kBlendStorePairs) {
        const std::string first = std::string("st.global.u32 [") + pair.base + "], " + pair.first + ";";
        const std::string second = std::string("st.global.u32 [") + pair.base + "+4], " + pair.second + ";";
        const size_t a = text.find(first), b = text.find(second);
        if (a == std::string::npos || b == std::string::npos || b < a || text.find(first, a + 1) != std::string::npos ||
            text.find(second, b + 1) != std::string::npos)
            return false;
        text.replace(b, second.size(), std::string("st.global.v2.u32 [") + pair.base + "], {" + pair.first + ", " + pair.second + "};");
        text.erase(a, first.size());
    }
    ptx.swap(text);
    return true;
}

bool SafeCopyBlob(const void* blob, size_t size, uint8_t* out) {
    __try {
        std::memcpy(out, blob, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The in-place retarget cannot grow the compressed PTX, so the exact
// image-kernel optimizations happen on the image the provider hands to the
// driver. Returns true with a one-entry uncompressed fatbin.
bool PrepareModuleImage(const void* blob, std::vector<uint8_t>& replacement) {
    auto& g = global();
    const uint32_t targetSm = g.targetSm.load(std::memory_order_relaxed);
    if (!blob || !g.options.optimizedKernels) return false;
    std::vector<uint8_t> fat(kOuterHeader);
    if (!SafeCopyBlob(blob, kOuterHeader, fat.data()) || ReadU32(fat.data()) != kFatbinMagic || ReadU16(fat.data() + 6) != kOuterHeader) return false;
    // The provider passes its descriptor's size, which may still describe the
    // original image of a redirected rebuild; the driver follows the header.
    const uint64_t total = ReadU64(fat.data() + 8) + kOuterHeader;
    if (total > (16u << 20)) return false;
    fat.resize(static_cast<size_t>(total));
    if (!SafeCopyBlob(blob, fat.size(), fat.data())) return false;
    for (size_t p = kOuterHeader; p + 64 <= fat.size();) {
        const uint16_t kind = ReadU16(fat.data() + p);
        const uint32_t hdr = ReadU32(fat.data() + p + 4);
        const uint64_t payload = ReadU64(fat.data() + p + 8);
        if (hdr < 64 || payload == 0 || hdr > fat.size() - p || payload > fat.size() - p - hdr) return false;
        if (kind != kPtxKind || ReadU32(fat.data() + p + 28) != targetSm) {
            p += hdr + static_cast<size_t>(payload);
            continue;
        }
        const uint32_t compressed = ReadU32(fat.data() + p + 16);
        std::string text;
        if (compressed) {
            if (compressed > payload || !DecompressEntry(fat.data(), p, text)) return false;
        } else {
            text.assign(reinterpret_cast<const char*>(fat.data() + p + hdr), static_cast<size_t>(payload));
        }
        while (!text.empty() && text.back() == '\0') text.pop_back();
        const char* optimized = nullptr;
        const std::string entry = GetPtxEntryName(text);
        if (entry == "Kernel_OutputPull" || entry == "Kernel_OutputPushFine") {
            const bool pull = entry == "Kernel_OutputPull";
            if (SourceHash(text) == (pull ? kOutputPullSourceHash : kOutputPushFineSourceHash)) {
                g.exactProviderKernels.store(true, std::memory_order_release);
                if (g.imageReplacements) {
                    text = pull ? g.outputPull : g.outputPushFine;
                    RetargetPtxText(text, targetSm);
                    optimized = pull ? "OutputPull (packed row masks)" : "OutputPushFine (early block exit)";
                }
            }
        } else if (entry == "Kernel_BlendCandidatesFused" && VectorizeBlendStores(text)) {
            optimized = "BlendCandidatesFused (vector stores)";
        }
        if (!optimized) return false;
        log::info(kTag, std::format("optimized image kernel: {} for sm_{}", optimized, targetSm));
        const size_t padded = (text.size() + 1 + 7) & ~size_t{7};
        replacement.assign(fat.begin(), fat.begin() + kOuterHeader);
        replacement.insert(replacement.end(), fat.begin() + static_cast<ptrdiff_t>(p), fat.begin() + static_cast<ptrdiff_t>(p + hdr));
        replacement.resize(kOuterHeader + hdr + padded, 0);
        std::memcpy(replacement.data() + kOuterHeader + hdr, text.data(), text.size());
        uint8_t* e = replacement.data() + kOuterHeader;
        const uint64_t payload64 = padded;
        const uint32_t zero32 = 0;
        const uint64_t zero64 = 0;
        std::memcpy(e + 8, &payload64, sizeof(payload64));
        std::memcpy(e + 16, &zero32, sizeof(zero32));
        std::memcpy(e + 40, &kUncompressedFlags, sizeof(kUncompressedFlags));
        std::memcpy(e + 56, &zero64, sizeof(zero64));
        const uint64_t outer = replacement.size() - kOuterHeader;
        std::memcpy(replacement.data() + 8, &outer, sizeof(outer));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// NvAPI_D3D12_CreateCuModule (cu_module_hook.h)
// ---------------------------------------------------------------------------
// Replacement images keyed by the content of the original image; never freed,
// the driver may keep referring to a module's source image.
std::mutex cacheMutex;
std::unordered_map<uint64_t, std::unique_ptr<std::vector<uint8_t>>> cache;

bool SafeHash(const uint8_t* data, size_t size, uint64_t& hash) {
    __try {
        hash = 1469598103934665603ull;
        for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 1099511628211ull;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const std::vector<uint8_t>* Replacement(const void* blob, uint32_t size) {
    if (!blob || !size) return nullptr;
    uint64_t key = 0;
    if (!SafeHash(static_cast<const uint8_t*>(blob), size, key)) return nullptr;
    key ^= size;
    std::lock_guard lock(cacheMutex);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second.get();
    auto image = std::make_unique<std::vector<uint8_t>>();
    if (!PrepareModuleImage(blob, *image)) image.reset();
    return cache.emplace(key, std::move(image)).first->second.get();
}

int __cdecl HookCreateModule(void* device, const void* blob, uint32_t size, void** module) {
    auto& g = global();
    const auto original = g.createModule.load(std::memory_order_acquire);
    if (!g.active.load(std::memory_order_acquire)) return original(device, blob, size, module);
    const std::vector<uint8_t>* image = nullptr;
    try { image = Replacement(blob, size); } catch (...) { image = nullptr; }
    const void* source = image ? image->data() : blob;
    const auto sourceSize = image ? static_cast<uint32_t>(image->size()) : size;
    const int status = original(device, source, sourceSize, module);
    (status == 0 ? g.accepted : g.rejected).fetch_add(1, std::memory_order_relaxed);
    if (image) g.rewritten.fetch_add(1, std::memory_order_relaxed);
    if (status != 0) log::warn(kTag, std::format("CreateCuModule status={} size={} rewritten={}", status, size, image != nullptr));
    return status;
}

// ---------------------------------------------------------------------------
// DL1/DL2 network optimization (network_optimizer.h)
// The kernels and launch shapes of the dlssg_for_sm86 0.3.5 backend, whose
// output is bit-identical to NVIDIA's; NVIDIA's weights and parameters.
// ---------------------------------------------------------------------------
namespace net {
enum class Kind : uint8_t { Other, NetworkStart, NetworkEnd, Upscale, ElementWise, Conv, Pool, Dl2 };
enum class Grid : uint8_t { None, W16H4, Flat16, Flat32, Flat64, Pool2048 };

struct Rule {
    uint16_t resource; // sm_86 PTX kernel file k<resource>.ptx
    Grid grid;
    uint32_t z;
    uint32_t block;
    uint32_t paramBytes;
};

// Per NVIDIA conv rank. Fused ranks (the conv after each decoder add) have no
// plain rule: the fusion consumes them.
constexpr Rule kConvRules[17] = {
    {4000, Grid::W16H4, 1, 128, 48}, {4008, Grid::Flat16, 1, 128, 40}, {4002, Grid::W16H4, 4, 128, 48},
    {4004, Grid::W16H4, 4, 128, 48}, {4006, Grid::W16H4, 8, 256, 48}, {4010, Grid::Flat32, 8, 512, 40},
    {4012, Grid::Flat32, 8, 128, 40}, {0, Grid::None, 0, 0, 40},      {4014, Grid::Flat32, 2, 512, 40},
    {0, Grid::None, 0, 0, 40},        {4016, Grid::Flat16, 1, 256, 40}, {0, Grid::None, 0, 0, 40},
    {4018, Grid::Flat16, 1, 128, 40}, {0, Grid::None, 0, 0, 40},        {4020, Grid::Flat32, 1, 128, 40},
    {0, Grid::None, 0, 0, 40},        {4022, Grid::Flat16, 1, 64, 40}};
constexpr Rule kFusedRules[5] = {
    {4024, Grid::Flat16, 1, 256, 48}, {4026, Grid::Flat16, 1, 256, 48}, {4028, Grid::Flat16, 1, 256, 48},
    {4030, Grid::Flat16, 1, 128, 48}, {4032, Grid::Flat64, 1, 128, 48}};
constexpr Rule kPoolRule{4039, Grid::Pool2048, 1, 256, 36};

enum class Dl2 : uint8_t { InitialMerge, ConvPre, B0Conv0, B0Conv1, B0Conv2, B0Bot1, Upsample, CentralBlock, B1Conv0,
    B1Conv1, B1Bot1, Count };
struct Dl2Rule {
    const char* name;        // NVIDIA kernel
    uint16_t resource;       // sm_86 PTX kernel file
    const char* entry;
    uint32_t paramBytes;
    uint8_t hWord, wWord;    // output height and width, as 32-bit parameter words
    uint16_t dx, dy, z;      // grid: ceil(w/dx), ceil(h/dy), z; dy == 0: ceil(w*h/dx), z, 1
    uint32_t block;
    std::array<std::pair<uint8_t, uint32_t>, 6> shape; // expected parameter words (channels, kernel size)
};
constexpr Dl2Rule kDl2Rules[size_t(Dl2::Count)] = {
    {"k_initial_merge", 0, nullptr, 44, 0, 0, 0, 0, 0, 0, {}},
    {"custom_block0_convPre_kernel", 4042, "conv_dl2_merge_pool", 80, 17, 18, 8, 4, 1, 256,
        {{{uint8_t{4}, uint32_t{0x20}}, {uint8_t{5}, uint32_t{3}}, {uint8_t{6}, uint32_t{3}}, {uint8_t{7}, uint32_t{0xa}}, {uint8_t{13}, uint32_t{0xa}}, {uint8_t{19}, uint32_t{0x20}}}}},
    {"custom_block0_conv0_c8_kernel", 4044, "conv_dl2_pool", 80, 17, 18, 8, 2, 1, 128,
        {{{uint8_t{4}, uint32_t{0x20}}, {uint8_t{5}, uint32_t{3}}, {uint8_t{6}, uint32_t{3}}, {uint8_t{7}, uint32_t{0x20}}, {uint8_t{13}, uint32_t{0x20}}, {uint8_t{19}, uint32_t{0x20}}}}},
    {"custom_block0_conv1_kernel", 4046, "conv_dl2_pool", 80, 17, 18, 8, 2, 1, 256,
        {{{uint8_t{4}, uint32_t{0x40}}, {uint8_t{5}, uint32_t{3}}, {uint8_t{6}, uint32_t{3}}, {uint8_t{7}, uint32_t{0x20}}, {uint8_t{13}, uint32_t{0x20}}, {uint8_t{19}, uint32_t{0x40}}}}},
    {"custom_block0_conv2_kernel", 4050, "conv_dl2_resid", 92, 19, 20, 16, 2, 2, 128,
        {{{uint8_t{4}, uint32_t{0x40}}, {uint8_t{5}, uint32_t{3}}, {uint8_t{6}, uint32_t{3}}, {uint8_t{7}, uint32_t{0x40}}, {uint8_t{15}, uint32_t{0x40}}, {uint8_t{21}, uint32_t{0x40}}}}},
    {"custom_block0_conv_bot1_hf_kernel", 4062, "conv_dl2_bot1_block0", 144, 21, 22, 16, 4, 1, 128,
        {{{uint8_t{4}, uint32_t{8}}, {uint8_t{7}, uint32_t{0x20}}, {uint8_t{17}, uint32_t{0x20}}, {uint8_t{23}, uint32_t{4}}, {uint8_t{29}, uint32_t{1}}, {uint8_t{35}, uint32_t{3}}}}},
    {"custom_upsample_hf_kernel", 4059, "upsample_hf", 144, 21, 22, 256, 0, 3, 256,
        {{{uint8_t{5}, uint32_t{4}}, {uint8_t{11}, uint32_t{1}}, {uint8_t{17}, uint32_t{3}}, {uint8_t{23}, uint32_t{4}}, {uint8_t{29}, uint32_t{1}}, {uint8_t{35}, uint32_t{3}}}}},
    {"k_central_block", 0, nullptr, 68, 0, 0, 0, 0, 0, 0, {}},
    {"custom_block1_conv0_kernel", 4060, "conv_dl2_central_pool", 80, 17, 18, 8, 4, 1, 256,
        {{{uint8_t{4}, uint32_t{0x10}}, {uint8_t{5}, uint32_t{3}}, {uint8_t{6}, uint32_t{3}}, {uint8_t{7}, uint32_t{0x12}}, {uint8_t{13}, uint32_t{0x12}}, {uint8_t{19}, uint32_t{0x10}}}}},
    {"custom_block1_conv1_kernel", 4048, "conv_dl2_pool", 80, 17, 18, 8, 2, 1, 128,
        {{{uint8_t{4}, uint32_t{0x20}}, {uint8_t{5}, uint32_t{3}}, {uint8_t{6}, uint32_t{3}}, {uint8_t{7}, uint32_t{0x10}}, {uint8_t{13}, uint32_t{0x10}}, {uint8_t{19}, uint32_t{0x20}}}}},
    {"custom_block1_conv_bot1_hf_kernel", 4058, "conv_dl2_bot1_block1", 152, 27, 28, 16, 8, 1, 256,
        {{{uint8_t{4}, uint32_t{8}}, {uint8_t{7}, uint32_t{0x10}}, {uint8_t{23}, uint32_t{0x10}}, {uint8_t{29}, uint32_t{4}}, {uint8_t{33}, uint32_t{1}}, {uint8_t{37}, uint32_t{3}}}}}};

std::vector<uint16_t> resources() {
    std::vector<uint16_t> ids{kPoolRule.resource};
    for (const auto& r : kConvRules) if (r.resource) ids.push_back(r.resource);
    for (const auto& r : kFusedRules) ids.push_back(r.resource);
    for (const auto& r : kDl2Rules) if (r.resource) ids.push_back(r.resource);
    return ids;
}

struct Entry {
    void* function;
    uint32_t grid[3];
    uint32_t block[3];
    uint32_t sharedBytes, reserved;
    const void* params;
    uint32_t paramBytes, reserved2;
};
static_assert(sizeof(Entry) == 56);

struct Role {
    Kind kind = Kind::Other;
    Dl2 layer = Dl2::Count;
};
std::mutex mutex;
std::unordered_map<void*, Role> kinds;         // provider function handle -> role
std::unordered_map<uint16_t, void*> functions; // our function per rule resource
void* device = nullptr;
std::atomic<bool> ready{false};
std::atomic<bool> failed{false};
std::atomic<uint64_t> optimizedRuns{0};
std::atomic<uint64_t> optimizedDl2Runs{0};
std::map<std::pair<uint16_t, uint32_t>, std::unique_ptr<std::vector<uint8_t>>> images; // module sources, kept alive

void* Function(uint16_t resource) {
    const auto found = functions.find(resource);
    return found != functions.end() ? found->second : nullptr;
}

bool Enabled() {
    auto& g = global();
    return g.active.load(std::memory_order_acquire) && g.options.optimizedKernels &&
           g.exactProviderKernels.load(std::memory_order_acquire);
}

Kind Classify(const char* name) {
    if (!name) return Kind::Other;
    if (std::strcmp(name, "Kernel_DL1Net_Input") == 0) return Kind::NetworkStart;
    if (std::strcmp(name, "Kernel_DL1Net_Output") == 0) return Kind::NetworkEnd;
    if (std::strcmp(name, "k_upscale") == 0) return Kind::Upscale;
    if (std::strcmp(name, "k_element_wise") == 0) return Kind::ElementWise;
    if (std::strcmp(name, "k_conv_fp16_nhwc") == 0) return Kind::Conv;
    if (std::strcmp(name, "k_pooling") == 0) return Kind::Pool;
    return Kind::Other;
}

Role ClassifyRole(const char* name) {
    for (size_t i = 0; name && i < std::size(kDl2Rules); ++i)
        if (std::strcmp(name, kDl2Rules[i].name) == 0) return {Kind::Dl2, static_cast<Dl2>(i)};
    return {Classify(name)};
}

// One uncompressed PTX entry; header fields as in the 310.9.1 provider's own
// fatbins (entry header 0x50 bytes, ISA 8.5).
std::vector<uint8_t> BuildFatbin(std::string_view ptx, uint32_t sm) {
    const size_t padded = (ptx.size() + 1 + 7) & ~size_t{7};
    std::vector<uint8_t> out(16 + 0x50 + padded, 0);
    const uint32_t magic = 0xBA55ED50u;
    const uint16_t version = 1, outerHeader = 0x10;
    const uint64_t outerSize = 0x50 + padded;
    std::memcpy(out.data(), &magic, 4);
    std::memcpy(out.data() + 4, &version, 2);
    std::memcpy(out.data() + 6, &outerHeader, 2);
    std::memcpy(out.data() + 8, &outerSize, 8);
    uint8_t* entry = out.data() + 16;
    const uint16_t kind = 1, entryVersion = 0x0101, isaMinor = 5, isaMajor = 8;
    const uint32_t headerSize = 0x50, field20 = 0x40, extra = 0x48;
    const uint64_t payload = padded, flags = 0x41;
    std::memcpy(entry, &kind, 2);
    std::memcpy(entry + 2, &entryVersion, 2);
    std::memcpy(entry + 4, &headerSize, 4);
    std::memcpy(entry + 8, &payload, 8);
    std::memcpy(entry + 20, &field20, 4);
    std::memcpy(entry + 24, &isaMinor, 2);
    std::memcpy(entry + 26, &isaMajor, 2);
    std::memcpy(entry + 28, &sm, 4);
    std::memcpy(entry + 40, &flags, 8);
    std::memcpy(entry + 64, &extra, 4);
    std::memcpy(entry + 0x50, ptx.data(), ptx.size());
    return out;
}

bool CreateVariant(void* providerDevice, uint16_t resource, const char* entry) {
    auto& g = global();
    const auto createModule = g.createModule.load(std::memory_order_acquire);
    const auto createFunction = g.createFunction.load(std::memory_order_acquire);
    const uint32_t sm = g.targetSm.load(std::memory_order_relaxed);
    const auto source = g.networkPtx.find(resource);
    if (source == g.networkPtx.end()) {
        log::warn(kTag, std::format("network optimization unavailable: kernel {} is not installed", resource));
        return false;
    }
    if (!createModule || !createFunction) return false;
    auto& image = images[{resource, sm}];
    if (!image) {
        std::string ptx = source->second;
        if (sm != 86) {
            const size_t target = ptx.find(".target sm_86");
            if (target == std::string::npos) return false;
            ptx.replace(target, sizeof(".target sm_86") - 1, ".target sm_" + std::to_string(sm));
        }
        image = std::make_unique<std::vector<uint8_t>>(BuildFatbin(ptx, sm));
    }
    void* module = nullptr;
    const int moduleStatus = createModule(providerDevice, image->data(), static_cast<uint32_t>(image->size()), &module);
    void* function = nullptr;
    const int status = moduleStatus == 0 ? createFunction(providerDevice, module, entry, &function) : moduleStatus;
    if (status != 0 || !function) {
        log::error(kTag, std::format("network optimization disabled: kernel {} not created (status {})", resource, status));
        return false;
    }
    functions[resource] = function;
    return true;
}

// Called with the lock held, from the provider's own network creation.
void CreateVariants(void* providerDevice) {
    ready.store(false, std::memory_order_release);
    functions.clear();
    bool ok = CreateVariant(providerDevice, kPoolRule.resource, "k_pooling");
    for (const Rule& rule : kConvRules) if (ok && rule.resource) ok = CreateVariant(providerDevice, rule.resource, "k_conv_fp16_nhwc");
    for (const Rule& rule : kFusedRules) if (ok) ok = CreateVariant(providerDevice, rule.resource, "k_conv_fp16_nhwc_fused_up");
    for (const Dl2Rule& rule : kDl2Rules) if (ok && rule.resource) ok = CreateVariant(providerDevice, rule.resource, rule.entry);
    if (!ok) {
        failed = true;
        return;
    }
    device = providerDevice;
    ready.store(true, std::memory_order_release);
    log::info(kTag, std::format("network optimization ready: {} DL1/DL2 kernels for sm_{}", functions.size(),
                                global().targetSm.load(std::memory_order_relaxed)));
}

int __cdecl HookFunction(void* providerDevice, void* module, const char* name, void** function) {
    auto& g = global();
    const int status = g.createFunction.load(std::memory_order_acquire)(providerDevice, module, name, function);
    if (status != 0 || !function || !*function || !g.active.load(std::memory_order_acquire)) return status;
    const Role role = ClassifyRole(name);
    std::lock_guard lock(mutex);
    kinds[*function] = role; // handles are recycled: always take the latest meaning
    // The provider builds its network: build ours next to it, on its device.
    if (role.kind == Kind::Upscale && g.options.optimizedKernels && !failed.load() && (!ready.load() || device != providerDevice)) {
        ready.store(false, std::memory_order_release);
        CreateVariants(providerDevice);
    }
    return status;
}

struct Held {
    Entry entry{};
    std::array<uint8_t, 96> params{};
};
struct State {
    void* list = nullptr;
    bool active = false;   // inside DL1, optimization still consistent
    int conv = 0;          // NVIDIA conv rank
    int level = 0;         // decoder level
    bool haveUpscale = false, haveAdd = false;
    Held upscale, add;
    Dl2 held = Dl2::Count; // DL2 producer held for fusion (merge or central block)
    Held dl2;
};
thread_local State t_state;

template <typename T> T Field(const void* params, size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const uint8_t*>(params) + offset, sizeof(value));
    return value;
}

bool Capture(const Entry& entry, Held& held) {
    if (entry.paramBytes > held.params.size() || !entry.params) return false;
    held.entry = entry;
    std::memcpy(held.params.data(), entry.params, entry.paramBytes);
    held.entry.params = held.params.data();
    return true;
}

// Replays held launches unchanged, in their original order.
void Flush(LaunchFn original) {
    auto& s = t_state;
    if (s.haveUpscale) original(s.list, &s.upscale.entry, 1);
    if (s.haveAdd) original(s.list, &s.add.entry, 1);
    if (s.held != Dl2::Count) original(s.list, &s.dl2.entry, 1);
    s.haveUpscale = s.haveAdd = false;
    s.held = Dl2::Count;
}

void Stop(LaunchFn original) {
    Flush(original);
    t_state.active = false;
}

uint32_t Ceil(uint64_t value, uint64_t divisor) { return static_cast<uint32_t>((value + divisor - 1) / divisor); }

void Shape(const Rule& rule, uint32_t w, uint32_t h, uint32_t channels, Entry& entry) {
    entry.grid[1] = entry.grid[2] = 1;
    switch (rule.grid) {
    case Grid::W16H4: entry.grid[0] = Ceil(w, 16); entry.grid[1] = Ceil(h, 4); entry.grid[2] = rule.z; break;
    case Grid::Flat16: entry.grid[0] = Ceil(uint64_t(w) * h, 16); entry.grid[2] = rule.z; break;
    case Grid::Flat32: entry.grid[0] = Ceil(uint64_t(w) * h, 32); entry.grid[2] = rule.z; break;
    case Grid::Flat64: entry.grid[0] = Ceil(uint64_t(w) * h, 64); entry.grid[2] = rule.z; break;
    case Grid::Pool2048: entry.grid[0] = Ceil(uint64_t(w) * h * channels, 2048); break;
    default: break;
    }
    entry.block[0] = rule.block;
    entry.block[1] = entry.block[2] = 1;
    entry.sharedBytes = 0;
}

// A specialized kernel with NVIDIA's parameters; dimensions are the output's.
int LaunchVariant(LaunchFn original, void* list, const Entry& entry, const Rule& rule, uint32_t w, uint32_t h, uint32_t channels) {
    Entry variant = entry;
    variant.function = Function(rule.resource);
    Shape(rule, w, h, channels, variant);
    return original(list, &variant, 1);
}

bool Dl2Shaped(const Dl2Rule& rule, const Entry& entry) {
    if (!entry.params || entry.paramBytes != rule.paramBytes) return false;
    for (const auto& [word, value] : rule.shape)
        if (word && Field<uint32_t>(entry.params, word * 4u) != value) return false;
    return true;
}

void Dl2Shape(const Dl2Rule& rule, const void* params, Entry& entry) {
    const uint32_t h = Field<uint32_t>(params, rule.hWord * 4u), w = Field<uint32_t>(params, rule.wWord * 4u);
    if (rule.dy) {
        entry.grid[0] = Ceil(w, rule.dx);
        entry.grid[1] = Ceil(h, rule.dy);
        entry.grid[2] = rule.z;
    } else {
        entry.grid[0] = Ceil(uint64_t(w) * h, rule.dx);
        entry.grid[1] = rule.z;
        entry.grid[2] = 1;
    }
    entry.block[0] = rule.block;
    entry.block[1] = entry.block[2] = 1;
    entry.sharedBytes = 0;
}

// Held producer (merge or central block) + its conv: one fused launch. The
// producer's output is the conv's input; the fused kernel never writes it.
// Returns false, launching nothing, when the two do not chain.
bool Dl2Fuse(LaunchFn original, void* list, const Dl2Rule& rule, const Entry& conv, bool central, int& status) {
    const uint8_t* producer = t_state.dl2.params.data();
    const void* p = conv.params;
    const uint32_t ih = Field<uint32_t>(p, 44), iw = Field<uint32_t>(p, 48);
    // merge: out, 3 inputs, h, w, factor; central block: out, 6 inputs, w, h, factor.
    const size_t inputs = central ? 6 : 3;
    const uint32_t ph = Field<uint32_t>(producer, 8 + inputs * 8 + (central ? 4 : 0));
    const uint32_t pw = Field<uint32_t>(producer, 8 + inputs * 8 + (central ? 0 : 4));
    if (Field<uint64_t>(producer, 0) != Field<uint64_t>(p, 32) || ph != ih || pw != iw) return false;
    // weights, bias, the producer's inputs, the conv's output, then
    // in h, w, out h, w and the producer's blend factor.
    std::array<uint8_t, 92> params{};
    const uint32_t tail[5] = {ih, iw, Field<uint32_t>(p, 68), Field<uint32_t>(p, 72), Field<uint32_t>(producer, 16 + inputs * 8)};
    std::memcpy(params.data(), p, 16);
    std::memcpy(params.data() + 16, producer + 8, inputs * 8);
    std::memcpy(params.data() + 16 + inputs * 8, static_cast<const uint8_t*>(p) + 56, 8);
    std::memcpy(params.data() + 24 + inputs * 8, tail, sizeof(tail));
    Entry fused{};
    fused.function = Function(rule.resource);
    Dl2Shape(rule, p, fused);
    fused.params = params.data();
    fused.paramBytes = static_cast<uint32_t>(24 + inputs * 8 + sizeof(tail)); // 68 or 92
    t_state.held = Dl2::Count;
    status = original(list, &fused, 1);
    return true;
}

int Dl2Launch(LaunchFn original, void* list, const Entry& entry, Dl2 layer) {
    auto& s = t_state;
    const Dl2Rule& rule = kDl2Rules[size_t(layer)];
    if (layer == Dl2::InitialMerge || layer == Dl2::CentralBlock) {
        Flush(original);
        if (entry.params && entry.paramBytes == rule.paramBytes && Capture(entry, s.dl2)) {
            s.held = layer;
            return 0;
        }
        return original(list, &entry, 1);
    }
    const Dl2 producer = layer == Dl2::ConvPre ? Dl2::InitialMerge : layer == Dl2::B1Conv0 ? Dl2::CentralBlock : Dl2::Count;
    if (producer != Dl2::Count) {
        int status = 0;
        if (s.held == producer && Dl2Shaped(rule, entry) && Dl2Fuse(original, list, rule, entry, producer == Dl2::CentralBlock, status)) {
            if (layer == Dl2::ConvPre && optimizedDl2Runs.fetch_add(1, std::memory_order_relaxed) == 0)
                log::info(kTag, std::format("network optimization: first optimized DL2 run ({}x{})", Field<uint32_t>(entry.params, 72),
                                            Field<uint32_t>(entry.params, 68)));
            return status;
        }
        Flush(original);
        return original(list, &entry, 1);
    }
    Flush(original);
    if (!Dl2Shaped(rule, entry)) return original(list, &entry, 1);
    Entry variant = entry;
    variant.function = Function(rule.resource);
    Dl2Shape(rule, entry.params, variant);
    return original(list, &variant, 1);
}

int __cdecl HookLaunch(void* list, const void* entries, uint32_t count) {
    const auto original = global().launch.load(std::memory_order_acquire);
    auto& s = t_state;
    if (!ready.load(std::memory_order_acquire) || count != 1 || !entries || !Enabled()) {
        Stop(original);
        return original(list, entries, count);
    }
    if (s.list != list) {
        Stop(original);
        s.list = list;
    }
    const Entry& entry = *static_cast<const Entry*>(entries);
    Role role;
    {
        std::lock_guard lock(mutex);
        const auto found = kinds.find(entry.function);
        if (found != kinds.end()) role = found->second;
    }
    const Kind kind = role.kind;
    if (kind == Kind::Dl2) return Dl2Launch(original, list, entry, role.layer);
    if (s.held != Dl2::Count) Flush(original); // a held DL2 producer not followed by its conv
    if (kind == Kind::NetworkStart) {
        Stop(original);
        s.active = true;
        s.conv = s.level = 0;
        return original(list, entries, count);
    }
    if (!s.active) return original(list, entries, count);
    switch (kind) {
    case Kind::Pool:
        Flush(original);
        if (entry.paramBytes == kPoolRule.paramBytes && entry.params)
            return LaunchVariant(original, list, entry, kPoolRule, Field<uint32_t>(entry.params, 24), Field<uint32_t>(entry.params, 28),
                                 Field<uint32_t>(entry.params, 32));
        break;
    case Kind::Upscale:
        Flush(original);
        if (++s.level <= 5 && entry.paramBytes == 40 && Capture(entry, s.upscale)) {
            s.haveUpscale = true;
            return 0;
        }
        break;
    case Kind::ElementWise:
        if (s.haveUpscale && !s.haveAdd && entry.paramBytes == 52 && Capture(entry, s.add) &&
            Field<uint64_t>(s.add.params.data(), 0) == Field<uint64_t>(s.upscale.params.data(), 8)) {
            s.haveAdd = true;
            return 0;
        }
        break;
    case Kind::Conv: {
        if (s.conv >= 17 || entry.paramBytes != kConvRules[s.conv].paramBytes || !entry.params) break;
        const Rule& rule = kConvRules[s.conv++];
        const uint32_t w = Field<uint32_t>(entry.params, entry.paramBytes - 8);
        const uint32_t h = Field<uint32_t>(entry.params, entry.paramBytes - 4);
        if (rule.resource) {
            if (s.haveUpscale || s.haveAdd) break;
            return LaunchVariant(original, list, entry, rule, w, h, 0);
        }
        // The conv after a decoder add: fuse upscale + add + conv.
        if (!s.haveUpscale || !s.haveAdd || s.level < 1 || s.level > 5) break;
        const uint8_t* up = s.upscale.params.data();
        const uint8_t* add = s.add.params.data();
        const uint32_t lh = Field<uint32_t>(up, 16), lw = Field<uint32_t>(up, 20);
        const uint32_t hh = Field<uint32_t>(up, 24), hw = Field<uint32_t>(up, 28);
        const bool chained = Field<uint64_t>(entry.params, 8) == Field<uint64_t>(add, 16) && w == hw && h == hh &&
                             Field<uint32_t>(add, 24) == hw && Field<uint32_t>(add, 28) == hh && hw == 2 * lw && hh == 2 * lh;
        if (!chained) break;
        struct Params { uint64_t weights, low, skip, out; uint32_t lw, lh, hw, hh; } params{
            Field<uint64_t>(entry.params, 0), Field<uint64_t>(up, 0), Field<uint64_t>(add, 8), Field<uint64_t>(entry.params, 24), lw, lh, hw, hh};
        static_assert(sizeof(Params) == 48);
        const Rule& fusedRule = kFusedRules[s.level - 1];
        Entry fused{};
        fused.function = Function(fusedRule.resource);
        Shape(fusedRule, hw, hh, 0, fused);
        fused.params = &params;
        fused.paramBytes = sizeof(params);
        s.haveUpscale = s.haveAdd = false;
        if (optimizedRuns.fetch_add(1, std::memory_order_relaxed) == 0)
            log::info(kTag, std::format("network optimization: first optimized DL1 run (decoder level {} fused, {}x{})", s.level, hw, hh));
        return original(list, &fused, 1);
    }
    case Kind::NetworkEnd:
        Stop(original);
        return original(list, entries, count);
    default:
        break;
    }
    // Anything unexpected: replay what is held, run this launch as is, and
    // leave the rest of this network run untouched.
    Stop(original);
    return original(list, entries, count);
}

void reset() {
    std::lock_guard lock(mutex);
    kinds.clear();
    functions.clear();
    device = nullptr;
    ready = false;
    failed = false;
    optimizedRuns = 0;
    optimizedDl2Runs = 0;
}
} // namespace net

// ---------------------------------------------------------------------------
// NvAPI entry point: the provider resolves nvapi_QueryInterface through its
// own GetProcAddress import; only that slot is redirected.
// ---------------------------------------------------------------------------
template <typename T> void* wrap(std::atomic<T>& original, void* function, void* hook) {
    const auto fn = reinterpret_cast<T>(function);
    T expected = nullptr;
    return original.compare_exchange_strong(expected, fn) || expected == fn ? hook : function;
}

void* __cdecl HookQueryInterface(uint32_t id) {
    auto& g = global();
    void* function = g.realQuery.load(std::memory_order_acquire)(id);
    if (!function || !g.active.load(std::memory_order_acquire) || !g.options.optimizedKernels) return function;
    if (id == 0xAD1A677Du) return wrap(g.createModule, function, reinterpret_cast<void*>(&HookCreateModule));   // NvAPI_D3D12_CreateCuModule
    if (id == 0xE2436E22u) return wrap(g.createFunction, function, reinterpret_cast<void*>(&net::HookFunction)); // NvAPI_D3D12_CreateCuFunction
    if (id == 0x24973538u) return wrap(g.launch, function, reinterpret_cast<void*>(&net::HookLaunch));          // NvAPI_D3D12_LaunchCuKernelChain
    return function;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR name) {
    auto& g = global();
    const FARPROC function = g.realGetProc.load(std::memory_order_acquire)(module, name);
    if (!function || reinterpret_cast<uintptr_t>(name) <= 0xFFFF || std::strcmp(name, "nvapi_QueryInterface") != 0 ||
        !g.active.load(std::memory_order_acquire))
        return function;
    const auto real = reinterpret_cast<QueryInterfaceFn>(function);
    QueryInterfaceFn expected = nullptr;
    if (!g.realQuery.compare_exchange_strong(expected, real) && expected != real) return function;
    g.resolutions.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<FARPROC>(&HookQueryInterface);
}

bool readFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

// Kernel files are user-replaceable runtime data: their identity is logged,
// not enforced (2026-09-11 decision); the provider fingerprint guard and the
// per-launch shape checks still decide whether they run.
void loadKernels(Global& g) {
    g.networkPtx.clear();
    g.outputPull.clear();
    g.outputPushFine.clear();
    g.imageReplacements = false;
    if (!g.options.optimizedKernels || g.options.kernelDirectory.empty()) return;
    const std::filesystem::path dir(g.options.kernelDirectory);
    size_t bytes = 0;
    for (uint16_t id : net::resources()) {
        std::string text;
        if (readFile(dir / std::format(L"k{}.ptx", id), text)) {
            bytes += text.size();
            g.networkPtx.emplace(id, std::move(text));
        }
    }
    g.imageReplacements = readFile(dir / L"OutputPull.ptx", g.outputPull) && readFile(dir / L"OutputPushFine.ptx", g.outputPushFine);
    std::string pullHash = g.outputPull.empty() ? "-" : sha256Hex(reinterpret_cast<const uint8_t*>(g.outputPull.data()), g.outputPull.size());
    log::info(kTag, std::format("optimized kernels dir={} network={}/{} ({} bytes) imageReplacements={} OutputPull sha256={}",
                                utf8(dir), g.networkPtx.size(), net::resources().size(), bytes, g.imageReplacements, pullHash));
    if (g.networkPtx.size() != net::resources().size()) g.networkPtx.clear(); // partial sets never run
}

bool rollback(Global& g) {
    bool restored = true;
    for (auto it = g.writes.rbegin(); it != g.writes.rend(); ++it)
        restored = compat::restoreMemory(it->address, it->original.data(), it->original.size()) && restored;
    if (!restored) return false; // retain records and backing storage
    g.writes.clear();
    for (void* allocation : g.allocations) VirtualFree(allocation, 0, MEM_RELEASE);
    g.allocations.clear();
    return true;
}

} // namespace

bool DlssgTransfusion::moduleIsKnown(uint64_t size, const std::string& sha256Upper) {
    return size == kKnownModuleSize && sha256Upper == kKnownModuleSha256;
}

bool DlssgTransfusion::moduleIsKnown(const std::wstring& path) {
    FileIdentity identity;
    IdentityError error;
    return computeFileIdentity(path, identity, error) && moduleIsKnown(identity.sizeBytes, identity.sha256Upper);
}

DlssgTransfusion::Options DlssgTransfusion::defaultOptions(const std::wstring& kernelDirectory) {
    Options options;
    options.kernelDirectory = kernelDirectory;
    options.blackwellKernels = !env(L"VEYRA_DLSSG_TF_DISABLE_BLACKWELL");
    options.qualityValidWarp = !env(L"VEYRA_DLSSG_TF_DISABLE_QUALITY");
    options.optimizedKernels = !env(L"VEYRA_DLSSG_TF_DISABLE_OPTIMIZED");
    wchar_t policy[32]{};
    if (GetEnvironmentVariableW(L"VEYRA_DLSSG_TF_QUALITY_POLICY", policy, 32) > 0 && std::wstring(policy) == L"transfusion")
        options.explainedWarp = false;
    return options;
}

DlssgTransfusion::State DlssgTransfusion::apply(HMODULE module, Target target, const Options& options) {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    State state;
    state.target = target;
    if (g.module || !g.writes.empty()) {
        state.detail = L"already applied in this process; release first";
        return state;
    }
    const auto* nt = imageHeaders(module);
    if (!nt || nt->FileHeader.TimeDateStamp != kKnownTimeDateStamp || nt->OptionalHeader.SizeOfImage != kKnownSizeOfImage) {
        state.detail = L"runtime identity (TimeDateStamp/SizeOfImage) does not match DLSS-G 310.9.1";
        g.state = state;
        return state;
    }
    state.identityVerified = true;
    const uint32_t arch = target == Target::Ampere ? kAmpereArchId : kAdaArchId;
    state.targetSm = target == Target::Ampere ? 86u : 89u;
    g.options = options;
    g.targetSm = state.targetSm;
    g.exactProviderKernels = false;
    g.resolutions = g.accepted = g.rejected = g.rewritten = 0;
    net::reset();
    auto refuse = [&](std::wstring why) {
        const bool restored = rollback(g);
        state.detail = why + (restored ? L"; rolled back" : L"; ROLLBACK UNPROVEN");
        state.applied = false;
        g.module = restored ? nullptr : module;
        g.state = state;
        log::error(kTag, utf8(state.detail));
        return state;
    };
    g.module = module;
    if (target == Target::Ampere) {
        state.minimumArchitectureExports = PatchDlssgMinimumArchitecture(module, arch);
        if (state.minimumArchitectureExports != kExpectedMinimumArchitectureExports) return refuse(L"minimum architecture exports mismatch");
    }
    state.archGateSites = PatchDlssgArchGates(module, arch);
    if (state.archGateSites != kExpectedArchGateSites) return refuse(L"arch gate sites mismatch");
    state.validatorPatched = PatchValidator(module);
    if (!state.validatorPatched) return refuse(L"count/index validator not patched");
    const auto provider = PatchProvider(module, state.targetSm, options);
    state.blackwellKernels = provider.blackwell;
    state.descriptorSlots = provider.descriptors;
    state.retargetedContainers = provider.retargeted;
    if (!provider.ok) return refuse(L"provider kernel patch failed");
    loadKernels(g);
    state.networkKernelsLoaded = g.networkPtx.size();
    state.imageKernelsLoaded = g.imageReplacements;
    if (options.optimizedKernels) {
        void** slot = findImportSlot(module, "GetProcAddress");
        if (!slot || !*slot) return refuse(L"GetProcAddress import slot not found");
        GetProcAddressFn expected = nullptr;
        const auto real = reinterpret_cast<GetProcAddressFn>(*slot);
        if (!g.realGetProc.compare_exchange_strong(expected, real) && expected != real) return refuse(L"GetProcAddress import changed");
        const void* hook = reinterpret_cast<const void*>(&HookGetProcAddress);
        if (!patchBytes(slot, &hook, sizeof(hook))) return refuse(L"GetProcAddress import slot not written");
        state.nvapiHooked = true;
    }
    state.writes = g.writes.size();
    state.applied = true;
    g.active = true;
    state.detail = std::format(L"target=sm_{} blackwell={} descriptors={} retargeted={} writes={} optimized={} network={} image={}",
                               state.targetSm, state.blackwellKernels, state.descriptorSlots, state.retargetedContainers, state.writes,
                               options.optimizedKernels, state.networkKernelsLoaded, state.imageKernelsLoaded);
    g.state = state;
    log::info(kTag, utf8(state.detail));
    return state;
}

bool DlssgTransfusion::release() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    if (!g.module && g.writes.empty()) return true;
    g.active = false;
    const bool restored = rollback(g);
    log::info(kTag, std::format("release restored={} nvapiResolutions={} modules accepted={} rejected={} rewritten={} dl1Runs={} dl2Runs={}",
                                restored, g.resolutions.load(), g.accepted.load(), g.rejected.load(), g.rewritten.load(),
                                net::optimizedRuns.load(), net::optimizedDl2Runs.load()));
    if (restored) {
        g.module = nullptr;
        g.state = State{};
    }
    return restored;
}

DlssgTransfusion::State DlssgTransfusion::snapshot() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    return g.state;
}

DlssgTransfusion::Counters DlssgTransfusion::counters() {
    auto& g = global();
    Counters c;
    c.nvapiResolutions = g.resolutions.load();
    c.modulesAccepted = g.accepted.load();
    c.modulesRejected = g.rejected.load();
    c.modulesRewritten = g.rewritten.load();
    c.exactProviderKernels = g.exactProviderKernels.load();
    c.networkReady = net::ready.load();
    c.networkFailed = net::failed.load();
    c.optimizedDl1Runs = net::optimizedRuns.load();
    c.optimizedDl2Runs = net::optimizedDl2Runs.load();
    return c;
}

bool DlssgTransfusion::applied() {
    return global().active.load(std::memory_order_acquire);
}

} // namespace veyra::ngx
