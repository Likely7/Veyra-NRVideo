#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <memory>
#include <format>
#include "veyra/Log.h"
namespace veyra::ui {
// Explicit premultiplied BGRA is required for DWM glass. Compatible GDI
// bitmaps lose alpha and turn transparent controls into solid rectangles.
class AlphaRaster {
    HBITMAP bitmap_=nullptr;HGDIOBJ previous_=nullptr;
public:
    HDC dc=nullptr;uint32_t* pixels=nullptr;int width=0,height=0;
    AlphaRaster()=default;
    AlphaRaster(const AlphaRaster&)=delete;
    ~AlphaRaster(){clear();}
    void clear(){if(dc&&previous_)SelectObject(dc,previous_);if(bitmap_)DeleteObject(bitmap_);if(dc)DeleteDC(dc);dc=nullptr;bitmap_=nullptr;previous_=nullptr;pixels=nullptr;}
    bool create(HDC target,int w,int h){clear();width=std::max(1,w);height=std::max(1,h);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;dc=CreateCompatibleDC(target);bitmap_=CreateDIBSection(target,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);if(!dc||!bitmap_){clear();return false;}previous_=SelectObject(dc,bitmap_);if(!previous_||previous_==HGDI_ERROR){previous_=nullptr;clear();return false;}std::fill_n(pixels,size_t(width)*height,0);return true;}
};
class AlphaGraphics {
    std::unique_ptr<Gdiplus::Bitmap> bitmap_;
    std::unique_ptr<Gdiplus::Graphics> graphics_;
public:
    explicit AlphaGraphics(HDC dc){DIBSECTION dib{};if(GetObjectW(GetCurrentObject(dc,OBJ_BITMAP),sizeof(dib),&dib)==sizeof(dib)&&dib.dsBm.bmBits&&dib.dsBm.bmBitsPixel==32){GdiFlush();bitmap_=std::make_unique<Gdiplus::Bitmap>(dib.dsBm.bmWidth,dib.dsBm.bmHeight,dib.dsBm.bmWidthBytes,PixelFormat32bppPARGB,static_cast<BYTE*>(dib.dsBm.bmBits));graphics_=std::make_unique<Gdiplus::Graphics>(bitmap_.get());}else graphics_=std::make_unique<Gdiplus::Graphics>(dc);}
    Gdiplus::Graphics& get(){return *graphics_;}
};
inline void opaqueBlack(HDC dc,RECT r){AlphaGraphics paint(dc);auto& g=paint.get();g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);Gdiplus::SolidBrush black(Gdiplus::Color(255,0,0,0));g.FillRectangle(&black,Gdiplus::Rect(r.left,r.top,r.right-r.left,r.bottom-r.top));}
struct GlassPane {
    RECT rect{};int radius=20;BYTE tint=36;
    bool operator==(const GlassPane& other)const{return EqualRect(&rect,&other.rect)&&radius==other.radius&&tint==other.tint;}
};
class GlassBackdrop {
    HWND window_=nullptr;bool active_=false,full_=false;int width_=0,height_=0;
    std::vector<GlassPane> panes_;
    static void path(Gdiplus::GraphicsPath& p,const RECT& r,int radius){const float d=float(std::min({radius*2,int(r.right-r.left-1),int(r.bottom-r.top-1)}));float x=float(r.left),y=float(r.top),w=float(r.right-r.left-1),h=float(r.bottom-r.top-1);p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();}
public:
    uint64_t revision=0;
    void configure(){if(!window_)return;BOOL dark=TRUE;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));const DWORD material=full_?1:3;const auto hr=DwmSetWindowAttribute(window_,38,&material,sizeof(material));MARGINS margins{full_?0:-1,0,0,0};const auto frame=DwmExtendFrameIntoClientArea(window_,&margins);active_=!full_&&SUCCEEDED(hr)&&SUCCEEDED(frame);veyra::log::info("ui-glass",std::format("system=DesktopAcrylic backdropHr=0x{:08X} frameHr=0x{:08X} apiAccepted={} fullscreen={} video=opaque",uint32_t(hr),uint32_t(frame),active_,full_));}
    void attach(HWND window){window_=window;SetPropW(window,L"Veyra.GlassBackdrop",this);configure();}
    void detach(){if(window_)RemovePropW(window_,L"Veyra.GlassBackdrop");window_=nullptr;}
    bool render(int width,int height,bool,bool full,const std::vector<GlassPane>& panes){if(full_!=full){full_=full;configure();}if(width==width_&&height==height_&&panes_==panes)return true;width_=width;height_=height;panes_=panes;++revision;return true;}
    bool copy(HDC target,HWND child,RECT area)const{
        if(!window_)return false;POINT origin{};MapWindowPoints(child,window_,&origin,1);AlphaGraphics paint(target);auto& g=paint.get();g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);g.SetClip(Gdiplus::Rect(area.left,area.top,area.right-area.left,area.bottom-area.top));
        // Fullscreen and the daily immersive overlay have no backdrop material
        // (nothing behind the window to blur), so the panes take the same solid
        // fallback the windowed glass uses when transparency is off: one
        // panel colour and one rim line, instead of a bare base fill.
        Gdiplus::SolidBrush base(Gdiplus::Color(active_?26:255,10,13,17));g.FillRectangle(&base,Gdiplus::Rect(area.left,area.top,area.right-area.left,area.bottom-area.top));
        g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.TranslateTransform(float(-origin.x),float(-origin.y));RECT rootArea=area;OffsetRect(&rootArea,origin.x,origin.y);
        for(const auto& pane:panes_){RECT overlap{};if(!IntersectRect(&overlap,&rootArea,&pane.rect)||pane.rect.right-pane.rect.left<3||pane.rect.bottom-pane.rect.top<3)continue;Gdiplus::GraphicsPath outline;path(outline,pane.rect,pane.radius);Gdiplus::SolidBrush tint(Gdiplus::Color(active_?pane.tint:BYTE(255),30,33,38));g.FillPath(&tint,&outline);Gdiplus::Pen rim(Gdiplus::Color(30,229,236,243),1);g.DrawPath(&rim,&outline);}
        return true;
    }
};
inline GlassBackdrop* glassBackdrop(HWND child){return reinterpret_cast<GlassBackdrop*>(GetPropW(GetAncestor(child,GA_ROOT),L"Veyra.GlassBackdrop"));}
inline bool copyGlass(HDC dc,RECT rect,HWND child){auto p=glassBackdrop(child);return p&&p->copy(dc,child,rect);}
}
