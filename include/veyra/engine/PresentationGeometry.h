#pragma once
#include "veyra/engine/PreviewView.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace veyra::engine {
// The QML cinema window rounds the film height to a DIP, then Qt rounds its
// HWND to physical pixels. Only that auto-fitted window supplies this bounded
// allowance (height pixels * 1024); other hosts keep ordinary contain.
inline constexpr wchar_t kFilmPixelRoundingProperty[]=L"Veyra.FilmPixelRounding";
inline PreviewView filmPixelAlignedView(HWND window,unsigned clientWidth,unsigned clientHeight,
    unsigned imageWidth,unsigned imageHeight,PreviewView view){
    const auto units=reinterpret_cast<uintptr_t>(GetPropW(window,kFilmPixelRoundingProperty));
    if(!units||!clientWidth||!clientHeight||!imageWidth||!imageHeight||
       view.mode!=0||view.zoom!=1||view.centerX!=.5f||view.centerY!=.5f)return view;
    const double aspect=view.aspect(float(imageWidth),float(imageHeight));
    if(aspect>0&&std::abs(double(clientWidth)/aspect-clientHeight)<=double(units)/1024.0){
        // Map the complete source to the integral client, including provider
        // guidance. This removes only the rounding residue, without cropping.
        view.mode=3;
    }
    return view;
}
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
