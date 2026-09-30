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
    bool operator==(const PreviewView&)const=default;
    double aspect(float iw,float ih)const{
        if(mode==4)return 16.0/9;if(mode==5)return 4.0/3;if(mode==6)return 21.0/9;
        return mode!=1&&std::isfinite(displayAspect)&&displayAspect>0?displayAspect:ih>0?double(iw)/ih:0;
    }
    std::pair<float,float> renderedSize(float cw,float ch,float iw,float ih)const{
        if(cw<=0||ch<=0||iw<=0||ih<=0)return {0,0};
        if(mode==1)return {iw*zoom,ih*zoom};
        if(mode==3)return {cw*zoom,ch*zoom};
        const float virtualW=float(aspect(iw,ih))*ih;
        const float scale=(mode==2?std::max(cw/virtualW,ch/ih):std::min(cw/virtualW,ch/ih))*zoom;
        return {virtualW*scale,ih*scale};
    }
    std::pair<float,float> sourcePoint(float x,float y,float cw,float ch,float iw,float ih)const{
        if(cw<=0||ch<=0||iw<=0||ih<=0)return {-1.0f,-1.0f};
        const auto [rw,rh]=renderedSize(cw,ch,iw,ih);
        return {centerX+(x-cw*.5f)/rw,centerY+(y-ch*.5f)/rh};
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
