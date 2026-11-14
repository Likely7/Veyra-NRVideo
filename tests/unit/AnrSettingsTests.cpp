#include "veyra/engine/PresetStore.h"
#include "veyra/engine/PresetLibrary.h"
#include "veyra/engine/FrameGenerationSelection.h"
#include "veyra/engine/ExportFeaturePolicy.h"
#include <filesystem>
#include <iostream>
#include <memory>
using namespace veyra::engine;
int wmain(int argc,wchar_t** argv){
    if(argc!=2||!std::filesystem::path(argv[1]).is_absolute())return 2;
    const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cout<<"FAIL "<<label<<'\n';}};
    const FrameGenerationAvailability available{true,true,true,true,true,true};
    check(restoredFrameGenerationBackend(FrameGenerationBackend::Anr,available)==FrameGenerationBackend::Anr,"manual ANR survives restore when installed");
    check(restoredFrameGenerationBackend(FrameGenerationBackend::Anr,{true,false,false,false,false,false})==FrameGenerationBackend::Dlss,"missing ANR returns to available provider");
    check(defaultFrameGenerationBackend(0x10de)==FrameGenerationBackend::Dlss&&defaultFrameGenerationBackend(0x1002)==FrameGenerationBackend::Fsr,"ANR never replaces a fresh profile default");
    check(crossVendorFrameGeneration(FrameGenerationBackend::Anr)&&!presentSinkFrameGeneration(FrameGenerationBackend::Anr),"ANR has independent admission and application-owned outputs");
    check(exportFrameGenerationSupported(FrameGenerationBackend::Anr,false)&&exportFrameGenerationSupported(FrameGenerationBackend::Anr,true),"ANR export does not require NVIDIA NGX core");
    check(motionUsesFlow(MotionSource::Automatic,FrameGenerationBackend::Anr),"ANR consumes estimated external motion");
    for(unsigned multiplier=2;multiplier<=6;++multiplier){
        EnhancementSettings s;s.frameGenerationBackend=FrameGenerationBackend::Anr;s.multiplier=multiplier;
        check(s.validate().empty(),"every ANR 2X through 6X including 5X validates");
        const auto name=L"anr-"+std::to_wstring(multiplier);
        PresetStore writer(root/(name+L".preset"));check(writer.load()&&writer.put(name,s),"ANR preset writes");
        PresetStore reader(root/(name+L".preset"));check(reader.load()&&reader.entries().size()==1&&reader.entries()[0].settings.frameGenerationBackend==FrameGenerationBackend::Anr&&reader.entries()[0].settings.multiplier==multiplier,"ANR preset restarts without migration to DLSS");
        auto entry=std::make_unique<PresetEntry>();entry->name=name;entry->chain=toChain(s);entry->fg={multiplier,FrameGenerationBackend::Anr,1};entry->globals=ChainGlobalSettings::capture(s);entry->contents=kPresetAllContent;
        EnhancementSettings restored;PresetLibrary::apply(*entry,restored);check(restored.frameGenerationBackend==FrameGenerationBackend::Anr&&restored.multiplier==multiplier,"ANR library applies both provider and multiplier");
        auto session=std::make_unique<ChainSession>(ChainSession::initial(s));check(session->select(ChainMode::Node),"ANR node mode starts");
        auto editor=std::make_shared<NodeEditorDocument>();editor->nodes=session->configurations[1].chain;editor->globals=ChainGlobalSettings::capture(s);check(editor->layout.initialize(editor->nodes).accepted,"ANR node layout initializes");session->configurations[1].editor=editor;
        ChainSessionStore store(root/(name+L".session"));check(store.save(*session),"ANR dual-mode session saves");
        auto reloaded=std::make_unique<ChainSession>();check(store.load(*reloaded)&&*reloaded==*session,"ANR list and node configuration survive disk restart");
    }
    EnhancementSettings bad;bad.frameGenerationBackend=FrameGenerationBackend::Anr;bad.multiplier=7;check(!bad.validate().empty(),"ANR rejects multiplier above six");bad.multiplier=2;bad.frameGenerationBackend=static_cast<FrameGenerationBackend>(6);check(!bad.validate().empty(),"unknown backend remains rejected");
    std::cout<<"ANR_SETTINGS checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
