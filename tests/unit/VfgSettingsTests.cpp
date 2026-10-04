#include "veyra/engine/PresetStore.h"
#include "veyra/engine/PresetLibrary.h"
#include "veyra/pipeline/FrameBatch.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
using namespace veyra::engine;
namespace {unsigned checks=0,failures=0;void check(bool ok,const char* why){++checks;if(!ok){++failures;std::cout<<"FAIL "<<why<<'\n';}}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}}
int wmain(int argc,wchar_t** argv){
    if(argc!=2||!std::filesystem::path(argv[1]).is_absolute())return 2;
    const std::filesystem::path root=argv[1];if(!std::filesystem::create_directories(root))return 2;
    check(!motionUsesFlow(MotionSource::OpticalFlow,FrameGenerationBackend::Vfg)&&
          !motionUsesFlow(MotionSource::Automatic,FrameGenerationBackend::Vfg),"VFG uses its own motion even after a DLSS flow preset");
    check(motionUsesFlow(MotionSource::OpticalFlow)&&motionUsesFlow(MotionSource::Automatic),"SR/NR external motion remains enabled");
    EnhancementSettings legacy;PresetStore old(root/L"legacy.txt");check(old.load()&&old.put(L"old",legacy),"write legacy default");
    PresetStore legacyReader(root/L"legacy.txt");check(legacyReader.load()&&legacyReader.entries()[0].settings.vfgQuality==1,"old presets default to Medium");
    const auto original=bytes(root/L"legacy.txt");
    PresetStore store(root/L"settings.txt");check(store.load(),"new store");
    for(unsigned quality=0;quality<3;++quality)for(unsigned m=2;m<=8;++m){
        EnhancementSettings s;s.frameGenerationBackend=FrameGenerationBackend::Vfg;s.multiplier=m;s.vfgQuality=quality;
        check(s.validate().empty(),"VFG legal multiplier/quality");
        const auto name=std::to_wstring(m)+L"x-q"+std::to_wstring(quality);
        check(store.put(name,s),"legacy settings store writes VFG");
        const auto path=root/(name+L".preset");PresetLibrary library(path);library.setIncludeBuiltins(false);
        auto e=std::make_unique<PresetEntry>();e->name=name;e->chain=toChain(s);e->fg={m,FrameGenerationBackend::Vfg,quality};e->globals=ChainGlobalSettings::capture(s);
        check(library.load()&&library.put(*e),"library saves VFG");
        PresetLibrary reloaded(path);reloaded.setIncludeBuiltins(false);
        check(reloaded.load()&&reloaded.entries().size()==1&&reloaded.entries()[0]==*e,"library restores full VFG state");
        auto applied=legacy;PresetLibrary::apply(*e,applied);
        check(applied.multiplier==m&&applied.frameGenerationBackend==FrameGenerationBackend::Vfg&&applied.vfgQuality==quality,"preset applies multiplier backend quality");
        e->contents=presetContentMask(PresetContent::Color);applied=s;applied.vfgQuality=(quality+1)%3;
        const auto kept=applied.vfgQuality;PresetLibrary::apply(*e,applied);
        check(applied.frameGenerationBackend==FrameGenerationBackend::Vfg&&applied.multiplier==m&&applied.vfgQuality==kept,"partial color preset preserves VFG");
        auto session=std::make_unique<ChainSession>(ChainSession::initial(s));check(session->select(ChainMode::Node),"VFG node mode initializes");
        auto editor=std::make_shared<NodeEditorDocument>();editor->nodes=session->configurations[1].chain;
        editor->globals=ChainGlobalSettings::capture(s);check(editor->layout.initialize(editor->nodes).accepted,"VFG node draft layout");
        session->configurations[1].editor=editor;check(session->valid(),"VFG session valid");
        auto restored=std::make_unique<ChainSession>();
        const auto sessionPath=root/(name+L".session");ChainSessionStore sessionWriter(sessionPath);
        check(sessionWriter.save(*session),"session atomic save");ChainSessionStore sessionReader(sessionPath);
        check(sessionReader.load(*restored)&&*session==*restored,"session restart finds VFG suffix");
        auto bad=s;bad.multiplier=9;check(!bad.validate().empty(),"reject VFG 9X");bad=s;bad.vfgQuality=3;check(!bad.validate().empty(),"reject unknown quality");
        bad=s;bad.frameGenerationBackend=FrameGenerationBackend::Dlss;if(m>6)check(!bad.validate().empty(),"DLSS ceiling remains 6X");
        veyra::pipeline::FrameBatch batch;batch.identity={1,2,3};batch.a100ns=10000000;batch.b100ns=10333333;
        for(unsigned j=1;j<m;++j){veyra::pipeline::BatchFrame f;f.identity=batch.identity;f.subframe=j;f.pts100ns=batch.interpolate(batch.a100ns,batch.b100ns,j,m);f.kind=veyra::pipeline::FrameKind::Generated;batch.append(f);}
        veyra::pipeline::BatchFrame real;real.identity=batch.identity;real.pts100ns=batch.b100ns;batch.append(real);
        check(batch.count==m&&batch.frames[m-1].pts100ns==batch.b100ns,"2..8 ordered PTS with every source frame");
        if(m==8){bool rejected=false;try{real.pts100ns++;batch.append(real);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&batch.count==8,"8X batch rejects ninth frame");}
    }
    PresetStore restoredStore(root/L"settings.txt");check(restoredStore.load()&&restoredStore.entries().size()==21,"all VFG settings restored");
    for(const auto& e:restoredStore.entries())check(e.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&e.settings.vfgQuality<3&&e.settings.multiplier>=2&&e.settings.multiplier<=8,"stored VFG values legal");
    check(bytes(root/L"legacy.txt")==original,"legacy file unchanged");
    std::cout<<"VFG settings "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
