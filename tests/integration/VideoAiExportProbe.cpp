// Diagnostic entry into RTX Video SR (every family, single, double SR and
// supersample) and VFG through the actual graph + D3D12 NVENC + mux/verify. No
// SR, interpolation or encoding implementation here.
// Usage: input output [--sr QUALITY] [--target 1440|4k] [--sr-output source|1440|4k]
//        [--dual-quality Q --dual-target 1440|4k --dual-prescale 720|900|1080|1440]
//        [--vfg M [--vfg-quality Q]] [--frames N]
#include "veyra/engine/VideoExportJob.h"
#include <windows.h>
#include <timeapi.h>
#include <atomic>
#include <iostream>
#include <string>
#pragma comment(lib,"winmm.lib")
int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    using veyra::pipeline::SrTarget;using veyra::pipeline::SrPrescale;using veyra::pipeline::SrOutput;
    if(argc<3)return 2;
    EnhancementSettings s;unsigned frames=0;
    const auto target=[](const std::wstring& v){return v==L"1440"?SrTarget::Qhd:SrTarget::Uhd4K;};
    try{
        for(int i=3;i<argc;++i){
            const std::wstring flag=argv[i];
            if(i+1>=argc)return 2;
            const std::wstring value=argv[++i];
            if(flag==L"--sr"){s.sr=true;s.videoSrQuality=std::stoul(value);}
            else if(flag==L"--target")s.srTarget=target(value);
            else if(flag==L"--sr-output")s.srOutput=value==L"source"?SrOutput::Source:value==L"1440"?SrOutput::Qhd:SrOutput::Uhd4K;
            else if(flag==L"--dual-quality"){s.dualSrEnabled=true;s.dualSrQuality=std::stoul(value);}
            else if(flag==L"--dual-target"){s.dualSrEnabled=true;s.dualSrTarget=target(value);}
            else if(flag==L"--dual-prescale"){s.dualSrEnabled=true;s.srPrescale=value==L"720"?SrPrescale::P720:value==L"900"?SrPrescale::P900:value==L"1440"?SrPrescale::P1440:SrPrescale::P1080;}
            else if(flag==L"--vfg"){s.frameGenerationBackend=FrameGenerationBackend::Vfg;s.multiplier=std::stoul(value);}
            else if(flag==L"--vfg-quality")s.vfgQuality=std::stoul(value);
            else if(flag==L"--frames")frames=std::stoul(value);
            else return 2;
        }
    }catch(...){return 2;}
    if(s.dualSrEnabled&&s.srPrescale==SrPrescale::Off)s.srPrescale=SrPrescale::P1080;
    s.nrPolicy=veyra::pipeline::NrSizePolicy::Native;s.exportBitrateMbps=18;
    if(const auto error=s.validate();!error.empty()){std::cout<<"VIDEO_AI_PRODUCT invalid="<<error<<'\n';return 2;}
    // Serial FG exports poll the GPU fence with 1 ms sleeps; at the default timer
    // resolution each one is a 15.6 ms tick, which hides the backend's cost.
    timeBeginPeriod(1);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto options=PlayerOptions::from(s);options.exportRateControl=veyra::sink::ExportRateControl::Vbr;
    std::atomic<bool> cancel{false};ExportCounts last;
    const bool ok=exportVideo(argv[1],argv[2],options,true,cancel,
        [](double p,const std::wstring& msg){std::wcout<<int(p*100)<<L"% "<<msg<<L'\n';},frames,{},
        [&](const ExportCounts& c){last=c;});
    const unsigned m=s.multiplier;
    std::cout<<"VIDEO_AI_PRODUCT source="<<last.source<<" generated="<<last.generated<<" hold="<<last.holds<<" output="<<last.encoded
             <<" multiplier="<<m<<" sr="<<(s.sr?int(s.videoSrQuality):-1)<<" dual="<<(s.dualSrEnabled?int(s.dualSrQuality):-1)
             <<" backend="<<frameGenerationBackendName(s.frameGenerationBackend)<<" ok="<<ok<<'\n';
    CoUninitialize();timeEndPeriod(1);
    return ok&&last.source>1&&(m<=1||last.generated>0)&&last.encoded==last.source*m?0:1;
}
