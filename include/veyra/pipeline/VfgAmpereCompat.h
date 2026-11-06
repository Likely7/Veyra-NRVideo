// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace veyra::pipeline {
constexpr bool vfgNativeArchitecture(int major,int minor) { return (major==8&&minor==9)||major==12; }
constexpr bool vfgAmpereArchitecture(int major,int minor) { return major==8&&minor==6; }
constexpr int vfgReportedAttribute(int value,int attribute,int device,int selectedDevice,int result) {
    return !result&&attribute==76&&device==selectedDevice&&value==6?9:value;
}
constexpr bool vfgRetargetContext(int currentDevice,int selectedDevice,int result) {return !result&&currentDevice==selectedDevice;}
struct VfgFatbinChanges { unsigned containers=0,elfFlags=0; };
// Bounded port of RTX30-Broadcast-FrameGen's SM89 -> SM86 container retarget.
// Writes a private copy, never the SDK's original bytes; false rejects malformed input.
bool retargetVfgFatbin(std::span<const std::byte>,std::vector<std::byte>&,VfgFatbinChanges&,std::string&);
// PE import discovery instead of the upstream hard-coded +0x3B000 IAT window.
bool findVfgCudaImports(std::span<const std::byte>,std::array<uint32_t,2>&,std::string&);
namespace detail { struct VfgAmpereState; }
class VfgAmpereCompat {
public:
    static constexpr uint64_t knownSize=207321712;
    static constexpr const char* knownSha256="270CF4FFF9329908F9770D306CB51B910EE04323AF5520CD02A98A2E22ABA795";
    VfgAmpereCompat()=default;
    ~VfgAmpereCompat();
    VfgAmpereCompat(const VfgAmpereCompat&)=delete;
    VfgAmpereCompat& operator=(const VfgAmpereCompat&)=delete;
    bool install(HMODULE vfg,HMODULE cuda,int selectedDevice,std::string& reason);
    bool reset(); // after SDK effects/worker are destroyed, before unloading libraries
    bool installed() const { return bool(state_); }
    static bool restoreUncertain();
private:
    std::shared_ptr<detail::VfgAmpereState> state_;
};
}
