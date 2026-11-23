#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/source/CaptureCompressedDecoder.h"
#include "veyra/source/CaptureColorOverride.h"
#include "CaptureFormatGpuCases.h"
#include <cmath>
#include <fstream>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
namespace {
bool captureNearBlack(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,bool vfg) {
    using namespace pipeline;
    constexpr unsigned width=2560,height=1440,viewWidth=1280,viewHeight=720;
    constexpr std::array<uint8_t,16> codes{0,4,8,12,15,16,17,18,20,24,28,32,40,48,64,235};
    std::unique_ptr<AVFrame,void(*)(AVFrame*)> frame(av_frame_alloc(),[](AVFrame* f){av_frame_free(&f);});
    if(!frame)return false;
    frame->format=AV_PIX_FMT_NV12;frame->width=width;frame->height=height;
    if(av_frame_get_buffer(frame.get(),32)<0)return false;
    for(unsigned y=0;y<height;++y)
        for(unsigned i=0;i<codes.size();++i)
            std::fill_n(frame->data[0]+y*frame->linesize[0]+i*(width/codes.size()),width/codes.size(),codes[i]);
    for(unsigned y=0;y<height/2;++y) {
        auto* row=frame->data[1]+y*frame->linesize[1];
        std::fill_n(row,width,uint8_t(128));
        if(y>=3*height/8)
            for(unsigned x=1;x<width;x+=2)row[x]=x%4==1?124:132;
    }
    const HWND window=CreateWindowExW(0,L"STATIC",L"NV12 near-black fixture",WS_POPUP,
        0,0,viewWidth,viewHeight,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return false;
    bool ok=true;
    for(const auto range:{ColorRange::Limited,ColorRange::Full}) {
        std::vector<uint8_t> nearest;
        for(const bool reconstruct:{false,true}) {
            EnhanceGraphDesc desc;
            desc.sourceWidth=desc.workWidth=width;desc.sourceHeight=desc.workHeight=height;
            desc.enableNr=desc.enableSr=false;desc.enableFg=vfg;desc.noFeatures=!vfg;
            desc.captureBitDepth=8;desc.highQualityPresentation=true;
            desc.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;
            desc.fgMultiplier=2;desc.vfgQuality=1;desc.vfgAsyncSubmission=vfg;
            desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
            ColorDescription color;
            color.pixelFormat=SourcePixelFormat::NV12;color.range=range;color.matrix=YuvMatrix::BT709;
            color.transfer=TransferFunction::SRGB;color.preserveSdrCodeValues=true;
            color.reconstructChroma=reconstruct;
            EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
            EnhanceGraph::FrameOutputs output;
            sink::RgbaImage real,generated;
            auto rendered=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
            for(unsigned i=0;rendered&&i<(vfg?4u:1u);++i) {
                output={};
                // Motions are outside the neutral probes so the provider can
                // generate a real midpoint while the gray bars stay still.
                for(unsigned y=height-128;y<height-64;++y) {
                    auto* patch=frame->data[0]+y*frame->linesize[0];
                    std::fill_n(patch,128,uint8_t(16));
                    std::fill_n(patch+16+8*i,32,uint8_t(80));
                }
                rendered=graph.process(frame.get(),i*(1000.0/30),i==0,output,i+1,&color);
            }
            rendered=rendered&&presenter.present(ctx,ring,graph,output.videoSlot,false)
                &&presenter.readPresentedFrameForTest(ctx,ring,real);
            if(rendered&&vfg)rendered=ring.waitIdle()&&graph.resolveGeneration(output)&&output.hasGenerated
                &&presenter.present(ctx,ring,graph,output.genSlot,true)
                &&presenter.readPresentedFrameForTest(ctx,ring,generated);
            auto inspect=[&](const sink::RgbaImage& image,bool interpolated) {
                if(image.width!=viewWidth||image.height!=viewHeight)return false;
                int maxError=0;
                std::cout<<"NEAR_BLACK "<<(range==ColorRange::Limited?"limited":"full")
                    <<" chroma="<<reconstruct<<" generated="<<interpolated<<" values=";
                const unsigned y=viewHeight/4;
                for(unsigned i=0;i<codes.size();++i) {
                    const auto px=(size_t(y)*viewWidth+(2*i+1)*(viewWidth/(2*codes.size())))*4;
                    const int expected=range==ColorRange::Full?codes[i]:
                        int(std::lround(std::clamp((codes[i]-16.0)/219.0,0.0,1.0)*255));
                    std::cout<<int(image.pixels[px])<<(i+1==codes.size()?"":";");
                    for(unsigned c=0;c<3;++c)
                        maxError=std::max(maxError,std::abs(int(image.pixels[px+c])-expected));
                }
                std::cout<<" error="<<maxError<<std::endl;
                return maxError<=(interpolated?3:1);
            };
            if(rendered) {
                const auto neutralEnd=real.pixels.begin()+size_t(real.width)*real.height*2;
                if(!reconstruct)nearest.assign(real.pixels.begin(),neutralEnd);
                else ok&=std::equal(nearest.begin(),nearest.end(),real.pixels.begin(),neutralEnd);
                ok&=inspect(real,false);
                if(vfg)ok&=inspect(generated,true);
            }else{
                std::cout<<"NEAR_BLACK render failed chroma="<<reconstruct<<" vfg="<<vfg<<std::endl;
                ok=false;
            }
            output={};ring.drainQueue();presenter.close();graph.shutdown();
        }
    }
    DestroyWindow(window);
    return ok;
}

bool reconstructedCaptureChroma(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring) {
    using namespace pipeline;
    constexpr unsigned w=64,h=32;
    struct Format { const char* name; AVPixelFormat input; SourcePixelFormat color; };
    const Format formats[]={
        {"YUY2",AV_PIX_FMT_YUYV422,SourcePixelFormat::Yuy2},
        {"UYVY",AV_PIX_FMT_UYVY422,SourcePixelFormat::Uyvy},
        {"YVYU",AV_PIX_FMT_YVYU422,SourcePixelFormat::Yvyu},
        {"NV12",AV_PIX_FMT_NV12,SourcePixelFormat::NV12},
        {"P010",AV_PIX_FMT_P010,SourcePixelFormat::P010}
    };
    bool all=true;
    for(const auto& spec:formats){
        const bool planar=spec.input==AV_PIX_FMT_NV12||spec.input==AV_PIX_FMT_P010;
        const bool p010=spec.input==AV_PIX_FMT_P010;
        AVFrame* frame=av_frame_alloc();
        if(!frame)return false;
        frame->format=spec.input;frame->width=w;frame->height=h;
        if(av_frame_get_buffer(frame,32)<0){av_frame_free(&frame);return false;}
        const auto u=[](unsigned x,unsigned y){return uint8_t(((x+y)&1)?160:96);};
        const auto v=[](unsigned x,unsigned y){return uint8_t(((x+y)&1)?112:144);};
        if(planar){
            for(unsigned y=0;y<h;++y){
                auto* line=frame->data[0]+y*frame->linesize[0];
                if(p010)std::fill_n(reinterpret_cast<uint16_t*>(line),w,uint16_t(126*256));
                else std::fill_n(line,w,uint8_t(126));
            }
            for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w/2;++x){
                auto* line=frame->data[1]+y*frame->linesize[1];
                if(p010){
                    auto* words=reinterpret_cast<uint16_t*>(line)+2*x;
                    words[0]=uint16_t(u(x,y))*256;words[1]=uint16_t(v(x,y))*256;
                }else{line[2*x]=u(x,y);line[2*x+1]=v(x,y);}
            }
        }else{
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w/2;++x){
                auto* pair=frame->data[0]+y*frame->linesize[0]+4*x;
                if(spec.color==SourcePixelFormat::Uyvy){
                    pair[0]=u(x,y);pair[1]=126;pair[2]=v(x,y);pair[3]=126;
                }else{
                    pair[0]=pair[2]=126;
                    pair[1]=spec.color==SourcePixelFormat::Yvyu?v(x,y):u(x,y);
                    pair[3]=spec.color==SourcePixelFormat::Yvyu?u(x,y):v(x,y);
                }
            }
        }
        EnhanceGraphDesc desc;
        desc.sourceWidth=desc.workWidth=w;desc.sourceHeight=desc.workHeight=h;desc.noFeatures=true;
        desc.yuy2Input=spec.color==SourcePixelFormat::Yuy2;
        desc.packedInput=packedInputCode(spec.color);desc.captureBitDepth=p010?10:8;
        ColorDescription color;
        color.pixelFormat=spec.color;color.range=ColorRange::Limited;color.matrix=YuvMatrix::BT709;
        color.transfer=TransferFunction::SRGB;
        color.reconstructChroma=planar;color.chromaLocation=ChromaLocation::TopLeft;
        EnhanceGraph graph(ctx,ring);EnhanceGraph::FrameOutputs out;
        sink::RgbaImage pixels;
        const bool rendered=graph.initialize(desc)&&graph.createViews()&&graph.process(frame,0,true,out,1,&color)&&
            sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),pixels);
        int maxError=0;
        if(rendered)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const unsigned row=planar?y/2:y,nextRow=planar?std::min(row+1,h/2-1):row;
            const auto col=x/2,nextCol=std::min(col+1,w/2-1);
            const double fx=x%2?.5:0,fy=planar&&y%2?.5:0;
            const auto sample=[&](auto c){return std::lerp(
                std::lerp(double(c(col,row)),double(c(nextCol,row)),fx),
                std::lerp(double(c(col,nextRow)),double(c(nextCol,nextRow)),fx),fy);};
            const double l=110.0/219,cb=(sample(u)-128.0)/224,cr=(sample(v)-128.0)/224;
            const double rgb[]={l+1.5748*cr,l-.187324*cb-.468124*cr,l+1.8556*cb};
            for(unsigned c=0;c<3;++c)maxError=std::max(maxError,std::abs(
                int(pixels.pixels[(size_t(y)*w+x)*4+c])-int(std::lround(std::clamp(rgb[c],0.0,1.0)*255))));
        }
        const bool pass=rendered&&maxError<=1;
        std::cout<<"CAPTURE_CHROMA "<<spec.name<<" maxError="<<maxError<<" pass="<<pass<<std::endl;
        all&=pass;
        out={};graph.shutdown();av_frame_free(&frame);
    }
    return all;
}
bool hdrCaptureChroma(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring) {
    using namespace pipeline;
    constexpr unsigned w=256,h=128;
    AVFrame* frame=av_frame_alloc();
    if(!frame)return false;
    frame->format=AV_PIX_FMT_P010;frame->width=w;frame->height=h;
    if(av_frame_get_buffer(frame,32)<0){av_frame_free(&frame);return false;}
    for(unsigned y=0;y<h;++y)
        std::fill_n(reinterpret_cast<uint16_t*>(frame->data[0]+y*frame->linesize[0]),w,uint16_t(500u<<6));
    for(unsigned y=0;y<h/2;++y){
        auto* row=reinterpret_cast<uint16_t*>(frame->data[1]+y*frame->linesize[1]);
        for(unsigned x=0;x<w/2;++x){
            row[2*x]=uint16_t(480u<<6);
            row[2*x+1]=uint16_t((570u+x+y)<<6);
        }
    }
    EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=w;desc.sourceHeight=desc.workHeight=h;desc.noFeatures=true;
    desc.enableNr=desc.enableSr=desc.enableFg=false;
    desc.hdrInput=desc.hdrOutput=true;desc.hdrOutputMode=engine::HdrOutputMode::ScRgb;
    desc.captureBitDepth=10;
    ColorDescription color;
    color.pixelFormat=SourcePixelFormat::P010;color.range=ColorRange::Limited;color.matrix=YuvMatrix::BT2020NCL;
    color.primaries=ColorPrimaries::BT2020;color.transfer=TransferFunction::PQ;color.chromaLocation=ChromaLocation::TopLeft;
    EnhanceGraph graph(ctx,ring);
    bool ok=graph.initialize(desc)&&graph.createViews();
    double energy[2][2]{};
    for(unsigned reconstructed=0;ok&&reconstructed<2;++reconstructed) {
        EnhanceGraph::FrameOutputs out;
        std::vector<float> pixels;
        color.reconstructChroma=reconstructed!=0;
        ok=graph.process(frame,double(reconstructed)*16.667,true,out,reconstructed+1,&color)&&
            captureReadFp16(ctx,ring,graph.videoFrameResource(out.videoSlot),pixels,D3D12_RESOURCE_STATE_COMMON);
        if(!ok)break;
        for(unsigned y=2;y+2<h;++y)for(unsigned x=2;x+2<w;++x) {
            const size_t center=(size_t(y)*w+x)*4;
            for(unsigned axis=0;axis<2;++axis) {
                const size_t step=axis?w*4:4;
                energy[reconstructed][axis]+=std::abs(2.0*pixels[center]-pixels[center-step]-pixels[center+step]);
            }
        }
    }
    graph.shutdown();av_frame_free(&frame);
    ok=ok&&energy[0][0]>1&&energy[0][1]>1&&energy[1][0]<energy[0][0]*.25&&energy[1][1]<energy[0][1]*.25;
    std::cout<<"CAPTURE_HDR_CHROMA nearestX="<<energy[0][0]<<" nearestY="<<energy[0][1]
        <<" reconstructedX="<<energy[1][0]<<" reconstructedY="<<energy[1][1]<<" pass="<<ok<<std::endl;
    return ok;
}

