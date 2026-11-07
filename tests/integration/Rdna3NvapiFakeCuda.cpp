// SPDX-License-Identifier: GPL-3.0-only
// CPU-only fixture. Never install this DLL in a product runtime/driver directory.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#ifdef VEYRA_RDNA3_NR_CALLER
struct ArchInfo { uint32_t version,architecture,implementation,revision; };
using Query = void* (*)(uint32_t);
extern "C" __declspec(dllexport) int testNeuralArchitecture(Query query,uint32_t* architecture) {
    auto fn=reinterpret_cast<int (*)(void*,ArchInfo*)>(query(0xd8265d24));
    if(!fn||!architecture)return -1;
    ArchInfo info{uint32_t(sizeof(ArchInfo))|(1u<<16),0,0,0};
    volatile int result=fn(reinterpret_cast<void*>(1),&info); // retain this caller's stack frame
    *architecture=info.architecture;
    return result;
}
#else
namespace {
std::atomic<int> deviceCount=2;
thread_local int currentDevice=101,currentStatus=0,luidStatus=0;
}
extern "C" __declspec(dllexport) void testState(int count,int ordinal,int status,int luidError) {
    deviceCount=count;currentDevice=100+ordinal;currentStatus=status;luidStatus=luidError;
}
extern "C" __declspec(dllexport) int cuCtxGetDevice(int* device) {
    if(currentStatus)return currentStatus;
    if(!device)return 1;
    *device=currentDevice;return 0;
}
extern "C" __declspec(dllexport) int cuDeviceGetCount(int* count) {
    if(!count)return 1;
    *count=deviceCount;return 0;
}
extern "C" __declspec(dllexport) int cuDeviceGet(int* device,int ordinal) {
    if(!device||ordinal<0||ordinal>=deviceCount)return 101;
    *device=100+ordinal;return 0;
}
extern "C" __declspec(dllexport) int cuDeviceGetLuid(char* luid,unsigned* nodeMask,int device) {
    if(luidStatus)return luidStatus;
    if(!luid||!nodeMask||device<100||device>=100+deviceCount)return 101;
    const uint64_t value=device==100?0x12004ull:0x10100ull;
    std::memcpy(luid,&value,sizeof(value));*nodeMask=1;return 0;
}
#endif
