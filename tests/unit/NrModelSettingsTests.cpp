#include "veyra/engine/EffectChain.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/PresetLibrary.h"
#include "veyra/engine/PresetStore.h"
#include "veyra/engine/ExportFeaturePolicy.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace veyra;
using namespace veyra::engine;
namespace {
int failures=0;
void check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    EnhancementSettings s;s.nr=true;
    check(s.nrRuntime==NrRuntime::Ampere&&s.model.performance&&nrRenderPreset(s.nrRuntime,s.model)==2,"fresh NVIDIA settings select SF preset B");
    auto a=s;a.model.performance=false;
    check(nrRenderPreset(a.nrRuntime,a.model)==1&&requiresGraphRebuild(s,a),"active SF model change rebuilds NR");
    for(auto runtime:{NrRuntime::NvidiaOriginal}){
        auto original=s;original.nrRuntime=runtime;auto edited=original;edited.model.performance=false;
        check(nrRenderPreset(runtime,edited.model)==0&&!requiresGraphRebuild(original,edited)&&
            original.sameActiveVideoParameters(edited),"NVIDIA original ignores the inactive performance option");
    }
    auto amd=s;amd.nrRuntime=NrRuntime::LmxxfAmd;
    auto amdOriginal=amd;amdOriginal.model.performance=false;
    check(nrRenderPreset(amd.nrRuntime,amd.model)==0&&requiresGraphRebuild(amd,amdOriginal)&&
        !amd.sameActiveVideoParameters(amdOriginal),"AMD model changes rebuild without setting an NGX preset");
    amd.nr=false;amdOriginal.nr=false;
    check(!requiresGraphRebuild(amd,amdOriginal)&&amd.sameActiveVideoParameters(amdOriginal),"disabled AMD edits preserve uninterrupted playback");
    auto off=s;off.nr=false;auto draft=off;draft.model.performance=false;
    check(!requiresGraphRebuild(off,draft)&&off.sameActiveVideoParameters(draft),"disabled NR model edit does not disturb playback");
    s.nrLayerCount=2;for(unsigned i=0;i<s.nrLayerCount;++i){s.nrLayers[i].enabled=true;s.nrLayers[i].runtime=s.nrRuntime;}
    a=s;a.nrLayers[1].model.performance=false;
    check(requiresGraphRebuild(s,a),"second active NR layer model change rebuilds");
    s.nrLayers[1].enabled=a.nrLayers[1].enabled=false;
    check(!requiresGraphRebuild(s,a)&&s.sameActiveVideoParameters(a),"bypassed layer model is saved without a rebuild");
    s.nrLayers[1].enabled=a.nrLayers[1].enabled=true;
    const auto frozen=freezeExportSettings(a);EnhancementSettings round=frozen;fromChain(toChain(frozen),round);
    check(round==frozen&&!round.nrLayers[1].model.performance,"export snapshot retains each layer's model");
    auto mixedAmd=a;mixedAmd.nrRuntime=NrRuntime::LmxxfAmd;
    for(auto& layer:mixedAmd.nrLayers)layer.runtime=NrRuntime::LmxxfAmd;
    EnhancementSettings amdRound;fromChain(toChain(freezeExportSettings(mixedAmd)),amdRound);
    check(amdRound.nrLayers[0].model.performance&&!amdRound.nrLayers[1].model.performance&&
        requiresGraphRebuild(mixedAmd,[&]{auto v=mixedAmd;v.nrLayers[1].model.performance=true;return v;}()),"AMD list/export preserves independent Small and Original layers");
    StageRequest request;request.width=1920;request.height=1080;request.nr=true;request.exportJob=true;
    pipeline::EnhanceGraphDesc desc;describeStages(request,frozen,desc);
    check(desc.nrLayersModel.size()==2&&desc.nrLayersModel[0].performance&&!desc.nrLayersModel[1].performance,"export graph receives both model choices");
    const auto storePath=out/L"legacy-store.v1";PresetStore legacy(storePath);
    check(legacy.load()&&legacy.put(L"mixed",a)&&legacy.setDefault(0),"legacy preset store accepts mixed models");
    PresetStore legacyRead(storePath);
    check(bytes(storePath).starts_with("VEYRA_PRESETS 32")&&legacyRead.load()&&legacyRead.defaultSettings()==a,"legacy schema 32 round-trips all layers");
    for(auto old:{NrRuntime::Original,NrRuntime::Community,NrRuntime::Ampere,NrRuntime::NvidiaOriginal,NrRuntime::LmxxfAmd}){
        EnhancementSettings oldSettings;oldSettings.nr=true;oldSettings.nrRuntime=old;
        PresetEntry entry;entry.name=L"legacy runtime";entry.chain=toChain(oldSettings);
        const auto path=out/(L"migration-"+std::to_wstring(int(old))+L".v1");
        PresetLibrary writer(path);writer.setIncludeBuiltins(false);
        const bool saved=writer.load()&&writer.put(entry);
        PresetLibrary reader(path);reader.setIncludeBuiltins(false);
        check(saved&&reader.load()&&reader.entries().size()==1&&
            reader.entries()[0].chain.firstOf(EffectType::NrEnhance)->nr.runtime==currentNrRuntime(old)&&
            reader.entries()[0].chain.firstOf(EffectType::NrEnhance)->nr.model.performance,"old runtime selection migrates without losing original or AMD");
    }
    const auto libraryPath=out/L"library.v1";PresetLibrary library(libraryPath);library.setIncludeBuiltins(false);
    PresetEntry entry;entry.name=L"NR models";entry.chain=toChain(s);
    check(library.load()&&library.put(entry),"save default B library");const auto rollback=bytes(libraryPath);
    entry.chain=toChain(a);
    check(library.put(entry,true)&&bytes(libraryPath)==rollback,"old-model opt-in preserves the earlier library file");
    PresetLibrary reloaded(libraryPath);reloaded.setIncludeBuiltins(false);
    check(reloaded.load()&&reloaded.entries().size()==1&&reloaded.entries()[0]==entry,"library schema 13 restores mixed A/B models");
    entry.chain=toChain(s);check(library.put(entry,true),"return to B in the selected sidecar");
    PresetLibrary again(libraryPath);again.setIncludeBuiltins(false);
    check(again.load()&&again.entries()[0]==entry,"return to B cannot resurrect a stale A choice");
    const auto sessionPath=out/L"session.v1";auto session=std::make_unique<ChainSession>(ChainSession::initial(s));
    check(session->select(ChainMode::Node),"initialize separate list and node configurations");ChainSessionStore sessions(sessionPath);
    check(sessions.save(*session),"save B session");const auto sessionRollback=bytes(sessionPath);
    auto& node=session->configurations[1];
    auto document=std::make_shared<NodeEditorDocument>();document->nodes=node.chain;
    check(document->layout.initialize(document->nodes).accepted,"initialize node editor layout");
    document->nodes.firstOf(EffectType::NrEnhance)->nr.model.performance=false;
    node.editor=document;
    check(sessions.save(*session)&&bytes(sessionPath)==sessionRollback,"detached editor model opts into session 8 and preserves old file");
    auto restored=std::make_unique<ChainSession>();ChainSessionStore reader(sessionPath);
    check(reader.load(*restored)&&*restored==*session,"session restores list, node and editor model choices independently");
    PresetLibrary amdLibrary(out/L"amd-library.v1");amdLibrary.setIncludeBuiltins(false);
    PresetEntry amdEntry;amdEntry.name=L"AMD Small + Original";amdEntry.chain=toChain(mixedAmd);
    const bool amdSaved=amdLibrary.load()&&amdLibrary.put(amdEntry);
    PresetLibrary amdReader(out/L"amd-library.v1");amdReader.setIncludeBuiltins(false);
    check(amdSaved&&amdReader.load()&&amdReader.entries().size()==1&&amdReader.entries()[0]==amdEntry,"AMD mixed models survive disk preset reload");
    auto amdSession=std::make_unique<ChainSession>(ChainSession::initial(mixedAmd));ChainSessionStore amdSessions(out/L"amd-session.v1");
    auto amdRestored=std::make_unique<ChainSession>();
    check(amdSessions.save(*amdSession)&&amdSessions.load(*amdRestored)&&*amdSession==*amdRestored,"AMD mixed models survive complete session restore");
    {
        using pipeline::Extent;using pipeline::NrSizePolicy;using pipeline::ResolutionPlan;
        check(int(NrSizePolicy::Auto)==6&&int(NrSizePolicy::Realtime)==0&&int(NrSizePolicy::Native)==1,"manual sizes preserve every existing preset ID");
        bool allSizes=true;
        for(unsigned percent=25;percent<=100;++percent){
            const auto policy=pipeline::nrSizePolicyForPercent(percent);
            const auto plan=ResolutionPlan::make({3840,2160},false,policy,false);
            const Extent expected{percent==100?3840u:(3840u*percent/100)&~1u,percent==100?2160u:(2160u*percent/100)&~1u};
            allSizes&=pipeline::validNrSizePolicy(policy)&&pipeline::nrSizePercent(policy)==percent&&plan.nr==expected&&plan.output==Extent{3840,2160};
            allSizes&=ResolutionPlan::make({3840,2160},false,policy,true).nr==Extent{3840,2160};
            allSizes&=ResolutionPlan::make({1,1},false,policy,false).nr==Extent{1,1};
        }
        check(allSizes,"all 76 percentages change NR dimensions only, keep offline native size and never make zero textures");
        check(!pipeline::validNrSizePolicy(pipeline::nrSizePolicyForPercent(24))&&!pipeline::validNrSizePolicy(pipeline::nrSizePolicyForPercent(101))&&
              !pipeline::validNrSizePolicy(NrSizePolicy(7))&&!pipeline::validNrSizePolicy(NrSizePolicy(124))&&!pipeline::validNrSizePolicy(NrSizePolicy(201)),"invalid manual percentages and reserved policy IDs fail closed");
        check(ResolutionPlan::make({1080,1920},false,pipeline::nrSizePolicyForPercent(50),false).nr==Extent{540,960},"portrait manual scale applies to width and height");
        auto manual=s;manual.nrLayers[0].sizePolicy=pipeline::nrSizePolicyForPercent(50);manual.nrLayers[1].sizePolicy=pipeline::nrSizePolicyForPercent(75);
        EnhancementSettings mr=manual;fromChain(toChain(manual),mr);
        check(mr.nrLayers[0].sizePolicy==manual.nrLayers[0].sizePolicy&&mr.nrLayers[1].sizePolicy==manual.nrLayers[1].sizePolicy,"two manual layers retain independent settings through the shared chain");
        StageRequest preview;preview.width=3840;preview.height=2160;preview.nr=true;pipeline::EnhanceGraphDesc md;describeStages(preview,mr,md);
        check(md.nrLayersExtent==std::vector<Extent>{{1920,1080},{2880,1620}}&&md.workWidth==3840&&md.workHeight==2160&&md.validateFixedExecutionPlan().empty(),"compiled list graph runs independent percentages and retains full output");
        preview.sr=true;preview.width=1920;preview.height=1080;mr.lowLatency=true;
        for(auto& n:mr.nrLayers)n.lowLatencyPairing=true;
        describeStages(preview,mr,md);
        check(md.nrBeforeSr&&md.nrLayersExtent==std::vector<Extent>{{960,540},{1440,810}}&&md.workWidth==3840,"NR-before-SR percentages use the actual source-sized input");
        preview.sr=false;preview.width=3840;preview.height=2160;preview.amdNr=true;preview.nvidiaAdapter=false;mr.nrRuntime=NrRuntime::LmxxfAmd;
        for(auto& n:mr.nrLayers)n.runtime=NrRuntime::LmxxfAmd;
        describeStages(preview,mr,md);
        check(md.nrLayersExtent==std::vector<Extent>{{960,540},{1440,810}}&&md.validateFixedExecutionPlan().empty(),"AMD manual preview percentages stay within its existing model budget");
        preview.exportJob=true;describeStages(preview,mr,md);
        check(md.nrLayersExtent==std::vector<Extent>{{1920,1080},{1920,1080}}&&md.workWidth==3840,"AMD video export preserves its previous bounded model and full-size output");
        mr.nrRuntime=NrRuntime::Ampere;for(auto& n:mr.nrLayers)n.runtime=mr.nrRuntime;preview.amdNr=false;preview.nvidiaAdapter=true;describeStages(preview,mr,md);
        check(md.nrLayersExtent==std::vector<Extent>{{3840,2160},{3840,2160}},"NVIDIA export does not silently inherit preview downsampling");
        PresetLibrary ml(out/L"manual-library.v1");ml.setIncludeBuiltins(false);PresetEntry me;me.name=L"manual percentages";me.chain=toChain(manual);
        const bool saved=ml.load()&&ml.put(me);PresetLibrary mlr(out/L"manual-library.v1");mlr.setIncludeBuiltins(false);
        check(saved&&mlr.load()&&mlr.entries().size()==1&&mlr.entries()[0]==me,"manual percentages survive an actual disk preset reload");
        PresetStore ms(out/L"manual-flat.v1");const bool flatSaved=ms.load()&&ms.put(L"manual",manual)&&ms.setDefault(0);PresetStore msr(out/L"manual-flat.v1");
        check(flatSaved&&msr.load()&&msr.defaultSettings()==manual,"legacy flat store retains each manual layer without a POD schema change");
        auto session=std::make_unique<ChainSession>(ChainSession::initial(manual));session->select(ChainMode::Node);
        auto& config=session->configurations[1];auto doc=std::make_shared<NodeEditorDocument>();doc->nodes=config.chain;
        check(doc->layout.initialize(doc->nodes).accepted,"manual node document initializes");doc->nodes.firstOf(EffectType::NrEnhance)->nr.sizePolicy=pipeline::nrSizePolicyForPercent(37);config.editor=doc;
        ChainSessionStore sc(out/L"manual-session.v1");auto reload=std::make_unique<ChainSession>();
        check(sc.save(*session)&&sc.load(*reload)&&*reload==*session,"List, Nodes and detached node drafts retain independent percentages across disk session restore");
    }
    std::cout<<"NR model settings failures="<<failures<<std::endl;return failures?1:0;
}
