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
        auto ms=[](std::optional<double> v){return v?std::format(L"{:.1f} ms",*v):std::wstring(L"未测");};
        auto timing=[&](const diagnostics::TimingAggregate& a){return playing&&a.mean&&a.p95?(state->advanced?std::format(L"{:.1f} / {:.1f}",*a.mean,*a.p95):ms(a.mean)):std::wstring(L"未测");};
        auto fps=[&](double value){return playing&&!f.rateWindowReady?std::wstring(L"采样中"):std::format(L"{:.1f} fps",value);};
        const auto& extra=f.cpuTiming[size_t(diagnostics::CpuStage::EnhancementDelayEstimate)];
        std::vector<std::pair<std::wstring,std::wstring>> rows;
        const bool live=s.capture||s.remotePlay;
        rows.emplace_back(live?L"额外显示延迟 · 估计":L"画面迟到 · 估计",playing?ms(extra.mean):L"未测");
        rows.emplace_back(L"估计口径",live?L"解码后至提交，扣基础开销":L"原帧提交时落后播放时钟");
        rows.emplace_back(L"测量范围",L"非屏幕实测 · 非增强处理耗时");
        rows.emplace_back(L"增强处理 · 平均 / P95",timing(f.enhancementProcessing));
        rows.emplace_back(L"处理统计口径",L"同帧光流+NR+SR+残差+FG区间去重");
        rows.emplace_back(L"处理范围",xess?L"不含显示补帧(XeSS/AMD FSR)":L"不含输入颜色、输出、声音和呈现等待");
        rows.emplace_back(L"平均范围",L"总计按源帧；单项按执行次数");
        rows.emplace_back(L"光流范围",L"含光流GPU依赖等待");
        rows.emplace_back(xess?L"SDK提交（非屏幕实测）":L"显示提交（非屏幕实测）",fps(xess?f.xessSdkSubmitFps:f.presentSubmitFps));
        if(!xess)rows.emplace_back(L"原帧 / 生成帧提交",std::format(L"{:.1f} / {:.1f} fps",f.realPresentFps,f.generatedPresentFps));
        rows.emplace_back(L"请求目标（非实测）",s.nominalSourceFps>0?std::format(L"{:.1f} fps",s.nominalSourceFps*(s.captureHalfRate?.5:1)*s.applied.multiplier):L"未确定");
        std::wstring progress=!playing?s.status:s.applying?L"正在应用设置":s.fgBudgetLimited?L"部分补帧未达截止时间":L"播放中";
        if(playing&&!s.applying&&s.remotePlay){
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;
            const auto old=[&](int64_t stamp){return stamp>0&&now-stamp>10000000;};const auto& r=s.remoteStream;
            if(old(r.lastVideo100ns))progress=L"等待 PS5 视频输入";
            else if(old(r.decodeStarted100ns))progress=L"解码调用尚未返回";
            else if(old(r.lastDecoded100ns))progress=r.video.waitingForIdr?L"等待关键帧恢复":L"收到视频，等待解码输出";
            else if(f.lastSubmit100ns>f.lastReady100ns&&old(f.lastSubmit100ns))progress=L"等待 GPU 完成观测";
            else if(old(f.lastSubmit100ns))progress=L"已解码，增强提交停顿";
            else if(old(f.lastPresent100ns))progress=L"GPU已就绪，等待呈现";
        }
        rows.emplace_back(L"状态",progress);
        if(playing&&s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Dlss&&s.applied.multiplier>1&&s.previewFgMultiplier>1)
            rows.emplace_back(L"补帧倍率 · 目标 / 调度",std::format(L"{}X / {}X",s.applied.multiplier,s.previewFgMultiplier));
        if(!s.capture)rows.emplace_back(L"媒体播放速度",playing?std::format(L"{:.2f} x",s.playbackSpeed):L"未测");
        if(!s.capture&&!s.image){
            rows.emplace_back(L"预览状态",!playing?L"未运行":s.previewSkipped?std::format(L"已跳帧 {}",s.previewSkipped):s.fgBudgetLimited?L"补帧预算不足":L"正常");
            if(s.xessGenerationSuppressed)rows.emplace_back(L"XeSS补帧",L"欠速暂停生成 · 实验");
        }
        rows.emplace_back(L"输入",s.remotePlay?std::format(L"PS5 · {:.1f} fps",s.remoteStream.receivedFps):s.capture?std::format(L"采集 · {:.1f} fps",s.captureFps):L"文件 / 图片");
        rows.emplace_back(L"链路耗时",state->advanced?L"均值 / P95 · ms":L"最近一秒平均");
        rows.emplace_back(s.remotePlay?L"① PS5实际解码":L"① 文件取帧 / 解码",s.remotePlay?ms(s.remoteStream.decodeMeanMs):timing(f.cpuTiming[size_t(diagnostics::CpuStage::Decode)]));
        if(s.remotePlay)rows.emplace_back(L"解码后等待增强",timing(f.cpuTiming[size_t(diagnostics::CpuStage::DecodedQueue)]));
        const diagnostics::GpuStage stages[]={diagnostics::GpuStage::Color,diagnostics::GpuStage::Sr,diagnostics::GpuStage::Flow,diagnostics::GpuStage::Nr,diagnostics::GpuStage::Residual,diagnostics::GpuStage::FgBatch,diagnostics::GpuStage::Blit};
        const wchar_t* names[]={L"② 输入颜色",L"③ 超分 SR",L"④ 光流队列区间",L"⑤ NR 增强",L"⑥ 残差合成",L"⑦ 补帧 FG",L"⑧ 输出合成"};
        for(size_t i=0;i<std::size(stages);++i){const auto& sample=s.metrics.gpu[size_t(stages[i])];
            // Present-sink FG (XeSS/FSR) is timed application-side; only fall
            // back to the placeholder while no sample has been collected.
            const auto value=(xess&&stages[i]==diagnostics::GpuStage::FgBatch&&!f.gpuTiming[size_t(stages[i])].mean)?L"SDK内部不可测":sample.state==diagnostics::SampleState::NotExecuted?L"未执行":timing(f.gpuTiming[size_t(stages[i])]);
            rows.emplace_back(names[i],value);
        }
        rows.emplace_back(L"⑨ 等待显示时间",timing(f.cpuTiming[size_t(diagnostics::CpuStage::DeadlineWait)]));
        rows.emplace_back(L"⑩ Present提交",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Present)]));
        rows.emplace_back(L"统计口径",L"GPU处理与CPU等待可重叠，不相加");
        if(s.remotePlay){
            const auto& r=s.remoteStream;
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;
            auto age=[&](int64_t stamp){return stamp>0?ms(double(std::max<int64_t>(0,now-stamp))/10000):std::wstring(L"尚未收到");};
            rows.emplace_back(L"PS5 串流",L"接收 → 解码 → 呈现");
            rows.emplace_back(L"请求串流格式",std::format(L"{}x{} · {} fps · {}",r.requestedProfile.width,r.requestedProfile.height,r.requestedProfile.fps,r.requestedProfile.codec==remoteplay::Codec::H264?L"H.264":L"H.265"));
            rows.emplace_back(L"请求码率",std::format(L"{:.1f} Mbps",r.requestedProfile.bitrateKbps/1000.0));
            rows.emplace_back(L"实际解码方式",!r.decodeConfirmed?L"等待首帧":r.hardwareDecode?L"D3D12VA 硬解":r.decodeFallback?L"软件解码 · 硬解已回退":L"CPU 软件解码");
            rows.emplace_back(L"接收 / 解码",std::format(L"{:.1f} / {:.1f} fps",r.receivedFps,r.decodedFps));
            rows.emplace_back(L"增强完成",fps(f.sourceCompletedFps));
        rows.emplace_back(L"原帧 / 生成帧呈现",xess?L"显示补帧 SDK 内部合计":std::format(L"{:.1f} / {:.1f} fps",f.realPresentFps,f.generatedPresentFps));
            rows.emplace_back(L"视频有效码率",std::format(L"{:.2f} Mbps",r.videoMbps));
            rows.emplace_back(L"接收后等待解码",ms(r.ingressWaitMeanMs));
            rows.emplace_back(L"距上次视频接收",age(r.lastVideo100ns));
            rows.emplace_back(L"距上次解码完成",age(r.lastDecoded100ns));
            rows.emplace_back(L"距上次呈现",age(f.lastPresent100ns));
            rows.emplace_back(L"串流端到端延迟",L"协议未提供，无法直接测量");
            rows.emplace_back(L"关键帧恢复请求",std::to_wstring(r.video.idrRequests));
            if(state->advanced){
                rows.emplace_back(L"协议报告缺失源帧 · 累计",std::to_wstring(r.framesLost));
                rows.emplace_back(L"参考恢复标记 · 累计",std::to_wstring(r.referenceRecoveryEvents));
                rows.emplace_back(L"压缩队列丢弃 · 累计",std::to_wstring(r.video.dropped));
                rows.emplace_back(L"解码邮箱覆盖 · 累计",std::to_wstring(s.remotePlaySkipped));
                rows.emplace_back(L"FEC 成功 / 实时 RTT",L"当前接口未提供有效测量");
            }
        }
        if(state->advanced){
        rows.emplace_back(live?L"额外显示延迟 · P95":L"画面迟到 · P95",playing?ms(extra.p95):L"未测");
        rows.emplace_back(L"原帧软件驻留 · 平均",playing?ms(f.softwareLatencyMs):L"未测");
        rows.emplace_back(L"原帧软件驻留 · P95",playing?ms(f.softwareLatencyP95Ms):L"未测");
        rows.emplace_back(L"估计基线",L"理想直接播放 · 非双路实测");
        rows.emplace_back(L"GPU完成产出（含过期）",fps(f.outputCompletedFps));
        rows.emplace_back(L"源帧处理完成",fps(f.sourceCompletedFps));
        rows.emplace_back(L"有效生成（含过期）",xess?L"SDK内部不可测":fps(f.validGeneratedFps));
        rows.emplace_back(L"采集覆盖 / 补帧过期",std::format(L"{} / {}",f.counters.mailboxOverwritten,f.counters.generatedExpiredAfterEval));
        rows.emplace_back(L"命令槽等待",timing(f.cpuTiming[size_t(diagnostics::CpuStage::SlotWait)]));
        rows.emplace_back(L"CPU图提交",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Submit)]));
        rows.emplace_back(L"GPU就绪等待（重叠）",timing(f.cpuTiming[size_t(diagnostics::CpuStage::ReadyWait)]));
        rows.emplace_back(f.pairCaptureCallbacks?L"A/B采集到达间隔":L"A/B软件取帧间隔",xess?L"SDK内部不可测":timing(f.pairTiming[size_t(diagnostics::PairTiming::ArrivalInterval)]));
        rows.emplace_back(L"生成呈现距A到达",xess?L"SDK内部不可测":timing(f.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)]));
        rows.emplace_back(L"生成呈现距B到达",xess?L"SDK内部不可测":timing(f.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromB)]));
        rows.emplace_back(L"延迟样本 / 统计溢出",std::format(L"{} / {}",f.latencySamples,f.timingOverflow));
        if(f.reset.sessionId){
            const auto& r=f.reset;
            const auto outcome=r.outcome==diagnostics::ResetOutcome::Completed?L"已恢复有效输出":r.outcome==diagnostics::ResetOutcome::RolledBack?L"已回滚":r.outcome==diagnostics::ResetOutcome::Cancelled?L"已取消":r.outcome==diagnostics::ResetOutcome::Failed?L"失败":L"等待有效输出";
            rows.emplace_back(std::format(L"最近设置{} · {}",r.rebuilt?L"重建":L"重置",r.settingsRevision),outcome);
            rows.emplace_back(L"切换总耗时",ms(r.totalMs));
            const wchar_t* resetNames[]={L"排空",L"销毁资源",L"创建资源",L"首帧预热提交",L"首帧完成观测"};
            for(size_t i=0;i<r.stageMs.size();++i)rows.emplace_back(resetNames[i],ms(r.stageMs[i]));
        }
        const wchar_t* fgName=s.applied.frameGenerationBackend==engine::FrameGenerationBackend::XeSS?L"XeSS"
            :s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Fsr?L"AMD FSR":L"DLSS";
        rows.emplace_back(L"补帧方式",s.applied.multiplier<=1?L"关闭":std::format(L"{} {}X",fgName,s.applied.multiplier));
        rows.emplace_back(L"NR内部尺寸",s.applied.nr?std::format(L"{} x {}",s.metrics.resolution.nr.width,s.metrics.resolution.nr.height):L"关闭");
        rows.emplace_back(L"NR运行版本",s.nrActive?(s.applied.nrRuntime==engine::NrRuntime::Ampere?L"RTX 30兼容 · SF-v2 实验":s.applied.nrRuntime==engine::NrRuntime::Community?L"社区兼容 · 实验":L"默认（Lecram 310.8.3）"):L"未运行");
        rows.emplace_back(L"显示模式",s.running?(s.applied.captureCompatible?L"直播兼容 · 实验":L"标准显示"):L"未运行");
        if(s.audioAvailable)rows.emplace_back(L"音频声道 · 输入 / 输出",std::format(L"{} / {}{}",s.capture?s.captureAudio.inputChannels:s.audioInputChannels,s.capture?s.captureAudio.outputChannels:s.audioOutputChannels,(s.capture?s.captureAudio.outputChannels<s.captureAudio.inputChannels:s.audioOutputChannels<s.audioInputChannels)?L" · 降混":L""));
        rows.emplace_back(L"音频同步",s.audioAvailable?(s.capture?(s.captureAudio.running?L"软件估算同步":L"等待视频锚点"):(s.audioEndpointRecovering?L"音频设备恢复中 · 时间线保持":s.audioRebuffering?L"重新同步 · 等待视频锚点":L"音频主时钟")):L"无音频");
        if(!s.capture&&s.audioAvailable){
            rows.emplace_back(L"音频设备恢复次数",std::to_wstring(s.audioEndpointRecoveries));
            if(FAILED(s.audioEndpointError))rows.emplace_back(L"最近音频设备错误",std::format(L"0x{:08X}",unsigned(s.audioEndpointError)));
        }
        if(!s.capture&&s.audioAvailable)rows.emplace_back(L"声音领先 · 软件估算",std::format(L"{:.1f} ms",s.lateMs));
        if(s.capture&&s.audioAvailable){
            rows.emplace_back(L"音频格式",std::format(L"{} Hz · {} bit / {} valid{}{}",s.captureAudio.inputSampleRate,s.captureAudio.inputContainerBits,s.captureAudio.inputValidBits,s.captureAudio.inputFloating?L" · Float":L"",s.captureAudio.inputBitstream.empty()?L"":std::format(L" · 位流解码为 {} 声道 ({})",s.captureAudio.inputChannels,s.captureAudio.inputBitstream)));
            rows.emplace_back(s.captureAudio.syncClockFallback?L"估算偏差 · 本机时钟":L"音画偏差 · 声音领先",ms(s.captureAudio.skewMs));
            rows.emplace_back(L"声音补偿",std::format(L"{:.1f} ms{}",s.captureAudio.compensationMs,s.captureAudio.limited?L" · 已达边界":L""));
            if(s.captureAudio.syncClockFallback)rows.emplace_back(L"同步状态",L"时间戳异常回退 · 本机延迟估算");
            rows.emplace_back(L"PCM队列",std::format(L"{:.1f} ms",s.captureAudio.bufferedMs));
            rows.emplace_back(L"音频设备队列",std::format(L"{:.1f} ms",s.captureAudio.endpointBufferedMs));
            rows.emplace_back(L"音频重锚 / 溢出",std::format(L"{} / {}",s.captureAudio.resets,s.captureAudio.overflows));
            if(s.captureAudio.recoveryDiscardedFrames)rows.emplace_back(L"恢复时跳过的旧音频",std::format(L"{:.1f} ms",s.captureAudio.recoveryDiscardedFrames/48.0));
            rows.emplace_back(L"欠载 / 可测缺口 / 插入静音",std::format(L"{} / {:.1f} / {:.1f} ms",s.captureAudio.underruns,s.captureAudio.underrunFrames/48.0,s.captureAudio.silenceFrames/48.0));
            if(s.captureAudio.clockStalledGaps)rows.emplace_back(L"时钟未量出时长的断流",std::to_wstring(s.captureAudio.clockStalledGaps));
            rows.emplace_back(L"转换峰值 / 超满幅 / 异常",std::format(L"{:.4f} / {} / {}",s.captureAudio.inputPeak,s.captureAudio.overRangeSamples,s.captureAudio.nonFiniteSamples+s.captureAudio.invalidPaddingSamples));
        }
        if(!s.colorStatus.empty())rows.emplace_back(L"实际颜色链路",s.colorStatus);
        if(!s.sourceNotice.empty())rows.emplace_back(L"片源兼容",s.sourceNotice);
        if(!s.backendWarning.empty())rows.emplace_back(L"后端状态",s.backendWarning);
        if(!s.captureAudio.error.empty())rows.emplace_back(L"采集音频异常",s.captureAudio.error);
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
    return CreateWindowExW(0,wc.lpszClassName,L"实时处理状态",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,&engine);
}
}
