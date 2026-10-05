#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <istream>
#include <ostream>

namespace veyra::engine {
// Appended only in the new preset schema; legacy files retain default/off.
inline void writeNrCorrection(std::ostream& out,const NrCorrectionSettings& c,bool extended=false){
    out<<int(c.enabled)<<' '<<int(c.automatic)<<' '<<c.hue<<' '<<c.chroma<<' '
       <<c.highlight<<' '<<c.compression<<' '<<c.stability;
    if(extended)out<<' '<<c.neutral<<' '<<c.colorKeep<<' '<<c.lumaKeep<<' '<<c.shadow<<' '<<c.autoAmount;
}
inline bool readNrCorrection(std::istream& in,NrCorrectionSettings& c,bool extended=false){
    c={};
    int enabled,automatic;
    if(!(in>>enabled>>automatic>>c.hue>>c.chroma>>c.highlight>>c.compression>>c.stability)||
       enabled<0||enabled>1||automatic<0||automatic>1)return false;
    if(extended&&!(in>>c.neutral>>c.colorKeep>>c.lumaKeep>>c.shadow>>c.autoAmount))return false;
    if(!c.valid())return false;
    c.enabled=enabled!=0;c.automatic=automatic!=0;return true;
}
}
