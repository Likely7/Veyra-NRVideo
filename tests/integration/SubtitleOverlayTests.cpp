#include <windows.h>
#include <vector>
#include <iostream>
static std::vector<unsigned long> pixels;
static int imageWidth=0,imageHeight=0,updates=0;
static BOOL captureLayer(HWND,HDC,POINT*,SIZE* size,HDC source,POINT*,COLORREF,BLENDFUNCTION*,DWORD){
    DIBSECTION bitmap{};
    GetObjectW(GetCurrentObject(source,OBJ_BITMAP),sizeof(bitmap),&bitmap);
    imageWidth=size->cx;imageHeight=size->cy;++updates;
    auto* data=static_cast<unsigned long*>(bitmap.dsBm.bmBits);
    pixels.assign(data,data+size_t(imageWidth)*imageHeight);
    return TRUE;
}
#define UpdateLayeredWindow captureLayer
#include "../../apps/veyra/ui/SubtitleOverlay.cpp"
#undef UpdateLayeredWindow

int main(){
    using namespace veyra::ui;
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return 1;
    HWND parent=CreateWindowExW(0,L"STATIC",L"overlay test",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    // Test executables do not carry the app's Windows 8+ manifest, which is
    // required for layered child windows. Use the same renderer in a popup.
    HWND registered=createSubtitleOverlay(parent);if(registered)DestroyWindow(registered);
    HWND overlay=CreateWindowExW(WS_EX_LAYERED,L"VeyraSubtitleOverlay",L"",WS_POPUP,0,0,640,480,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!overlay)return 2;
    int failures=0;
    auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    SubtitleView view;view.outline=false;view.targetLines=0;
    SubtitleLine red,green;red.text=L"First subtitle";green.text=L"Second subtitle";
    red.style.size=green.style.size=28;red.style.primary=0xFFFF0000;green.style.primary=0xFF00FF00;
    for(int alignment:{2,5,8}){
        red.style.alignment=green.style.alignment=alignment;
        updateSubtitleOverlay(overlay,{red,green},view);
        int r0=480,r1=-1,g0=480,g1=-1;
        for(int y=0;y<imageHeight;++y)for(int x=0;x<imageWidth;++x){auto c=pixels[size_t(y)*imageWidth+x];int r=(c>>16)&255,g=(c>>8)&255;
            if(r>30&&r>2*g){r0=std::min(r0,y);r1=std::max(r1,y);}
            if(g>30&&g>2*r){g0=std::min(g0,y);g1=std::max(g1,y);}}
        check(r1>=r0&&g1>=g0&&(r1<g0||g1<r0),"top/middle/bottom cues do not overlap");
        std::cout<<"alignment="<<alignment<<" extent="<<imageWidth<<'x'<<imageHeight<<" red="<<r0<<','<<r1<<" green="<<g0<<','<<g1<<'\n';
        if(alignment==5)check(std::abs((std::min(r0,g0)+std::max(r1,g1))/2-240)<12,"middle group is centered");
    }
    // Compare real glyph raster heights, not just the setting used by the renderer.
    SubtitleLine stable;stable.text=L"HHHH";stable.style.size=28;stable.style.shadow=0;
    view.targetLines=1;
    auto inkBands=[&](){std::vector<int> heights;int run=0;for(int y=0;y<imageHeight;++y){bool ink=false;for(int x=0;x<imageWidth;++x)ink|=(pixels[size_t(y)*imageWidth+x]>>24)>30;if(ink)++run;else if(run){heights.push_back(run);run=0;}}if(run)heights.push_back(run);return heights;};
    updateSubtitleOverlay(overlay,{stable},view);const auto one=inkBands();
    stable.text=L"HHHH\nHHHH";updateSubtitleOverlay(overlay,{stable},view);const auto two=inkBands();
    check(one.size()==1&&two.size()==2&&std::abs(one[0]-two[0])<=1&&std::abs(one[0]-two[1])<=1,"two-line cue preserves one-line glyph size even with target one");
    stable.text=L"HHHH, HHHH\nHHHH, HHHH\nHHHH, HHHH";
    updateSubtitleOverlay(overlay,{stable},view);
    check(inkBands().size()==3,"three authored lines with commas survive the real glyph renderer");
    stable.text=L"HHHH\nHHHH";
    view.fitToLines=true;updateSubtitleOverlay(overlay,{stable},view);const auto fitted=inkBands();
    check(fitted.size()==2&&fitted[0]<two[0],"explicit fit-to-lines still shrinks text");
    view.fitToLines=false;
    RECT rect{0,0,640,480};
    const auto original=signatureOf({red},view,rect);
    auto changes=[&](auto change){auto copy=red;change(copy.style);check(signatureOf({copy},view,rect)!=original,"style mutation invalidates cache");};
    changes([](auto& s){s.bold=true;});changes([](auto& s){s.italic=true;});
    changes([](auto& s){s.marginL=10;});changes([](auto& s){s.marginR=10;});changes([](auto& s){s.marginV=10;});
    changes([](auto& s){s.back=0xFF112233;});changes([](auto& s){s.background=true;});
    changes([](auto& s){s.outlineWidth=3;});changes([](auto& s){s.shadow=3;});changes([](auto& s){s.size+=.001;});
    updateSubtitleOverlay(overlay,{red},view);auto before=pixels;int count=updates;
    updateSubtitleOverlay(overlay,{red},view);check(updates==count,"unchanged cues reuse cache");
    red.style.bold=true;updateSubtitleOverlay(overlay,{red},view);
    check(updates==count+1&&pixels!=before,"bold change repaints actual raster");
    DestroyWindow(overlay);DestroyWindow(parent);Gdiplus::GdiplusShutdown(token);
    return failures?1:0;
}
