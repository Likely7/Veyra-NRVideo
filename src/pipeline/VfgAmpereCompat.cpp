// Derived from Xchen991/RTX30-Broadcast-FrameGen, MIT, Copyright (c) 2026.
// Fixed commit 07536ab32a508b7daa714a4a9bda5b56f031bbc3, src/dllmain.cpp:
// Hook_cuDeviceGetAttribute / Hook_cuModuleLoadData / InstallHooksOnVFG.
// Original permission text: licenses/RTX30-Broadcast-FrameGen-MIT.txt.
// Veyra changes: named PE imports; audited module identity; selected CUDA device
// only; structured, bounded fatbin parsing into private copies; reference-counted
// module/session ownership; checked IAT restoration. No Broadcast UI patch,
// proxy DLL, driver-wide hook, original-data write, or polling thread.
#include "veyra/pipeline/VfgAmpereCompat.h"
#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>

namespace veyra::pipeline {
namespace {
constexpr size_t maxFatbin=32u*1024u*1024u;
template<class T> T get(std::span<const std::byte> s,size_t p) { T v{};std::memcpy(&v,s.data()+p,sizeof(v));return v; }
template<class T> void put(std::vector<std::byte>& s,size_t p,T v) {std::memcpy(s.data()+p,&v,sizeof(v));}
bool fits(size_t p,size_t n,size_t size) {return p<=size&&n<=size-p;}
bool bad(std::string& why,const char* text) {why=text;return false;}
bool cstring(std::span<const std::byte> s,size_t p,std::string& out) {
    if(!fits(p,1,s.size()))return false;
    const auto n=std::min(size_t(256),s.size()-p);
    const auto* start=reinterpret_cast<const char*>(s.data()+p);
    const auto* end=static_cast<const char*>(std::memchr(start,0,n));
    if(!end)return false;out.assign(start,end);return true;
}
// SEH is isolated from all C++ objects with destructors.
bool safeCopy(void* dst,const void* src,size_t bytes) {
    __try {std::memcpy(dst,src,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
bool retargetVfgFatbin(std::span<const std::byte> input,std::vector<std::byte>& output,VfgFatbinChanges& changed,std::string& why) {
    output.clear();changed={};why.clear();
    if(input.size()<16||input.size()>maxFatbin)return bad(why,"fatbin length outside bounded range");
    if(get<uint32_t>(input,0)!=0xBA55ED50||get<uint16_t>(input,4)!=1||get<uint16_t>(input,6)!=16)
        return bad(why,"unknown fatbin header");
    if(get<uint64_t>(input,8)!=input.size()-16)return bad(why,"fatbin payload length mismatch");
    std::vector<std::byte> copy(input.begin(),input.end());
    size_t p=16;unsigned entries=0;
    while(p<input.size()) {
        if(++entries>4096||!fits(p,64,input.size()))return bad(why,"truncated fatbin entry");
        const auto kind=get<uint16_t>(input,p),version=get<uint16_t>(input,p+2);
        const auto header=get<uint32_t>(input,p+4);
        const auto bytes=get<uint64_t>(input,p+8);
        if((kind!=1&&kind!=2)||version!=0x101||header<64||header>4096||header%8||
           !fits(p,header,input.size())||bytes>input.size()-p-header||!bytes)
            return bad(why,"invalid fatbin entry bounds or format");
        const auto sm=get<uint32_t>(input,p+28);const size_t payload=p+header;
        if(sm==89) {
            // Audited SDK: uncompressed ELF64 CUDA entry, header=64, e_flags=0x00590559.
            // Retargeting metadata does not translate the kernel's instructions.
            if(kind!=2||header!=64||bytes<64||get<uint32_t>(input,payload)!=0x464c457f||
               get<uint8_t>(input,payload+4)!=2||get<uint8_t>(input,payload+5)!=1||
               get<uint16_t>(input,payload+18)!=190||get<uint32_t>(input,payload+48)!=0x00590559)
                return bad(why,"unrecognized SM89 CUDA ELF layout");
            put<uint32_t>(copy,p+28,86);put<uint32_t>(copy,payload+48,0x00560556);
            ++changed.containers;++changed.elfFlags;
        }
        p=payload+size_t(bytes);
    }
    if(!entries)return bad(why,"empty fatbin");
    output=std::move(copy);return true;
}
bool findVfgCudaImports(std::span<const std::byte> image,std::array<uint32_t,2>& slots,std::string& why) {
    slots={};why.clear();
    if(image.size()<sizeof(IMAGE_DOS_HEADER)||get<uint16_t>(image,0)!=IMAGE_DOS_SIGNATURE)return bad(why,"invalid DOS header");
    const auto nt=get<uint32_t>(image,offsetof(IMAGE_DOS_HEADER,e_lfanew));
    if(!fits(nt,sizeof(IMAGE_NT_HEADERS64),image.size())||get<uint32_t>(image,nt)!=IMAGE_NT_SIGNATURE)return bad(why,"invalid PE header");
    const auto h=get<IMAGE_NT_HEADERS64>(image,nt);
    if(h.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||h.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
       h.OptionalHeader.SizeOfImage!=image.size()||h.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT)
        return bad(why,"unsupported mapped PE");
    const auto dir=h.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(dir.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR)||!fits(dir.VirtualAddress,dir.Size,image.size()))return bad(why,"invalid import directory");
    bool terminated=false;
    for(size_t offset=0;fits(offset,sizeof(IMAGE_IMPORT_DESCRIPTOR),dir.Size);offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto d=get<IMAGE_IMPORT_DESCRIPTOR>(image,dir.VirtualAddress+offset);
        if(!d.Name&&!d.FirstThunk&&!d.OriginalFirstThunk){terminated=true;break;}
        std::string name;if(!cstring(image,d.Name,name))return bad(why,"invalid import name");
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(c>='A'&&c<='Z'?c+32:c);});
        if(name!="nvcuda.dll")continue;
        if(!d.OriginalFirstThunk||!d.FirstThunk||d.FirstThunk%alignof(void*))return bad(why,"missing or unaligned named CUDA thunks");
        bool end=false;
        for(size_t i=0;i<65536;++i) {
            const size_t original=size_t(d.OriginalFirstThunk)+i*8,iat=size_t(d.FirstThunk)+i*8;
            if(!fits(original,8,image.size())||!fits(iat,8,image.size()))return bad(why,"truncated CUDA thunks");
            const auto value=get<uint64_t>(image,original);if(!value){end=true;break;}
            if(value&IMAGE_ORDINAL_FLAG64)continue;
            if(value>UINT32_MAX||!cstring(image,size_t(value)+2,name))return bad(why,"invalid CUDA function name");
            int index=name=="cuDeviceGetAttribute"?0:name=="cuModuleLoadData"?1:-1;
            if(index<0)continue;
            if(slots[size_t(index)])return bad(why,"ambiguous CUDA import");
            slots[size_t(index)]=uint32_t(iat);
        }
        if(!end)return bad(why,"unterminated CUDA imports");
    }
    if(!terminated||!slots[0]||!slots[1])return bad(why,"required CUDA imports not unique");
    return true;
}
namespace detail {
struct VfgAmpereState {
    using Attribute=int(__stdcall*)(int*,int,int);
    using Load=int(__stdcall*)(void**,const void*);
    using ContextDevice=int(__stdcall*)(int*);
    HMODULE module=nullptr,driver=nullptr;
    Attribute attribute=nullptr;Load load=nullptr;ContextDevice contextDevice=nullptr;
    int device=-1;unsigned leases=0;
    struct Slot {void** pointer=nullptr;void* original=nullptr;void* hook=nullptr;DWORD protection=0;bool changed=false;};
    std::array<Slot,2> slots{};
    std::atomic<unsigned> patchedLoads{0},attributeSpoofs{0},rejectedLoads{0};
    ~VfgAmpereState(){if(module)FreeLibrary(module);if(driver)FreeLibrary(driver);}
};
}
namespace {
std::mutex leaseMutex;
std::atomic<std::shared_ptr<detail::VfgAmpereState>> activeState;
std::atomic<bool> poisoned=false;
int __stdcall Hook_cuDeviceGetAttribute(int* value,int attribute,int device) {
    auto state=activeState.load();if(!state)return 3; // CUDA_ERROR_NOT_INITIALIZED
    const int result=state->attribute(value,attribute,device);
    if(!result&&value){const int reported=vfgReportedAttribute(*value,attribute,device,state->device,result);if(reported!=*value){*value=reported;++state->attributeSpoofs;}}
    return result;
}
int __stdcall Hook_cuModuleLoadData(void** module,const void* image) {
    auto state=activeState.load();if(!state)return 3;
    int currentDevice=-1;
    const int contextResult=state->contextDevice(&currentDevice);
    if(!vfgRetargetContext(currentDevice,state->device,contextResult))return state->load(module,image);
    uint32_t magic=0;
    if(!image)return state->load(module,image);
    if(!safeCopy(&magic,image,4)){++state->rejectedLoads;return 200;}
    if(magic!=0xBA55ED50)return state->load(module,image);
    try {
        std::array<std::byte,16> header{};
        if(!safeCopy(header.data(),image,header.size()))return 200; // CUDA_ERROR_INVALID_IMAGE
        const auto payload=get<uint64_t>(header,8);
        if(payload>maxFatbin-16){++state->rejectedLoads;return 200;}
        std::vector<std::byte> original(size_t(payload)+16),copy;
        if(!safeCopy(original.data(),image,original.size())){++state->rejectedLoads;return 200;}
        VfgFatbinChanges changes;std::string why;
        if(!retargetVfgFatbin(original,copy,changes,why)){
            ++state->rejectedLoads;log::error("vfg-ampere","kernel rejected: "+why);return 200;
        }
        if(!changes.containers)return state->load(module,image);
        // CUDA takes ownership of its module storage during this synchronous call.
        const int result=state->load(module,copy.data());
        ++state->patchedLoads;
        log::info("vfg-ampere",std::format("private fatbin bytes={} containers={} elfFlags={} cuModuleLoadData={}",copy.size(),changes.containers,changes.elfFlags,result));
        return result;
    } catch(...) {++state->rejectedLoads;return 2;} // CUDA_ERROR_OUT_OF_MEMORY
}
bool changeSlot(detail::VfgAmpereState::Slot& slot,bool restore) {
    DWORD prior=0;if(!VirtualProtect(slot.pointer,sizeof(void*),PAGE_READWRITE,&prior))return false;
    if(!slot.protection)slot.protection=prior;
    void* expected=restore?slot.hook:slot.original;
    void* desired=restore?slot.original:slot.hook;
    const bool changed=InterlockedCompareExchangePointer(slot.pointer,desired,expected)==expected;
    if(changed)slot.changed=!restore;
    DWORD ignored=0;bool protectedAgain=false;
    for(unsigned attempt=0;attempt<3&&!protectedAgain;++attempt)
        protectedAgain=VirtualProtect(slot.pointer,sizeof(void*),slot.protection,&ignored)!=FALSE;
    MEMORY_BASIC_INFORMATION actual{};
    protectedAgain=protectedAgain&&VirtualQuery(slot.pointer,&actual,sizeof(actual))&&actual.Protect==slot.protection;
    const bool equal=*slot.pointer==desired;
    if(!protectedAgain)poisoned=true;
    if(!changed||!protectedAgain||!equal)log::error("vfg-ampere",std::format("IAT {} failed changed={} protected={} equal={} win32={}",restore?"restore":"install",changed,protectedAgain,equal,GetLastError()));
    return changed&&protectedAgain&&equal;
}
bool restoreSlots(detail::VfgAmpereState& state) {
    bool okay=true;
    for(auto it=state.slots.rbegin();it!=state.slots.rend();++it)if(it->changed&&!changeSlot(*it,true))okay=false;
    if(!okay)poisoned=true;
    return okay&&!poisoned;
}
}
VfgAmpereCompat::~VfgAmpereCompat(){reset();}
bool VfgAmpereCompat::restoreUncertain(){return poisoned.load();}
bool VfgAmpereCompat::install(HMODULE module,HMODULE cuda,int device,std::string& why) {
    if(state_)return bad(why,"compatibility lease already held");
    std::lock_guard lock(leaseMutex);
    if(poisoned)return bad(why,"previous VFG import restoration failed; restart required");
    if(auto existing=activeState.load()){
        if(existing->module!=module||existing->driver!=cuda||existing->device!=device)return bad(why,"another VFG device owns compatibility");
        ++existing->leases;state_=std::move(existing);return true;
    }
    std::array<wchar_t,32768> path{};
    if(!module||!cuda||!GetModuleFileNameW(module,path.data(),DWORD(path.size())))return bad(why,"VFG module path unavailable");
    FileIdentity identity;IdentityError error{};
    if(!computeFileIdentity(path.data(),identity,error)||identity.sizeBytes!=knownSize||identity.sha256Upper!=knownSha256)
        return bad(why,"RTX30 VFG compatibility does not recognize this runtime; native paths are unchanged");
    auto state=std::make_shared<detail::VfgAmpereState>();
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(module),&state->module)||
       !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(cuda),&state->driver))
        return bad(why,"cannot retain VFG/CUDA module ownership");
    state->device=device;
    state->attribute=reinterpret_cast<detail::VfgAmpereState::Attribute>(GetProcAddress(cuda,"cuDeviceGetAttribute"));
    state->load=reinterpret_cast<detail::VfgAmpereState::Load>(GetProcAddress(cuda,"cuModuleLoadData"));
    state->contextDevice=reinterpret_cast<detail::VfgAmpereState::ContextDevice>(GetProcAddress(cuda,"cuCtxGetDevice"));
    if(!state->attribute||!state->load||!state->contextDevice)return bad(why,"required CUDA exports missing");
    auto* bytes=reinterpret_cast<const std::byte*>(module);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes+dos->e_lfanew);
    std::array<uint32_t,2> imports{};
    if(!findVfgCudaImports({bytes,nt->OptionalHeader.SizeOfImage},imports,why))return false;
    const std::array<void*,2> original={reinterpret_cast<void*>(state->attribute),reinterpret_cast<void*>(state->load)};
    const std::array<void*,2> hooks={reinterpret_cast<void*>(Hook_cuDeviceGetAttribute),reinterpret_cast<void*>(Hook_cuModuleLoadData)};
    for(size_t i=0;i<2;++i){auto& slot=state->slots[i];slot.pointer=reinterpret_cast<void**>(reinterpret_cast<std::byte*>(module)+imports[i]);slot.original=original[i];slot.hook=hooks[i];
        if(*slot.pointer!=slot.original)return bad(why,"VFG CUDA import already redirected by another component");}
    activeState.store(state);
    for(auto& slot:state->slots)if(!changeSlot(slot,false)){
        if(restoreSlots(*state))activeState.store({});
        return bad(why,"cannot install VFG compatibility imports");
    }
    state->leases=1;state_=std::move(state);
    log::info("vfg-ampere",std::format("installed selectedCudaDevice={} imports=2 identity={} (experimental SM86; private kernel copies; other driver callers unchanged)",device,identity.sha256Upper));
    return true;
}
bool VfgAmpereCompat::reset() {
    if(!state_)return !poisoned;
    std::lock_guard lock(leaseMutex);auto state=std::move(state_);
    if(--state->leases)return !poisoned;
    const bool okay=restoreSlots(*state);
    log::info("vfg-ampere",std::format("restored={} privateLoads={} attributeSpoofs={} rejectedLoads={}",okay,state->patchedLoads.load(),state->attributeSpoofs.load(),state->rejectedLoads.load()));
    if(okay)activeState.store({}); // Failed restoration intentionally pins callbacks/modules.
    return okay;
}
}
