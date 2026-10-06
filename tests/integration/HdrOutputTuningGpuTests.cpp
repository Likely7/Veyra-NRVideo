// PR19 integration: real graph -> VideoPresenter -> native DXGI readback.
// Readback/waits are diagnostic-only. This measures pixels, not panel light.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <DirectXPackedVector.h>
#include <iostream>
#include <array>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
}
#include "CaptureFormatGpuCases.h"
#include "HdrNativeRoundTripCases.h"

using namespace veyra;
int wmain(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    int failures=0;
    const auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;};
    constexpr std::array<double,16> patches={0,.005,.05,.5,1,2,5,10,20,50,100,203,400,1000,4000,10000};
    AVFrame* frame=av_frame_alloc();frame->width=64;frame->height=16;frame->format=AV_PIX_FMT_YUV420P10LE;
    frame->color_range=AVCOL_RANGE_MPEG;frame->colorspace=AVCOL_SPC_BT2020_NCL;
    frame->color_primaries=AVCOL_PRI_BT2020;frame->color_trc=AVCOL_TRC_SMPTE2084;
    if(av_frame_get_buffer(frame,32)<0)return 2;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x){
        reinterpret_cast<uint16_t*>(frame->data[0]+y*frame->linesize[0])[x]=uint16_t(std::lround(64+876*hdrRoundTrip::pq(patches[x/4])));
        if(!(y%2)&&!(x%2))for(unsigned c=1;c<3;++c)reinterpret_cast<uint16_t*>(frame->data[c]+y/2*frame->linesize[c])[x/2]=512;
    }
    for(bool packed:{false,true}){
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.workWidth=64;gd.sourceHeight=gd.workHeight=16;
        gd.hdrInput=gd.hdrOutput=true;gd.noFeatures=gd.noNgx=true;gd.enableNr=gd.enableSr=gd.enableFg=false;
        gd.hdrOutputMode=packed?engine::HdrOutputMode::Hdr10:engine::HdrOutputMode::ScRgb;
        gd.hdrCurve.displayPeakNits=1000;
        pipeline::EnhanceGraph::FrameOutputs output;std::vector<float> raw,shown;
        bool ready=graph.initialize(gd)&&graph.createViews()&&graph.process(frame,0,true,output,1)&&hdrRoundTrip::read(ctx,ring,graph.videoFrameResource(output.videoSlot),raw);
        HWND window=CreateWindowExW(0,L"STATIC",L"PR19 HDR output diagnostic",WS_POPUP,0,0,64,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        engine::VideoPresenter presenter;
        ready=ready&&window&&presenter.open(ctx,window,graph);
        const auto present=[&](int comparison=0,bool retain=false){
            bool ok=presenter.present(ctx,ring,graph,output.videoSlot,false,true,comparison,false,.5f,{1,1,1},{},-1,retain);
            auto buffer=presenter.presentedResourceForTest();
            ok=ok&&hdrRoundTrip::read(ctx,ring,buffer.Get(),shown);buffer.Reset();return ok;
        };
        check(ready&&present(0,true)&&shown==raw,packed?"HDR10 disabled curve: original PQ pixels exact":"scRGB disabled curve: original signed pixels exact");
        const auto idleCount=presenter.submittedCount();
        check(present(0,true)&&presenter.submittedCount()==idleCount,"unchanged paused view reuses its buffer");
        engine::EnhancementSettings settings;settings.hdrOutputMode=gd.hdrOutputMode;
        settings.hdrCurve=gd.hdrCurve;settings.hdrCurve.enabled=true;
        settings.hdrCurve.highlightStartNits=200;settings.hdrCurve.peakCapNits=400;
        ready=ready&&graph.applySettings(settings)&&present(0,true);
        check(ready&&presenter.submittedCount()>idleCount,"HDR uniform edits invalidate the paused display cache");
        double maxRelative=0;bool monotonic=true;double previous=0;
        if(ready)for(unsigned i=0;i<patches.size();++i){
            const auto at=size_t(i*4+2)*3;
            const double before=packed?hdrRoundTrip::nits(raw[at]):raw[at]*80;
            const double after=packed?hdrRoundTrip::nits(shown[at]):shown[at]*80;
            // Independent expected knee, expressed in absolute nits from the
            // actual graph output (including its input/output quantization).
            const double expected=before<=200?before:200+200*(before-200)/(200+(before-200));
            if(expected>=1)maxRelative=std::max(maxRelative,std::abs(after-expected)/expected);
            monotonic=monotonic&&after+.001>=previous&&std::isfinite(after)&&after<=402;previous=after;
        }
        std::cout<<"CURVE_EVIDENCE packed="<<packed<<" maxRelative="<<maxRelative<<" monotonic="<<monotonic<<std::endl;
        check(ready&&maxRelative<.018&&monotonic,packed?"HDR10 curve: PQ decoded to nits, knee and re-encode":"scRGB curve: linear nits knee and peak bound");
        std::vector<float> untouched;
        check(hdrRoundTrip::read(ctx,ring,graph.videoFrameResource(output.videoSlot),untouched)&&untouched==raw,"preview tuning leaves graph/export surface byte-equivalent");
        settings.hdrCurve.strengthPercent=0;
        check(graph.applySettings(settings)&&present()&&shown==raw,"live zero strength restores exact identity without rebuilding");
        settings.hdrCurve.strengthPercent=100;
        check(graph.applySettings(settings)&&present(1),"linear comparison reference presents through the same curve (no PQ double decode)");
        // Reopening the same presenter instance must reset metadata and monitor
        // caches; a new graph/source never inherits the previous clip's state.
        ring.drainQueue();presenter.close();
        check(presenter.open(ctx,window,graph)&&present(),"same presenter reopen retains working HDR output");
        if(packed){
            // Exercise the live DXGI hint through ResizeBuffers, then disable
            // before another enabled render. The runner checks accepted hints
            // are followed by a clear call; unsupported hints remain optional.
            std::cout<<"METADATA_RESIZE_BEGIN"<<std::endl;
            settings.hdrMetadata.enabled=true;
            check(graph.applySettings(settings)&&present(),"optional HDR metadata does not block presentation");
            SetWindowPos(window,nullptr,0,0,96,16,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            Sleep(120); // Native resize coalescing; diagnostic-only.
            check(present(),"HDR window resizes with metadata active");
            {const auto buffer=presenter.presentedResourceForTest();check(buffer&&buffer->GetDesc().Width==96,"resize exercised real DXGI buffers");}
            settings.hdrMetadata.enabled=false;
            check(graph.applySettings(settings)&&present(),"disable metadata immediately after resize");
            std::cout<<"METADATA_RESIZE_END"<<std::endl;
            SetWindowPos(window,nullptr,0,0,64,16,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            Sleep(120);
            check(present(),"restore diagnostic window extent");
        }
        // BT.2020 primaries decoded into scRGB contain negative components.
        // A curve must scale signed RGB uniformly instead of changing hue.
        constexpr std::array<std::array<double,3>,4> colors={{{1000,0,0},{0,1000,0},{0,0,1000},{400,100,20}}};
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x){
            const auto& c=colors[x/16];
            const double r=hdrRoundTrip::pq(c[0]),g=hdrRoundTrip::pq(c[1]),b=hdrRoundTrip::pq(c[2]);
            const double yy=.2627*r+.678*g+.0593*b;
            reinterpret_cast<uint16_t*>(frame->data[0]+y*frame->linesize[0])[x]=uint16_t(std::lround(64+876*yy));
            if(!(y%2)&&!(x%2)){
                reinterpret_cast<uint16_t*>(frame->data[1]+y/2*frame->linesize[1])[x/2]=uint16_t(std::lround(512+896*(b-yy)/1.8814));
                reinterpret_cast<uint16_t*>(frame->data[2]+y/2*frame->linesize[2])[x/2]=uint16_t(std::lround(512+896*(r-yy)/1.4746));
            }
        }
        output={};ring.drainQueue();
        ready=graph.process(frame,16,true,output,2)&&hdrRoundTrip::read(ctx,ring,graph.videoFrameResource(output.videoSlot),raw);
        settings.hdrCurve=engine::findHdrTuningPreset("darkLift")->curve;settings.hdrCurve.strengthPercent=200;settings.hdrCurve.displayPeakNits=1000;
        ready=ready&&graph.applySettings(settings)&&present();
        const auto linear=[packed](const std::vector<float>& pixels,size_t at){
            std::array<double,3> c{pixels[at],pixels[at+1],pixels[at+2]};
            if(!packed)return c;
            for(auto& v:c)v=hdrRoundTrip::nits(v)/80;
            return std::array<double,3>{1.660491*c[0]-.587641*c[1]-.072850*c[2],-.124550*c[0]+1.132900*c[1]-.008349*c[2],-.018151*c[0]-.100579*c[1]+1.118730*c[2]};
        };
        double maxChromaError=0;bool signs=true;
        if(ready)for(unsigned i=0;i<4;++i){
            const auto at=size_t(i*16+8)*3;const auto a=linear(raw,at),b=linear(shown,at);
            const double aNorm=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
            const double bNorm=std::sqrt(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
            if(aNorm<=0||bNorm<=0){signs=false;continue;}
            for(unsigned c=0;c<3;++c){maxChromaError=std::max(maxChromaError,std::abs(a[c]/aNorm-b[c]/bNorm));if(a[c]<-.05)signs=signs&&b[c]<0;}
        }
        std::cout<<"COLOR_EVIDENCE packed="<<packed<<" normalizedRgbError="<<maxChromaError<<" negativeSigns="<<signs<<std::endl;
        check(ready&&signs&&maxChromaError<(packed?.015:.002),"enabled HDR curve preserves wide-gamut RGB direction and negative scRGB");
        ring.drainQueue();presenter.close();DestroyWindow(window);output={};graph.shutdown();
        // Restore grayscale input for the second output contract.
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x){
            reinterpret_cast<uint16_t*>(frame->data[0]+y*frame->linesize[0])[x]=uint16_t(std::lround(64+876*hdrRoundTrip::pq(patches[x/4])));
            if(!(y%2)&&!(x%2))for(unsigned c=1;c<3;++c)reinterpret_cast<uint16_t*>(frame->data[c]+y/2*frame->linesize[c])[x/2]=512;
        }
    }
    av_frame_free(&frame);ring.drainQueue();
    std::cout<<"HDR output integration failures="<<failures<<" (GPU/DXGI pixels only; physical monitor unmeasured)"<<std::endl;
    return failures?1:0;
}
