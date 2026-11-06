// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// HIP device selection for the RX7000 worker.
//
// ZLUDA and its NVAPI stand-in take HIP device 0 in several places: the adapter
// LUID NGX checks, and the stream a context zero-fills every new allocation on
// (created before the context switches HIP to its own device). On a machine
// with a Ryzen iGPU, HIP device 0 is often the iGPU, not the selected card.
// Field 2026-11-03 (RX 7900 XTX + gfx1036): NGX first refused the architecture,
// then, with the LUID repaired, the worker died at the 108.6 MB weight heap.
//
// The worker therefore hides every HIP device but the selected adapter with
// HIP_VISIBLE_DEVICES before it loads HIP. The HIP library reads its
// environment when it loads, so the index comes from a separate probe process
// (this executable, --hip-probe) that lists the devices and exits.
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace veyra::pipeline::rdna3::hip {
struct Device { int index=-1; uint64_t luid=0; std::string arch; std::string name; };

// hipDeviceProp_tR0600 begins with char name[256], hipUUID uuid (16 bytes) and
// char luid[8] (Windows only). gcnArchName lies further in and moves between
// releases, so it is found by its prefix, as the network layer does.
inline constexpr size_t kPropertiesBytes=16384,kNameBytes=256,kLuidOffset=272;

inline Device parseProperties(int index,const char* props,size_t size){
    Device d;d.index=index;
    if(!props||size<kLuidOffset+8)return d;
    d.name.assign(props,strnlen(props,kNameBytes));
    memcpy(&d.luid,props+kLuidOffset,sizeof(d.luid));
    for(size_t k=kLuidOffset+8;k+4<size;++k){
        if(memcmp(props+k,"gfx",3)!=0||props[k+3]<'0'||props[k+3]>'9')continue;
        size_t end=k;
        while(end<size&&(isalnum(static_cast<unsigned char>(props[end]))||props[end]=='-'||props[end]==':'||props[end]=='+'))++end;
        d.arch.assign(props+k,end-k);break;
    }
    return d;
}

// The index of the one device with this LUID; none when absent or ambiguous.
inline std::optional<int> indexForLuid(const std::vector<Device>& devices,uint64_t luid){
    std::optional<int> found;
    for(const auto& d:devices){
        if(d.luid!=luid)continue;
        if(found)return std::nullopt;
        found=d.index;
    }
    return found;
}

// The probe's output: one line per device, then a summary line.
inline std::string formatDevice(const Device& d){
    char line[512]{};
    _snprintf_s(line,sizeof(line),_TRUNCATE,"hip-device index=%d luid=0x%016llX arch=%s name=%s\n",
        d.index,static_cast<unsigned long long>(d.luid),d.arch.empty()?"unknown":d.arch.c_str(),d.name.c_str());
    return line;
}
inline bool parseDevice(const std::string& line,Device& d){
    int index=-1,consumed=0;unsigned long long luid=0;char arch[64]{};
    if(sscanf_s(line.c_str(),"hip-device index=%d luid=0x%llX arch=%63s name=%n",&index,&luid,arch,unsigned(sizeof(arch)),&consumed)!=3||
       index<0||consumed<=0)return false;
    d.index=index;d.luid=luid;d.arch=strcmp(arch,"unknown")==0?"":arch;d.name=line.substr(size_t(consumed));
    while(!d.name.empty()&&(d.name.back()=='\n'||d.name.back()=='\r'))d.name.pop_back();
    return true;
}
struct ProbeResult { std::vector<Device> devices; int count=-1; std::string status; };
// False unless the summary is present, says ok and matches the device lines.
inline bool parseProbe(const std::string& output,ProbeResult& result){
    result={};size_t start=0;
    while(start<output.size()){
        const size_t end=std::min(output.find('\n',start),output.size());
        const std::string line=output.substr(start,end-start);start=end+1;
        Device d;
        if(parseDevice(line,d)){result.devices.push_back(d);continue;}
        int count=-1;char status[64]{};
        if(sscanf_s(line.c_str(),"hip-devices count=%d status=%63s",&count,status,unsigned(sizeof(status)))==2){result.count=count;result.status=status;}
    }
    return result.status=="ok"&&result.count>=0&&size_t(result.count)==result.devices.size();
}

// Probe mode: load this HIP library, list its devices, nothing else.
inline int probeMain(const wchar_t* hipPath){
    const HMODULE library=LoadLibraryExW(hipPath,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library){printf("hip-devices count=0 status=load-failed-win32-%lu\n",GetLastError());return 2;}
    const auto count=reinterpret_cast<int(*)(int*)>(GetProcAddress(library,"hipGetDeviceCount"));
    const auto properties=reinterpret_cast<int(*)(void*,int)>(GetProcAddress(library,"hipGetDevicePropertiesR0600"));
    if(!count||!properties){printf("hip-devices count=0 status=missing-export\n");return 3;}
    int devices=0;const int status=count(&devices);
    if(status!=0){printf("hip-devices count=0 status=hip-error-%d\n",status);return 4;}
    std::vector<char> buffer(kPropertiesBytes);
    for(int i=0;i<devices;++i){
        std::fill(buffer.begin(),buffer.end(),0);
        const int result=properties(buffer.data(),i);
        if(result!=0){printf("hip-devices count=%d status=properties-error-%d-device-%d\n",devices,result,i);return 5;}
        fputs(formatDevice(parseProperties(i,buffer.data(),buffer.size())).c_str(),stdout);
    }
    printf("hip-devices count=%d status=ok\n",devices);fflush(stdout);
    return 0;
}

// Runs `executable --hip-probe hipPath` with only its own output pipe inherited
// (never the caller's protocol pipes) and reads the device list back.
inline bool runProbe(const std::wstring& executable,const std::wstring& hipPath,ProbeResult& result,std::string& detail,DWORD timeoutMs=15000){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&sa,65536)){detail="probe pipe failed win32="+std::to_string(GetLastError());return false;}
    SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> storage(bytes);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdOutput=write;startup.StartupInfo.hStdError=write;
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)||
       !UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&write,sizeof(write),nullptr,nullptr)){
        CloseHandle(read);CloseHandle(write);detail="probe handle allowlist failed";return false;
    }
    std::wstring command=L"\""+executable+L"\" --hip-probe \""+hipPath+L"\"";
    PROCESS_INFORMATION process{};
    const bool created=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,
        nullptr,nullptr,&startup.StartupInfo,&process)!=FALSE;
    const DWORD launchError=GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);CloseHandle(write);
    if(!created){CloseHandle(read);detail="probe launch failed win32="+std::to_string(launchError);return false;}
    // The output is a few hundred bytes: it fits the pipe, so waiting first cannot block the probe.
    const DWORD waited=WaitForSingleObject(process.hProcess,timeoutMs);
    if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);
    DWORD exitCode=0;GetExitCodeProcess(process.hProcess,&exitCode);
    std::string output;char buffer[4096];DWORD n=0;
    while(waited==WAIT_OBJECT_0&&ReadFile(read,buffer,sizeof(buffer),&n,nullptr)&&n)output.append(buffer,n);
    CloseHandle(read);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(waited!=WAIT_OBJECT_0){detail="probe did not finish within "+std::to_string(timeoutMs)+" ms";return false;}
    const bool parsed=parseProbe(output,result);
    if(!parsed||exitCode){
        detail="probe exit="+std::to_string(exitCode)+" status="+(result.status.empty()?"none":result.status);
        return false;
    }
    return true;
}

// A visibility setting made outside Veyra wins: the probe already ran under it.
inline std::wstring outsideVisibility(){
    for(const wchar_t* name:{L"HIP_VISIBLE_DEVICES",L"CUDA_VISIBLE_DEVICES",L"GPU_DEVICE_ORDINAL"}){
        wchar_t value[128]{};
        if(GetEnvironmentVariableW(name,value,DWORD(std::size(value))))return std::wstring(name)+L"="+value;
    }
    return {};
}

// Both the process block (read by a statically linked CRT when the HIP library
// loads) and this module's CRT copy. Must run before HIP is loaded.
inline bool showOnly(int index){
    const std::wstring value=std::to_wstring(index);
    return SetEnvironmentVariableW(L"HIP_VISIBLE_DEVICES",value.c_str())&&_wputenv_s(L"HIP_VISIBLE_DEVICES",value.c_str())==0;
}
} // namespace veyra::pipeline::rdna3::hip
