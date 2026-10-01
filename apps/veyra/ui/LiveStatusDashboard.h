#pragma once
#include <deque>
#include <optional>
#include "LiveStatusHistory.h"

namespace veyra::ui::live_status {
inline void paintDashboard(HWND h,HDC dc,int w,int height,const engine::PlayerSnapshot& s,const DashboardHistory& history,bool advanced=false){
    const auto& f=s.metrics.flow;const bool active=s.running&&!s.image&&s.transport==engine::TransportState::Playing;
    auto text=[&](std::wstring str,int x,int y,int width,int ht,int size,COLORREF color){chromeText(dc,h,str,x,y,width,ht,size,color);};
    auto card=[&](int x,int y,int width,int ht){
        AlphaGraphics draw(dc);auto& g=draw.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath p;const float r=float(dip(h,12)),xx=float(dip(h,x)),yy=float(dip(h,y)),ww=float(dip(h,width)),hh=float(dip(h,ht));
        p.AddArc(xx,yy,r,r,180,90);p.AddArc(xx+ww-r,yy,r,r,270,90);p.AddArc(xx+ww-r,yy+hh-r,r,r,0,90);p.AddArc(xx,yy+hh-r,r,r,90,90);p.CloseFigure();
        Gdiplus::SolidBrush bg(Gdiplus::Color(155,29,32,32));g.FillPath(&bg,&p);
        Gdiplus::Pen edge(Gdiplus::Color(40,132,145,140),1);g.DrawPath(&edge,&p);
    };
    text(L"Live Processing Status",12,7,w-100,24,12,textColor);
    const int gap=6,left=10,cw=(w-20-gap*3)/4,top=38;
    const diagnostics::GpuStage stages[]={diagnostics::GpuStage::Flow,diagnostics::GpuStage::Nr,diagnostics::GpuStage::Sr,diagnostics::GpuStage::FgBatch};
    const wchar_t* names[]={L"Optical flow time",L"NR time",L"Super res time",L"Frame gen time"};
    for(int i=0;i<4;++i){int x=left+i*(cw+gap);card(x,top,cw,58);text(names[i],x+6,top+7,cw-10,17,9,secondary);
        const auto& a=f.gpuTiming[size_t(stages[i])];std::wstring value=L"—";
        if(active){if(a.mean)value=std::format(L"{:.1f}",*a.mean);else if(s.metrics.gpu[size_t(stages[i])].state==diagnostics::SampleState::NotExecuted)value=(i==3&&s.applied.multiplier>1)?L"Awaiting frame gen":L"Off";}
        // Present-sink FG backends now report an application-side GPU sample;
        // show it instead of a hardcoded placeholder, and only say
        // "sampling" while no sample has arrived yet.
        if(i==3&&s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend)&&!a.mean)value=L"Sampling";
        text(value,x+6,top+27,cw-10,24,14,textColor);
    }
    const int chartTop=104,chartH=std::max(92,height-188),bottom=chartTop+chartH+8,bw=(w-26)/2;
    card(10,chartTop,w-20,chartH);
    const bool xess=s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
    text(advanced?L"Detailed data":xess?L"Enhancement time · excludes display frame gen":L"Total enhancement time",20,chartTop+8,w-100,20,10,secondary);
    text(advanced?L"◂":L"▸",w-38,chartTop+5,24,24,14,secondary);
    if(!advanced){
    const auto extra=active?f.enhancementProcessing.mean:std::optional<double>{};
    text(extra?std::format(L"{:.1f}",*extra):L"—",w-103,chartTop+30,85,30,23,textColor);
    text(L"ms · avg over last 1s",w-103,chartTop+62,85,18,9,secondary);
    const int x0=20,x1=w-117,y0=chartTop+35,y1=chartTop+chartH-24;
    if(x1>x0&&y1>y0){
        double peak=10;for(const auto& v:history.points)if(v)peak=std::max(peak,*v*1.15);
        {AlphaGraphics draw(dc);auto& g=draw.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen grid(Gdiplus::Color(50,150,160,155),1),line(Gdiplus::Color(240,203,219,210),float(dip(h,1)));
        grid.SetDashStyle(Gdiplus::DashStyleDash);for(int i=0;i<3;++i){int y=y0+(y1-y0)*i/2;g.DrawLine(&grid,dip(h,x0),dip(h,y),dip(h,x1),dip(h,y));}
        std::optional<Gdiplus::PointF> prev;size_t index=120-history.points.size();
        for(const auto& v:history.points){const float x=float(dip(h,x0))+float(dip(h,x1-x0))*float(index++)/119;
            if(!v){prev.reset();continue;}Gdiplus::PointF p(x,float(dip(h,y1))-float(dip(h,y1-y0))*float(*v/peak));if(prev)g.DrawLine(&line,*prev,p);prev=p;}}
        text(L"30s ago",x0,y1+3,65,16,8,secondary);text(L"Now",x1-28,y1+3,28,16,8,secondary);
    }
    }
    card(10,bottom,bw,58);card(16+bw,bottom,bw,58);
    text(L"Frames pending output",20,bottom+6,bw-20,18,10,secondary);
    text(std::format(L"{} frames{}",active?f.pendingOutputFrames:0,xess?L" *":L""),20,bottom+27,bw-20,23,17,textColor);
    std::wstring status=L"Idle";COLORREF color=RGB(145,151,149);
    if(s.failed){status=L"Error";color=RGB(245,86,86);}
    else if(s.remoteRecovering){status=L"Recovering";color=RGB(242,185,65);}
    else if(active&&!s.applying&&f.rateWindowReady){status=history.rateStatus(s);color=status!=L"Normal"?RGB(242,185,65):RGB(111,211,127);}
    else if(s.applying&&s.running)status=L"Adjusting";else if(active)status=L"Sampling";else if(s.transport==engine::TransportState::Paused)status=L"Paused";
    text(L"Current status",26+bw,bottom+6,bw-20,18,10,secondary);
    {AlphaGraphics draw(dc);Gdiplus::SolidBrush dot(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));draw.get().FillEllipse(&dot,dip(h,27+bw),dip(h,bottom+35),dip(h,8),dip(h,8));}
    text(status,42+bw,bottom+27,bw-38,23,status.size()>5?11:16,textColor);
    text(xess?L"* Frame gen time is app-side timing, excludes the provider's internal interpolation":L"Enhancement-stage GPU timing · excludes audio and present wait",12,height-18,w-24,16,8,secondary);
}
}
