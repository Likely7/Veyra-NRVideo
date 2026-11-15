#pragma once
#include <nvsdk_ngx.h>
#include <memory>
#include <string>

namespace veyra::ngx {
// Independent, unmodified ANR DLL. Never registered with NVIDIA's NGX core.
// This host adapter is Veyra code; no upstream implementation is copied.
class AnrProvider {
public:
    AnrProvider();
    ~AnrProvider();
    static std::wstring directory(const std::wstring& runtimeRoot);
    static bool available(const std::wstring& runtimeRoot);
    bool open(ID3D12Device*,const std::wstring& runtimeRoot,unsigned& maxGenerated);
    NVSDK_NGX_Parameter* parameters();
    NVSDK_NGX_Result create(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
    NVSDK_NGX_Result evaluate(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*);
    NVSDK_NGX_Result release(NVSDK_NGX_Handle*);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
