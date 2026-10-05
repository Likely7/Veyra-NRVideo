#include "SubtitleOverlay.h"

#include "Theme.h"
#include "veyra/Log.h"

#ifdef VEYRA_HAS_LIBASS
#include "AssSubtitleRenderer.h"
#include <array>
#endif

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace veyra::ui {
namespace {
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}

int toFontStyle(bool bold,bool italic){
    int style=Gdiplus::FontStyleRegular;
    if(bold)style|=Gdiplus::FontStyleBold;
    if(italic)style|=Gdiplus::FontStyleItalic;
    return style;
}
// numpad layout: column 1/2/3 = left/centre/right, row 1/2/3 = bottom/middle/top
Gdiplus::StringAlignment horizontalOf(int alignment){const int column=(alignment-1)%3;return column==0?Gdiplus::StringAlignmentNear:column==2?Gdiplus::StringAlignmentFar:Gdiplus::StringAlignmentCenter;}
Gdiplus::StringAlignment verticalOf(int alignment){const int row=(alignment-1)/3;return row==0?Gdiplus::StringAlignmentFar:row==2?Gdiplus::StringAlignmentNear:Gdiplus::StringAlignmentCenter;}

std::wstring signatureOf(const std::vector<SubtitleLine>& lines,const SubtitleView& view,const RECT& rect){
    std::wstring signature;
    signature.reserve(lines.size()*64);
    for(const auto& line:lines){
        signature+=std::format(L"{}:{}|{:08X}|{:08X}|{}:{}|{}|{}|{}|{}|",line.text.size(),line.text,line.style.primary,line.style.outline,line.style.font.size(),line.style.font,
            line.style.size,line.style.alignment,line.alignOverride,line.secondary?1:0);
        signature+=std::format(L"{}:{}:{}:{}|",reinterpret_cast<uintptr_t>(line.bitmap.get()),reinterpret_cast<uintptr_t>(line.ass.get()),line.posX,line.posY);
        const auto& s=line.style;
        signature+=std::format(L"{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|",s.back,s.outlineWidth,s.shadow,s.marginL,s.marginR,s.marginV,s.bold,s.italic,s.background,s.font.size());
    }
    signature+=std::format(L"#{}|{}:{}|{}|{}|{}|{}",view.scale,view.fontOverride.size(),view.fontOverride,view.outline?1:0,view.background?1:0,view.bottomMargin,view.blockGap);
    signature+=std::format(L"/{}x{}/{}/{}",rect.right,rect.bottom,view.targetLines,view.fitToLines);
    signature+=std::format(L"/{}/{}/{}/{}/{}",view.preview.zoom,view.preview.centerX,view.preview.centerY,view.videoWidth,view.videoHeight);
    return signature;
}

struct Line {
    std::unique_ptr<Gdiplus::GraphicsPath> path;
    Gdiplus::RectF bounds{};
    engine::SubtitleStyle style;
    bool positioned=false;
    float positionX=0,positionY=0;
};
} // namespace

HWND createSubtitleOverlay(HWND parent){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraSubtitleOverlay";RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,nullptr);
}

