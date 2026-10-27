#pragma once
#include <DirectXPackedVector.h>
#include <array>
#include <cmath>

// Bounded diagnostic readback, equivalent to HdrNativeRoundTripCases::read.
// Never called by playback. The production graph performs no CPU pixel reads.
namespace amdHdrTest {
inline bool read(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring,ID3D12Resource* tex,std::vector<float>& rgb){
    using namespace veyra;if(!tex)return false;auto d=tex->GetDesc();
    if(d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&d.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))return false;
    Status st;unsigned slot;auto* list=ring.acquireNext(slot,st);if(!list)return false;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={tex,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=tex;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=buffer.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;void* data;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(buffer->Map(0,&range,&data)))return false;
    rgb.resize(size_t(d.Width)*d.Height*3);
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<3;++c){auto* row=static_cast<uint8_t*>(data)+fp.Offset+y*fp.Footprint.RowPitch;
        rgb[(size_t(y)*d.Width+x)*3+c]=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<uint16_t*>(row)[x*4+c]):float((reinterpret_cast<uint32_t*>(row)[x]>>(10*c))&1023)/1023;
    }
    D3D12_RANGE none{};buffer->Unmap(0,&none);return true;
}

inline void run(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring,unsigned& checks,unsigned& failures){
    using namespace veyra;using Microsoft::WRL::ComPtr;
    constexpr unsigned w=128,h=32;
    const float patches[8][4]={{0,0,0,1},{.25f,.4f,.6f,1},{2.5375f,2.5375f,2.5375f,1},{12.5f,12.5f,12.5f,1},
                              {.5f,-.1f,.05f,1},{-.15f,1.25f,.05f,1},{.01f,.01f,.01f,1},{4,-.2f,.1f,1}};
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;
    td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> texture,upload;
    bool ok=SUCCEEDED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;ctx.device()->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto bd=td;bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.Format=DXGI_FORMAT_UNKNOWN;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    ok=ok&&SUCCEEDED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    void* data=nullptr;ok=ok&&SUCCEEDED(upload->Map(0,nullptr,&data));
    if(!ok){++checks;++failures;return;}
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c)
        reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(data)+fp.Offset+y*fp.Footprint.RowPitch)[x*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(patches[x/16][c]);
    upload->Unmap(0,nullptr);Status status;unsigned slot=0;auto* list=ring.acquireNext(slot,status);
    if(!list){++checks;++failures;return;}
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;to.pResource=texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={texture.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON};list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.drainQueue()){++checks;++failures;return;}
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_D3D11;frame->width=w;frame->height=h;frame->buf[0]=av_buffer_alloc(1);frame->data[0]=frame->buf[0]->data;
    pipeline::ColorDescription color;color.scRgb=true;color.pixelFormat=pipeline::SourcePixelFormat::Rgba16F;color.range=pipeline::ColorRange::Full;color.transfer=pipeline::TransferFunction::Linear;color.primaries=pipeline::ColorPrimaries::BT709;color.matrix=pipeline::YuvMatrix::BT709;
    pipeline::HardwareSurfaceInput surface{texture.Get(),0,nullptr,0,w,h};
    for(unsigned ingress:{0u,1u,2u}){
      // Native scRGB, PQ/P010 and HLG/P010 all enter the same HDR working graph.
      AVFrame* native=nullptr;
      if(ingress){
        native=av_frame_alloc();native->format=AV_PIX_FMT_P010;native->width=w;native->height=h;
        native->color_range=AVCOL_RANGE_MPEG;native->colorspace=AVCOL_SPC_BT2020_NCL;native->color_primaries=AVCOL_PRI_BT2020;
        native->color_trc=ingress==1?AVCOL_TRC_SMPTE2084:AVCOL_TRC_ARIB_STD_B67;
        if(av_frame_get_buffer(native,32)<0){av_frame_free(&native);++checks;++failures;continue;}
        const double nits[]={0,20,203,1000,50,100,.1,400};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const double light=nits[x/16];double code=0;
            if(ingress==1){const double p=std::pow(light/10000,2610.0/16384);code=std::pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);}
            else{const double scene=std::pow(light/1000,1/1.2);code=scene<=1.0/12?std::sqrt(3*scene):.17883277*std::log(12*scene-.28466892)+.55991073;}
            reinterpret_cast<uint16_t*>(native->data[0]+y*native->linesize[0])[x]=uint16_t(std::lround(64+876*code))<<6;
        }
        for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w;++x)reinterpret_cast<uint16_t*>(native->data[1]+y*native->linesize[1])[x]=512<<6;
      }
      for(bool packed:{false,true}){
        std::vector<float> baseline;
        for(unsigned layers:{0u,1u,2u})for(bool downsample:{false,true})for(bool temporal:{false,true})for(bool marker:{false,true})for(bool zeroStrength:{false,true}){
            if(!layers&&(downsample||temporal||marker))continue;
            if(zeroStrength&&(layers!=1||!marker||temporal||downsample))continue;
            SetEnvironmentVariableA("VEYRA_TEST_LMXXF_MODE",marker?"marker":"identity");
            pipeline::EnhanceGraphDesc d;d.sourceWidth=d.workWidth=w;d.sourceHeight=d.workHeight=h;
            d.rgbInput=ingress==0;d.hdrInput=d.hdrOutput=true;d.captureBitDepth=10;d.hdrOutputMode=packed?engine::HdrOutputMode::Hdr10:engine::HdrOutputMode::ScRgb;
            d.enableNr=layers>0;d.enableSr=d.enableFg=false;d.nrRuntime=engine::NrRuntime::LmxxfAmd;d.nrMotion=engine::MotionSource::Zero;
            d.runtimeAbsPath=std::filesystem::absolute("runtime/nvidia").wstring();
            d.nrWidth=downsample?w/2:w;d.nrHeight=downsample?h/2:h;
            d.nrLayersModel.resize(layers);d.nrLayersResidual.resize(layers);
            if(zeroStrength)d.nrLayersResidual[0].total=0;
            for(unsigned n=0;n<layers;++n)d.nrLayersModel[n].performance=n%2==0;
            d.nrLayersTemporal.assign(layers,temporal);d.nrLayersSizePolicy.assign(layers,pipeline::NrSizePolicy::Native);d.nrLayersExtent.assign(layers,{d.nrWidth,d.nrHeight});
            pipeline::EnhanceGraph graph(ctx,ring);bool pass=graph.initialize(d)&&graph.createViews();
            double worst=0,midChange=0,highlightError=0,blackError=0;
            for(unsigned f=0;pass&&f<4;++f){
                pipeline::EnhanceGraph::FrameOutputs out;std::vector<float> actual;
                pass=graph.process(native?native:frame,f*33.333,f==0||f==2,out,f+1,native?nullptr:&color,native?nullptr:&surface)&&read(ctx,ring,graph.videoFrameResource(out.videoSlot),actual);
                if(pass&&!layers)baseline=actual;
                pass=pass&&actual.size()==baseline.size();
                if(pass){
                    for(size_t i=0;i<actual.size();++i){pass=pass&&std::isfinite(actual[i]);worst=std::max(worst,double(std::abs(actual[i]-baseline[i])));}
                    for(unsigned c=0;c<3;++c){
                        auto sample=[&](unsigned patch){return (size_t(h/2)*w+patch*16+8)*3+c;};
                        midChange=std::max(midChange,double(std::abs(actual[sample(1)]-baseline[sample(1)])));
                        highlightError=std::max(highlightError,double(std::abs(actual[sample(3)]-baseline[sample(3)])));
                        blackError=std::max(blackError,double(std::abs(actual[sample(0)]-baseline[sample(0)])));
                    }
                    if(!marker||zeroStrength)pass=pass&&worst<.003;
                    else pass=pass&&midChange>.005&&highlightError<.003&&blackError<.003;
                }
            }
            pass=pass&&graph.metrics().nrEvaluateCount==uint64_t(layers)*4;
            ring.drainQueue();graph.shutdown();++checks;failures+=!pass;
            std::cout<<"AMD_HDR_GRAPH ingress="<<ingress<<" packed="<<packed<<" layers="<<layers<<" downsample="<<downsample<<" temporal="<<temporal<<" marker="<<marker<<" zeroStrength="<<zeroStrength<<" worst="<<worst<<" midChange="<<midChange<<" highlightError="<<highlightError<<" blackError="<<blackError<<" pass="<<pass<<std::endl;
        }
      }
      av_frame_free(&native);
    }
    SetEnvironmentVariableA("VEYRA_TEST_LMXXF_MODE","identity");av_frame_free(&frame);
}
}
