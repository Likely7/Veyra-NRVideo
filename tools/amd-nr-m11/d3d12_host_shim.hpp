#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>
#include "nr_gpu_arch_ids.hpp"

namespace m11_shim {

// Initialize log target, ACO directory, shell aliases, shader search paths, and D3D12 net planner.
// root_dir must be the directory containing "dlssnr-amd\".
bool init_runtime(const std::string& root_dir, const std::string& log_file, int model_choice,
                  bool quiet_stdout = true, nr::arch::GpuArch arch = nr::arch::GpuArch::rdna3);

// Update active model choice dynamically before building a new network/resolution.
void set_model_choice(int model_choice);
int model_choice();

// Command queue & command list recording lifetime hooks replacing MinHook D3D12Core hooks.
void set_last_direct_queue(ID3D12CommandQueue* queue);
void begin_recording(ID3D12CommandList* cmd);
void end_recording(ID3D12CommandList* cmd, ID3D12Fence* fence, uint64_t signal_value);

std::string last_network_name();

}  // namespace m11_shim
