#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <memory>
#include <array>
#include <string>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

// Matched deterministic frames, actual SDK evaluation and output pixels.
// Readbacks are restricted to this test; these are not playback timings.
static int referenceTest(const std::filesystem::path& directory,const std::wstring& path,int seconds,bool moving,bool zeroMotion,int inputHeight,int outputHeight,bool models=false){
    using namespace veyra;
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    struct Case{const char* name;unsigned quality;bool reference,enabled;unsigned model=0;};
    bool ok=true;
    const std::vector<Case> cases=models
        ?std::vector<Case>{{"dlss-K",0,false,true,0},{"dlss-M",0,false,true,1},{"dlss-L",0,false,true,2}}
        :std::vector<Case>{{"reference",4,true,false},{"bilinear",4,false,false},
            {"vsr1",1,false,true},{"vsr4",4,false,true},{"dlss",0,false,true},{"fsr",5,false,true}};
    for(const auto test:cases){
        source::MediaFileSource media;source::SourceOpenDesc open;open.path=path;open.preferHardwareDecode=false;
        if(!media.open(open)||!media.seek({seconds,1}))return 2;
        pipeline::FramePacket packet;const AVFrame* natural=nullptr;
        do{
            if(media.read(packet,&natural)!=source::SourceReadStatus::Frame||!natural||natural->width!=3840||natural->height!=2160)return 2;
        }while(packet.pts.toDouble()+1e-6<seconds);
        const double startPts=packet.pts.toDouble();
        engine::EnhancementSettings settings;settings.nr=false;settings.sr=!test.reference;settings.multiplier=1;
        settings.videoSrQuality=test.quality;settings.dlssSrModel=test.model;settings.srTarget=outputHeight==1440?pipeline::SrTarget::Qhd:pipeline::SrTarget::Uhd4K;
        settings.nrPolicy=pipeline::NrSizePolicy::Native;settings.srMotion=zeroMotion?engine::MotionSource::Zero:engine::MotionSource::OpticalFlow;
        engine::StageRequest request;request.height=test.reference?outputHeight:inputHeight;request.width=request.height*16/9;request.sr=settings.sr;
        pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
        desc.enableSr=test.enabled;desc.rgbInput=true;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        pipeline::EnhanceGraph graph(ctx,ring);if(!graph.initialize(desc)||!graph.createViews())return 2;
        graph.recordGpuTimings();std::ofstream timings(directory/(std::string(test.name)+"-gpu.csv"));timings<<"frame,srMs\n";
        auto freeFrame=[](AVFrame* frame){av_frame_free(&frame);};
        std::unique_ptr<AVFrame,decltype(freeFrame)> owned(av_frame_alloc(),freeFrame);auto* frame=owned.get();
        if(!frame)return 2;frame->format=AV_PIX_FMT_RGBA;frame->width=request.width;frame->height=request.height;
        if(av_frame_get_buffer(frame,32)<0)return 2;
        auto* scaler=sws_getContext(natural->width,natural->height,AVPixelFormat(natural->format),frame->width,frame->height,AV_PIX_FMT_RGBA,SWS_LANCZOS,nullptr,nullptr,nullptr);
        if(!scaler)return 2;
        // swscale defaults to BT.601; explicitly retain the actual video's
        // matrix and range when making the controlled low-resolution input.
        const int matrix=(natural->colorspace==AVCOL_SPC_BT709||
            (natural->colorspace==AVCOL_SPC_UNSPECIFIED&&natural->height>=720))?SWS_CS_ITU709:
            natural->colorspace==AVCOL_SPC_BT2020_NCL?SWS_CS_BT2020:SWS_CS_ITU601;
        const int* coefficients=sws_getCoefficients(matrix);
        if(sws_setColorspaceDetails(scaler,coefficients,natural->color_range==AVCOL_RANGE_JPEG,
            coefficients,1,0,1<<16,1<<16)<0){sws_freeContext(scaler);return 2;}
        bool passed=true;uint64_t firstEvaluations=0;double lastPts=0;
        for(unsigned i=0;i<32&&passed;++i){
            if(i&&moving&&(media.read(packet,&natural)!=source::SourceReadStatus::Frame||!natural)){passed=false;break;}
            if(sws_scale(scaler,natural->data,natural->linesize,0,natural->height,frame->data,frame->linesize)!=frame->height){passed=false;break;}
            frame->color_range=AVCOL_RANGE_JPEG;frame->color_primaries=natural->color_primaries;frame->color_trc=natural->color_trc;frame->colorspace=AVCOL_SPC_RGB;
            pipeline::EnhanceGraph::FrameOutputs out;
            lastPts=moving?packet.pts.toDouble():seconds+i/30.0;
            passed=graph.process(frame,lastPts*1000,i==0,out,i+1)&&ring.waitIdle();
            for(const auto& t:graph.takeGpuTimings()){const auto& v=t.gpu[size_t(diagnostics::GpuStage::Sr)];if(v.milliseconds)timings<<t.identity.sourceFrameId<<','<<*v.milliseconds<<'\n';}
            if(i==0)firstEvaluations=graph.metrics().srEvaluateCount;
            if(passed&&(i==0||i==7||i==31)){
                sink::RgbaImage image;passed=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image)&&
                    sink::saveImage((directory/(std::string(test.name)+"-f"+std::to_string(i+1)+".png")).wstring(),image,false);
            }
        }
        sws_freeContext(scaler);
        const auto evaluations=graph.metrics().srEvaluateCount;
        passed=passed&&evaluations==(test.enabled?32u:0u);ok=passed&&ok;
        std::cout<<"SR_REFERENCE case="<<test.name<<" moving="<<moving<<" zeroMotion="<<zeroMotion<<" source="<<request.width<<'x'<<request.height
                 <<" target="<<desc.workWidth<<'x'<<desc.workHeight<<" sourceStartPts="<<startPts<<" sourceEndPts="<<packet.pts.toDouble()
                 <<" firstEvaluations="<<firstEvaluations<<" evaluations="<<evaluations<<" passed="<<passed<<std::endl;
        ring.drainQueue();graph.shutdown();media.close();
    }
    ring.shutdown();ctx.shutdown();return ok?0:1;
}


