#include "TelemetryWindow.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include <shellapi.h>
#include <filesystem>
#include "ui/Theme.h"
#include <sstream>
#include <iomanip>
namespace veyra::ui {
namespace {
unsigned windowDpi=96;HWND window=nullptr;engine::EngineController* engine=nullptr;HFONT font=nullptr;std::wstring preview;
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
std::wstring value(std::optional<double> v){if(!v)return L"not measured";std::wostringstream o;o<<std::fixed<<std::setprecision(3)<<*v;return o.str();}
void refresh(){auto s=engine->snapshot();const wchar_t* names[]={L"Upload/color convert",L"Super resolution SR",L"Optical flow GPU queue span (incl. sync)",L"NR",L"NR delta composite",L"FG subframe 1",L"FG subframe 2",L"FG subframe 3",L"FG batch",L"Final blit"};std::wostringstream o;
    o<<L"GPU timestamps (ms); not-executed is not counted as 0. Sample frame "<<s.metrics.identity.sourceFrameId<<L" / epoch "<<s.metrics.identity.epoch<<L" / settings revision "<<s.metrics.identity.settingsRevision<<L"\r\n";
    for(size_t i=0;i<s.metrics.gpu.size();++i){const auto& g=s.metrics.gpu[i];o<<names[i]<<L": "<<(g.state==diagnostics::SampleState::NotExecuted?L"not executed":g.state==diagnostics::SampleState::Pending?L"awaiting GPU completion":value(g.milliseconds))<<L"\r\n";}
    o<<L"\r\nCPU and scheduling (ms, independent of GPU): frame fetch "<<value(s.metrics.decodeCpuMs)<<L" / graph submit "<<value(s.metrics.submitCpuMs)<<L" / GPU-ready wait "<<value(s.metrics.gpuWaitCpuMs)<<L"\r\nDeadline wait "<<value(s.metrics.deadlineWaitCpuMs)<<L" / Present call "<<value(s.metrics.presentCpuMs)<<L"\r\nSource frames "<<s.metrics.sourceFrames<<L" / valid generated "<<s.metrics.validGenerated<<L" / actually submitted "<<s.metrics.submitted<<L" / expired generated "<<s.metrics.expired<<L"\r\nCurrent batch capacity "<<s.metrics.queueWatermark<<L" (max 4); capture ingress capacity 1; capture dropped "<<s.captureDropped<<L"\r\nActual submission rate (observed over last 1s): "<<value(s.submissionFps)<<L"fps; actual display scan rate / photon latency: not measured\r\n";
    const auto flowName=s.applied.flow==engine::FlowQuality::Performance?L"Performance":s.applied.flow==engine::FlowQuality::Balanced?L"Balanced":L"Quality";
    const auto& f=s.metrics.flow;const auto& c=f.counters;
    if(s.remotePlay){const auto& r=s.remoteStream;
        o<<L"\r\nThis connection request: "<<r.requestedProfile.width<<L"×"<<r.requestedProfile.height<<L" / "<<r.requestedProfile.fps<<L" fps / "<<(r.requestedProfile.codec==remoteplay::Codec::H264?L"H.264":r.requestedProfile.codec==remoteplay::Codec::H265Hdr?L"H.265 HDR":L"H.265")<<L" / "<<r.requestedProfile.bitrateKbps/1000.0<<L" Mbps (not the fixed actual throughput)";
        o<<L"\r\nPS5 full video received "<<r.receivedFps<<L" fps / actually decoded "<<r.decodedFps<<L" fps / effective bitrate "<<r.videoMbps<<L" Mbps"
         <<L"\r\nActual decode path: "<<(!r.decodeConfirmed?L"not yet confirmed":r.hardwareDecode?L"D3D12VA hardware decode":r.decodeFallback?L"CPU software (hardware decode fallback)":L"CPU software")
         <<L"\r\nReal decode mean / P95 "<<value(r.decodeMeanMs)<<L" / "<<value(r.decodeP95Ms)<<L" ms; post-receive wait "<<value(r.ingressWaitMeanMs)
         <<L"\r\nReal / generated presented "<<f.realPresentFps<<L" / "<<f.generatedPresentFps<<L" fps"
         <<L"\r\nKeyframe requests "<<r.video.idrRequests<<L"; compressed queue dropped "<<r.video.dropped<<L"; decode mailbox overwrites "<<s.remotePlaySkipped
         <<L"\r\nLatency scope: full video received -> real-frame Present returns, excluding PS5 render, encode, one-way network and display scan. Live RTT / end-to-end latency not measured.\r\n";
    }
    o<<L"\r\nLast-second pipeline stats: mean / P95 ms, n is sample count\r\n";
    for(size_t i=0;i<f.gpuTiming.size();++i){const auto& a=f.gpuTiming[i];o<<names[i]<<L": "<<value(a.mean)<<L" / "<<value(a.p95)<<L" n="<<a.samples<<L"\r\n";}
    o<<L"\r\nCurrent stats window: session "<<f.latest.sessionId<<L" / settings "<<f.latest.frame.settingsRevision<<L" / epoch "<<f.latest.frame.epoch
     <<L"\r\nValid generated "<<value(f.validGeneratedFps)<<L" fps / Present submitted "<<value(f.presentSubmitFps)<<L" fps"
     <<L"\r\nActual source frames completed "<<value(f.sourceCompletedFps)<<L" fps / total processing output "<<value(f.outputCompletedFps)<<L" fps"
     <<L"\r\nXeSS SDK submitted to present "<<value(f.xessSdkSubmitFps)<<L" fps (cannot be summed with processing output)"
     <<L"\r\nSoftware end-to-end latency mean "<<value(f.softwareLatencyMs)<<L" / P95 "<<value(f.softwareLatencyP95Ms)<<L" ms; valid samples "<<f.latencySamples
     <<L"\r\nXeSS SDK generated submitted "<<c.xessSdkGenerated<<L" (not equal to screen scan frame count)"
     <<L"\r\nGenerated candidates "<<c.fgCandidate<<L" / skipped before submit "<<c.fgSkippedBeforeEval<<L" / executed "<<c.fgEvaluated<<L" / warmup "<<c.fgWarmup
     <<L"\r\nValid generated "<<c.fgReadyValid<<L" / invalid "<<c.fgInvalid<<L" / presented "<<c.generatedPresented<<L" / expired "<<c.generatedExpiredAfterEval
     <<L"\r\nReal frames presented "<<c.realPresented<<L" / cancelled before present "<<c.cancelledBeforePresent<<L" / samples skipped "<<c.sourceSkippedBeforeGraph
     <<L"\r\nCommand slots in flight "<<c.commandSlotsInFlight<<L" / 6; observed peak "<<c.commandSlotHighWater<<L"; presentation batch peak "<<c.presentationBatchHighWater<<L" / 2"
     <<L"\r\nSlot reuse waits "<<f.slotReuseWaitCount<<L" times / "<<value(f.slotReuseWaitMs)<<L" ms\r\n";
    o<<L"Optical flow request: "<<flowName<<L"; actual SDK perf="<<s.flowPerf<<L"; grid=4 (verified SDK capability)\r\nContent cadence: "<<(s.contentFps?std::to_wstring(s.contentFps)+L"fps":L"unconfirmed (static or insufficient evidence)")<<L"; source timestamps preserved, repeated content not counted as valid generated\r\n"<<s.status;
    // Preserve the reader's scroll position and selection across the
    // 250 ms refresh (the text differs almost every tick).
    HWND edit=GetDlgItem(window,1);
    const auto firstLine=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);
    DWORD selStart=0,selEnd=0;SendMessageW(edit,EM_GETSEL,WPARAM(&selStart),LPARAM(&selEnd));
    const auto text=o.str();
    wchar_t old[8]{};GetWindowTextW(edit,old,8);
    SetWindowTextW(edit,text.c_str());
    if(selEnd>selStart)SendMessageW(edit,EM_SETSEL,selStart,selEnd);
    const auto nowFirst=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);
    if(firstLine!=nowFirst)SendMessageW(edit,EM_LINESCROLL,0,firstLine-nowFirst);
}
void arrange(){RECT r{};GetClientRect(window,&r);int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window)),height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(window));auto pos=[&](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,y),dip(window,std::max(1,w)),dip(window,std::max(1,h)),TRUE);};bool wide=width>=700;pos(6,width-56,8,40,32);pos(7,16,10,width-80,32);pos(1,16,52,wide?width/2-24:width-32,wide?height-116:height/2-68);pos(2,wide?width/2+8:16,wide?96:height/2+36,wide?width/2-24:width-32,wide?height-160:height/2-100);pos(3,wide?width/2+8:16,wide?52:height/2-8,wide?width/2-24:width-32,36);pos(4,16,height-52,width/2-24,36);pos(5,width/2+8,height-52,width/2-24,36);}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){case WM_CREATE:{preview.clear();window=h;font=makeFont(h,13);auto add=[&](const wchar_t* c,const wchar_t* t,int id,DWORD style){auto child=CreateWindowExW(0,c,t,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);themeControl(child);};
    add(L"EDIT",L"",1,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);add(L"BUTTON",L"View / refresh redacted preview",3,BS_PUSHBUTTON|WS_TABSTOP);add(L"EDIT",L"Click the button above to view diagnostics. Copy only copies the redacted text previewed here.",2,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);add(L"BUTTON",L"Copy previewed content",4,BS_PUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"Open this app's log folder",5,BS_PUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"Close diagnostics",6,BS_PUSHBUTTON|WS_TABSTOP);icon(GetDlgItem(h,6),Icon::Close);add(L"STATIC",L"Performance & diagnostics  /  playback session",7,0);SetTimer(h,1,250,nullptr);arrange();refresh();return 0;}
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);return 0;}
case WM_SIZE:arrange();return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(m,w,l);

