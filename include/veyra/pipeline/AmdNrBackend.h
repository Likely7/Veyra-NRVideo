// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>
namespace veyra::pipeline {
// Same-frame linear input/output contract shared by independent AMD providers.
class AmdNrBackend {
public:
    virtual ~AmdNrBackend()=default;
    virtual bool ready()const=0;
    virtual bool admits(uint32_t,uint32_t)const=0;
    virtual bool recordInputs(ID3D12GraphicsCommandList*,ID3D12Resource*,uint64_t,bool,float)=0;
    virtual bool enqueue()=0;
    virtual bool recordOutputs(ID3D12GraphicsCommandList*,ID3D12Resource*)=0;
    virtual bool retireSubmitted()=0;
    virtual const std::string& error()const=0;
};
}
