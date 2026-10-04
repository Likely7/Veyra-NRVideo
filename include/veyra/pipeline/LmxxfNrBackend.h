// SPDX-License-Identifier: GPL-3.0-only
// Host adapter for lmxxf's MIT C ABI; third_party/lmxxf/ records the pinned source.
#pragma once
#include <d3d12.h>
#include <filesystem>
#include <cstdint>
#include <string>
#include "../../../third_party/lmxxf/LmxxfNrApi.h"

namespace veyra::pipeline {
class LmxxfNrBackend {
public:
    ~LmxxfNrBackend() { close(); }
    LmxxfNrBackend() = default;
    LmxxfNrBackend(const LmxxfNrBackend&) = delete;
    LmxxfNrBackend& operator=(const LmxxfNrBackend&) = delete;
    bool open(const std::filesystem::path& directory, ID3D12Device*, ID3D12CommandQueue*, uint32_t vendor);
    void close() noexcept;
    bool ready() const { return context_ != nullptr; }
    bool admits(uint32_t width, uint32_t height) const;
    bool recordInputs(ID3D12GraphicsCommandList*, ID3D12Resource* color, uint64_t frame, bool reset, float intensity = 1);
    bool enqueue(); // immediately after the producer submission, on the session queue
    bool recordOutputs(ID3D12GraphicsCommandList*, ID3D12Resource* destination);
    bool retireSubmitted(); // after the list containing RecordOutputs was submitted
    const std::string& error() const { return error_; }
private:
    bool result(int32_t code, const char* operation);
    HMODULE module_ = nullptr;
    LmxxfNrApi api_{};
    LmxxfNrCapabilities caps_{};
    LmxxfNrJob job_{};
    void* context_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr; // graph owns it and outlives the adapter
    uint64_t session_ = 0;
    bool enqueued_ = false, outputsRecorded_ = false;
    std::string error_;
};
}
