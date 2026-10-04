#pragma once
#include "veyra/engine/RecentGraphCache.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/ngx/NgxCoreCache.h"

namespace veyra::engine {
// One owner, on the engine dispatcher only. The entire device/ring/core/graph
// moves from idle prewarm to playback; no second device or snippet Init owner.
struct PreviewGpuSession {
    gfx::D3D12DeviceContext context;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> executionQueue;
    gfx::CommandSlotRing ring;
    ngx::NgxCoreCache core;
    RecentGraphCache recent{context};
    std::unique_ptr<pipeline::EnhanceGraph> graph=std::make_unique<pipeline::EnhanceGraph>(context,ring,&core);
    uint64_t graphBytes=0;
    std::optional<RecentGraphCache::Key> prepared;
    ~PreviewGpuSession();
    bool initialize(unsigned slots=16);
    static bool computeAllowed(const gfx::AdapterInfo&,const pipeline::EnhanceGraphDesc&);
    bool configureQueue(const pipeline::EnhanceGraphDesc&,bool allowAutomatic=true); // drained graph rebuild only
    static bool requestAllowed(const EnhancementSettings&);
    bool prepare(EnhancementSettings,unsigned width,unsigned height);
    bool adopt(const EnhancementSettings&,const pipeline::EnhanceGraphDesc&);
    void shutdown();
};
}
