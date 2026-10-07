#include "veyra/gfx/GpuSchedulingPriority.h"
#include "veyra/Log.h"
#include <windows.h>
#include <format>
#include <mutex>

namespace veyra::gfx {
namespace {
using GetPriority=LONG(WINAPI*)(HANDLE,int*);
using SetPriority=LONG(WINAPI*)(HANDLE,int);
struct Api {
    // Documented gdi32 exports, loaded from the Windows system directory.
    HMODULE module=LoadLibraryExW(L"gdi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    GetPriority get=module?reinterpret_cast<GetPriority>(GetProcAddress(module,"D3DKMTGetProcessSchedulingPriorityClass")):nullptr;
    SetPriority set=module?reinterpret_cast<SetPriority>(GetProcAddress(module,"D3DKMTSetProcessSchedulingPriorityClass")):nullptr;
    ~Api(){if(module)FreeLibrary(module);}
};
std::mutex mutex;
GpuPriorityStatus current;
void publish(GpuPriorityState state,int actual,uint32_t status){
    if(current.state==state&&current.actual==actual&&current.status==status)return;
    current.state=state;current.actual=actual;current.status=status;++current.revision;
    log::info("gpu-priority",std::format("requested={} actual={} state={} ntstatus=0x{:08X}",
        int(current.requested),actual,int(state),status));
}
void applyLocked(){
    static Api api;
    if(!api.get||!api.set){publish(GpuPriorityState::Unavailable,-1,ERROR_PROC_NOT_FOUND);return;}
    int before=-1,after=-1;
    const auto query=api.get(GetCurrentProcess(),&before);
    if(query<0){
        // WDDM has no process scheduling record before the first GPU context.
        publish(uint32_t(query)==0xC000000Du?GpuPriorityState::Pending:GpuPriorityState::Rejected,-1,uint32_t(query));return;
    }
    // Query first and avoid a redundant scheduler call when the requested
    // class is already in effect, including after a device reopen.
    if(before==int(current.requested)){
        publish(GpuPriorityState::Applied,before,0);return;
    }
    auto set=api.set(GetCurrentProcess(),int(current.requested));
    auto verify=api.get(GetCurrentProcess(),&after);
    // Realtime is the default (field request 2026-10-05). Where Windows refuses it
    // for this process, High is the closest class it may still grant; the status
    // then reports Rejected with High as the actual class.
    if(current.requested==GpuPriority::Realtime&&(set<0||verify<0||after!=int(current.requested))){
        const auto refused=set<0?set:verify;
        set=api.set(GetCurrentProcess(),int(GpuPriority::High));
        verify=api.get(GetCurrentProcess(),&after);
        publish(GpuPriorityState::Rejected,verify>=0?after:before,uint32_t(refused));
        return;
    }
    publish(set>=0&&verify>=0&&after==int(current.requested)?GpuPriorityState::Applied:GpuPriorityState::Rejected,
        verify>=0?after:before,uint32_t(set<0?set:verify));
}
}
void requestGpuPriority(GpuPriority priority){
    if(priority!=GpuPriority::Normal&&priority!=GpuPriority::High&&priority!=GpuPriority::Realtime)return;
    std::lock_guard lock(mutex);
    if(current.requested!=priority){current.requested=priority;++current.revision;}
    applyLocked();
}
void applyRequestedGpuPriority(){std::lock_guard lock(mutex);applyLocked();}
GpuPriorityStatus gpuPriorityStatus(){std::lock_guard lock(mutex);return current;}
}
