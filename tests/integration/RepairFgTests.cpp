// Synthetic pixels only. Diagnostic readback is intentionally outside all
// player/capture/export paths. The actual product EnhanceGraph is under test.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <cmath>
#include <algorithm>
#include <memory>
#include <cstring>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace {
uint64_t hash(const veyra::sink::RgbaImage& im){uint64_t v=14695981039346656037ull;for(auto b:im.pixels){v^=b;v*=1099511628211ull;}return v;}
double latticeValue(unsigned x,unsigned y){
    uint32_t value=x*0x9e3779b9u+y*0x85ebca6bu;
    value^=value>>16;value*=0x7feb352du;value^=value>>15;
    return 32.0+double(value%193);
}
uint8_t detailedPlane(unsigned x,unsigned y,unsigned frame){
    // A translated, nonperiodic bilinear texture avoids the sine fixture's
    // repeated low-frequency structure without changing the position gate.
    const unsigned local=x+256-frame*16;
    const unsigned cellX=local/8,cellY=y/8;
    const double tx=double(local%8)/8,ty=double(y%8)/8;
    const double a=latticeValue(cellX,cellY)*(1-tx)+latticeValue(cellX+1,cellY)*tx;
    const double b=latticeValue(cellX,cellY+1)*(1-tx)+latticeValue(cellX+1,cellY+1)*tx;
    return uint8_t(std::lround(a*(1-ty)+b*ty));
}
// The same lattice as detailedPlane at a fractional texture position: the analytic
// intermediate frame, also where content enters through the frame edge.
double detailedPlaneAt(double local,unsigned y){
    const double cell=std::floor(local/8);const unsigned cellX=unsigned(cell),cellY=y/8;
    const double tx=(local-cell*8)/8,ty=double(y%8)/8;
    const double a=latticeValue(cellX,cellY)*(1-tx)+latticeValue(cellX+1,cellY)*tx;
    const double b=latticeValue(cellX,cellY+1)*(1-tx)+latticeValue(cellX+1,cellY+1)*tx;
    return a*(1-ty)+b*ty;
}
// "-hud": a static striped UI box above the translation measurement rows (y 200..880).
constexpr unsigned kHudX0=1560,kHudX1=1860,kHudY0=40,kHudY1=180;
bool hudPixel(unsigned x,unsigned y,uint8_t& value){
    if(x<kHudX0||x>=kHudX1||y<kHudY0||y>=kHudY1)return false;
    const unsigned lx=x-kHudX0,ly=y-kHudY0;
    const bool frame=lx<4||lx>=kHudX1-kHudX0-4||ly<4||ly>=kHudY1-kHudY0-4;
    const bool text=(ly/6)%3==1&&((lx/5)%4)!=3&&lx>=16&&lx<kHudX1-kHudX0-16;
    value=frame||text?uint8_t(20):uint8_t(236);return true;
}
double center(const veyra::sink::RgbaImage& im){double total=0,sum=0;for(unsigned y=300;y<780;++y)for(unsigned x=0;x<im.width;++x){const auto v=im.pixels[(size_t(y)*im.width+x)*4];if(v>160){total+=1;sum+=x;}}return total?sum/total:-1;}
double translation(const veyra::sink::RgbaImage& previous,const veyra::sink::RgbaImage& generated,bool vertical=false){
    double best=1e30,shift=0;
    for(unsigned quarter=0;quarter<=64;++quarter){
        const double dx=quarter*0.25;double error=0;
        for(unsigned y=200;y<880;y+=8)for(unsigned x=300;x<1620;x+=8){
            const double sample=(vertical?y:x)-dx;const unsigned left=unsigned(sample);const double fraction=sample-left;
            const size_t offset=(vertical?(size_t(left)*previous.width+x):(size_t(y)*previous.width+left))*4;
            const double expected=previous.pixels[offset]*(1-fraction)+previous.pixels[offset+(vertical?previous.width*4:4)]*fraction;
            const double delta=expected-generated.pixels[(size_t(y)*generated.width+x)*4];error+=delta*delta;
        }
        if(error<best){best=error;shift=dx;}
    }
    return shift;
}
}
// Decode diagnostic RGB10/PQ readback to an 80-nit SDR reference so the same
// geometric truth metric can test both provider programs. Product frames never
// enter this CPU path. HDR range/color is checked separately by HDR tests.
static bool readImage(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring,ID3D12Resource* texture,veyra::sink::RgbaImage& image){
    using namespace veyra;
    const auto desc=texture->GetDesc();
    if(desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)return sink::readRgba8(ctx,ring,texture,image);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;ctx.device()->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    pipeline::ComPtr<ID3D12Resource> rb;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb))))return false;
    Status st;unsigned slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.drainQueue())return false;
    void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};if(FAILED(rb->Map(0,&range,&mapped)))return false;
    image.width=unsigned(desc.Width);image.height=desc.Height;image.pixels.resize(size_t(image.width)*image.height*4);
    const auto decode=[](unsigned code){const double v=std::pow(double(code)/1023,1/78.84375);const double nits=10000*std::pow(std::max(v-.8359375,0.)/(18.8515625-18.6875*v),1/.1593017578125);const double l=nits/80;const double srgb=l<=.0031308?12.92*l:1.055*std::pow(l,1/2.4)-.055;return uint8_t(std::lround(std::clamp(srgb,0.,1.)*255));};
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){uint32_t pixel=0;std::memcpy(&pixel,static_cast<const uint8_t*>(mapped)+fp.Offset+y*fp.Footprint.RowPitch+x*4,4);for(unsigned c=0;c<3;++c)image.pixels[(size_t(y)*image.width+x)*4+c]=decode((pixel>>(c*10))&1023);image.pixels[(size_t(y)*image.width+x)*4+3]=255;}
    D3D12_RANGE written{0,0};rb->Unmap(0,&written);return true;
}
#include "EffectExclusionCases.h"
int wmain(int argc,wchar_t** argv){
    const std::wstring modeWide=argc>1?argv[1]:L"off";
    const std::string mode(modeWide.begin(),modeWide.end());
    if(mode.starts_with("exclusion"))return effectExclusionCases(argc,argv);
    using namespace veyra;CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);
    Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;dd.enableDebugLayer=mode.find("-debug")!=std::string::npos;bool ok=ctx.initialize(dd,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);
    pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=1920;gd.sourceHeight=gd.workHeight=1080;gd.enableNr=false;gd.enableSr=false;gd.enableFg=true;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    const std::filesystem::path artifacts=argc>2?std::filesystem::path(argv[2]):std::filesystem::path{};
    if(!artifacts.empty())std::filesystem::create_directories(artifacts);
    if(argc>3)gd.runtimeAbsPath=std::filesystem::path(argv[3]).wstring();
    if(argc>4&&std::wstring(argv[4])==L"anr")gd.frameGenerationBackend=engine::FrameGenerationBackend::Anr;
    if(argc>4&&std::wstring(argv[4])==L"fsr")gd.frameGenerationBackend=engine::FrameGenerationBackend::Fsr;
    gd.hdrInput=gd.hdrOutput=mode.find("-hdr")!=std::string::npos;
    gd.rgbInput=mode.find("-rgb")!=std::string::npos;
    gd.enableNr=mode!="off";gd.enableNvofStandalone=gd.enableNr;
    if(mode.starts_with("dis"))gd.opticalFlowBackend=engine::OpticalFlowBackend::GpuDis;
    if(mode=="flowP")gd.flowQuality=engine::FlowQuality::Performance;
    if(mode=="flowQ")gd.flowQuality=engine::FlowQuality::Quality;
    if(mode=="dis-sr"){gd.workWidth=3840;gd.workHeight=2160;gd.nrWidth=1920;gd.nrHeight=1080;gd.enableSr=true;gd.videoSrQuality=2;}
    if(mode=="sr"){gd.workWidth=3840;gd.workHeight=2160;gd.nrWidth=1920;gd.nrHeight=1080;gd.enableSr=true;}
    const bool textured=mode.ends_with("-textured");
    const bool exact=mode.find("-exact")!=std::string::npos;
    const bool vertical=mode.find("-vertical")!=std::string::npos;
    const bool globalMotion=mode.find("-pan")!=std::string::npos;
    const bool planar=mode.find("-planar")!=std::string::npos;
    const bool detailed=mode.find("-detail")!=std::string::npos;
    const bool hud=mode.find("-hud")!=std::string::npos;
    double hudSum=0,edgeSum=0,interiorSum=0;unsigned qualityFrames=0;
    const uint16_t motionBits=mode.find("-reverse")!=std::string::npos?0x4c00:mode.find("-zero")!=std::string::npos?0:0xcc00;
    pipeline::ComPtr<ID3D12Resource> motion,upload;
    if(ok&&exact){motion=pipeline::makeTexture(ctx.device(),1920,1080,DXGI_FORMAT_R16G16_FLOAT,false);upload=pipeline::makeUploadBuffer(ctx.device(),1920ull*1080*4);ok=motion&&upload;gd.fgMotionProbe=motion.Get();}
    if(mode.starts_with("mfg")&&mode.size()>=4&&mode[3]>='2'&&mode[3]<='6'){gd.fgMultiplier=uint32_t(mode[3]-'0');gd.enableNr=false;gd.enableNvofStandalone=false;}
    std::unique_ptr<pipeline::EnhanceGraph> companion;
    if(ok&&argc>5){auto cd=gd;cd.frameGenerationBackend=std::wstring(argv[5])==L"anr"?engine::FrameGenerationBackend::Anr:engine::FrameGenerationBackend::Dlss;cd.fgMultiplier=2;companion=std::make_unique<pipeline::EnhanceGraph>(ctx,ring);ok=companion->initialize(cd)&&companion->createViews();}
    if(ok)ok=graph.initialize(gd)&&graph.createViews();
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=1920;f->height=1080;f->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(f,32)>=0;
    AVFrame* hdrFrame=nullptr;
    if(gd.hdrInput){
        hdrFrame=av_frame_alloc();hdrFrame->format=AV_PIX_FMT_P010;hdrFrame->width=1920;hdrFrame->height=1080;
        hdrFrame->color_range=AVCOL_RANGE_MPEG;hdrFrame->colorspace=AVCOL_SPC_BT2020_NCL;hdrFrame->color_primaries=AVCOL_PRI_BT2020;hdrFrame->color_trc=AVCOL_TRC_SMPTE2084;
        ok=ok&&av_frame_get_buffer(hdrFrame,32)>=0;
        if(ok)for(unsigned y=0;y<540;++y)std::fill_n(reinterpret_cast<uint16_t*>(hdrFrame->data[1]+y*hdrFrame->linesize[1]),1920,uint16_t(512<<6));
    }
    sink::RgbaImage previous;unsigned generated=0,valid=0;pipeline::FrameBatch retained;
    for(unsigned i=0;ok&&i<12;++i){
        if(i==6&&companion){ring.drainQueue();companion->shutdown();companion.reset();std::cout<<"COEXIST released companion while foreground stays live\n";}
        if(exact){
            void* data=nullptr;if(FAILED(upload->Map(0,nullptr,&data))){ok=false;break;}
            for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x){auto* p=static_cast<uint16_t*>(data)+(size_t(y)*1920+x)*2;const bool square=x>=240+i*16&&x<880+i*16&&y>=220&&y<860;p[0]=!vertical&&i&&(square||globalMotion||planar)?motionBits:0;p[1]=vertical&&i&&(square||globalMotion||planar)?motionBits:0;}
            upload->Unmap(0,nullptr);unsigned slot=0;auto* list=ring.acquireNext(slot,st);if(!list){ok=false;break;}
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={motion.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=motion.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16_FLOAT,1920,1080,1,1920*4};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
            if(!ring.submitAndSignal(slot)||!ring.drainQueue()){ok=false;break;}
        }
        for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;const int local=x-int(i)*16;const bool square=local>=240&&local<880&&y>=220&&y<860;const int texture=textured?((local*13+y*7)^((local/8)*19+(y/8)*23))&31:0;p[0]=p[1]=p[2]=square?240-texture:20;p[3]=255;}
        if(planar)for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){
            auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;const double local=(vertical?y:x)-int(i)*16;const double cross=vertical?x:y;
            p[0]=p[1]=p[2]=uint8_t(std::lround(128+45*std::sin(local*0.07)+35*std::sin(local*0.031+cross*0.06)+25*std::sin(cross*0.031)));p[3]=255;
            if(detailed)p[0]=p[1]=p[2]=detailedPlane(unsigned(vertical?y:x),unsigned(vertical?x:y),i);
            if(uint8_t v=0;hud&&hudPixel(unsigned(x),unsigned(y),v))p[0]=p[1]=p[2]=v;
        }
        if(hdrFrame)for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x){
            const double srgb=double(f->data[0][size_t(y)*f->linesize[0]+x*4])/255;
            const double linear=srgb<=.04045?srgb/12.92:std::pow((srgb+.055)/1.055,2.4);
            const double v=std::pow(linear*.008,.1593017578125);
            const double pq=std::pow((.8359375+18.8515625*v)/(1+18.6875*v),78.84375);
            reinterpret_cast<uint16_t*>(hdrFrame->data[0]+y*hdrFrame->linesize[0])[x]=uint16_t(std::lround(64+876*pq))<<6;
        }
        pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(hdrFrame?hdrFrame:f,i*20.0,i==0,out)){ok=false;break;}
        sink::RgbaImage real,g;if(!readImage(ctx,ring,graph.videoFrameResource(out.videoSlot),real)){ok=false;break;}
        if(!ring.waitIdle()||!graph.resolveGeneration(out)){ok=false;break;}
        // Completed queue timestamps, including reverse flow and refinement.
        // Keep CPU readback/fixture work outside the measured GPU interval.
        if(i>=2){
            const auto timing=graph.gpuMetrics();
            const auto flow=timing.gpu[size_t(diagnostics::GpuStage::Flow)].milliseconds;
            const auto span=diagnostics::graphExecutionSpanMs({timing.identity,timing.gpu});
            std::cout<<"GPU_TIMING frame="<<i<<" flowMs="<<flow.value_or(-1)<<" graphMs="<<span.value_or(-1)<<std::endl;
        }
        uint64_t previousGeneratedHash=0;
        for(uint32_t j=0;j<out.batch.count;++j){const auto& item=out.batch.frames[j];if(item.kind!=pipeline::FrameKind::Generated)continue;
            ++generated;if(!readImage(ctx,ring,graph.generatedFrameResource(item.lease->slot),g)){ok=false;break;}
            if(i==8&&!artifacts.empty()){
                sink::saveImage((artifacts/("generated-"+std::to_string(item.subframe)+".png")).wstring(),g);
                sink::saveImage((artifacts/"previous.png").wstring(),previous);sink::saveImage((artifacts/"current.png").wstring(),real);
            }
            const double t=double(item.subframe)/gd.fgMultiplier;
            double blendResidual=0;for(size_t k=0;k<g.pixels.size();k+=4)blendResidual+=std::abs(double(g.pixels[k])-(double(previous.pixels[k])*(1-t)+real.pixels[k]*t));blendResidual/=g.width*g.height;
            const double expected=center(previous)*(1-t)+center(real)*t;
            const double displacement=planar?translation(previous,g,vertical):0;
            const bool position=planar?std::abs(displacement-16*t)<=1.0:std::abs(center(g)-expected)<=3.0;
            const bool good=item.validity==pipeline::GenerationValidity::Valid&&hash(g)!=hash(previous)&&hash(g)!=hash(real)&&hash(g)!=previousGeneratedHash&&blendResidual>0.1&&position&&std::abs(item.pts100ns/10000.0-((i-1+t)*20.0))<=0.0001;
            valid+=good;
            previousGeneratedHash=hash(g);
            std::cout<<"CONTENT frame="<<i<<" subframe="<<item.subframe<<" pts="<<item.pts100ns<<" previousHash="<<hash(previous)<<" realHash="<<hash(real)<<" generatedHash="<<hash(g)<<" expectedCenter="<<expected<<" actualCenter="<<center(g)<<" blendResidual="<<blendResidual<<" valid="<<good<<std::endl;
            if(planar)std::cout<<"TRANSLATION expected="<<16*t<<" observed="<<displacement<<std::endl;
            if(planar&&detailed&&!vertical&&!gd.hdrInput){
                // Mean absolute error (0..255) against the analytic intermediate plane or the static UI.
                double hudError=0,edgeError=0,interiorError=0;unsigned hudCount=0,edgeCount=0,interiorCount=0;
                const double frame=double(i)-1+t;
                for(unsigned y=0;y<g.height;++y)for(unsigned x=0;x<g.width;++x){
                    const double actual=g.pixels[(size_t(y)*g.width+x)*4];uint8_t ui=0;
                    if(hud&&hudPixel(x,y,ui)){hudError+=std::abs(actual-ui);++hudCount;continue;}
                    const double expected=detailedPlaneAt(double(x)+256-frame*16,y);
                    if(x<48||x>=g.width-48){edgeError+=std::abs(actual-expected);++edgeCount;}
                    else if(x>=300&&x<1620&&y>=200&&y<880){interiorError+=std::abs(actual-expected);++interiorCount;}
                }
                if(hudCount)hudError/=hudCount;if(edgeCount)edgeError/=edgeCount;if(interiorCount)interiorError/=interiorCount;
                if(i>=2){hudSum+=hudError;edgeSum+=edgeError;interiorSum+=interiorError;++qualityFrames;}
                std::cout<<"QUALITY frame="<<i<<" subframe="<<item.subframe<<" hudMae="<<hudError<<" edgeMae="<<edgeError<<" interiorMae="<<interiorError<<std::endl;
            }
        }
        previous=std::move(real);
        if(i==10)retained=out.batch;
    }
    if(qualityFrames)std::cout<<"QUALITY_SUMMARY frames="<<qualityFrames<<" hudMae="<<hudSum/qualityFrames<<" edgeMae="<<edgeSum/qualityFrames<<" interiorMae="<<interiorSum/qualityFrames<<std::endl;
    std::cout<<"RESULT source="<<graph.metrics().nvofExecuteCount+graph.metrics().gpuDisExecuteCount+1<<" generated="<<generated<<" contentValid="<<valid<<" display=unmeasured"<<std::endl;
    pipeline::EnhanceGraph::FrameOutputs rejected;
    const bool protectedLease=ok&&!graph.process(hdrFrame?hdrFrame:f,240,false,rejected);
    std::cout<<"LEASE retained-slot-overwrite-rejected="<<protectedLease<<std::endl;
    retained={};rejected={};
    ok=ok&&generated==11*(gd.fgMultiplier-1)&&valid==generated&&protectedLease;companion.reset();ring.drainQueue();graph.shutdown();av_frame_free(&f);av_frame_free(&hdrFrame);
    if(dd.enableDebugLayer){pipeline::ComPtr<ID3D12InfoQueue> queue;unsigned errors=0;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&queue))))ok=false;else for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T size=0;queue->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());queue->GetMessage(i,message,&size);if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<std::endl;}}std::cout<<"DEBUG errors="<<errors<<std::endl;ok=ok&&!errors;}
    ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
