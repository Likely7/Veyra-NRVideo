// SPDX-License-Identifier: GPL-3.0-only
// RX7000 worker HIP device selection (Rdna3HipDevices.h). Unit checks always;
// with --hip <amdhip64 dll> also the real HIP library: the probe, its LUIDs
// against DXGI, and HIP_VISIBLE_DEVICES set in-process before the library loads.
// No NR inference: this machine needs no RX7000 for these.
#include "veyra/pipeline/Rdna3HipDevices.h"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <iostream>
#include <set>

namespace h=veyra::pipeline::rdna3::hip;
using Microsoft::WRL::ComPtr;

// Runs a child of this executable and returns what it printed.
std::string capture(std::wstring command){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&sa,65536))return {};
    SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=write;startup.hStdError=write;
    PROCESS_INFORMATION process{};
    const bool created=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
    CloseHandle(write);
    std::string output;
    if(created){
        if(WaitForSingleObject(process.hProcess,20000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);
        char buffer[4096];DWORD n=0;
        while(ReadFile(read,buffer,sizeof(buffer),&n,nullptr)&&n)output.append(buffer,n);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
    CloseHandle(read);return output;
}

int wmain(int argc,wchar_t** argv){
    // Children: the probe itself, and the probe after the worker's in-process visibility step.
    if(argc==3&&std::wstring(argv[1])==L"--hip-probe")return h::probeMain(argv[2]);
    if(argc==4&&std::wstring(argv[1])==L"--visible-probe")return h::showOnly(_wtoi(argv[2]))?h::probeMain(argv[3]):9;
    const bool realHip=argc==3&&std::wstring(argv[1])==L"--hip";
    if(argc!=1&&!realHip)return 2;
    wchar_t self[MAX_PATH]{};GetModuleFileNameW(nullptr,self,MAX_PATH);
    unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};

    {
        std::vector<char> props(h::kPropertiesBytes,0);
        strcpy_s(props.data(),h::kNameBytes,"AMD Radeon RX 7900 XTX");
        memcpy(props.data()+256,"gfx9999-in-uuid",15); // UUID bytes that look like an arch are not the arch
        const unsigned char luid[8]={0xE2,0x00,0x01,0x00,0,0,0,0};memcpy(props.data()+h::kLuidOffset,luid,sizeof(luid));
        strcpy_s(props.data()+1200,64,"gfx1100");
        const auto d=h::parseProperties(1,props.data(),props.size());
        check(d.index==1&&d.name=="AMD Radeon RX 7900 XTX"&&d.luid==0x100E2ull&&d.arch=="gfx1100","properties: name, LUID at offset 272, arch after the LUID");
        const auto shortBuffer=h::parseProperties(0,props.data(),100);
        check(shortBuffer.luid==0&&shortBuffer.name.empty(),"properties: a short buffer yields nothing");
    }
    {
        std::vector<h::Device> devices{{0,0x12089,"gfx1036","AMD Radeon(TM) Graphics"},{1,0x100E2,"gfx1100","AMD Radeon RX 7900 XTX"}};
        check(h::indexForLuid(devices,0x100E2)==1,"selected LUID finds the dGPU behind an iGPU at HIP index 0");
        check(!h::indexForLuid(devices,0xDEAD),"an unknown LUID selects nothing");
        devices.push_back({2,0x100E2,"gfx1100","duplicate"});
        check(!h::indexForLuid(devices,0x100E2),"an ambiguous LUID selects nothing");
    }
    {
        const h::Device d{3,0x00000001000100E2ull,"","AMD Radeon(TM) Graphics (iGPU)"};h::Device back;
        check(h::parseDevice(h::formatDevice(d),back)&&back.index==3&&back.luid==d.luid&&back.arch.empty()&&back.name==d.name,
            "device line round trip with spaces in the name and an unknown arch");
        check(!h::parseDevice("hip-device index=x",back),"a malformed device line is refused");
        h::ProbeResult r;
        const std::string two=h::formatDevice({0,0x12089,"gfx1036","iGPU"})+h::formatDevice({1,0x100E2,"gfx1100","dGPU"})+"noise\nhip-devices count=2 status=ok\n";
        check(h::parseProbe(two,r)&&r.devices.size()==2&&r.devices[1].arch=="gfx1100","probe output is read back");
        check(!h::parseProbe(h::formatDevice({0,1,"gfx1100","a"})+"hip-devices count=2 status=ok\n",r),"a count that disagrees with the device lines is refused");
        check(!h::parseProbe("hip-devices count=0 status=hip-error-100\n",r)&&r.status=="hip-error-100","a HIP error is reported, not accepted");
        check(!h::parseProbe(h::formatDevice({0,1,"gfx1100","a"}),r),"output without the summary line is refused");
    }
    {
        for(const wchar_t* name:{L"HIP_VISIBLE_DEVICES",L"CUDA_VISIBLE_DEVICES",L"GPU_DEVICE_ORDINAL"}){SetEnvironmentVariableW(name,nullptr);_wputenv_s(name,L"");}
        check(h::outsideVisibility().empty(),"no visibility variable from outside");
        SetEnvironmentVariableW(L"CUDA_VISIBLE_DEVICES",L"0");
        check(h::outsideVisibility()==L"CUDA_VISIBLE_DEVICES=0","a visibility variable from outside is reported and kept");
        SetEnvironmentVariableW(L"CUDA_VISIBLE_DEVICES",nullptr);
        wchar_t block[16]{},crt[16]{};size_t length=0;
        check(h::showOnly(1)&&GetEnvironmentVariableW(L"HIP_VISIBLE_DEVICES",block,16)==1&&std::wstring(block)==L"1"&&
              _wgetenv_s(&length,crt,16,L"HIP_VISIBLE_DEVICES")==0&&std::wstring(crt)==L"1","showOnly sets the process block and the CRT copy");
        SetEnvironmentVariableW(L"HIP_VISIBLE_DEVICES",nullptr);_wputenv_s(L"HIP_VISIBLE_DEVICES",L"");
    }
    {
        h::ProbeResult r;std::string detail;
        check(!h::runProbe(self,L"C:\\nonexistent-veyra-test\\amdhip64_7.dll",r,detail)&&detail.find("load-failed")!=std::string::npos,
            "a probe that cannot load HIP fails with its reason (the worker then keeps every device visible)");
    }
    if(realHip){
        h::ProbeResult r;std::string detail;
        const bool probed=h::runProbe(self,argv[2],r,detail);
        check(probed&&!r.devices.empty(),"the real HIP library is probed in a child process");
        std::set<uint64_t> dxgi;ComPtr<IDXGIFactory1> factory;
        if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            for(UINT i=0;;++i){
                ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
                DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);dxgi.insert(uint64_t(uint32_t(desc.AdapterLuid.HighPart))<<32|desc.AdapterLuid.LowPart);
            }
        bool matched=probed&&!r.devices.empty();
        for(const auto& d:r.devices){std::cout<<"  "<<h::formatDevice(d);matched=matched&&dxgi.count(d.luid)&&d.arch.rfind("gfx",0)==0;}
        check(matched,"every HIP LUID is a DXGI adapter LUID and every arch is gfx*");
        if(probed&&!r.devices.empty()){
            const auto& first=r.devices.front();h::ProbeResult one,none;
            check(h::parseProbe(capture(L"\""+std::wstring(self)+L"\" --visible-probe "+std::to_wstring(first.index)+L" \""+argv[2]+L"\""),one)&&
                  one.devices.size()==1&&one.devices[0].luid==first.luid,"HIP_VISIBLE_DEVICES set in-process before the load keeps exactly the chosen device");
            h::parseProbe(capture(L"\""+std::wstring(self)+L"\" --visible-probe "+std::to_wstring(int(r.devices.size())+3)+L" \""+argv[2]+L"\""),none);
            check(none.devices.empty()&&none.count==0,"an index past the last device leaves HIP none (the variable is honoured)");
        }
    }
    std::cout<<"checks="<<checks<<" failures="<<failures<<(realHip?" (real HIP library: listing and visibility only, no NR inference)":" (unit only)")<<"\n";
    return failures?1:0;
}
