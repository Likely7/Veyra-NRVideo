// RTX Video SR model families (standard 1..4, high bitrate 16..19, streaming
// 21/23) with tiers, double SR and the supersample shrink across families, and
// persistence. Older readers reject the Video Effects values, so files that use
// them (as the base or the second SR) get a new version (settings presets) or a
// .video-ai sidecar (preset library, mode session); the older file stays as it was.
// Since 2026-11-08 double SR also takes DLSS SR and FSR in either pass; a DLSS SR /
// FSR second pass gets its own version again (v36, library v19 / session v14 in
// a .dual-sr-any sidecar).
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/PresetStore.h"
#include "veyra/engine/PresetLibrary.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
using namespace veyra::engine;
using veyra::pipeline::SrTarget;using veyra::pipeline::SrPrescale;using veyra::pipeline::SrOutput;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::cout<<"FAIL "<<why<<'\n';}}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
std::filesystem::path sidecar(const std::filesystem::path& p,const wchar_t* suffix=L".video-ai"){return std::filesystem::path(p).concat(suffix);}
// Every file of one library: the base name plus the sidecars older versions chained onto it.
std::map<std::wstring,std::string> files(const std::filesystem::path& base){
    std::map<std::wstring,std::string> out;
    for(const auto& e:std::filesystem::directory_iterator(base.parent_path()))
        if(e.path().filename().wstring().starts_with(base.filename().wstring()))out[e.path().filename().wstring()]=bytes(e.path());
    return out;
}
bool kept(const std::map<std::wstring,std::string>& before,const std::map<std::wstring,std::string>& after){
    return std::all_of(before.begin(),before.end(),[&](const auto& f){const auto it=after.find(f.first);return it!=after.end()&&it->second==f.second;});
}
veyra::pipeline::EnhanceGraphDesc describe(const EnhancementSettings& s,uint32_t width,uint32_t height,veyra::pipeline::ResolutionPlan* plan=nullptr,
                                           bool nvidia=true,bool still=false){
    StageRequest request;request.sr=s.sr;request.width=width;request.height=height;request.nvidiaAdapter=nvidia;request.stillImage=still;
    veyra::pipeline::EnhanceGraphDesc desc;const auto p=describeStages(request,s,desc);
    if(plan)*plan=p;
    return desc;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2||!std::filesystem::path(argv[1]).is_absolute())return 2;
    const std::filesystem::path root=argv[1];if(!std::filesystem::create_directories(root))return 2;

    // Stored values: 0 DLSS, 5 FSR, and the three RTX Video SR families.
    for(uint32_t q=0;q<40;++q){
        const bool rtx=(q>=1&&q<=4)||(q>=16&&q<=19)||q==21||q==23;
        check(rtxVideoSr(q)==rtx,"RTX Video SR family values");
        check(validVideoSrQuality(q)==(rtx||q==0||q==kVideoSrFsr),"stored SR quality table");
        EnhancementSettings s;s.sr=true;s.videoSrQuality=q;
        check(s.validate().empty()==validVideoSrQuality(q),"validation accepts exactly the stored SR qualities");
        check(ngxVideoSr(q)==(q>=1&&q<=4)&&vfxVideoSr(q)==(rtx&&q>4),"standard runs on NGX, the others on Video Effects");
    }
    check(vfxVideoSrNeedsAmpere(21)&&vfxVideoSrNeedsAmpere(23)&&!vfxVideoSrNeedsAmpere(16)&&!vfxVideoSrNeedsAmpere(19)&&!vfxVideoSrNeedsAmpere(4),
          "only the streaming family needs Ampere");
    // Family + tier <-> stored value.
    for(uint32_t tier=1;tier<=4;++tier){
        check(videoSrQualityFor(VideoSrFamily::Standard,tier)==tier,"standard tiers are 1..4");
        check(videoSrQualityFor(VideoSrFamily::HighBitrate,tier)==15+tier,"high bitrate tiers are 16..19");
        check(videoSrQualityFor(VideoSrFamily::Streaming,tier)==(tier<=2?21u:23u),"streaming has medium and ultra only");
        for(auto family:{VideoSrFamily::Standard,VideoSrFamily::HighBitrate}){
            const auto q=videoSrQualityFor(family,tier);
            check(videoSrFamily(q)==family&&videoSrTier(q)==tier,"family and tier round trip");
        }
    }
    check(videoSrFamily(21)==VideoSrFamily::Streaming&&videoSrTier(21)==2&&videoSrFamily(23)==VideoSrFamily::Streaming&&videoSrTier(23)==4,
          "streaming values report medium and ultra");
    check(videoSrQualityFor(VideoSrFamily::HighBitrate,0)==16&&videoSrQualityFor(VideoSrFamily::HighBitrate,9)==19,"tiers clamp to 1..4");

    // Double SR and the supersample shrink accept every family, also mixed.
    {
        EnhancementSettings s;s.sr=true;s.videoSrQuality=19;s.dualSrEnabled=true;s.dualSrQuality=23;s.srPrescale=SrPrescale::P1080;
        check(s.validate().empty(),"high bitrate base with a streaming second pass");
        auto active=s.activeVideoSettings();
        check(active.dualSrEnabled&&active.dualSrQuality==23&&active.srPrescale==SrPrescale::P1080,"double SR stays active on high bitrate");
        for(uint32_t bad2:{6u,15u,20u,22u,24u}){auto bad=s;bad.dualSrQuality=bad2;check(!bad.validate().empty(),"second SR must be a stored SR value");}
        for(uint32_t good2:{0u,kVideoSrFsr}){auto good=s;good.dualSrQuality=good2;check(good.validate().empty(),"DLSS SR and FSR are valid second passes");}
        s.videoSrQuality=4;s.dualSrQuality=17;check(s.validate().empty()&&s.activeVideoSettings().dualSrEnabled,"standard base with a high bitrate second pass");
        s.videoSrQuality=0;check(s.activeVideoSettings().dualSrEnabled,"a DLSS SR base runs double SR");
        s.videoSrQuality=kVideoSrFsr;check(s.activeVideoSettings().dualSrEnabled,"an FSR base runs double SR");
        // The DLSS SR model belongs to every DLSS SR pass, the second one too.
        auto model=s;model.videoSrQuality=4;model.dualSrQuality=0;model.dlssSrModel=1;
        check(model.activeVideoSettings().dlssSrModel==1,"a DLSS SR second pass keeps the DLSS SR model");
        model.dualSrQuality=17;check(model.activeVideoSettings().dlssSrModel==0,"without a DLSS SR pass the model is inactive");
        // DLSS SR / FSR passes read the SR motion source, RTX Video SR passes do not.
        auto motion=s;motion.videoSrQuality=4;motion.dualSrQuality=kVideoSrFsr;
        auto spatial=motion;spatial.dualSrQuality=17;
        check(motion.temporalSr()&&!spatial.temporalSr(),"a temporal second pass needs the SR motion source");
        EnhancementSettings ss;ss.sr=true;ss.videoSrQuality=16;ss.srOutput=SrOutput::Source;
        check(ss.activeVideoSettings().srOutput==SrOutput::Source,"supersample shrink stays on high bitrate");
        ss.videoSrQuality=0;check(ss.activeVideoSettings().srOutput==SrOutput::Target,"DLSS SR drops the supersample shrink");
    }
    // The graph plan: two passes with each family's quality, supersample sizes, and no same-size restore for Video Effects.
    {
        EnhancementSettings s;s.sr=true;s.videoSrQuality=19;s.srTarget=SrTarget::Uhd4K;
        s.dualSrEnabled=true;s.dualSrQuality=23;s.dualSrTarget=SrTarget::Uhd4K;s.srPrescale=SrPrescale::P1080;
        const auto desc=describe(s,1280,720);
        check(desc.enableSr&&desc.dualSr&&desc.videoSrQuality==19&&desc.dualSrQuality==23,"double SR plan carries both families");
        check(desc.firstSrWidth==3840&&desc.firstSrHeight==2160&&desc.srInputWidth==1920&&desc.srInputHeight==1080&&
              desc.srOutputWidth==3840&&desc.srOutputHeight==2160&&desc.workWidth==3840,"double SR sizes: 720p -> 4K -> 1080p -> 4K");
        s.videoSrQuality=4;s.dualSrQuality=16;
        check(describe(s,1280,720).dualSr,"standard base with high bitrate second pass plans two passes");
        // DLSS SR and FSR in either pass (2026-11-08).
        s.videoSrQuality=0;s.dualSrQuality=0;
        const auto dlss=describe(s,1280,720);
        check(dlss.dualSr&&dlss.videoSrQuality==0&&dlss.dualSrQuality==0&&dlss.firstSrWidth==3840&&dlss.srInputWidth==1920&&dlss.workWidth==3840,
              "DLSS SR -> DLSS SR plans 720p -> 4K -> 1080p -> 4K");
        s.dualSrQuality=kVideoSrFsr;check(describe(s,1280,720).dualSr,"DLSS SR base with an FSR second pass");
        s.videoSrQuality=kVideoSrFsr;s.dualSrQuality=19;check(describe(s,1280,720).dualSr,"FSR base with a high bitrate second pass");
        s.videoSrQuality=17;s.dualSrQuality=0;check(describe(s,1280,720).dualSr,"high bitrate base with a DLSS SR second pass");
        // Adapters: FSR runs everywhere, the others on NVIDIA only.
        s.videoSrQuality=kVideoSrFsr;s.dualSrQuality=kVideoSrFsr;
        const auto amd=describe(s,1280,720,nullptr,false);
        check(amd.enableSr&&amd.dualSr&&amd.srInputWidth==1920,"FSR -> FSR double SR runs on a non-NVIDIA adapter");
        s.dualSrQuality=2;const auto amdSingle=describe(s,1280,720,nullptr,false);
        check(!amdSingle.dualSr&&amdSingle.enableSr,"a non-NVIDIA adapter keeps the single FSR pass when the second pass needs NVIDIA");
        // FSR skips still images, so a double SR with an FSR pass keeps one pass there.
        s.dualSrQuality=kVideoSrFsr;check(!describe(s,1280,720,nullptr,true,true).dualSr,"no double SR with FSR on a still image");
        s.videoSrQuality=0;s.dualSrQuality=0;check(describe(s,1280,720,nullptr,true,true).dualSr,"DLSS SR double SR on a still image");
        EnhancementSettings ss;ss.sr=true;ss.videoSrQuality=17;ss.srTarget=SrTarget::Uhd4K;ss.srOutput=SrOutput::Qhd;
        const auto sd=describe(ss,1280,720);
        check(sd.enableSr&&!sd.dualSr&&sd.srOutputWidth==3840&&sd.workWidth==2560&&sd.workHeight==1440,"high bitrate supersample: SR to 4K, shrink to 1440p");
        EnhancementSettings same;same.sr=true;same.srTarget=SrTarget::Uhd4K;same.videoSrQuality=19;
        check(!describe(same,3840,2160).enableSr,"Video Effects families do not run at the target size");
        same.videoSrQuality=4;check(describe(same,3840,2160).enableSr,"standard family restores at the target size");
    }

    EnhancementSettings vsr;vsr.sr=true;vsr.videoSrQuality=23;vsr.srTarget=SrTarget::Qhd;
    EnhancementSettings second;second.sr=true;second.videoSrQuality=4;second.dualSrEnabled=true;second.dualSrQuality=18;second.srPrescale=SrPrescale::P900;
    EnhancementSettings both=vsr;both.videoSrQuality=19;both.dualSrEnabled=true;both.dualSrQuality=21;both.srPrescale=SrPrescale::P1080;
    // DLSS SR / FSR second passes (2026-11-08).
    EnhancementSettings dlssSecond;dlssSecond.sr=true;dlssSecond.videoSrQuality=4;dlssSecond.dualSrEnabled=true;dlssSecond.dualSrQuality=0;
    dlssSecond.dlssSrModel=1;dlssSecond.srPrescale=SrPrescale::P1080;
    EnhancementSettings fsrSecond;fsrSecond.sr=true;fsrSecond.videoSrQuality=0;fsrSecond.dualSrEnabled=true;fsrSecond.dualSrQuality=kVideoSrFsr;
    fsrSecond.srPrescale=SrPrescale::P900;fsrSecond.srTarget=SrTarget::Uhd8K;

    // Settings presets: v35 only when a Video Effects value is stored.
    {
        PresetStore plain(root/L"plain.txt");check(plain.load()&&plain.put(L"plain",EnhancementSettings{}),"plain settings preset");
        check(!bytes(root/L"plain.txt").starts_with("VEYRA_PRESETS 35"),"settings without new values keep the older version");
        EnhancementSettings stdDual;stdDual.sr=true;stdDual.dualSrEnabled=true;stdDual.dualSrQuality=2;stdDual.srPrescale=SrPrescale::P1080;
        PresetStore standardDual(root/L"standard-dual.txt");
        check(standardDual.load()&&standardDual.put(L"std",stdDual)&&bytes(root/L"standard-dual.txt").starts_with("VEYRA_PRESETS 34"),"standard double SR keeps v34");
        PresetStore store(root/L"video-ai.txt");
        check(store.load()&&store.put(L"vsr",vsr)&&store.put(L"second",second)&&store.put(L"both",both),"settings presets with new values");
        check(bytes(root/L"video-ai.txt").starts_with("VEYRA_PRESETS 35"),"a Video Effects value writes v35");
        PresetStore reader(root/L"video-ai.txt");check(reader.load()&&reader.entries().size()==3,"v35 reload");
        const auto find=[&](const wchar_t* name)->const EnhancementSettings*{
            for(const auto& e:reader.entries())if(e.name==name)return &e.settings;return nullptr;};
        const auto* a=find(L"vsr");const auto* b=find(L"second");const auto* c=find(L"both");
        check(a&&a->sr&&a->videoSrQuality==23&&a->srTarget==SrTarget::Qhd,"v35 restores the base family and tier");
        check(b&&b->videoSrQuality==4&&b->dualSrEnabled&&b->dualSrQuality==18&&b->srPrescale==SrPrescale::P900,"v35 restores a Video Effects second pass");
        check(c&&c->videoSrQuality==19&&c->dualSrQuality==21&&c->srPrescale==SrPrescale::P1080,"v35 restores both passes");
        // A DLSS SR / FSR second pass: v36, older (v35) readers reject the file whole.
        PresetStore anySr(root/L"any-sr.txt");
        check(anySr.load()&&anySr.put(L"dlss",dlssSecond)&&anySr.put(L"fsr",fsrSecond),"settings presets with DLSS SR / FSR second passes");
        check(bytes(root/L"any-sr.txt").starts_with("VEYRA_PRESETS 36"),"a DLSS SR / FSR second pass writes v36");
        PresetStore anyReader(root/L"any-sr.txt");check(anyReader.load()&&anyReader.entries().size()==2,"v36 reload");
        for(const auto& e:anyReader.entries()){
            const auto& want=e.name==L"dlss"?dlssSecond:fsrSecond;
            check(e.settings.videoSrQuality==want.videoSrQuality&&e.settings.dualSrEnabled&&e.settings.dualSrQuality==want.dualSrQuality&&
                  e.settings.srPrescale==want.srPrescale&&e.settings.dlssSrModel==want.dlssSrModel&&e.settings.srTarget==want.srTarget,"v36 restores both passes");
        }
        PresetStore mixed(root/L"video-ai.txt");check(mixed.load()&&mixed.put(L"dlss",dlssSecond)&&bytes(root/L"video-ai.txt").starts_with("VEYRA_PRESETS 36"),
              "adding a DLSS SR second pass upgrades a v35 store to v36");
        PresetStore mixedReader(root/L"video-ai.txt");check(mixedReader.load()&&mixedReader.entries().size()==4,"v36 keeps the Video Effects entries");
    }

    // Preset library: new entries go to a v18 .video-ai sidecar; the older files stay.
    {
        const auto path=root/L"library.preset";
        {
            PresetLibrary old(path);old.setIncludeBuiltins(false);
            auto e=std::make_unique<PresetEntry>();e->name=L"old";EnhancementSettings s;e->chain=toChain(s);e->globals=ChainGlobalSettings::capture(s);
            check(old.load()&&old.put(*e),"older library entry");
        }
        const auto before=files(path);
        check(before.size()==1&&!before.begin()->second.starts_with("VEYRA_PRESET_LIBRARY 18")&&!before.begin()->first.ends_with(L".video-ai"),
              "library without new values keeps an older version and file");
        std::vector<std::unique_ptr<PresetEntry>> saved;
        for(const auto& [name,settings]:{std::pair{L"vsr",&vsr},std::pair{L"second",&second},std::pair{L"both",&both}}){
            PresetLibrary library(path);library.setIncludeBuiltins(false);check(library.load(),"library reload before save");
            auto e=std::make_unique<PresetEntry>();e->name=name;e->chain=toChain(*settings);e->globals=ChainGlobalSettings::capture(*settings);
            check(library.put(*e),"save an entry with a Video Effects value");saved.push_back(std::move(e));
        }
        const auto after=files(path);
        check(kept(before,after),"older library file untouched");
        check(after.size()==before.size()+1&&std::any_of(after.begin(),after.end(),[&](const auto& f){
            return !before.contains(f.first)&&f.first.ends_with(L".video-ai")&&f.second.starts_with("VEYRA_PRESET_LIBRARY 18");}),
              "library writes v18 to one new .video-ai sidecar");
        PresetLibrary reloaded(path);reloaded.setIncludeBuiltins(false);
        check(reloaded.load()&&reloaded.entries().size()==4,"library reload reads the sidecar");
        for(const auto& e:saved){
            const auto it=std::find_if(reloaded.entries().begin(),reloaded.entries().end(),[&](const auto& r){return r.name==e->name;});
            check(it!=reloaded.entries().end()&&*it==*e,"library restores the full entry");
            if(it==reloaded.entries().end()||!e->globals)continue;
            EnhancementSettings applied;PresetLibrary::apply(*it,applied);
            check(applied.videoSrQuality==e->globals->videoSrQuality&&applied.dualSrQuality==e->globals->dualSrQuality&&
                  applied.dualSrEnabled==e->globals->dualSrEnabled,"applying restores both SR passes");
        }
        // Removing the new values keeps the newer build on the sidecar.
        PresetLibrary trimmed(path);trimmed.setIncludeBuiltins(false);check(trimmed.load(),"reload before removal");
        for(const auto* name:{L"vsr",L"second",L"both"}){
            const auto& list=trimmed.entries();
            const auto it=std::find_if(list.begin(),list.end(),[&](const auto& r){return r.name==name;});
            check(it!=list.end()&&trimmed.erase(size_t(it-list.begin())),"remove new entry");
        }
        const auto removed=files(path);
        check(kept(before,removed)&&removed.size()==before.size()+1,"removal writes the sidecar, not the older file");
    }

    // Preset library with DLSS SR / FSR second passes: v19 in a .dual-sr-any sidecar.
    {
        const auto path=root/L"any-sr.preset";
        {
            PresetLibrary old(path);old.setIncludeBuiltins(false);
            auto e=std::make_unique<PresetEntry>();e->name=L"old";e->chain=toChain(vsr);e->globals=ChainGlobalSettings::capture(vsr);
            check(old.load()&&old.put(*e),"library entry with a Video Effects value");
        }
        const auto before=files(path);
        check(before.size()==1&&before.begin()->first.ends_with(L".video-ai")&&before.begin()->second.starts_with("VEYRA_PRESET_LIBRARY 18"),
              "a Video Effects library stays v18 in its sidecar");
        std::vector<std::unique_ptr<PresetEntry>> saved;
        for(const auto& [name,settings]:{std::pair{L"dlss",&dlssSecond},std::pair{L"fsr",&fsrSecond}}){
            PresetLibrary library(path);library.setIncludeBuiltins(false);check(library.load(),"library reload before save");
            auto e=std::make_unique<PresetEntry>();e->name=name;e->chain=toChain(*settings);e->globals=ChainGlobalSettings::capture(*settings);
            check(library.put(*e),"save an entry with a DLSS SR / FSR second pass");saved.push_back(std::move(e));
        }
        const auto after=files(path);
        check(kept(before,after),"older library files untouched");
        check(after.size()==before.size()+1&&std::any_of(after.begin(),after.end(),[&](const auto& f){
            return !before.contains(f.first)&&f.first.ends_with(L".dual-sr-any")&&f.second.starts_with("VEYRA_PRESET_LIBRARY 19");}),
              "library writes v19 to one new .dual-sr-any sidecar");
        PresetLibrary reloaded(path);reloaded.setIncludeBuiltins(false);
        check(reloaded.load()&&reloaded.entries().size()==3,"library reload reads the newest sidecar");
        for(const auto& e:saved){
            const auto it=std::find_if(reloaded.entries().begin(),reloaded.entries().end(),[&](const auto& r){return r.name==e->name;});
            check(it!=reloaded.entries().end()&&*it==*e,"library restores the full DLSS SR / FSR entry");
            if(it==reloaded.entries().end()||!e->globals)continue;
            EnhancementSettings applied;PresetLibrary::apply(*it,applied);
            check(applied.dualSrEnabled&&applied.dualSrQuality==e->globals->dualSrQuality&&applied.videoSrQuality==e->globals->videoSrQuality&&
                  applied.validate().empty(),"applying restores a DLSS SR / FSR second pass");
        }
    }

    // Mode session (node mode, high bitrate base with a streaming second pass).
    {
        auto session=std::make_unique<ChainSession>(ChainSession::initial(both));check(session->select(ChainMode::Node),"node mode initializes");
        auto editor=std::make_shared<NodeEditorDocument>();editor->nodes=session->configurations[1].chain;
        editor->globals=ChainGlobalSettings::capture(both);check(editor->layout.initialize(editor->nodes).accepted,"node draft layout");
        session->configurations[1].editor=editor;check(session->valid(),"session valid");
        const auto path=root/L"modes.session";
        ChainSessionStore writer(path);check(writer.save(*session),"session save");
        check(!std::filesystem::exists(path)&&bytes(sidecar(path)).starts_with("VEYRA_CHAIN_SESSION 13"),"session writes v13 to the .video-ai sidecar only");
        auto restored=std::make_unique<ChainSession>();ChainSessionStore reader(path);
        check(reader.load(*restored)&&*session==*restored,"session restart restores both SR passes");
        auto plain=std::make_unique<ChainSession>(ChainSession::initial(EnhancementSettings{}));
        const auto plainPath=root/L"plain.session";ChainSessionStore plainWriter(plainPath);
        check(plainWriter.save(*plain)&&std::filesystem::exists(plainPath)&&!std::filesystem::exists(sidecar(plainPath)),"plain session keeps the older file");
        // A DLSS SR second pass: v14 in the .dual-sr-any sidecar, nothing else written.
        auto anySession=std::make_unique<ChainSession>(ChainSession::initial(dlssSecond));
        const auto anyPath=root/L"any-sr.session";ChainSessionStore anyWriter(anyPath);
        check(anyWriter.save(*anySession)&&!std::filesystem::exists(anyPath)&&!std::filesystem::exists(sidecar(anyPath))&&
              bytes(sidecar(anyPath,L".dual-sr-any")).starts_with("VEYRA_CHAIN_SESSION 14"),"session writes v14 to the .dual-sr-any sidecar only");
        auto anyRestored=std::make_unique<ChainSession>();ChainSessionStore anyReader(anyPath);
        check(anyReader.load(*anyRestored)&&*anySession==*anyRestored,"session restart restores the DLSS SR second pass");
    }

    std::cout<<"Video AI settings "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
