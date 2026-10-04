#include "veyra/ui/EffectAvailability.h"
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
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
