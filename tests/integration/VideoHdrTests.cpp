#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/GraphDescription.h"
#include <DirectXPackedVector.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
using namespace veyra::pipeline;

// Diagnostic readback only. The product conversion stays on the GPU.
static bool pixels(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,ID3D12Resource* texture,std::vector<float>& result,
                   D3D12_RESOURCE_STATES initialState=D3D12_RESOURCE_STATE_COMMON){
    if(!texture)return false;
    const auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))return false;
    unsigned slot=0;Status status;auto* list=ring.acquireNext(slot,status);if(!list)return false;
    StateTracker states;states.set(texture,initialState);states.transition(list,texture,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,texture,initialState);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    void* p=nullptr;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(buffer->Map(0,&range,&p)))return false;
    result.resize(size_t(d.Width)*d.Height*3);
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<3;++c){
        const auto* row=static_cast<const uint8_t*>(p)+fp.Offset+y*fp.Footprint.RowPitch;
        float value=0;
        if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)value=DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const uint16_t*>(row)[x*4+c]);
        else if(d.Format==DXGI_FORMAT_R10G10B10A2_UNORM){const double coded=double((reinterpret_cast<const uint32_t*>(row)[x]>>(10*c))&1023)/1023;const double t=pow(coded,32.0/2523);value=float(125*pow(std::max(t-3424.0/4096,0.0)/(2413.0/128-2392.0/128*t),16384.0/2610));}
        else {buffer->Unmap(0,nullptr);return false;}
        result[(size_t(y)*d.Width+x)*3+c]=value;
    }
    D3D12_RANGE none{};buffer->Unmap(0,&none);return true;
}
static bool colorChain(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return false;
    const auto freeFrame=[](AVFrame* p){av_frame_free(&p);};
    std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);if(!frame)return false;
    frame->format=AV_PIX_FMT_BGRA;frame->width=1280;frame->height=720;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_primaries=AVCOL_PRI_BT709;frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    if(av_frame_get_buffer(frame.get(),32)<0)return false;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
        p[0]=p[1]=p[2]=uint8_t(x*255/1279);p[3]=255;}
    // The HDR chain must exercise the same six independent LUT files that the
    // export worker uses. Keep these files in the isolated executable staging
    // directory; the normal user's runtime_local is never touched.
    const auto lutFolder=runtime::localDataDirectory()/L"luts";
    std::error_code lutEc;std::filesystem::create_directories(lutFolder,lutEc);if(lutEc)return false;
    const auto stamp=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const unsigned lutSizes[]={2,3,5,17,33,4};
    auto writeLut=[&](unsigned index)->std::wstring{
        const auto name=L"hdr-color-"+std::wstring(stamp.begin(),stamp.end())+L"-"+std::to_wstring(index)+L".cube";
        std::ofstream file(lutFolder/name);if(!file)return {};
        const unsigned size=lutSizes[index];file.precision(9);file<<"LUT_3D_SIZE "<<size<<'\n';
        for(unsigned z=0;z<size;++z)for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            const float d=float(size-1);
            // Stay close to identity so the HDR peak remains representative;
            // the per-instance offsets still make every LUT observable.
            file<<(float(x)/d*(.98f+.002f*index)+.004f*(index+1))<<' '
                <<(float(y)/d*(.98f-.001f*index)+.002f*(6-index))<<' '
                <<(float(z)/d*(.98f+.001f*index)+.003f*(index%3))<<'\n';
        }
        file.close();return file?name:L"";
    };
    std::array<std::wstring,engine::kMaxColorInstances> lutNames{};
    for(unsigned i=0;i<engine::kMaxColorInstances;++i){
        lutNames[i]=writeLut(i);if(lutNames[i].empty())return false;
    }
    auto render=[&](const char* name,const engine::EnhancementSettings& settings,std::vector<float>& result,std::vector<float>* linear=nullptr){
        EnhanceGraphDesc desc;desc.rgbInput=true;desc.hdrOutput=true;desc.outputDitherStep=0;
        desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        engine::StageRequest request;request.width=1280;request.height=720;request.stillImage=true;
        engine::describeStages(request,settings,desc);
        auto graph=std::make_unique<EnhanceGraph>(ctx,ring);
        bool ok=graph->initialize(desc)&&graph->createViews()&&graph->videoHdrActive();
        EnhanceGraph::FrameOutputs out;
        for(unsigned n=0;n<3&&ok;++n){out={};ok=graph->process(frame.get(),n*1000.0/30,n==0,out,n+1);}
        if(ok)ok=pixels(ctx,ring,graph->videoFrameResource(out.videoSlot),result);
        if(ok&&linear)ok=pixels(ctx,ring,graph->diagnosticLinearInput(),*linear,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        double peak=0;
        if(ok){ok=result.size()==size_t(1280)*720*3&&std::all_of(result.begin(),result.end(),[](float v){return std::isfinite(v)&&v>=-.5f&&v<100;});
            if(!result.empty())peak=*std::max_element(result.begin(),result.end())*80;
            ok=ok&&peak>100&&peak<2100;
            ok=ok&&sink::saveHdrScreenshot((directory/(std::string(name)+".jxr")).wstring(),ctx,ring,graph->videoFrameResource(out.videoSlot));}
        out={};ring.drainQueue();graph->shutdown();graph.reset();
        std::cout<<"VIDEO_HDR_COLOR case="<<name<<" peakNits="<<peak<<" values="<<result.size()<<" pass="<<ok<<std::endl;
        return ok;
    };
    engine::EnhancementSettings plain;plain.videoHdr.enabled=true;plain.hdrOutputMode=engine::HdrOutputMode::ScRgb;
    auto fused=plain;fused.color.enabled=true;fused.color.exposure=1;
    auto standalone=plain;standalone.additionalColorCount=1;standalone.additionalColors[0]=fused.color;
    auto six=plain;six.color.enabled=true;six.color.exposure=.1f;six.additionalColorCount=5;
    for(auto& color:six.additionalColors){color.enabled=true;color.exposure=.1f;}
    auto sixLut=six;
    sixLut.color.setLutName(lutNames[0]);sixLut.color.lutInputSpace=engine::ColorSettings::kLutInputCineon;
    for(unsigned i=1;i<engine::kMaxColorInstances;++i){
        sixLut.additionalColors[i-1].setLutName(lutNames[i]);
        sixLut.additionalColors[i-1].lutInputSpace=engine::ColorSettings::kLutInputCineon;
    }
    std::vector<float> reference,a,b,c,d,repeated,linearPlain,linearFused,linearStandalone;
    bool ok=render("plain",plain,reference,&linearPlain);ok=render("fused",fused,a,&linearFused)&&ok;
    ok=render("standalone",standalone,b,&linearStandalone)&&ok;ok=render("six",six,c)&&ok;ok=render("six-lut",sixLut,d)&&ok;
    ok=render("fused-repeat",fused,repeated)&&ok;
    double maxFusedError=0,meanSingleChange=0,meanSixChange=0,meanSixLutChange=0,maxRepeatError=0,meanFusedError=0;
    size_t worstIndex=0,differentValues=0;
    if(ok){ok=a.size()==reference.size()&&b.size()==reference.size()&&c.size()==reference.size();
        ok=ok&&repeated.size()==reference.size();
        ok=ok&&d.size()==reference.size();
        if(ok){for(size_t i=0;i<reference.size();++i){const double error=std::abs(a[i]-b[i]);
            if(error>maxFusedError){maxFusedError=error;worstIndex=i;}
            meanFusedError+=error;differentValues+=error!=0;
            maxRepeatError=std::max(maxRepeatError,double(std::abs(a[i]-repeated[i])));
            meanSingleChange+=std::abs(a[i]-reference[i]);meanSixChange+=std::abs(c[i]-reference[i]);meanSixLutChange+=std::abs(d[i]-reference[i]);}
            meanSingleChange/=reference.size();meanSixChange/=reference.size();meanSixLutChange/=reference.size();meanFusedError/=reference.size();
            std::cout<<"VIDEO_HDR_COLOR_DIAGNOSTIC maxRepeatError="<<maxRepeatError<<" meanFusedError="<<meanFusedError
                <<" differentValues="<<differentValues<<" worstX="<<(worstIndex/3)%1280<<" worstY="<<worstIndex/(3*1280)
                <<" channel="<<worstIndex%3<<" fused="<<a[worstIndex]<<" standalone="<<b[worstIndex]<<std::endl;
            // Compare final TrueHDR output for the same one-stop grade. 1/64 scRGB is
            // a bounded FP16 tolerance (1.25 nits), not a visual-quality claim.
            ok=maxFusedError<=1.0/64&&meanSingleChange>.001&&meanSixChange>.001&&meanSixLutChange>.001;}}
    // Separate diagnostic renders expose the exact SDR codes passed to TrueHDR:
    // the same blit encode is used when TrueHDR is disabled. Do not use this
    // diagnostic to relax the HDR tolerance or change product precision.
    auto renderSdr=[&](engine::EnhancementSettings settings,sink::RgbaImage& image){
        settings.videoHdr.enabled=false;
        EnhanceGraphDesc desc;desc.rgbInput=true;desc.outputDitherStep=0;
        engine::StageRequest request;request.width=1280;request.height=720;request.stillImage=true;
        engine::describeStages(request,settings,desc);
        auto graph=std::make_unique<EnhanceGraph>(ctx,ring);EnhanceGraph::FrameOutputs out;
        bool rendered=graph->initialize(desc)&&graph->createViews()&&graph->process(frame.get(),0,true,out,1);
        if(rendered)rendered=sink::readRgba8(ctx,ring,graph->videoFrameResource(out.videoSlot),image);
        out={};ring.drainQueue();graph->shutdown();return rendered;
    };
    sink::RgbaImage sdrFused,sdrStandalone;
    const bool sdrOk=renderSdr(fused,sdrFused)&&renderSdr(standalone,sdrStandalone)&&
        sdrFused.pixels.size()==sdrStandalone.pixels.size()&&!sdrFused.pixels.empty();
    int maxCodeError=0;size_t differentCodes=0;
    if(sdrOk)for(size_t i=0;i<sdrFused.pixels.size();++i){const int error=std::abs(int(sdrFused.pixels[i])-int(sdrStandalone.pixels[i]));
        maxCodeError=std::max(maxCodeError,error);differentCodes+=error!=0;}
    std::cout<<"VIDEO_HDR_COLOR_SDR_DIAGNOSTIC rendered="<<sdrOk<<" maxCodeError="<<maxCodeError
        <<" differentCodes="<<differentCodes<<std::endl;
    ok=ok&&sdrOk;
    // Diagnostic only: identify the earliest divergence without changing the
    // legacy fused shader, the standalone RGBA16F contract, or the HDR gate.
    const bool linearOk=linearPlain.size()==size_t(1280)*720*3&&linearFused.size()==linearPlain.size()&&
        linearStandalone.size()==linearPlain.size()&&a.size()==linearPlain.size()&&b.size()==linearPlain.size();
    double maxLinearError=0,meanLinearError=0;size_t linearDifferent=0,linearWorst=0;
    if(linearOk){
        for(size_t i=0;i<linearFused.size();++i){const double error=std::abs(linearFused[i]-linearStandalone[i]);
            if(error>maxLinearError){maxLinearError=error;linearWorst=i;}
            meanLinearError+=error;linearDifferent+=error!=0;}
        meanLinearError/=linearFused.size();
        std::cout<<"VIDEO_HDR_COLOR_LINEAR_DIAGNOSTIC maxError="<<maxLinearError<<" meanError="<<meanLinearError
            <<" differentValues="<<linearDifferent<<" worstX="<<(linearWorst/3)%1280<<" channel="<<linearWorst%3
            <<" fused="<<linearFused[linearWorst]<<" standalone="<<linearStandalone[linearWorst]<<std::endl;
        std::ofstream csv(directory/"linear-sdr-hdr-row.csv");
        csv<<"x,inputCode,channel,plainLinear,fusedLinear,standaloneLinear,fusedSdr,standaloneSdr,fusedHdr,standaloneHdr\n"<<std::setprecision(10);
        if(sdrOk)for(size_t x=0;x<1280;++x)for(size_t ch=0;ch<3;++ch){const size_t i=x*3+ch;
            csv<<x<<','<<x*255/1279<<','<<ch<<','<<linearPlain[i]<<','<<linearFused[i]<<','<<linearStandalone[i]
                <<','<<unsigned(sdrFused.pixels[x*4+ch])<<','<<unsigned(sdrStandalone.pixels[x*4+ch])
                <<','<<a[i]<<','<<b[i]<<'\n';}
        csv.flush();ok=bool(csv)&&ok;
    }
    ok=linearOk&&ok;
    ComPtr<ID3D12InfoQueue> info;uint64_t errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info))))ok=false;
    else for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;
        if(FAILED(info->GetMessage(i,nullptr,&size))){ok=false;break;}
        std::vector<uint8_t> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(info->GetMessage(i,message,&size))){ok=false;break;}
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<std::endl;}}
    ok=ok&&errors==0;
    std::cout<<"VIDEO_HDR_COLOR_MATRIX maxFusedErrorScRgb="<<maxFusedError<<" meanSingleChange="<<meanSingleChange
        <<" meanSixChange="<<meanSixChange<<" meanSixLutChange="<<meanSixLutChange
        <<" debugErrors="<<errors<<" pass="<<ok<<std::endl;
    info.Reset();ring.shutdown();ctx.shutdown();return ok;
}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(argc>1&&std::wstring_view(argv[1])==L"--color-chain"){
        const bool ok=argc==3&&colorChain(argv[2]);CoUninitialize();return ok?0:1;}
    // Leave legacy numeric arguments unchanged; the harness supplies an isolated
    // evidence path rather than deleting or overwriting a previous screenshot.
    std::wstring screenshotPath=L"video-hdr.jxr";
    if(argc>=3&&std::wstring_view(argv[argc-2])==L"--output"){screenshotPath=argv[argc-1];argc-=2;}
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc dd;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    EnhanceGraph graph(ctx,ring);EnhanceGraphDesc gd;
    gd.sourceWidth=gd.workWidth=gd.nrWidth=1280;gd.sourceHeight=gd.workHeight=gd.nrHeight=720;
    gd.rgbInput=true;gd.enableNr=gd.enableSr=gd.enableFg=gd.enableNvofStandalone=false;
    gd.videoHdr.enabled=true;gd.hdrOutput=true;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    if(argc>1){const auto multiplier=unsigned(_wtoi(argv[1]));gd.fgMultiplier=std::max(2u,multiplier);gd.enableFg=multiplier>1;}
    if(argc>2){gd.enableNr=_wtoi(argv[2])!=0;gd.enableNvofStandalone=gd.enableNr;}
    if(argc>3&&_wtoi(argv[3])>0){gd.enableSr=true;gd.workWidth=1920;gd.workHeight=1080;gd.videoSrQuality=unsigned(_wtoi(argv[3]));}
    source::MediaFileSource media;const AVFrame* decodedFrame=nullptr;FramePacket packet;
    if(argc>5){source::SourceOpenDesc input;input.path=argv[5];input.preferHardwareDecode=false;
        if(!media.open(input)||media.read(packet,&decodedFrame)!=source::SourceReadStatus::Frame)return 6;
        gd.sourceWidth=gd.workWidth=gd.nrWidth=decodedFrame->width;gd.sourceHeight=gd.workHeight=gd.nrHeight=decodedFrame->height;gd.rgbInput=false;
    }
    if(!graph.initialize(gd)||!graph.createViews()||!graph.videoHdrActive())return 3;
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_BGRA;frame->width=1280;frame->height=720;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_primaries=AVCOL_PRI_BT709;frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    if(av_frame_get_buffer(frame,32)<0)return 4;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto* p=frame->data[0]+y*frame->linesize[0]+x*4;const uint8_t v=uint8_t(x*255/1279);p[0]=p[1]=p[2]=v;p[3]=255;}
    if(argc>4)for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto* p=frame->data[0]+y*frame->linesize[0]+x*4;p[0]=uint8_t(x*17+y*13);p[1]=uint8_t(x/5+y/3);p[2]=uint8_t((x^y)&255);p[3]=255;}
    bool ok=true;double peak=0,changed=0;std::vector<float> baseline;
    unsigned generated=0;
    for(unsigned n=0;n<12&&ok;++n){
        if(n==6){engine::EnhancementSettings s;s.nr=gd.enableNr;s.sr=gd.enableSr;s.multiplier=gd.enableFg?gd.fgMultiplier:1;s.videoSrQuality=gd.videoSrQuality;s.videoHdr=gd.videoHdr;s.videoHdr.peakNits=400;ok=graph.applySettings(s);}
        if(argc>5&&n&&media.read(packet,&decodedFrame)!=source::SourceReadStatus::Frame){ok=false;break;}
        EnhanceGraph::FrameOutputs out;ok=ok&&graph.process(decodedFrame?decodedFrame:frame,n*1000.0/30,n==0||n==8,out,n+1);
        if(!ok)break;
        std::vector<float> data;ok=pixels(ctx,ring,graph.videoFrameResource(out.videoSlot),data);
        if(!ok)break;
        ok=std::all_of(data.begin(),data.end(),[](float v){return std::isfinite(v)&&v>=-.5f&&v<100;});
        peak=*std::max_element(data.begin(),data.end())*80;
        ok=ok&&peak>100&&peak<2100;
        if(n==5)baseline=data;
        if(n==11){for(size_t i=0;i<data.size();++i)changed+=std::abs(data[i]-baseline[i]);changed/=data.size();
            ok=ok&&changed>.001&&sink::saveHdrScreenshot(screenshotPath,ctx,ring,graph.videoFrameResource(out.videoSlot));}
        if(gd.enableFg){ok=ok&&graph.resolveGeneration(out);if(out.hasGenerated)++generated;}
    }
    ok=ok&&(!gd.enableFg||generated>0);
    std::cout<<"VIDEO_HDR pass="<<ok<<" peakNits="<<peak<<" parameterChange="<<changed<<" generatedBatches="<<generated<<" nr="<<graph.metrics().nrEvaluateCount<<" sr="<<graph.metrics().srEvaluateCount<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&frame);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:5;
}
