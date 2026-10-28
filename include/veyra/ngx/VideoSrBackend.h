#pragma once
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include "veyra/ngx/NgxCoreHost.h"
namespace veyra::ngx {
// Independent adapter to the RTX Video SDK 1.1 documented VSR parameter ABI.
// Uses the existing NGX core session; input/output are encoded SDR RGBA8.
class VideoSrBackend {
public:
    ~VideoSrBackend(){release();}
    bool create(NgxCoreHost&,ID3D12GraphicsCommandList*,unsigned width=0,unsigned height=0,unsigned outWidth=0,unsigned outHeight=0);
    bool evaluate(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*,unsigned);
    bool release();
    const void* identity() const { return handle_; }
    bool duplicateCreation() const { return duplicateCreation_; }
private:
    NgxCoreHost* core_=nullptr;
    NVSDK_NGX_Parameter* params_=nullptr;
    NVSDK_NGX_Handle* handle_=nullptr;
    bool duplicateCreation_=false;
};
}
