#pragma once
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/engine/VideoHdrSettings.h"
#include <windows.h>

namespace veyra::ngx {
// RTX Video SDK 1.1 public ABI, independent implementation. The shared NGX
// core owns initialization; the pinned module reference outlives the feature.
class TrueHdrBackend {
public:
    ~TrueHdrBackend(){release();}
    bool create(NgxCoreHost&,ID3D12GraphicsCommandList*,const std::wstring& runtimeDirectory);
    bool evaluate(ID3D12GraphicsCommandList*,ID3D12Resource* sdr,ID3D12Resource* scrgb,const engine::VideoHdrSettings&);
    bool release();
private:
    NgxCoreHost* core_=nullptr;
    NVSDK_NGX_Parameter* params_=nullptr;
    NVSDK_NGX_Handle* handle_=nullptr;
    HMODULE module_=nullptr;
    bool first_=true;
};
}
