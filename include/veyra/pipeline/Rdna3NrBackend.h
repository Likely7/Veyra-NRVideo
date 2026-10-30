// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/pipeline/AmdNrBackend.h"
#include <filesystem>
#include <memory>
namespace veyra::pipeline {
class Rdna3NrBackend final:public AmdNrBackend {
public:
    Rdna3NrBackend();
    ~Rdna3NrBackend() override;
    Rdna3NrBackend(const Rdna3NrBackend&)=delete;
    Rdna3NrBackend& operator=(const Rdna3NrBackend&)=delete;
    bool open(const std::filesystem::path&,ID3D12Device*,ID3D12CommandQueue*,uint32_t,uint32_t,bool);
    // Explicit test binary; the product never calls this or loads it by fallback.
    bool openContractTest(const std::filesystem::path&,ID3D12Device*,ID3D12CommandQueue*,uint32_t,uint32_t,bool);
    void close() noexcept;
    bool ready()const override;
    bool admits(uint32_t,uint32_t)const override;
    bool recordInputs(ID3D12GraphicsCommandList*,ID3D12Resource*,uint64_t,bool,float) override;
    bool enqueue() override;
    bool recordOutputs(ID3D12GraphicsCommandList*,ID3D12Resource*) override;
    bool retireSubmitted() override;
    const std::string& error()const override;
    static bool runtimeAvailable(const std::filesystem::path&);
    static bool hipRuntimeAvailable();
    ID3D12Resource* diagnosticProxyForTest()const;
private:
    bool start(const std::filesystem::path&,ID3D12Device*,ID3D12CommandQueue*,uint32_t,uint32_t,bool,bool);
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
