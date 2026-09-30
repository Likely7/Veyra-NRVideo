#pragma once
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <memory>
#include <span>
#include <string>

namespace veyra::ngx {
// Exclusive owner of process-local provider patches. Destroy after NGX shutdown.
class FgCompatibilitySession {
public:
    FgCompatibilitySession();
    ~FgCompatibilitySession();
    FgCompatibilitySession(const FgCompatibilitySession&) = delete;
    FgCompatibilitySession& operator=(const FgCompatibilitySession&) = delete;
    static bool requested(uint32_t vendor, uint32_t device);
    static bool processHealthy();
    bool open(ID3D12Device* device, uint32_t vendor, uint32_t deviceId, const std::wstring& directory);
    HMODULE provider() const;
    // True when the 310.9.1 provider was opened and patched by DlssgTransfusion.
    bool transfusionProfile() const;
    bool prepareDriver(const std::wstring& directory, const char* project, const char* engine);
    bool bindResources(std::span<ID3D12Resource* const> real, std::span<ID3D12Resource* const> generated);
    bool beginInitialization();
    bool endInitialization(bool success);
    bool beginCapabilities();
    bool endCapabilities();
    bool publishStartup(NVSDK_NGX_Parameter* parameters);
    bool beginCreate(ID3D12GraphicsCommandList* list);
    bool endCreate(bool success);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
