#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <algorithm>
#include <string_view>
#include <d3d12sdklayers.h>
#include <vector>
#include <sstream>
#include <set>
#include <bit>
#include <dxgi1_4.h>

// Offline, matched source-frame observations (synthetic or natural). Readbacks and CPU waits are
// confined to this executable; these timings are NOT playback performance.
namespace {
using namespace veyra;
constexpr unsigned cropWidth=640,cropHeight=360;
bool dumpCrop(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,
              ID3D12Resource* texture,std::ostream& output,
              D3D12_RESOURCE_STATES before=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE){
    if(!texture)return false;
    const auto desc=texture->GetDesc();
    const unsigned channels=desc.Format==DXGI_FORMAT_R16G16_FLOAT?2:4;
    if((desc.Format!=DXGI_FORMAT_R16G16_FLOAT&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)||
       desc.Width<cropWidth||desc.Height<cropHeight)return false;
    const unsigned rowBytes=cropWidth*channels*2;
    const unsigned pitch=(rowBytes+255)&~255u;
    const UINT64 bytes=UINT64(pitch)*cropHeight;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width=bytes;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;
    buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if(FAILED(ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
    Status status;unsigned slot;auto* list=ring.acquireNext(slot,status);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        before,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=texture;
    src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();
    dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={desc.Format,cropWidth,cropHeight,1,pitch};
    const unsigned x=(unsigned(desc.Width)-cropWidth)/2,y=(desc.Height-cropHeight)/2;
    const D3D12_BOX box{x,y,0,x+cropWidth,y+cropHeight,1};
    list->CopyTextureRegion(&dst,0,0,0,&src,&box);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    void* data=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};
    if(FAILED(readback->Map(0,&range,&data)))return false;
    for(unsigned row=0;row<cropHeight;++row)
        output.write(static_cast<const char*>(data)+size_t(row)*pitch,rowBytes);
    D3D12_RANGE empty{};readback->Unmap(0,&empty);return bool(output);
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=5&&argc!=6)return 2; // source, output, start, count, optional video-sr4k / stack2..4[-raw]
    const bool videoSr4k=argc==6&&std::wstring_view(argv[5])==L"video-sr4k";
    unsigned stackLayers=1;
    bool temporal=true;
    bool liveEdit=false;
    bool protectedStack=false;
    bool cycle=false;
    bool featherStack=false,colorProtected=false,hdrProtected=false,srFirst=false,nrFirst=false;
    bool referenceOnly=false,referenceOff=false;
    bool mixedSizes=false,exportFull=false,stillFull=false;
    if(argc==6&&!videoSr4k){
        const std::wstring_view option(argv[5]);
        if(option.size()<6||option.substr(0,5)!=L"stack"||option[5]<L'1'||option[5]>L'4')return 2;
        stackLayers=unsigned(option[5]-L'0');
        if(option.substr(6)==L"-sizes"){mixedSizes=true;temporal=false;}
        else if(option.substr(6)==L"-sizes-nr-first"){mixedSizes=true;temporal=false;nrFirst=true;}
        else if(option.substr(6)==L"-sizes-sr-first"){mixedSizes=true;temporal=false;srFirst=true;}
        else if(option.substr(6)==L"-sizes-export"){mixedSizes=true;temporal=false;exportFull=true;}
        else if(option.substr(6)==L"-sizes-still"){mixedSizes=true;temporal=false;stillFull=true;}
        else if(option.size()==10&&option.substr(6)==L"-raw")temporal=false;
        else if(option.size()==11&&option.substr(6)==L"-edit")liveEdit=true;
        else if(option.substr(6)==L"-protected")protectedStack=true;
        else if(option.substr(6)==L"-protected-raw"){protectedStack=true;temporal=false;}
        else if(option.substr(6)==L"-cycle")cycle=true;
        else if(option.substr(6)==L"-feather")featherStack=true;
        else if(option.substr(6)==L"-color-protected"){protectedStack=true;colorProtected=true;}
        else if(option.substr(6)==L"-hdr-protected"){protectedStack=true;hdrProtected=true;}
        else if(option.substr(6)==L"-sr-first")srFirst=true;
        else if(option.substr(6)==L"-nr-first")nrFirst=true;
        else if(option.substr(6)==L"-reference")referenceOnly=true;
        else if(option.substr(6)==L"-reference-off"){referenceOnly=true;referenceOff=true;}
        else if(option.size()!=6)return 2;
        if(liveEdit&&stackLayers<2)return 2;
    }
    const double start=_wtof(argv[3]);const unsigned count=unsigned(_wtoi(argv[4]));
    if(start<0||count<2||count>240)return 2;
    const std::filesystem::path directory=argv[2];std::filesystem::create_directories(directory);
    Logger::instance().openFile((directory/L"engine.log").wstring());
    Logger::instance().setConsoleEnabled(false);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource source;source::SourceOpenDesc open;
    open.path=argv[1];open.preferHardwareDecode=false;
    if(!source.open(open))return 2;
    const auto info=source.info();
    // Keep input/motion at 1080p; the optional SR case exercises a 4K working image.
    if(info.width!=1920||info.height!=1080)return 2;
    if(start>0&&!source.seek({int64_t(start*1000),1000}))return 2;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc settings;
    settings.sourceWidth=settings.workWidth=settings.nrWidth=settings.flowWidth=info.width;
    settings.sourceHeight=settings.workHeight=settings.nrHeight=settings.flowHeight=info.height;
    settings.enableNr=true;settings.nrTemporal=temporal;settings.enableFg=false;settings.enableSr=false;
    // Product path, not a hand-filled descriptor that bypasses the UI boundary.
    engine::EffectChain chain;chain.nodeCount=stackLayers;
    for(unsigned layer=0;layer<stackLayers;++layer){
        auto& node=chain.nodes[layer];node.type=engine::EffectType::NrEnhance;node.enabled=!referenceOff;
        node.nr.model.intensity=1.f-.2f*float(layer);node.nr.temporal=temporal;
        node.nr.lowLatencyPairing=nrFirst;
        if(mixedSizes){const pipeline::NrSizePolicy sizes[]={pipeline::NrSizePolicy::P480,pipeline::NrSizePolicy::P720,
            pipeline::NrSizePolicy::P1440,pipeline::NrSizePolicy::Native};node.nr.sizePolicy=sizes[layer];}
    }
    engine::EnhancementSettings pending;engine::fromChain(chain,pending);
    if(protectedStack){pending.protection.enabled=true;pending.protection.featherPixels=12;pending.protection.regions[0]={.25f,.25f,.75f,.75f};}
    if(featherStack){pending.protection.enabled=true;pending.protection.featherPixels=32;pending.protection.regions[0]={.45f,.3f,.8f,.7f};}
    if(colorProtected){pending.color.enabled=true;pending.color.exposure=.3f;pending.color.contrast=20;}
    if(referenceOnly&&!referenceOff){pending.color.enabled=true;pending.color.exposure=.3f;}
    if(srFirst||nrFirst){pending.sr=true;pending.videoSrQuality=3;pending.srTarget=pipeline::SrTarget::Uhd4K;}
    if(!engine::validateChain(chain).accepted||!pending.validate().empty())return 2;
    engine::StageRequest request;request.nr=true;request.width=info.width;request.height=info.height;
    request.sr=srFirst||nrFirst;
    request.exportJob=exportFull;request.stillImage=stillFull;
    engine::describeStages(request,pending,settings);
    if(exportFull||stillFull){
        for(const auto extent:settings.nrLayersExtent)
            if(extent!=pipeline::Extent{settings.workWidth,settings.workHeight})return 2;
        std::cout<<"fullProcessing=1 exportJob="<<exportFull<<" stillImage="<<stillFull<<std::endl;
    }
    // The HDR mode requires the caller to supply a genuinely PQ/HLG-tagged fixture.
    if(hdrProtected){settings.hdrInput=true;settings.hdrOutput=true;}
    if(request.sr&&(!settings.enableSr||settings.nrBeforeSr!=nrFirst))return 2;
    std::cout<<"mappedSr="<<settings.enableSr<<" nrBeforeSr="<<settings.nrBeforeSr<<" work="<<settings.workWidth<<'x'<<settings.workHeight<<" nr="<<settings.nrWidth<<'x'<<settings.nrHeight<<std::endl;
    settings.enableNvofStandalone=settings.enableNr;settings.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    std::set<NVSDK_NGX_Parameter*> parameterBlocks;
    std::set<ID3D12Resource*> proxies,neuralOutputs;
    bool resourceSizes=true;
    auto expectedExtents=settings.nrLayersExtent;
    size_t sizeProbeCalls=0;
    settings.nrParameterProbe=[&](NVSDK_NGX_Parameter* parameters,ID3D12Resource* proxy,ID3D12Resource* neural,uint32_t width,uint32_t height){
        parameterBlocks.insert(parameters);proxies.insert(proxy);neuralOutputs.insert(neural);
        const auto a=proxy->GetDesc(),b=neural->GetDesc();
        if(expectedExtents.empty()){resourceSizes=false;return;}
        const auto extent=expectedExtents[sizeProbeCalls++%expectedExtents.size()];
        resourceSizes=resourceSizes&&a.Width==width&&a.Height==height&&b.Width==width&&b.Height==height;
        // Evaluate order must match each active layer, not merely any requested size.
        resourceSizes=resourceSizes&&extent.width==width&&extent.height==height;
    };
    if(videoSr4k){
        settings.workWidth=3840;settings.workHeight=2160;
        settings.enableSr=true;settings.videoSrQuality=3;
    }
    if(cycle){
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||
           FAILED(factory->EnumAdapterByLuid(ctx.device()->GetAdapterLuid(),IID_PPV_ARGS(&adapter))))return 2;
        auto usage=[&](){DXGI_QUERY_VIDEO_MEMORY_INFO m{};return SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&m))?m.CurrentUsage:UINT64_MAX;};
        std::ofstream memory(directory/L"memory.csv");memory<<"cycle,layers,nrWidth,nrHeight,before,live,after\n";
        const unsigned layers[]={1,2,3,4,4,4,4,4,4};
        const unsigned widths[]={1920,1920,1920,1920,1280,960,1920,1920,1920};
        std::vector<uint64_t> released;bool pass=true;
        for(unsigned i=0;i<std::size(layers);++i){
            auto d=settings;d.nrWidth=widths[i];d.nrHeight=widths[i]*9/16;
            d.nrLayersExtent.assign(layers[i],pipeline::Extent{d.nrWidth,d.nrHeight});
            d.nrLayersModel.resize(layers[i]);d.nrLayersResidual.resize(layers[i]);
            d.nrLayersTemporal.resize(layers[i]);d.nrLayersProtection.resize(layers[i]);
            parameterBlocks.clear();proxies.clear();neuralOutputs.clear();
            expectedExtents=d.nrLayersExtent;sizeProbeCalls=0;
            const auto before=usage();
            if(!graph.initialize(d)||!graph.createViews()||!source.seek({0,1000}))return 2;
            pass=pass&&graph.nrLayerCount()==layers[i]&&graph.nrWidth()==d.nrWidth&&graph.nrHeight()==d.nrHeight;
            const auto evaluatedBefore=graph.metrics().nrEvaluateCount;
            for(unsigned j=0;j<3;++j){
                pipeline::FramePacket packet;const AVFrame* frame=nullptr;pipeline::EnhanceGraph::FrameOutputs result;
                if(source.read(packet,&frame)!=source::SourceReadStatus::Frame||
                   !graph.process(frame,packet.pts.toDouble()*1000,j==0||j==2,result,packet.sequence,&packet.colorInfo)||!ring.waitIdle())return 2;
            }
            pass=pass&&parameterBlocks.size()==layers[i]&&proxies.size()==layers[i]&&neuralOutputs.size()==layers[i]
                &&graph.metrics().nrEvaluateCount-evaluatedBefore==3*layers[i];
            std::cout<<"parameters="<<parameterBlocks.size()<<" proxies="<<proxies.size()<<" outputs="<<neuralOutputs.size()<<" evaluateDelta="<<graph.metrics().nrEvaluateCount-evaluatedBefore<<std::endl;
            const auto live=usage();ring.drainQueue();graph.shutdown();const auto after=usage();
            pass=pass&&graph.nrLayerCount()==0&&before!=UINT64_MAX&&live!=UINT64_MAX&&after!=UINT64_MAX;
            released.push_back(after);memory<<i<<','<<layers[i]<<','<<d.nrWidth<<','<<d.nrHeight<<','<<before<<','<<live<<','<<after<<'\n';
            std::cout<<"cycle="<<i<<" layers="<<layers[i]<<" nrExtent="<<d.nrWidth<<'x'<<d.nrHeight<<" before="<<before<<" live="<<live<<" released="<<after<<" structuralPass="<<pass<<std::endl;
            if(!pass)break;
        }
        source.close();
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
        if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
        for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);std::vector<unsigned char>b(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(b.data());if(FAILED(debug->GetMessage(i,m,&size)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}
        const auto tail=std::minmax_element(released.end()-std::min<size_t>(3,released.size()),released.end());
        const auto spread=released.empty()?UINT64_MAX:*tail.second-*tail.first;
        // Short-run process-local residency, not a long-run leak or hardware certification.
        const bool bounded=spread<=64ull*1024*1024;
        std::cout<<"releaseTailSpreadBytes="<<spread<<" bounded64MiB="<<bounded<<" debugErrors="<<errors<<std::endl;
        return pass&&memory&&released.size()==std::size(layers)&&bounded&&errors==0?0:1;
    }
    if(!graph.initialize(settings)||!graph.createViews())return 2;
    // Re-run the product's exact area shader on the completed upstream output.
    // Even equal extents use floating coordinates in that shader, so comparing
    // its output directly to the source texture is not a bit-exact oracle.
    std::array<pipeline::ComputePass,3> expectedPasses;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,3> expectedInputs;
    pipeline::StateTracker expectedStates;
    for(unsigned layer=1;!referenceOnly&&layer<stackLayers;++layer){
        auto& pass=expectedPasses[layer-1];
        std::vector<uint8_t> shader;
        const auto extent=graph.diagnosticNrLayerInput(layer)->GetDesc();
        expectedInputs[layer-1]=pipeline::makeTexture(ctx.device(),uint32_t(extent.Width),extent.Height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        if(!expectedInputs[layer-1]||!pass.loadShader("NrDownsample.dxil",shader)||!pass.create(ctx.device(),shader,2,1,1))return 2;
        pipeline::DescriptorStager staging;if(!staging.initialize(ctx.device(),1))return 2;
        staging.stageSrv(graph.diagnosticNrLayerOutput(layer-1),nullptr,pass.heap.Get(),0);
        pipeline::makeUav(ctx.device(),expectedInputs[layer-1].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,pipeline::cpuHandleOf(pass,1));
    }
    pipeline::ComputePass protectionOracle;
    Microsoft::WRL::ComPtr<ID3D12Resource> protectionExpected;
    if(featherStack){
        std::vector<uint8_t> shader;
        protectionExpected=pipeline::makeTexture(ctx.device(),info.width,info.height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        if(!protectionExpected||!protectionOracle.loadShader("NrResidualComposite.dxil",shader)||!protectionOracle.create(ctx.device(),shader,4,3,1,32))return 2;
        pipeline::DescriptorStager staging;if(!staging.initialize(ctx.device(),3))return 2;
        staging.stageSrv(graph.diagnosticNrBase(),nullptr,protectionOracle.heap.Get(),0);
        staging.stageSrv(graph.diagnosticNrBase(),nullptr,protectionOracle.heap.Get(),1);
        staging.stageSrv(graph.diagnosticNrLayerOutput(stackLayers-1),nullptr,protectionOracle.heap.Get(),2);
        pipeline::makeUav(ctx.device(),protectionExpected.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,pipeline::cpuHandleOf(protectionOracle,3));
    }
    std::array<std::ofstream,4> streams;
    const char* names[]={"base.rgba16f","raw.rgba16f","filtered.rgba16f","motion.rg16f"};
    for(unsigned i=0;i<4;++i)streams[i].open(directory/names[i],std::ios::binary);
    std::ofstream stackOutput;
    if(featherStack)stackOutput.open(directory/L"stack.rgba16f",std::ios::binary);
    std::ofstream metadata(directory/L"frames.csv");metadata<<"index,source,ptsMs,epoch\n";
    bool ok=true;unsigned captured=0;
    while(captured<count){
        pipeline::FramePacket packet;const AVFrame* frame=nullptr;
        if(source.read(packet,&frame)!=source::SourceReadStatus::Frame){ok=false;break;}
        if(packet.pts.toDouble()<start)continue;
        pipeline::EnhanceGraph::FrameOutputs output;
        if(liveEdit&&captured==count/2){
            chain.nodes[1].nr.model.tone=0.25f;chain.nodes[1].nr.residual.total=0.7f;
            engine::fromChain(chain,pending);++pending.revision;
            const auto firstInput=graph.diagnosticNrLayerInput(0);
            if(!graph.applySettings(pending)||graph.diagnosticNrLayerInput(0)!=firstInput){ok=false;break;}
            for(unsigned layer=0;layer<stackLayers;++layer){
                const auto actual=graph.diagnosticNrLayerSettings(layer);
                const auto& want=chain.nodes[layer].nr;
                ok=ok&&actual.model==want.model&&actual.residual==want.residual&&
                    actual.temporal==want.temporal&&graph.diagnosticNrInputRevision(layer)>1;
            }
            std::cout<<"liveSettingsApplied="<<ok<<" allLayerHistoriesInvalidated="<<ok<<std::endl;
            if(!ok)break;
        }
        if(!graph.process(frame,packet.pts.toDouble()*1000,captured==0||(!liveEdit&&(videoSr4k||stackLayers>1)&&captured==count/2),output,packet.sequence,&packet.colorInfo,nullptr,referenceOnly)||!ring.waitIdle()){ok=false;break;}
        if(referenceOnly){
            const auto& lease=output.batch.frames[output.batch.count-1].lease;
            ok=lease&&lease->referencesValid&&dumpCrop(ctx,ring,lease->sourceReference.Get(),streams[0],D3D12_RESOURCE_STATE_COMMON);
            auto* processed=referenceOff?graph.diagnosticLinearInput():graph.diagnosticNrFiltered();
            ok=dumpCrop(ctx,ring,processed,streams[2],graph.diagnosticResourceState(processed))&&ok;
            if(!ok)break;
            metadata<<captured<<','<<packet.sequence<<','<<packet.pts.toDouble()*1000<<','<<output.batch.identity.epoch<<'\n';++captured;continue;
        }
        ID3D12Resource* textures[]={graph.diagnosticNrBase(),graph.diagnosticNrRaw(),graph.diagnosticNrFiltered(),graph.flowResource()};
        for(unsigned i=0;i<4;++i)ok=dumpCrop(ctx,ring,textures[i],streams[i],graph.diagnosticResourceState(textures[i]))&&ok;
        if(featherStack){auto* texture=graph.diagnosticNrLayerOutput(stackLayers-1);ok=dumpCrop(ctx,ring,texture,stackOutput,graph.diagnosticResourceState(texture))&&ok;}
        if(featherStack){
            unsigned slot=0;auto* list=ring.acquireNext(slot,status);if(!list)return 2;
            expectedStates.transition(list,protectionExpected.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // Protect the complete stack, preserving signed residuals outside it.
            float constants[32]={1,1,1,1,1,1,32,1};
            constants[8]=.45f;constants[9]=.3f;constants[10]=.8f;constants[11]=.7f;
            protectionOracle.bind(list,constants,pipeline::gpuHandleOf(protectionOracle,0).ptr,pipeline::gpuHandleOf(protectionOracle,3).ptr);
            list->Dispatch((info.width+15)/16,(info.height+15)/16,1);
            expectedStates.uavBarrier(list,protectionExpected.Get());expectedStates.transition(list,protectionExpected.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;
            std::ostringstream expected,actual;
            const bool matched=dumpCrop(ctx,ring,protectionExpected.Get(),expected)&&dumpCrop(ctx,ring,textures[2],actual,graph.diagnosticResourceState(textures[2]))&&expected.str()==actual.str();
            std::cout<<"featherExact="<<matched<<" frame="<<captured<<std::endl;ok=ok&&matched;
        }
        if(protectedStack){
            std::ostringstream base,final;
            const bool read=dumpCrop(ctx,ring,textures[0],base,graph.diagnosticResourceState(textures[0]))
                &&dumpCrop(ctx,ring,textures[2],final,graph.diagnosticResourceState(textures[2]));
            const bool identical=read&&!base.str().empty()&&base.str()==final.str();
            std::cout<<"protectedOriginal="<<identical<<" frame="<<captured<<" bytes="<<base.str().size()<<std::endl;
            ok=ok&&identical;
        }
        // Every downstream layer must have consumed the CURRENT composed output,
        // including temporal filtering, not uninitialized/previous-frame data.
        for(unsigned layer=1;layer<stackLayers;++layer){
            unsigned slot=0;auto* list=ring.acquireNext(slot,status);if(!list){ok=false;break;}
            auto& pass=expectedPasses[layer-1];auto* expected=expectedInputs[layer-1].Get();
            expectedStates.transition(list,expected,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const auto upstreamDesc=graph.diagnosticNrLayerOutput(layer-1)->GetDesc();
            const auto extent=expected->GetDesc();
            const float dims[8]={std::bit_cast<float>(uint32_t(upstreamDesc.Width)),std::bit_cast<float>(upstreamDesc.Height),std::bit_cast<float>(uint32_t(extent.Width)),std::bit_cast<float>(extent.Height),0,0,0,0};
            pass.bind(list,dims,pipeline::gpuHandleOf(pass,0).ptr,pipeline::gpuHandleOf(pass,1).ptr);
            list->Dispatch((UINT(extent.Width)+15)/16,(extent.Height+15)/16,1);
            expectedStates.uavBarrier(list,expected);expectedStates.transition(list,expected,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if(!ring.submitAndSignal(slot)||!ring.waitIdle()){ok=false;break;}
            std::ostringstream upstream,downstream;
            const bool copied=dumpCrop(ctx,ring,expected,upstream)
                &&dumpCrop(ctx,ring,graph.diagnosticNrLayerInput(layer),downstream);
            const auto a=upstream.str(),b=downstream.str();
            size_t different=0;
            if(a.size()==b.size())for(size_t i=0;i<a.size();++i)different+=a[i]!=b[i];
            const bool matched=copied&&!a.empty()&&a.size()==b.size()&&different==0;
            std::cout<<"same-frame layer="<<layer+1<<" frame="<<captured<<" matched="<<matched
                     <<" bytes="<<a.size()<<" different="<<different<<std::endl;
            ok=ok&&matched;
        }
        if(!ok)break;
        metadata<<captured<<','<<packet.sequence<<','<<packet.pts.toDouble()*1000<<','<<output.batch.identity.epoch<<'\n';
        ++captured;
    }
    const auto metrics=graph.metrics();
    const unsigned activeLayers=referenceOff?0:stackLayers;
    const bool isolated=parameterBlocks.size()==activeLayers&&proxies.size()==activeLayers&&neuralOutputs.size()==activeLayers;
    const bool sharedFlow=metrics.nvofExecuteCount<=captured&&metrics.nvofFrameFailures==0;
    const bool allLayersRan=metrics.nrEvaluateCount==uint64_t(captured)*activeLayers;
    std::cout<<"isolated="<<isolated<<" parameters="<<parameterBlocks.size()<<" proxies="<<proxies.size()<<" neuralOutputs="<<neuralOutputs.size()
             <<" nrEvaluations="<<metrics.nrEvaluateCount<<" flowExecutions="<<metrics.nvofExecuteCount
             <<" flowFailures="<<metrics.nvofFrameFailures<<" sharedFlow="<<sharedFlow<<" allLayersRan="<<allLayersRan<<std::endl;
    std::cout<<"perLayerTextureSizes="<<resourceSizes<<std::endl;
    ok=ok&&isolated&&sharedFlow&&allLayersRan&&resourceSizes;
    if(request.sr){const bool srRan=metrics.srEvaluateCount==captured;std::cout<<"srEvaluations="<<metrics.srEvaluateCount<<" srRan="<<srRan<<std::endl;ok=ok&&srRan;}
    ring.drainQueue();graph.shutdown();source.close();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;
    unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){
        SIZE_T size=0;if(FAILED(debug->GetMessage(i,nullptr,&size)))return 2;
        std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,message,&size)))return 2;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<'\n';}
    }
    std::cout<<"captured="<<captured<<" requested="<<count<<" crop=640x360 centered; linear FP16; real NR/NVOF, offline only\n";
    std::cout<<"debugErrors="<<errors<<'\n';
    return ok&&captured==count&&metadata&&errors==0?0:1;
}