void updateSubtitleOverlay(HWND h,const std::vector<SubtitleLine>& lines,const SubtitleView& view){
    RECT rect{};GetClientRect(h,&rect);
    static thread_local std::wstring lastSignature;
    static thread_local HWND lastWindow=nullptr;
    static thread_local std::vector<std::shared_ptr<const engine::SubtitleBitmapFrame>> lastBitmaps;
    if(h!=lastWindow){lastSignature.clear();lastBitmaps.clear();lastWindow=h;}
    if(lines.empty()||rect.right<1||rect.bottom<1){lastSignature.clear();lastBitmaps.clear();ShowWindow(h,SW_HIDE);SetWindowTextW(h,L"");return;}
    const int width=rect.right,height=rect.bottom;
    auto signature=signatureOf(lines,view,rect)+std::format(L"/dpi{}",GetDpiForWindow(h));
#ifdef VEYRA_HAS_LIBASS
    // libass decides whether its frame changed (animation, karaoke, new
    // events); the rest of the signature covers geometry and other lines.
    static thread_local std::array<std::unique_ptr<AssSubtitleRenderer>,2> assRenderers;
    static thread_local uint64_t assGeneration=0;
    struct AssPlacement {AssSubtitleRenderer* renderer=nullptr;int x=0,y=0;};
    std::vector<AssPlacement> assPlaced;
    for(const auto& line:lines){
        if(!line.ass)continue;
        auto& renderer=assRenderers[line.secondary?1:0];
        if(!renderer)renderer=std::make_unique<AssSubtitleRenderer>();
        if(!renderer->prepare(line.ass))continue;
        const double vw=view.videoWidth>0?view.videoWidth:width,vh=view.videoHeight>0?view.videoHeight:height;
        const double fit=std::min(width/vw,height/vh)*view.preview.zoom;
        const int frameW=int(std::lround(vw*fit)),frameH=int(std::lround(vh*fit));
        const int ox=int(std::lround(width*.5-view.preview.centerX*vw*fit)),oy=int(std::lround(height*.5-view.preview.centerY*vh*fit));
        const int change=renderer->render(int64_t(std::llround(line.assTimeMs)),frameW,frameH,int(vw),int(vh),view.scale);
        if(change>0)++assGeneration;
        signature+=std::format(L"/ass{}:{}:{}x{}@{},{}#{}",line.secondary?1:0,change>=0,frameW,frameH,ox,oy,assGeneration);
        if(change>=0)assPlaced.push_back({renderer.get(),ox,oy});
    }
#endif
    if(lastSignature==signature){ShowWindow(h,SW_SHOWNOACTIVATE);return;}
    // The DIB is reused while the overlay size is unchanged; zoom/drag only
    // re-renders into it (a 4K DIB is ~33 MB, re-allocating it per tick stalled
    // middle-button drags).
    static thread_local HBITMAP cachedBitmap=nullptr;static thread_local void* cachedBits=nullptr;static thread_local int cachedW=0,cachedH=0;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    if(!cachedBitmap||cachedW!=width||cachedH!=height){
        if(cachedBitmap){DeleteObject(cachedBitmap);cachedBitmap=nullptr;cachedBits=nullptr;}
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        cachedBitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&cachedBits,nullptr,0);
        if(!cachedBitmap||!cachedBits){if(cachedBitmap)DeleteObject(cachedBitmap);cachedBitmap=nullptr;cachedBits=nullptr;DeleteDC(memory);ReleaseDC(nullptr,screen);return;}
        cachedW=width;cachedH=height;
    }
    auto bitmap=cachedBitmap;void* bits=cachedBits;
    auto previousObject=SelectObject(memory,bitmap);
    memset(bits,0,size_t(width)*height*4);
    {
        using namespace Gdiplus;
        Bitmap canvas(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
        Graphics graphics(&canvas);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const float padding=float(dip(h,6));
        const float layoutWidth=std::max(1.0f,float(width)-2*padding);
        FontFamily fallbackFamily(L"Microsoft YaHei UI");   // per call: never survives GdiplusShutdown
        std::vector<Line> laid;
        laid.reserve(lines.size());
        for(const auto& line:lines){
            if(line.ass)continue;   // drawn by libass below (or skipped in builds without it)
            if(line.bitmap){
                const auto& frame=*line.bitmap;
                const double vw=view.videoWidth>0?view.videoWidth:frame.width,vh=view.videoHeight>0?view.videoHeight:frame.height;
                if(frame.width<=0||frame.height<=0||vw<=0||vh<=0)continue;
                const double fit=std::min(width/vw,height/vh)*view.preview.zoom;
                const double sx=vw*fit/frame.width,sy=vh*fit/frame.height;
                const double ox=width*.5-view.preview.centerX*vw*fit,oy=height*.5-view.preview.centerY*vh*fit;
                graphics.SetInterpolationMode(InterpolationModeHighQualityBilinear);
                for(const auto& image:frame.images){
                    auto pixels=image.pixels();if(pixels.empty())continue;
                    Bitmap source(image.width,image.height,image.width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(pixels.data()));
                    graphics.DrawImage(&source,RectF(float(ox+image.x*sx),float(oy+image.y*sy),float(image.width*sx),float(image.height*sy)),0,0,float(image.width),float(image.height),UnitPixel);
                }
                continue;
            }
            if(line.text.empty())continue;
            Line entry;
            entry.path=std::make_unique<Gdiplus::GraphicsPath>();
            entry.style=line.style;
            if(line.alignOverride>=1&&line.alignOverride<=9)entry.style.alignment=line.alignOverride;
            if(entry.style.alignment<1||entry.style.alignment>9)entry.style.alignment=2;
            const double logicalSize=std::max(8.0,(line.style.size>0?line.style.size:24.0)*view.scale);
            const std::wstring family=!view.fontOverride.empty()?view.fontOverride:(line.style.font.empty()?L"Microsoft YaHei UI":line.style.font);
            FontFamily requested(family.c_str());
            FontFamily* use=requested.IsAvailable()?&requested:&fallbackFamily;
            if(!requested.IsAvailable())entry.style.font=L"Microsoft YaHei UI";
            StringFormat format;
            format.SetAlignment(StringAlignmentNear);
            format.SetLineAlignment(StringAlignmentNear);
            format.SetFormatFlags(StringFormatFlagsNoClip|StringFormatFlagsMeasureTrailingSpaces);
            const auto& text=line.text;
            const bool positioned=line.posX>=0&&line.posY>=0;
            const float availableWidth=positioned?layoutWidth:std::max(1.0f,layoutWidth-float(dip(h,std::max(0.0,line.style.marginL)+std::max(0.0,line.style.marginR))));
            float fontPixels=float(dip(h,logicalSize));
            // Measure the complete cue without a height cap. A two-screen
            // rectangle can silently omit the end of a long cue before fitting.
            auto measure=[&](float size){Font font(use,size,toFontStyle(line.style.bold,line.style.italic),UnitPixel);RectF bounds;
                graphics.MeasureString(text.c_str(),int(text.size()),&font,RectF(0,0,availableWidth,1e7f),&format,&bounds);return bounds;};
            if(!positioned&&view.fitToLines&&view.targetLines>0){
                const float minimum=float(dip(h,12));
                for(int attempt=0;attempt<24&&fontPixels>minimum;++attempt){
                    Font font(use,fontPixels,toFontStyle(line.style.bold,line.style.italic),UnitPixel);
                    if(measure(fontPixels).Height<=font.GetHeight(&graphics)*view.targetLines+1)break;
                    fontPixels=std::max(minimum,fontPixels*.9f);
                }
            }
            const auto measured=measure(fontPixels);
            entry.path->AddString(text.c_str(),int(text.size()),use,toFontStyle(line.style.bold,line.style.italic),fontPixels,RectF(0,0,availableWidth,std::max(measured.Height+fontPixels*2,1.0f)),&format);
            entry.path->GetBounds(&entry.bounds);
            if(line.posX>=0&&line.posY>=0){
                entry.positioned=true;
                entry.positionX=float(line.posX)*float(width);
                entry.positionY=float(line.posY)*float(height);
            }
            laid.push_back(std::move(entry));
        }
        // The caller passes secondary lines first, primary last: the loop below
        // lays them out bottom-up, so the primary cue ends up on the bottom.
        float cursor=std::max(padding+1,float(height)-padding-float(dip(h,view.bottomMargin)));
        float totalHeight=0;size_t stacked=0;
        for(const auto& entry:laid)if(!entry.positioned){totalHeight+=entry.bounds.Height;++stacked;}
        const float gap=stacked>1?std::min(float(dip(h,view.blockGap)),std::max(0.0f,(cursor-padding)/float(stacked*2))):0;
        const float space=std::max(1.0f,cursor-padding-gap*float(stacked?stacked-1:0));
        const float scale=totalHeight>space?space/totalHeight:1;
        if(scale<1)for(auto& entry:laid)if(!entry.positioned){Matrix shrink;shrink.Scale(scale,scale);entry.path->Transform(&shrink);entry.path->GetBounds(&entry.bounds);}
        float middleHeight=0;size_t middleCount=0;
        for(const auto& entry:laid)if(!entry.positioned&&verticalOf(entry.style.alignment)==StringAlignmentCenter){middleHeight+=entry.bounds.Height;++middleCount;}
        if(middleCount>1)middleHeight+=gap*float(middleCount-1);
        float topCursor=padding,middleCursor=std::max(padding,(float(height)-middleHeight)/2);
        for(size_t index=laid.size();index-->0;){
            auto& entry=laid[index];
            const int alignment=(entry.style.alignment>=1&&entry.style.alignment<=9)?entry.style.alignment:2;
            const float blockWidth=entry.bounds.Width,blockHeight=entry.bounds.Height;
            float x=padding,y=cursor-blockHeight;
            if(entry.positioned){
                const auto horizontal=horizontalOf(alignment),vertical=verticalOf(alignment);
                x=entry.positionX-(horizontal==StringAlignmentNear?0.0f:horizontal==StringAlignmentFar?blockWidth:blockWidth/2);
                y=entry.positionY-(vertical==StringAlignmentNear?0.0f:vertical==StringAlignmentFar?blockHeight:blockHeight/2);
                y=std::clamp(y,0.0f,std::max(0.0f,float(height)-blockHeight));
                x=std::clamp(x,0.0f,std::max(0.0f,float(width)-blockWidth));
            }else{
                const auto vertical=verticalOf(alignment);
                const float marginV=float(dip(h,std::max(0.0,entry.style.marginV)));
                if(vertical==StringAlignmentNear){y=std::max(topCursor,padding+marginV);topCursor=y+blockHeight+gap;}
                else if(vertical==StringAlignmentCenter){y=middleCursor;middleCursor=y+blockHeight+gap;}
                else{y=std::min(cursor,float(height)-padding-float(dip(h,view.bottomMargin))-marginV)-blockHeight;cursor=y-gap;}
                const float marginL=float(dip(h,entry.style.marginL));
                const float marginR=float(dip(h,entry.style.marginR));
                const float left=padding+marginL,right=float(width)-padding-marginR;
                const auto horizontal=horizontalOf(alignment);
                x=horizontal==StringAlignmentNear?left:horizontal==StringAlignmentFar?right-blockWidth:left+(right-left-blockWidth)/2;
            }
            Matrix translation;
            translation.Translate(x-entry.bounds.X,y-entry.bounds.Y);
            entry.path->Transform(&translation);
            entry.bounds.X=x;entry.bounds.Y=y;
        }
        for(const auto& entry:laid){
            const auto& style=entry.style;
            const Color primary(Color::MakeARGB(byte((style.primary>>24)&0xFF),byte((style.primary>>16)&0xFF),byte((style.primary>>8)&0xFF),byte(style.primary&0xFF)));
            if(view.background||style.background){
                const float inflate=float(dip(h,2));
                RectF box(entry.bounds.X-inflate,entry.bounds.Y-inflate,entry.bounds.Width+2*inflate,entry.bounds.Height+2*inflate);
                SolidBrush back(Color::MakeARGB(byte((style.back>>24)&0xFF),byte((style.back>>16)&0xFF),byte((style.back>>8)&0xFF),byte(style.back&0xFF)));
                graphics.FillRectangle(&back,box);
            }
            const float shadow=float(dip(h,style.shadow>0?style.shadow:(view.outline?1.8:0.0)));
            if(shadow>0.5f){
                GraphicsPath shadowPath;
                shadowPath.AddPath(entry.path.get(),FALSE);
                Matrix offset;
                offset.Translate(shadow,shadow);
                shadowPath.Transform(&offset);
                SolidBrush shadowBrush(Color(150,0,0,0));
                graphics.FillPath(&shadowBrush,&shadowPath);
            }
            if(view.outline){
                Pen outline(Color::MakeARGB(byte((style.outline>>24)&0xFF),byte((style.outline>>16)&0xFF),byte((style.outline>>8)&0xFF),byte(style.outline&0xFF)),
                            float(dip(h,style.outlineWidth>0?style.outlineWidth:2.4)));
                outline.SetLineJoin(LineJoinRound);
                graphics.DrawPath(&outline,entry.path.get());
            }
            SolidBrush fill(primary);
            graphics.FillPath(&fill,entry.path.get());
        }
        graphics.Flush(FlushIntentionSync);
        lastSignature=signature;
        // Retain the identities used by the cache until its signature changes.
        lastBitmaps.clear();
        for(const auto& line:lines)if(line.bitmap)lastBitmaps.push_back(line.bitmap);
    }
#ifdef VEYRA_HAS_LIBASS
    // After GDI+ released the DIB: other subtitle lines are composed already.
    for(const auto& placed:assPlaced)placed.renderer->composite(static_cast<uint32_t*>(bits),width,height,placed.x,placed.y);
#endif
    SIZE dimensions{width,height};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(UpdateLayeredWindow(h,screen,nullptr,&dimensions,memory,&origin,0,&blend,ULW_ALPHA)){
        SetWindowTextW(h,L"subtitle");
        ShowWindow(h,SW_SHOWNOACTIVATE);
        if(!GetPropW(h,L"subtitle.logged")){
            log::info("subtitle",std::format("layered update succeeded extent={}x{} lines={}",width,height,lines.size()));
            SetPropW(h,L"subtitle.logged",HANDLE(1));
        }
    }else{
        lastSignature.clear();
        if(!GetPropW(h,L"subtitle.error")){
            log::error("subtitle",std::format("UpdateLayeredWindow failed error={}",GetLastError()));
            SetPropW(h,L"subtitle.error",HANDLE(1));
        }
    }
    SelectObject(memory,previousObject);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
} // namespace veyra::ui
