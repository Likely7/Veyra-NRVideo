#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <istream>
#include <ostream>
#include <utility>

namespace veyra::engine {
inline unsigned exclusionModes(const ProtectionSettings& value) {
    unsigned modes=0;
    for(unsigned i=0;i<value.regions.size();++i)modes|=unsigned(value.regions[i].content)<<(2*i);
    return modes;
}
inline bool readExclusionModes(std::istream& in,ProtectionSettings& value) {
    unsigned modes=0;
    if(!(in>>modes)||modes>0xff)return false;
    for(auto& region:value.regions){
        const auto mode=ExclusionContent(modes&3);
        if(mode>ExclusionContent::Hud)return false;
        region.content=mode;
        modes>>=2;
    }
    return true;
}

inline void writeProtection(std::ostream& out,const ProtectionSettings& value) {
    out<<value.enabled<<' '<<value.featherPixels;
    for(const auto& region:value.regions){
        const auto encoded=protectionConstants(region);
        for(float coordinate:encoded)out<<' '<<coordinate;
    }
}
inline bool readProtection(std::istream& in,ProtectionSettings& value) {
    ProtectionSettings next;
    int enabled=0;
    if(!(in>>enabled>>next.featherPixels)||enabled<0||enabled>1)return false;
    next.enabled=enabled!=0;
    for(auto& region:next.regions){
        if(!(in>>region.left>>region.top>>region.right>>region.bottom))return false;
        if(region.left>region.right){std::swap(region.left,region.right);region.ellipse=true;}
    }
    if(!next.validate().empty())return false;
    value=next;
    return true;
}
}
