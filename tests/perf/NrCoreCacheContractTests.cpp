#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/ngx/NgxCoreCache.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <iostream>
#include <array>

// Real GPU + SDK lifetime exercises. Intentional rejected operations below
// are caller contract tests; they do not simulate a real TDR or SDK failure.
int wmain(){
    using namespace veyra;
    Logger::instance().openFile((runtime::logsDirectory()/L"core-cache-contract.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    ngx::NgxCoreCache cache;pipeline::EnhanceGraph graph(ctx,ring,&cache);
    engine::EnhancementSettings settings;settings.nr=true;settings.multiplier=1;
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=true;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
    desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    desc.enableNvofStandalone=true;desc.enableFg=false;
    bool pass=graph.initialize(desc)&&graph.createViews();
    // A second graph/core must never be initialized through this cache while
    // a successful first graph still borrows the core.
    ngx::NgxCoreCache::Key key;key.device=ctx.device();key.runtimeDirectory=desc.runtimeAbsPath;
    const bool activeRefused=!cache.prepare(key,true);
    pass=pass&&activeRefused;std::cout<<"CONTRACT activeBorrowRefused="<<activeRefused<<std::endl;
    graph.shutdown();
    auto core=cache.borrow();pass=pass&&bool(core)&&core->liveParameterBlockCount()==0;
    std::array<NVSDK_NGX_Parameter*,64> parameters{};
    for(auto& p:parameters){if(!core){pass=false;break;}p=core->allocateParameters(status);pass=pass&&p!=nullptr;}
    auto* rejected=core?core->allocateParameters(status):nullptr;
    const bool capacityRefused=core&&rejected==nullptr&&!core->healthy();pass=pass&&capacityRefused;
    if(rejected)core->destroyParameters(rejected);
    if(core)for(auto p:parameters)if(p)core->destroyParameters(p);
    pass=pass&&core&&core->liveParameterBlockCount()==0;core.reset();
    const bool unhealthyEvicted=!cache.borrow();pass=pass&&unhealthyEvicted;
    std::cout<<"CONTRACT capacityRefused="<<capacityRefused<<" unhealthyEvicted="<<unhealthyEvicted<<std::endl;
    pass=pass&&graph.initialize(desc)&&graph.createViews();graph.shutdown();
    // Failure before initNgxFeatures/borrow must also close the previous core.
    SetEnvironmentVariableW(L"VEYRA_TEST_NR_INIT_FAILURE",L"1");
    const bool earlyFailed=!graph.initialize(desc);graph.shutdown();
    SetEnvironmentVariableW(L"VEYRA_TEST_NR_INIT_FAILURE",nullptr);
    const bool earlyEvicted=!cache.borrow();pass=pass&&earlyFailed&&earlyEvicted;
    std::cout<<"CONTRACT earlyFailure="<<earlyFailed<<" previousCoreEvicted="<<earlyEvicted<<std::endl;
    pass=pass&&graph.initialize(desc)&&graph.createViews();graph.shutdown();
    pass=cache.close("contract-final-close")&&pass;
    ring.drainQueue();const auto removed=ctx.device()->GetDeviceRemovedReason();pass=pass&&SUCCEEDED(removed);
    std::cout<<"RESULT pass="<<pass<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass?0:1;
}
