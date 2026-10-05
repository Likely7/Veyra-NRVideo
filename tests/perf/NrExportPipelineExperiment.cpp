// Directed entry into the actual export graph, NVENC and mux. No encoder implementation here.
#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/Log.h"
#include "veyra/gfx/GpuSchedulingPriority.h"
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <objbase.h>

int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc<6||argc>7)return 2;
    const std::wstring group=argv[3];const unsigned frames=std::stoul(argv[4]);
    if(!frames||frames>300)return 2;
    const std::filesystem::path logs=argv[5];std::filesystem::create_directories(logs);
    Logger::instance().setConsoleEnabled(false);Logger::instance().openFile((logs/L"engine.log").wstring());
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    log::info("export-pipeline-test",std::format("COM init hr=0x{:X}",unsigned(com)));if(FAILED(com))return 2;
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    engine::EnhancementSettings settings;settings.exportBitrateMbps=30;
    settings.nrPolicy=pipeline::NrSizePolicy::Native;
    if(group==L"nr"||group==L"nr2"||group==L"nr2-auto"||group==L"nrsr4k")settings.nr=true;
    if(group==L"sr4k"||group==L"sr8k"||group==L"nrsr4k"){
        settings.sr=true;settings.videoSrQuality=0;
        settings.srTarget=group==L"sr8k"?pipeline::SrTarget::Uhd8K:pipeline::SrTarget::Uhd4K;
    }
    if(group==L"nr2"||group==L"nr2-auto"){
        engine::EffectChain chain;chain.nodeCount=2;
        for(unsigned i=0;i<2;++i){chain.nodes[i].enabled=true;chain.nodes[i].type=engine::EffectType::NrEnhance;chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Native;}
        if(group==L"nr2-auto")chain.nodes[0].nr.sizePolicy=pipeline::NrSizePolicy::Auto;
        engine::fromChain(chain,settings);
    }
    if(group==L"fg2")settings.multiplier=2;
    if(group!=L"none"&&group!=L"nr"&&group!=L"nr2"&&group!=L"nr2-auto"&&group!=L"sr4k"&&group!=L"nrsr4k"&&group!=L"sr8k"&&group!=L"fg2")return 2;
    settings.nrPolicy=pipeline::NrSizePolicy::Native;settings.exportBitrateMbps=30;
    auto options=engine::PlayerOptions::from(settings);options.exportRateControl=sink::ExportRateControl::Vbr;
    const bool cancelTest=argc==7&&std::wstring(argv[6])==L"--cancel";
    if(argc==7&&!cancelTest)return 2;
    std::atomic<bool> cancel{false};engine::ExportCounts last;bool priorityApplied=false,priorityPass=true;
    const auto start=std::chrono::steady_clock::now();
    const bool ok=engine::exportVideo(argv[1],argv[2],options,true,cancel,[](double,const std::wstring&){},frames,
        [&](){if(!priorityApplied){gfx::requestGpuPriority(gfx::GpuPriority::Normal);const auto p=gfx::gpuPriorityStatus();priorityPass=p.state==gfx::GpuPriorityState::Applied&&p.actual==2;priorityApplied=true;}return priorityPass;},
        [&](const engine::ExportCounts& c){last=c;if(cancelTest&&c.source>=2)cancel=true;});
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    Logger::instance().flush();
    std::cout<<"EXPORT_PIPELINE_RESULT ok="<<ok<<" cancelled="<<cancel.load()<<" source="<<last.source<<" generated="<<last.generated
        <<" hold="<<last.holds<<" encoded="<<last.encoded<<" totalMs="<<elapsed<<std::endl;
    if(cancelTest)return !ok&&cancel&&!std::filesystem::exists(argv[2])?0:1;
    return ok&&last.source==frames&&last.encoded==last.source*(group==L"fg2"?2:1)?0:1;
}
