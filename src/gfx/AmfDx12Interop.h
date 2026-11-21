// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/Log.h"

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
#include <format>
#include <mutex>
#include "core/Factory.h"
#include "core/D3D12AMF.h"

namespace veyra::gfx::amfDx12 {
using Microsoft::WRL::ComPtr;

// AMFInit hands out one factory per process; the runtime stays loaded with it.
struct Runtime {
    amf::AMFFactory* factory = nullptr;
    std::string version;
    std::wstring compiler;
    bool systemCompiler = false;
};
inline const Runtime& runtime() {
    static Runtime r;
    static std::once_flag once;
    std::call_once(once, [] {
        // AMF reflects its D3D12 kernels with d3dcompiler_47.dll, loaded by name. The copy Qt
        // deploys next to the program (6.3.9600, Windows 8.1) cannot reflect them: D3DReflect
        // returns E_NOINTERFACE and FRC Init fails with AMF_DIRECTX_FAILED (AMF trace,
        // 2026-11-14). Bind the name to the system copy before AMF asks for it.
        HMODULE compiler = GetModuleHandleW(L"d3dcompiler_47.dll");
        if (!compiler) compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        wchar_t compilerPath[MAX_PATH]{}, systemPath[MAX_PATH]{};
        if (compiler) GetModuleFileNameW(compiler, compilerPath, MAX_PATH);
        GetSystemDirectoryW(systemPath, MAX_PATH);
        r.compiler = compilerPath;
        std::error_code ec;
        r.systemCompiler = compiler && std::filesystem::equivalent(std::filesystem::path(compilerPath).parent_path(), systemPath, ec);
        if (!r.systemCompiler)
            log::warn("amf", "d3dcompiler_47.dll is not the system copy; AMF kernels may fail to load: " +
                      std::filesystem::path(compilerPath).string());
        // Only the driver's copy in System32, never one next to the program.
        HMODULE module = LoadLibraryExW(AMF_DLL_NAME, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) { log::warn("amf", std::format("AMF runtime not loaded err={}", GetLastError())); return; }
        auto query = reinterpret_cast<AMFQueryVersion_Fn>(GetProcAddress(module, AMF_QUERY_VERSION_FUNCTION_NAME));
        auto init = reinterpret_cast<AMFInit_Fn>(GetProcAddress(module, AMF_INIT_FUNCTION_NAME));
        amf_uint64 installed = 0;
        if (query) query(&installed);
        r.version = std::format("{}.{}.{}.{}", AMF_GET_MAJOR_VERSION(installed), AMF_GET_MINOR_VERSION(installed),
                                AMF_GET_SUBMINOR_VERSION(installed), AMF_GET_BUILD_VERSION(installed));
        // A runtime older than these headers is asked for its own version.
        const amf_uint64 requested = installed && installed < AMF_FULL_VERSION ? installed : AMF_FULL_VERSION;
        const AMF_RESULT result = init ? init(requested, &r.factory) : AMF_FAIL;
        if (result != AMF_OK || !r.factory) {
            r.factory = nullptr;
            log::error("amf", std::format("AMFInit failed result={} runtime={}", int(result), r.version));
            return;
        }
        // Diagnostics only: VEYRA_AMF_TRACE=<file> writes AMF's own debug trace there.
        wchar_t tracePath[MAX_PATH]{};
        amf::AMFTrace* trace = nullptr;
        if (GetEnvironmentVariableW(L"VEYRA_AMF_TRACE", tracePath, MAX_PATH) && r.factory->GetTrace(&trace) == AMF_OK && trace) {
            trace->SetPath(tracePath);
            trace->EnableWriter(AMF_TRACE_WRITER_FILE, true);
            trace->SetGlobalLevel(AMF_TRACE_DEBUG);
            trace->SetWriterLevel(AMF_TRACE_WRITER_FILE, AMF_TRACE_DEBUG);
            log::info("amf", "AMF trace enabled (VEYRA_AMF_TRACE)");
        }
    });
    return r;
}
inline UINT64 fenceValue(ID3D12Fence* fence) {
    UINT64 value = 0; UINT size = sizeof(value);
    return SUCCEEDED(fence->GetPrivateData(AMFFenceValueGUID, &size, &value)) ? value : 0;
}
inline void setFenceValue(ID3D12Fence* fence, UINT64 value) { fence->SetPrivateData(AMFFenceValueGUID, sizeof(value), &value); }
inline ComPtr<ID3D12Fence> resourceFence(ID3D12Resource* resource) {
    IUnknown* unknown = nullptr; UINT size = sizeof(unknown);
    ComPtr<ID3D12Fence> fence;
    if (SUCCEEDED(resource->GetPrivateData(AMFFenceGUID, &size, &unknown)) && unknown) {
        unknown->QueryInterface(IID_PPV_ARGS(&fence));
        unknown->Release();
    }
    return fence;
}
inline D3D12_RESOURCE_STATES resourceState(ID3D12Resource* resource) {
    UINT value = 0; UINT size = sizeof(value);
    resource->GetPrivateData(AMFResourceStateGUID, &size, &value);
    return D3D12_RESOURCE_STATES(value);
}
inline void setResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
    const UINT value = UINT(state);
    resource->SetPrivateData(AMFResourceStateGUID, sizeof(value), &value);
}
inline void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}
// Shutdown only: the textures may still be read on AMF's queue.
inline void waitFence(ID3D12Fence* fence, UINT64 value) {
    if (!fence || !value || fence->GetCompletedValue() >= value) return;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event && SUCCEEDED(fence->SetEventOnCompletion(value, event))) WaitForSingleObject(event, 2000);
    if (event) CloseHandle(event);
}
}
