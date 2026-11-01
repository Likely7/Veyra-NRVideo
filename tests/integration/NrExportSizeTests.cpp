// Real process-isolated production export; no test NR or encoder implementation.
#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/ExportFeaturePolicy.h"
#include "veyra/Log.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using namespace veyra;using namespace veyra::engine;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(argc==3&&std::wstring(argv[1])==L"--export-worker"){
        const int result=runExportWorker(reinterpret_cast<HANDLE>(std::stoull(argv[2])));
        if(SUCCEEDED(com))CoUninitialize();return result;
    }
    if(argc!=6&&argc!=7)return 2; // input, output, policies, codec, max frames, optional flow
    Logger::instance().openFile((std::filesystem::path(argv[2]).wstring()+L".parent.log"));Logger::instance().setConsoleEnabled(false);
    EnhancementSettings settings;settings.nr=true;settings.sr=false;settings.multiplier=1;
    settings.nrRuntime=NrRuntime::Ampere;settings.nrTemporal=false;settings.nrMotion=MotionSource::Zero;
    if(argc==7){if(std::wstring(argv[6])!=L"flow")return 2;settings.nrMotion=MotionSource::OpticalFlow;}
    settings.exportBitrateMbps=24;
    std::wistringstream policies(argv[3]);std::wstring part;
    while(std::getline(policies,part,L',')){
        if(settings.nrLayerCount>=kMaxNrInstances)return 2;
        auto& layer=settings.nrLayers[settings.nrLayerCount++];layer.enabled=true;layer.runtime=NrRuntime::Ampere;
        layer.model.performance=true;layer.temporal=false;layer.sizePolicy=pipeline::NrSizePolicy(std::stoi(part));
        if(!pipeline::validNrSizePolicy(layer.sizePolicy))return 2;
    }
    if(!settings.nrLayerCount)return 2;
    settings.nrPolicy=settings.nrLayers[0].sizePolicy;
    const auto saved=settings;const auto expected=freezeExportSettings(settings);
    const unsigned frames=std::stoul(argv[5]);
    ExportJobManager job;
    if(!job.start(argv[1],argv[2],settings,std::wstring(argv[4])==L"hevc",frames))return 3;
    ExportJobSnapshot state;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do{
        state=job.poll();if(!state.active())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }while(std::chrono::steady_clock::now()<until);
    const bool ok=state.state==ExportState::Succeeded&&state.frozen==expected&&settings==saved&&
        state.sourceFrames==frames&&state.encoded==frames&&state.generated==0&&std::filesystem::exists(argv[2]);
    std::cout<<"NR_EXPORT_SIZE ok="<<ok<<" state="<<unsigned(state.state)<<" source="<<state.sourceFrames<<" encoded="<<state.encoded<<" generated="<<state.generated<<" frozenPolicy="<<int(state.frozen.nrPolicy)<<" frozenLayers="<<state.frozen.nrLayerCount<<std::endl;
    const auto path=std::filesystem::path(state.workerLog).u8string();std::cout<<"WORKER_LOG "<<std::string(path.begin(),path.end())<<std::endl;
    if(state.active())job.cancel();
    if(SUCCEEDED(com))CoUninitialize();return ok?0:1;
}
