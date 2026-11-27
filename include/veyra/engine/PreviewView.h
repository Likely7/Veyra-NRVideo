#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
namespace veyra::engine {
struct PreviewView {
    float zoom=1,centerX=.5f,centerY=.5f;
    // Preserve UI IDs 0=fit, 1=native pixels, 2=fill; append stretch/ratios.
    int mode=0;
    double displayAspect=0;
    // Container display rotation, clockwise. View centers live in the oriented
    // picture; processing/protection and estimated motion keep coded coordinates.
    int rotationDegrees=0;
    // Session-local user offset; source DAR already includes container rotation.
    unsigned userQuarterTurns=0;
    static constexpr int kAdaptiveWideMode=7;
    int adaptivePreservation=87,adaptiveZoom=0;
    unsigned quarterTurns()const {
        const int angle=((rotationDegrees%360)+360)%360;
        return ((angle%90==0?unsigned(angle/90):0)+userQuarterTurns)%4;
    }
    void rotateClockwise(){userQuarterTurns=(userQuarterTurns+1)%4;zoom=1;centerX=centerY=.5f;}
    std::pair<float,float> sourceUv(float u,float v)const {
        switch(quarterTurns()){
        case 1:return {v,1-u};case 2:return {1-u,1-v};case 3:return {1-v,u};default:return {u,v};
        }
    }
    std::pair<float,float> displayUv(float u,float v)const {
        switch(quarterTurns()){
        case 1:return {1-v,u};case 2:return {1-u,1-v};case 3:return {v,1-u};default:return {u,v};
        }
    }
    bool operator==(const PreviewView&)const=default;
    double aspect(float iw,float ih)const{
        const auto oriented=[this](double a){return userQuarterTurns&1?1/a:a;};
        if(mode==4)return oriented(16.0/9);if(mode==5)return oriented(4.0/3);if(mode==6)return oriented(21.0/9);
        if(quarterTurns()&1)std::swap(iw,ih);
        return mode!=1&&std::isfinite(displayAspect)&&displayAspect>0?oriented(displayAspect):ih>0?double(iw)/ih:0;
    }
    // Wider displays get a gentler horizontal stretch around the center.
    // The endpoint-preserving map's derivative at the center is this ratio.
    float adaptiveWidthRatio(float cw,float ch,float iw,float ih)const{
        if(mode!=kAdaptiveWideMode||ch<=0)return 1.0f;
        const double sourceWidth=aspect(iw,ih)*ch;
        if(sourceWidth<=0||cw<=sourceWidth)return 1.0f;
        return std::lerp(1.0f,float(std::min(cw/sourceWidth,3.0)),adaptivePreservation/100.0f);
    }
    // Reciprocal ratios give exact inverse maps. Extend linearly outside the
    // image so crop and overlay hit tests still recognize the black bars.
    static float remapWidth(float u,float ratio){
        if(ratio==1.0f)return u;
        const float x=2*u-1;
        const float a=std::min(std::abs(x),1.0f);
        return .5f+.5f*x*ratio/std::sqrt(1+(ratio*ratio-1)*a*a);
    }
    std::pair<float,float> renderedSize(float cw,float ch,float iw,float ih)const{
        if(cw<=0||ch<=0||iw<=0||ih<=0)return {0,0};
        const float orientedH=(quarterTurns()&1)?iw:ih;
        if(mode==1)return {(quarterTurns()&1?ih:iw)*zoom,orientedH*zoom};
        if(mode==3)return {cw*zoom,ch*zoom};
        const float virtualW=float(aspect(iw,ih))*orientedH;
        const float viewZoom=mode==kAdaptiveWideMode?zoom*(1.0f+adaptiveZoom*.01f):zoom;
        if(mode==kAdaptiveWideMode&&cw*orientedH>ch*virtualW)return {cw*viewZoom,ch*viewZoom};
        const float scale=(mode==2?std::max(cw/virtualW,ch/orientedH):std::min(cw/virtualW,ch/orientedH))*viewZoom;
        return {virtualW*scale,orientedH*scale};
    }
    std::pair<float,float> sourcePoint(float x,float y,float cw,float ch,float iw,float ih)const{
        if(cw<=0||ch<=0||iw<=0||ih<=0)return {-1.0f,-1.0f};
        const auto [rw,rh]=renderedSize(cw,ch,iw,ih);
        const float u=centerX+(x-cw*.5f)/rw;
        return sourceUv(remapWidth(u,adaptiveWidthRatio(cw,ch,iw,ih)),centerY+(y-ch*.5f)/rh);
    }
    std::pair<float,float> displayPoint(float u,float v,float cw,float ch,float iw,float ih)const{
        const auto [rw,rh]=renderedSize(cw,ch,iw,ih);
        const auto uv=displayUv(u,v);
        const float x=remapWidth(uv.first,1.0f/adaptiveWidthRatio(cw,ch,iw,ih));
        return {cw*.5f+(x-centerX)*rw,ch*.5f+(uv.second-centerY)*rh};
    }
    void wheel(float steps,float x,float y,float clientW,float clientH,float imageW,float imageH){
        if(clientW<=0||clientH<=0||imageW<=0||imageH<=0||!std::isfinite(steps))return;
        const auto [rw,rh]=renderedSize(clientW,clientH,imageW,imageH);
        const float next=std::clamp(zoom*std::pow(1.2f,steps),.05f,64.0f);
        centerX+=(x-clientW*.5f)/rw*(1-zoom/next);
        centerY+=(y-clientH*.5f)/rh*(1-zoom/next);
        zoom=next;
    }
    void pan(float dx,float dy,float clientW,float clientH,float imageW,float imageH){
        if(clientW<=0||clientH<=0||imageW<=0||imageH<=0)return;
        const auto [rw,rh]=renderedSize(clientW,clientH,imageW,imageH);
        centerX-=dx/rw;centerY-=dy/rh;
    }
};
}
