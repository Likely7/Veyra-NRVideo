#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <istream>
#include <ostream>
#include <utility>

namespace veyra::engine {
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
