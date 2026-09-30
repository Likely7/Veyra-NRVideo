#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace veyra::ngx {

// Process-memory DLSS-G 310.9.1 provider patches for RTX 40 (Ada) and RTX 30
// (Ampere), replacing the audited-310.7 AdaMfgUnlock/AmpereMfgUnlock pair when
// the 310.9.1 provider is installed.
//
// Ported from SilyNoMeta/DLSSG-Transfusion (MIT), tag v1.4.5.3-rtx20-30-40,
// commit b56bd2deed114507ad2c88f986d90ed50ffb4639 (a fork of
// TonyJoaca/DLSSG-Transfusion, MIT):
//   source/native/patcher.cpp       PatchDlssgArchGates, PatchDlssgMinimumArchitecture,
//                                   kNgxPatch (validator guard), PatchUniqueExecutablePattern
//   source/native/midpoint_fix.cpp  PatchProvider (Blackwell kernels, in-place retarget),
//                                   PrepareModuleImage (exact image-kernel substitution)
//   source/native/cu_module_hook.h  NvAPI_D3D12_CreateCuModule interception
//   source/native/network_optimizer.h  DL1/DL2 launch substitution
//   source/native/image_kernels.h, quality_fix.h, quality_explained_warp.h
// Veyra adaptations: every written byte is recorded and restored on release;
// NvAPI is intercepted through the provider's own GetProcAddress import slot
// (no Detours, no global hook); the optimized kernels are separate runtime
// files, not resources; Turing, Vulkan, Streamline, HUD assist and the
// quality experiments are not ported.
//
// Never applied on Blackwell: RTX 50 keeps NVIDIA's native path.
class DlssgTransfusion {
public:
    // NVIDIA DLSS SDK 310.9.1 lib/Windows_x86_64/rel/nvngx_dlssg.dll.
    static constexpr uint64_t kKnownModuleSize = 7460976ull;
    static constexpr const char* kKnownModuleSha256 =
        "FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82";
    static constexpr uint32_t kKnownTimeDateStamp = 0x6A986031u;
    static constexpr uint32_t kKnownSizeOfImage = 0x00737000u;
    static constexpr uint32_t kAdaArchId = 0x190u;
    static constexpr uint32_t kAmpereArchId = 0x170u;
    static constexpr size_t kExpectedArchGateSites = 2;
    static constexpr size_t kExpectedMinimumArchitectureExports = 4;

    enum class Target : uint8_t { Ada, Ampere };

    struct Options {
        // Transfusion defaults: Blackwell kernels, valid-warp protection with
        // the explained-warp policy, and bit-exact optimized kernels.
        bool blackwellKernels = true;
        bool qualityValidWarp = true;
        bool explainedWarp = true;
        bool optimizedKernels = true;
        // Directory holding k40xx.ptx, OutputPull.ptx and OutputPushFine.ptx.
        std::wstring kernelDirectory;
    };

    struct State {
        bool applied = false;
        bool identityVerified = false;
        Target target = Target::Ada;
        uint32_t targetSm = 0;
        size_t archGateSites = 0;
        size_t minimumArchitectureExports = 0;
        bool validatorPatched = false;
        bool blackwellKernels = false;
        size_t descriptorSlots = 0;
        size_t retargetedContainers = 0;
        bool nvapiHooked = false;
        size_t networkKernelsLoaded = 0;
        bool imageKernelsLoaded = false;
        size_t writes = 0;
        std::wstring detail;
    };

    struct Counters {
        uint32_t nvapiResolutions = 0;
        uint32_t modulesAccepted = 0;
        uint32_t modulesRejected = 0;
        uint32_t modulesRewritten = 0;
        bool exactProviderKernels = false;
        bool networkReady = false;
        bool networkFailed = false;
        uint64_t optimizedDl1Runs = 0;
        uint64_t optimizedDl2Runs = 0;
    };

    // True for the pinned 310.9.1 provider file (size + SHA-256).
    static bool moduleIsKnown(const std::wstring& path);
    static bool moduleIsKnown(uint64_t size, const std::string& sha256Upper);

    // Installs every patch into the mapped provider. Must run before NGX
    // initializes it (the NvAPI entry point and CUDA programs are resolved
    // then). Refuses, rolling back, unless each site matches its expectation.
    static State apply(HMODULE module, Target target, const Options& options);

    // Restores every patched byte and the import slot. Call after NGX shutdown.
    // Returns false when restoration cannot be proved.
    static bool release();

    static State snapshot();
    static Counters counters();
    static bool applied();

    // Options from the product defaults, overridable for A/B tests by
    // VEYRA_DLSSG_TF_{DISABLE_BLACKWELL,DISABLE_QUALITY,DISABLE_OPTIMIZED}=1
    // and VEYRA_DLSSG_TF_QUALITY_POLICY=transfusion.
    static Options defaultOptions(const std::wstring& kernelDirectory);
};

} // namespace veyra::ngx
