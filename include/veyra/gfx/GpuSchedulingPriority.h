#pragma once
#include <cstdint>

namespace veyra::gfx {
enum class GpuPriority : int { Normal=2, High=4, Realtime=5 };
enum class GpuPriorityState { Pending, Applied, Unavailable, Rejected };
struct GpuPriorityStatus {
    GpuPriority requested=GpuPriority::High;
    int actual=-1;
    GpuPriorityState state=GpuPriorityState::Pending;
    uint32_t status=0;
    uint64_t revision=0;
    bool capped=false;   // the effective class is below `requested` (setGpuPriorityCap)
};
// Own process only. Does not create a device, adjust CPU priority, elevate or
// change another process. Pending requests are retried once a GPU session exists.
void requestGpuPriority(GpuPriority);
void applyRequestedGpuPriority();
GpuPriorityStatus gpuPriorityStatus();
// Highest class to use while another program shares the GPU (Realtime = no cap). The
// class applied is the lower of the request and the cap; `requested` keeps the request.
void setGpuPriorityCap(GpuPriority cap);
// Highest class an in-process compute runtime survives here (AMD HIP self-test,
// AmdHipPreflight.h). Independent of the cap above; the lowest of the three applies.
void setGpuPriorityComputeCap(GpuPriority cap);
}
