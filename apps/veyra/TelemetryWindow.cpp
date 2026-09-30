#include "TelemetryWindow.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include <shellapi.h>
#include <filesystem>
#include "ui/Theme.h"
#include <sstream>
#include <array>
#include <iomanip>
namespace veyra::ui {
namespace {
unsigned windowDpi=96;HWND window=nullptr;engine::EngineController* engine=nullptr;HFONT font=nullptr;std::wstring preview;
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
std::wstring value(std::optional<double> v){if(!v)return L"未能测量";std::wostringstream o;o<<std::fixed<<std::setprecision(3)<<*v;return o.str();}
void refresh(){auto s=engine->snapshot();static constexpr std::array<const wchar_t*,size_t(diagnostics::GpuStage::Count)> names={L"上传/颜色转换",L"超分 SR",L"光流 GPU队列区间（含同步）",L"NR",L"NR变化量合成",L"FG 子帧1",L"FG 子帧2",L"FG 子帧3",L"FG 子帧4",L"FG 子帧5",L"FG批次",L"最终blit",L"RTX Video HDR",L"NR节点1",L"NR节点2",L"NR节点3",L"NR节点4",L"调色节点1",L"调色节点2",L"调色节点3",L"调色节点4",L"调色节点5",L"调色节点6"};std::wostringstream o;
    o<<L"GPU时间戳（毫秒）；未执行不记作0。样本帧 "<<s.metrics.identity.sourceFrameId<<L" / epoch "<<s.metrics.identity.epoch<<L" / 设置版本 "<<s.metrics.identity.settingsRevision<<L"\r\n";
    for(size_t i=0;i<s.metrics.gpu.size();++i){const auto& g=s.metrics.gpu[i];o<<names[i]<<L"："<<(g.state==diagnostics::SampleState::NotExecuted?L"未执行":g.state==diagnostics::SampleState::Pending?L"待GPU完成":value(g.milliseconds))<<L"\r\n";}
    o<<L"\r\nCPU与调度（毫秒，独立于GPU）：取帧 "<<value(s.metrics.decodeCpuMs)<<L" / 图提交 "<<value(s.metrics.submitCpuMs)<<L" / GPU就绪等待 "<<value(s.metrics.gpuWaitCpuMs)<<L"\r\n截止时间等待 "<<value(s.metrics.deadlineWaitCpuMs)<<L" / Present调用 "<<value(s.metrics.presentCpuMs)<<L"\r\n源帧 "<<s.metrics.sourceFrames<<L" / 有效生成 "<<s.metrics.validGenerated<<L" / 实际提交 "<<s.metrics.submitted<<L" / 过期补帧 "<<s.metrics.expired<<L"\r\n当前批次容量 "<<s.metrics.queueWatermark<<L"（上限4）；采集入口容量1；采集丢弃 "<<s.captureDropped<<L"\r\n实际提交频率（最近1秒观察）："<<value(s.submissionFps)<<L"fps；实际显示扫描率/光子延迟：未测\r\n";
    const auto flowName=s.applied.flow==engine::FlowQuality::Performance?L"性能":s.applied.flow==engine::FlowQuality::Balanced?L"平衡":L"质量";
    const auto& f=s.metrics.flow;const auto& c=f.counters;
    if(s.remotePlay){const auto& r=s.remoteStream;
        o<<L"\r\n本次连接请求："<<r.requestedProfile.width<<L"×"<<r.requestedProfile.height<<L" / "<<r.requestedProfile.fps<<L" fps / "<<(r.requestedProfile.codec==remoteplay::Codec::H264?L"H.264":r.requestedProfile.codec==remoteplay::Codec::H265Hdr?L"H.265 HDR":L"H.265")<<L" / "<<r.requestedProfile.bitrateKbps/1000.0<<L" Mbps（并非固定实际流量）";
        o<<L"\r\nPS5完整视频接收 "<<r.receivedFps<<L" fps / 实际解码 "<<r.decodedFps<<L" fps / 有效码率 "<<r.videoMbps<<L" Mbps"
         <<L"\r\n实际解码路径："<<(!r.decodeConfirmed?L"尚未确认":r.hardwareDecode?L"D3D12VA 硬解":r.decodeFallback?L"CPU软件（硬解回退）":L"CPU软件")
         <<L"\r\n真实解码均值 / P95 "<<value(r.decodeMeanMs)<<L" / "<<value(r.decodeP95Ms)<<L" ms；接收后等待 "<<value(r.ingressWaitMeanMs)
         <<L"\r\n原帧 / 生成呈现 "<<f.realPresentFps<<L" / "<<f.generatedPresentFps<<L" fps"
         <<L"\r\n关键帧请求 "<<r.video.idrRequests<<L"；压缩队列丢弃 "<<r.video.dropped<<L"；解码邮箱覆盖 "<<s.remotePlaySkipped
         <<L"\r\n延迟口径：完整视频收到→原帧Present返回，不含PS5渲染、编码、单程网络和显示扫描。实时RTT/端到端延迟未测。\r\n";
    }
    o<<L"\r\n最近一秒链路统计：均值 / P95 ms，n为样本数\r\n";
    for(size_t i=0;i<f.gpuTiming.size();++i){const auto& a=f.gpuTiming[i];o<<names[i]<<L"："<<value(a.mean)<<L" / "<<value(a.p95)<<L" n="<<a.samples<<L"\r\n";}
    o<<L"\r\n当前统计窗口：会话 "<<f.latest.sessionId<<L" / 设置 "<<f.latest.frame.settingsRevision<<L" / epoch "<<f.latest.frame.epoch
     <<L"\r\n有效生成 "<<value(f.validGeneratedFps)<<L" fps / Present 提交 "<<value(f.presentSubmitFps)<<L" fps"
     <<L"\r\n实际源帧完成 "<<value(f.sourceCompletedFps)<<L" fps / 总处理产出 "<<value(f.outputCompletedFps)<<L" fps"
     <<L"\r\nXeSS SDK送呈现 "<<value(f.xessSdkSubmitFps)<<L" fps（不能与处理产出混加）"
     <<L"\r\n软件首尾延迟均值 "<<value(f.softwareLatencyMs)<<L" / P95 "<<value(f.softwareLatencyP95Ms)<<L" ms；有效样本 "<<f.latencySamples
     <<L"\r\nXeSS SDK 生成提交 "<<c.xessSdkGenerated<<L"（不等于屏幕扫描帧数）"
     <<L"\r\n补帧候选 "<<c.fgCandidate<<L" / 提交前跳过 "<<c.fgSkippedBeforeEval<<L" / 已执行 "<<c.fgEvaluated<<L" / 预热 "<<c.fgWarmup
     <<L"\r\n有效生成 "<<c.fgReadyValid<<L" / 无效 "<<c.fgInvalid<<L" / 已呈现 "<<c.generatedPresented<<L" / 过期 "<<c.generatedExpiredAfterEval
     <<L"\r\n真实帧呈现 "<<c.realPresented<<L" / 取消呈现 "<<c.cancelledBeforePresent<<L" / 采样跳过 "<<c.sourceSkippedBeforeGraph
     <<L"\r\n命令槽占用 "<<c.commandSlotsInFlight<<L" / 6；观察峰值 "<<c.commandSlotHighWater<<L"；呈现批次峰值 "<<c.presentationBatchHighWater<<L" / 2"
     <<L"\r\n槽复用等待 "<<f.slotReuseWaitCount<<L" 次 / "<<value(f.slotReuseWaitMs)<<L" ms\r\n";
    o<<L"光流请求："<<flowName<<L"；实际SDK perf="<<s.flowPerf<<L"；grid=4（已验证SDK能力）\r\n内容节奏："<<(s.contentFps?std::to_wstring(s.contentFps)+L"fps":L"未确认（静态或证据不足）")<<L"；保留源时间戳，重复内容不计有效生成\r\n"<<s.status;
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
    add(L"EDIT",L"",1,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);add(L"BUTTON",L"查看 / 刷新脱敏预览",3,BS_PUSHBUTTON|WS_TABSTOP);add(L"EDIT",L"点击上方按钮查看诊断。复制只会复制这里预览过的脱敏文字。",2,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);add(L"BUTTON",L"复制已预览内容",4,BS_PUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"打开本应用日志目录",5,BS_PUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"关闭诊断",6,BS_PUSHBUTTON|WS_TABSTOP);icon(GetDlgItem(h,6),Icon::Close);add(L"STATIC",L"性能与诊断  /  播放会话",7,0);SetTimer(h,1,250,nullptr);arrange();refresh();return 0;}
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
HWND createTelemetryPanel(HWND parent,engine::EngineController& controller){engine=&controller;WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraTelemetry";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"性能与诊断中心",WS_CHILD|WS_CLIPCHILDREN,0,0,800,620,parent,nullptr,wc.hInstance,nullptr);}
void telemetryDpi(){if(!window)return;auto old=font;font=makeFont(window,13);EnumChildWindows(window,[](HWND child,LPARAM f)->BOOL{SendMessageW(child,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();}
}
