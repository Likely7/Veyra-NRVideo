#pragma once
#include "WorkspaceChrome.h"
#include <chrono>
#include "LiveStatusDashboard.h"

namespace veyra::ui {
namespace live_status {
struct State {engine::EngineController* engine;int scroll=0,maxScroll=0;bool advanced=false;DashboardHistory history;};
inline LRESULT CALLBACK proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){state=new State{static_cast<engine::EngineController*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)};SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(state));}
    if(!state)return DefWindowProcW(h,message,wp,lp);
    switch(message){
    case WM_CREATE:SetTimer(h,1,250,nullptr);return 0;
    case WM_TIMER:state->history.sample(state->engine->snapshot());if(IsWindowVisible(h))InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_SIZE:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_LBUTTONUP:if(GET_Y_LPARAM(lp)>=dip(h,109)&&GET_Y_LPARAM(lp)<dip(h,133)&&GET_X_LPARAM(lp)>=([&]{RECT r{};GetClientRect(h,&r);return r.right-dip(h,42);}())){state->advanced=!state->advanced;state->scroll=0;InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_MOUSEWHEEL:{POINT at{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&at);RECT r{};GetClientRect(h,&r);const int bottom=dip(h,104+std::max(92,MulDiv(r.bottom,96,layoutDpi(h))-188));if(!state->advanced||at.y<dip(h,136)||at.y>=bottom)return 0;}state->scroll=std::clamp(state->scroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*50,0,state->maxScroll);InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);
        const int width=MulDiv(paint.rect.right,96,veyra::ui::layoutDpi(h)),height=MulDiv(paint.rect.bottom,96,veyra::ui::layoutDpi(h));
        const auto s=state->engine->snapshot();const auto& f=s.metrics.flow;
        paintDashboard(h,paint.dc,width,height,s,state->history,state->advanced);
        if(!state->advanced)return 0;
        const bool playing=s.running&&!s.image&&s.transport==engine::TransportState::Playing;
        const bool xess=s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
        auto write=[&](const std::wstring& value,int x,int y,int w,int ht,int size,COLORREF color){chromeText(paint.dc,h,value,x,y,w,ht,size,color);};
        auto ms=[](std::optional<double> v){return v?std::format(L"{:.1f} ms",*v):std::wstring(L"not measured");};
        auto timing=[&](const diagnostics::TimingAggregate& a){return playing&&a.mean&&a.p95?(state->advanced?std::format(L"{:.1f} / {:.1f}",*a.mean,*a.p95):ms(a.mean)):std::wstring(L"not measured");};
        auto fps=[&](double value){return playing&&!f.rateWindowReady?std::wstring(L"Sampling"):std::format(L"{:.1f} fps",value);};
        const auto& extra=f.cpuTiming[size_t(diagnostics::CpuStage::EnhancementDelayEstimate)];
        std::vector<std::pair<std::wstring,std::wstring>> rows;
        const bool live=s.capture||s.remotePlay;
        rows.emplace_back(live?L"Extra display latency · estimate":L"Frame late · estimate",playing?ms(extra.mean):L"not measured");
        rows.emplace_back(L"Estimate scope",live?L"Post-decode to submit, minus base overhead":L"How far the real frame lags the playback clock at submit");
        rows.emplace_back(L"Measurement scope",L"Not measured on screen · not enhancement processing time");
        rows.emplace_back(L"Enhancement processing · mean / P95",timing(f.enhancementProcessing));
        rows.emplace_back(L"Processing stats scope",L"Same-frame optical flow + NR + SR + residual + FG span, deduplicated");
        rows.emplace_back(L"Processing scope",xess?L"Excludes display frame gen (XeSS/AMD FSR)":L"Excludes input color, output, audio and present wait");
        rows.emplace_back(L"Average scope",L"Total per source frame; individual items per execution count");
        rows.emplace_back(L"Optical flow scope",L"Includes optical flow GPU dependency wait");
        rows.emplace_back(xess?L"SDK submit (not measured on screen)":L"Display submit (not measured on screen)",fps(xess?f.xessSdkSubmitFps:f.presentSubmitFps));
        if(!xess)rows.emplace_back(L"Real / generated submitted",std::format(L"{:.1f} / {:.1f} fps",f.realPresentFps,f.generatedPresentFps));
        rows.emplace_back(L"Requested target (not measured)",s.nominalSourceFps>0?std::format(L"{:.1f} fps",s.nominalSourceFps*(s.captureHalfRate?.5:1)*s.applied.multiplier):L"Not determined");
        std::wstring progress=!playing?s.status:s.applying?L"Applying settings":s.fgBudgetLimited?L"Some generated frames missed the deadline":L"Playing";
        if(playing&&!s.applying&&s.remotePlay){
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;
            const auto old=[&](int64_t stamp){return stamp>0&&now-stamp>10000000;};const auto& r=s.remoteStream;
            if(old(r.lastVideo100ns))progress=L"Waiting for PS5 video input";
            else if(old(r.decodeStarted100ns))progress=L"Decode call hasn't returned";
            else if(old(r.lastDecoded100ns))progress=r.video.waitingForIdr?L"Waiting for keyframe recovery":L"Video received, waiting for decode output";
            else if(f.lastSubmit100ns>f.lastReady100ns&&old(f.lastSubmit100ns))progress=L"Waiting for GPU completion observation";
            else if(old(f.lastSubmit100ns))progress=L"Decoded, enhancement submit stalled";
            else if(old(f.lastPresent100ns))progress=L"GPU ready, waiting to present";
        }
        rows.emplace_back(L"Status",progress);
        if(playing&&s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Dlss&&s.applied.multiplier>1&&s.previewFgMultiplier>1)
            rows.emplace_back(L"Frame gen multiplier · target / scheduled",std::format(L"{}X / {}X",s.applied.multiplier,s.previewFgMultiplier));
        if(!s.capture)rows.emplace_back(L"Media playback speed",playing?std::format(L"{:.2f} x",s.playbackSpeed):L"not measured");
        if(!s.capture&&!s.image){
            rows.emplace_back(L"Preview status",!playing?L"Not running":s.previewSkipped?std::format(L"Frames skipped {}",s.previewSkipped):s.fgBudgetLimited?L"Frame gen budget insufficient":L"Normal");
            if(s.xessGenerationSuppressed)rows.emplace_back(L"XeSS frame gen",L"Generation paused under-speed · experimental");
        }
        rows.emplace_back(L"Input",s.remotePlay?std::format(L"PS5 · {:.1f} fps",s.remoteStream.receivedFps):s.capture?std::format(L"Capture · {:.1f} fps",s.captureFps):L"File / image");
        rows.emplace_back(L"Pipeline time",state->advanced?L"Mean / P95 · ms":L"Average over last second");
        rows.emplace_back(s.remotePlay?L"① PS5 actual decode":L"① File fetch / decode",s.remotePlay?ms(s.remoteStream.decodeMeanMs):timing(f.cpuTiming[size_t(diagnostics::CpuStage::Decode)]));
        if(s.remotePlay)rows.emplace_back(L"Post-decode wait for enhancement",timing(f.cpuTiming[size_t(diagnostics::CpuStage::DecodedQueue)]));
        const diagnostics::GpuStage stages[]={diagnostics::GpuStage::Color,diagnostics::GpuStage::Sr,diagnostics::GpuStage::Flow,diagnostics::GpuStage::Nr,diagnostics::GpuStage::Residual,diagnostics::GpuStage::FgBatch,diagnostics::GpuStage::Blit};
        const wchar_t* names[]={L"② Input color",L"③ Super resolution SR",L"④ Optical flow queue span",L"⑤ NR enhancement",L"⑥ Residual composite",L"⑦ Frame gen FG",L"⑧ Output composite"};
        for(size_t i=0;i<std::size(stages);++i){const auto& sample=s.metrics.gpu[size_t(stages[i])];
            // Present-sink FG (XeSS/FSR) is timed application-side; only fall
            // back to the placeholder while no sample has been collected.
            const auto value=(xess&&stages[i]==diagnostics::GpuStage::FgBatch&&!f.gpuTiming[size_t(stages[i])].mean)?L"Not measurable inside SDK":sample.state==diagnostics::SampleState::NotExecuted?L"Not executed":timing(f.gpuTiming[size_t(stages[i])]);
            rows.emplace_back(names[i],value);
        }
        rows.emplace_back(L"⑨ Wait for display time",timing(f.cpuTiming[size_t(diagnostics::CpuStage::DeadlineWait)]));
        rows.emplace_back(L"⑩ Present submit",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Present)]));
        rows.emplace_back(L"Stats scope",L"GPU processing and CPU wait can overlap, not added together");
        if(s.remotePlay){
            const auto& r=s.remoteStream;
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;
            auto age=[&](int64_t stamp){return stamp>0?ms(double(std::max<int64_t>(0,now-stamp))/10000):std::wstring(L"Not received yet");};
            rows.emplace_back(L"PS5 stream",L"Receive -> decode -> present");
            rows.emplace_back(L"Requested stream format",std::format(L"{}x{} · {} fps · {}",r.requestedProfile.width,r.requestedProfile.height,r.requestedProfile.fps,r.requestedProfile.codec==remoteplay::Codec::H264?L"H.264":L"H.265"));
            rows.emplace_back(L"Requested bitrate",std::format(L"{:.1f} Mbps",r.requestedProfile.bitrateKbps/1000.0));
            rows.emplace_back(L"Actual decode method",!r.decodeConfirmed?L"Waiting for first frame":r.hardwareDecode?L"D3D12VA hardware decode":r.decodeFallback?L"Software decode · hardware fell back":L"CPU software decode");
            rows.emplace_back(L"Received / decoded",std::format(L"{:.1f} / {:.1f} fps",r.receivedFps,r.decodedFps));
            rows.emplace_back(L"Enhancement complete",fps(f.sourceCompletedFps));
        rows.emplace_back(L"Real / generated presented",xess?L"Display frame gen counted inside SDK":std::format(L"{:.1f} / {:.1f} fps",f.realPresentFps,f.generatedPresentFps));
            rows.emplace_back(L"Video effective bitrate",std::format(L"{:.2f} Mbps",r.videoMbps));
            rows.emplace_back(L"Post-receive wait for decode",ms(r.ingressWaitMeanMs));
            rows.emplace_back(L"Since last video received",age(r.lastVideo100ns));
            rows.emplace_back(L"Since last decode complete",age(r.lastDecoded100ns));
            rows.emplace_back(L"Since last present",age(f.lastPresent100ns));
            rows.emplace_back(L"Stream end-to-end latency",L"Not provided by the protocol, can't be measured directly");
            rows.emplace_back(L"Keyframe recovery requests",std::to_wstring(r.video.idrRequests));
            if(state->advanced){
                rows.emplace_back(L"Protocol-reported missing source frames · total",std::to_wstring(r.framesLost));
                rows.emplace_back(L"Reference recovery markers · total",std::to_wstring(r.referenceRecoveryEvents));
                rows.emplace_back(L"Compressed queue dropped · total",std::to_wstring(r.video.dropped));
                rows.emplace_back(L"Decode mailbox overwrites · total",std::to_wstring(s.remotePlaySkipped));
                rows.emplace_back(L"FEC success / live RTT",L"Current interface provides no valid measurement");
            }
        }
        if(state->advanced){
        rows.emplace_back(live?L"Extra display latency · P95":L"Frame late · P95",playing?ms(extra.p95):L"not measured");
        rows.emplace_back(L"Real-frame software residency · mean",playing?ms(f.softwareLatencyMs):L"not measured");
        rows.emplace_back(L"Real-frame software residency · P95",playing?ms(f.softwareLatencyP95Ms):L"not measured");
        rows.emplace_back(L"Estimate baseline",L"Ideal direct playback · not a dual-path measurement");
        rows.emplace_back(L"GPU completed output (incl. expired)",fps(f.outputCompletedFps));
        rows.emplace_back(L"Source frames processed",fps(f.sourceCompletedFps));
        rows.emplace_back(L"Valid generated (incl. expired)",xess?L"Not measurable inside SDK":fps(f.validGeneratedFps));
        rows.emplace_back(L"Capture overwrites / generated expired",std::format(L"{} / {}",f.counters.mailboxOverwritten,f.counters.generatedExpiredAfterEval));
        rows.emplace_back(L"Command slot wait",timing(f.cpuTiming[size_t(diagnostics::CpuStage::SlotWait)]));
        rows.emplace_back(L"CPU graph submit",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Submit)]));
        rows.emplace_back(L"GPU-ready wait (overlapping)",timing(f.cpuTiming[size_t(diagnostics::CpuStage::ReadyWait)]));
        rows.emplace_back(f.pairCaptureCallbacks?L"A/B capture arrival interval":L"A/B software fetch interval",xess?L"Not measurable inside SDK":timing(f.pairTiming[size_t(diagnostics::PairTiming::ArrivalInterval)]));
        rows.emplace_back(L"Generated present since A arrival",xess?L"Not measurable inside SDK":timing(f.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)]));
        rows.emplace_back(L"Generated present since B arrival",xess?L"Not measurable inside SDK":timing(f.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromB)]));
        rows.emplace_back(L"Latency samples / stats overflow",std::format(L"{} / {}",f.latencySamples,f.timingOverflow));
        if(f.reset.sessionId){
            const auto& r=f.reset;
            const auto outcome=r.outcome==diagnostics::ResetOutcome::Completed?L"Valid output restored":r.outcome==diagnostics::ResetOutcome::RolledBack?L"Rolled back":r.outcome==diagnostics::ResetOutcome::Cancelled?L"Cancelled":r.outcome==diagnostics::ResetOutcome::Failed?L"Failed":L"Waiting for valid output";
            rows.emplace_back(std::format(L"Last settings {} · {}",r.rebuilt?L"rebuild":L"reset",r.settingsRevision),outcome);
            rows.emplace_back(L"Total switch time",ms(r.totalMs));
            const wchar_t* resetNames[]={L"Drain",L"Destroy resources",L"Create resources",L"First-frame warmup submit",L"First-frame completion observed"};
            for(size_t i=0;i<r.stageMs.size();++i)rows.emplace_back(resetNames[i],ms(r.stageMs[i]));
        }
        const wchar_t* fgName=s.applied.frameGenerationBackend==engine::FrameGenerationBackend::XeSS?L"XeSS"
            :s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Fsr?L"AMD FSR":L"DLSS";
        rows.emplace_back(L"Frame gen method",s.applied.multiplier<=1?L"Off":std::format(L"{} {}X",fgName,s.applied.multiplier));
        rows.emplace_back(L"NR internal size",s.applied.nr?std::format(L"{} x {}",s.metrics.resolution.nr.width,s.metrics.resolution.nr.height):L"Off");
        rows.emplace_back(L"NR runtime version",s.nrActive?(s.applied.nrRuntime==engine::NrRuntime::Ampere?L"RTX 30 compatible · experimental":s.applied.nrRuntime==engine::NrRuntime::Community?L"Community compatible · experimental":L"NVIDIA original"):L"Not running");
        rows.emplace_back(L"Display mode",s.running?(s.applied.captureCompatible?L"Live-broadcast compatible · experimental":L"Standard display"):L"Not running");
        if(s.audioAvailable)rows.emplace_back(L"Audio channels · in / out",std::format(L"{} / {}{}",s.capture?s.captureAudio.inputChannels:s.audioInputChannels,s.capture?s.captureAudio.outputChannels:s.audioOutputChannels,(s.capture?s.captureAudio.outputChannels<s.captureAudio.inputChannels:s.audioOutputChannels<s.audioInputChannels)?L" · downmix":L""));
        rows.emplace_back(L"Audio sync",s.audioAvailable?(s.capture?(s.captureAudio.running?L"Software-estimated sync":L"Waiting for video anchor"):(s.audioEndpointRecovering?L"Audio device recovering · timeline held":s.audioRebuffering?L"Re-syncing · waiting for video anchor":L"Audio master clock")):L"No audio");
        if(!s.capture&&s.audioAvailable){
            rows.emplace_back(L"Audio device recovery count",std::to_wstring(s.audioEndpointRecoveries));
            if(FAILED(s.audioEndpointError))rows.emplace_back(L"Last audio device error",std::format(L"0x{:08X}",unsigned(s.audioEndpointError)));
        }
        if(!s.capture&&s.audioAvailable)rows.emplace_back(L"Audio lead · software estimate",std::format(L"{:.1f} ms",s.lateMs));
        if(s.capture&&s.audioAvailable){
            rows.emplace_back(L"Audio format",std::format(L"{} Hz · {} bit / {} valid{}{}",s.captureAudio.inputSampleRate,s.captureAudio.inputContainerBits,s.captureAudio.inputValidBits,s.captureAudio.inputFloating?L" · Float":L"",s.captureAudio.inputBitstream.empty()?L"":std::format(L" · bitstream decoded to {} channels ({})",s.captureAudio.inputChannels,s.captureAudio.inputBitstream)));
            rows.emplace_back(s.captureAudio.syncClockFallback?L"Estimated offset · local clock":L"A/V offset · audio lead",ms(s.captureAudio.skewMs));
            rows.emplace_back(L"Audio compensation",std::format(L"{:.1f} ms{}",s.captureAudio.compensationMs,s.captureAudio.limited?L" · at limit":L""));
            if(s.captureAudio.syncClockFallback)rows.emplace_back(L"Sync status",L"Timestamp anomaly fallback · local latency estimate");
            rows.emplace_back(L"PCM queue",std::format(L"{:.1f} ms",s.captureAudio.bufferedMs));
            rows.emplace_back(L"Audio device queue",std::format(L"{:.1f} ms",s.captureAudio.endpointBufferedMs));
            rows.emplace_back(L"Audio re-anchor / overflow",std::format(L"{} / {}",s.captureAudio.resets,s.captureAudio.overflows));
            if(s.captureAudio.recoveryDiscardedFrames)rows.emplace_back(L"Old audio skipped during recovery",std::format(L"{:.1f} ms",s.captureAudio.recoveryDiscardedFrames/48.0));
            rows.emplace_back(L"Underrun / measurable gap / inserted silence",std::format(L"{} / {:.1f} / {:.1f} ms",s.captureAudio.underruns,s.captureAudio.underrunFrames/48.0,s.captureAudio.silenceFrames/48.0));
            if(s.captureAudio.clockStalledGaps)rows.emplace_back(L"Dropouts with no clock-measured duration",std::to_wstring(s.captureAudio.clockStalledGaps));
            rows.emplace_back(L"Conversion peak / over-range / anomalies",std::format(L"{:.4f} / {} / {}",s.captureAudio.inputPeak,s.captureAudio.overRangeSamples,s.captureAudio.nonFiniteSamples+s.captureAudio.invalidPaddingSamples));
        }
        if(!s.colorStatus.empty())rows.emplace_back(L"Actual color pipeline",s.colorStatus);
        if(!s.sourceNotice.empty())rows.emplace_back(L"Source compatibility",s.sourceNotice);
        if(!s.backendWarning.empty())rows.emplace_back(L"Backend status",s.backendWarning);
        if(!s.captureAudio.error.empty())rows.emplace_back(L"Capture audio error",s.captureAudio.error);
        }
        std::vector<std::pair<std::wstring,std::wstring>> wrapped;
        const auto rowFont=makeFont(h,11,FW_NORMAL);const auto oldFont=SelectObject(paint.dc,rowFont);
        auto split=[&](std::wstring value,int room){
            std::vector<std::wstring> lines;
            do{
                int fit=0;SIZE extent{};
                GetTextExtentExPointW(paint.dc,value.c_str(),int(value.size()),dip(h,room),&fit,nullptr,&extent);
                const auto count=std::min(value.size(),size_t(std::max(1,fit)));
                lines.push_back(value.substr(0,count));value.erase(0,count);
            }while(!value.empty());
            return lines;
        };
        for(const auto& row:rows){
            const auto labels=split(row.first,width/2-24),values=split(row.second,width/2-24);
            for(size_t line=0;line<std::max(labels.size(),values.size());++line)wrapped.emplace_back(line<labels.size()?labels[line]:L"",line<values.size()?values[line]:L"");
        }
        SelectObject(paint.dc,oldFont);DeleteObject(rowFont);rows=std::move(wrapped);
        const int top=136,rowHeight=25,available=std::max(0,std::max(92,height-188)-40)/rowHeight*rowHeight;
        state->maxScroll=std::max(0,int(rows.size())*rowHeight-available);state->scroll=std::clamp(state->scroll/rowHeight*rowHeight,0,state->maxScroll);
        const int saved=SaveDC(paint.dc);IntersectClipRect(paint.dc,0,dip(h,top),paint.rect.right,dip(h,top+available));
        for(size_t i=0;i<rows.size();++i){const int y=top+int(i)*rowHeight-state->scroll;
            // glassText renders through its own DIB; a parent DC clip does not
            // constrain that buffer. Never draw partial rows into fixed text.
            if(y<top||y+rowHeight>top+available)continue;
            write(rows[i].first,20,y,width/2-24,rowHeight,11,secondary);
            write(rows[i].second,width/2,y,width/2-24,rowHeight,11,textColor);
        }
        RestoreDC(paint.dc,saved);
        if(state->maxScroll&&available>0){const int thumb=std::max(18,available*available/(int(rows.size())*rowHeight));const int y=top+(available-thumb)*state->scroll/state->maxScroll;RECT r{dip(h,width-16),dip(h,y),dip(h,width-14),dip(h,y+thumb)};FillRect(paint.dc,&r,panelBrush());}

        return 0;
    }
    case WM_NCDESTROY:KillTimer(h,1);delete state;SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
inline HWND createLiveStatusPanel(HWND parent,engine::EngineController& engine){
    WNDCLASSW wc{};wc.lpfnWndProc=live_status::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraLiveStatus";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"Live Processing Status",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,&engine);
}
}
