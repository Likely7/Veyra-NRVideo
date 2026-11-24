// SPDX-License-Identifier: GPL-3.0-only
// Product-library VFG test. Pixel readback and final CPU waits are diagnostic
// only; production uses the same GPU handoff and leased textures without them.
#include "veyra/pipeline/VfgBackend.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <set>
#include <chrono>
#include <vector>
#include <algorithm>
#include <string_view>
using namespace veyra;
void transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);
}
Microsoft::WRL::ComPtr<ID3D12Resource> makeReadback(ID3D12Device* device,uint64_t bytes){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))return {};return buffer;
}
int wmain(int argc,wchar_t** argv){
    if(argc<2){std::puts("usage: veyra_vfg_gpu_tests <runtime dir> [width height [async]]");return 2;}
    const unsigned w=argc>2?unsigned(_wtoi(argv[2])):1280,h=argc>3?unsigned(_wtoi(argv[3])):720;
    if(!w||!h||w>3840||h>2160)return 2;
    const bool asynchronous=argc>4&&std::wstring_view(argv[4])==L"async";
    unsigned checks=0,failed=0;const auto check=[&](bool b,const char* label){++checks;if(!b){++failed;std::printf("FAIL %s\n",label);}};
    gfx::D3D12DeviceContext ctx;Status st=Status::Ok;gfx::DeviceContextDesc d{};d.enableDebugLayer=true;d.commandSlotCount=6;
    if(!ctx.initialize(d,st))return 2;
    gfx::CommandSlotRing ring;if(!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 2;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;ctx.device()->QueryInterface(IID_PPV_ARGS(&debug));
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM}){
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> textures;
        for(auto& t:textures)t=pipeline::makeTexture(ctx.device(),w,h,format,false);
        auto output=pipeline::makeTexture(ctx.device(),w,h,format,false);if(!textures[0]||!textures[1]||!output)return 2;
        const auto desc=textures[0]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows=0;uint64_t rowBytes=0,total=0;
        ctx.device()->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowBytes,&total);
        const uint64_t bytes=uint64_t(fp.Footprint.RowPitch)*h;
        auto upload=pipeline::makeUploadBuffer(ctx.device(),bytes*3);auto readback=makeReadback(ctx.device(),bytes*7);if(!upload||!readback)return 2;
        void* mapped=nullptr;if(FAILED(upload->Map(0,nullptr,&mapped)))return 2;
        std::array<double,2> inputCentroid{};
        for(unsigned p=0;p<2;++p){double sum=0,weighted=0;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                const unsigned left=w/2+p*32;const bool box=x>=left&&x<left+w/10&&y>=h/3&&y<2*h/3;
                const unsigned r=box?230:16,g=unsigned(16+(x*29/w)),b=unsigned(24+(y*31/h));
                if(box){sum+=r-16;weighted+=(r-16)*x;}
                auto* pixel=static_cast<uint8_t*>(mapped)+p*bytes+y*fp.Footprint.RowPitch+x*4;
                if(format==DXGI_FORMAT_R8G8B8A8_UNORM){pixel[0]=uint8_t(r);pixel[1]=uint8_t(g);pixel[2]=uint8_t(b);pixel[3]=255;}
                else{const uint32_t packed=(r*1023/255)|((g*1023/255)<<10)|((b*1023/255)<<20)|(3u<<30);std::memcpy(pixel,&packed,4);}
            }inputCentroid[p]=weighted/sum;
        }
        // Frame 2: an unrelated bright checkerboard, i.e. a hard cut after either frame.
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const unsigned v=((x/64)+(y/64))%2?220:60;auto* pixel=static_cast<uint8_t*>(mapped)+2*bytes+y*fp.Footprint.RowPitch+x*4;
            if(format==DXGI_FORMAT_R8G8B8A8_UNORM){pixel[0]=uint8_t(v);pixel[1]=uint8_t(255-v);pixel[2]=uint8_t(v);pixel[3]=255;}
            else{const uint32_t packed=(v*1023/255)|(((255-v)*1023/255)<<10)|((v*1023/255)<<20)|(3u<<30);std::memcpy(pixel,&packed,4);}
        }
        auto cutTexture=pipeline::makeTexture(ctx.device(),w,h,format,false);if(!cutTexture)return 2;
        auto results=makeReadback(ctx.device(),256);if(!results)return 2;
        {uint32_t slot=0;auto* l=ring.acquireNext(slot,st);if(!l)return 2;
            transition(l,cutTexture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;src.PlacedFootprint.Offset=2*bytes;
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=cutTexture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(l,cutTexture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
            if(!ring.submitAndSignal(slot))return 2;}
        // Hashes of every single-model subframe: a mixed tier must reproduce them exactly.
        std::array<std::array<std::array<uint64_t,7>,9>,engine::kVfgQualityCount> modelHashes{};
        // Keep mapping for readback comparisons; upload memory is immutable.
        using CaptureMode=pipeline::VfgBackend::CaptureMode;
        for(unsigned quality=0;quality<engine::kVfgQualityCount;++quality){
            pipeline::VfgBackend vfg;
            if(!vfg.initialize(ctx,argv[1],w,h,format,8,quality,asynchronous)){std::puts("FAIL native VFG initialization");return 3;}
            for(unsigned p=0;p<2;++p){uint32_t slot=0;auto* l=ring.acquireNext(slot,st);if(!l)return 2;
                transition(l,textures[p].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;src.PlacedFootprint.Offset=p*bytes;
                D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=textures[p].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
                transition(l,textures[p].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
                check(vfg.capture(l,textures[p].Get(),p,p?CaptureMode::ComparePair:CaptureMode::ResetPair),"capture encoded GPU frame");
                transition(l,textures[p].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                if(!ring.submitAndSignal(slot))return 2;
            }
            for(unsigned multiplier=2;multiplier<=8;++multiplier){
                const auto start=std::chrono::steady_clock::now();
                for(unsigned sub=1;sub<multiplier;++sub){
                    if(!vfg.generate(1,sub,multiplier,false)){std::puts("FAIL native VFG asynchronous Run");return 4;}
                    uint32_t slot=0;auto* l=ring.acquireNext(slot,st);if(!l)return 2;
                    transition(l,output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                    check(vfg.copyOutput(l,output.Get(),1+2*(sub-1)),"consume native output on GPU");
                    transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
                    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;dst.PlacedFootprint.Offset=(sub-1)*bytes;
                    l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
                    transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                    if(!ring.submitAndSignal(slot))return 2;
                }
                check(ring.waitIdle(),"diagnostic final group completion");
                for(unsigned sub=1;sub<multiplier;++sub)check(vfg.submissionState(1+2*(sub-1))==pipeline::VfgBackend::SubmissionState::Succeeded,"SDK success before consuming each generated subframe");
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                D3D12_RANGE range{0,size_t(bytes*(multiplier-1))};void* pixels=nullptr;if(FAILED(readback->Map(0,&range,&pixels)))return 2;
                std::set<uint64_t> hashes;double previous=inputCentroid[0];
                std::array<uint64_t,7> orderedHashes{};
                std::printf("VFG format=%u size=%ux%u quality=%u multiplier=%u groupWallMs=%.3f centroids=",unsigned(format),w,h,quality,multiplier,ms);
                for(unsigned sub=1;sub<multiplier;++sub){uint64_t hash=1469598103934665603ull;double weight=0,sum=0;
                    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto* pixel=static_cast<uint8_t*>(pixels)+(sub-1)*bytes+y*fp.Footprint.RowPitch+x*4;
                        for(unsigned c=0;c<4;++c){hash^=pixel[c];hash*=1099511628211ull;}
                        unsigned red=0;if(format==DXGI_FORMAT_R8G8B8A8_UNORM)red=pixel[0];else{uint32_t packed=0;std::memcpy(&packed,pixel,4);red=(packed&1023)*255/1023;}
                        const double value=red>40?red-16:0;sum+=value;weight+=value*x;
                    }
                    const auto center=sum?weight/sum:-1;std::printf("%.3f,",center);hashes.insert(hash);
                    orderedHashes[sub-1]=hash;modelHashes[quality][multiplier][sub-1]=hash;
                    check(center>previous-1&&center>inputCentroid[0]-1&&center<inputCentroid[1]+1,"intermediate red block advances between A/B");previous=center;
                }
                std::printf(" distinct=%zu hashes=",hashes.size());
                for(unsigned sub=1;sub<multiplier;++sub)std::printf("%016llx,",static_cast<unsigned long long>(orderedHashes[sub-1]));
                std::puts("");check(hashes.size()==multiplier-1,"all generated subframes distinct");
                if(engine::vfgMixedQuality(quality))for(unsigned sub=1;sub<multiplier;++sub){
                    const unsigned model=sub==engine::vfgPrimarySubframe(multiplier)?engine::vfgPrimaryMode(quality):engine::vfgSecondaryMode(quality);
                    check(orderedHashes[sub-1]==modelHashes[model][multiplier][sub-1],"mixed tier subframe equals the single-model output of its model");
                }
                D3D12_RANGE written{0,0};readback->Unmap(0,&written);
            }
            // Manual cut must not reuse the previous input; clear ShotChange on
            // the next run, and retain both imported buffers for normal pairs.
            check(vfg.generate(1,1,2,true),"manual shot change native call");
            uint32_t slot=0;auto* l=ring.acquireNext(slot,st);if(!l)return 2;
            transition(l,output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);check(vfg.copyOutput(l,output.Get(),1),"cut output GPU copy");
            transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
            l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
            check(ring.submitAndSignal(slot)&&ring.waitIdle(),"cut output ready");
            check(vfg.submissionState(1)==pipeline::VfgBackend::SubmissionState::Succeeded,"manual cut SDK success");
            void* pixels=nullptr;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(readback->Map(0,&range,&pixels)))return 2;
            unsigned maxError=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){
                const auto a=static_cast<uint8_t*>(pixels)[y*fp.Footprint.RowPitch+x];const auto b=static_cast<uint8_t*>(mapped)[bytes+y*fp.Footprint.RowPitch+x];maxError=std::max(maxError,unsigned(std::abs(int(a)-int(b))));}
            check(maxError<=2,"manual cut equals current encoded input");std::printf("cutMaxByteError=%u\n",maxError);D3D12_RANGE written{0,0};readback->Unmap(0,&written);
            // Reuse the exact imported addresses with fresh contents, then
            // reverse parity. This catches stale-pair caches after a cut and
            // exercises reduced 2X -> full 4X -> 8X without reallocating SDK images.
            for(unsigned direction=0;direction<2;++direction){
                const unsigned parity=direction?0:1,multiplier=direction?8:4;
                for(unsigned p=0;p<2;++p){
                    auto* l=ring.acquireNext(slot,st);if(!l)return 2;
                    transition(l,textures[1-p].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
                    check(vfg.capture(l,textures[1-p].Get(),p),"refresh pixels at same imported CUDA address");
                    transition(l,textures[1-p].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                    if(!ring.submitAndSignal(slot))return 2;
                }
                for(unsigned sub=1;sub<multiplier;++sub){
                    check(vfg.generate(parity,sub,multiplier,false),"resume full group after cut/reduced group");
                    auto* l=ring.acquireNext(slot,st);if(!l)return 2;
                    transition(l,output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                    check(vfg.copyOutput(l,output.Get(),parity+2*(sub-1)),"refreshed pair output copy");
                    transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
                    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;to.PlacedFootprint.Offset=(sub-1)*bytes;
                    l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
                    transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                    if(!ring.submitAndSignal(slot))return 2;
                }
                check(ring.waitIdle(),"refreshed pair final completion");
                range={0,size_t(bytes*(multiplier-1))};if(FAILED(readback->Map(0,&range,&pixels)))return 2;
                double previous=inputCentroid[direction?0:1];std::printf("VFG_REFRESH format=%u quality=%u parity=%u multiplier=%u centroids=",unsigned(format),quality,parity,multiplier);
                for(unsigned sub=1;sub<multiplier;++sub){double sum=0,weight=0;
                    check(vfg.submissionState(parity+2*(sub-1))==pipeline::VfgBackend::SubmissionState::Succeeded,"refreshed pair SDK success");
                    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                        const auto* pixel=static_cast<uint8_t*>(pixels)+(sub-1)*bytes+y*fp.Footprint.RowPitch+x*4;
                        unsigned red=pixel[0];if(format!=DXGI_FORMAT_R8G8B8A8_UNORM){uint32_t packed=0;std::memcpy(&packed,pixel,4);red=(packed&1023)*255/1023;}
                        const double v=red>40?red-16:0;sum+=v;weight+=v*x;
                    }
                    const double center=sum?weight/sum:-1;std::printf("%.3f,",center);
                    check(center>inputCentroid[0]-1&&center<inputCentroid[1]+1&&
                        (direction?center>previous-1:center<previous+1),"new pair motion follows refreshed pixels and parity");previous=center;
                }
                std::puts("");readback->Unmap(0,&written);
            }
            // Queue fresh contents and begin at index 2, then repeat/backtrack
            // without a CPU drain. A pair-only producer dependency must still
            // protect reused output slots and input-address refreshes.
            const auto refresh=[&](bool reverse){
                for(unsigned p=0;p<2;++p){
                    auto* l=ring.acquireNext(slot,st);if(!l)return false;
                    auto* texture=textures[reverse?1-p:p].Get();
                    transition(l,texture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
                    if(!vfg.capture(l,texture,p))return false;
                    transition(l,texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                    if(!ring.submitAndSignal(slot))return false;
                }return true;
            };
            const auto consume=[&](unsigned sub,unsigned readbackSlot){
                if(!vfg.generate(1,sub,4,false))return false;
                auto* l=ring.acquireNext(slot,st);if(!l)return false;
                transition(l,output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                if(!vfg.copyOutput(l,output.Get(),1+2*(sub-1)))return false;
                transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;to.PlacedFootprint.Offset=readbackSlot*bytes;
                l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
                transition(l,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                return ring.submitAndSignal(slot);
            };
            check(refresh(false),"fresh pair before non-first subframe");
            const std::array<unsigned,5> sequence={2,3,2,1,2};
            for(unsigned i=0;i<sequence.size();++i)check(consume(sequence[i],i),"non-first/repeated/backward subframe queued");
            check(refresh(true),"input pair refreshed while earlier outputs pending");
            check(consume(3,5)&&consume(2,6),"refreshed input starts at index 3 and reuses output slot");
            check(ring.waitIdle(),"edge sequence GPU completion");
            range={0,size_t(bytes*7)};if(FAILED(readback->Map(0,&range,&pixels)))return 2;
            const auto equalPixels=[&](unsigned a,unsigned b){
                for(unsigned y=0;y<h;++y)
                    if(std::memcmp(static_cast<uint8_t*>(pixels)+a*bytes+y*fp.Footprint.RowPitch,static_cast<uint8_t*>(pixels)+b*bytes+y*fp.Footprint.RowPitch,w*4))return false;
                return true;
            };
            check(equalPixels(0,2)&&equalPixels(0,4),"repeated index preserves every pixel despite queued slot reuse");
            check(!equalPixels(0,1)&&!equalPixels(0,3),"non-first group still produces distinct intermediate frames");
            std::array<double,7> centers{};
            for(unsigned i=0;i<centers.size();++i){double sum=0,weight=0;
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                    const auto* pixel=static_cast<uint8_t*>(pixels)+i*bytes+y*fp.Footprint.RowPitch+x*4;
                    unsigned red=pixel[0];if(format!=DXGI_FORMAT_R8G8B8A8_UNORM){uint32_t packed=0;std::memcpy(&packed,pixel,4);red=(packed&1023)*255/1023;}
                    const double v=red>40?red-16:0;sum+=v;weight+=v*x;
                }centers[i]=sum?weight/sum:-1;
                check(centers[i]>inputCentroid[0]-1&&centers[i]<inputCentroid[1]+1,"edge sequence remains inside current A/B pair");
            }
            check(centers[3]<centers[0]&&centers[0]<centers[1],"non-first input follows forward motion");
            check(centers[5]<centers[6],"mid-sequence refreshed input follows reversed motion");
            std::printf("VFG_EDGE format=%u quality=%u centers=",unsigned(format),quality);for(double center:centers)std::printf("%.3f,",center);std::puts("");
            readback->Unmap(0,&written);
            check(!vfg.generate(1,8,8,false)&&!vfg.generate(2,1,2,false)&&!vfg.generate(0,1,9,false),"invalid slot/multiplier rejected");
            // Hard cut A -> checkerboard with ShotChange unset: every subframe must
            // show B (GPU pair test + predicated copy, or the SDK's own detection
            // under VEYRA_TEST_VFG_AUTO_SHOT_CHANGE=1). Then cut back, and a normal
            // pair must interpolate again (the predicate is per pair).
            const auto capturePair=[&](ID3D12Resource* a,ID3D12Resource* b,CaptureMode mode=CaptureMode::ComparePair){
                for(unsigned p=0;p<2;++p){auto* l=ring.acquireNext(slot,st);if(!l)return false;auto* texture=p?b:a;
                    transition(l,texture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
                    if(!vfg.capture(l,texture,p,p?mode:CaptureMode::HistoryOnly))return false;
                    transition(l,texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                    if(!ring.submitAndSignal(slot))return false;}
                return true;
            };
            const auto cutCount=[&](bool expectedCut)->int{
                auto* buffer=vfg.shotChangeResultsForTest();if(!buffer)return -1;
                auto* l=ring.acquireNext(slot,st);if(!l)return -2;
                transition(l,buffer,D3D12_RESOURCE_STATE_PREDICATION,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyBufferRegion(results.Get(),0,buffer,0,256);
                transition(l,buffer,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PREDICATION);
                if(!ring.submitAndSignal(slot)||!ring.waitIdle())return -2;
                void* data=nullptr;D3D12_RANGE all{0,256};if(FAILED(results->Map(0,&all,&data)))return -2;
                const auto* words=static_cast<const uint32_t*>(data);const int count=int(words[12]);
                float sad=0,histogram=0;std::memcpy(&sad,words+8,4);std::memcpy(&histogram,words+9,4);
                std::printf("VFG_SHOT_CHANGE parity1 flag=%u sad=%.4f histogram=%.4f cuts=%d\n",words[2],sad,histogram,count);
                check(words[2]==unsigned(expectedCut),"parity1 predicate matches capture mode");
                results->Unmap(0,&written);return count;
            };
            check(capturePair(textures[0].Get(),cutTexture.Get()),"capture cut pair");
            for(unsigned sub=1;sub<4;++sub)check(consume(sub,sub-1),"cut pair subframes queued");
            check(ring.waitIdle(),"cut pair completion");
            range={0,size_t(bytes*3)};if(FAILED(readback->Map(0,&range,&pixels)))return 2;
            unsigned cutError=0;for(unsigned sub=0;sub<3;++sub)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){
                const auto a=static_cast<uint8_t*>(pixels)[sub*bytes+y*fp.Footprint.RowPitch+x];const auto b=static_cast<uint8_t*>(mapped)[2*bytes+y*fp.Footprint.RowPitch+x];
                cutError=std::max(cutError,unsigned(std::abs(int(a)-int(b))));}
            readback->Unmap(0,&written);std::printf("VFG_CUT format=%u quality=%u maxByteError=%u\n",unsigned(format),quality,cutError);
            check(cutError<=2,"undeclared cut shows the current input in every subframe");
            const int afterCut=cutCount(true);
            if(afterCut!=-1){
                check(afterCut==1,"GPU pair test found exactly the one cut");
                check(capturePair(textures[0].Get(),textures[1].Get(),CaptureMode::HistoryOnly),"capture a skipped pair");
                check(cutCount(true)==afterCut,"history-only leaves the old predicate and cut count untouched");
                check(capturePair(textures[0].Get(),cutTexture.Get(),CaptureMode::ResetPair),"record a reset after a cut");
                check(cutCount(false)==afterCut,"reset clears the old predicate without counting a cut");
            }
            check(capturePair(cutTexture.Get(),textures[1].Get())&&capturePair(textures[0].Get(),textures[1].Get()),"cut back, then a normal pair");
            for(unsigned sub=1;sub<4;++sub)check(consume(sub,sub-1),"normal pair after cut queued");
            check(ring.waitIdle(),"normal pair after cut completion");
            range={0,size_t(bytes*3)};if(FAILED(readback->Map(0,&range,&pixels)))return 2;
            for(unsigned sub=0;sub<3;++sub){double sum=0,weight=0;
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                    const auto* pixel=static_cast<uint8_t*>(pixels)+sub*bytes+y*fp.Footprint.RowPitch+x*4;
                    unsigned red=pixel[0];if(format!=DXGI_FORMAT_R8G8B8A8_UNORM){uint32_t packed=0;std::memcpy(&packed,pixel,4);red=(packed&1023)*255/1023;}
                    const double v=red>40?red-16:0;sum+=v;weight+=v*x;}
                const double center=sum?weight/sum:-1;
                check(center>inputCentroid[0]+1&&center<inputCentroid[1]-1,"normal pair after a cut is interpolated again");
            }
            readback->Unmap(0,&written);
            // Pair tests that crossed a cut: into the checkerboard and out of it.
            // Every other capture (identical or shifted frames) must stay unflagged.
            const int afterReturn=cutCount(false);if(afterReturn!=-1)check(afterReturn==2,"cut back counted; normal pairs never flagged");
            vfg.shutdown();
        }
        D3D12_RANGE written{0,0};upload->Unmap(0,&written);
    }
    uint32_t removed=0;check(ctx.checkDeviceAlive(removed),"device alive after all formats/models/multipliers");
    if(debug){for(uint64_t i=0;i<debug->GetNumStoredMessages();++i){size_t size=0;debug->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());debug->GetMessage(i,message,&size);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){check(false,"D3D12 debug error");std::printf("D3D12 ERROR %s\n",message->pDescription);}}}
    std::printf("Native GPU VFG: %u checks %u failures. Hardware/cadence/film/HDR quality remain separate.\n",checks,failed);return failed?1:0;
}
