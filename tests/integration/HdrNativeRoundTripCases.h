#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "veyra/source/CaptureMediaType.h"

namespace hdrRoundTrip {
inline double pq(double nits){const double p=std::pow(std::max(nits,0.0)/10000,2610.0/16384);return std::pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);}
inline double nits(double code){const double p=std::pow(std::clamp(code,0.0,1.0),32.0/2523);return 10000*std::pow(std::max(p-3424.0/4096,0.0)/(2413.0/128-2392.0/128*p),16384.0/2610);}
inline double hlgEncode(double scene){return scene<=1.0/12?std::sqrt(3*scene):.17883277*std::log(12*scene-.28466892)+.55991073;}
inline double hlgDecode(double code){code=std::clamp(code,0.0,1.0);return code<=.5?code*code/3:(std::exp((code-.55991073)/.17883277)+.28466892)/12;}
inline bool read(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring,ID3D12Resource* tex,std::vector<float>& rgb){
    using namespace veyra;if(!tex)return false;auto d=tex->GetDesc();
    if(d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&d.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))return false;
    if(!buffer)return false;Status st;unsigned slot;auto* list=ring.acquireNext(slot,st);if(!list)return false;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={tex,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=tex;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=buffer.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;void* data;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(buffer->Map(0,&range,&data)))return false;
    rgb.resize(size_t(d.Width)*d.Height*3);
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<3;++c){auto* row=static_cast<uint8_t*>(data)+fp.Offset+y*fp.Footprint.RowPitch;
        rgb[(size_t(y)*d.Width+x)*3+c]=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<uint16_t*>(row)[x*4+c]):float((reinterpret_cast<uint32_t*>(row)[x]>>(10*c))&1023)/1023;
    }
    D3D12_RANGE none{};buffer->Unmap(0,&none);return true;
}
inline int run(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring){
    using namespace veyra;int failures=0;
    constexpr std::array<std::array<double,3>,16> patches={{{0,0,0},{.005,.005,.005},{.1,.1,.1},{1,1,1},{10,10,10},{80,80,80},{100,100,100},{203,203,203},{400,400,400},{1000,1000,1000},{4000,4000,4000},{10000,10000,10000},{1000,0,0},{0,1000,0},{0,0,1000},{400,100,20}}};
    for(bool hlg:{false,true})for(bool packed:{false,true})for(bool planar:{false,true})for(bool full:{false,true}){
        AVFrame* f=av_frame_alloc();f->width=64;f->height=16;f->format=planar?AV_PIX_FMT_YUV420P10LE:AV_PIX_FMT_P010;
        f->color_range=full?AVCOL_RANGE_JPEG:AVCOL_RANGE_MPEG;f->colorspace=AVCOL_SPC_BT2020_NCL;f->color_primaries=AVCOL_PRI_BT2020;f->color_trc=hlg?AVCOL_TRC_ARIB_STD_B67:AVCOL_TRC_SMPTE2084;
        if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return failures+1;}
        std::array<std::array<double,3>,16> expected{};
        for(unsigned i=0;i<patches.size();++i){
            const auto encode=[&](double n){return hlg?hlgEncode(n/10000):pq(n);};
            const double r=encode(patches[i][0]),g=encode(patches[i][1]),b=encode(patches[i][2]);const double y=.2627*r+.678*g+.0593*b;
            const int yy=int(std::lround((full?0:64)+(full?1023:876)*y)),u=int(std::lround(512+(full?1023:896)*(b-y)/1.8814)),v=int(std::lround(512+(full?1023:896)*(r-y)/1.4746));
            for(unsigned row=0;row<16;++row)for(unsigned x=i*4;x<i*4+4;++x){reinterpret_cast<uint16_t*>(f->data[0]+row*f->linesize[0])[x]=uint16_t(planar?yy:yy<<6);
                if(!(row%2)&&!(x%2)){auto* uv=reinterpret_cast<uint16_t*>(f->data[1]+row/2*f->linesize[1]);if(planar){uv[x/2]=uint16_t(u);reinterpret_cast<uint16_t*>(f->data[2]+row/2*f->linesize[2])[x/2]=uint16_t(v);}else{uv[x]=uint16_t(u<<6);uv[x+1]=uint16_t(v<<6);}}
            }
            const double l=(yy-(full?0.0:64.0))/(full?1023:876),cb=(u-512.0)/(full?1023:896),cr=(v-512.0)/(full?1023:896);
            const std::array<double,3> signal={l+1.4746*cr,l-.1645531268436578*cb-.5713531268436578*cr,l+1.8814*cb};
            if(hlg){
                const double r=hlgDecode(signal[0]),g=hlgDecode(signal[1]),b=hlgDecode(signal[2]);
                const double gain=1000*std::pow(.2627*r+.678*g+.0593*b,.2);
                expected[i]={r*gain,g*gain,b*gain};
            }else expected[i]={nits(signal[0]),nits(signal[1]),nits(signal[2])};
        }
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=64;gd.sourceHeight=gd.workHeight=16;
        gd.hdrInput=gd.hdrOutput=true;gd.noFeatures=gd.noNgx=true;gd.enableNr=gd.enableSr=false;gd.enableFg=packed;
        // Packing is an explicit output contract since HdrOutputMode was added.
        gd.hdrOutputMode=packed?engine::HdrOutputMode::Hdr10:engine::HdrOutputMode::ScRgb;
        pipeline::EnhanceGraph::FrameOutputs out;std::vector<float> output;
        bool ok=graph.initialize(gd)&&graph.createViews()&&graph.process(f,0,true,out,1)&&read(ctx,ring,graph.videoFrameResource(out.videoSlot),output);
        if(ok&&!planar){
            VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=f->width;vi.bmiHeader.biHeight=f->height;
            DXVA2_ExtendedFormat flags{};flags.NominalRange=full?DXVA2_NominalRange_0_255:DXVA2_NominalRange_16_235;flags.VideoTransferFunction=hlg?16:15;
            vi.dwControlFlags=flags.value|AMCONTROL_COLORINFO_PRESENT;
            AM_MEDIA_TYPE mt{};mt.majortype=MEDIATYPE_Video;mt.subtype={source::captureFourcc('P','0','1','0'),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
            mt.formattype=FORMAT_VideoInfo2;mt.pbFormat=reinterpret_cast<BYTE*>(&vi);mt.cbFormat=sizeof(vi);
            source::CaptureMediaLayout layout;AVFrame* capture=av_frame_clone(f);
            bool pass=capture&&source::captureMediaLayout(mt,layout);
            if(capture){capture->color_range=AVCOL_RANGE_UNSPECIFIED;capture->colorspace=AVCOL_SPC_UNSPECIFIED;capture->color_trc=AVCOL_TRC_UNSPECIFIED;capture->color_primaries=AVCOL_PRI_UNSPECIFIED;}
            pipeline::EnhanceGraph captureGraph(ctx,ring);pipeline::EnhanceGraph::FrameOutputs captureOut;std::vector<float> captured;
            auto captureDesc=gd;captureDesc.hdrInput=layout.color.isHdrPath();captureDesc.captureBitDepth=10;
            pass=pass&&captureGraph.initialize(captureDesc)&&captureGraph.createViews()&&captureGraph.process(capture,0,true,captureOut,1,&layout.color)&&read(ctx,ring,captureGraph.videoFrameResource(captureOut.videoSlot),captured)&&captured==output;
            std::cout<<"HDR_CAPTURE_PARTIAL_TAGS hlg="<<hlg<<" full="<<full<<" pqOutput="<<packed<<" exact="<<pass<<std::endl;
            failures+=!pass;captureOut={};ring.drainQueue();captureGraph.shutdown();av_frame_free(&capture);
        }
        if(ok&&!planar&&!packed){
            // HDR primaries extend beyond BT.709. Enabling neutral grading
            // must preserve them; exposure must multiply signed light equally.
            std::vector<float> baseline;
            bool gradeOk=captureReadFp16(ctx,ring,graph.diagnosticLinearInput(),baseline);
            for(float exposure:{0.0f,1.0f}){
                auto gradedDesc=gd;gradedDesc.color.enabled=true;gradedDesc.color.exposure=exposure;
                pipeline::EnhanceGraph gradedGraph(ctx,ring);pipeline::EnhanceGraph::FrameOutputs gradedOut;std::vector<float> graded;
                bool pass=gradeOk&&gradedGraph.initialize(gradedDesc)&&gradedGraph.createViews()&&gradedGraph.process(f,0,true,gradedOut,1)&&captureReadFp16(ctx,ring,gradedGraph.diagnosticLinearInput(),graded);
                double error=0;
                if(pass)for(size_t p=0;p<baseline.size();++p)if(p%4!=3)error=std::max(error,double(std::abs(graded[p]-baseline[p]*std::exp2(exposure))/std::max(1.0f,std::abs(baseline[p]*std::exp2(exposure)))));
                pass=pass&&error<.003;
                std::cout<<"HDR_GRADE_SIGNED hlg="<<hlg<<" full="<<full<<" exposure="<<exposure<<" relativeError="<<error<<" pass="<<pass<<std::endl;
                failures+=!pass;gradedOut={};ring.drainQueue();gradedGraph.shutdown();
            }
        }
        double relative=0,nearBlack=0,neutralSpread=0,neutralRelative=0;
        if(ok)for(unsigned i=0;i<patches.size();++i){const auto* p=output.data()+(i*4+2)*3;std::array<double,3> actual;
            if(packed)actual={nits(p[0]),nits(p[1]),nits(p[2])};
            else actual={80*(.627404*p[0]+.329283*p[1]+.043313*p[2]),80*(.069097*p[0]+.919540*p[1]+.011362*p[2]),80*(.016391*p[0]+.088013*p[1]+.895595*p[2])};
            for(unsigned c=0;c<3;++c){const double e=std::abs(actual[c]-expected[i][c]);if(expected[i][c]>=1)relative=std::max(relative,e/expected[i][c]);else nearBlack=std::max(nearBlack,e);}
            if(i<12){const double spread=*std::max_element(actual.begin(),actual.end())-*std::min_element(actual.begin(),actual.end());neutralSpread=std::max(neutralSpread,spread);neutralRelative=std::max(neutralRelative,spread/std::max(1.0,expected[i][0]));}
        }
        // FP16 has ~0.1% relative precision: a fixed 0.1-nit allowance at
        // 10000 nits would demand more precision than the working format.
        ok=ok&&relative<(packed?.012:.003)&&nearBlack<.15&&neutralRelative<.0011;
        HWND window=CreateWindowExW(0,L"STATIC",L"HDR native round trip",WS_POPUP,0,0,64,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);engine::VideoPresenter presenter;std::vector<float> presented;
        bool presentOk=window&&presenter.open(ctx,window,graph)&&presenter.present(ctx,ring,graph,out.videoSlot,false);
        auto buffer=presenter.presentedResourceForTest();presentOk=presentOk&&read(ctx,ring,buffer.Get(),presented)&&presented==output;
        buffer.Reset();ring.drainQueue();presenter.close();if(window)DestroyWindow(window);
        std::cout<<"HDR_NATIVE_ROUNDTRIP hlg="<<hlg<<" pqOutput="<<packed<<" planar="<<planar<<" full="<<full<<" relativeError="<<relative<<" nearBlackNits="<<nearBlack<<" neutralSpreadNits="<<neutralSpread<<" neutralRelative="<<neutralRelative<<" presentExact="<<presentOk<<" pass="<<(ok&&presentOk)<<std::endl;
        failures+=!(ok&&presentOk);out={};ring.drainQueue();graph.shutdown();av_frame_free(&f);
    }
    return failures;
}
}
