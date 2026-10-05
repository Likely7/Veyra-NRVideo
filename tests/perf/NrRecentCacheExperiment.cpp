#include "veyra/engine/RecentGraphCache.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/ngx/NgxCoreCache.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <d3d12sdklayers.h>
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>

// Real cached product graphs, natural changing frames, full-image readbacks
// outside the create timing. Same executable opt-out is the fresh-graph control.
namespace {
std::string sha(const std::vector<uint8_t>& pixels){BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> digest{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    const bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0&&BCryptHashData(hash,const_cast<PUCHAR>(pixels.data()),ULONG(pixels.size()),0)>=0&&BCryptFinishHash(hash,digest.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);if(!ok)return {};std::string s;const char* hex="0123456789abcdef";for(auto c:digest){s+=hex[c>>4];s+=hex[c&15];}return s;}
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra;using Clock=std::chrono::steady_clock;
    if(argc!=3&&argc!=4)return 2;
    const std::wstring group=argc==4?argv[3]:L"nr";
    if(group!=L"nr"&&group!=L"sr"&&group!=L"srnr")return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;
    if(!media.open(open)||media.info().width!=1920||media.info().height!=1080)return 2;
    ngx::NgxCoreCache core;engine::RecentGraphCache cache(ctx);
    std::unique_ptr<pipeline::EnhanceGraph> graph;
    const bool enabled=!GetEnvironmentVariableW(L"VEYRA_TEST_DISABLE_RECENT_GRAPH_CACHE",nullptr,0);
    engine::EnhancementSettings prior;pipeline::EnhanceGraphDesc priorDesc;uint64_t graphBytes=0;
    auto memory=[&](){uint64_t budget=0,usage=0;return ctx.videoMemoryInfo(budget,usage)?usage:UINT64_MAX;};
    std::ofstream csv(out/L"cache.csv");csv<<"cycle,nr,hit,createMs,vramBefore,vramLive,graphBytes,nrEvaluations,sourceFrame,sha256\n";
    bool pass=true;unsigned hits=0;
    for(unsigned cycle=0;cycle<50&&pass;++cycle){
        const bool active=cycle%2==0,nr=active&&group!=L"sr",sr=active&&group!=L"nr";
        engine::EffectChain chain;chain.nodeCount=1;
        chain.nodes[0].type=engine::EffectType::NrEnhance;chain.nodes[0].enabled=nr;
        chain.nodes[0].nr.temporal=false;chain.nodes[0].nr.sizePolicy=pipeline::NrSizePolicy::Realtime;
        engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.revision=cycle+1;settings.multiplier=1;
        settings.sr=sr;settings.videoSrQuality=0;settings.srTarget=pipeline::SrTarget::Uhd4K;
        engine::StageRequest request;request.width=1920;request.height=1080;request.nr=nr;request.sr=sr;
        pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
        desc.enableNvofStandalone=nr;desc.enableFg=false;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        if(!ring.drainQueue())return 2;
        if(graph){
            // SR admission is experimental here; production remains single NR
            // until fresh-vs-retained hidden history is proven pixel identical.
            const bool mayRetain=group==L"nr"?engine::RecentGraphCache::eligible(priorDesc):prior.sr;
            if(enabled&&mayRetain&&engine::RecentGraphCache::effectsOff(desc)&&
                cache.retain(graph,engine::RecentGraphCache::key(prior,{},priorDesc),graphBytes)){}
            else{graph->shutdown();graph.reset();}
        }
        const auto cachedBytes=cache.bytes();bool hit=false;
        const auto before=memory();const auto start=Clock::now();
        if(enabled&&active)graph=cache.take(engine::RecentGraphCache::key(settings,{},desc));
        if(graph){hit=true;graphBytes=cachedBytes;pass=graph->applySettings(settings);++hits;}
        else{
            if(active)cache.evict("new-enhancement-key");
            graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,cache.hasCachedGraph()?nullptr:&core);
            pass=graph->initialize(desc);const auto after=memory();graphBytes=after>before?after-before:0;
        }
        pass=pass&&graph->createViews()&&ring.waitIdle();
        const auto createMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();const auto live=memory();
        // Every on/off pair uses a different natural interval. Reset remains
        // mandatory even when the feature and its textures are retained.
        if(pass&&!media.seek({int64_t(cycle<20?0:cycle/2-10)*200,1000}))pass=false;
        const auto priorMetrics=graph->metrics();sink::RgbaImage image;uint64_t sequence=0;
        for(unsigned frame=0;frame<3&&pass;++frame){
            pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;
            pass=media.read(packet,&input)==source::SourceReadStatus::Frame&&
                graph->process(input,packet.pts.toDouble()*1000,frame==0,output,packet.sequence,&packet.colorInfo)&&ring.waitIdle();
            sequence=packet.sequence;
            if(pass&&frame==2)pass=sink::readRgba8(ctx,ring,graph->videoFrameResource(output.videoSlot),image);
        }
        const auto nrDelta=graph->metrics().nrEvaluateCount-priorMetrics.nrEvaluateCount;
        const auto srDelta=graph->metrics().srEvaluateCount-priorMetrics.srEvaluateCount;
        pass=pass&&nrDelta==unsigned(nr?3:0)&&srDelta==unsigned(sr?3:0);
        const auto hash=sha(image.pixels);pass=pass&&!hash.empty();
        if(pass&&(cycle==0||cycle==1||cycle==2||cycle==18||cycle==20||cycle==30||cycle==48||cycle==49)){std::ofstream f(out/(L"frame-"+std::to_wstring(cycle)+L".rgba"),std::ios::binary);
            f.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));}
        csv<<cycle<<','<<active<<','<<hit<<','<<createMs<<','<<before<<','<<live<<','<<graphBytes<<','<<nrDelta<<','<<sequence<<','<<hash<<'\n';csv.flush();
        std::cout<<"CACHE_CYCLE i="<<cycle<<" hit="<<hit<<" createMs="<<createMs<<" pass="<<pass<<std::endl;
        prior=settings;priorDesc=desc;
    }
    ring.drainQueue();if(graph){graph->shutdown();graph.reset();}cache.evict("test-close");
    pass=core.close("test-close")&&pass;media.close();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);
        std::vector<unsigned char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,m,&size)))return 2;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();pass=pass&&errors==0&&SUCCEEDED(removed);
    std::cout<<"CACHE_RESULT pass="<<pass<<" hits="<<hits<<" debugErrors="<<errors
        <<" deviceRemoved="<<unsigned(removed)<<" afterFinalClose="<<memory()<<std::endl;
    return pass?0:1;
}
