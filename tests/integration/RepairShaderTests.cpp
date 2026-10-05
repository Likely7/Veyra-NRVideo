#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <cstring>
int main(){
    using namespace veyra;using namespace pipeline;gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status=Status::Ok;gfx::DeviceContextDesc dd;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    auto base=makeTexture(ctx.device(),16,16,DXGI_FORMAT_R32G32B32A32_FLOAT,true),low=makeTexture(ctx.device(),8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,true),result=makeTexture(ctx.device(),16,16,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    auto upload=makeUploadBuffer(ctx.device(),16*256);void* data=nullptr;upload->Map(0,nullptr,&data);auto* values=static_cast<float*>(data);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x){const size_t at=y*64+x*4;values[at]=values[at+1]=values[at+2]=((x+y)%2)?0.75f:0.25f;values[at+3]=1;}
    upload->Unmap(0,nullptr);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=16*256;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;
    if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return 2;
    ComputePass down,residual;std::vector<uint8_t> cs;
    if(!down.loadShader("NrDownsample.dxil",cs)||!down.create(ctx.device(),cs,2,1,1)||!residual.loadShader("NrResidualComposite.dxil",cs)||!residual.create(ctx.device(),cs,4,3,1,40))return 2;
    makeSrv(ctx.device(),base.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(down,0));makeUav(ctx.device(),low.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(down,1));
    makeSrv(ctx.device(),base.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(residual,0));makeSrv(ctx.device(),low.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(residual,1));makeSrv(ctx.device(),low.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(residual,2));makeUav(ctx.device(),result.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(residual,3));
    StateTracker states;uint32_t slot;auto* list=ring.acquireNext(slot,status);states.transition(list,base.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=base.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,16,16,1,256};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,base.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);states.transition(list,low.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    float dims[8]={std::bit_cast<float>(16u),std::bit_cast<float>(16u),std::bit_cast<float>(8u),std::bit_cast<float>(8u),0,0,0,0};down.bind(list,dims,gpuHandleOf(down,0).ptr,gpuHandleOf(down,1).ptr);list->Dispatch(1,1,1);states.uavBarrier(list,low.Get());states.transition(list,low.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    auto copy=[&](ID3D12Resource* texture,unsigned w,unsigned h){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=texture;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,w,h,1,256};list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
    copy(low.Get(),8,8);if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;
    D3D12_RANGE range{0,16*256};readback->Map(0,&range,&data);values=static_cast<float*>(data);bool ok=true;for(int y=0;y<8;++y)for(int x=0;x<8;++x)ok=ok&&std::abs(values[y*64+x*4]-0.5f)<1e-6f;D3D12_RANGE written{0,0};readback->Unmap(0,&written);
    std::cout<<"area downsample checkerboard mean="<<ok<<std::endl;
    for(float strength:{0.0f,1.0f,2.0f}){
        list=ring.acquireNext(slot,status);states.transition(list,low.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);states.transition(list,result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        float c[40]={strength,0.5f,1.5f,2,0,0,0,0};residual.bind(list,c,gpuHandleOf(residual,0).ptr,gpuHandleOf(residual,3).ptr);list->Dispatch(1,1,1);states.uavBarrier(list,result.Get());states.transition(list,result.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);copy(result.Get(),16,16);if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;
        readback->Map(0,&range,&data);values=static_cast<float*>(data);bool identity=true;for(int y=0;y<16;++y)for(int x=0;x<16;++x)identity=identity&&values[y*64+x*4]==(((x+y)%2)?0.75f:0.25f);readback->Unmap(0,&written);ok=ok&&identity;std::cout<<"zero residual strength="<<strength<<" base identity="<<identity<<std::endl;
    }
    auto neural=makeTexture(ctx.device(),8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    makeSrv(ctx.device(),neural.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,cpuHandleOf(residual,2));
    auto fill=[&](ID3D12Resource* texture,unsigned width,unsigned height,float r,float g,float b){
        upload->Map(0,nullptr,&data);auto* v=static_cast<float*>(data);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto at=y*64+x*4;v[at]=r;v[at+1]=g;v[at+2]=b;v[at+3]=1;}
        upload->Unmap(0,nullptr);list=ring.acquireNext(slot,status);
        states.transition(list,texture,D3D12_RESOURCE_STATE_COPY_DEST);
        dst.pResource=texture;src.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,width,height,1,256};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return ring.submitAndSignal(slot)&&ring.waitIdle();
    };
    // Known positive source and NR shadows. Doubling the old residual made
    // .10 + 2*(.04-.10) negative, destroying all of this patch's detail.
    ok=ok&&fill(base.Get(),16,16,.10f,.10f,.10f)&&fill(low.Get(),8,8,.10f,.10f,.10f)&&fill(neural.Get(),8,8,.04f,.04f,.04f);
    for(unsigned control=0;control<5;++control)for(float strength:{0.f,.5f,1.f,2.f}){
        float c[40]={1,1,1,1,1,0,0,0};c[control]=strength;
        list=ring.acquireNext(slot,status);states.transition(list,result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        residual.bind(list,c,gpuHandleOf(residual,0).ptr,gpuHandleOf(residual,3).ptr);list->Dispatch(1,1,1);
        states.uavBarrier(list,result.Get());states.transition(list,result.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);copy(result.Get(),16,16);
        if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;
        readback->Map(0,&range,&data);values=static_cast<float*>(data);
        bool valid=true;for(int y=0;y<16;++y)for(int x=0;x<16;++x)for(int channel=0;channel<3;++channel){
            const float v=values[y*64+x*4+channel];valid&=std::isfinite(v)&&v>0&&v<=.10001f;
            if(strength==1||control==2||control==3)valid&=std::abs(v-.04f)<1e-6f;
            if(control==0&&strength<=1)valid&=std::abs(v-(.10f-.06f*strength))<1e-6f;
            if((control==0||control==1||control==4)&&strength==2)valid&=v<.04f;
        }
        std::cout<<"shadow control="<<control<<" strength="<<strength<<" output="<<values[0]<<" valid="<<valid<<std::endl;
        readback->Unmap(0,&written);ok&=valid;
    }
    // The HDR working space can legitimately contain negative RGB and >1
    // highlights. Default residual settings must not clamp either boundary.
    ok=ok&&fill(base.Get(),16,16,-.1f,2.f,.5f)&&fill(low.Get(),8,8,-.1f,2.f,.5f)&&fill(neural.Get(),8,8,-.2f,3.f,.75f);
    list=ring.acquireNext(slot,status);states.transition(list,result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    float hdr[40]={1,1,1,1,1,0,0,1};residual.bind(list,hdr,gpuHandleOf(residual,0).ptr,gpuHandleOf(residual,3).ptr);list->Dispatch(1,1,1);
    states.uavBarrier(list,result.Get());states.transition(list,result.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);copy(result.Get(),16,16);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 2;
    readback->Map(0,&range,&data);values=static_cast<float*>(data);
    bool hdrIdentity=std::abs(values[0]+.2f)<1e-6f&&std::abs(values[1]-3.f)<1e-6f&&std::abs(values[2]-.75f)<1e-6f;
    readback->Unmap(0,&written);ok&=hdrIdentity;std::cout<<"HDR signed endpoint preserved="<<hdrIdentity<<std::endl;
    ring.drainQueue();return ok?0:1;
}
