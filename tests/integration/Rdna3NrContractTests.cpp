// SPDX-License-Identifier: GPL-3.0-only
// Real D3D12 cross-process GPU-contract tests. The test worker copies the proxy:
// these results are deliberately NOT Radeon NR/model inference evidence.
#include "veyra/pipeline/Rdna3NrBackend.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/parity/RenoDxParityCodec.h"
#include <cstdio>
#include <vector>
#include <array>
#include <fstream>
#include <chrono>
#include <cmath>
using namespace veyra;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;if(!ok)++failures;printf("%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);}
void transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
double halfValue(uint16_t h){const auto exponent=(h>>10)&31;const double v=exponent?std::ldexp(1.+(h&1023)/1024.,int(exponent)-15):std::ldexp(double(h&1023),-24);return h&0x8000?-v:v;}
double halfQuantise(double value){if(value==0)return 0;const int scale=std::min(24,10-std::ilogb(std::abs(value)));return std::ldexp(std::nearbyint(std::ldexp(value,scale)),-scale);}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const auto root=std::filesystem::absolute(argv[1]),worker=std::filesystem::absolute(argv[2]);
    if(std::filesystem::exists(root))return 2;std::filesystem::create_directories(root);
    gfx::D3D12DeviceContext ctx;gfx::DeviceContextDesc cd{};cd.enableDebugLayer=true;Status st=Status::Ok;
    if(!ctx.initialize(cd,st))return 2;
    gfx::CommandSlotRing ring;if(!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 2;
    constexpr uint32_t w=160,h=90;const uint32_t pitch=(w*8+255)&~255u;
    auto input=pipeline::makeTexture(ctx.device(),w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto destination=pipeline::makeTexture(ctx.device(),w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto upload=pipeline::makeUploadBuffer(ctx.device(),uint64_t(pitch)*h);
    if(!input||!destination||!upload)return 2;
    auto inputState=D3D12_RESOURCE_STATE_COMMON;auto outputState=D3D12_RESOURCE_STATE_COMMON;
    auto fill=[&](std::array<uint16_t,4> color){
        if(!ring.drainQueue())return false;
        void* data=nullptr;if(FAILED(upload->Map(0,nullptr,&data)))return false;
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)memcpy(static_cast<char*>(data)+y*pitch+x*8,color.data(),8);upload->Unmap(0,nullptr);
        uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return false;
        transition(list,input.Get(),inputState,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,1,pitch};dst.pResource=input.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(list,input.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);inputState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return ring.submitAndSignal(slot);
    };
    auto verify=[&](std::array<uint16_t,4> color,bool raw=false,ID3D12Resource* encoded=nullptr,float strength=1){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=uint64_t(pitch)*h;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        pipeline::ComPtr<ID3D12Resource> readback;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
        uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return false;
        transition(list,destination.Get(),outputState,D3D12_RESOURCE_STATE_COPY_SOURCE);outputState=D3D12_RESOURCE_STATE_COPY_SOURCE;
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=destination.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,1,pitch};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        if(!ring.submitAndSignal(slot)||!ring.drainQueue())return false;
        // Compare against the independent double-precision CPU golden, with
        // the FP16 proxy quantisation of the actual upstream CUDA interface.
        const parity::RgbF original{halfValue(color[0]),halfValue(color[1]),halfValue(color[2])};
        const auto proxy=parity::encodeProxyLinear(original,{});
        double stored[]={halfQuantise(proxy.r),halfQuantise(proxy.g),halfQuantise(proxy.b)};
        if(encoded){
            D3D12_RESOURCE_DESC ed=d;ed.Width=256;pipeline::ComPtr<ID3D12Resource> proxyReadback;
            if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&ed,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&proxyReadback))))return false;
            uint32_t es=0;auto* el=ring.acquireNext(es,st);if(!el)return false;
            transition(el,encoded,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=encoded;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=proxyReadback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,1,1,1,256};const D3D12_BOX box{0,0,0,1,1,1};el->CopyTextureRegion(&to,0,0,0,&from,&box);
            transition(el,encoded,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
            if(!ring.submitAndSignal(es)||!ring.drainQueue())return false;
            void* ep=nullptr;if(FAILED(proxyReadback->Map(0,nullptr,&ep)))return false;
            for(unsigned c=0;c<3;++c){uint16_t value=0;memcpy(&value,static_cast<char*>(ep)+c*2,2);stored[c]=halfValue(value);}proxyReadback->Unmap(0,nullptr);
            const auto encodingError=std::max({std::abs(stored[0]-proxy.r),std::abs(stored[1]-proxy.g),std::abs(stored[2]-proxy.b)});
            printf("diagnostic encoded=(%.9f,%.9f,%.9f) FP64=(%.9f,%.9f,%.9f) maxError=%.9f\n",stored[0],stored[1],stored[2],proxy.r,proxy.g,proxy.b,encodingError);
            check(encodingError<=0.0005,"FP16 proxy encoding matches independent FP64 transfer within one FP16 ULP");
        }
        const parity::RgbF decodedProxy{parity::srgbDecode(stored[0]),parity::srgbDecode(stored[1]),parity::srgbDecode(stored[2])};
        const auto expected=raw?original:parity::parityDecode(original,decodedProxy,decodedProxy,{1,strength,1});const double golden[]={expected.r,expected.g,expected.b,1};
        void* data=nullptr;if(FAILED(readback->Map(0,nullptr,&data)))return false;double maximum=0;
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)for(unsigned c=0;c<4;++c){uint16_t value=0;memcpy(&value,static_cast<char*>(data)+y*pitch+x*8+c*2,2);maximum=std::max(maximum,std::abs(halfValue(value)-golden[c]));}
        readback->Unmap(0,nullptr);printf("diagnostic maximum error against CPU parity golden=%.9f\n",maximum);return maximum<=(raw?0:0.0005);
    };
    auto prepareDir=[&](const wchar_t* name){const auto p=root/name;std::filesystem::create_directories(p);std::filesystem::copy_file(worker,p/L"veyra_amd_nr_test_worker.exe");return p;};
    const auto smallDir=prepareDir(L"small"),originalDir=prepareDir(L"original");
    pipeline::Rdna3NrBackend smallNr,originalNr;
    check(!pipeline::Rdna3NrBackend::runtimeAvailable(root),"missing package is never advertised as NR available");
    check(!smallNr.open(root,ctx.device(),ctx.directQueue(),w,h,true),"product refuses incomplete runtime before launching");
    check(smallNr.openContractTest(smallDir,ctx.device(),ctx.directQueue(),w,h,true),"Small contract worker opens on exact host GPU LUID");
    check(originalNr.openContractTest(originalDir,ctx.device(),ctx.directQueue(),w,h,false),"Original contract worker coexists independently");
    if(!smallNr.ready()||!originalNr.ready())return 1;
    check(!smallNr.admits(w+1,h)&&smallNr.admits(w,h),"provider accepts only its selected internal extent");
    check(!smallNr.enqueue(),"enqueue without prepared source is rejected");
    const std::array<std::array<uint16_t,4>,4> colors={{{0x3400,0x3800,0x3a00,0x3c00},{0x3800,0x3000,0x3400,0x3c00},{0x3a00,0x3400,0x3800,0x3c00},{0x3000,0x3a00,0x3400,0x3c00}}};
    for(unsigned index=0;index<colors.size();++index){
        auto& nr=index%2?originalNr:smallNr;
        check(fill(colors[index]),"upload distinct current source frame (diagnostic only)");
        uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return 2;
        check(!nr.recordOutputs(list,destination.Get()),"stale result cannot be consumed before current evaluation");
        check(nr.recordInputs(list,input.Get(),index+1,index!=0,index%2?.5f:1.f),"prepare current source and reset boundary");
        check(!nr.recordInputs(list,input.Get(),index+2,false,1.f),"duplicate unretired source is rejected");
        check(ring.submitAndSignal(slot)&&nr.enqueue(),"producer -> other process GPU work -> consumer fence queued");
        list=ring.acquireNext(slot,st);if(!list)return 2;
        transition(list,destination.Get(),outputState,D3D12_RESOURCE_STATE_COPY_DEST);outputState=D3D12_RESOURCE_STATE_COPY_DEST;
        check(nr.recordOutputs(list,destination.Get()),"decode same-frame output into stable graph target");
        check(ring.submitAndSignal(slot)&&nr.retireSubmitted(),"retire only after consumer is submitted");
        check(verify(colors[index],false,nr.diagnosticProxyForTest(),index%2?.5f:1.f),"all pixels belong to this source frame and match the CPU parity golden within FP16 precision");
    }
    smallNr.close();originalNr.close();check(!smallNr.ready()&&!originalNr.ready(),"both workers and models close independently");
    check(smallNr.openContractTest(smallDir,ctx.device(),ctx.directQueue(),w,h,false),"model switch/rebuild can reopen after closing");smallNr.close();
    const auto crashDir=prepareDir(L"crash");std::ofstream(crashDir/L"contract-crash-after-ack")<<"test-only";
    check(smallNr.openContractTest(crashDir,ctx.device(),ctx.directQueue(),w,h,true),"abnormal-exit test worker opens");
    check(fill(colors[0]),"abnormal-exit source submitted");uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return 2;
    check(smallNr.recordInputs(list,input.Get(),1,true,1)&&ring.submitAndSignal(slot),"prepare one source before owned worker exits");
    (void)smallNr.enqueue();const auto start=std::chrono::steady_clock::now();
    while(smallNr.ready()&&std::chrono::steady_clock::now()-start<std::chrono::seconds(2))Sleep(10);
    check(!smallNr.ready(),"worker exit changes provider to failed, never enhancement success");
    check(ring.drainQueue(),"dead worker cannot leave the host GPU queue permanently waiting");smallNr.close();
    const auto noResultDir=prepareDir(L"no-result");std::ofstream(noResultDir/L"contract-no-result")<<"test-only";
    check(smallNr.openContractTest(noResultDir,ctx.device(),ctx.directQueue(),w,h,true),"GPU completion-guard test worker opens");
    check(fill(colors[1]),"fresh source before unvalidated-result test");list=ring.acquireNext(slot,st);if(!list)return 2;
    check(smallNr.recordInputs(list,input.Get(),1,true,1)&&ring.submitAndSignal(slot)&&smallNr.enqueue(),"queue completion alone does not certify an NR result");
    list=ring.acquireNext(slot,st);if(!list)return 2;transition(list,destination.Get(),outputState,D3D12_RESOURCE_STATE_COPY_DEST);outputState=D3D12_RESOURCE_STATE_COPY_DEST;
    check(smallNr.recordOutputs(list,destination.Get())&&ring.submitAndSignal(slot)&&smallNr.retireSubmitted(),"unvalidated result passes through the guarded decoder");
    check(verify(colors[1],true),"missing GPU result marker preserves the exact current linear source, not an old/partial result");smallNr.close();
    printf("%d checks %d failures (GPU protocol/parity only; no NR inference)\n",checks,failures);return failures?1:0;
}
