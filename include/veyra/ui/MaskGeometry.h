// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/PreviewView.h"
#include <array>
namespace veyra::ui::mask {
inline bool contains(const engine::ProtectionRect& r,float u,float v) {
    if(r.empty()||u<r.left||u>r.right||v<r.top||v>r.bottom)return false;
    if(!r.ellipse)return true;
    const float x=(2*u-r.left-r.right)/(r.right-r.left),y=(2*v-r.top-r.bottom)/(r.bottom-r.top);
    return x*x+y*y<=1;
}
inline engine::ProtectionRect moved(engine::ProtectionRect r,float du,float dv) {
    const auto clampShift=[](float delta,float lo,float hi){
        // Imported/tiled ellipses may extend beyond the frame. Preserve their size.
        if(hi-lo>1)return std::clamp((lo+hi)*.5f+delta,0.0f,1.0f)-(lo+hi)*.5f;
        return std::clamp(delta,-lo,1-hi);
    };
    du=clampShift(du,r.left,r.right);dv=clampShift(dv,r.top,r.bottom);
    r.left+=du;r.right+=du;r.top+=dv;r.bottom+=dv;return r;
}
// Coded source coordinates -> oriented, zoomed host pixels, inverse of sourcePoint.
inline std::array<float,4> hostBounds(const engine::ProtectionRect& r,const engine::PreviewView& view,
                                     float cw,float ch,float iw,float ih) {
    const auto [rw,rh]=view.renderedSize(cw,ch,iw,ih);
    const auto a=view.displayUv(r.left,r.top),b=view.displayUv(r.right,r.bottom);
    const float x0=cw*.5f+(a.first-view.centerX)*rw,x1=cw*.5f+(b.first-view.centerX)*rw;
    const float y0=ch*.5f+(a.second-view.centerY)*rh,y1=ch*.5f+(b.second-view.centerY)*rh;
    return {std::min(x0,x1),std::min(y0,y1),std::max(x0,x1),std::max(y0,y1)};
}
}