bool fullscreenFrameStats(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring) {
    constexpr unsigned w=384,h=216;
    const HWND window=CreateWindowExW(0,L"STATIC",L"Fullscreen FPS fixture",WS_POPUP,0,0,w,h,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return false;
    AVFrame* frame=av_frame_alloc();
    if(!frame){DestroyWindow(window);return false;}
    frame->format=AV_PIX_FMT_NV12;frame->width=w;frame->height=h;
    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=w;desc.sourceHeight=desc.workHeight=h;desc.noFeatures=true;
    desc.enableNr=desc.enableSr=desc.enableFg=false;
    pipeline::EnhanceGraph graph(ctx,ring);
    engine::VideoPresenter presenter;
    pipeline::EnhanceGraph::FrameOutputs output;
    bool ok=av_frame_get_buffer(frame,32)>=0&&graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
    if(ok){
        for(unsigned y=0;y<h;++y)std::fill_n(frame->data[0]+y*frame->linesize[0],w,110);
        for(unsigned y=0;y<h/2;++y)std::fill_n(frame->data[1]+y*frame->linesize[1],w,128);
        ok=graph.process(frame,0,true,output,1);
    }
    sink::RgbaImage baseline,shown;
    const auto present=[&](sink::RgbaImage& out){
        return presenter.present(ctx,ring,graph,output.videoSlot,false)&&presenter.readPresentedFrameForTest(ctx,ring,out);
    };
    if(ok)ok=present(baseline)&&baseline.width==w&&baseline.height==h;
    std::cout<<"frame stats baseline="<<ok<<std::endl;
    for(int corner=0;ok&&corner<4;++corner){
        presenter.setFullscreenFrameStats(corner);presenter.updateFrameStats(59.9,54.0);
        std::cout<<"frame stats before present corner="<<corner<<std::endl;
        ok=present(shown)&&shown.pixels.size()==baseline.pixels.size();
        if(!ok)break;
        unsigned count=0,outside=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const size_t pixel=(size_t(y)*w+x)*4;
            if(shown.pixels[pixel]==baseline.pixels[pixel])continue;
            ++count;
            outside+=((x>=w/2)!=(corner&1))||((y>=h/2)!=bool(corner&2));
        }
        ok=count>300&&outside==0;
        std::cout<<"FULLSCREEN_FRAME_STATS corner="<<corner<<" changedPixels="<<count<<" outside="<<outside<<" pass="<<ok<<std::endl;
    }
    presenter.setFullscreenFrameStats(-1);
    if(ok)ok=present(shown)&&shown.pixels==baseline.pixels;
    output={};ring.drainQueue();presenter.close();graph.shutdown();av_frame_free(&frame);DestroyWindow(window);
    return ok;
}

