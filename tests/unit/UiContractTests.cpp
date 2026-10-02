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
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){try{
    using namespace veyra;
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