case WM_TIMER:if(!IsWindowVisible(h))return 0;refresh();return 0;
case WM_COMMAND:if(LOWORD(w)==6){SendMessageW(GetParent(h),WM_APP+43,0,0);}else if(LOWORD(w)==3){preview=wide(Logger::instance().diagnosticReport());SetDlgItemTextW(h,2,preview.c_str());}else if(LOWORD(w)==4&&!preview.empty()){
    if(OpenClipboard(h)){HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(preview.size()+1)*sizeof(wchar_t));if(data){if(void* p=GlobalLock(data)){memcpy(p,preview.c_str(),(preview.size()+1)*sizeof(wchar_t));GlobalUnlock(data);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data))GlobalFree(data);}else GlobalFree(data);}CloseClipboard();}
}else if(LOWORD(w)==5)ShellExecuteW(h,L"open",runtime::logsDirectory().c_str(),nullptr,nullptr,SW_SHOW);return 0;

case WM_CLOSE:DestroyWindow(h);return 0;case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;return 0;}return DefWindowProcW(h,m,w,l);}
}
HWND createTelemetryPanel(HWND parent,engine::EngineController& controller){engine=&controller;WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraTelemetry";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Performance & Diagnostics Center",WS_CHILD|WS_CLIPCHILDREN,0,0,800,620,parent,nullptr,wc.hInstance,nullptr);}
void telemetryDpi(){if(!window)return;auto old=font;font=makeFont(window,13);EnumChildWindows(window,[](HWND child,LPARAM f)->BOOL{SendMessageW(child,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();}
}
