// RTX Video SR model families (standard 1..4, high bitrate 16..19, streaming
// 21/23) with tiers, double SR and the supersample shrink across families, and
// persistence. Older readers reject the Video Effects values, so files that use
// them (as the base or the second SR) get a new version (settings presets) or a
// .video-ai sidecar (preset library, mode session); the older file stays as it was.
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
std::filesystem::path sidecar(const std::filesystem::path& p){return std::filesystem::path(p).concat(L".video-ai");}
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
veyra::pipeline::EnhanceGraphDesc describe(const EnhancementSettings& s,uint32_t width,uint32_t height,veyra::pipeline::ResolutionPlan* plan=nullptr){
    StageRequest request;request.sr=s.sr;request.width=width;request.height=height;
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
        for(uint32_t bad2:{0u,5u,20u,22u,24u}){auto bad=s;bad.dualSrQuality=bad2;check(!bad.validate().empty(),"second SR must be an RTX Video SR value");}
        s.videoSrQuality=4;s.dualSrQuality=17;check(s.validate().empty()&&s.activeVideoSettings().dualSrEnabled,"standard base with a high bitrate second pass");
        s.videoSrQuality=0;check(!s.activeVideoSettings().dualSrEnabled,"DLSS SR keeps its single pass");
        s.videoSrQuality=kVideoSrFsr;check(!s.activeVideoSettings().dualSrEnabled,"FSR keeps its single pass");
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
        s.videoSrQuality=0;check(!describe(s,1280,720).dualSr,"DLSS SR plans one pass");
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
    }

    std::cout<<"Video AI settings "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
