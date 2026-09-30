#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/BackendRecovery.h"
#include "veyra/diagnostics/DiagnosticEvent.h"
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/diagnostics/ResetCause.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/LiveFgAdmission.h"
#include "veyra/engine/FrameFlowWindow.h"
#include "veyra/engine/EnhancementDelayEstimate.h"
#include "veyra/ThunkHook.h"
#include "veyra/source/CaptureFormatRank.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>

namespace {
int thunkHookCalls = 0;
int (*thunkOriginal)(int) = nullptr;
int thunkTargetFunction(int value) { return value + 1; }
int thunkReplacementFunction(int value) { ++thunkHookCalls; return thunkOriginal ? thunkOriginal(value) + 100 : -1; }
}
#include <windows.h>
#include "veyra/engine/ContentCadence.h"
#include "veyra/diagnostics/Redaction.h"
#include <limits>
#include <vector>
#include "veyra/engine/CfrTimeline.h"
#include "veyra/Log.h"
#include "veyra/sink/AudioFrameTimeline.h"
#include "veyra/sink/ArrivalClockMapping.h"
#include "veyra/sink/CaptureSyncTarget.h"
#include "veyra/source/DolbyVision.h"
#include "veyra/source/AudioInputRecovery.h"
#include "veyra/engine/Subtitles.h"
#include "veyra/engine/PresentationSettings.h"
#include "veyra/engine/PresentationGeometry.h"
int main(){
    using namespace veyra;int failures=0,checks=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
    {
        engine::PreviewView v;v.displayAspect=16.0/9;
        auto [w,h]=v.renderedSize(1000,1000,1440,1080);
        check(std::abs(w-1000)<.01&&std::abs(h-562.5)<.01,"anamorphic source uses reported DAR");
        const auto before=v.sourcePoint(850,420,1000,1000,1440,1080);
        v.wheel(2,850,420,1000,1000,1440,1080);
        const auto after=v.sourcePoint(850,420,1000,1000,1440,1080);
        check(std::abs(before.first-after.first)<.00001&&std::abs(before.second-after.second)<.00001,"DAR zoom preserves cursor source coordinate");
        v.zoom=1;v.mode=1;auto native=v.renderedSize(1000,1000,1440,1080);
        check(native.first==1440&&native.second==1080,"native mode maps each output pixel once");
        v.mode=2;auto fill=v.renderedSize(1000,1000,1440,1080);
        check(std::abs(fill.first-1777.7778f)<.01&&fill.second==1000,"fill uses DAR and crops");
        v.mode=3;auto stretch=v.renderedSize(1000,1000,1440,1080);
        check(stretch.first==1000&&stretch.second==1000,"stretch fills client independently");
        for(int mode=4;mode<=6;++mode){v.mode=mode;const auto size=v.renderedSize(1000,1000,1440,1080);
            const double ratio=mode==4?16.0/9:mode==5?4.0/3:21.0/9;
            check(std::abs(size.first/size.second-ratio)<.00001,"forced display ratio shares preview transform");}
    }
    check(std::wstring_view(engine::ngxFailureHint(0xFFFFFFFFBAD00002ull)).find(L"驱动")!=std::wstring_view::npos,"signed NGX PlatformError reports actual driver/platform failure");
    check(std::wstring_view(engine::ngxFailureHint(0xBAD00001)).find(L"不支持")!=std::wstring_view::npos,"NGX FeatureNotSupported has a distinct visible reason");
    for(bool lowQueue:{false,true})for(bool fullscreen:{false,true})for(auto sync:{engine::DisplaySync::Tearing,engine::DisplaySync::Vsync,engine::DisplaySync::Automatic}) {
        engine::PresentationSettings s;s.enabled=lowQueue;s.fullscreen=fullscreen;s.display=sync;
        check(engine::presentationVsync(s)==(sync==engine::DisplaySync::Vsync)&&engine::presentationTearing(s)==(sync==engine::DisplaySync::Tearing),"VSync and tearing only when explicitly selected; Automatic is neither, windowed or fullscreen");
    }
    {
        source::AudioInputRecovery retry;retry.reset(1000);
        check(!retry.due(0,3999)&&retry.due(0,4000),"missing initial PCM retries after bounded startup grace");
        check(!retry.due(0,4500)&&retry.due(0,5000),"audio retry has bounded backoff instead of every video frame");
        check(!retry.due(1,5100)&&!retry.due(2,8100),"silent PCM packet progress prevents reconnect");
        check(!retry.due(2,11099)&&retry.due(2,11100),"audio-only PCM stall is detected while video may continue");
    }
    {
        engine::EnhancementSettings s;s.nr=s.sr=true;s.multiplier=2;
        check(engine::disableUnsupportedNvidiaEffects(s,false)&&!s.nr&&!s.sr&&s.multiplier==1,"unsupported NVIDIA effects are removed from actual settings");
        s.nr=s.sr=true;s.multiplier=2;s.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
        check(engine::disableUnsupportedNvidiaEffects(s,false)&&!s.nr&&!s.sr&&s.multiplier==2,"non-NVIDIA normalization retains independently selected XeSS");
        s.nr=true;check(!engine::disableUnsupportedNvidiaEffects(s,true)&&s.nr,"NVIDIA capability normalization retains supported effects");
    }
    {
        engine::EnhancementSettings requested;requested.nr=requested.sr=true;requested.multiplier=2;requested.audioOffsetMs=37;
        auto recovered=requested;
        check(engine::disableFailedBackend(recovered,engine::FailedBackend::Fg)&&recovered.nr&&recovered.sr&&recovered.multiplier==1&&recovered.audioOffsetMs==37,"FG failure preserves NR, SR and independent audio settings");
        recovered=requested;recovered.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
        check(engine::disableFailedBackend(recovered,engine::FailedBackend::NgxCore)&&!recovered.nr&&!recovered.sr&&recovered.multiplier==2,"NGX failure does not disable independent XeSS backend");
        check(!engine::disableFailedBackend(recovered,engine::FailedBackend::Infrastructure),"device and color failures cannot silently degrade into success");
        recovered=requested;recovered.videoSrQuality=2;
        check(engine::disableFailedBackend(recovered,engine::FailedBackend::OpticalFlow)&&!recovered.nr&&recovered.sr&&recovered.multiplier==1,"flow failure disables dependent consumers while retaining spatial video SR");
        check(!engine::disableFailedBackend(recovered,engine::FailedBackend::OpticalFlow),"repeated failure cannot create an unbounded recovery loop");
        source::DolbyVisionInfo dv;dv.present=dv.baseLayer=true;dv.profile=5;dv.compatibility=1;
        check(dv.route()==source::DolbyBaseLayer::Unsupported,"DV P5 cannot be relabelled as HDR10 even with a conflicting compatibility id");
        dv.profile=8;
        check(dv.route()==source::DolbyBaseLayer::Hdr10,"DV P8.1 selects HDR10 base-layer compatibility");
        pipeline::ColorDescription color;color.transfer=pipeline::TransferFunction::PQ;color.matrix=pipeline::YuvMatrix::BT2020NCL;color.primaries=pipeline::ColorPrimaries::BT2020;
        check(dv.matches(color),"DV HDR10 route requires matching decoded color contract");
        color.transfer=pipeline::TransferFunction::SRGB;check(!dv.matches(color),"DV declaration cannot force SDR pixels into PQ");
        dv.compatibility=4;check(dv.route()==source::DolbyBaseLayer::Hlg,"DV P8.4 selects HLG base layer");
        dv.compatibility=2;check(dv.route()==source::DolbyBaseLayer::Sdr,"DV P8.2 selects SDR base layer");
        dv.baseLayer=false;check(dv.route()==source::DolbyBaseLayer::Unsupported,"missing DV base layer fails closed");
        diagnostics::GpuFrameTiming gpu;auto& first=gpu.gpu[size_t(diagnostics::GpuStage::Color)];first={diagnostics::SampleState::Measured,2,1000,3000,1000000};
        auto& last=gpu.gpu[size_t(diagnostics::GpuStage::FgBatch)];last={diagnostics::SampleState::Measured,3,10000,13000,1000000};
        gpu.gpu[size_t(diagnostics::GpuStage::Blit)]={diagnostics::SampleState::Measured,2,100000,102000,1000000};
        check(diagnostics::graphExecutionSpanMs(gpu)==12,"GPU budget excludes delayed presentation and CPU completion observation");
        for(unsigned sub=1;sub<=5;++sub){
            const auto stage=static_cast<diagnostics::GpuStage>(unsigned(diagnostics::GpuStage::Fg1)+sub-1);
            gpu.gpu[size_t(stage)]={diagnostics::SampleState::Measured,1,3000+sub*1000,4000+sub*1000,1000000};
        }
        check(last.begin==10000&&last.end==13000&&gpu.gpu[size_t(diagnostics::GpuStage::Blit)].begin==100000,
            "6X subframe timestamps cannot overwrite batch or presentation timestamps");
        last.frequency=1000;check(!diagnostics::graphExecutionSpanMs(gpu),"unrelated GPU clocks cannot form a processing envelope");
    }
    const auto logPath=std::filesystem::temp_directory_path()/(L"veyra-log-reopen-"+std::to_wstring(GetCurrentProcessId())+L".log");
    {
        auto logger=std::make_unique<Logger>();logger->setConsoleEnabled(false);
        check(logger->openFile(logPath.wstring()),"open isolated diagnostic log");
        logger->write(LogLevel::Info,"test","before-restart");logger->closeFile();
        check(logger->openFile(logPath.wstring(),true),"append diagnostic log after restart");
        logger->write(LogLevel::Info,"test","after-restart");logger->flush();
        auto other=std::make_unique<Logger>();other->setConsoleEnabled(false);
        check(!other->openFile(logPath.wstring()),"second writer cannot truncate active diagnostic log");
        logger->closeFile();
        std::ifstream file(logPath);std::string content((std::istreambuf_iterator<char>(file)),{});
        check(content.find("before-restart")!=content.npos&&content.find("after-restart")!=content.npos,"both sessions survive reopen and rejected concurrent writer");
    }
    std::filesystem::remove(logPath);
    sink::ArrivalClockMapping ingressClock;
    double mapping=0;
    for(int i=0;i<60000;++i)mapping=ingressClock.observe(1000+i*10.01+(i%13==5?4:0),i*10.0);
    check(std::abs(mapping-(1000+59999*.01))<2.1,"ten-minute audio ingress mapping ages oscillator drift instead of retaining startup minimum");
    ingressClock.reset();check(ingressClock.observe(100,0)==100,"audio discontinuity replaces prior ingress mapping");
    {
        sink::CaptureSyncTarget sync;bool valid=true;
        for(int i=0;i<300;++i){
            const double host=10000+i*17+(i%3)*3,delay=30+(i%2)*5;
            const auto r=sync.observe(delay,host,host-delay);
            valid&=!r.fallback&&r.targetMs>=30&&r.targetMs<=35;
        }
        check(valid,"capture sync accepts variable cadence with consistent clocks");
        auto r=sync.observe(1235,16000,15965);
        check(r.fallback&&r.targetMs<=35,"capture timestamp jump cannot create a second of audio waiting");
        sync.invalidate();r=sync.observe(935,16020,15985);
        check(r.fallback&&r.targetMs==35,"video history reset retains clock fallback");
        for(int i=0;i<90;++i)r=sync.observe(35,17000+i*17,16965+i*17);
        check(r.fallback,"clock fallback requires sustained matching evidence");
        r=sync.observe(35,19100,19065);
        check(!r.fallback,"consistent clocks recover after two seconds");
        sync.reset();bool bounded=true;
        for(int i=0;i<60000;++i){const double host=10000+i*10.;r=sync.observe(35+i*.03,host,host-35);bounded&=r.targetMs<=115;}
        check(bounded&&r.fallback&&r.targetMs==35,"independent video clock drift cannot accumulate seconds of compensation");
        for(double delay:{80.,400.,900.}){
            sync.reset();r=sync.observe(delay,10000,10000-delay);
            check(!r.fallback&&r.targetMs==delay,"genuine measured video residence retains automatic synchronization");
        }
        sync.reset();for(int i=0;i<5;++i)sync.observe(35,10000+i*20,9965+i*20);
        r=sync.observe(400,10100,9700);
        check(r.targetMs==35,"one real presentation stall does not change the audio target");
        r=sync.observe(35,10120,10121);
        check(r.fallback&&std::isfinite(r.targetMs),"invalid future ingress triggers fallback");
        sync.reset();r=sync.observe(std::numeric_limits<double>::quiet_NaN(),11000,10965);
        check(r.fallback&&r.targetMs==35,"nonfinite cross-stream timestamp cannot poison audio target");
        sync.reset();r=sync.observe(900,10000,std::nullopt);
        check(!r.fallback&&r.targetMs==900,"sources without capture provenance retain their separate clock contract");
        r=sync.observe(2000,12000,10000);
        check(r.limited&&r.targetMs==1500,"real excessive residence retains explicit compensation limit");
    }
    sink::AudioFrameTimeline pcmTime;
    check(pipeline::ResolutionPlan::make({1920,1080},true,pipeline::NrSizePolicy::Native,false,1,pipeline::SrTarget::Uhd4K,true).nr==pipeline::Extent{1920,1080},"NR-first preview uses source resolution");
    check(pipeline::ResolutionPlan::make({1920,1080},true,pipeline::NrSizePolicy::Native,true,1,pipeline::SrTarget::Uhd4K,true).nr==pipeline::Extent{3840,2160},"export ignores low latency preview order");
    check(engine::enhancementDelayEstimate(true,false,-20)==0,"file prefetch is not added latency");
    check(engine::enhancementDelayEstimate(true,false,12)==12,"file lateness remains visible");
    check(engine::enhancementDelayEstimate(true,true,45,5)==40,"live baseline work excluded");
    check(engine::enhancementDelayEstimate(false,true,45)==0,"disabled enhancement defines zero baseline");
    check(!engine::enhancementDelayEstimate(true,true,45),"unknown live baseline is not fabricated");
    check(!engine::enhancementDelayEstimate(true,false,std::numeric_limits<double>::quiet_NaN()),"invalid clock rejected");
    pcmTime.append(0,480,100,105);pcmTime.append(480,480,105,120);
    check(pcmTime.at(240)==102.5&&pcmTime.at(720)==112.5,"resampled PCM maps each span at its actual media rate");
    check(!pcmTime.at(1000),"unwritten or silent output has no invented media clock");
    pcmTime.discardBefore(481);check(!pcmTime.at(100)&&pcmTime.at(720)==112.5,"consumed PCM mapping prunes without losing queued audio");
    pcmTime.clear();check(!pcmTime.at(720),"audio reset clears old media anchors");
    auto p=pipeline::ResolutionPlan::make({1920,1080},true,pipeline::NrSizePolicy::Realtime,false);
    check(p.base==pipeline::Extent{3840,2160}&&p.nr==pipeline::Extent{1920,1080}&&p.flow==pipeline::Extent{1920,1080}&&p.output==p.base,"SR4K preserves base; NR and flow remain source1080");
    p=pipeline::ResolutionPlan::make({3840,2160},false,pipeline::NrSizePolicy::Realtime,false);
    check(p.nr==pipeline::Extent{1920,1080}&&p.flow==p.nr,"realtime native4K bounds NR and source-space flow to 1080");
    p=pipeline::ResolutionPlan::make({3840,2160},false,pipeline::NrSizePolicy::Native,false);
    check(p.nr==p.source&&p.flow==p.source,"native mode retains native4K NR and flow");
    p=pipeline::ResolutionPlan::make({3840,2160},true,pipeline::NrSizePolicy::Realtime,true);
    check(!p.srApplied&&p.nr==p.base,"native export and 1:1 SR bypass");
    for(auto target:{pipeline::SrTarget::Qhd,pipeline::SrTarget::Uhd4K,pipeline::SrTarget::Uhd8K,pipeline::SrTarget::Uhd5K,pipeline::SrTarget::Uhd6K,pipeline::SrTarget::Uhd7K}){
        const auto plan=pipeline::ResolutionPlan::make({1920,1080},true,pipeline::NrSizePolicy::Realtime,false,7,target);
        check(plan.output==pipeline::srTargetExtent(target)&&plan.nr==pipeline::Extent{1920,1080}&&plan.settingsRevision==7,"SR target changes real output, retains realtime NR and revision");
    }
    check(pipeline::ResolutionPlan::make({1448,1086},true,pipeline::NrSizePolicy::Native,true,1,pipeline::SrTarget::Uhd8K).output==pipeline::Extent{5760,4320},"8K 4:3 aspect preserved");
    for(auto target:{pipeline::SrTarget::Uhd5K,pipeline::SrTarget::Uhd6K,pipeline::SrTarget::Uhd7K}){
        const auto limit=pipeline::srTargetExtent(target);
        for(auto source:{pipeline::Extent{1920,1080},pipeline::Extent{1440,1080},pipeline::Extent{2520,1080}}){
            const auto output=pipeline::ResolutionPlan::make(source,true,pipeline::NrSizePolicy::Native,true,1,target).output;
            check(output.width<=limit.width&&output.height<=limit.height&&!(output.width%2)&&!(output.height%2)
                &&std::abs(int64_t(output.width)*source.height-int64_t(output.height)*source.width)<=2*source.width,"5/6/7K contain aspect within two-pixel rounding and even dimensions");
        }
    }
    check(pipeline::ResolutionPlan::make({8000,2000},true,pipeline::NrSizePolicy::Native,true,1,pipeline::SrTarget::Qhd).output==pipeline::Extent{8000,2000},"small target never shrinks long image");
    engine::EnhancementSettings s;check(s.validate().empty(),"default settings valid");
    check(engine::motionUsesFlow(s.fgMotion,engine::FrameGenerationBackend::Dlss)&&!engine::motionUsesFlow(s.fgMotion,engine::FrameGenerationBackend::XeSS),"default motion follows provider: DLSS flow, XeSS zero");
    check(!engine::motionUsesFlow(engine::MotionSource::Zero)&&engine::motionUsesFlow(engine::MotionSource::OpticalFlow),"explicit consumer motion choice wins");
    auto audioOnly=s;audioOnly.revision=2;audioOnly.audioSync=engine::AudioSyncMode::Manual;audioOnly.audioOffsetMs=90;
    check(audioOnly.sameVideoConfiguration(s),"audio changes do not invalidate video configuration");
    auto attempted=audioOnly;attempted.model.style=1;
    check(!attempted.sameVideoConfiguration(s),"NR model changes still invalidate video configuration");
    auto latest=attempted;latest.audioOffsetMs=100;latest.rejectVideoRequest(attempted,s);
    check(latest.revision==s.revision&&latest.model==s.model&&latest.audioOffsetMs==100,"video failure preserves a newer independent audio request");
    latest=attempted;latest.rejectVideoRequest(attempted,s);
    check(latest==s,"failed combined request rolls back its own audio and video fields");
    latest=audioOnly;latest.revision=3;const auto newer=latest;latest.rejectVideoRequest(attempted,s);
    check(latest==newer,"late failure cannot roll back a newer video request");
    engine::FrameLineageTracker lineage;pipeline::FrameBatch first;first.identity={1,1,10};first.b100ns=100000;
    check(!lineage.observe(first,1000000,true,false),"first source has no invented prior arrival");
    auto second=first;second.identity.sourceFrameId=11;second.a100ns=first.b100ns;second.b100ns=433333;
    const auto pair=lineage.observe(second,1470000,true,false);
    check(pair&&pair->b.host100ns-pair->a.host100ns==470000,"lineage measures irregular47ms arrival, not nominal33ms PTS interval");
    engine::FrameFlowWindow pairFlow(1,second.identity,1000000);
    if(pair){pairFlow.pairArrived(*pair);pairFlow.generatedLatency(*pair,1700000);}
    const auto pairStats=pairFlow.snapshot(2000000);
    check(pairStats.pairSourceA==10&&pairStats.pairSourceB==11&&pairStats.pairTiming[size_t(diagnostics::PairTiming::ArrivalInterval)].mean==47,"pair identity and measured lookahead use actual arrivals");
    check(pairStats.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)].mean==70&&pairStats.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromB)].mean==23&&!pairStats.softwareLatencyMs,"generated A/B ages remain separate from real-frame latency");
    auto broken=second;broken.identity={2,1,12};broken.a100ns=second.b100ns;broken.b100ns+=333333;
    check(!lineage.observe(broken,1800000,true,false),"epoch reset never reuses an old source arrival");
    auto skipped=broken;skipped.identity.sourceFrameId=13;skipped.a100ns=123;skipped.b100ns+=333333;
    check(!lineage.observe(skipped,2100000,true,false),"missing A identity is unknown rather than extrapolated");
    check(!lineage.observe(skipped,2400000,true,true),"cached preview cannot seed latency lineage");
    check(!pairFlow.snapshot(13000000).pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)].mean,"generated latency expires when no new frames are presented");
    check(engine::frameGenerationBackendName(engine::FrameGenerationBackend::Dlss)=="DLSS"&&engine::frameGenerationBackendName(engine::FrameGenerationBackend::XeSS)=="XeSS"&&engine::frameGenerationBackendName(engine::FrameGenerationBackend::Fsr)=="AMD-FSR","frame-generation backend names identify every backend");
    // The present-sink backends own generation inside their swapchain provider;
    // the in-graph DLSSG path must never try to run for them.
    check(engine::presentSinkFrameGeneration(engine::FrameGenerationBackend::XeSS)&&!engine::presentSinkFrameGeneration(engine::FrameGenerationBackend::Fsr)&&!engine::presentSinkFrameGeneration(engine::FrameGenerationBackend::Fsr4)&&!engine::presentSinkFrameGeneration(engine::FrameGenerationBackend::Dlss),"only XeSS owns provider presentation; FSR uses application batches");
    check(engine::crossVendorFrameGeneration(engine::FrameGenerationBackend::Fsr)&&engine::crossVendorFrameGeneration(engine::FrameGenerationBackend::Fsr4)&&!engine::crossVendorFrameGeneration(engine::FrameGenerationBackend::Dlss),"FSR 3/4 admission is independent of NVIDIA requirements");
    check(int(engine::FrameGenerationBackend::Fsr)==2&&int(engine::FrameGenerationBackend::Fsr4)==3,"FSR persisted identities remain stable and distinct");
    {
        engine::EnhancementSettings settings;
        settings.multiplier=4;settings.frameGenerationBackend=engine::FrameGenerationBackend::Fsr;
        check(!settings.validate().empty()&&std::string(settings.validate()).find("2X")!=std::string::npos,"AMD FSR requests above 2X are rejected by validation");
        settings.multiplier=2;
        check(settings.validate().empty(),"AMD FSR 2X is a valid request");
    }
    {
        engine::EnhancementSettings settings;
        settings.videoSrQuality=engine::kVideoSrFsr;
        check(settings.validate().empty(),"AMD FSR upscaling is a valid video SR quality");
        settings.videoSrQuality=engine::kVideoSrFsr+1;
        check(!settings.validate().empty(),"video SR quality above the AMD FSR slot is rejected");
        settings.videoSrQuality=0;
        check(settings.validate().empty(),"DLSS SR quality remains valid");
    }
    {
        engine::EnhancementSettings settings;
        check(settings.captureAudio==engine::CaptureAudioIngress::Auto,"capture audio ingress defaults to automatic");
        check(!settings.captureFlipVertical,"capture vertical flip defaults off");
        for(auto mode:{engine::CaptureAudioIngress::Auto,engine::CaptureAudioIngress::PcmOnly,engine::CaptureAudioIngress::BitstreamPreferred}){
            settings.captureAudio=mode;
            check(settings.validate().empty(),"capture audio ingress modes validate");
        }
        settings.captureAudio=static_cast<engine::CaptureAudioIngress>(3);
        check(!settings.validate().empty(),"capture audio ingress outside the enum is rejected");
    }
    {
        engine::EnhancementSettings settings;
        check(settings.captureBuffer==veyra::source::CaptureBufferMode::Auto,"capture buffer mode defaults to auto");
        for(auto mode:{veyra::source::CaptureBufferMode::Auto,veyra::source::CaptureBufferMode::Minimum,veyra::source::CaptureBufferMode::DriverDefault}){
            settings.captureBuffer=mode;
            check(settings.validate().empty(),"capture buffer modes validate");
        }
        settings.captureBuffer=static_cast<veyra::source::CaptureBufferMode>(3);
        check(!settings.validate().empty(),"capture buffer mode outside the enum is rejected");
        check(veyra::source::captureDesiredVideoBuffers(veyra::source::CaptureBufferMode::Auto,1920,1080)==2&&
            veyra::source::captureDesiredVideoBuffers(veyra::source::CaptureBufferMode::Auto,3840,2160)==3&&
            veyra::source::captureDesiredVideoBuffers(veyra::source::CaptureBufferMode::Minimum,3840,2160)==1&&
            veyra::source::captureDesiredVideoBuffers(veyra::source::CaptureBufferMode::DriverDefault,1920,1080)==0,
            "capture buffer policy: auto 2/3 by size, minimum 1, driver default none");
    }
    {
        using veyra::source::CaptureFormatTier;
        using veyra::source::CapturePacking;
        check(veyra::source::captureFormatRank(CapturePacking::Nv12)<veyra::source::captureFormatRank(CapturePacking::Yuy2)&&
            veyra::source::captureFormatRank(CapturePacking::Yuy2)<veyra::source::captureFormatRank(CapturePacking::Bgr24)&&
            veyra::source::captureFormatRank(CapturePacking::Bgr24)<veyra::source::captureFormatRank(CapturePacking::Rgb565)&&
            veyra::source::captureFormatRank(CapturePacking::Rgb565)<veyra::source::captureFormatRank(CapturePacking::Unknown),
            "format rank: NV12 < YUY2 < RGB24 < RGB565 < decoded");
        check(veyra::source::captureFormatTier(CapturePacking::Nv12)==CaptureFormatTier::Low&&
            veyra::source::captureFormatTier(CapturePacking::Bgr24)==CaptureFormatTier::Medium&&
            veyra::source::captureFormatTier(CapturePacking::Rgb565)==CaptureFormatTier::High&&
            veyra::source::captureFormatTier(CapturePacking::Unknown)==CaptureFormatTier::Decoded,
            "format tiers: low/medium/high/decoded");
        check(veyra::source::captureFormatNeedsCostHint(CaptureFormatTier::High)&&veyra::source::captureFormatNeedsCostHint(CaptureFormatTier::Decoded)&&
            !veyra::source::captureFormatNeedsCostHint(CaptureFormatTier::Medium),"cost hint only for high/decoded formats");
        std::vector<CapturePacking> order{CapturePacking::Rgb565,CapturePacking::Unknown,CapturePacking::Yuy2,CapturePacking::Nv12};
        std::stable_sort(order.begin(),order.end(),[](CapturePacking a,CapturePacking b){return veyra::source::captureFormatRank(a)<veyra::source::captureFormatRank(b);});
        check(order[0]==CapturePacking::Nv12&&order[1]==CapturePacking::Yuy2&&order[2]==CapturePacking::Rgb565&&order[3]==CapturePacking::Unknown,
            "recommended order: a device offering YUY2 and RGB565 lists YUY2 first");
    }
    check(engine::opticalFlowBackendName(engine::OpticalFlowBackend::Nvidia)=="NVIDIA_NVOF"&&engine::opticalFlowBackendName(engine::OpticalFlowBackend::AmdFidelityFx)=="AMD_FIDELITYFX_OF","optical-flow backend names identify every backend");
    s.model.intensity=std::numeric_limits<float>::quiet_NaN();check(!s.validate().empty(),"reject NaN transaction");
    s={};s.multiplier=5;check(s.validate().empty(),"5X multiplier accepted by the API guard (6X ceiling)");
    s={};s.multiplier=6;check(s.validate().empty(),"6X multiplier accepted");
    s={};s.multiplier=7;check(!s.validate().empty(),"reject multiplier above the 6X ceiling");
    check(pipeline::FrameBatch::interpolate(-200000,0,1,2)==-100000,"negative PTS midpoint");
    check(pipeline::FrameBatch::interpolate(0,200000,1,3)==66667&&pipeline::FrameBatch::interpolate(0,200000,2,3)==133333,"rational 3X PTS within tick");
    pipeline::FrameBatch b;b.identity={1,2,3};pipeline::BatchFrame f;f.identity=b.identity;f.pts100ns=1;b.append(f);f.identity.settingsRevision=3;
    bool rejected=false;try{b.append(f);}catch(...){rejected=true;}check(rejected,"reject mixed settingsRevision");
    for(uint32_t multiplier=2;multiplier<=pipeline::FrameBatch::Capacity;++multiplier){
        pipeline::FrameBatch batch;batch.identity={7,9,11};batch.a100ns=0;batch.b100ns=1000000;
        for(uint32_t j=1;j<multiplier;++j){pipeline::BatchFrame item;item.identity=batch.identity;item.kind=pipeline::FrameKind::Generated;item.subframe=j;item.pts100ns=pipeline::FrameBatch::interpolate(0,1000000,j,multiplier);batch.append(item);}
        pipeline::BatchFrame real;real.identity=batch.identity;real.pts100ns=1000000;batch.append(real);
        check(batch.count==multiplier&&batch.frames[batch.count-1].kind==pipeline::FrameKind::Real,"2-6X batch retains every generated position and final real frame");
        if(multiplier==pipeline::FrameBatch::Capacity){real.pts100ns++;bool overflow=false;try{batch.append(real);}catch(const std::invalid_argument&){overflow=true;}check(overflow,"full 6X batch rejects seventh frame before writing");
            batch.count=UINT32_MAX;overflow=false;try{batch.append(real);}catch(const std::invalid_argument&){overflow=true;}check(overflow,"corrupt batch count rejects before indexing previous frame");}
    }
    diagnostics::FrameMetrics m;check(!m.displayFps&& !m.gpu[0].milliseconds,"unknown display/GPU is not zero");
    diagnostics::DiagnosticHistory h;diagnostics::DiagnosticEvent e;e.fingerprint="same";for(int i=0;i<100;++i)h.add(e);
    check(h.size()==1&&h.events()[0].occurrenceCount==100,"merge repeated error");
    engine::PresentationScheduler timeline;timeline.reset(3,0,1000000,200000);
    check(timeline.deadline(200000)==1400000&&timeline.deadline(400000)==1600000,"live deadlines do not drift with CPU completion");
    check(timeline.expired(0,1400000,100000)&&!timeline.anchored(4),"expire generated debt and reject epoch change");
    // First arrival is 12ms late; the next is on time and GPU work takes 3ms.
    // The previous no-FG mapping holds an already-ready frame another 9ms.
    engine::PresentationScheduler captureTimeline;
    captureTimeline.reset(1,0,1120000,0);
    constexpr int64_t nextPts=166667,nextReady=1000000+nextPts+30000;
    check(captureTimeline.deadline(nextPts)-nextReady==90000,"reproduce 9ms no-FG hold after first-callback jitter");
    captureTimeline.reset(1,0,1120000,0,false);
    check(captureTimeline.deadline(nextPts)<=nextReady,"unbuffered capture presents ready frame without inherited jitter");
    check(captureTimeline.deadline(6000600000LL)<=6001030000LL,"unbuffered capture cannot accumulate source-clock drift");
    captureTimeline.reset(2,0,2000000,166667,true);
    check(captureTimeline.deadline(166667)==2333334,"switch to FG restores continuous source-PTS pacing and lookahead");
    // A physical capture device may report 59.94fps while its negotiated
    // duration is treated as 60fps. A single source/host anchor accumulates
    // roughly 300ms of error in five minutes; pair anchoring keeps the FG
    // deadline within the current input pair instead.
    constexpr int64_t nominal60=166667,actual5994=166834;
    engine::PresentationScheduler continuousCapture;
    continuousCapture.reset(7,0,0,nominal60,true);
    constexpr int64_t samples=18000;
    const int64_t lastSource=samples*nominal60,lastArrival=samples*actual5994;
    const int64_t lastGenerated=lastSource-nominal60/2;
    check(continuousCapture.deadline(lastGenerated)<lastArrival-2500000,"single capture source anchor exposes long-run 59.94/60 drift");
    engine::PresentationScheduler pairCapture;bool pairDeadlineBounded=true;
    for(int64_t i=1;i<=samples;++i){
        const int64_t source=i*nominal60,arrival=i*actual5994;
        pairCapture.resetPair(7,source,arrival,nominal60,true);
        const int64_t generated=source-nominal60/2,deadline=pairCapture.deadline(generated);
        pairDeadlineBounded&=deadline>=arrival&&deadline<=arrival+nominal60;
    }
    check(pairDeadlineBounded,"pair-anchored capture FG deadline does not accumulate source-clock drift");
    engine::PresentationScheduler ps5Timeline;bool decodedPairFits=true;
    for(int64_t i=1;i<120;++i){
        const int64_t b=i*166667,decoded=1000000+b+(i%2?150000:20000);
        ps5Timeline.reset(1,b,decoded,166667);
        const auto deadline=ps5Timeline.deadline(b-83333);
        decodedPairFits&=engine::admitLiveFg(decoded+10000,deadline,1,12,.3);
        decodedPairFits&=!engine::admitLiveFg(decoded+400000,deadline,40,12,.3);
    }
    check(decodedPairFits,"PS5 decoder jitter does not spend FG budget; delayed enhancement still expires against the original decoded input deadline");
    check(!engine::admitLiveFg(1200000,1000000,0,std::nullopt,0),"warmup rejects already-expired FG before evaluate");
    check(engine::admitLiveFg(900000,1000000,0,std::nullopt,0),"warmup does not invent a measured completion estimate");
    check(!engine::admitLiveFg(900000,1000000,2,30.0,1),"predicted completion beyond deadline skips optional pair");
    check(engine::admitLiveFg(900000,1000000,10,15.0,1),"remaining predicted work fits deadline");
    engine::FrameFlowWindow oldWindow(1,{3,4,5},0),newWindow(1,{4,5,6},0);
    oldWindow.ready(1,true,3,0,10000000);oldWindow.presented(true,9,10000000);
    auto oldStats=oldWindow.snapshot(10000000),newStats=newWindow.snapshot(10000000);
    check(oldStats.counters.generatedPresented==1&&newStats.counters.generatedPresented==0&&newStats.counters.fgReadyValid==0,"old asynchronous completion cannot contaminate new epoch/revision");
    check(oldStats.validGeneratedFps==3&&oldWindow.snapshot(21000000).validGeneratedFps==0,"effective generation rate expires without new frames");
    engine::FrameFlowWindow rates(1,{1,1,1},0);
    for(unsigned i=1;i<=60;++i){rates.ready(i,true,i<=40?1:0,0,i*100000);rates.presented(false,i,i*100000);if(i<=30)rates.presented(true,i,i*100000);}
    rates.ready(60,true,3,0,7000000);
    const auto counted=rates.snapshot(10000000);
    check(counted.sourceCompletedFps==60&&counted.outputCompletedFps==100&&counted.validGeneratedFps==40&&counted.presentSubmitFps==90,"real60 plus valid40 yields output100 but present90; duplicate completion ignored");
    check(counted.realPresentFps==60&&counted.generatedPresentFps==30,"source and generated presentation counts are independently measured");
    check(counted.counters.realReady==60&&counted.counters.fgReadyValid==40,"GPU-ready counts are independent of requested multiplier");
    rates.xessSubmitted(2,1,10000000);rates.xessSubmitted(1,0,10000000);
    check(rates.snapshot(10000000).xessSdkSubmitFps==3&&rates.snapshot(10000000).outputCompletedFps==100,"XeSS SDK submissions never enter measured GPU completions");
    rates.latency(10100000,10300000);rates.latency(10200000,10600000);
    const auto times=rates.snapshot(13000000);
    check(times.softwareLatencyMs==30&&times.softwareLatencyP95Ms==40&&times.latencySamples==2,"software latency measures same-frame endpoints and nearest-rank P95");
    const auto stale=rates.snapshot(23000000);
    check(!stale.softwareLatencyMs&&stale.outputCompletedFps==0&&stale.xessSdkSubmitFps==0,"stalled stream expires rates and latency samples");
    engine::FrameFlowWindow thousand(1,{1,1,1},0);
    for(unsigned i=1;i<=1000;++i)thousand.ready(i,true,0,0,i*10000);
    check(thousand.snapshot(10000000).outputCompletedFps==1000,"completion accounting supports measured 1000fps without a 60fps clamp");
    auto sharedRates=std::make_shared<engine::FrameCompletionRates>(0);
    engine::FrameFlowWindow epochA(1,{1,1,1},0,sharedRates),epochB(1,{2,1,2},5000000,sharedRates);
    epochA.ready(1,true,1,0,1000000);epochB.ready(1,true,1,0,6000000);
    const auto acrossDrop=epochB.snapshot(10000000);
    check(acrossDrop.outputCompletedFps==4&&acrossDrop.rateWindowReady&&acrossDrop.counters.realReady==1,"drop resets retain revision throughput without merging epoch counters");
    engine::FrameFlowWindow newRevision(1,{3,2,3},6000000);
    check(newRevision.snapshot(10000000).outputCompletedFps==0&&!newRevision.snapshot(10000000).rateWindowReady,"new settings revision has an independent rate window");
    engine::FrameFlowWindow stageMetrics(1,{1,1,1},0);
    stageMetrics.cpu(diagnostics::CpuStage::Present,10,1000000);stageMetrics.cpu(diagnostics::CpuStage::Present,30,2000000);
    std::array<diagnostics::GpuSample,size_t(diagnostics::GpuStage::Count)> gpuSamples{};
    gpuSamples[0]={diagnostics::SampleState::Measured,12,100,200,1000};stageMetrics.gpu(gpuSamples,2000000);stageMetrics.gpu(gpuSamples,3000000);
    const auto timing=stageMetrics.snapshot(4000000);
    check(timing.cpuTiming[size_t(diagnostics::CpuStage::Present)].mean==20&&timing.cpuTiming[size_t(diagnostics::CpuStage::Present)].p95==30&&timing.gpuTiming[0].samples==1,"stage window averages actual samples and deduplicates GPU timestamps");
    const auto expiredTiming=stageMetrics.snapshot(14000000);
    check(!expiredTiming.cpuTiming[size_t(diagnostics::CpuStage::Present)].mean&&!expiredTiming.gpuTiming[0].mean,"expired stage samples become unmeasured rather than stale zero");
    engine::FrameFlowWindow deliveredTiming(1,{1,1,1},0);
    diagnostics::GpuFrameTiming delivered{{1,1,2},gpuSamples};
    deliveredTiming.gpuFrame(delivered,1000000);delivered.identity.sourceFrameId=3;deliveredTiming.gpuFrame(delivered,1000000);
    delivered.identity.settingsRevision=2;deliveredTiming.gpuFrame(delivered,1000000);
    delivered.identity.settingsRevision=1;delivered.identity.epoch=2;deliveredTiming.gpuFrame(delivered,1000000);
    check(deliveredTiming.snapshot(2000000).gpuTiming[0].samples==2,"dequeued GPU frames each count once even with equal timestamps; old epoch and revision rejected");
    for(uint64_t epoch=2;epoch<80;++epoch){
        deliveredTiming.advanceHistory({epoch,1,epoch});
        delivered.identity={epoch-1,1,epoch};deliveredTiming.gpuFrame(delivered,2000000+int64_t(epoch)*1000);
    }
    check(deliveredTiming.snapshot(5000000).gpuTiming[0].samples==80,"overload history changes retain delayed GPU samples across consecutive mailbox drops");
    engine::FrameFlowWindow afterSeek(1,{80,1,80},6000000);
    afterSeek.gpuFrame(delivered,6000000);
    check(!afterSeek.snapshot(7000000).gpuTiming[0].mean,"hard reset rejects GPU samples from previous measurement window");
    diagnostics::GpuFrameTiming work{{1,1,1},{}};
    auto stamp=[&](diagnostics::GpuStage stage,uint64_t begin,uint64_t end){work.gpu[size_t(stage)]={diagnostics::SampleState::Measured,double(end-begin),begin,end,1000};};
    stamp(diagnostics::GpuStage::Color,0,2);
    stamp(diagnostics::GpuStage::Flow,2,8);stamp(diagnostics::GpuStage::Nr,10,30);
    stamp(diagnostics::GpuStage::Sr,30,39);stamp(diagnostics::GpuStage::Residual,39,40);
    stamp(diagnostics::GpuStage::FgBatch,50,56);stamp(diagnostics::GpuStage::Fg1,50,52);
    stamp(diagnostics::GpuStage::Fg2,52,54);stamp(diagnostics::GpuStage::Fg3,54,56);
    check(diagnostics::enhancementProcessingMs(work)==42,"processing total excludes idle gaps and color, counts full FG batch once without dividing by multiplier");
    engine::FrameFlowWindow processingWindow(1,work.identity,0);
    processingWindow.gpuFrame(work,1000000);
    stamp(diagnostics::GpuStage::Nr,5,30);
    check(diagnostics::enhancementProcessingMs(work)==44,"overlapping enhancement intervals are counted once");
    work.identity.sourceFrameId=2;processingWindow.gpuFrame(work,2000000);
    diagnostics::GpuFrameTiming blitOnly{work.identity,{}};
    blitOnly.gpu[size_t(diagnostics::GpuStage::Blit)]={diagnostics::SampleState::Measured,1,100,101,1000};
    check(!diagnostics::enhancementProcessingMs(blitOnly),"presenter-only records cannot dilute graph averages with zeros");
    processingWindow.gpuFrame(blitOnly,2000000);
    processingWindow.cpu(diagnostics::CpuStage::EnhancementDelayEstimate,600,2000000);
    work.identity.settingsRevision=2;processingWindow.gpuFrame(work,2000000);
    auto processingStats=processingWindow.snapshot(3000000);
    check(processingStats.enhancementProcessing.mean==43&&processingStats.enhancementProcessing.p95==44&&processingStats.enhancementProcessing.samples==2,"same-frame totals have independent mean/P95; lateness and stale settings cannot contaminate them");
    check(!processingWindow.snapshot(14000000).enhancementProcessing.mean,"expired enhancement measurements display unmeasured");
    work.gpu[size_t(diagnostics::GpuStage::Nr)].frequency=2000;
    check(!diagnostics::enhancementProcessingMs(work),"incompatible timestamp clocks are not combined");
    work.gpu[size_t(diagnostics::GpuStage::Nr)].state=diagnostics::SampleState::Unavailable;
    check(!diagnostics::enhancementProcessingMs(work),"unavailable active stage cannot become an understated total");
    work.gpu={};stamp(diagnostics::GpuStage::Color,0,2);
    check(diagnostics::enhancementProcessingMs(work)==0,"measured unenhanced graph reports zero enhancement work");
    check(!oldStats.latest.sameWindow(2,{3,4,5})&&!oldStats.latest.sameWindow(1,{4,4,5})&&!oldStats.latest.sameWindow(1,{3,5,5}),"session, epoch and revision all partition flow counters");
    for(int i=0;i<100;++i){e.fingerprint=std::to_string(i);h.add(e);}check(h.size()==64,"bounded diagnostic queue");
    engine::ContentCadence cadence;for(int i=0;i<120;++i)cadence.observe(i*1000.0/60,i%2?.01:0,true);check(cadence.measuredRate()==30,"moving 30 in 60 cadence");
    cadence.reset();for(int i=0;i<120;++i)cadence.observe(i*1000.0/60,0,true);check(cadence.measuredRate()==0,"static scene cannot establish lower FPS");
    cadence.reset();for(int i=0;i<120;++i)cadence.observe(i*20.0,.01,true);check(cadence.measuredRate()==50,"moving 50fps cadence");
    cadence.reset();for(int i=0;i<120;++i)cadence.observe(i*1000.0/60,.01,true);check(cadence.measuredRate()==60,"moving 60fps cadence");check(cadence.confirmedRate(engine::ContentRate::Fps60)==60&&cadence.confirmedRate(engine::ContentRate::Fps30)==0&&cadence.conflicts(engine::ContentRate::Fps30),"manual identification target is not a resampling fiction");
    check(diagnostics::redact("failure C:\\Users\\private-user\\media file.mp4\nserial=DEVICE123\nuser=private-user").find("private-user")==std::string::npos,"redact Windows paths, usernames and serials");
    engine::CfrTimeline high(1000,1,.000001,0);bool highOk=true;for(unsigned i=0;i<1000;++i)highOk &= high.accepts(i,i/1000.0);check(highOk&&!high.accepts(1000,1.001),"1000fps preserves exact timestamps and rejects dropped frame");
    engine::CfrTimeline cfr(60,1,.001,0);bool quantized=true;
    for(unsigned i=0;i<6000;++i)quantized &= cfr.accepts(i,std::round(i*1000.0/60)/1000);
    check(quantized,"60fps millisecond quantization does not drift or reject");
    engine::CfrTimeline ntsc(30000,1001,.001,-.017);bool fractional=true;
    for(unsigned i=0;i<30000;++i)fractional &= ntsc.accepts(i,std::round((i*1001.0/30000-.017)*1000)/1000);
    check(fractional,"fractional CFR with negative origin survives quantization");
    engine::CfrTimeline gap(60,1,.001,0);gap.accepts(0,0);gap.accepts(1,.017);
    engine::CfrTimeline drift(60,1,.001,0);for(unsigned i=0;i<60;++i)drift.accepts(i,std::round(i*1000.0/60)/1000);
    engine::CfrTimeline invalid(60,1,.001,0);
    check(!gap.accepts(2,.05)&&!drift.accepts(60,1.016667)&&!invalid.accepts(0,std::numeric_limits<double>::quiet_NaN()),"VFR gap, drift and invalid PTS rejected with fresh timelines");
    check(!engine::CfrTimeline(60,1,.02,0).valid(),"time base coarser than one frame rejected");
    engine::CfrTimeline ticks(60,1,1.0/60,0);bool exactTicks=true;for(unsigned i=0;i<120;++i)exactTicks &= ticks.accepts(i,i/60.0);check(exactTicks&&!ticks.accepts(120,121.0/60),"one-tick CFR accepted but missing frame rejected");
    std::vector<double> mkv;for(unsigned i=0;i<24;++i)mkv.push_back(std::round(i*1000.0/60)/1000);
    check(engine::CfrTimeline::select(29990,499,.001,mkv)==std::pair<int,int>{60,1},"misdeclared MKV rate selects consistent standard CFR candidate");
    {
        // User report 2026-09-16: a 854.15s / 60fps MKV whose final frame sits
        // exactly one frame interval late aborted a 25 minute export at 99%.
        // The grid write is index based, so only that last frame's duration may
        // move - and only when the stream really ends there.
        engine::CfrTimeline tail(60,1,.001,0);bool seated=true;
        for(unsigned i=0;i<51247;++i)seated &= tail.accepts(i,std::round(i*1000.0/60)/1000);
        check(seated&&!tail.accepts(51247,854.133),"a one-frame tail jump still breaks the strict CFR phase test");
        check(tail.tailAccepts(51247,854.133,51249)&&tail.tailAccepts(51248,854.15,51249),"tail snap covers the real end of stream");
        check(!tail.tailAccepts(51247,854.133,60000)&&!tail.tailAccepts(100,std::round(100*1000.0/60)/1000+.1,60000),"tail snap never covers a mid-stream gap");
        check(!tail.tailAccepts(51247,854.9,51249)&&!tail.tailAccepts(51247,854.133,0),"tail snap rejects larger deviations and unknown streams");
    }
    {
        // User report 2026-09-17: an 11 minute CFR mp4 where ONE sample was
        // dropped (frame 10467 carries the timestamp of slot 10468) aborted the
        // export at 51%. A whole-slot hole is now filled with the previous frame
        // and the validator re-aligns; off-grid jitter and bigger jumps still fail.
        engine::CfrTimeline holes(30,1,1.0/90000,0);
        bool seated=true;for(uint64_t i=0;i<10467;++i)seated &= holes.accepts(i,double(i)/30.0);
        check(seated&&!holes.accepts(10467,10468/30.0),"a dropped mid-stream sample still breaks the strict phase test");
        check(holes.missingSlots(10467,10468/30.0)==1,"whole-slot hole is detected as one missing sample");
        check(holes.missingSlots(10467,10467/30.0)==0&&holes.missingSlots(10467,10467/30.0+0.0002)==0
            &&holes.missingSlots(10467,10470/30.0)==0,"on-grid frames, off-grid jitter and oversized jumps are not fillable holes");
        check(holes.missingSlots(10467,10468/30.0,1.0)==1&&holes.missingSlots(10467,10469/30.0,1.0)==0,"fill limit bounds how much damage is repaired");
        holes.resync(10467,10468/30.0);
        // resync consumes frame 10467 itself (the exporter keeps processing it),
        // so the grid is verified from the next source frame onwards.
        bool realigned=true;for(uint64_t i=10468;i<10468+120;++i)realigned &= holes.accepts(i,double(i+1)/30.0);
        check(realigned,"gap resync re-aligns the grid for the rest of the stream");
        check(holes.missingSlots(10587,10588.0/30.0)==0,"post-resync frames sit on the new grid instead of reporting another hole");
    }
    {
        // Subtitle engine: external SRT/ASS/WebVTT parsing, style mapping and
        // indexed lookup (the renderer and the embedded-track extraction share
        // this code).
        const auto folder=std::filesystem::temp_directory_path()/std::format("veyra-subtitle-{}",GetCurrentProcessId());
        std::filesystem::create_directories(folder);
        const auto assPath=(folder/L"styled.ass").wstring();
        {std::ofstream out(assPath);out<<
            "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,SimHei,64,&H0000FFFF,&H000000FF,&H00FF0000,&H80000000,1,0,0,0,100,100,0,0,1,3,1,8,40,40,60,1\n\n"
            "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,{\\an2}第一行\\N第二行\n"
            "Dialogue: 0,0:00:05.00,0:00:08.00,Default,,0,0,0,,{\\pos(960,200)}定位行\n";}
        const auto ass=engine::loadSubtitleFile(assPath);
        check(ass.usable()&&ass.cues.size()==2&&ass.styles.size()==2,"ASS loads cues and styles");
        check(ass.scriptWidth==1920&&ass.scriptHeight==1080,"ASS PlayRes is parsed for pos scaling");
        check(ass.styles[1].font==L"SimHei"&&ass.styles[1].bold&&ass.styles[1].alignment==8,"ASS style fields map onto the render style");
        check(ass.styles[1].primary==0xFF00FFFFu&&ass.styles[1].outline==0xFFFF0000u,"ASS BGR colours convert to ARGB with 00 = opaque");
        check(ass.cues[0].text==L"第一行\n第二行"&&ass.cues[0].alignOverride==2,"override tags are stripped and N-breaks become line breaks");
        check(ass.cues[1].posX==960&&ass.cues[1].posY==200,"pos survives for positioning");
        check(engine::cuesAt(ass,2.0).size()==1&&engine::textAt(ass,2.0)==L"第一行\n第二行","indexed lookup returns the overlapping cue");
        check(engine::textAt(ass,4.5).empty()&&engine::textAt(ass,6.0)==L"定位行","indexed lookup respects cue windows");
        const auto srtPath=(folder/L"plain.srt").wstring();
        {std::ofstream out(srtPath);out<<"1\n00:00:00,500 --> 00:00:02,000\n简单字幕\n\n2\n00:00:09,000 --> 00:00:10,000\n晚一点\n";}
        const auto plain=engine::loadSubtitleFile(srtPath);
        check(plain.usable()&&plain.cues.size()==2&&engine::textAt(plain,1.0)==L"简单字幕"&&engine::textAt(plain,5.0).empty(),"SRT windows behave");
        const auto vttPath=(folder/L"web.vtt").wstring();
        {std::ofstream out(vttPath);out<<"WEBVTT\n\n00:00:01.000 --> 00:00:03.000\nVTT 字幕\n";}
        const auto vtt=engine::loadSubtitleFile(vttPath);
        check(vtt.usable()&&vtt.cues.size()==1&&engine::textAt(vtt,2.0)==L"VTT 字幕","WebVTT windows behave");
        check(engine::isTextSubtitleCodec(L"subrip")&&engine::isTextSubtitleCodec(L"ass")&&!engine::isTextSubtitleCodec(L"hdmv_pgs_subtitle"),"text and image subtitle codecs are distinguished");
        std::error_code cleanup;
        std::filesystem::remove_all(folder,cleanup);
    }
    using Reason=pipeline::ResetReason;
    check(diagnostics::resetCause(0,Reason::PauseResume,Reason::SceneCut)==Reason::PauseResume&&
        diagnostics::resetCause(0,Reason::Seek,Reason::None)==Reason::Seek&&
        diagnostics::resetCause(0,Reason::Settings,Reason::None)==Reason::Settings,"explicit transport/settings reset causes are not invented scene cuts");
    check(diagnostics::resetCause(unsigned(pipeline::FrameFlagBits::Discontinuity),Reason::None,Reason::None)==Reason::PtsDiscontinuity&&
        diagnostics::resetCause(0,Reason::None,Reason::CadenceBreak)==Reason::CadenceBreak&&
        diagnostics::resetCause(0,Reason::None,Reason::None)==Reason::None,"PTS/cadence and unknown reset causes remain distinct");
    check(diagnostics::resetCause(unsigned(pipeline::FrameFlagBits::DeviceLost),Reason::Seek,Reason::None)==Reason::DeviceLost&&
        diagnostics::resetCause(unsigned(pipeline::FrameFlagBits::Drop),Reason::None,Reason::SceneCut)==Reason::FrameDrop,"device loss and source drops retain explicit causes");
    auto trace=std::make_unique<diagnostics::FrameTrace>();
    for(unsigned i=0;i<diagnostics::FrameTrace::capacity+5;++i)
        trace->add({int64_t(i),7,{i/100,9,i},i,2*i,int64_t(i)*100,diagnostics::TraceKind::Submitted,0,1,.25});
    const auto traceCopy=trace->snapshot();
    check(traceCopy.size()==8192&&trace->overwritten()==5&&traceCopy.front().identity.sourceFrameId==5&&traceCopy.back().identity.sourceFrameId==8196,"trace ring preserves chronological tail and reports overwritten events");
    trace->add({});check(traceCopy.front().session==7&&traceCopy.back().batch==8196,"diagnostic trace snapshot is independent of later writes");
    Logger::instance().recordFrame({12345,7,{3,9,55},6,8,100,diagnostics::TraceKind::Ready,0,2,.25});
    Logger::instance().setConsoleEnabled(false);
    log::error("nvof-session","test-only nvOFInit failed status=5");
    log::error("nvof-session","test-only caps failed st=7");
    log::error("ngx","test-only fg-backend failed result=0xBAD00005 seh=0xC0000005");
    const auto report=Logger::instance().diagnosticReport();
    check(report.find("event=Ready host=12345 session=7 revision=9 epoch=3 source=55 batch=6 fence=8")!=std::string::npos,
        "existing diagnostic preview exports frame identity and actual completion events");
    check(report.find("NVOF=0x5")!=std::string::npos&&report.find("NVOF=0x7")!=std::string::npos&&report.find("NGX=0xBAD00005")!=std::string::npos&&report.find("SEH=0xC0000005")!=std::string::npos,"diagnostic report retains NVOF aliases and NGX/SEH codes (synthetic errors)");
    {
        // ThunkHook is the detour primitive the XeSS pacing port needs: build a
        // canonical provider-style thunk, hook it, forward through the
        // trampoline, then verify byte-exact restoration.
        auto* page=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        check(page!=nullptr,"thunk test page allocated");
        if(page){
            // Layout: [thunk E9 rel32 -> target stub][target stub: jmp [rip+0]; abs64]
            // The stub keeps the whole test inside one page, so the relative jump
            // is always in range regardless of where VirtualAlloc lands.
            auto* thunk=page;
            auto* targetStub=page+64;
            targetStub[0]=0xFF;targetStub[1]=0x25;targetStub[2]=0x00;targetStub[3]=0x00;targetStub[4]=0x00;targetStub[5]=0x00;
            *reinterpret_cast<uint64_t*>(targetStub+6)=reinterpret_cast<uint64_t>(&thunkTargetFunction);
            const int64_t relative=targetStub-(thunk+5);
            check(relative>=-0x7FFFFFFFll&&relative<=0x7FFFFFFFll,"test thunk target is in relative range");
            thunk[0]=0xE9;
            *reinterpret_cast<int32_t*>(thunk+1)=static_cast<int32_t>(relative);
            for(int i=0;i<11;++i)thunk[5+i]=0xCC;
            FlushInstructionCache(GetCurrentProcess(),page,0x1000);
            auto callThunk=reinterpret_cast<int(*)(int)>(thunk);
            thunkHookCalls=0;
            check(callThunk(5)==6,"thunk reaches the original function");
            veyra::ThunkHook hook;
            const auto status=hook.install(thunk,reinterpret_cast<void*>(&thunkReplacementFunction));
            check(status.installed,"thunk hook installs on a canonical thunk");
            thunkOriginal=reinterpret_cast<int(*)(int)>(status.trampoline);
            check(callThunk(5)==106&&thunkHookCalls==1,"replacement runs and forwards through the trampoline");
            check(hook.remove(),"thunk hook restores the original bytes");
            check(callThunk(5)==6&&thunkHookCalls==1,"original thunk works after removal");
            veyra::ThunkHook reject;
            const auto rejected=reject.install(page+512,reinterpret_cast<void*>(&thunkReplacementFunction));
            check(!rejected.installed&&!rejected.error.empty(),"non-thunk target is rejected instead of guessed");
            VirtualFree(page,0,MEM_RELEASE);
        }
    }
    std::cout<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