// ---- double RTX Video SR and supersampling (2026-10-28) ------------------------
// Base SR, an exact area reduction of the first AI result, then a second SR with
// real SDK evaluations: sizes, the reduction against a CPU area average of the
// graph's own first result, and both HDR restores against the bases they must
// use. Supersampling: one pass shrunk into the output, against a CPU area average
// of the same graph's larger result. Readbacks are restricted to this test.
namespace stage {
using namespace veyra;
// Copy a texture in a known state to the CPU and restore that state.
bool read(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,ID3D12Resource* tex,D3D12_RESOURCE_STATES state,unsigned bytesPerPixel,sink::RgbaImage& img){
    if(!tex)return false;
    const auto d=tex->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;
    ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&total);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;
    buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if(FAILED(ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
    Status st=Status::Ok;const auto slot=ring.slotCount()-1;auto* list=ring.acquire(slot,st);if(!list)return false;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={tex,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=tex;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    uint8_t* data=nullptr;D3D12_RANGE range{0,SIZE_T(total)};
    if(FAILED(readback->Map(0,&range,reinterpret_cast<void**>(&data))))return false;
    img.width=uint32_t(d.Width);img.height=d.Height;img.pixels.resize(size_t(img.width)*img.height*bytesPerPixel);
    for(UINT y=0;y<img.height;++y)std::memcpy(img.pixels.data()+size_t(y)*img.width*bytesPerPixel,data+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(img.width)*bytesPerPixel);
    D3D12_RANGE none{0,0};readback->Unmap(0,&none);return true;
}
double decode(uint8_t v){const double c=v/255.0;return c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4);}
double encode(double c){c=std::clamp(c,0.0,1.0);return 255.0*(c<=0.0031308?c*12.92:1.055*std::pow(c,1.0/2.4)-0.055);}
float half(const sink::RgbaImage& img,size_t i){uint16_t h;std::memcpy(&h,img.pixels.data()+i*2,2);return DirectX::PackedVector::XMConvertHalfToFloat(h);}
// Natural-ish detail at every frequency: zone plate, hard blocks, thin lines and noise.
sink::RgbaImage pattern(unsigned w,unsigned h){
    sink::RgbaImage img;img.width=w;img.height=h;img.pixels.resize(size_t(w)*h*4);uint32_t seed=12345;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const double u=double(x)/w,v=double(y)/h;seed=seed*1664525u+1013904223u;const double noise=((seed>>24)&255)/255.0-0.5;
        const double zone=0.5+0.5*std::sin((u*u+v*v)*w*0.35),block=((x/37+y/53)%2)?0.85:0.15;
        double r=0.15+0.55*u+0.2*zone*(v<0.5)+0.06*noise,g=0.2+0.5*v+0.25*block*(u>0.5)+0.06*noise,b=0.3+0.3*zone*(u<0.5)+0.3*block*(v>0.5)+0.06*noise;
        if((x+y)%61<2||(x>w/2&&(x+2*y)%47==0))r=g=b=0.95;
        auto* p=&img.pixels[(size_t(y)*w+x)*4];
        p[0]=uint8_t(std::clamp(r,0.0,1.0)*255+0.5);p[1]=uint8_t(std::clamp(g,0.0,1.0)*255+0.5);p[2]=uint8_t(std::clamp(b,0.0,1.0)*255+0.5);p[3]=255;
    }
    return img;
}
struct FreeFrame{void operator()(AVFrame* f)const{av_frame_free(&f);}};
using Frame=std::unique_ptr<AVFrame,FreeFrame>;
Frame sdrFrame(const sink::RgbaImage& img){
    Frame f(av_frame_alloc());if(!f)return {};
    f->format=AV_PIX_FMT_RGBA;f->width=int(img.width);f->height=int(img.height);
    if(av_frame_get_buffer(f.get(),32)<0)return {};
    for(unsigned y=0;y<img.height;++y)std::memcpy(f->data[0]+size_t(y)*f->linesize[0],img.pixels.data()+size_t(y)*img.width*4,size_t(img.width)*4);
    f->color_range=AVCOL_RANGE_JPEG;f->colorspace=AVCOL_SPC_RGB;f->color_trc=AVCOL_TRC_IEC61966_2_1;f->color_primaries=AVCOL_PRI_BT709;
    return f;
}
// The same picture as P010 PQ BT.2020: 0..200 nits with 1000-nit lines.
Frame hdrFrame(const sink::RgbaImage& img){
    Frame f(av_frame_alloc());if(!f)return {};
    f->format=AV_PIX_FMT_P010;f->width=int(img.width);f->height=int(img.height);
    f->color_range=AVCOL_RANGE_MPEG;f->colorspace=AVCOL_SPC_BT2020_NCL;f->color_primaries=AVCOL_PRI_BT2020;f->color_trc=AVCOL_TRC_SMPTE2084;
    if(av_frame_get_buffer(f.get(),32)<0)return {};
    const auto pq=[](double nits){const double y=std::pow(std::clamp(nits/10000.0,0.0,1.0),2610.0/16384.0);
        return std::pow((3424.0/4096.0+2413.0/128.0*y)/(1+2392.0/128.0*y),2523.0/32.0);};
    std::vector<double> yv(size_t(img.width)*img.height),cbv(yv.size()),crv(yv.size());
    for(size_t i=0;i<yv.size();++i){
        const auto* p=&img.pixels[i*4];const bool bright=p[0]==242&&p[1]==242&&p[2]==242;
        const double R=pq(bright?1000:decode(p[0])*200),G=pq(bright?1000:decode(p[1])*200),B=pq(bright?1000:decode(p[2])*200);
        const double Y=0.2627*R+0.6780*G+0.0593*B;yv[i]=Y;cbv[i]=(B-Y)/1.8814;crv[i]=(R-Y)/1.4746;
    }
    const auto q=[](double v){return uint16_t(std::clamp(std::lround(v),0l,1023l)<<6);};
    for(unsigned y=0;y<img.height;++y)for(unsigned x=0;x<img.width;++x)
        reinterpret_cast<uint16_t*>(f->data[0]+size_t(y)*f->linesize[0])[x]=q(64+876*yv[size_t(y)*img.width+x]);
    for(unsigned y=0;y<img.height/2;++y)for(unsigned x=0;x<img.width/2;++x){
        double cb=0,cr=0;for(unsigned k=0;k<4;++k){const size_t i=size_t(y*2+k/2)*img.width+x*2+k%2;cb+=cbv[i]/4;cr+=crv[i]/4;}
        auto* row=reinterpret_cast<uint16_t*>(f->data[1]+size_t(y)*f->linesize[1]);row[x*2]=q(512+896*cb);row[x*2+1]=q(512+896*cr);
    }
    return f;
}
struct Run{bool ok=false;sink::RgbaImage output,srInput,firstLinear;double srMs=-1;uint64_t evaluations=0;pipeline::Extent srIn{},srOut{},work{},first{};bool enabled=false;};
Run run(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,const AVFrame* frame,bool hdr,const engine::EnhancementSettings& settings,
        bool readSrInput,bool still=false,bool uncompressed=false,unsigned frames=6,bool readFirst=false){
    engine::StageRequest request;request.width=unsigned(frame->width);request.height=unsigned(frame->height);request.sr=settings.sr;
    request.stillImage=still;request.uncompressedSource=uncompressed;request.nr=settings.nr;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
    desc.rgbInput=!hdr;desc.hdrInput=hdr;desc.hdrOutput=hdr;desc.outputDitherStep=0;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    Run r;r.srIn={desc.srInputWidth,desc.srInputHeight};r.srOut={desc.srOutputWidth,desc.srOutputHeight};r.work={desc.workWidth,desc.workHeight};r.first={desc.firstSrWidth,desc.firstSrHeight};
    if(!desc.fixedExecutionPlanError.empty()){std::cout<<"plan error: "<<desc.fixedExecutionPlanError<<std::endl;return r;}
    pipeline::EnhanceGraph graph(ctx,ring);
    if(!graph.initialize(desc)||!graph.createViews()){graph.shutdown();return r;}
    graph.recordGpuTimings();std::vector<double> ms;pipeline::EnhanceGraph::FrameOutputs out;bool ok=true;
    for(unsigned i=0;i<frames&&ok;++i){
        out={};ok=graph.process(frame,i*33.333333,i==0,out,i+1)&&ring.waitIdle();
        for(const auto& t:graph.takeGpuTimings()){const auto& v=t.gpu[size_t(diagnostics::GpuStage::Sr)];if(v.milliseconds&&t.identity.sourceFrameId>2)ms.push_back(*v.milliseconds);}
    }
    if(ok){auto* tex=graph.videoFrameResource(out.videoSlot);
        ok=tex&&read(ctx,ring,tex,D3D12_RESOURCE_STATE_COMMON,tex->GetDesc().Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4,r.output);}
    // The single-step and prescale SDR paths leave the encoded SR input in COMMON.
    if(ok&&readSrInput)ok=read(ctx,ring,graph.diagnosticVideoSrInput(),hdr?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,4,r.srInput);
    if(ok&&readFirst)ok=read(ctx,ring,graph.diagnosticFirstSrLinear(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,8,r.firstLinear);
    r.evaluations=graph.metrics().srEvaluateCount;r.enabled=graph.srEnabled();
    if(!ms.empty()){std::sort(ms.begin(),ms.end());r.srMs=ms[ms.size()/2];}
    r.ok=ok;out={};ring.drainQueue();graph.shutdown();return r;
}
engine::EnhancementSettings vsr(unsigned quality,pipeline::SrTarget target){
    engine::EnhancementSettings s;s.nr=false;s.sr=quality!=0;s.multiplier=1;s.videoSrQuality=quality?quality:engine::kDefaultVideoSrQuality;
    s.srTarget=target;s.srMotion=engine::MotionSource::Zero;s.hdrOutputMode=engine::HdrOutputMode::ScRgb;
    return s;
}
// Base SR to `target`, its result reduced to `reduce`, then a second SR to `second`.
engine::EnhancementSettings twoPass(unsigned quality,pipeline::SrTarget target,pipeline::SrPrescale reduce,pipeline::SrTarget second,unsigned secondQuality){
    auto s=vsr(quality,target);s.dualSrEnabled=true;s.srPrescale=reduce;s.dualSrTarget=second;s.dualSrQuality=secondQuality;
    return s;
}
// CPU area average of an image by an integer factor, SDR (sRGB) or scRGB FP16.
std::vector<double> shrink(const sink::RgbaImage& img,unsigned factor,bool hdr){
    const unsigned w=img.width/factor,h=img.height/factor;std::vector<double> out(size_t(w)*h*3);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<3;++c){
        double sum=0;for(unsigned j=0;j<factor;++j)for(unsigned i=0;i<factor;++i){
            const size_t p=(size_t(y*factor+j)*img.width+x*factor+i)*4+c;sum+=hdr?half(img,p):decode(img.pixels[p]);}
        out[(size_t(y)*w+x)*3+c]=sum/(factor*factor);
    }
    return out;
}
// CPU exact area resample to linear RGB with NrDownsample's weights; fp16 reads
// linear/scRGB FP16 texels, otherwise sRGB bytes.
std::vector<double> area(const sink::RgbaImage& img,unsigned w,unsigned h,bool fp16){
    std::vector<double> out(size_t(w)*h*3);const double sx=double(img.width)/w,sy=double(img.height)/h;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const double x0=x*sx,x1=(x+1)*sx,y0=y*sy,y1=(y+1)*sy;double sum[3]{},weights=0;
        for(unsigned j=unsigned(y0);j<unsigned(std::ceil(y1))&&j<img.height;++j)for(unsigned i=unsigned(x0);i<unsigned(std::ceil(x1))&&i<img.width;++i){
            const double weight=(std::min(x1,i+1.0)-std::max(x0,double(i)))*(std::min(y1,j+1.0)-std::max(y0,double(j)));
            if(weight<=0)continue;weights+=weight;
            for(unsigned c=0;c<3;++c){const size_t p=(size_t(j)*img.width+i)*4+c;sum[c]+=weight*(fp16?half(img,p):decode(img.pixels[p]));}
        }
        for(unsigned c=0;c<3;++c)out[(size_t(y)*w+x)*3+c]=sum[c]/weights;
    }
    return out;
}
std::vector<double> linear(const sink::RgbaImage& img){
    std::vector<double> out(size_t(img.width)*img.height*3);
    for(size_t p=0;p<size_t(img.width)*img.height;++p)for(unsigned c=0;c<3;++c)out[p*3+c]=half(img,p*4+c);
    return out;
}
// HdrVideoSrRestore's bilinear base sample for output pixel (x,y) of a W x H output.
double bilinear(const std::vector<double>& img,unsigned w,unsigned h,unsigned W,unsigned H,unsigned x,unsigned y,unsigned c){
    const double px=(x+0.5)*double(w)/W-0.5,py=(y+0.5)*double(h)/H-0.5;const int qx=int(std::floor(px)),qy=int(std::floor(py));
    const double fx=px-qx,fy=py-qy;double v=0;
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){const int sx=std::clamp(qx+i,0,int(w)-1),sy=std::clamp(qy+j,0,int(h)-1);
        v+=img[(size_t(sy)*w+sx)*3+c]*(i?fx:1-fx)*(j?fy:1-fy);}
    return v;
}
// SDR input/output transparency (the A/B baseline) and both HDR restores of
// double SR. HdrRestore returns its base plus a delta clamped to +-0.5*203/80,
// so every output lies within that bound of the base it sampled: the first
// restore must sample the source, the second the area reduction of the first
// AI result. A full-size base would break the bound on the thin 1000-nit lines.
int restore(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    bool ok=true;
    const auto check=[&](bool pass,const std::string& label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;ok=ok&&pass;};
    using pipeline::SrTarget;using pipeline::SrPrescale;
    {
        const auto uhd=pattern(3840,2160);const auto frame=sdrFrame(uhd);if(!frame)return 2;
        const auto off=run(ctx,ring,frame.get(),false,vsr(0,SrTarget::Uhd4K),false);
        int worst=0;for(size_t i=0;off.ok&&i<uhd.pixels.size();++i)if(i%4!=3)worst=std::max(worst,std::abs(int(off.output.pixels[i])-int(uhd.pixels[i])));
        check(off.ok&&off.output.width==3840&&worst<=1,"SDR input/output conversion is transparent (max "+std::to_string(worst)+")");
    }
    const auto hdr=hdrFrame(pattern(2560,1440));if(!hdr)return 2;
    const auto source=run(ctx,ring,hdr.get(),true,vsr(0,SrTarget::Uhd4K),false);
    const auto two=run(ctx,ring,hdr.get(),true,twoPass(2,SrTarget::Uhd4K,SrPrescale::P1080,SrTarget::Uhd4K,2),false,false,false,3,true);
    check(source.ok&&source.output.width==2560&&two.ok&&two.evaluations==6&&two.firstLinear.width==3840&&two.output.width==3840,
          "HDR double SR: 2560x1440 -> base 3840x2160 -> 1920x1080 -> second 3840x2160");
    if(source.ok&&two.ok&&source.output.width==2560&&two.firstLinear.width==3840&&two.output.width==3840){
        const auto base=linear(source.output),reduced=area(two.firstLinear,1920,1080,true);
        const double bound=0.5*203.0/80.0+0.05;double first=0,second=0;size_t bad=0;
        for(unsigned y=0;y<2160;++y)for(unsigned x=0;x<3840;++x)for(unsigned c=0;c<3;++c){
            const size_t i=(size_t(y)*3840+x)*4+c;const double f=half(two.firstLinear,i),o=half(two.output,i);
            if(!std::isfinite(f)||!std::isfinite(o)){++bad;continue;}
            first=std::max(first,std::abs(f-bilinear(base,2560,1440,3840,2160,x,y,c)));
            second=std::max(second,std::abs(o-bilinear(reduced,1920,1080,3840,2160,x,y,c)));
        }
        check(!bad&&first<=bound,"HDR first restore uses the source base ("+std::to_string(first)+")");
        check(!bad&&second<=bound,"HDR second restore uses the reduced first result ("+std::to_string(second)+")");
    }
    ring.shutdown();ctx.shutdown();std::cout<<"DUAL_RESTORE passed="<<ok<<std::endl;return ok?0:1;
}
int dual(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    bool ok=true;const auto check=[&](bool pass,const std::string& name){std::cout<<(pass?"PASS ":"FAIL ")<<name<<std::endl;ok=ok&&pass;};
    const auto image=pattern(2560,1440);const auto sdr=sdrFrame(image),hdr=hdrFrame(image);
    if(!sdr||!hdr)return 2;
    auto s=vsr(1,pipeline::SrTarget::Uhd4K);s.dualSrEnabled=true;s.srPrescale=pipeline::SrPrescale::P1080;s.dualSrTarget=pipeline::SrTarget::Uhd4K;s.dualSrQuality=2;
    const auto two=run(ctx,ring,sdr.get(),false,s,true,false,false,3,true);
    check(two.ok&&two.evaluations==6&&two.firstLinear.width==3840&&two.srInput.width==1920&&two.work==pipeline::Extent{3840,2160},"two independent AI evaluations per frame, with first 4K output retained");
    if(two.ok){
        const auto expected=shrink(two.firstLinear,2,true);int worst=0;
        for(size_t p=0;p<size_t(1920)*1080;++p)for(unsigned c=0;c<3;++c)
            worst=std::max(worst,std::abs(int(two.srInput.pixels[p*4+c])-int(std::lround(encode(expected[p*3+c])))));
        check(worst<=2,"second input is the area reduction of the FIRST AI result, max difference "+std::to_string(worst));
    }
    s.srTarget=pipeline::SrTarget::Uhd8K;
    const auto eightThenFour=run(ctx,ring,sdr.get(),false,s,false,false,false,2);
    check(eightThenFour.ok&&eightThenFour.evaluations==4&&eightThenFour.output.width==3840,"base 8K -> reduce 1080p -> second 4K actually runs twice");
    s.srTarget=pipeline::SrTarget::Uhd4K;
    const auto matched=sdrFrame(pattern(1920,1080));
    const auto shared=run(ctx,ring,matched.get(),false,s,false,false,false,2);
    check(shared.ok&&shared.evaluations==4&&shared.output.width==3840,"equal input/output create keys still execute two AI passes without duplicate ownership");
    const auto h=run(ctx,ring,hdr.get(),true,s,false,false,false,2,true);
    bool finite=h.ok;for(size_t i=0;finite&&i<h.output.pixels.size()/2;++i)finite=std::isfinite(half(h.output,i));
    check(finite&&h.evaluations==4&&h.firstLinear.width==3840,"HDR restores the first result before the second reduction and evaluation");
    s.dualSrEnabled=false;s.srTarget=pipeline::SrTarget::Uhd8K;s.srOutput=pipeline::SrOutput::Target;
    const auto normal=run(ctx,ring,sdr.get(),false,s,false,false,false,2);
    check(normal.ok&&normal.evaluations==2&&normal.output.width==7680,"dual OFF preserves normal 8K output and ignores the kept reduction");
    ring.shutdown();ctx.shutdown();std::cout<<"DUAL_SR_RESULT passed="<<ok<<std::endl;return ok?0:1;
}
// A final output smaller than the source, also with native NR first at the
// source size, and a non-integer reduction of the first AI result.
int boundaries(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    bool ok=true;
    const auto check=[&](bool pass,const std::string& label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;ok=ok&&pass;};
    using pipeline::SrTarget;using pipeline::SrPrescale;using pipeline::Extent;
    const auto uhd=sdrFrame(pattern(3840,2160)),qhd=sdrFrame(pattern(2560,1440));if(!uhd||!qhd)return 2;
    auto settings=twoPass(1,SrTarget::Uhd4K,SrPrescale::P720,SrTarget::Qhd,1);
    const auto down=run(ctx,ring,uhd.get(),false,settings,false,false,false,3);
    check(down.ok&&down.enabled&&down.srIn==Extent{1280,720}&&down.work==Extent{2560,1440}&&down.evaluations==6,
          "4K -> base 4K -> 720p -> second 2K: the final output may be smaller than the source");
    settings.nr=true;settings.lowLatency=true;settings.nrPolicy=pipeline::NrSizePolicy::Native;
    settings.nrTemporal=false;settings.nrMotion=engine::MotionSource::Zero;
    const auto nrFirst=run(ctx,ring,uhd.get(),false,settings,false,false,false,3);
    check(nrFirst.ok&&nrFirst.enabled&&nrFirst.work==Extent{2560,1440}&&nrFirst.evaluations==6,
          "native NR first at the 4K source still runs with a 2K double SR output");
    const auto manual=run(ctx,ring,qhd.get(),false,twoPass(1,SrTarget::Uhd4K,pipeline::srPrescaleForPercent(75),SrTarget::Uhd4K,1),true,false,false,3,true);
    check(manual.ok&&manual.srIn==Extent{2880,1620}&&manual.firstLinear.width==3840&&manual.work==Extent{3840,2160}&&manual.evaluations==6,
          "75 % of the 4K first result is 2880x1620");
    if(manual.ok&&manual.srInput.width==2880&&manual.firstLinear.width==3840){
        const auto expected=area(manual.firstLinear,2880,1620,true);int worst=0;size_t over=0;
        for(size_t p=0;p<size_t(2880)*1620;++p)for(unsigned c=0;c<3;++c){
            const int d=std::abs(int(manual.srInput.pixels[p*4+c])-int(std::lround(encode(expected[p*3+c]))));worst=std::max(worst,d);over+=d>1;}
        check(worst<=2&&over*1000<size_t(2880)*1620*3,"the non-integer reduction equals the exact area average (max "+std::to_string(worst)+", >1: "+std::to_string(over)+")");
    }
    ring.shutdown();ctx.shutdown();std::cout<<"DUAL_BOUNDARIES passed="<<ok<<std::endl;return ok?0:1;
}
// Every RTX Video SR quality with each larger second target, a 720p reduction
// and a 21:9 source, through real SDK evaluations of both passes.
int capabilities(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    using pipeline::SrTarget;using pipeline::SrPrescale;using pipeline::Extent;
    const auto fhd=sdrFrame(pattern(1920,1080)),wide=sdrFrame(pattern(3840,1600));if(!fhd||!wide)return 2;
    bool ok=true;unsigned cases=0;
    for(unsigned quality=1;quality<=4;++quality){
        for(auto target:{SrTarget::Uhd5K,SrTarget::Uhd6K,SrTarget::Uhd7K,SrTarget::Uhd8K}){
            const auto result=run(ctx,ring,fhd.get(),false,twoPass(quality,SrTarget::Uhd4K,SrPrescale::P1080,target,quality),false,false,false,2);
            const bool passed=result.ok&&result.first==Extent{3840,2160}&&result.srIn==Extent{1920,1080}&&
                result.srOut==pipeline::srTargetExtent(target)&&result.work==pipeline::srTargetExtent(target)&&result.evaluations==4;
            std::cout<<"DUAL_CAPABILITY q="<<quality<<" second="<<uint32_t(target)<<" passed="<<passed<<std::endl;ok=ok&&passed;++cases;
        }
        for(bool isWide:{false,true}){
            const auto result=run(ctx,ring,isWide?wide.get():fhd.get(),false,
                twoPass(quality,SrTarget::Uhd4K,isWide?SrPrescale::P1080:SrPrescale::P720,SrTarget::Uhd4K,quality),false,false,false,2);
            const bool passed=result.ok&&result.evaluations==4&&result.srIn==(isWide?Extent{1920,800}:Extent{1280,720})&&
                result.work==(isWide?Extent{3840,1600}:Extent{3840,2160});
            std::cout<<"DUAL_CAPABILITY q="<<quality<<" wide="<<isWide<<" passed="<<passed<<std::endl;ok=ok&&passed;++cases;
        }
    }
    ring.shutdown();ctx.shutdown();std::cout<<"DUAL_CAPABILITIES cases="<<cases<<" passed="<<ok<<std::endl;return ok?0:1;
}
// Supersampling: one RTX Video SR pass at 8K shrunk to 4K equals the CPU area
// average of the same graph's 8K result (SDR in sRGB; HDR after the per-texel
// restore), the exact source size and a non-integer 2K shrink run, and double
// SR ignores a kept shrink.
int supersample(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    bool ok=true;
    const auto check=[&](bool pass,const std::string& label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;ok=ok&&pass;};
    using pipeline::SrTarget;using pipeline::SrOutput;using pipeline::Extent;
    const auto qhd=pattern(2560,1440);const auto qhdSdr=sdrFrame(qhd),qhdHdr=hdrFrame(qhd),hd=sdrFrame(pattern(1920,1080));
    if(!qhdSdr||!qhdHdr||!hd)return 2;
    const auto shrunk=[](unsigned quality,SrTarget target,SrOutput output){auto s=vsr(quality,target);s.srOutput=output;return s;};
    {
        const auto full=run(ctx,ring,qhdSdr.get(),false,vsr(2,SrTarget::Uhd8K),false);
        const auto fit=run(ctx,ring,qhdSdr.get(),false,shrunk(2,SrTarget::Uhd8K,SrOutput::Uhd4K),false);
        check(full.ok&&full.output.width==7680&&fit.ok&&fit.enabled&&fit.srOut==Extent{7680,4320}&&fit.work==Extent{3840,2160}&&
              fit.output.width==3840&&fit.output.height==2160&&fit.evaluations==6,"SDR: SR writes 7680x4320 once per frame, the chain runs at 3840x2160");
        if(full.ok&&fit.ok&&full.output.width==7680&&fit.output.width==3840){
            const auto reference=shrink(full.output,2,false);int worst=0;size_t over=0,count=0;
            for(size_t p=0;p<size_t(3840)*2160;++p)for(unsigned c=0;c<3;++c){
                const int d=std::abs(int(fit.output.pixels[p*4+c])-int(std::lround(encode(reference[p*3+c]))));worst=std::max(worst,d);over+=d>1;++count;}
            check(worst<=2&&over*200<count,"SDR shrink equals the area average of the 8K result (max "+std::to_string(worst)+", >1: "+std::to_string(over)+")");
        }
        std::cout<<"SUPERSAMPLE_TIMING case=sdr-2k-8k-4k q=2 fullSrMs="<<full.srMs<<" shrinkSrMs="<<fit.srMs<<std::endl;
    }
    {
        const auto full=run(ctx,ring,qhdHdr.get(),true,vsr(2,SrTarget::Uhd8K),false);
        const auto fit=run(ctx,ring,qhdHdr.get(),true,shrunk(2,SrTarget::Uhd8K,SrOutput::Uhd4K),false);
        check(full.ok&&fit.ok&&fit.enabled&&fit.output.width==3840&&full.output.width==7680,"HDR: the 8K SR result is shrunk to 3840x2160");
        if(full.ok&&fit.ok&&full.output.width==7680&&fit.output.width==3840){
            const auto reference=shrink(full.output,2,true);size_t over=0,count=0,bad=0;double worst=0,peak=0;
            for(size_t p=0;p<size_t(3840)*2160;++p)for(unsigned c=0;c<3;++c){
                const double v=half(fit.output,p*4+c),r=reference[p*3+c];if(!std::isfinite(v)){++bad;continue;}
                const double d=std::abs(v-r);worst=std::max(worst,d);peak=std::max(peak,v);over+=d>0.003+0.003*std::abs(r);++count;}
            check(!bad&&over*1000<count,"HDR shrink equals the area average of the 8K restore (worst "+std::to_string(worst)+", over "+std::to_string(over)+", peak "+std::to_string(peak)+")");
        }
    }
    {
        const auto back=run(ctx,ring,qhdSdr.get(),false,shrunk(4,SrTarget::Uhd8K,SrOutput::Source),false,false,false,2);
        check(back.ok&&back.srOut==Extent{7680,4320}&&back.output.width==2560&&back.output.height==1440&&back.evaluations==2,"2K -> 8K -> back to the exact 2560x1440 source");
        const auto twoK=run(ctx,ring,hd.get(),false,shrunk(4,SrTarget::Uhd4K,SrOutput::Qhd),false,false,false,2);
        check(twoK.ok&&twoK.srOut==Extent{3840,2160}&&twoK.output.width==2560&&twoK.output.height==1440,"1080p -> 4K -> a non-integer 2K shrink");
        auto dual=twoPass(2,SrTarget::Uhd8K,pipeline::SrPrescale::P1080,SrTarget::Uhd4K,2);dual.srOutput=SrOutput::Qhd;
        const auto both=run(ctx,ring,hd.get(),false,dual,false,false,false,2);
        check(both.ok&&both.evaluations==4&&both.output.width==3840,"double SR runs both passes and ignores a kept shrink");
    }
    ring.shutdown();ctx.shutdown();std::cout<<"SUPERSAMPLE_RESULT passed="<<ok<<std::endl;return ok?0:1;
}
pipeline::SrPrescale reduceArg(std::wstring_view v){
    if(v==L"720")return pipeline::SrPrescale::P720;if(v==L"900")return pipeline::SrPrescale::P900;
    if(v==L"1080")return pipeline::SrPrescale::P1080;if(v==L"1440")return pipeline::SrPrescale::P1440;
    if(v.ends_with(L"%"))return pipeline::srPrescaleForPercent(unsigned(std::stoi(std::wstring(v.substr(0,v.size()-1)))));
    return pipeline::SrPrescale::Off;
}
pipeline::SrTarget targetArg(std::wstring_view v){
    if(v==L"2k")return pipeline::SrTarget::Qhd;if(v==L"5k")return pipeline::SrTarget::Uhd5K;if(v==L"6k")return pipeline::SrTarget::Uhd6K;
    if(v==L"7k")return pipeline::SrTarget::Uhd7K;if(v==L"8k")return pipeline::SrTarget::Uhd8K;return pipeline::SrTarget::Uhd4K;
}
pipeline::SrOutput shrinkArg(std::wstring_view v){
    if(v==L"source")return pipeline::SrOutput::Source;if(v==L"2k")return pipeline::SrOutput::Qhd;if(v==L"4k")return pipeline::SrOutput::Uhd4K;
    if(v==L"5k")return pipeline::SrOutput::Uhd5K;if(v==L"6k")return pipeline::SrOutput::Uhd6K;if(v==L"7k")return pipeline::SrOutput::Uhd7K;
    return pipeline::SrOutput::Target;
}
// One A/B picture: <dir> sr-image <input.png> <name> <quality 0..4> <target> <shrink off|source|2k..7k>
//   <reduce off|720|900|1080|1440|NN%> <second-target> <second-quality> [video|still|capture]
// Quality 0 leaves SR off; a shrink supersamples one pass; reduce "off" keeps a single pass.
int image(const std::filesystem::path& directory,const std::wstring& input,const std::wstring& name,unsigned quality,std::wstring_view target,
          std::wstring_view shrinkTo,std::wstring_view reduce,std::wstring_view second,unsigned secondQuality,std::wstring_view mode){
    std::filesystem::create_directories(directory);Logger::instance().openFile((directory/(name+L".log")).wstring());
    sink::RgbaImage source;if(!sink::loadImage(input,source))return 2;
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    if(!ctx.initialize({},status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    const auto frame=sdrFrame(source);if(!frame)return 2;
    auto settings=quality&&reduce!=L"off"?twoPass(quality,targetArg(target),reduceArg(reduce),targetArg(second),secondQuality):vsr(quality,targetArg(target));
    settings.srOutput=shrinkArg(shrinkTo);
    const auto r=run(ctx,ring,frame.get(),false,settings,false,mode==L"still",mode==L"capture");
    std::error_code ec;std::filesystem::remove(directory/(name+L".png"),ec);  // saveImage never replaces a file
    const bool saved=r.ok&&sink::saveImage((directory/(name+L".png")).wstring(),r.output,false);
    char label[1024]{};WideCharToMultiByte(CP_UTF8,0,name.c_str(),-1,label,sizeof(label),nullptr,nullptr);
    std::cout<<"SR_IMAGE name="<<label<<" source="<<source.width<<'x'<<source.height<<" first="<<r.first.width<<'x'<<r.first.height
             <<" srInput="<<r.srIn.width<<'x'<<r.srIn.height<<" srOutput="<<r.srOut.width<<'x'<<r.srOut.height<<" output="<<r.output.width<<'x'<<r.output.height
             <<" srActive="<<r.enabled<<" evaluations="<<r.evaluations<<" srMs="<<r.srMs<<" passed="<<saved<<std::endl;
    ring.shutdown();ctx.shutdown();return saved?0:1;
}
}

int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc<2)return 2;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com))return 2;
    if(argc==3&&std::wstring_view(argv[2])==L"dual"){const int result=stage::dual(argv[1]);CoUninitialize();return result;}
    if(argc==3&&std::wstring_view(argv[2])==L"dual-restore"){const int result=stage::restore(argv[1]);CoUninitialize();return result;}
    if(argc==3&&std::wstring_view(argv[2])==L"dual-boundaries"){const int result=stage::boundaries(argv[1]);CoUninitialize();return result;}
    if(argc==3&&std::wstring_view(argv[2])==L"dual-capabilities"){const int result=stage::capabilities(argv[1]);CoUninitialize();return result;}
    if(argc==3&&std::wstring_view(argv[2])==L"supersample"){const int result=stage::supersample(argv[1]);CoUninitialize();return result;}
    if(argc>=11&&std::wstring_view(argv[2])==L"sr-image"){
        const int result=stage::image(argv[1],argv[3],argv[4],unsigned(std::stoi(argv[5])),argv[6],argv[7],argv[8],argv[9],unsigned(std::stoi(argv[10])),argc>11?argv[11]:L"video");
        CoUninitialize();return result;
    }
    if(argc>5&&(std::wstring_view(argv[2])==L"reference"||std::wstring_view(argv[2])==L"models")){
        const std::wstring_view mode=argv[5];
        if(mode.starts_with(L"legacy-"))return 2; // withdrawn experiment; no production toggle remains
        const int result=referenceTest(argv[1],argv[3],std::stoi(argv[4]),mode.ends_with(L"moving"),mode.starts_with(L"zero-"),
            mode.find(L"p720")!=std::wstring_view::npos?720:1080,mode.find(L"qhd")!=std::wstring_view::npos?1440:2160,std::wstring_view(argv[2])==L"models");
        CoUninitialize();return result;
    }
    const bool baseline=argc>2&&std::wstring_view(argv[2])==L"baseline";
    source::MediaFileSource media;const AVFrame* natural=nullptr;
    if(argc>3){
        source::SourceOpenDesc open;open.path=argv[3];open.preferHardwareDecode=false;
        if(!media.open(open)||!media.seek({12,1}))return 2;
        pipeline::FramePacket packet;
        if(media.read(packet,&natural)!=source::SourceReadStatus::Frame||!natural)return 2;
    }
    const std::filesystem::path directory=argv[1];std::filesystem::create_directories(directory);
    Logger::instance().openFile((directory/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    bool ok=true;
    for(unsigned quality:{0u,4u,5u})for(bool still:{false,true})for(bool exportJob:{false,true}){
        engine::EnhancementSettings settings;settings.nr=false;settings.sr=true;settings.multiplier=1;
        settings.videoSrQuality=quality;settings.srTarget=pipeline::SrTarget::Qhd;
        engine::StageRequest request;request.width=2560;request.height=1440;request.sr=true;request.stillImage=still;request.exportJob=exportJob;
        pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
        const bool expected=!baseline&&quality==4&&!still;
        const bool passed=desc.enableSr==expected;ok=passed&&ok;
        std::cout<<"SR_POLICY quality="<<quality<<" stillImage="<<still<<" exportJob="<<exportJob<<" nativeActive="<<desc.enableSr<<" passed="<<passed<<std::endl;
    }
    auto render=[&](unsigned quality,bool native,bool color,bool enabled,sink::RgbaImage& image){
        engine::EnhancementSettings settings;settings.nr=false;settings.sr=enabled;settings.multiplier=1;
        settings.videoSrQuality=quality;settings.srTarget=natural?pipeline::SrTarget::Uhd4K:pipeline::SrTarget::Qhd;
        settings.color.enabled=color;settings.color.exposure=color?1.0f:0.0f;
        settings.srMotion=quality==engine::kVideoSrFsr?engine::MotionSource::OpticalFlow:engine::MotionSource::Zero;
        engine::StageRequest request;request.sr=enabled;
        request.width=natural?(native?unsigned(natural->width):unsigned(natural->width)/2):(native?2560:1280);
        request.height=natural?(native?unsigned(natural->height):unsigned(natural->height)/2):(native?1440:720);
        pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
        desc.rgbInput=true;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        pipeline::EnhanceGraph graph(ctx,ring);
        if(!graph.initialize(desc)||!graph.createViews())return false;
        AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_RGBA;frame->width=request.width;frame->height=request.height;
        if(av_frame_get_buffer(frame,32)<0){av_frame_free(&frame);return false;}
        if(natural){
            auto* scaler=sws_getContext(natural->width,natural->height,AVPixelFormat(natural->format),frame->width,frame->height,AV_PIX_FMT_RGBA,SWS_LANCZOS,nullptr,nullptr,nullptr);
            if(!scaler){av_frame_free(&frame);return false;}
            const int rows=sws_scale(scaler,natural->data,natural->linesize,0,natural->height,frame->data,frame->linesize);sws_freeContext(scaler);
            if(rows!=frame->height){av_frame_free(&frame);return false;}
            frame->color_range=AVCOL_RANGE_JPEG;frame->color_primaries=natural->color_primaries;frame->color_trc=natural->color_trc;frame->colorspace=AVCOL_SPC_RGB;
        }else for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x){
            auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
            const auto texture=((x/3+y/3)%2)*18;
            p[0]=uint8_t(25+(x*105/request.width)+texture);p[1]=uint8_t(25+(y*105/request.height)+texture);
            p[2]=uint8_t(45+((x/32+y/32)%3)*20+texture);p[3]=255;
        }
        pipeline::EnhanceGraph::FrameOutputs out;bool passed=true;uint64_t first=0;
        for(unsigned i=0;i<3&&passed;++i){
            out={};passed=graph.process(frame,i*33.333333,i==0,out,i+1)&&ring.waitIdle();
            if(i==0)first=graph.metrics().srEvaluateCount;
        }
        if(passed)passed=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
        const bool expectedActive=enabled&&(!native||(!baseline&&quality>=1&&quality<=4));
        const uint64_t expectedEvaluations=expectedActive?(quality==engine::kVideoSrFsr&&baseline?2:3):0;
        passed=passed&&graph.srEnabled()==expectedActive&&graph.metrics().srEvaluateCount==expectedEvaluations;
        if(enabled&&quality==engine::kVideoSrFsr)passed=passed&&first==(baseline?0u:1u);
        const auto name=std::to_string(quality)+(native?"-native":"-upscale")+(color?"-color":"")+(enabled?"-on":"-off");
        if(passed)passed=sink::saveImage((directory/(name+".png")).wstring(),image,false);
        std::cout<<"SR_CASE name="<<name<<" active="<<graph.srEnabled()<<" firstEvaluations="<<first<<" evaluations="<<graph.metrics().srEvaluateCount<<" width="<<image.width<<" height="<<image.height<<" passed="<<passed<<std::endl;
        out={};ring.drainQueue();graph.shutdown();av_frame_free(&frame);return passed;
    };
    auto difference=[](const sink::RgbaImage& a,const sink::RgbaImage& b){
        uint64_t changed=0,error=0;
        if(!a.width||!a.height||a.width!=b.width||a.height!=b.height||a.pixels.size()!=b.pixels.size())return std::pair<uint64_t,double>{0,-1};
        for(size_t i=0;i<a.pixels.size();++i)if(i%4!=3){const auto d=std::abs(int(a.pixels[i])-int(b.pixels[i]));changed+=d!=0;error+=d;}
        return std::pair<uint64_t,double>{changed,double(error)/(a.width*a.height*3)};
    };
    auto mean=[](const sink::RgbaImage& a){if(!a.width||!a.height)return -1.0;double v=0;for(size_t i=0;i<a.pixels.size();++i)if(i%4!=3)v+=a.pixels[i];return v/(a.width*a.height*3);};
    sink::RgbaImage nativeOff,nativeOn;
    ok=render(4,true,false,false,nativeOff)&&ok;ok=render(4,true,false,true,nativeOn)&&ok;
    auto [changed,mae]=difference(nativeOff,nativeOn);
    const bool nativePass=baseline?changed==0:changed>1000;
    std::cout<<"SR_NATIVE_CHANGED channels="<<changed<<" mae8="<<mae<<" passed="<<nativePass<<std::endl;ok=ok&&nativePass;
    sink::RgbaImage low,high,colored;
    ok=render(1,false,false,true,low)&&ok;ok=render(4,false,false,true,high)&&ok;ok=render(4,false,true,true,colored)&&ok;
    const bool colorPass=mean(colored)>mean(high)+10;
    auto [qualityChanged,qualityMae]=difference(low,high);
    std::cout<<"SR_PRECOLOR baseMean="<<mean(high)<<" coloredMean="<<mean(colored)<<" passed="<<colorPass<<std::endl;
    std::cout<<"SR_QUALITY_CHANGED channels="<<qualityChanged<<" mae8="<<qualityMae<<" passed="<<(qualityChanged>100)<<std::endl;
    ok=ok&&colorPass&&qualityChanged>100;
    sink::RgbaImage dlss,fsr;
    ok=render(0,false,false,true,dlss)&&ok;ok=render(5,false,false,true,fsr)&&ok;
    media.close();ring.shutdown();ctx.shutdown();CoUninitialize();std::cout<<"SR_RESULT baseline="<<baseline<<" natural="<<(natural!=nullptr)<<" passed="<<ok<<std::endl;return ok?0:1;
}
