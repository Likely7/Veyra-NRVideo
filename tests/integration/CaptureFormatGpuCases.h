#pragma once
#include "veyra/source/CaptureMediaType.h"
#include <DirectXPackedVector.h>
#include <iostream>

// Test-only readback of the real FP16 output, so a hidden 8-bit conversion
// cannot pass merely because black/white screenshots look correct.
inline bool captureReadFp16(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring,ID3D12Resource* texture,std::vector<float>& pixels){
    using namespace veyra;auto desc=texture->GetDesc();if(desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;ctx.device()->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=size;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> read;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))))return false;
    Status st;uint32_t slot;auto* list=ring.acquireNext(slot,st);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=texture;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=texture;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=read.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;list->CopyTextureRegion(&b,0,0,0,&a,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;void* ptr=nullptr;D3D12_RANGE range{0,size};if(FAILED(read->Map(0,&range,&ptr)))return false;
    pixels.resize(size_t(desc.Width)*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y){const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(ptr)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);for(size_t x=0;x<desc.Width*4;++x)pixels[(size_t(y)*desc.Width*4)+x]=DirectX::PackedVector::XMConvertHalfToFloat(row[x]);}
    D3D12_RANGE written{};read->Unmap(0,&written);return true;
}
inline int captureGpuCases(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring){
    using namespace veyra;using namespace veyra::source;int failures=0;
    auto fourcc=[](const char* n){return GUID{captureFourcc(n[0],n[1],n[2],n[3]),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};};
    const GUID ids[]={MEDIASUBTYPE_RGB24,MEDIASUBTYPE_RGB32,MEDIASUBTYPE_ARGB32,MEDIASUBTYPE_RGB555,MEDIASUBTYPE_RGB565,fourcc("YUY2"),fourcc("UYVY"),fourcc("HDYC"),fourcc("YVYU"),fourcc("NV12"),fourcc("NV21"),fourcc("I420"),fourcc("IYUV"),fourcc("YV12"),fourcc("P010"),fourcc("P016")};
    for(const auto& id:ids)for(bool full:{false,true}){
        VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=64;vi.bmiHeader.biHeight=32;vi.AvgTimePerFrame=166667;
        DXVA2_ExtendedFormat ext{};ext.NominalRange=full?DXVA2_NominalRange_0_255:DXVA2_NominalRange_16_235;ext.VideoTransferMatrix=DXVA2_VideoTransferMatrix_BT709;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;
        AM_MEDIA_TYPE t{};t.majortype=MEDIATYPE_Video;t.subtype=id;t.formattype=FORMAT_VideoInfo2;t.pbFormat=reinterpret_cast<BYTE*>(&vi);t.cbFormat=sizeof(vi);CaptureMediaLayout l;
        if(!captureMediaLayout(t,l)){++failures;continue;}
        const bool rgbDib=l.format==AV_PIX_FMT_BGR0||l.format==AV_PIX_FMT_BGR24||l.format==AV_PIX_FMT_RGB555LE||l.format==AV_PIX_FMT_RGB565LE,wide=l.format==AV_PIX_FMT_P010||l.format==AV_PIX_FMT_P016;const unsigned bits=l.format==AV_PIX_FMT_P016?16:wide?10:8;
        // RGB bitfield tests use full range (unit tests cover channel masks).
        if(rgbDib){l.color.range=pipeline::ColorRange::Full;}
        std::vector<uint8_t> raw(l.sampleBytes,0);const unsigned max=bits==16?65535:bits==10?1023:255,black=full?0:16*(1u<<(bits-8)),white=full?max:235*(1u<<(bits-8));
        for(unsigned y=0;y<32;++y){auto* row=raw.data()+y*l.stride;
            for(unsigned x=0;x<64;++x){
                const unsigned code=x<16?black:x>=48?white:(wide?max/4+(x-16):black+(white-black)/2);
                if(l.format==AV_PIX_FMT_BGR0){for(unsigned b=0;b<3;++b)row[x*4+b]=x<32?0:255;row[x*4+3]=0;}
                else if(l.format==AV_PIX_FMT_BGR24){row[x*3]=row[x*3+1]=row[x*3+2]=x<32?0:255;}
                else if(l.format==AV_PIX_FMT_RGB555LE){const uint16_t v=x<32?0:0x7FFF;memcpy(row+x*2,&v,2);}
                else if(l.format==AV_PIX_FMT_RGB565LE){const uint16_t v=x<32?0:0xFFFF;memcpy(row+x*2,&v,2);}
                else if(l.format==AV_PIX_FMT_YUYV422||l.format==AV_PIX_FMT_UYVY422||l.format==AV_PIX_FMT_YVYU422){row[x*2]=uint8_t(code);row[x*2+1]=128;}
                else if(wide){const uint16_t v=uint16_t(bits==10?code<<6:code);memcpy(row+x*2,&v,2);}
                else row[x]=uint8_t(code);
            }
            if(l.packing==CapturePacking::Uyvy)for(unsigned x=0;x<128;x+=2)std::swap(row[x],row[x+1]);
        }
        if(l.planes>1)for(size_t i=size_t(l.stride)*32;i<raw.size();i+=wide?2:1){if(wide){raw[i]=0;raw[i+1]=128;}else raw[i]=128;}
        AVFrame* f=av_frame_alloc();f->width=64;f->height=32;f->format=l.format;bool ok=av_frame_get_buffer(f,32)>=0&&copyCaptureSample(l,raw.data(),raw.size(),*f);
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=64;gd.sourceHeight=gd.workHeight=32;gd.enableNr=gd.enableFg=gd.enableSr=false;gd.noFeatures=true;gd.rgbInput=l.format==AV_PIX_FMT_BGR0;gd.yuy2Input=l.format==AV_PIX_FMT_YUYV422;gd.packedInput=pipeline::packedInputCode(l.color.pixelFormat);gd.captureBitDepth=bits;
        pipeline::EnhanceGraph::FrameOutputs out;std::vector<float> pixels;
        ok=ok&&graph.initialize(gd)&&graph.createViews()&&graph.process(f,0,true,out,1,&l.color)&&captureReadFp16(ctx,ring,graph.diagnosticLinearInput(),pixels);
        double error=0;if(ok){for(unsigned c=0;c<3;++c){error=std::max(error,std::abs(double(pixels[4*4+c])));error=std::max(error,std::abs(double(pixels[56*4+c])-1));}ok=error<.003;}
        bool precision=true;if(ok&&wide){
            // 10-bit: single-code steps must remain distinct. P016 is retained
            // through ingress; FP16 working storage can quantize 16-bit steps.
            const unsigned step=bits==10?1:8;
            for(unsigned x=19;x+step<45;x+=step)precision&=pixels[(x+step)*4]>pixels[x*4];
            ok&=precision;
        }
        std::cout<<"CAPTURE_GPU subtype="<<std::hex<<id.Data1<<std::dec<<" bits="<<bits<<" full="<<full<<" error="<<error<<" precision="<<precision<<" pass="<<ok<<std::endl;
        if(!ok)++failures;out={};ring.drainQueue();graph.shutdown();av_frame_free(&f);
    }
    // Blackmagic HDYC: colored padded samples at an SD-sized extent must
    // match UYVY with explicit BT.709, including the manual row flip. Gray
    // ramps alone cannot expose an accidental BT.601 matrix fallback.
    for(bool flip:{false,true}){
        std::vector<float> reference;bool pass=true;double colorSpread=0;
        for(bool hdyc:{false,true}){
            VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=64;vi.bmiHeader.biHeight=32;vi.bmiHeader.biSizeImage=(128+16)*32;vi.AvgTimePerFrame=333667;
            if(!hdyc){DXVA2_ExtendedFormat ext{};ext.NominalRange=DXVA2_NominalRange_16_235;ext.VideoTransferMatrix=DXVA2_VideoTransferMatrix_BT709;ext.VideoPrimaries=DXVA2_VideoPrimaries_BT709;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;}
            AM_MEDIA_TYPE type{};type.majortype=MEDIATYPE_Video;type.subtype=fourcc(hdyc?"HDYC":"UYVY");type.formattype=FORMAT_VideoInfo2;type.cbFormat=sizeof(vi);type.pbFormat=reinterpret_cast<BYTE*>(&vi);
            CaptureMediaLayout l;bool ok=captureMediaLayout(type,l);std::vector<uint8_t> raw(l.sampleBytes,0xee);
            for(unsigned y=0;y<32&&ok;++y)for(unsigned x=0;x<64;x+=2){auto* p=raw.data()+size_t(y)*l.stride+x*2;p[0]=y<16?90:150;p[1]=x<32?40:200;p[2]=y<16?240:34;p[3]=x<32?80:160;}
            AVFrame* f=av_frame_alloc();f->width=64;f->height=32;f->format=l.format;
            ok=ok&&av_frame_get_buffer(f,32)>=0&&copyCaptureSample(l,raw.data(),raw.size(),*f,flip);
            pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=64;gd.sourceHeight=gd.workHeight=32;gd.enableNr=gd.enableFg=gd.enableSr=false;gd.noFeatures=true;gd.packedInput=pipeline::packedInputCode(l.color.pixelFormat);
            pipeline::EnhanceGraph::FrameOutputs out;std::vector<float> pixels;
            ok=ok&&graph.initialize(gd)&&graph.createViews()&&graph.process(f,0,true,out,1,&l.color)&&captureReadFp16(ctx,ring,graph.diagnosticLinearInput(),pixels);
            if(ok){
                for(size_t p=0;p<pixels.size();p+=4)colorSpread=std::max(colorSpread,std::abs(double(pixels[p])-pixels[p+2]));
                if(!hdyc)reference=pixels;else ok=pixels==reference&&colorSpread>.2;
            }
            pass&=ok;out={};ring.drainQueue();graph.shutdown();av_frame_free(&f);
        }
        std::cout<<"BLACKMAGIC_HDYC_GPU flip="<<flip<<" identical709="<<pass<<" colorSpread="<<colorSpread<<" pass="<<pass<<std::endl;
        if(!pass)++failures;
    }
    return failures;
}
