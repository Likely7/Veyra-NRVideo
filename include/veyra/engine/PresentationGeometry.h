#pragma once
#include "veyra/engine/PreviewView.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace veyra::engine {
// PresentBlit fits in client coordinates; DXGI stretches the buffer to that
// client. Provider color rectangles must use the inverse of the same mapping.
inline RECT presentationRegion(unsigned imageWidth,unsigned imageHeight,
    unsigned clientWidth,unsigned clientHeight,unsigned bufferWidth,unsigned bufferHeight,PreviewView view){
    if(!imageWidth||!imageHeight||!clientWidth||!clientHeight||bufferWidth<2||bufferHeight<2)return {};
    const auto [rw,rh]=view.renderedSize(float(clientWidth),float(clientHeight),float(imageWidth),float(imageHeight));
    const float sx=float(bufferWidth)/clientWidth,sy=float(bufferHeight)/clientHeight;
    const float left=(clientWidth*.5f-view.centerX*rw)*sx;
    const float top=(clientHeight*.5f-view.centerY*rh)*sy;
    const auto edge=[](float value,unsigned bound){return LONG(std::clamp(value,0.0f,float(bound)))&~1L;};
    return {edge(left,bufferWidth),edge(top,bufferHeight),
        edge(left+rw*sx+.5f,bufferWidth),edge(top+rh*sy+.5f,bufferHeight)};
}
}