bool mjpegRange(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,const wchar_t* path) {
    constexpr unsigned w=64,h=32;
    std::ifstream file(std::filesystem::path(path),std::ios::binary);
    const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    source::CaptureCompressedDecoder decoder;
    if(encoded.empty()||!decoder.open(source::CaptureCodec::Mjpeg,w,h,nullptr,0,nullptr,nullptr))return false;
    AVFrame* buffer=av_frame_alloc();
    if(!buffer)return false;
    buffer->format=AV_PIX_FMT_NV12;buffer->width=w;buffer->height=h;
    AVFrame* frame=nullptr;bool hardware=false;
    if(av_frame_get_buffer(buffer,32)<0||!decoder.decode(encoded.data(),encoded.size(),1,buffer,&frame,hardware)){
        av_frame_free(&buffer);return false;
    }
    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=w;desc.sourceHeight=desc.workHeight=h;desc.noFeatures=true;
    pipeline::EnhanceGraph graph(ctx,ring);
    if(!graph.initialize(desc)||!graph.createViews()){av_frame_free(&buffer);return false;}
    pipeline::ColorDescription fallback;fallback.preserveSdrCodeValues=true;
    int black[2]{};
    bool ok=frame->data[0][0]==16&&!hardware;
    for(unsigned range=0;range<2;++range){
        const auto color=source::resolveCompressedCaptureColor(*frame,fallback,source::effectiveCaptureColorOverride(source::captureColorOverride(1,range),true));
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;
        if(!graph.process(frame,range*33.3,true,out,range+1,&color)||
            !sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image)){ok=false;break;}
        black[range]=image.pixels[0];
    }
    const bool pass=ok&&std::abs(black[0]-16)<=1&&black[1]==0;
    std::cout<<"MJPEG_RANGE auto="<<black[0]<<" limited="<<black[1]<<" pass="<<pass<<std::endl;
    graph.shutdown();av_frame_free(&buffer);
    return pass;
}
}
int wmain(int argc,wchar_t**argv){
    bool legacyRemote=false;
    bool remoteYuv=false,nr=false,rgb=false,native4k=false,temporal=false,realtime=false;
    for(int i=1;i<argc;++i){remoteYuv|=wcscmp(argv[i],L"--remote-yuv")==0;nr|=wcscmp(argv[i],L"--nr")==0;rgb|=wcscmp(argv[i],L"--rgb")==0;native4k|=wcscmp(argv[i],L"--4k")==0;temporal|=wcscmp(argv[i],L"--temporal")==0;realtime|=wcscmp(argv[i],L"--realtime")==0;}
    for(int i=1;i<argc;++i)legacyRemote|=wcscmp(argv[i],L"--legacy-remote-transfer")==0;
    if(legacyRemote&&!remoteYuv)return 4;
    nr|=temporal;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;
    if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 1;
    if(argc==2&&std::wstring_view(argv[1])==L"--capture-near-black")
        return captureNearBlack(ctx,ring,false)?0:1;
    if(argc==2&&std::wstring_view(argv[1])==L"--capture-near-black-vfg")
        return captureNearBlack(ctx,ring,true)?0:1;
    if(argc==2&&std::wstring_view(argv[1])==L"--capture-chroma")
        return reconstructedCaptureChroma(ctx,ring)?0:1;
    if(argc==2&&std::wstring_view(argv[1])==L"--capture-hdr-chroma")
        return hdrCaptureChroma(ctx,ring)?0:1;
    if(argc==2&&std::wstring_view(argv[1])==L"--fullscreen-stats")
        return fullscreenFrameStats(ctx,ring)?0:1;
    if(argc==3&&std::wstring_view(argv[1])==L"--mjpeg-range")
        return mjpegRange(ctx,ring,argv[2])?0:1;
    const unsigned w=native4k?3840:nr?1920:256,h=native4k?2160:nr?1080:64;
    HWND window=CreateWindowExW(0,L"STATIC",L"YUY2 contract",WS_POPUP,0,0,w,h,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.workWidth=w;gd.sourceHeight=gd.workHeight=h;gd.yuy2Input=!rgb&&!remoteYuv;gd.rgbInput=rgb;gd.enableFg=false;gd.enableNr=nr;gd.enableNvofStandalone=nr;gd.noFeatures=!nr;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    if(realtime){
        if(!native4k||!nr)return 4;
        gd.nrWidth=gd.flowWidth=1920;gd.nrHeight=gd.flowHeight=1080;
    }
    if(!window||!graph.initialize(gd)||!presenter.open(ctx,window,graph)||!graph.createViews())return 2;
    const unsigned expectedInternalWidth=realtime?1920:w,expectedInternalHeight=realtime?1080:h;
    if(graph.nrWidth()!=expectedInternalWidth||graph.nrHeight()!=expectedInternalHeight||graph.flowWidth()!=expectedInternalWidth||graph.flowHeight()!=expectedInternalHeight)return 4;
    AVFrame* f=av_frame_alloc();f->format=remoteYuv?AV_PIX_FMT_YUV420P:rgb?AV_PIX_FMT_BGRA:AV_PIX_FMT_YUYV422;f->width=w;f->height=h;if(av_frame_get_buffer(f,32)<0)return 3;
    int failures=0;unsigned frameId=0;
    for(unsigned index=0;index<(temporal?8u:nr?2u:4u);++index){
        const bool full=!temporal&&(nr?index>0:index>=2),hd=temporal||(!nr&&(index%2));
        if(!remoteYuv)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;x+=2){auto* p=f->data[0]+ptrdiff_t(y)*f->linesize[0]+x*2;p[0]=uint8_t((x+(temporal?index*2:0))%256);p[2]=uint8_t((x+1+(temporal?index*2:0))%256);p[1]=y%4==0?128:y%4==1?16:y%4==2?240:90;p[3]=y%4==0?128:y%4==1?240:y%4==2?16:180;}
        if(!remoteYuv&&!rgb&&!nr)
            for(unsigned y=h/2;y<h;++y)for(unsigned x=0;x<w;x+=2){
                auto* p=f->data[0]+ptrdiff_t(y)*f->linesize[0]+x*2;
                p[1]=((x/2)&1)?224:32;
                p[3]=((x/2)&1)?48:208;
            }
        if(remoteYuv){
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)f->data[0][ptrdiff_t(y)*f->linesize[0]+x]=uint8_t(x%256);
            for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w/2;++x){f->data[1][ptrdiff_t(y)*f->linesize[1]+x]=y%4==0?128:y%4==1?16:y%4==2?240:90;f->data[2][ptrdiff_t(y)*f->linesize[2]+x]=y%4==0?128:y%4==1?240:y%4==2?16:180;}
        }
        // Alternating luma must survive 4:2:2 unpack and 1:1 presentation.
        // The lower half retains the matrix/range color coverage above.
        if(!remoteYuv&&!rgb&&!nr)for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w;x+=2){
            auto* p=f->data[0]+ptrdiff_t(y)*f->linesize[0]+x*2;
            p[0]=(y&1)?235:16;p[2]=(y&1)?16:235;p[1]=p[3]=128;
        }
        if(rgb)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto* p=f->data[0]+ptrdiff_t(y)*f->linesize[0]+x*4;p[0]=uint8_t(x%256);p[1]=uint8_t((x+37)%256);p[2]=uint8_t((x+129)%256);p[3]=255;}
        pipeline::ColorDescription color;color.pixelFormat=pipeline::SourcePixelFormat::Yuy2;color.range=full?pipeline::ColorRange::Full:pipeline::ColorRange::Limited;color.matrix=hd?pipeline::YuvMatrix::BT709:pipeline::YuvMatrix::BT601;color.transfer=pipeline::TransferFunction::SRGB;
        if(remoteYuv){color.pixelFormat=pipeline::SourcePixelFormat::Yuv420P;color.transfer=pipeline::TransferFunction::BT709;color.preserveSdrCodeValues=!legacyRemote;color.displayReferred709=legacyRemote;}
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image,display;
        if(rgb)color.pixelFormat=pipeline::SourcePixelFormat::Bgra8;
        const double ptsMs=frameId*100.0;++frameId;
        bool ok=graph.process(f,ptsMs,!temporal||index==0,out,frameId,&color)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
        int error=0,displayError=0;uint64_t sum=0;
        if(ok){for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint8_t neutral[4]={0,128,0,128};auto* p=remoteYuv?neutral:f->data[0]+ptrdiff_t(y)*f->linesize[0]+(x/2)*4;const double Y=(double(p[(x&1)?2:0])-(full?0:16))/(full?255:219);const auto* next=!remoteYuv&&(x&1)&&x+1<w?p+4:p;
                const double U=(0.5*(p[1]+next[1])-128)/(full?255:224),V=(0.5*(p[3]+next[3])-128)/(full?255:224);const double kr=hd?.2126:.299,kb=hd?.0722:.114;double expected[3]={Y+2*(1-kr)*V,0,Y+2*(1-kb)*U};expected[1]=(Y-kr*expected[0]-kb*expected[2])/(1-kr-kb);
                if(remoteYuv){const double luma=(double(x%256)-(full?0:16))/(full?255:219);
                    const double u=(double(f->data[1][ptrdiff_t(y/2)*f->linesize[1]+x/2])-128)/(full?255:224),v=(double(f->data[2][ptrdiff_t(y/2)*f->linesize[2]+x/2])-128)/(full?255:224);
                    const double normalized=std::clamp(luma,0.0,1.0);expected[0]=normalized+2*(1-kr)*v;expected[2]=normalized+2*(1-kb)*u;expected[1]=(normalized-kr*expected[0]-kb*expected[2])/(1-kr-kb);
                    // No-effects SDR output must preserve post-matrix RGB code values.
                }
                if(rgb){const auto* bgra=f->data[0]+ptrdiff_t(y)*f->linesize[0]+x*4;for(unsigned c=0;c<3;++c)expected[c]=(double(bgra[2-c])-(full?0:16))/(full?255:219);}
                for(unsigned c=0;c<3;++c){const int actual=image.pixels[(size_t(y)*w+x)*4+c];sum+=actual;error=std::max(error,std::abs(actual-int(std::lround(std::clamp(expected[c],0.0,1.0)*255))));}}
            ok=sum>0&&(nr?graph.metrics().nrEvaluateCount==frameId:error<=1);
        }
        if(ok)ok=presenter.present(ctx,ring,graph,out.videoSlot,false)&&presenter.readPresentedFrameForTest(ctx,ring,display);
        if(ok){ok=image.pixels.size()==display.pixels.size();if(ok)for(size_t i=0;i<image.pixels.size();++i)displayError=std::max(displayError,std::abs(int(image.pixels[i])-int(display.pixels[i])));ok=ok&&displayError<=1;}
        std::cout<<(remoteYuv?"REMOTE_YUV_COLOR":rgb?"RGB_COLOR":"YUY2_COLOR")<<" extent="<<w<<"x"<<h<<" nrExtent="<<graph.nrWidth()<<"x"<<graph.nrHeight()<<" flowExtent="<<graph.flowWidth()<<"x"<<graph.flowHeight()<<" full="<<full<<" hd="<<hd<<" nr="<<nr<<" sourceError="<<error<<" displayError="<<displayError<<" pass="<<ok<<std::endl;
        if(!ok)++failures;out={};
    }
    if(temporal){const auto metrics=graph.metrics();const bool ok=metrics.nvofExecuteCount>0&&metrics.nrMotionFrames>0;std::cout<<"YUY2_TEMPORAL nvof="<<metrics.nvofExecuteCount<<" nrMotion="<<metrics.nrMotionFrames<<" pass="<<ok<<std::endl;if(!ok)++failures;}
    ring.drainQueue();presenter.close();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return failures?1:0;
}
