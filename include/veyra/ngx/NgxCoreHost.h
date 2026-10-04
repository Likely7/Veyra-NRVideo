#pragma once

#include <d3d12.h>
#include <nvsdk_ngx.h>

#include <cstdint>
#include <string>

#include "veyra/Result.h"

namespace veyra::ngx {

// Process-level single NGX Core host (Playbook section 7.1). One D3D12 device
// gets exactly one Init; SR/NR/FG are consumers and never call core
// Init/Shutdown themselves. Parameter blocks are allocated/destroyed through
// this host so reverse-order teardown is guaranteed.
class NgxCoreHost {
public:
    NgxCoreHost() = default;
    ~NgxCoreHost();

    NgxCoreHost(const NgxCoreHost&) = delete;
    NgxCoreHost& operator=(const NgxCoreHost&) = delete;

    // `runtimeDir` is the absolute staged runtime directory used both as the
    // feature search path (PathListInfo) and application data path. The
    // project identity strings come from runtime_local/config/ngx-local.json.
    bool initialize(ID3D12Device* device,
                    const std::wstring& runtimeDir,
                    const char* projectId,
                    const char* engineVersion,
                    Status& status);
    void shutdown();

    bool initialized() const { return initialized_; }
    uint64_t initResult() const { return initResult_; }
    uint32_t liveParameterBlockCount() const { return liveParameterBlockCount_; }

    // Parameter blocks are tracked; shutdown() destroys any still-live block
    // before core Shutdown1 (defense in depth; consumers should destroy them
    // themselves in reverse order first).
    NVSDK_NGX_Parameter* allocateParameters(Status& status);
    void destroyParameters(NVSDK_NGX_Parameter* parameters);

private:
    bool initialized_ = false;
    ID3D12Device* device_ = nullptr; // non-owning; consumer owns the device
    uint64_t initResult_ = 0;
    NVSDK_NGX_Parameter* liveParameterBlocks_[64] = {};
    uint32_t liveParameterBlockCount_ = 0;
};

} // namespace veyra::ngx
