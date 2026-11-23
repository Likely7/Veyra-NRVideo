#include "veyra/source/RemotePlaySessionSource.h"
#include "veyra/Log.h"
#include "veyra/remoteplay/StreamRecovery.h"
#include <cmath>
#include <format>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
}
namespace veyra::source {
namespace {
std::wstring sessionFailureDetail(const remoteplay::NativeSnapshot& native, int errorCode) {
    using Advice=remoteplay::NativeSnapshot::SessionAdvice;
    std::wstring message=std::format(L"（终止码 {} / 错误 {}）",native.lastQuitReason,errorCode);
    if(native.sessionAdvice!=Advice::None)message+=L"\n";
    switch(native.sessionAdvice) {
    case Advice::Account:
        message+=L"主机拒绝了 Account ID，请在获取指南中使用当前配对用户的 PSN 账号重新查询。";
        break;
    case Advice::Registration:
        message+=L"主机拒绝了已保存的配对，请使用同一 PSN 账号和主机新生成的配对码重新配对。";
        break;
    case Advice::InUse:
        message+=L"请结束其他远程游玩会话，等待主机释放连接后重试。";
        break;
    case Advice::Version:
        message+=L"主机报告远程游玩协议版本不匹配，请检查主机系统和 Veyra 版本。";
        break;
    case Advice::Rejected:
        message+=L"主机未提供可识别的拒绝原因，请将本次连接的诊断日志提供给维护者。";
        break;
    case Advice::None: break;
    }
    return message;
}
}

bool RemotePlaySessionSource::connect(RemotePlayConnectDesc desc) {
    close();
    { std::lock_guard lock(mutex_); initialized_=started_=failed_=false; skipped_=publishedSequence_=0; recovery_={}; rates_={}; feedback_={}; controller_={}; pendingControllers_.clear(); snapshot_={}; }
    owner_=std::jthread([this, request=std::move(desc)](std::stop_token stop) mutable { run(stop,std::move(request)); });
    std::unique_lock lock(mutex_);
    ready_.wait(lock,[&]{return initialized_;});
    info_=publishedInfo_;
    return started_;
}
void RemotePlaySessionSource::run(std::stop_token stop, RemotePlayConnectDesc desc) {
    alive_.store(true);
    RemotePlaySource source;
    remoteplay::StreamRecovery recovery;
    bool repeatedTestDisconnect=false;
    const auto milliseconds=[] {return remoteplay::monotonic100ns()/10000;};
    // Keep the existing in-memory credentials only for this user-started run.
    // Their move-only destructor wipes them; never reread another saved profile.
    for(;;){
    recovery.beginAttempt(milliseconds());
    bool retry=false;
    // RP_IN_USE needs its own, much longer backoff: the console keeps the old
    // session slot for roughly twenty seconds after it ends.
    bool retryWasRpInUse=false;
    // A live (connected) session ended by a decode failure or stall: the console
    // is still tearing it down, so the retry needs the same long wait.
    bool retryAfterLiveSession=false;
    uint64_t receivedBase=0,droppedBase=0;
    {std::lock_guard lock(mutex_);receivedBase=rates_.received;droppedBase=rates_.ingressDropped;}
    std::jthread feeder;
    std::jthread monitor;
    try {
        const bool ok=source.connect(desc);
        if(!ok){retryWasRpInUse=source.nativeSnapshot().startupRetryAllowed;retryAfterLiveSession=false;retry=recovery.poll(milliseconds(),false,true)==remoteplay::StreamRecovery::Action::Reconnect;}
        {std::lock_guard lock(mutex_);publishedInfo_=source.info();if(!initialized_)started_=ok;failed_=!ok&&!retry;initialized_=true;telemetryInbox_=source.telemetryInbox();
            if(failed_)recovery_.message=desc.controlOnly?L"PS5 手柄控制连接失败，请检查主机及网络；已保存的配对无需重输。":L"PS5 串流连接失败，请检查主机及网络；已保存的配对无需重输。";}
        ready_.notify_all();
        if(ok&&desc.controlOnly) {
            // PS5 手柄控制: the capture card is the picture. No decoder, no WASAPI,
            // no mailbox: the backend counts and drops the console's keepalive
            // video, and that count is the progress StreamRecovery watches.
            log::info("remoteplay-control",std::format("controller-only session attempt={} keepalive={}x{}@{} {} kbps (dropped on receipt; the console still sends it)",recovery.reconnects(),desc.request.video.width,desc.request.video.height,desc.request.video.fps,desc.request.video.bitrateKbps));
            // Sleep(1) without a raised timer resolution can take 15.6 ms; the
            // controller is sent every 4 ms, so wait on a high-resolution timer.
            const auto timer=std::unique_ptr<void,decltype(&CloseHandle)>(CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS),&CloseHandle);
            auto nextController=std::chrono::steady_clock::now();
            auto nextLog=nextController,rateStart=nextController;
            uint64_t previousReceived=receivedBase,lastVideo=0;
            bool connectedLogged=false;
            while(!stop.stop_requested()) {
                std::string pin;
                {std::lock_guard lock(mutex_);pin.swap(pin_);}
                if(!pin.empty()) {
                    const auto r=source.submitLoginPin(pin);SecureZeroMemory(pin.data(),pin.size());
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                }
                auto now=std::chrono::steady_clock::now();
                if(now>=nextController) {
                    remoteplay::ControllerState controller;
                    {std::lock_guard lock(mutex_);controller=takeControllerLocked(remoteplay::monotonic100ns());}
                    const auto r=source.submitController(controller);
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                    nextController=now+std::chrono::milliseconds(4);
                }
                const auto native=source.nativeSnapshot();
                const auto snapshot=source.sessionSnapshot();
                if(now>=nextLog){nextLog=now+std::chrono::seconds(1);
                    log::info("remoteplay-transport",std::format("attempt={} connected={} videoCallbacks={} audioCallbacks={} packetWindowReceived={} packetWindowLost={} upstreamWarnings={} upstreamErrors={} transportErrors={} quitReason={} apiError={} (controller-only; media dropped on receipt)",recovery.reconnects(),native.connected,native.videoCallbacks,native.audioCallbacks,native.packetReceived,native.packetLost,native.warnings,native.errors,native.transportErrors,native.lastQuitReason,native.lastApiError));}
                if(native.connected&&!connectedLogged){connectedLogged=true;log::info("remoteplay-control","console connected; controller channel open");}
                {std::lock_guard lock(mutex_);snapshot_=snapshot;feedback_.merge(source.takeFeedback());
                    rates_.received=receivedBase+native.videoCallbacks;
                    const double elapsed=std::chrono::duration<double>(now-rateStart).count();
                    if(elapsed>=1){rates_.receivedFps=double(rates_.received-previousReceived)/elapsed;rates_.ready=true;previousReceived=rates_.received;rateStart=now;}
                }
                if(native.videoCallbacks>lastVideo) {
                    lastVideo=native.videoCallbacks;
                    if(recovery.frame(milliseconds()))log::info("remoteplay-recovery",std::format("retry allowance renewed after 30s continuous keepalive; lifetimeAttempts={}",recovery.reconnects()));
                    std::lock_guard lock(mutex_);recovery_.active=false;recovery_.message.clear();
                } else {
                    // Keyframe actions are ignored: there is no decoder to repair.
                    const auto action=recovery.poll(milliseconds(),snapshot.state==remoteplay::SessionState::LoginPinRequired,snapshot.state==remoteplay::SessionState::Failed,native.automaticRetryAllowed,native.startupRetryAllowed);
                    if(action==remoteplay::StreamRecovery::Action::Reconnect){retryWasRpInUse=native.startupRetryAllowed;retryAfterLiveSession=false;retry=true;break;}
                    if(action==remoteplay::StreamRecovery::Action::Fail){
                        log::error("remoteplay-recovery",std::format("controller-only recovery stopped attempts={} quitReason={} error={} applicationReason=0x{:08X}",recovery.reconnects(),native.lastQuitReason,snapshot.errorCode,native.applicationReason));
                        std::lock_guard lock(mutex_);failed_=true;recovery_.active=false;
                        recovery_.message=snapshot.state==remoteplay::SessionState::LoginPinRequired?L"等待 PS5 登录 PIN 超时，请重新连接。":native.startupRetryAllowed?L"PS5 仍被串流会话占用，请结束其他串流或稍后重试；无需重新配对。":!native.automaticRetryAllowed?L"PS5 已结束或拒绝串流，请检查主机状态后重新连接。":L"PS5 手柄控制没有恢复，请检查主机及网络后重新打开；无需重新配对。";
                        recovery_.message+=sessionFailureDetail(native,snapshot.errorCode);break;
                    }
                }
                now=std::chrono::steady_clock::now();
                const auto wait=std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(nextController-now).count(),100,4000);
                if(timer){LARGE_INTEGER due;due.QuadPart=-wait*10;if(SetWaitableTimer(timer.get(),&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer.get(),20);else Sleep(1);}
                else Sleep(1);
            }
        } else if(ok) {
            monitor=std::jthread([inbox=source.telemetryInbox()](std::stop_token cancel){
                while(!cancel.stop_requested()){
                    const auto s=inbox->snapshot();const auto now=remoteplay::monotonic100ns();
                    const auto age=[&](auto stamp){return stamp>0?double(now-stamp)/10000:-1.0;};
                    log::info("remoteplay-progress",std::format("state={} received={} decoded={} inputFps={:.1f} decodeFps={:.1f} videoMbps={:.3f} receiveAgeMs={:.1f} decodeAgeMs={:.1f} decodeBusyMs={:.1f} queue={} waitingIdr={} idrRequests={} errors={} (complete-video callback; not network latency)",int(s.state),s.video.accessUnits,s.decodedFrames,s.receivedFps,s.decodedFps,s.videoMbps,age(s.lastVideo100ns),age(s.lastDecoded100ns),age(s.decodeStarted100ns),s.video.depth,s.video.waitingForIdr,s.video.idrRequests,s.errorCode));
                    for(int i=0;i<10&&!cancel.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
            WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;
            format.nSamplesPerSec=48000;format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=384000;
            if(audio_.configure(format)&&audio_.start()) {
                // Separate from both decoder and GPU owners. Source is closed
                // only after this reader and the WASAPI owner have joined.
                feeder=std::jthread([this,&source](std::stop_token cancel){
                    try {
                        float pcm[960]; double next=-1;
                        while(!cancel.stop_requested()) {
                            double pts=0;const auto count=source.pullAudio(pcm,480,&pts);
                            if(count) {
                                const bool gap=next>=0&&std::abs(pts-next)>0.5;
                                if(!audio_.push(pcm,count*2*sizeof(float),pts,gap))
                                    log::warn("remoteplay-audio","PCM queue rejected block");
                                next=pts+1000.0*double(count)/48000;
                            } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                    } catch(const std::exception& e) {log::error("remoteplay-audio",e.what());}
                });
            } else log::error("remoteplay-audio","output initialization failed; video remains available");
            auto lastFrame=std::chrono::steady_clock::now();
            auto nextController=lastFrame;
            auto rateStart=lastFrame;uint64_t previousReceived=receivedBase,previousDecoded=0;
            {std::lock_guard lock(mutex_);previousDecoded=rates_.decoded;}
            auto nextNativeLog=lastFrame;
            remoteplay::NativeSnapshot native;
            bool firstFrame=true;
            wchar_t disconnectAfter[24]{};uint64_t testDisconnectFrames=0;bool testDisconnected=false;
            if(!recovery.reconnects()&&GetEnvironmentVariableW(L"VEYRA_TEST_REMOTEPLAY_DISCONNECT_AFTER_FRAMES",disconnectAfter,24))testDisconnectFrames=std::clamp<uint64_t>(_wtoi64(disconnectAfter),60,3600);
            while(!stop.stop_requested()) {
                std::string pin;
                {std::lock_guard lock(mutex_);pin.swap(pin_);}
                if(!pin.empty()) {
                    const auto r=source.submitLoginPin(pin);SecureZeroMemory(pin.data(),pin.size());
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                }
                const auto now=std::chrono::steady_clock::now();
                if(!desc.request.viewOnly&&now>=nextController) {
                    remoteplay::ControllerState controller;
                    {std::lock_guard lock(mutex_);controller=takeControllerLocked(remoteplay::monotonic100ns());}
                    const auto r=source.submitController(controller);
                    if(!r.ok)log::warn("remoteplay",std::format("{} code={}",r.operation,r.code));
                    nextController=now+std::chrono::milliseconds(4);
                }
                pipeline::FramePacket packet; const AVFrame* frame=nullptr;
                const auto result=source.read(packet,&frame);
                auto feedback=source.takeFeedback();
                const auto snapshot=source.sessionSnapshot();
                if(now>=nextNativeLog||result==SourceReadStatus::Error){native=source.nativeSnapshot();nextNativeLog=now+std::chrono::seconds(1);
                    log::info("remoteplay-transport",std::format(
                        "attempt={} connected={} videoCallbacks={} audioCallbacks={} callbackRejected={} "
                        "packetWindowReceived={} packetWindowLost={} upstreamWarnings={} upstreamErrors={} "
                        "transportErrors={} assemblyErrors={} quitReason={} apiError={} "
                        "queueRecoveries={} queueHighWater={} queueDropped={} waitingIdr={} (upstream resetting packet window; not cumulative UDP)",
                        recovery.reconnects(),native.connected,native.videoCallbacks,native.audioCallbacks,native.callbackRejected,
                        native.packetReceived,native.packetLost,native.warnings,native.errors,native.transportErrors,
                        native.assemblyErrors,native.lastQuitReason,native.lastApiError,
                        snapshot.video.recoveries,snapshot.video.highWaterDepth,snapshot.video.dropped,snapshot.video.waitingForIdr));}
                {std::lock_guard lock(mutex_);snapshot_=snapshot;feedback_.merge(std::move(feedback));
                    rates_.received=receivedBase+snapshot.video.accessUnits;rates_.ingressDropped=droppedBase+snapshot.video.dropped;
                    const double elapsed=std::chrono::duration<double>(now-rateStart).count();
                    if(elapsed>=1){rates_.receivedFps=double(rates_.received-previousReceived)/elapsed;
                        rates_.decodedFps=double(rates_.decoded-previousDecoded)/elapsed;rates_.ready=true;
                        previousReceived=rates_.received;previousDecoded=rates_.decoded;rateStart=now;}
                }
                if(result==SourceReadStatus::Frame && frame) {
                    packet.sourceEpoch+=uint64_t(recovery.reconnects())<<32;
                    if(firstFrame&&recovery.reconnects())packet.flags|=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
                    if(firstFrame&&recovery.reconnects())log::info("remoteplay-recovery",std::format("reconnected attempt={} first decoded frame; reset video history and audio clock",recovery.reconnects()));
                    firstFrame=false;
                    publishDecoded(frame,packet,source.info());
                    lastFrame=now;
                    const bool renewed=recovery.frame(milliseconds());
                    if(renewed){
                        log::info("remoteplay-recovery",std::format("retry allowance renewed after 30s continuous decoded progress; lifetimeAttempts={}",recovery.reconnects()));
                        if(!repeatedTestDisconnect&&GetEnvironmentVariableW(L"VEYRA_TEST_REMOTEPLAY_REPEAT_OUTAGE",nullptr,0)){
                            repeatedTestDisconnect=true;const auto r=source.disconnectTransportForTest();
                            log::warn("remoteplay-recovery-test",std::format("second owned transport stop after renewal ok={} code={}",r.ok,r.code));
                        }
                    }
                    {std::lock_guard lock(mutex_);recovery_.active=false;recovery_.message.clear();}
                    if(testDisconnectFrames&&!testDisconnected&&snapshot.decodedFrames>=testDisconnectFrames){
                        testDisconnected=true;const auto r=source.disconnectTransportForTest();
                        log::warn("remoteplay-recovery-test",std::format("injected owned transport stop ok={} code={} (no console power command)",r.ok,r.code));
                    }
                } else {
                    const auto action=recovery.poll(milliseconds(),snapshot.state==remoteplay::SessionState::LoginPinRequired,result==SourceReadStatus::Error,native.automaticRetryAllowed,native.startupRetryAllowed);
                    if(action==remoteplay::StreamRecovery::Action::Keyframe){
                        source.recoverVideo();log::warn("remoteplay-recovery",std::format("request keyframe; received={} decoded={} videoCallbacks={} rejected={} packetReceived={} packetLost={}",snapshot.video.accessUnits,snapshot.decodedFrames,native.videoCallbacks,native.callbackRejected,native.packetReceived,native.packetLost));
                        std::lock_guard lock(mutex_);recovery_.active=true;recovery_.message=L"画面中断，正在请求关键帧恢复…";
                    }else if(action==remoteplay::StreamRecovery::Action::Reconnect){retryWasRpInUse=native.startupRetryAllowed;retryAfterLiveSession=true;retry=true;break;}
                    else if(action==remoteplay::StreamRecovery::Action::Fail){
                        log::error("remoteplay-recovery",std::format("recovery stopped attempts={} quitReason={} error={} applicationReason=0x{:08X}",recovery.reconnects(),native.lastQuitReason,snapshot.errorCode,native.applicationReason));
                        std::lock_guard lock(mutex_);failed_=true;recovery_.active=false;
                        recovery_.message=snapshot.state==remoteplay::SessionState::LoginPinRequired?L"等待 PS5 登录 PIN 超时，请重新连接。":native.startupRetryAllowed?L"PS5 仍被串流会话占用，请结束其他串流或稍后重试；无需重新配对。":!native.automaticRetryAllowed?L"PS5 已结束或拒绝串流，请检查主机状态后重新连接。":L"PS5 串流未恢复，请检查主机及网络后点击连接；无需重新配对。";
                        recovery_.message+=sessionFailureDetail(native,snapshot.errorCode);break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
    } catch(const std::exception& e) {
        log::error("remoteplay",e.what());
        retry=false;
        {std::lock_guard lock(mutex_);failed_=true;initialized_=true;recovery_.message=L"PS5 串流处理异常，已停止自动恢复，请查看日志。";}
        ready_.notify_all();
    }
    if(retry&&!stop.stop_requested()){
        std::lock_guard lock(mutex_);
        recovery_={true,recovery.episodeRetries(),retryWasRpInUse
            ?std::format(L"PS5 正在释放上一个串流会话，等待后自动重试（{}/3）…",recovery.episodeRetries())
            :desc.controlOnly?std::format(L"PS5 手柄控制中断，正在重新连接（{}/3）…",recovery.episodeRetries())
            :std::format(L"串流中断，正在重新连接（{}/3）…",recovery.episodeRetries())};
        latest_.reset();pendingControllers_.clear();controller_={};feedback_={};
    }
    log::info("remoteplay-recovery",std::format("teardown begin attempt={} retry={} cancelled={}",recovery.reconnects(),retry,stop.stop_requested()));
    if(monitor.joinable()){monitor.request_stop();monitor.join();}
    if(feeder.joinable()){feeder.request_stop();feeder.join();}
    audio_.stop();
    source.close();
    log::info("remoteplay-recovery",std::format("teardown complete attempt={}",recovery.reconnects()));
    if(!retry||stop.stop_requested())break;
    log::warn("remoteplay-recovery",std::format("old session joined; reconnect={} using existing in-memory pairing",recovery.reconnects()));
    // Interruptible backoff; manual Stop prevents the next start. The console
    // keeps the previous session slot for ~20 s after it ends, so an RP_IN_USE
    // refusal gets 10/20/40 s instead of the generic 1/2/4 s: the short budget
    // spent all three retries inside that window (field log 2026-09-17: three
    // refusals in 11 s, while a 25 s wait reconnected fine).
    // A retry after a live session (decode failure/stall) also lands inside that
    // window: field log 2026-11-22 showed 1 s and 4 s waits ending in
    // quitReason=4 error=2004 and the third attempt budget gone. It waits a
    // fixed 20 s; the stop check below keeps it cancellable within 50 ms.
    constexpr unsigned kLiveSessionBackoffSeconds=20;
    const unsigned backoffSeconds=retryWasRpInUse?(10u<<(recovery.episodeRetries()-1)):retryAfterLiveSession?kLiveSessionBackoffSeconds:(1u<<(recovery.episodeRetries()-1));
    if(retryAfterLiveSession&&!retryWasRpInUse)log::warn("remoteplay-recovery",std::format("live session ended; PS5 is still releasing it, waiting {}s before retry {}/3",backoffSeconds,recovery.episodeRetries()));
    if(retryWasRpInUse)log::warn("remoteplay-recovery",std::format("PS5 still holds the previous session; waiting {}s before retry {}/3",backoffSeconds,recovery.episodeRetries()));
    for(unsigned i=0;i<backoffSeconds*20&&!stop.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if(stop.stop_requested())break;
    }
    {std::lock_guard lock(mutex_);recovery_.active=false;}
    // run() has no early return: the owner loop and its retries are over.
    alive_.store(false);
}
void RemotePlaySessionSource::publishDecoded(const AVFrame* frame,pipeline::FramePacket packet,const SourceInfo& info){
    auto* cloned=av_frame_clone(frame);if(!cloned)throw std::bad_alloc();
    Frame item{std::shared_ptr<AVFrame>(cloned,[](AVFrame* p){av_frame_free(&p);}),packet,info};
    std::lock_guard lock(mutex_);
    item.packet.sequence=++publishedSequence_;
    ++rates_.decoded;
    if(latest_){++skipped_;item.packet.flags|=latest_->packet.flags;
        item.packet.flags|=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);}
    latest_=std::move(item);
}
SourceReadStatus RemotePlaySessionSource::read(pipeline::FramePacket& packet,const AVFrame** frame) {
    if(frame)*frame=nullptr;
    std::lock_guard lock(mutex_);
    if(failed_)return SourceReadStatus::Error;
    if(!latest_)return started_?SourceReadStatus::Waiting:SourceReadStatus::Error;
    packet=latest_->packet;info_=latest_->info;view_=std::move(latest_->frame);latest_.reset();
    if(frame)*frame=view_.get();return SourceReadStatus::Frame;
}
void RemotePlaySessionSource::close() noexcept {
    if(owner_.joinable()){owner_.request_stop();owner_.join();}
    std::lock_guard lock(mutex_);latest_.reset();view_.reset();info_={};started_=false;
    SecureZeroMemory(pin_.data(),pin_.size());pin_.clear();
}
void RemotePlaySessionSource::controller(remoteplay::ControllerState state){
    std::lock_guard lock(mutex_);controllerStamp_=remoteplay::monotonic100ns();
    // A neutral input (focus/device loss) cancels queued actions immediately.
    if(!state.inputActive)pendingControllers_.clear();
    if(!pendingControllers_.empty()){
        auto& last=pendingControllers_.back();
        const auto& previous=last.second;
        // Keep real press/release and touch-contact edges. Axes, pressure,
        // touch motion and sensor updates are current state, not a FIFO of
        // actions: a 125 Hz UI must not build latency behind a 60 Hz owner.
        const bool sameEdges=state.inputActive==previous.inputActive&&state.buttons==previous.buttons&&
            bool(state.l2)==bool(previous.l2)&&bool(state.r2)==bool(previous.r2)&&
            state.touches[0].id==previous.touches[0].id&&state.touches[1].id==previous.touches[1].id;
        if(sameEdges){
            // Retain a bounded ordered sensor batch across coalesced UI polls.
            if(state.motionValid&&previous.motionValid){
                std::array<remoteplay::ControllerState::MotionSample,16> samples{};uint8_t count=0;
                const auto append=[&](const auto& sample){
                    if(!sample.timestampUs||(count&&sample.timestampUs<=samples[count-1].timestampUs))return;
                    if(count==samples.size()){std::move(samples.begin()+1,samples.end(),samples.begin());--count;}
                    samples[count++]=sample;
                };
                for(size_t i=0;i<std::min(size_t(previous.motionSampleCount),previous.motionSamples.size());++i)append(previous.motionSamples[i]);
                for(size_t i=0;i<std::min(size_t(state.motionSampleCount),state.motionSamples.size());++i)append(state.motionSamples[i]);
                state.motionSamples=samples;state.motionSampleCount=count;
            }
            last={controllerStamp_,std::move(state)};return;
        }
    }
    if(pendingControllers_.size()>=16){
        pendingControllers_.clear();pendingControllers_.push_back({controllerStamp_,{}});
        log::warn("remoteplay-input","Controller queue overflow; releasing before latest state");
    }
    pendingControllers_.push_back({controllerStamp_,std::move(state)});
}
remoteplay::ControllerState RemotePlaySessionSource::takeControllerLocked(remoteplay::HostTime now){
    if(now-controllerStamp_>1000000){pendingControllers_.clear();controller_={};return controller_;}
    // Remove only expired states so a fresh press and release still reach the
    // console in order, even when the decoder owner has stalled.
    while(!pendingControllers_.empty()&&now-pendingControllers_.front().first>1000000)
        pendingControllers_.pop_front();
    if(!pendingControllers_.empty()){controller_=std::move(pendingControllers_.front().second);pendingControllers_.pop_front();}
    return controller_;
}
void RemotePlaySessionSource::loginPin(std::string pin){std::lock_guard lock(mutex_);SecureZeroMemory(pin_.data(),pin_.size());pin_=std::move(pin);}
remoteplay::SessionInbox::Snapshot RemotePlaySessionSource::sessionSnapshot()const{
    std::shared_ptr<const remoteplay::SessionInbox> inbox;{std::lock_guard lock(mutex_);inbox=telemetryInbox_;if(!inbox)return snapshot_;}
    return inbox->snapshot();
}
RemotePlaySessionSource::Rates RemotePlaySessionSource::rates()const{std::lock_guard lock(mutex_);return rates_;}
remoteplay::ControllerFeedback RemotePlaySessionSource::takeFeedback(){std::lock_guard lock(mutex_);auto result=std::move(feedback_);feedback_={};return result;}
uint64_t RemotePlaySessionSource::skipped()const{std::lock_guard lock(mutex_);return skipped_;}
}
