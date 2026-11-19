#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include <cmath>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
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
        if(ok){for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint8_t neutral[4]={0,128,0,128};auto* p=remoteYuv?neutral:f->data[0]+ptrdiff_t(y)*f->linesize[0]+(x/2)*4;const double Y=(double(p[(x&1)?2:0])-(full?0:16))/(full?255:219);const double U=(double(p[1])-128)/(full?255:224),V=(double(p[3])-128)/(full?255:224);const double kr=hd?.2126:.299,kb=hd?.0722:.114;double expected[3]={Y+2*(1-kr)*V,0,Y+2*(1-kb)*U};expected[1]=(Y-kr*expected[0]-kb*expected[2])/(1-kr-kb);
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
