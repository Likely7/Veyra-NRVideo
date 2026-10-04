// Authored diagnostic stimulus, not a production crop/readback path.
#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/diagnostics/EnhancementProcessingTime.h"
#include "veyra/RuntimePaths.h"
#include "veyra/FileIdentity.h"
#include <d3d12sdklayers.h>
#include <cstring>
#include <fstream>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

int wmain(int argc,wchar_t** argv){
    using namespace veyra;if(argc!=5)return 2;
    const std::wstring mode=argv[3],shape=argv[4];
    if((mode!=L"none"&&mode!=L"full"&&mode!=L"crop")||(shape!=L"239"&&shape!=L"43"))return 2;
    const bool crop=mode==L"crop",nr=mode!=L"none";
    const unsigned x=shape==L"43"?240:0,y=shape==L"239"?138:0,w=1920-x*2,h=1080-y*2;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
    struct Apartment{~Apartment(){CoUninitialize();}} apartment;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> layer;if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer))))return 2;layer->EnableDebugLayer();
    engine::EnhancementSettings settings;settings.nr=nr;settings.nrTemporal=false;settings.nrPolicy=pipeline::NrSizePolicy::Native;
    engine::StageRequest request;request.nr=nr;request.width=crop?w:1920;request.height=crop?h:1080;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    engine::PreviewGpuSession session;if(!session.initialize()||!session.graph->initialize(desc)||!session.graph->createViews())return 2;
    session.graph->recordGpuTimings();
    pipeline::EnhanceGraph base(session.context,session.ring,&session.core);
    if(crop){auto off=settings;off.nr=false;request.nr=false;request.width=1920;request.height=1080;
        pipeline::EnhanceGraphDesc plain;engine::describeStages(request,off,plain);plain.noNgx=true;plain.noFeatures=true;
        if(!base.initialize(plain)||!base.createViews())return 2;}
    source::MediaFileSource source;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;if(!source.open(open))return 2;
    std::ofstream frames(out/L"frames.csv");frames<<"frame,pts100ns,width,height,sha256,enhancementMs,nrMs,flowMs\n";
    bool pass=true;unsigned done=0;
    for(unsigned i=0;i<40&&pass;++i){
        if(i==20)pass=source.seek({10,1});
        pipeline::FramePacket packet;const AVFrame* input=nullptr;
        pass=pass&&source.read(packet,&input)==source::SourceReadStatus::Frame&&input&&input->format==AV_PIX_FMT_YUV420P;
        if(!pass)break;
        AVFrame* canvas=av_frame_alloc();canvas->format=input->format;canvas->width=1920;canvas->height=1080;
        pass=av_frame_get_buffer(canvas,32)>=0&&av_frame_copy_props(canvas,input)>=0;
        if(pass)for(unsigned p=0;p<3;++p){const unsigned div=p?2:1,pw=1920/div,ph=1080/div;
            for(unsigned row=0;row<ph;++row)std::memset(canvas->data[p]+row*canvas->linesize[p],p?128:16,pw);
            for(unsigned row=0;row<h/div;++row)std::memcpy(canvas->data[p]+(y/div+row)*canvas->linesize[p]+x/div,
                input->data[p]+(y/div+row)*input->linesize[p]+x/div,w/div);}
        // Late burned-in text/OSD outside the previously confirmed black ROI.
        if(pass&&i>=10){const unsigned tx=shape==L"239"?840:60,ty=shape==L"239"?992:420;
            for(unsigned row=0;row<22;++row)for(unsigned col=0;col<120;++col)
                if((col/8+row/4)%3!=0)canvas->data[0][(ty+row)*canvas->linesize[0]+tx+col]=235;}
        AVFrame* view=pass?av_frame_clone(canvas):nullptr;
        if(pass&&crop){view->crop_left=x;view->crop_right=x;view->crop_top=y;view->crop_bottom=y;
            pass=av_frame_apply_cropping(view,AV_FRAME_CROP_UNALIGNED)>=0&&view->width==int(w)&&view->height==int(h);}
        sink::RgbaImage image,baseImage;pipeline::EnhanceGraph::FrameOutputs output;
        if(pass)pass=session.graph->process(view,packet.pts.toDouble()*1000,i==0||i==20,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
        const auto times=session.graph->takeGpuTimings();
        std::optional<double> enhanced,nrMs,flowMs;
        for(const auto& timing:times){enhanced=diagnostics::enhancementProcessingMs(timing);
            nrMs=timing.gpu[size_t(diagnostics::GpuStage::Nr)].milliseconds;flowMs=timing.gpu[size_t(diagnostics::GpuStage::Flow)].milliseconds;}
        if(pass)pass=sink::readRgba8(session.context,session.ring,session.graph->videoFrameResource(output.videoSlot),image);
        if(pass&&crop){pipeline::EnhanceGraph::FrameOutputs plain;
            pass=base.process(canvas,packet.pts.toDouble()*1000,i==0||i==20,plain,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle()&&
                sink::readRgba8(session.context,session.ring,base.videoFrameResource(plain.videoSlot),baseImage);
            if(pass){for(unsigned row=0;row<h;++row)std::memcpy(baseImage.pixels.data()+((y+row)*1920+x)*4,image.pixels.data()+row*w*4,w*4);
                image=std::move(baseImage);}}
        av_frame_free(&view);av_frame_free(&canvas);
        if(pass)pass=image.width==1920&&image.height==1080&&enhanced.has_value()&&
            sink::saveImage((out/(L"frame-"+std::to_wstring(i)+L".png")).wstring(),image);
        if(pass){frames<<i<<','<<int64_t(packet.pts.toDouble()*10000000)<<",1920,1080,"<<sha256Hex(image.pixels.data(),image.pixels.size())
            <<','<<*enhanced<<','<<nrMs.value_or(0)<<','<<flowMs.value_or(0)<<'\n';frames.flush();++done;}
    }
    source.close();session.ring.drainQueue();base.shutdown();session.graph->shutdown();pass=session.core.close("letterbox-diagnostic")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(FAILED(debug->GetMessage(i,message,&n)))return 2;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<message->pDescription<<std::endl;}}
    const auto removed=session.context.device()->GetDeviceRemovedReason();pass=pass&&done==40&&errors==0&&SUCCEEDED(removed);
    debug.Reset();session.shutdown();std::cout<<"LETTERBOX_RESULT pass="<<pass<<" frames="<<done<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass?0:1;
}
