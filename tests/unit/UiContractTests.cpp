#include "../../apps/veyra/ui/UiSessionState.h"
#include "../../apps/veyra/ui/WorkspaceChrome.h"
#include "../../apps/veyra/ui/WorkspaceTransition.h"
#include "../../apps/veyra/ui/TransportLayout.h"
#include "../../apps/veyra/ui/UiPreferenceStore.h"
#include "../../apps/veyra/ui/CapturePreferenceStore.h"
#include "../../apps/veyra/ui/SourceTitle.h"
#include "veyra/engine/EngineController.h"
#include "../../apps/veyra/ui/LiveStatusHistory.h"
#include "veyra/sink/AudioGain.h"
#include "veyra/engine/PreviewView.h"
#include "veyra/source/CaptureAudioClock.h"
#include "veyra/source/DeckLinkInputConnection.h"
#include "veyra/ui/InterfaceRenderer.h"
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
// Judge intentional rate changes against policy, never against measured output.
void outputRateContracts(){
    using namespace veyra;
    using ui::live_status::DashboardHistory;
    using ui::live_status::RateContext;
    const auto live=[](){
        engine::PlayerSnapshot s;s.running=s.capture=true;s.transport=engine::TransportState::Playing;
        s.sessionId=1;s.applied.revision=1;s.captureReceived=120;s.captureFps=s.nominalSourceFps=60;
        s.metrics.flow.rateWindowReady=true;s.presentationEffective.display=engine::DisplaySync::Tearing;
        return s;
    };
    const auto settle=[](DashboardHistory& h,engine::PlayerSnapshot& s,double skipHz,RateContext c={}){
        for(int i=0;i<24;++i){c.nowSeconds=i*.25;s.captureRateSkipped=uint64_t(skipHz*c.nowSeconds);h.sample(s,c);}
    };
    DashboardHistory h;auto s=live();
    s.applied.content=engine::ContentRate::Auto;s.contentFps=30;s.metrics.flow.presentSubmitFps=30;
    settle(h,s,30);
    require(std::abs(h.output.targetFps-30)<.01&&!h.overloaded&&std::wstring(h.rateStatus(s))==L"正常","60Hz transport with 30 unique frames is healthy at 30fps");
    require(h.output.deduplicating,"dedup reason requires observed intentional skips");
    s.metrics.flow.presentSubmitFps=15;settle(h,s,30);
    require(h.overloaded,"real output deficit remains visible after dedup");
    h={};s=live();s.applied.content=engine::ContentRate::Auto;s.contentFps=30;s.metrics.flow.presentSubmitFps=30;
    settle(h,s,0);
    require(h.overloaded&&!h.output.deduplicating&&h.output.targetFps==60,"enabling Auto or a learned hint alone cannot excuse missing frames");
    h={};s=live();s.applied.content=engine::ContentRate::Auto;s.contentFps=4;s.metrics.flow.presentSubmitFps=4;
    settle(h,s,56);
    require(!h.overloaded&&h.output.targetFps==4,"intentional static-picture dedup is healthy");
    s.metrics.flow.presentSubmitFps=0;settle(h,s,60);
    require(h.overloaded&&h.output.targetFps>=1,"even maximal skip counts cannot hide a stopped presenter");
    h={};s=live();s.applied.content=engine::ContentRate::Capture60To30;s.captureHalfRate=true;s.contentFps=30;
    s.metrics.flow.presentSubmitFps=30;settle(h,s,30);
    require(h.output.targetFps==30&&!h.overloaded,"60-to-30 and its skip counter are counted once");
    h={};s=live();s.captureFps=40;s.applied.content=engine::ContentRate::Auto;s.contentFps=20;
    s.metrics.flow.presentSubmitFps=20;settle(h,s,20);
    require(!h.overloaded&&h.inputLimited&&std::wstring(h.rateStatus(s))==L"输入帧率不足","dedup never hides a deficient input");
    h={};s=live();s.applied.multiplier=4;s.presentationEffective.display=engine::DisplaySync::Vsync;
    s.metrics.flow.presentSubmitFps=60;settle(h,s,0,{60});
    require(h.output.targetFps==60&&h.output.displayLimited&&!h.overloaded,"VSync target is bounded by the actual display");
    s.fgBudgetLimited=true;settle(h,s,0,{60});
    require(std::wstring(h.rateStatus(s))==L"正常","meeting a display-limited target is normal despite redundant FG admission being reduced");
    s.metrics.flow.presentSubmitFps=45;settle(h,s,0,{60});
    require(h.overloaded&&std::wstring(h.rateStatus(s))!=L"正常","VSync cannot hide output below its effective target");
    h={};s=live();s.applied.multiplier=4;s.presentationEffective.display=engine::DisplaySync::Automatic;
    s.metrics.flow.presentSubmitFps=60;settle(h,s,0,{60});
    require(h.overloaded&&h.output.targetFps==240&&!h.output.displayLimited,"Automatic replaces unshown frames instead of waiting, so the refresh rate is not its submission target");
    s.presentationEffective.display=engine::DisplaySync::Tearing;++s.presentationRevision;settle(h,s,0,{60});
    require(h.overloaded&&h.output.targetFps==240&&!h.output.displayLimited,"Tearing does not inherit a display cap");
    h={};s=live();s.applied.multiplier=4;s.presentationEffective.display=engine::DisplaySync::Vsync;
    s.metrics.flow.presentSubmitFps=60;settle(h,s,0);
    require(h.overloaded&&!h.output.displayLimited,"unknown monitor refresh is never guessed");
    h={};s=live();s.applied.multiplier=4;s.presentationEffective.outputRate=engine::OutputRateMode::Custom;
    s.presentationEffective.customFps=30;s.metrics.flow.presentSubmitFps=30;settle(h,s,0);
    require(!h.overloaded&&h.output.outputLimited&&h.output.targetFps==30,"effective custom output cap is a normal target");
    s.metrics.flow.presentSubmitFps=15;settle(h,s,0);
    require(h.overloaded,"real deficit below a custom cap remains visible");
    h={};s=live();s.applied.multiplier=4;s.presentationEffective.outputRate=engine::OutputRateMode::FollowDisplay;
    s.metrics.flow.presentSubmitFps=144;settle(h,s,0,{144});
    require(!h.overloaded&&h.output.targetFps==144&&h.output.outputLimited,"follow-display cap is applied even with unsynchronized present");
    h={};s=live();s.applied.multiplier=2;s.applied.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    s.presentation.outputRate=engine::OutputRateMode::Custom;s.presentation.customFps=30;
    s.presentationEffective.outputRate=engine::OutputRateMode::Off;s.metrics.flow.xessSdkSubmitFps=120;
    settle(h,s,0);
    require(!h.overloaded&&h.output.targetFps==120&&!h.output.outputLimited,"provider-disabled requested cap cannot lower the target");
    s.metrics.flow.xessSdkSubmitFps=30;settle(h,s,0);
    require(h.overloaded,"requested but ineffective provider cap cannot conceal missing SDK submissions");
    h={};s=live();s.capture=false;s.nominalSourceFps=30;s.playbackRate=.5;s.metrics.flow.presentSubmitFps=15;
    settle(h,s,0);require(!h.overloaded&&h.output.targetFps==15&&h.output.speedAdjusted,"half-speed file playback is healthy at half the source rate");
    s.playbackRate=1.5;s.metrics.flow.presentSubmitFps=45;settle(h,s,0);
    require(!h.overloaded&&h.output.targetFps==45,"faster file playback raises rather than lowers the target");
    h={};s=live();s.applied.multiplier=4;s.metrics.flow.presentSubmitFps=60;
    settle(h,s,0,{0,true});require(!h.overloaded&&h.output.targetFps==60,"comparison suspends FG and is judged at the original-frame rate");
    s.metrics.flow.presentSubmitFps=30;settle(h,s,0,{0,true});
    require(h.overloaded,"comparison still reports original-frame starvation");
    h={};s=live();s.presentationEffective.enabled=true;s.metrics.flow.presentSubmitFps=30;
    settle(h,s,0);require(h.overloaded&&h.output.targetFps==60,"low-latency queue alone is not a rate cap");
    s.presentationEffective.outputRate=engine::OutputRateMode::Custom;s.presentationEffective.customFps=30;++s.presentationRevision;
    h.sample(s,{0,false,6});require(!h.overloaded,"changing the effective presentation policy revokes stale output warnings");
    h={};s=live();s.applied.content=engine::ContentRate::Auto;s.contentFps=30;s.metrics.flow.presentSubmitFps=30;
    settle(h,s,30);s.applied.content=engine::ContentRate::Transport;++s.applied.revision;
    h.sample(s,{0,false,6});require(h.skipped.empty()&&!h.output.deduplicating&&h.output.targetFps==60,"source-timestamp mode revokes dedup history");
    s.applied.multiplier=2;s.fgBudgetLimited=true;settle(h,s,0);
    require(std::wstring(h.rateStatus(s))==L"补帧调度降档","uncapped FG admission shortfall retains its own warning");
    std::cout<<"PASS output-rate policy: dedup, static, half-rate, display sync, caps, provider, speed, comparison, real deficits and resets\n";
}
// 设置 → 界面绘制 (2026-10-26): software unless GPU is chosen; the OBS and 监控软件兼容
// rules and VEYRA_UI_RHI keep the meaning they had before the default changed.
static void interfaceRendererContracts(){
    using namespace veyra::ui;
    using R=InterfaceRendererReason;
    auto decide=[](bool gpu,bool obs,bool autoCompat,bool overlay,bool restart,bool forced){
        return decideInterfaceRenderer({gpu,obs,autoCompat,overlay,restart,forced});
    };
    auto d=decide(false,false,true,false,false,false);
    require(d.software&&d.reason==R::Default&&!d.overlayCompat,"a fresh profile draws the interface in software");
    d=decide(true,false,true,false,false,false);
    require(!d.software&&d.reason==R::Gpu,"GPU chosen and nothing to stay compatible with: GPU interface");
    d=decide(true,false,true,true,false,false);
    require(d.software&&d.reason==R::OverlayCompat&&d.overlayCompat,"GPU chosen, an overlay at start and 监控软件兼容 auto: software as before");
    d=decide(true,false,false,true,false,false);
    require(!d.software&&!d.overlayCompat,"GPU chosen with 监控软件兼容 off stays GPU next to an overlay, as before");
    d=decide(true,false,true,false,true,false);
    require(d.software&&d.reason==R::OverlayCompat,"an accepted compatibility restart draws in software");
    for(bool gpu:{false,true})for(bool forced:{false,true}){
        d=decide(gpu,true,true,true,false,forced);
        require(d.software&&d.reason==R::ObsGameCapture&&!d.overlayCompat,"OBS game capture always draws in software");
    }
    d=decide(false,false,true,true,false,false);
    require(d.software&&d.reason==R::OverlayCompat&&d.overlayCompat,"software default with an overlay still reports the compatibility rule");
    for(bool gpu:{false,true})for(bool overlay:{false,true}){
        d=decide(gpu,false,true,overlay,false,true);
        require(!d.software&&d.reason==R::Gpu&&!d.overlayCompat,"VEYRA_UI_RHI picks a GPU renderer and skips the compatibility rule");
    }
    std::cout<<"PASS interface renderer: software default, GPU choice, OBS / overlay compatibility and VEYRA_UI_RHI precedence\n";
}
int wmain(int argc,wchar_t** argv){try{
    using namespace veyra;
    outputRateContracts();
    interfaceRendererContracts();
    // Manual turns compose with metadata, including an already-oriented DAR.
    for(int metadata:{0,90,180,270})for(int mode=0;mode<=6;++mode){
        engine::PreviewView v;v.rotationDegrees=metadata;v.mode=mode;
        v.displayAspect=metadata%180?3.0/4:4.0/3;
        const auto base=v;const auto aspect=v.aspect(1920,1080);
        for(int n=1;n<=4;++n){
            v.rotateClockwise();
            require(v.quarterTurns()==unsigned((metadata/90+n)%4),"manual turn composes with metadata");
            require(std::abs(v.aspect(1920,1080)-(n%2?1/aspect:aspect))<.00001,"all display ratios follow manual rotation");
            const auto source=v.sourceUv(.23f,.71f);const auto display=v.displayUv(source.first,source.second);
            require(std::abs(display.first-.23f)<.00001&&std::abs(display.second-.71f)<.00001,"manual rotation inverse preserves pointer coordinates");
            const auto anchor=v.sourcePoint(133,91,320,240,1920,1080);
            v.wheel(2,133,91,320,240,1920,1080);
            const auto after=v.sourcePoint(133,91,320,240,1920,1080);
            require(std::abs(anchor.first-after.first)<.00001&&std::abs(anchor.second-after.second)<.00001,"zoom remains anchored after rotation");
            v.wheel(-2,133,91,320,240,1920,1080);
        }
        v.zoom=base.zoom;v.centerX=base.centerX;v.centerY=base.centerY;
        require(v==base,"four manual turns restore original metadata and display transform");
        v.zoom=3;v.centerX=.2f;v.rotateClockwise();
        require(v.zoom==1&&v.centerX==.5f&&v.centerY==.5f,"rotation recentres a zoomed picture");
    }
    {source::CaptureAudioClock c;require(c.observe({},100,10,false)==100,"untimed audio anchors to arrival");require(c.observe({},170,10,false)==110,"driver arrival jitter cannot stretch untimed PCM");require(c.observe(120,180,10,false)==120,"valid driver timestamp takes precedence");require(c.observe({},190,10,false)==130,"missing timestamps continue from valid samples");require(c.observe({},500,10,true)==500,"discontinuity reanchors untimed audio");require(c.observe(0,510,10,true)==0,"timestamp zero remains valid");}
    {
        ui::live_status::DashboardHistory history;engine::PlayerSnapshot s;
        s.running=s.capture=true;s.transport=engine::TransportState::Playing;s.sessionId=1;s.applied.revision=1;
        s.captureReceived=120;s.captureFps=40;s.nominalSourceFps=60;s.applied.multiplier=4;
        s.metrics.flow.rateWindowReady=true;s.metrics.flow.presentSubmitFps=160;
        for(int i=0;i<9;++i)history.sample(s);
        require(!history.overloaded&&history.inputLimited,"40fps source delivering160fps at4X is input limited, not FG limited");
        s.captureFps=60;s.metrics.flow.presentSubmitFps=120;
        for(int i=0;i<9;++i)history.sample(s);
        require(history.overloaded&&!history.inputLimited,"actual output deficit remains visible");
        s.applied.multiplier=1;s.fgBudgetLimited=true;
        require(std::wstring(history.rateStatus(s))!=L"补帧调度降档","disabled FG cannot report FG throttling");
        for(int i=1;i<8;++i){s.metrics.flow.reset.epoch=i*10;history.sample(s);}
        require(history.resetSamples>=3,"continuous reset storm is identified separately");
        s.applied.revision=2;history.sample(s);require(!history.overloaded&&!history.inputLimited&&!history.resetSamples,"revision invalidates stale warnings");
    }
    {engine::PresentationCadence c;require(c.due(100,10)==100,"no startup pacing reserve");c.submitted(108);require(c.due(110,10)==117,"front edge spaces late outputs with bounded catch-up");c.submitted(999);require(c.due(120,10)==1008,"stale subframe cannot burst after stall");c.reset();require(c.due(120,10)==120,"reset revokes optional deadline");}
    {engine::PresentationCadence c;c.submitted(100000);require(c.due(100001,27777,20)==122222,"file MFG catch-up retains 80 percent spacing");require(c.due(100001,27777,100)==122222,"catch-up cannot request an unbounded burst");require(c.due(150000,27777,20)==150000,"catch-up never presents before media deadline");require(c.due(100001,27777)==125000,"default pacing retains 90 percent spacing");c.reset();require(c.due(90000,27777,20)==90000,"file catch-up resets on timeline change");}
    {engine::PreviewView v;v.wheel(3,800,300,1000,600,1920,1080);require(std::abs(v.zoom-1.728f)<.0001f,"wheel scale");
    const float anchored=(800-500)/(1920.0f*(1000.0f/1920)*v.zoom)+v.centerX;require(std::abs(anchored-.8f)<.0001f,"pointer anchor fixed");
    v.wheel(-3,800,300,1000,600,1920,1080);require(std::abs(v.zoom-1)<.0001f&&std::abs(v.centerX-.5f)<.0001f,"inverse wheel returns same view");
    v.pan(100,0,1000,600,1920,1080);require(std::abs(v.centerX-.4f)<.0001f,"pan measured in displayed pixels");}
    {engine::PreviewView v;v.rotationDegrees=90;
        require(std::abs(v.aspect(1920,1080)-9.0/16)<.00001,"missing SAR still gives oriented preview aspect");
        const auto size=v.renderedSize(320,240,64,32);require(size.first==120&&size.second==240,"portrait contain uses rotated extent");
        const auto source=v.sourcePoint(130,60,320,240,64,32);require(source.first==.25f&&source.second==.75f,"portrait protection maps display corner to coded source");
        const auto display=v.displayUv(source.first,source.second);require(display.first==.25f&&display.second==.25f,"inverse source/display rotation agrees");
        v.mode=1;const auto native=v.renderedSize(320,240,64,32);require(native.first==32&&native.second==64,"native-pixel view swaps dimensions for portrait");
        require(std::abs(v.aspect(1920,1080)-9.0/16)<.00001,"native preview aspect rotates the output extent");
        v.mode=0;v.rotationDegrees=-90;const auto opposite=v.sourceUv(.25f,.25f);require(opposite.first==.75f&&opposite.second==.25f,"opposite display matrix rotates the other way");
        v.rotationDegrees=180;const auto upsideDown=v.sourceUv(.25f,.25f);require(upsideDown.first==.75f&&upsideDown.second==.75f,"upside-down display matrix maps both axes");
        v.rotationDegrees=90;v.displayAspect=.75;require(std::abs(v.aspect(64,32)-.75)<.00001,"already-oriented SAR is not inverted twice");}
    ui::UiSessionState state;require(!state.enhanced&&!state.configured.nr&&!state.configured.sr&&state.configured.multiplier==1,"fresh startup effects disabled");state.configured.forceSdrPreview=true;state.enhanced=true;state.configured.nr=true;state.configured.multiplier=4;state.configured.sr=true;state.configured.model.intensity=.37f;state.configured.residual.color=1.4f;
    auto before=state.effective();require(state.mode==ui::Mode::Daily,"startup Daily");state.mode=ui::Mode::Professional;require(state.effective()==before,"mode cannot alter settings");state.enhanced=false;auto bypass=state.effective();require(!bypass.nr&&!bypass.sr&&bypass.multiplier==1,"true full bypass");require(bypass.forceSdrPreview,"master bypass retains SDR display choice");state.enhanced=true;require(state.effective()==before,"restore all settings");
    for(bool input:{false,true})for(bool display:{false,true})for(bool force:{false,true}){engine::EnhancementSettings s;s.forceSdrPreview=force;require(s.useHdrPreview(input,display)==(input&&display&&!force),"HDR output policy");}
    ui::ChromeLayout daily(1440,900,false,false);require(daily.left==0&&daily.top==0&&daily.viewWidth==1440&&daily.viewHeight==812&&daily.bottom==812,"daily video owns all space above transport");
    for(int width:{688,768,868,928,1008,1040,1148,1248,1888,2528}){
        ui::TransportLayout t(width,true);std::vector<ui::TransportSlot> slots={t.open,t.capture,t.recent,t.master,t.sr,t.stop,t.play,t.mute,t.volume,t.subtitle,t.fullscreen,t.mode,t.minimize,t.close};
        for(size_t i=0;i<slots.size();++i){require(slots[i].width>0&&slots[i].x>=0&&slots[i].x+slots[i].width<=width,"all daily actions visible and contained");for(size_t j=0;j<i;++j)require(slots[i].x>=slots[j].x+slots[j].width||slots[j].x>=slots[i].x+slots[i].width,"direct daily actions do not overlap");}
    }
    for(int width:{290,400,480,508,700,1000}){ui::TransportLayout t(width,false);require(t.play.x+t.play.width<=t.mute.x&&t.stop.x+t.stop.width<=t.mute.x,"compact professional transport leaves room for audio and subtitle controls");}
    ui::WorkspaceTransition animation;animation.start(true,1000);animation.sample(1120);const auto halfway=animation.value;require(halfway>.49&&halfway<.51,"visible intermediate expansion");animation.start(false,1120);require(animation.value==halfway,"reverse without jump");animation.sample(1240);require(animation.value>0&&animation.value<halfway,"panel collapses progressively");animation.sample(1360);require(!animation.running&&animation.value==0,"collapse ends exactly");
    using pipeline::ResolutionPlan;using pipeline::NrSizePolicy;using pipeline::Extent;
    for(auto policy:{NrSizePolicy::P480,NrSizePolicy::P720,NrSizePolicy::P900,NrSizePolicy::Realtime,NrSizePolicy::P1440}){
        engine::EnhancementSettings s;s.nrPolicy=policy;
        require(s.validate().empty()&&engine::PlayerOptions::from(s).snapshot().nrPolicy==policy,"NR policy survives controller round trip");
        const auto p=ResolutionPlan::make({3840,2160},false,policy,false);
        require(p.nr.height==pipeline::nrHeightLimit(policy)&&p.output==Extent{3840,2160}&&p.flow==p.nr,"NR tier reduces internal work only");
        require(ResolutionPlan::make({3840,2160},false,policy,true).nr==Extent{3840,2160},"all export policies retain full extent");
        require(ResolutionPlan::make({320,180},false,policy,false).nr==Extent{320,180},"NR tier never upscales smaller source");
        auto portrait=ResolutionPlan::make({1080,1920},false,policy,false).nr;
        require(std::abs(double(portrait.width)/portrait.height-1080.0/1920)<.005,"NR portrait aspect retained");
    }
    if(argc>1){
        ui::CapturePreferenceStore store(argv[1]);ui::CapturePreferences p{L"\\\\?\\usb#video-\u91c7\u96c6",L"1920:1080:166833:{SUBTYPE}:RGB32",L"{audio-endpoint}",-3,2};
        require(store.load().videoPath.empty(),"fresh capture preference empty");require(store.save(p),"capture preference atomic save");
        const auto restored=store.load();require(restored.videoPath==p.videoPath&&restored.formatKey==p.formatKey&&restored.audioPath==p.audioPath&&restored.audioMode==p.audioMode&&restored.colorOverride==p.colorOverride,"capture stable identifiers and Unicode round trip");
        for(uint32_t connection:{1u,2u}){
            auto native=p;native.videoPath=source::decklink::inputConnectionPath(L"decklink-native:p:0000000000000123:0",connection);native.formatKey=L"decklink:auto";
            require(store.save(native),"save BMD explicit connection using existing preference format");
            const auto resumed=store.load();require(resumed.videoPath==native.videoPath&&resumed.formatKey==native.formatKey&&source::decklink::inputConnectionSelection(resumed.videoPath).connection==connection,"resume keeps exact HDMI or SDI connection and automatic signal mode");
        }
        for(double fps:{0.0,29.97,30.0,40.0,50.0,60.0}){p.requestedFps=fps;require(store.save(p)&&store.load().requestedFps==fps,"device capture rate survives restart");}
        p.requestedFps=-1;require(!store.save(p),"invalid capture rate not persisted");
        p.requestedFps=0;p.colorOverride=5;p.deviceColors={{L"card-A",5},{L"card-B",10}};
        require(store.save(p),"per-device capture color save");
        const auto profiles=store.load();
        require(profiles.colorForDevice(L"card-A")==5&&profiles.colorForDevice(L"card-B")==10&&profiles.colorForDevice(L"new-card")==0,"independent device PQ limited and HLG full survive restart");
        p.colorOverride=12;require(!store.save(p),"invalid packed color rejected");
        for(unsigned space=0;space<4;++space)for(unsigned range=0;range<3;++range){
            const unsigned packed=source::captureColorOverride(space,range);
            pipeline::ColorDescription color; color.range=pipeline::ColorRange::Full;
            source::applyCaptureColorOverride(color,packed);
            require(source::validCaptureColorOverride(packed)&&source::captureColorSpace(packed)==space&&source::captureColorRange(packed)==range,"capture color encoding round trip");
            require(color.range==(range==1?pipeline::ColorRange::Limited:pipeline::ColorRange::Full),"range auto preserves metadata, manual wins");
            if(space)require(color.transfer==(space==1?pipeline::TransferFunction::PQ:space==2?pipeline::TransferFunction::HLG:pipeline::TransferFunction::BT709)&&!color.transferAssumed&&!color.matrixAssumed&&color.preserveSdrCodeValues==(space==3),"manual color transfer and SDR preservation contract");
        }
        for(int version:{1,2}){
            std::wstring legacy=L"VEYRA_CAPTURE "+std::to_wstring(version)+L"\n\"video\" \"format\" -1 \"\" 0"+(version==2?L" 1":L"")+L"\n";
            {std::ofstream file(std::filesystem::path(argv[1])/"capture-preferences.v1",std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(legacy.data()),std::streamsize(legacy.size()*sizeof(wchar_t)));}
            const auto old=store.load();require(old.videoPath==L"video"&&old.requestedFps==0&&old.formatHintDismissed==(version==2),"old capture preferences migrate without changing device rate");
        }
    }
    {
        double fps=0;
        for(auto value:{L"nan",L"inf",L"-1",L"0.5",L"1001",L"30x",L"30.0.1",L"",L"30?fps=60"})require(!source::parseCaptureFrameRate(value,fps),"invalid device rate rejected");
        require(source::parseCaptureFrameRate(L"29.97",fps)&&fps==29.97,"fractional rate accepted");
        require(source::captureFrameInterval(30)==333333&&source::captureFrameInterval(40)==250000,"device receives 100ns intervals");
        require(source::captureFrameRateMatches(30,333667)&&!source::captureFrameRateMatches(30,166667)&&!source::captureFrameRateMatches(40,333333),"driver rounded or ignored rate distinguished");
    }
    require(ResolutionPlan::make({1448,1086},true,NrSizePolicy::Native,true).output==Extent{2880,2160},"4:3 SR preserves aspect");
    {
        // A live capture card shares its window list entry with every external
        // broadcast tool. The encoded connection string must never become the
        // window title again (it made the live window unrecognisable).
        const std::wstring encoded=L"capture2:005C005C003F005C0075007300620023007600690064005F003300340035006600:0:0:005C005C003F005C0061007500640069006F:0";
        require(ui::isCaptureCardSource(encoded)&&ui::isCaptureCardSource(L"capture:0:1:-1:0"),"capture connection strings detected");
        require(ui::windowTitleForSource(encoded)==L"Veyra — 采集卡 · LIVE","capture2 connection string never becomes the window title");
        require(ui::windowTitleForSource(L"capture:0:1:-1:0")==L"Veyra — 采集卡 · LIVE","legacy capture connection string never becomes the window title");
        require(ui::windowTitleForSource(L"remoteplay:")==L"Veyra — PS5 Remote Play","remote play keeps its readable title");
        require(ui::windowTitleForSource(L"D:\\media\\clip.mp4")==L"Veyra — clip.mp4","file title keeps the file name");
        require(ui::windowTitleForSource(L"")==L"Veyra — 本地实验版","empty source restores the default title");
        for(const auto& title:{ui::windowTitleForSource(encoded),ui::windowTitleForSource(L"capture:0:1:-1:0")})
            require(title.find(L"capture2:")==std::wstring::npos&&title.find(L"capture:")==std::wstring::npos&&title.find(L'\\')==std::wstring::npos&&title.size()<64,"exposed window title stays short and path free");
    }
    require(ResolutionPlan::make({1080,1920},true,NrSizePolicy::Native,true).output==Extent{1214,2160},"portrait SR even dimensions");
    require(!ResolutionPlan::make({2880,2160},true,NrSizePolicy::Native,true).srApplied,"already maximum edge skips SR");
    require(ResolutionPlan::make({1920,1080},true,NrSizePolicy::Realtime,false).output==Extent{3840,2160},"16:9 SR unchanged");
    require(ResolutionPlan::make({3840,2160},false,NrSizePolicy::Realtime,false).flow==Extent{1920,1080},"realtime 4K uses bounded flow extent");
    require(ResolutionPlan::make({3840,2160},false,NrSizePolicy::Native,false).flow==Extent{3840,2160},"native 4K retains native flow extent");
    for(int dpi:{96,120,144,192})for(int width:{720,900,960,1180,1280,1920})for(int height:{540,720,800,1080})for(bool pro:{false,true})for(bool drawer:{false,true}){
        ui::ChromeLayout l(width,height,pro,drawer);require(l.left>=0&&l.top>=0&&l.viewWidth>0&&l.viewHeight>0,"nonnegative viewport");require(l.left+l.viewWidth<=width&&l.top+l.viewHeight<=height,"viewport contained");if(pro&&(width>=960||drawer))require(l.left+l.viewWidth<l.right&&l.right+l.panelWidth<=width,"disjoint inspector");
        require(MulDiv(l.viewWidth,dpi,96)>0,"DPI physical extent");
    }
    std::vector<float> tone(4800*2,1);float gain=1;sink::applyStereoGain(tone.data(),4800,.5f,gain);require(std::abs(gain-.5f)<1e-6,"gain ramp reaches half");for(size_t i=480;i<tone.size();++i)require(std::abs(tone[i]-.5f)<1e-6,"actual PCM scaled");tone.assign(tone.size(),1);sink::applyStereoGain(tone.data(),4800,0,gain);require(gain==0,"mute reaches zero");for(size_t i=480;i<tone.size();++i)require(tone[i]==0,"muted PCM exactly zero");
    if(argc>1){auto dir=std::filesystem::path(argv[1]);ui::UiPreferenceStore prefs(dir);const auto fresh=prefs.startup({});require(!fresh.nr&&!fresh.sr&&fresh.multiplier==1,"missing preferences do not enable effects");ui::UiPreferences p;p.volume=.45f;p.muted=true;p.inspector=3;p.width=1180;require(prefs.save(p,&before),"atomic preferences save");ui::UiPreferenceStore loaded(dir);auto restored=loaded.load();require(restored.volume==p.volume&&restored.muted&&restored.inspector==3,"preferences round trip");
    // Regression (2026-09-17): version 3 preferences were rejected wholesale
    // (inspectorWidth was read only for v2, so the first v3 field misaligned),
    // and inspector 4 - the audio page, one of the five rail tabs - failed the
    // range check. Both silently reset window size, volume and subtitle style.
    {ui::UiPreferences audio;audio.volume=.9f;audio.muted=true;audio.inspector=4;audio.inspectorWidth=380;audio.width=1600;audio.height=900;require(prefs.save(audio,nullptr),"audio-page preferences save");ui::UiPreferenceStore reload(dir);const auto back=reload.load();require(std::abs(back.volume-.9f)<1e-6&&back.inspector==4&&back.inspectorWidth==380&&back.width==1600,"audio page and geometry survive the version 3 round trip");}
    auto setting=loaded.startup({});before.revision=setting.revision;require(setting==before,"all confirmed parameters round trip");
    for(int enabled:{0,1})for(const auto runtime:{engine::NrRuntime::Original,engine::NrRuntime::Community,engine::NrRuntime::Ampere,engine::NrRuntime::NvidiaOriginal}){p.enhancementEnabled=enabled;before.nr=true;before.nrRuntime=runtime;before.nrTemporal=true;require(prefs.save(p,&before),"master and runtime save");ui::UiPreferenceStore restart(dir);require(restart.load().enhancementEnabled==enabled,"master state survives restart");auto restoredSettings=restart.startup({});require(restoredSettings.nr&&restoredSettings.nrRuntime==engine::currentNrRuntime(runtime)&&restoredSettings.nrTemporal,"bypassed effects, selected runtime (legacy migrated) and temporal NR survive restart");}
    for(bool enabled:{false,true})for(unsigned mode=0;mode<3;++mode)for(unsigned sync=0;sync<3;++sync)for(unsigned rate=0;rate<3;++rate){p.presentation={enabled,engine::PacingMode(mode),engine::DisplaySync(sync),engine::OutputRateMode(rate),rate==2?119.88:60.0};require(prefs.save(p,nullptr),"presentation atomic save");ui::UiPreferenceStore restart(dir);require(restart.load().presentation==p.presentation,"all presentation combinations and output caps survive restart");}
    auto path=dir/"ui-preferences.v1";
    for(int lines:{0,1,2,8})for(bool fit:{false,true}){p.subtitleLines=lines;p.subtitleFitToLines=fit;require(prefs.save(p,nullptr),"subtitle layout save");ui::UiPreferenceStore restart(dir);auto back=restart.load();require(back.subtitleLines==lines&&back.subtitleFitToLines==fit,"subtitle lines and explicit fitting survive restart");}
    {std::ofstream legacy(path);legacy<<"VEYRA_UI 6\n0.45 1 1 1280 800 0 0 0 4 22 392 1 0 1 16 3 0 1 1 2 1\n";legacy.close();ui::UiPreferenceStore migration(dir);auto old=migration.load();require(old.subtitleLines==2&&old.presentation.enabled&&old.presentation.mode==engine::PacingMode::Reflex&&old.subtitleMargin==16&&old.inspector==4,"v6 migrates subtitle layout without losing settings");require(migration.save(old,nullptr),"v6 remains writable");}
    for(int version=1;version<=5;++version){std::ofstream legacy(path);legacy<<"VEYRA_UI "<<version<<"\n1 0 1 1280 800 0 0 0 1 22";if(version>=2)legacy<<" 392";if(version>=3)legacy<<" 1 0 0 0 0";if(version>=4)legacy<<" 0";if(version>=5)legacy<<" 1";legacy.close();ui::UiPreferenceStore migration(dir);require(!migration.load().presentation.enabled,"old preferences default pacing off");require(migration.save({},nullptr),"old preferences migrate without corrupt lock");}
    {std::ofstream bad(path);bad<<"UNKNOWN 9 invalid";}ui::UiPreferenceStore damaged(dir);damaged.load();require(!damaged.save(p,nullptr),"corrupt preference cannot be overwritten");std::ifstream f(path);std::string data((std::istreambuf_iterator<char>(f)),{});require(data=="UNKNOWN 9 invalid","corrupt original preserved");}
    std::cout<<"PASS: mode/bypass, 384 layout cases at four DPI scales, actual PCM gain/mute, confirmed settings persistence/corruption\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
