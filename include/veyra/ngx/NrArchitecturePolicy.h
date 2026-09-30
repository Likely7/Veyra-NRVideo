#pragma once
#include <cstdint>
#include <string_view>
namespace veyra::ngx {
// Defaults for a fresh UI only. Explicit choices restored from a preset or
// session win. Unknown/professional adapters are not guessed from a PCI range.
constexpr bool preferSfNr(uint32_t vendorId, std::wstring_view description) {
    return vendorId == 0x10DE && (description.find(L"RTX 20") != description.npos ||
        description.find(L"RTX 30") != description.npos || description.find(L"RTX 40") != description.npos);
}
// The experimental profile only changes a successful query for the exact
// selected Ampere adapter. Other GPUs and driver failures retain their result.
constexpr bool rewriteNrAmpereArchitecture(uint32_t& architecture, bool selected, int result) {
    if(result!=0||!selected||(architecture&0xFFFFFFF0u)!=0x170u)return false;
    architecture=0x1B0u;
    return true;
}
// The rewrite suits builds that put Ampere-runnable kernels in the Blackwell
// slot (NeuralScreen's 310.8.0.0). The ShortFuse "SF" family (310.8.2.x and the
// later Lecram 310.8.3.x) lifts the gate itself and picks an FP16 route from the
// real architecture, so reporting Blackwell there would select kernels Ampere
// cannot run. Versions come from the loaded DLL's VS_FIXEDFILEINFO.
constexpr bool nrRuntimeNeedsAmpereRewrite(uint32_t fileVersionMs, uint32_t fileVersionLs) {
    const uint32_t major=fileVersionMs>>16, minor=fileVersionMs&0xFFFFu, build=fileVersionLs>>16;
    return !(major==310u&&minor==8u&&build>=2u);
}
}
