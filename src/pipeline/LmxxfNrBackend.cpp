// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/pipeline/LmxxfNrBackend.h"
#include "veyra/Log.h"
#include <atomic>
#include <format>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace veyra::pipeline {
bool LmxxfNrBackend::result(int32_t code, const char* operation) {
    if (code == LMXXF_NR_OK) return true;
    char detail[2048]{};
    if (api_.GetLastError) api_.GetLastError(detail, sizeof(detail));
    error_ = std::format("{} code={} {}", operation, code, detail);
    log::error("amd-nr", error_);
    return false;
}
bool LmxxfNrBackend::open(const std::filesystem::path& directory, ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t vendor) {
    close(); error_.clear();
    if (vendor != 0x1002 || !device || !queue || !directory.is_absolute()) {
        error_ = "lmxxf NR requires an AMD adapter, a D3D12 queue and an absolute runtime directory";
        log::error("amd-nr", error_); return false;
    }
    const auto path = directory / L"LmxxfNrRuntime.dll";
    module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module_) { error_ = std::format("LmxxfNrRuntime.dll load failed win32={}", GetLastError()); log::error("amd-nr", error_); return false; }
    const auto getApi = reinterpret_cast<decltype(&LmxxfNrGetApi)>(GetProcAddress(module_, "LmxxfNrGetApi"));
    if (!getApi) { error_ = "LmxxfNrGetApi export is missing"; log::error("amd-nr",error_); close(); return false; }
    api_.struct_size = sizeof(api_);
    int32_t status = getApi(LMXXF_NR_ABI_VERSION, &api_);
    if (status == LMXXF_NR_INVALID_ARGUMENT) { api_ = {}; api_.struct_size = LMXXF_NR_API_V1_SIZE; status = getApi(LMXXF_NR_ABI_VERSION, &api_); }
    if (status != LMXXF_NR_OK || api_.struct_size < LMXXF_NR_API_V1_SIZE || api_.abi_version != LMXXF_NR_ABI_VERSION ||
        !api_.QueryCapabilities || !api_.Create || !api_.Destroy || !api_.PrepareSession ||
        !api_.PrepareFrame || !api_.RecordInputs || !api_.EnqueueHip || !api_.RecordOutputs ||
        !api_.CancelUnsubmitted || !api_.Retire || !api_.ResetHistory || !api_.Drain || !api_.GetLastError) {
        error_ = std::format("invalid lmxxf ABI/function table code={}", status); log::error("amd-nr", error_); close(); return false;
    }
    caps_.struct_size = sizeof(caps_);
    if (!result(api_.QueryCapabilities(&caps_), "QueryCapabilities") || caps_.abi_version != LMXXF_NR_ABI_VERSION || !caps_.max_input_width || !caps_.max_input_height) { close(); return false; }
    // The add-on's sRGB input mode is incompatible with Veyra's linear FP16
    // contract. Runtime flags ignore that file key, but a process environment
    // override still reaches the upstream codec. Do not silently change it.
    const auto* srgb=std::getenv("DLSS5_CODEC_SRGB");
    const auto* adaptive=std::getenv("DLSS5_VIT_ADAPTIVE");
    if((srgb&&std::string_view(srgb)=="1") || (!caps_.history_supported&&adaptive&&std::string_view(adaptive)!="0"&&*adaptive)){
        error_="Veyra AMD NR requires DLSS5_CODEC_SRGB=0 and DLSS5_VIT_ADAPTIVE=0: linear input and source-reset isolation";
        log::error("amd-nr",error_);close();return false;
    }
    const auto assets = directory / L"assets";
    LmxxfNrCreateInfo info{}; info.struct_size = sizeof(info); info.device = device; info.queue = queue;
    info.assets_directory = assets.c_str(); // flags=0: a failed network is never passed off as NR
    if (!result(api_.Create(&info, &context_), "Create") || !context_ || !result(api_.PrepareSession(context_), "PrepareSession")) { close(); return false; }
    queue_ = queue;
    static std::atomic<uint64_t> next{1}; session_ = next++;
    log::info("amd-nr", std::format("lmxxf ABI={} session={} maxInput={}x{}; module metadata admitted, HIP inference initializes on first frame", api_.abi_version, session_, caps_.max_input_width, caps_.max_input_height));
    return true;
}
bool LmxxfNrBackend::admits(uint32_t w, uint32_t h) const {
    return ready() && w && h && w <= 2560 && h <= caps_.max_input_height && uint64_t(w)*h <= uint64_t(caps_.max_input_width)*caps_.max_input_height;
}
bool LmxxfNrBackend::recordInputs(ID3D12GraphicsCommandList* list, ID3D12Resource* color, uint64_t frame, bool reset, float intensity) {
    if (!ready() || job_.handle || !list || !color) { error_ = "invalid/unretired lmxxf input job"; return false; }
    const auto desc = color->GetDesc();
    if (!admits(uint32_t(desc.Width), desc.Height) || desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) { error_ = "lmxxf NR needs linear RGBA16F, height <=1080 and <=1920x1080 pixels (width <=2560)"; log::error("amd-nr", error_); return false; }
    if (reset && !result(api_.ResetHistory(context_), "ResetHistory")) return false;
    LmxxfNrFrameInfo info{}; info.struct_size = sizeof(info); info.session_id = session_;
    info.frame_id = frame; info.list_generation = frame; info.command_list = list;
    info.color = color; info.color_width = uint32_t(desc.Width); info.color_height = desc.Height;
    info.color_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    // Veyra applies residual/protection/temporal controls after the full network output.
    if(!std::isfinite(intensity)) { error_ = "invalid model intensity"; return false; }
    info.flags = LMXXF_NR_FRAME_FLAG_STRENGTH; info.transfer_strength = std::clamp(intensity,0.f,1.f); info.color_strength = info.model_scale = 1;
    info.pre_exposure = info.exposure_scale = 1;
    job_ = {}; job_.struct_size = sizeof(job_);
    int32_t prepared=api_.PrepareFrame(context_,&info,&job_);
    if(prepared==LMXXF_NR_INVALID_ARGUMENT){info.struct_size=LMXXF_NR_FRAME_INFO_V1_SIZE;prepared=api_.PrepareFrame(context_,&info,&job_);}
    if (!result(prepared, "PrepareFrame") || !job_.handle || !job_.private_output) return false;
    auto* output = static_cast<ID3D12Resource*>(job_.private_output);
    const auto out = output->GetDesc();
    if (out.Format != desc.Format || out.Width != desc.Width || out.Height != desc.Height) { error_ = "lmxxf output format/extent differs from the linear input"; return false; }
    return result(api_.RecordInputs(context_, job_.handle, list), "RecordInputs");
}
bool LmxxfNrBackend::enqueue() {
    if (!job_.handle || enqueued_ || !result(api_.EnqueueHip(context_, job_.handle, queue_), "EnqueueHip")) return false;
    enqueued_ = true;
    if(!diagnosticsLogged_&&api_.GetStatus){
        char status[4096]{};
        if(api_.GetStatus(context_,status,sizeof(status))==LMXXF_NR_OK)
            log::info("amd-nr",std::format("first enqueue session={} runtime={}",session_,status));
        diagnosticsLogged_=true;
    }
    return true;
}
bool LmxxfNrBackend::recordOutputs(ID3D12GraphicsCommandList* list, ID3D12Resource* destination) {
    if (!job_.handle || !enqueued_ || !list || !destination || !result(api_.RecordOutputs(context_, job_.handle, list), "RecordOutputs")) return false;
    // The ABI leaves private_output shader-readable. Copy into Veyra's stable
    // linear layer output; the graph already transitioned destination to COPY_DEST.
    auto* source = static_cast<ID3D12Resource*>(job_.private_output);
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {source, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &b); list->CopyResource(destination, source);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter); list->ResourceBarrier(1, &b);
    outputsRecorded_ = true; return true;
}
bool LmxxfNrBackend::retireSubmitted() {
    if (!outputsRecorded_) return true;
    if (!result(api_.Retire(context_, job_.handle), "Retire")) return false;
    job_ = {}; enqueued_ = outputsRecorded_ = false; return true;
}
void LmxxfNrBackend::close() noexcept {
    if (context_) {
        if (job_.handle && !enqueued_) (void)result(api_.CancelUnsubmitted(context_, job_.handle),"CancelUnsubmitted");
        (void)result(api_.Drain(context_),"Drain"); (void)result(api_.Destroy(context_),"Destroy");
    }
    context_ = nullptr; queue_ = nullptr; job_ = {}; api_ = {}; caps_ = {};
    enqueued_ = outputsRecorded_ = diagnosticsLogged_ = false;
    if (module_) FreeLibrary(module_); module_ = nullptr;
}
}
