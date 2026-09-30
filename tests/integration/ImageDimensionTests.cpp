#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/TiledImageProcessor.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/ngx/NgxParameters.h"
#include <filesystem>
#include <iostream>
#include <cmath>
#include <vector>
#include <d3d12sdklayers.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;
bool run(unsigned width,unsigned height,bool nr,const std::filesystem::path& directory){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.workWidth=width;gd.sourceHeight=gd.workHeight=height;
    gd.rgbInput=true;gd.stillImage=true;gd.enableFg=false;gd.enableNr=nr;gd.noFeatures=!nr;
    gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(!graph.initialize(gd)||!graph.createViews())return false;
    if(graph.fgCapabilityAvailable()||graph.fgCreated())return false;
    graph.setFgEnabled(true);if(graph.fgEnabled())return false;
    engine::EnhancementSettings settings;settings.multiplier=2;
    if(graph.applySettings(settings))return false;
    AVFrame* f=av_frame_alloc();f->width=width;f->height=height;f->format=AV_PIX_FMT_RGBA;
    f->color_range=AVCOL_RANGE_JPEG;f->color_trc=AVCOL_TRC_IEC61966_2_1;f->colorspace=AVCOL_SPC_RGB;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;
        p[0]=uint8_t((x*13+y*7)%256);p[1]=uint8_t((x*3+y*11)%256);p[2]=uint8_t((x*5+y*17+113)%256);p[3]=255;}
    pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
    bool ok=graph.process(f,0,true,out,1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
    int maxError=0;uint64_t brightness=0;
    if(ok){ok=result.width==width&&result.height==height;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c){const int value=result.pixels[(size_t(y)*width+x)*4+c];brightness+=value;maxError=std::max(maxError,std::abs(value-int(f->data[0][size_t(y)*f->linesize[0]+x*4+c])));}
        ok=ok&&brightness>0&&(nr?graph.metrics().nrEvaluateCount==1:maxError<=1);
        const auto path=directory/(std::to_string(width)+"x"+std::to_string(height)+(nr?"-nr.png":"-off.png"));
        sink::RgbaImage decoded;ok=ok&&sink::saveImage(path.wstring(),result)&&sink::loadImage(path.wstring(),decoded);
        ok=ok&&decoded.width==width&&decoded.height==height&&decoded.pixels==result.pixels;
    }
    std::cout<<"IMAGE_DIMENSION "<<width<<'x'<<height<<" nr="<<nr<<" maxError8="<<maxError<<" nrEvaluations="<<graph.metrics().nrEvaluateCount<<" pass="<<ok<<std::endl;
    out={};ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
