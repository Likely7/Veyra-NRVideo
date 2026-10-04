#pragma once
#include <cstdint>

namespace veyra::gfx {
enum class GpuPriority : int { Normal=2, High=4, Realtime=5 };
enum class GpuPriorityState { Pending, Applied, Unavailable, Rejected };
struct GpuPriorityStatus {
    GpuPriority requested=GpuPriority::Normal;
    int actual=-1;
    GpuPriorityState state=GpuPriorityState::Pending;
    uint32_t status=0;
    uint64_t revision=0;
};
// Own process only. Does not create a device, adjust CPU priority, elevate or
// change another process. Pending requests are retried once a GPU session exists.
void requestGpuPriority(GpuPriority);
void applyRequestedGpuPriority();
GpuPriorityStatus gpuPriorityStatus();
}
