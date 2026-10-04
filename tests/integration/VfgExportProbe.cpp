// Diagnostic entry into the actual graph + D3D12 NVENC + mux/verify path.
// This file contains no interpolation or encoding implementation.
#include "veyra/engine/VideoExportJob.h"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    if(argc<5)return 2;const unsigned m=std::stoul(argv[3]),quality=std::stoul(argv[4]);
    if(m<2||m>8||quality>2)return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    EnhancementSettings s;s.frameGenerationBackend=FrameGenerationBackend::Vfg;s.multiplier=m;s.vfgQuality=quality;
    bool cancelTest=false;
    for(int i=5;i<argc;++i){const std::wstring flag=argv[i];
        if(flag==L"--nr")s.nr=true;
        else if(flag==L"--sr"){s.sr=true;s.videoSrQuality=4;s.srTarget=veyra::pipeline::SrTarget::Uhd4K;}
        else if(flag==L"--cancel")cancelTest=true;
        else return 2;
    }
    s.nrPolicy=veyra::pipeline::NrSizePolicy::Native;s.exportBitrateMbps=18;
    auto options=PlayerOptions::from(s);options.exportRateControl=veyra::sink::ExportRateControl::Vbr;
    std::atomic<bool> cancel{false};ExportCounts last;
    const bool ok=exportVideo(argv[1],argv[2],options,true,cancel,
        [](double p,const std::wstring& msg){std::wcout<<int(p*100)<<L"% "<<msg<<L'\n';},0,{},
        [&](const ExportCounts& c){last=c;if(cancelTest&&c.source>=2)cancel=true;});
    std::cout<<"VFG_PRODUCT source="<<last.source<<" generated="<<last.generated<<" hold="<<last.holds<<" output="<<last.encoded<<" multiplier="<<m<<" quality="<<quality<<" ok="<<ok<<" cancelled="<<cancel.load()<<'\n';
    CoUninitialize();
    if(cancelTest)return !ok&&cancel&&!std::filesystem::exists(argv[2])?0:1;
    return ok&&last.source>1&&last.generated>0&&last.encoded==last.source*m?0:1;
}