bool tiled(unsigned width,unsigned height,bool nr,const std::filesystem::path& directory,int protect=0){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    sink::RgbaImage source;source.width=width;source.height=height;source.pixels.resize(size_t(width)*height*4);
    for(size_t i=0;i<source.pixels.size();i+=4){source.pixels[i]=uint8_t(i/4%251);source.pixels[i+1]=uint8_t(i/width%239);source.pixels[i+2]=113;source.pixels[i+3]=255;}
    // Independent display contract: half-transparent white composites to
    // sRGB 188 over black; fully transparent color contributes nothing.
    if(!nr){source.pixels[0]=source.pixels[1]=source.pixels[2]=255;source.pixels[3]=128;source.pixels[7]=0;}
    pipeline::EnhanceGraphDesc gd;gd.enableNr=nr;gd.noFeatures=!nr;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(protect){gd.protection.enabled=true;gd.protection.featherPixels=0;gd.protection.regions[0]={0,0,1,1};if(protect==2){gd.protection.featherPixels=32;gd.protection.regions[0]={.25f,.25f,.75f,.75f};}}
    std::atomic<bool> cancel=false;engine::TiledImageProcessor::Stats stats;sink::RgbaImage result;
    bool ok=engine::TiledImageProcessor::process(ctx,ring,source,result,gd,cancel,stats);
    int maxError=0;uint64_t brightness=0;
    if(ok){ok=result.width==width&&result.height==height&&result.pixels.size()==source.pixels.size();
        for(size_t i=0;i<result.pixels.size();++i){int expected=source.pixels[i];if(!nr&&i<8)expected=i%4==3?255:(i<4?188:0);maxError=std::max(maxError,std::abs(int(result.pixels[i])-expected));if(i%4!=3)brightness+=result.pixels[i];}
        ok=ok&&brightness>0&&(nr?stats.nrEvaluations==stats.tiles&&(protect!=1||maxError<=1):maxError<=1);
        if(protect==2){
            sink::RgbaImage baseline;ok=ok&&sink::loadImage((directory/(std::to_string(width)+"x"+std::to_string(height)+"-tiled-nr.png")).wstring(),baseline);
            ok=ok&&baseline.width==width&&baseline.height==height&&baseline.pixels.size()==result.pixels.size();
            int inside=0,outside=0,envelope=0;uint64_t interiorCount=0,exteriorCount=0;
            if(ok)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                float edge=std::min(std::min(x+.5f-width*.25f,width*.75f-x-.5f),std::min(y+.5f-height*.25f,height*.75f-y-.5f));
                interiorCount+=edge>=32;exteriorCount+=edge<=0;
                for(unsigned c=0;c<3;++c){size_t i=(size_t(y)*width+x)*4+c;int v=result.pixels[i],a=source.pixels[i],b=baseline.pixels[i];
                    if(edge>=32)inside=std::max(inside,std::abs(v-a));
                    if(edge<=0)outside=std::max(outside,std::abs(v-b));
                    envelope=std::max(envelope,std::max(std::min(a,b)-v,v-std::max(a,b)));
                }
            }
            ok=ok&&inside<=1&&outside<=1&&envelope<=2&&interiorCount>100000&&exteriorCount>100000;
            std::cout<<"PROTECTION_PARTIAL_TILE feather=32 insideError8="<<inside<<" outsideError8="<<outside<<" envelopeError8="<<envelope<<" pass="<<ok<<std::endl;
        }
        sink::RgbaImage decoded;auto path=directory/(std::to_string(width)+"x"+std::to_string(height)+(protect==2?"-tiled-partial.png":protect?"-tiled-protected.png":nr?"-tiled-nr.png":"-tiled-off.png"));
        ok=ok&&sink::saveImage(path.wstring(),result)&&sink::loadImage(path.wstring(),decoded)&&decoded.pixels==result.pixels&&decoded.width==width&&decoded.height==height;
    }
    std::cout<<"IMAGE_TILED "<<width<<'x'<<height<<" nr="<<nr<<" protect="<<protect<<" tiles="<<stats.tiles<<" evaluations="<<stats.nrEvaluations<<" maxError8="<<maxError<<" pass="<<ok<<std::endl;
    if(ok&&!nr){cancel=false;sink::RgbaImage abandoned;engine::TiledImageProcessor::Stats partial;
        const bool accepted=engine::TiledImageProcessor::process(ctx,ring,source,abandoned,gd,cancel,partial,[&](uint32_t,uint32_t){cancel=true;});
        ok=!accepted&&abandoned.pixels.empty()&&partial.tiles==1;
        std::cout<<"IMAGE_TILED_CANCEL pass="<<ok<<std::endl;
    }
    ring.shutdown();ctx.shutdown();return ok;
}
bool codecBands(const std::filesystem::path& directory){
    sink::RgbaImage source;source.width=1024;source.height=3073;source.pixels.resize(size_t(source.width)*source.height*4);
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){auto* p=source.pixels.data()+(size_t(y)*source.width+x)*4;p[std::min(2u,y/1024)]=255;p[3]=255;}
    bool ok=true;
    for(bool jpeg:{false,true}){sink::RgbaImage decoded;const auto path=directory/(jpeg?"banded.jpg":"banded.png");
        ok=ok&&sink::saveImage(path.wstring(),source,jpeg)&&sink::loadImage(path.wstring(),decoded)&&decoded.width==source.width&&decoded.height==source.height;
        if(!ok)break;
        if(!jpeg)ok=decoded.pixels==source.pixels;
        else for(unsigned y:{512u,1536u,2560u})for(unsigned c=0;c<3;++c){auto i=(size_t(y)*source.width+512)*4+c;ok=ok&&std::abs(int(decoded.pixels[i])-int(source.pixels[i]))<=5;}
    }
    std::cout<<"IMAGE_CODEC_BANDS pass="<<ok<<std::endl;return ok;
}
bool colorFallback(){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=64;gd.stillImage=gd.noFeatures=true;gd.enableNr=gd.enableFg=false;
    if(!graph.initialize(gd)||!graph.createViews())return false;
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_YUV420P;f->width=f->height=64;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    bool ok=true;uint64_t sequence=0;
    auto test=[&](int y,int u,int v,AVColorTransferCharacteristic trc,int expectedR,int expectedG,int expectedB){
        f->color_trc=trc;f->color_range=AVCOL_RANGE_UNSPECIFIED;f->colorspace=AVCOL_SPC_UNSPECIFIED;
        for(int row=0;row<64;++row)memset(f->data[0]+row*f->linesize[0],y,64);
        for(int row=0;row<32;++row){memset(f->data[1]+row*f->linesize[1],u,32);memset(f->data[2]+row*f->linesize[2],v,32);}
        auto metadata=pipeline::resolveFrameColor(*f);pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        bool pass=graph.process(f,0,true,out,++sequence,&metadata)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        if(pass){auto* p=result.pixels.data()+(32*64+32)*4;pass=std::abs(int(p[0])-expectedR)<=2&&std::abs(int(p[1])-expectedG)<=2&&std::abs(int(p[2])-expectedB)<=2;std::cout<<"COLOR_PIXEL transfer="<<trc<<" rgb="<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<" pass="<<pass<<std::endl;}
        out={};return pass;
    };
    ok=test(128,128,128,AVCOL_TRC_UNSPECIFIED,142,142,142)&&test(128,128,128,AVCOL_TRC_IEC61966_2_1,130,130,130)&&test(128,128,128,AVCOL_TRC_LINEAR,189,189,189)&&test(81,90,240,AVCOL_TRC_UNSPECIFIED,255,0,0);
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
bool captureRgb(bool nr){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=256;gd.rgbInput=true;gd.enableNr=nr;gd.enableNvofStandalone=nr;gd.enableFg=false;gd.noFeatures=!nr;
    gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(!graph.initialize(gd)||!graph.createViews())return false;
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_BGR0;f->width=f->height=256;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    bool ok=true;int maxError=0;
    for(unsigned frame=0;frame<4&&ok;++frame){
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;
            // Adjacent saturated red/blue proves that no 4:2:0 averaging occurs.
            p[0]=frame<2?(x%2?255:0):255;p[1]=frame<2?0:255;p[2]=frame<2?(x%2?0:255):255;p[3]=0;}
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        ok=graph.process(f,frame*33.333333,frame==0,out,frame+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        if(ok&&!nr)for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto* expected=f->data[0]+size_t(y)*f->linesize[0]+x*4;auto* actual=result.pixels.data()+(size_t(y)*256+x)*4;
            for(unsigned c=0;c<3;++c)maxError=std::max(maxError,std::abs(int(actual[c])-int(expected[2-c])));ok=ok&&actual[3]==255;}
        out={};
    }
    const auto m=graph.metrics();ok=ok&&maxError<=1&&m.sceneCutCount==1&&(!nr||(m.nrEvaluateCount==4&&m.nvofExecuteCount>=2));
    std::cout<<"CAPTURE_RGB nr="<<nr<<" maxError8="<<maxError<<" cuts="<<m.sceneCutCount<<" nrEvaluations="<<m.nrEvaluateCount<<" nvof="<<m.nvofExecuteCount<<" pass="<<ok<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
bool srPixels(unsigned iw,unsigned ih,unsigned ow,unsigned oh,const std::filesystem::path& directory){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=iw;gd.sourceHeight=ih;gd.workWidth=ow;gd.workHeight=oh;
    gd.rgbInput=gd.stillImage=gd.enableSr=true;gd.enableNr=gd.enableFg=false;
    gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(!graph.initialize(gd)||!graph.createViews())return false;
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=iw;f->height=ih;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    for(unsigned y=0;y<ih;++y)for(unsigned x=0;x<iw;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;
        unsigned quadrant=(x>=iw/2)+2*(y>=ih/2);p[0]=p[1]=p[2]=uint8_t(32+quadrant*64);p[3]=255;}
    bool ok=true;int error=0;
    for(unsigned frame=0;frame<3&&ok;++frame){
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        ok=graph.process(f,frame*33.333333,frame==0,out,frame+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        if(ok){ok=result.width==ow&&result.height==oh;
            for(unsigned q=0;q<4;++q){auto x=ow*(q%2?3:1)/4,y=oh*(q/2?3:1)/4;const auto* p=result.pixels.data()+(size_t(y)*ow+x)*4;
                for(unsigned c=0;c<3;++c)error=std::max(error,std::abs(int(p[c])-int(32+q*64)));}
            ok=ok&&sink::saveImage((directory/("sr-"+std::to_string(iw)+"x"+std::to_string(ih)+"-to-"+std::to_string(ow)+"x"+std::to_string(oh)+"-"+std::to_string(frame)+".png")).wstring(),result);
        }out={};
    }
    auto evals=graph.metrics().srEvaluateCount;ok=ok&&error<=8&&evals==((iw==ow&&ih==oh)?0:3);
    std::cout<<"SR_PIXELS "<<iw<<'x'<<ih<<" -> "<<ow<<'x'<<oh<<" interiorMaxError8="<<error<<" evaluates="<<evals<<" pass="<<ok<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
bool protectionPixels(){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=256;gd.rgbInput=gd.stillImage=true;gd.enableNr=true;gd.enableFg=false;
    gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    if(!graph.initialize(gd)||!graph.createViews())return false;
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=f->height=256;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;p[0]=(x*7+y*3)%256;p[1]=(x*3+y*11)%256;p[2]=(x*5+y*17)%256;p[3]=255;}
    sink::RgbaImage baseline;bool ok=true;int protectedError=0,outsideError=0;uint64_t changed=0;
    for(unsigned mode=0;mode<8&&ok;++mode){
        // applySettings() drives the NR stage from the settings snapshot, so a
        // default struct would switch NR off and the protection math below
        // would measure an unmodified image (pre-existing test defect: the
        // section reported changed=0/nr=0 and could never pass).
        engine::EnhancementSettings settings;settings.nr=true;settings.protection.enabled=mode>0;settings.protection.featherPixels=0;
        if(mode==2)settings.protection.regions[0]={0,0,1,1};
        if(mode>=3)settings.protection.regions[0]={.25f,.25f,.75f,.75f};
        // The accepted feather range is 0-64 px; 64 is the widest ramp the
        // panel exposes and must stay inside the NR exclusion contract.
        if(mode==4||mode==5||mode==7)settings.protection.featherPixels=mode==4?2.f:mode==5?32.f:64.f;
        if(mode==6){settings.protection.regions[0]={.125f,.125f,.375f,.375f};settings.protection.regions[1]={.625f,.625f,.875f,.875f};}
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        ok=graph.applySettings(settings)&&graph.process(f,0,true,out,mode+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        int envelope=0;uint64_t mixed=0;
        if(ok){if(!mode)baseline=result;
            for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)for(unsigned c=0;c<3;++c){auto i=(size_t(y)*256+x)*4+c;auto original=f->data[0][size_t(y)*f->linesize[0]+x*4+c];
                if(!mode){changed+=result.pixels[i]!=original;continue;}
                float edge=std::min(std::min(x+.5f-64,192.f-x-.5f),std::min(y+.5f-64,192.f-y-.5f));
                if(mode==4||mode==5||mode==7){
                    int v=result.pixels[i],a=original,b=baseline.pixels[i];
                    envelope=std::max(envelope,std::max(std::min(a,b)-v,v-std::max(a,b)));
                    if(edge>0&&edge<settings.protection.featherPixels){mixed+=std::abs(v-a)>1&&std::abs(v-b)>1;continue;}
                }
                bool protectedPixel=mode==2||((mode>=3&&mode<=5||mode==7)&&edge>=settings.protection.featherPixels)||(mode==6&&((x>=32&&x<96&&y>=32&&y<96)||(x>=160&&x<224&&y>=160&&y<224)));
                if(protectedPixel)protectedError=std::max(protectedError,std::abs(int(result.pixels[i])-int(original)));
                else outsideError=std::max(outsideError,std::abs(int(result.pixels[i])-int(baseline.pixels[i])));
            }
        }
        if(mode==4||mode==5||mode==7){ok=ok&&envelope<=1&&mixed>100;std::cout<<"PROTECTION_FEATHER pixels="<<settings.protection.featherPixels<<" mixedChannels="<<mixed<<" envelopeError8="<<envelope<<" pass="<<ok<<std::endl;}
        std::cout<<"PROTECTION_MODE mode="<<mode<<" protectedError8="<<protectedError<<" outsideError8="<<outsideError<<std::endl;
        out={};
    }
    ok=ok&&changed>100&&protectedError<=1&&outsideError==0&&graph.metrics().nrEvaluateCount==8;
    std::cout<<"PROTECTION_PIXELS empty/full/rectangle/feather/disjoint changed="<<changed<<" protectedError8="<<protectedError<<" outsideError8="<<outsideError<<" nr="<<graph.metrics().nrEvaluateCount<<" pass="<<ok<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();return ok;
}
// Two independent temporal sequences share identical input, PTS and history.
// Readback is diagnostic only; normal playback never calls this test.
bool protectionMoving(const std::filesystem::path& directory){
    constexpr unsigned width=256,height=256,frames=12;
    std::vector<sink::RgbaImage> baseline(frames);
    auto protectedPixel=[](unsigned x,unsigned y){return (x>=16&&x<112&&y>=16&&y<64)||(x>=32&&x<224&&y>=192&&y<240);};
    bool ok=true;uint64_t changedOutside=0,changedHud=0,replacedCaption=0;
    int protectedError=0,outsideError=0;
    for(unsigned mode=0;mode<2&&ok;++mode){
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.workWidth=width;gd.sourceHeight=gd.workHeight=height;
        gd.rgbInput=gd.enableNr=gd.enableNvofStandalone=true;gd.enableFg=false;
        gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        gd.protection.enabled=mode==1;gd.protection.featherPixels=0;
        gd.protection.regions[0]={16.f/width,16.f/height,112.f/width,64.f/height};
        gd.protection.regions[1]={32.f/width,192.f/height,224.f/width,240.f/height};
        if(!graph.initialize(gd)||!graph.createViews())return false;
        AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=width;f->height=height;
        if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
        sink::RgbaImage previousInput;
        for(unsigned frame=0;frame<frames&&ok;++frame){
            sink::RgbaImage input;input.width=width;input.height=height;input.pixels.resize(size_t(width)*height*4);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                auto* p=input.pixels.data()+(size_t(y)*width+x)*4;
                unsigned sx=(x+frame*3)%width,sy=(y+frame)%height;
                p[0]=uint8_t(40+(sx*7+sy*3)%160);p[1]=uint8_t(32+(sx*3+sy*11)%176);p[2]=uint8_t(24+(sx*5+sy*17)%192);p[3]=255;
                if(protectedPixel(x,y)){
                    // Fixed top HUD; bottom synthetic glyphs change at frames 4/8.
                    unsigned textPhase=y>=192?frame/4:0;
                    unsigned gx=(x/3+textPhase*2)%7,gy=(y/3)%9;
                    bool ink=(gx==1||gx==5||gy==2||gy==6)&&((x/21+textPhase)%3!=1||gy!=2);
                    p[0]=ink?235:24;p[1]=ink?224:30;p[2]=ink?180:40;
                }
                memcpy(f->data[0]+size_t(y)*f->linesize[0]+x*4,p,4);
            }
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
            ok=graph.process(f,frame*(1000.0/30),frame==0,out,frame+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
            if(ok){ok=result.width==width&&result.height==height&&result.pixels.size()==input.pixels.size();
                if(!mode)baseline[frame]=result;
                for(unsigned y=0;y<height&&ok;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c){
                    size_t i=(size_t(y)*width+x)*4+c;bool region=protectedPixel(x,y);
                    if(!mode){if(region)changedHud+=result.pixels[i]!=input.pixels[i];else changedOutside+=result.pixels[i]!=input.pixels[i];}
                    else if(region){protectedError=std::max(protectedError,std::abs(int(result.pixels[i])-int(input.pixels[i])));
                        if((frame==4||frame==8)&&y>=192)replacedCaption+=input.pixels[i]!=previousInput.pixels[i]&&result.pixels[i]!=previousInput.pixels[i];
                    }else outsideError=std::max(outsideError,std::abs(int(result.pixels[i])-int(baseline[frame].pixels[i])));
                }
                if(frame==0||frame==4||frame==8||frame==11){
                    auto prefix="moving-"+std::to_string(frame);
                    ok=ok&&sink::saveImage((directory/(prefix+(mode?"-protected.png":"-nr.png"))).wstring(),result);
                    if(!mode)ok=ok&&sink::saveImage((directory/(prefix+"-input.png")).wstring(),input);
                }
            }
            std::cout<<"PROTECTION_MOVING_FRAME mode="<<mode<<" frame="<<frame<<" protectedError8="<<protectedError<<" outsideError8="<<outsideError<<" pass="<<ok<<std::endl;
            previousInput=std::move(input);out={};
        }
        const auto m=graph.metrics();ok=ok&&m.nrEvaluateCount==frames&&m.nvofExecuteCount>=9&&m.nrMotionFrames>=9;
        std::cout<<"PROTECTION_MOVING_SEQUENCE mode="<<mode<<" nr="<<m.nrEvaluateCount<<" nvof="<<m.nvofExecuteCount<<" motionFrames="<<m.nrMotionFrames<<" resets="<<m.resetCount<<" cuts="<<m.sceneCutCount<<" pass="<<ok<<std::endl;
        ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();
    }
    ok=ok&&protectedError<=1&&outsideError==0&&changedOutside>1000&&changedHud>100&&replacedCaption>100;
    std::cout<<"PROTECTION_MOVING_RESULT protectedError8="<<protectedError<<" outsideError8="<<outsideError<<" changedBackground="<<changedOutside<<" changedHud="<<changedHud<<" freshCaptionChannels="<<replacedCaption<<" pass="<<ok<<std::endl;
    return ok;
}
bool nrOptionalResourcePixels(const std::filesystem::path& directory,bool rgba=false){
    constexpr unsigned width=256,height=256;const unsigned bytesPerPixel=rgba?4:1;const auto format=rgba?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8_UNORM;
    sink::RgbaImage baseline,rawBaseline;bool ok=true,matrixMatch=true;uint64_t changed=0;
    for(unsigned mode=0;mode<8&&ok;++mode){
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;device.enableDebugLayer=true;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        // Allocate/upload before graph descriptor creation. No per-frame upload.
        auto mask=pipeline::makeTexture(ctx.device(),width,height,format,false);
        auto upload=pipeline::makeUploadBuffer(ctx.device(),width*height*bytesPerPixel);
        if(!mask||!upload)return false;
        void* mapped=nullptr;if(FAILED(upload->Map(0,nullptr,&mapped)))return false;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            bool rect=x>=64&&x<192&&y>=64&&y<192;
            for(unsigned c=0;c<bytesPerPixel;++c)static_cast<uint8_t*>(mapped)[(y*width+x)*bytesPerPixel+c]=(mode==2||mode==4||mode==6||((mode==3||mode==7)&&rect))?255:0;
        }upload->Unmap(0,nullptr);
        uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return false;
        pipeline::StateTracker states;states.transition(list,mask.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=mask.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={format,width,height,1,width*bytesPerPixel};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,mask.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!ring.submitAndSignal(slot))return false;ring.drainQueue();
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.workWidth=width;gd.sourceHeight=gd.workHeight=height;
        gd.rgbInput=gd.stillImage=gd.enableNr=true;gd.enableFg=false;
        gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        ID3D12Resource* rawProxy=nullptr;ID3D12Resource* rawNeural=nullptr;
        gd.nrParameterProbe=[&](NVSDK_NGX_Parameter* parameters,ID3D12Resource* proxy,ID3D12Resource* neural,uint32_t w,uint32_t h){
            rawProxy=proxy;rawNeural=neural;if(!mode)return;
            ngx::ParameterBlock pb(parameters);const char* name=mode<=3?"DLSSNR.ControlMask":"DLSSNR.UIAlpha";
            pb.setD3D12Resource(name,mask.Get());
            // Pinned author probe uses signed I32 optional subrects; test exact types.
            for(auto suffix:{"SubrectBaseX","SubrectBaseY","SubrectWidth","SubrectHeight"}){
                std::string key=std::string(name)+suffix;pb.setI32(key.c_str(),std::string(suffix)=="SubrectWidth"?w:std::string(suffix)=="SubrectHeight"?h:0);
            }
            if(mode>=4){
                pb.setI32("DLSSNR.UICorrection",mode==4?0:1);pb.setD3D12Resource("DLSSNR.Backbuffer",proxy);
                pb.setI32("DLSSNR.BackbufferSubrectBaseX",0);pb.setI32("DLSSNR.BackbufferSubrectBaseY",0);
                pb.setI32("DLSSNR.BackbufferSubrectWidth",w);pb.setI32("DLSSNR.BackbufferSubrectHeight",h);
            }
        };
        if(!graph.initialize(gd)||!graph.createViews())return false;
        AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=width;f->height=height;
        if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;p[0]=(x*7+y*3)%256;p[1]=(x*3+y*11)%256;p[2]=(x*5+y*17)%256;p[3]=255;}
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage result;
        ok=graph.process(f,0,true,out,1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result);
        // Image readback helper requires COMMON; graph internals are SRVs.
        auto rawStates=[&](bool restore){
            uint32_t readSlot=0;auto* readList=ring.acquireNext(readSlot,st);if(!readList)return false;
            for(auto* resource:{rawProxy,rawNeural}){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,restore?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,restore?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON};readList->ResourceBarrier(1,&b);}
            return ring.submitAndSignal(readSlot);
        };
        sink::RgbaImage proxyPixels,neuralPixels;
        ok=ok&&rawProxy&&rawNeural&&rawStates(false);
        bool rawPrepared=ok;
        ok=ok&&rawProxy&&rawNeural&&sink::readRgba8(ctx,ring,rawProxy,proxyPixels)&&sink::readRgba8(ctx,ring,rawNeural,neuralPixels);
        if(rawPrepared)ok=rawStates(true)&&ok;
        ok=ok&&proxyPixels.width==width&&proxyPixels.height==height&&neuralPixels.width==width&&neuralPixels.height==height;
        int rawSourceError=0,rawBaselineError=0,rawAllProxyError=0;bool contractMatch=false;
        int sourceError=0,baselineError=0;uint64_t sourcePixels=0,baselinePixels=0;
        if(ok){ok=result.width==width&&result.height==height;if(!mode){baseline=result;rawBaseline=neuralPixels;}
            for(unsigned y=0;y<height&&ok;++y)for(unsigned x=0;x<width;++x){
                bool rect=x>=64&&x<192&&y>=64&&y<192;
                bool sourceExpected=mode==1||mode==6||(mode==3&&!rect)||(mode==7&&rect);
                sourcePixels+=sourceExpected;baselinePixels+=!sourceExpected;
                for(unsigned c=0;c<3;++c){auto i=(size_t(y)*width+x)*4+c;auto original=f->data[0][size_t(y)*f->linesize[0]+x*4+c];
                    rawAllProxyError=std::max(rawAllProxyError,std::abs(int(neuralPixels.pixels[i])-int(proxyPixels.pixels[i])));
                    if(!mode)changed+=result.pixels[i]!=original;
                    else if(sourceExpected){sourceError=std::max(sourceError,std::abs(int(result.pixels[i])-int(original)));rawSourceError=std::max(rawSourceError,std::abs(int(neuralPixels.pixels[i])-int(proxyPixels.pixels[i])));}
                    else {baselineError=std::max(baselineError,std::abs(int(result.pixels[i])-int(baseline.pixels[i])));rawBaselineError=std::max(rawBaselineError,std::abs(int(neuralPixels.pixels[i])-int(rawBaseline.pixels[i])));}
                }
            }
            contractMatch=rawSourceError==0&&rawBaselineError==0&&baselineError==0;matrixMatch=matrixMatch&&contractMatch;
            ok=ok&&graph.metrics().nrEvaluateCount==1;
            ok=sink::saveImage((directory/("optional-nr-"+std::to_string(mode)+".png")).wstring(),result)&&ok;
        }
        std::cout<<"NR_OPTIONAL format="<<(rgba?"RGBA8":"R8")<<" mode="<<mode<<" finalSourceError8="<<sourceError<<" allRawProxyError8="<<rawAllProxyError<<" contractMatch="<<contractMatch<<" rawProxyError8="<<rawSourceError<<" rawBaselineError8="<<rawBaselineError<<" baselineError8="<<baselineError<<" sourcePixels="<<sourcePixels<<" baselinePixels="<<baselinePixels<<" positiveControl="<<changed<<" nr="<<graph.metrics().nrEvaluateCount<<" pass="<<ok<<std::endl;
        out={};ring.drainQueue();graph.shutdown();
        pipeline::ComPtr<ID3D12InfoQueue> iq;uint64_t debugErrors=0;
        if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&iq))))ok=false;
        else for(UINT64 i=0;i<iq->GetNumStoredMessages();++i){SIZE_T size=0;iq->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(FAILED(iq->GetMessage(i,message,&size))){ok=false;break;}if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++debugErrors;std::cerr<<message->pDescription<<std::endl;}}
        ok=ok&&!debugErrors;std::cout<<"NR_OPTIONAL_DEBUG mode="<<mode<<" errors="<<debugErrors<<" pass="<<ok<<std::endl;iq.Reset();
        av_frame_free(&f);mask.Reset();upload.Reset();ring.shutdown();ctx.shutdown();
    }
    std::cout<<"NR_OPTIONAL_MATRIX executionPass="<<ok<<" expectedContractMatch="<<matrixMatch<<std::endl;
    return ok&&changed>100&&matrixMatch;
}
bool tiledColors(unsigned width,unsigned height,unsigned count,bool sparse,const std::filesystem::path& directory){
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return false;
    engine::EnhancementSettings settings;settings.additionalColorCount=count?count-1:0;
    for(unsigned i=0;i<count;++i){auto& c=i?settings.additionalColors[i-1]:settings.color;
        c.enabled=!sparse||(i!=0&&i!=2);c.exposure=.1f*float(i+1);c.contrast=float(i)*3;}
    pipeline::EnhanceGraphDesc desc;desc.rgbInput=true;desc.stillImage=true;desc.noFeatures=true;desc.noNgx=true;desc.outputDitherStep=0;
    engine::StageRequest request;request.width=width;request.height=height;request.stillImage=true;
    engine::describeStages(request,settings,desc);
    sink::RgbaImage source;source.width=width;source.height=height;source.pixels.resize(size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* p=source.pixels.data()+(size_t(y)*width+x)*4;
        p[0]=uint8_t(24+(x*7+y*11)%144);p[1]=uint8_t(32+(x*3+y*5)%128);p[2]=uint8_t(40+(x*13+y*7)%120);p[3]=255;}
    const auto freeFrame=[](AVFrame* p){av_frame_free(&p);};
    std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);if(!frame)return false;
    frame->width=width;frame->height=height;frame->format=AV_PIX_FMT_RGBA;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
    if(av_frame_get_buffer(frame.get(),32)<0)return false;
    for(unsigned y=0;y<height;++y)memcpy(frame->data[0]+size_t(y)*frame->linesize[0],source.pixels.data()+size_t(y)*width*4,size_t(width)*4);
    sink::RgbaImage reference,result;pipeline::EnhanceGraph::FrameOutputs out;
    auto graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring);
    bool direct=graph->initialize(desc)&&graph->createViews()&&graph->process(frame.get(),0,true,out,1)&&
        sink::readRgba8(ctx,ring,graph->videoFrameResource(out.videoSlot),reference);
    out={};ring.drainQueue();graph->shutdown();graph.reset();
    std::atomic<bool> cancel=false;engine::TiledImageProcessor::Stats stats;
    const bool processed=engine::TiledImageProcessor::process(ctx,ring,source,result,desc,cancel,stats);
    bool ok=direct&&processed&&result.width==width&&result.height==height&&result.pixels.size()==source.pixels.size()&&reference.pixels.size()==source.pixels.size()&&stats.tiles==2&&stats.nrEvaluations==0;
    int maxError=0;size_t changed=0;
    if(direct&&reference.pixels.size()==source.pixels.size())for(size_t i=0;i<source.pixels.size();++i)
        if(i%4!=3&&std::abs(int(reference.pixels[i])-int(source.pixels[i]))>2)++changed;
    if(ok){for(size_t i=0;i<result.pixels.size();++i)maxError=std::max(maxError,std::abs(int(result.pixels[i])-int(reference.pixels[i])));
        ok=maxError<=1&&(count?changed>100:changed==0);
        const auto prefix="tiled-color-"+std::to_string(width)+"x"+std::to_string(height)+"-"+std::to_string(count)+(sparse?"-sparse":"");
        ok=sink::saveImage((directory/(prefix+"-full.png")).wstring(),reference)&&sink::saveImage((directory/(prefix+"-tiles.png")).wstring(),result)&&ok;}
    ring.drainQueue();pipeline::ComPtr<ID3D12InfoQueue> info;uint64_t errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info))))ok=false;
    else for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;
        if(FAILED(info->GetMessage(i,nullptr,&bytes))){ok=false;break;}
        std::vector<uint8_t> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if(FAILED(info->GetMessage(i,message,&bytes))){ok=false;break;}
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<'\n';}}
    ok=ok&&errors==0;
    std::cout<<"TILED_COLOR width="<<width<<" height="<<height<<" instances="<<count<<" sparse="<<sparse
        <<" direct="<<direct<<" tiled="<<processed<<" tiles="<<stats.tiles<<" maxError8="<<maxError<<" changed="<<changed<<" debugErrors="<<errors<<" pass="<<ok<<std::endl;
    info.Reset();ring.shutdown();ctx.shutdown();return ok;
}
bool tiledColorMatrix(const std::filesystem::path& directory){
    bool ok=true;for(unsigned count:{0u,1u,2u,6u})ok=tiledColors(1301,17,count,false,directory)&&ok;
    ok=tiledColors(17,1301,6,false,directory)&&ok;
    return tiledColors(1301,17,6,true,directory)&&ok;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2&&!(argc==3&&(std::wstring(argv[1])==L"--nr-optional"||std::wstring(argv[1])==L"--nr-optional-rgba"||std::wstring(argv[1])==L"--tiled-color")))return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::filesystem::path directory(argv[argc-1]);std::filesystem::create_directories(directory);
    if(argc==3){bool result=std::wstring(argv[1])==L"--tiled-color"?tiledColorMatrix(directory):nrOptionalResourcePixels(directory,std::wstring(argv[1])==L"--nr-optional-rgba");CoUninitialize();return result?0:1;}
    if(!tiledColorMatrix(directory)){CoUninitialize();return 1;}
    bool ok=true;for(auto e:{pipeline::Extent{1,1},{257,513},{97,9001},{4097,257},{257,4097}}){if(!run(e.width,e.height,false,directory)){ok=false;break;}}
    if(ok)ok=run(257,513,true,directory);
    if(ok)ok=run(97,9001,true,directory);
    if(ok)ok=tiled(17001,17,false,directory);
    if(ok)ok=tiled(17,17001,false,directory);
    if(ok)ok=tiled(97,17001,true,directory);
    if(ok)ok=tiled(2561,2561,false,directory);
    if(ok)ok=tiled(2561,2561,true,directory);
    if(ok)ok=codecBands(directory);
    if(ok)ok=colorFallback();
    if(ok)ok=srPixels(256,256,512,512,directory);
    if(ok)ok=srPixels(256,128,256,256,directory);
    if(ok)ok=srPixels(256,256,256,256,directory);
    if(ok)ok=captureRgb(false)&&captureRgb(true);
    if(ok)ok=protectionPixels();
    if(ok)ok=tiled(2561,2561,true,directory,1);
    if(ok)ok=tiled(2561,2561,true,directory,2);
    if(ok)ok=protectionMoving(directory);
    CoUninitialize();return ok?0:1;
}
