#include "veyra/ui/EffectAvailability.h"
#include "veyra/pipeline/AmdNrProvider.h"
#include "veyra/pipeline/Rdna3NrProtocol.h"
#include <iostream>
using namespace veyra::ui;
int main() {
    unsigned checks=0, failures=0;
    const EffectGpu devices[] = {{0,false,false,false,false},{0x10DE,true,true,false,false},
        {0x10DE,true,false,true,false},{0x10DE,true,false,false,false},
        {0x10DE,false,false,false,false},{0x1002,false,false,false,true},
        {0x1002,false,false,false,false},{0x8086,false,false,false,false}};
    const bool expected[][7] = {{0,0,0,0,0,0,0},{1,1,1,1,0,0,1},{1,0,1,1,0,0,1},
        {1,0,1,0,0,0,1},{0,0,0,0,0,0,1},{0,0,0,0,1,1,1},{0,0,0,0,0,0,1},{0,0,0,0,0,0,1}};
    for (unsigned device=0;device<8;++device) {
        for (int feature=0;feature<7;++feature) {
            ++checks;
            if (hardwareSupports(HardwareEffect(feature),devices[device])!=expected[device][feature]) ++failures;
        }
    }
    auto verify=[&](bool ok){++checks;if(!ok)++failures;};
    using namespace veyra::pipeline;
    verify(amdNrProvider(0x1002,L"AMD Radeon RX 7900 XTX")==AmdNrProvider::RedDukeRdna3);
    verify(amdNrProvider(0x1002,L"amd radeon rx7800 xt")==AmdNrProvider::RedDukeRdna3);
    verify(amdNrProvider(0x1002,L"AMD Radeon RX 7600M")==AmdNrProvider::RedDukeRdna3);
    verify(amdNrProvider(0x1002,L"AMD Radeon RX 9070 XT")==AmdNrProvider::LmxxfRdna4);
    verify(amdNrProvider(0x1002,L"AMD Radeon RX 9060")==AmdNrProvider::LmxxfRdna4);
    verify(amdNrProvider(0x1002,L"AMD Radeon RX 6800 XT")==AmdNrProvider::Unsupported);
    verify(amdNrProvider(0x1002,L"AMD Radeon 780M")==AmdNrProvider::Unsupported);
    verify(amdNrProvider(0x10de,L"RX 7900 XTX")==AmdNrProvider::Unsupported);
    EffectGpu rdna3Gpu{};rdna3Gpu.vendor=0x1002;rdna3Gpu.rx7000=true;
    verify(hardwareSupports(HardwareEffect::AmdNr,rdna3Gpu));
    verify(!hardwareSupports(HardwareEffect::Fsr4,rdna3Gpu));
    verify(!hardwareSupports(HardwareEffect::NvidiaNrSf,rdna3Gpu));
    verify(rdna3::validExtent(1920,1080)&&rdna3::validExtent(2560,800));
    verify(!rdna3::validExtent(3840,2160)&&!rdna3::validExtent(2560,1080)&&!rdna3::validExtent(0,1080));
    rdna3::Evaluate packet;verify(rdna3::validHeader(packet.h,rdna3::Op::Evaluate,sizeof(packet)));
    packet.h.version++;verify(!rdna3::validHeader(packet.h,rdna3::Op::Evaluate,sizeof(packet)));
    packet={};packet.h.bytes=UINT32_MAX;verify(!rdna3::validHeader(packet.h,rdna3::Op::Evaluate,sizeof(packet)));
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
