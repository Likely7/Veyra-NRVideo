#pragma once
#include "CaptureFormatGpuCases.h"
#include "veyra/pipeline/CaptureUploadFrame.h"
#include <array>
#include <cmath>

// Independent double-precision color reference and test-only GPU readback.
// 64-pixel v210 rows end inside a six-pixel group; padding must stay invisible.
inline int decklinkGpuCases(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring){
    using namespace veyra;using namespace pipeline;using namespace source;
    int failures=0;
    for(unsigned code:{6u,7u})for(bool full:{false,true})for(unsigned curve:{1u,4u,5u})for(bool upload:{false,true}){
        constexpr unsigned w=64,h=32;const unsigned rowBytes=code==6?((w+5)/6)*16:w*4,stride=rowBytes+128;
        std::vector<uint8_t> raw(size_t(stride)*h,0xab);std::vector<std::array<double,3>> expected(w*h);
        ColorDescription color;color.pixelFormat=code==6?SourcePixelFormat::V210:SourcePixelFormat::Rgb10xLE;
        color.range=full?ColorRange::Full:ColorRange::Limited;color.matrix=curve==1?YuvMatrix::BT709:YuvMatrix::BT2020NCL;
        color.primaries=curve==1?ColorPrimaries::BT709:ColorPrimaries::BT2020;color.transfer=curve==1?TransferFunction::SRGB:curve==4?TransferFunction::PQ:TransferFunction::HLG;
        auto component=[&](unsigned x,unsigned y,unsigned channel){
            if(channel==0)return unsigned((full?0:64)+((x*11+y*17)%(full?1024:877)));
            return unsigned(channel==1?384+(x/2%4)*64:416+(y%4)*48);
        };
        auto decode=[&](double v){
            if(curve==1)return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);
            if(curve==4){const auto p=std::pow(v,32.0/2523);return 10000*std::pow(std::max(p-3424.0/4096,0.0)/(2413.0/128-2392.0/128*p),16384.0/2610);}
            return v<=.5?v*v/3:(std::exp((v-.55991073)/.17883277)+.28466892)/12;
        };
        for(unsigned y=0;y<h;++y){
            auto* row=raw.data()+size_t(y)*stride;
            if(code==6)for(unsigned x=0;x<w;x+=6){
                unsigned yy[6],u[3],v[3];for(unsigned i=0;i<6;++i)yy[i]=component(x+i,y,0);
                for(unsigned i=0;i<3;++i){u[i]=component(x+2*i,y,1);v[i]=component(x+2*i,y,2);}
                const uint32_t words[]={u[0]|yy[0]<<10|v[0]<<20,yy[1]|u[1]<<10|yy[2]<<20,v[1]|yy[3]<<10|u[2]<<20,yy[4]|v[2]<<10|yy[5]<<20};
                memcpy(row+(x/6)*16,words,16);
            }else for(unsigned x=0;x<w;++x){
                const uint32_t r=component(x,y,0),g=component((x+19)%w,y,0),b=component((x+37)%w,y,0);
                const uint32_t word=r<<22|g<<12|b<<2|3u;memcpy(row+x*4,&word,4); // X bits deliberately nonzero
            }
            for(unsigned x=0;x<w;++x){
                std::array<double,3> c;
                if(code==6){
                    const double yy=(double(component(x,y,0))-(full?0:64))/(full?1023:876);
                    const unsigned next=(x%2&&x+1<w)?x+1:x;
                    const double u=(.5*(component(x,y,1)+component(next,y,1))-512)/(full?1023:896);
                    const double v=(.5*(component(x,y,2)+component(next,y,2))-512)/(full?1023:896);
                    c=curve==1?std::array{yy+1.5748*v,yy-.187324*u-.468124*v,yy+1.8556*u}:std::array{yy+1.4746*v,yy-.164553*u-.571353*v,yy+1.8814*u};
                }else{for(unsigned i=0;i<3;++i)c[i]=(double(component((x+(i==1?19:i==2?37:0))%w,y,0))-(full?0:64))/(full?1023:876);}
                for(auto& v:c)v=decode(std::clamp(v,0.0,1.0));
                if(curve==5){const auto light=.2627*c[0]+.6780*c[1]+.0593*c[2];for(auto& v:c)v*=1000*std::pow(light,.2);}
                if(curve!=1){const auto old=c;c={1.660491*old[0]-.587641*old[1]-.072850*old[2],-.124550*old[0]+1.132900*old[1]-.008349*old[2],-.018151*old[0]-.100579*old[1]+1.118730*old[2]};for(auto& v:c)v/=80;}
                expected[y*w+x]=c;
            }
        }
        CaptureMediaLayout layout;layout.width=w;layout.height=h;layout.stride=stride;layout.rowBytes=rowBytes;layout.sampleBytes=raw.size();layout.format=AV_PIX_FMT_BGRA;layout.packing=code==6?CapturePacking::V210:CapturePacking::Rgb10xLE;layout.color=color;
        AVFrame* frame=upload?allocCaptureUploadFrame(ctx.device(),AV_PIX_FMT_BGRA,w,h,true):av_frame_alloc();
        bool ok=frame!=nullptr;if(ok&&!upload){frame->width=w;frame->height=h;frame->format=AV_PIX_FMT_BGRA;ok=av_frame_get_buffer(frame,256)>=0;}
        if(ok){selectCaptureUploadTarget(*frame);ok=copyCaptureSample(layout,raw.data(),raw.size(),*frame);if(code==6)sampleCaptureV210Proxy(*frame,raw.data(),stride,false);else sampleCaptureUploadProxy(*frame,raw.data(),stride,false);}
        EnhanceGraph graph(ctx,ring);EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=w;gd.sourceHeight=gd.workHeight=h;gd.enableNr=gd.enableFg=gd.enableSr=false;gd.noFeatures=true;gd.packedInput=code;gd.captureBitDepth=10;gd.hdrInput=curve!=1;gd.hdrOutput=curve!=1;
        EnhanceGraph::FrameOutputs out;std::vector<float> actual;
        ok=ok&&graph.initialize(gd)&&graph.createViews()&&graph.process(frame,0,true,out,1,&color)&&captureReadFp16(ctx,ring,graph.diagnosticLinearInput(),actual);
        double maxError=0;if(ok)for(unsigned i=0;i<w*h;++i)for(unsigned c=0;c<3;++c){const double error=std::abs(actual[i*4+c]-expected[i][c])/std::max(1.0,std::abs(expected[i][c]));if(!std::isfinite(error))ok=false;maxError=std::max(maxError,error);}
        ok=ok&&maxError<.004;
        if(upload)ok=ok&&captureUploadFrame(frame)&&captureUploadFrame(frame)->direct&&captureUploadFrame(frame)->proxyValid;
        std::cout<<"DECKLINK_GPU packing="<<code<<" full="<<full<<" transfer="<<curve<<" upload="<<upload<<" normalizedError="<<maxError<<" pass="<<ok<<std::endl;
        if(!ok)++failures;out={};ring.drainQueue();graph.shutdown();av_frame_free(&frame);
    }
    return failures;
}
