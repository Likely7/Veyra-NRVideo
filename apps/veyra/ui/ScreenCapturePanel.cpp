#include "ScreenCapturePanel.h"
#include "Theme.h"
#include "ProtectionOverlay.h"
#include "veyra/source/ScreenCaptureSource.h"
#include "veyra/RuntimePaths.h"
#include <format>

namespace veyra::ui {
namespace {
HWND panelWindow=nullptr,outline=nullptr;HFONT font=nullptr;
std::function<void(const std::wstring&)> start;std::function<void()> stop;
std::function<engine::PlayerSnapshot()> state;std::function<void(bool)> displayFit;
std::unique_ptr<engine::EngineController> preview;
std::vector<source::ScreenCaptureTarget> targets;
enum {Kind=101,Target,Refresh,Method,Cursor,Fps,Left,Top,Right,Bottom,Start,Stop,State,Preview,PreviewStart,Fit,ResetCrop};
struct Placement {HWND window;int x,y,w,h;};std::vector<Placement> placements;
bool active=false,dragging=false;POINT dragStart{};unsigned previewWidth=0,previewHeight=0;
std::wstring validationError;
HWND child(int id){return GetDlgItem(panelWindow,id);}
int selection(int id){return int(SendMessageW(child(id),CB_GETCURSEL,0,0));}
void add(HWND w,const wchar_t* value){SendMessageW(w,CB_ADDSTRING,0,LPARAM(value));}
std::wstring ini(){return (runtime::localDataDirectory()/"veyra.ini").wstring();}
void persist(){for(auto [id,key]:{std::pair{Kind,L"Kind"},{Method,L"Method"},{Fps,L"RateMode"},{Fit,L"Fit"}})WritePrivateProfileStringW(L"ScreenCapture",key,std::to_wstring(selection(id)).c_str(),ini().c_str());WritePrivateProfileStringW(L"ScreenCapture",L"Cursor",SendMessageW(child(Cursor),BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0",ini().c_str());}
void layout(){
    RECT client{};GetClientRect(panelWindow,&client);
    const int available=MulDiv(client.bottom,96,layoutDpi(panelWindow));
    const int reduction=std::clamp(640-available,0,148);
    for(auto& p:placements){const int y=p.y>=278?p.y-reduction:p.y;const int height=p.window==child(Preview)?p.h-reduction:p.h;
        SetWindowPos(p.window,nullptr,dip(panelWindow,p.x),dip(panelWindow,y),dip(panelWindow,p.w),dip(panelWindow,height),SWP_NOZORDER);}
}
void methods(){const bool monitor=selection(Kind)==1;EnableWindow(child(Method),monitor);if(!monitor)SendMessageW(child(Method),CB_SETCURSEL,0,0);const bool wgc=selection(Method)==0;EnableWindow(child(Cursor),wgc);if(!wgc)SendMessageW(child(Cursor),BM_SETCHECK,BST_UNCHECKED,0);}
void stopPreview(){if(preview){preview.reset();preview=std::make_unique<engine::EngineController>();}previewWidth=previewHeight=0;if(outline)ShowWindow(outline,SW_HIDE);}
void resetCrop(){for(int id=Left;id<=Bottom;++id)SetDlgItemInt(panelWindow,id,0,FALSE);if(outline)ShowWindow(outline,SW_HIDE);}
void enumerate(){
    auto old=selection(Target);uint64_t handle=old>=0&&size_t(old)<targets.size()?targets[old].handle:0;
    targets=source::ScreenCaptureSource::targets(selection(Kind)==1?source::ScreenTargetKind::Monitor:source::ScreenTargetKind::Window);
    SendMessageW(child(Target),CB_RESETCONTENT,0,0);int selected=0;
    for(size_t i=0;i<targets.size();++i){add(child(Target),targets[i].name.c_str());if(targets[i].handle==handle)selected=int(i);}
    SendMessageW(child(Target),CB_SETCURSEL,selected,0);EnableWindow(child(Start),!targets.empty());EnableWindow(child(PreviewStart),!targets.empty());methods();
    if(targets.empty())SetWindowTextW(child(State),L"No capturable targets");
}
bool options(source::ScreenCaptureOptions& value,bool cropped){
    const auto index=selection(Target);if(index<0||size_t(index)>=targets.size())return false;
    value.kind=targets[index].kind;value.target=targets[index].handle;value.method=source::ScreenCaptureMethod(selection(Method));
    constexpr unsigned rates[]={0,30,60,120,144,240};value.fps=rates[std::clamp(selection(Fps),0,5)];value.cursor=SendMessageW(child(Cursor),BM_GETCHECK,0,0)==BST_CHECKED;
    if(cropped){unsigned* margins[]={&value.left,&value.top,&value.right,&value.bottom};for(int i=0;i<4;++i){BOOL valid=FALSE;*margins[i]=GetDlgItemInt(panelWindow,Left+i,&valid,FALSE);if(!valid||*margins[i]>16384){validationError=L"Crop value must be an integer from 0 to 16384";SetWindowTextW(child(State),validationError.c_str());return false;}}}
    return true;
}
std::pair<float,float> point(HWND window,POINT p){RECT rect{};GetClientRect(window,&rect);return engine::PreviewView{}.sourcePoint(float(p.x),float(p.y),float(rect.right),float(rect.bottom),float(previewWidth),float(previewHeight));}
LRESULT CALLBACK previewProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_LBUTTONDOWN&&previewWidth&&previewHeight){dragStart={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto uv=point(window,dragStart);if(uv.first>=0&&uv.first<=1&&uv.second>=0&&uv.second<=1){dragging=true;SetCapture(window);}return 0;}
    if(message==WM_MOUSEMOVE&&dragging){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(!outline)outline=createSubtitleOverlay(window);protectionOutline(outline,window,{std::min(p.x,dragStart.x),std::min(p.y,dragStart.y),std::max(p.x,dragStart.x),std::max(p.y,dragStart.y)});return 0;}
    if(message==WM_LBUTTONUP&&dragging){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto a=point(window,dragStart),b=point(window,p);dragging=false;ReleaseCapture();if(std::abs(p.x-dragStart.x)>3&&std::abs(p.y-dragStart.y)>3){
        SetDlgItemInt(panelWindow,Left,unsigned(std::clamp(std::min(a.first,b.first),0.f,1.f)*previewWidth),FALSE);SetDlgItemInt(panelWindow,Right,unsigned((1-std::clamp(std::max(a.first,b.first),0.f,1.f))*previewWidth),FALSE);
        SetDlgItemInt(panelWindow,Top,unsigned(std::clamp(std::min(a.second,b.second),0.f,1.f)*previewHeight),FALSE);SetDlgItemInt(panelWindow,Bottom,unsigned((1-std::clamp(std::max(a.second,b.second),0.f,1.f))*previewHeight),FALSE);
    }return 0;}
    if(message==WM_CAPTURECHANGED)dragging=false;
    if(message==WM_RBUTTONUP){resetCrop();return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);if(!preview||!preview->snapshot().frames)FillRect(dc,&ps.rcPaint,HBRUSH(GetStockObject(BLACK_BRUSH)));EndPaint(window,&ps);return 0;}
    return DefSubclassProc(window,message,wp,lp);
}
HWND make(const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x,int y,int w,int h){
    auto control=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,dip(panelWindow,x),dip(panelWindow,y),dip(panelWindow,w),dip(panelWindow,h),panelWindow,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(control,WM_SETFONT,WPARAM(font),TRUE);if(id!=Preview)themeControl(control);placements.push_back({control,x,y,w,h});return control;
}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:{panelWindow=window;placements.clear();validationError.clear();font=makeFont(window);titleTheme(window);preview=std::make_unique<engine::EngineController>();
        auto kind=make(L"COMBOBOX",L"",Kind,CBS_DROPDOWNLIST|WS_TABSTOP,20,16,126,160);add(kind,L"Window");add(kind,L"Monitor");
        make(L"COMBOBOX",L"",Target,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,158,16,330,400);make(L"BUTTON",L"Refresh",Refresh,BS_PUSHBUTTON|WS_TABSTOP,500,16,80,32);
        auto viewport=make(L"STATIC",L"",Preview,SS_NOTIFY,20,60,560,208);SetWindowSubclass(viewport,previewProc,1,0);
        make(L"BUTTON",L"Preview",PreviewStart,BS_PUSHBUTTON|WS_TABSTOP,20,278,110,32);icon(child(PreviewStart),Icon::Play,true);
        make(L"BUTTON",L"Reset crop",ResetCrop,BS_PUSHBUTTON|WS_TABSTOP,142,278,120,32);
        auto fit=make(L"COMBOBOX",L"",Fit,CBS_DROPDOWNLIST|WS_TABSTOP,390,278,190,160);add(fit,L"Fit to window");add(fit,L"Fill window");
        make(L"STATIC",L"Capture method",0,0,20,330,115,26);auto method=make(L"COMBOBOX",L"",Method,CBS_DROPDOWNLIST|WS_TABSTOP,145,326,435,180);add(method,L"Windows Graphics Capture");add(method,L"DXGI monitor compatible (no pointer)");
        make(L"STATIC",L"Frame rate cap",0,0,20,374,115,26);auto fps=make(L"COMBOBOX",L"",Fps,CBS_DROPDOWNLIST|WS_TABSTOP,145,370,225,220);for(auto value:{L"Follow display refresh rate",L"30",L"60",L"120",L"144",L"240"})add(fps,value);
        make(L"BUTTON",L"Show mouse pointer",Cursor,BS_AUTOCHECKBOX|WS_TABSTOP,386,370,194,28);
        make(L"STATIC",L"Crop / pixels",0,0,20,420,180,24);const wchar_t* labels[]={L"Left",L"Top",L"Right",L"Bottom"};for(int i=0;i<4;++i){make(L"STATIC",labels[i],0,0,20+i*140,462,40,24);make(L"EDIT",L"0",Left+i,WS_BORDER|ES_NUMBER|WS_TABSTOP,62+i*140,458,74,30);}
        make(L"STATIC",L"",State,0,20,504,560,68);make(L"BUTTON",L"Start / switch",Start,BS_DEFPUSHBUTTON|WS_TABSTOP,300,582,160,38);icon(child(Start),Icon::Play,true);
        make(L"BUTTON",L"Stop",Stop,BS_PUSHBUTTON|WS_TABSTOP,474,582,106,38);icon(child(Stop),Icon::Stop,true);
        for(auto [id,key]:{std::pair{Kind,L"Kind"},{Method,L"Method"},{Fps,L"RateMode"},{Fit,L"Fit"}}){auto value=GetPrivateProfileIntW(L"ScreenCapture",key,0,ini().c_str());auto count=SendMessageW(child(id),CB_GETCOUNT,0,0);SendMessageW(child(id),CB_SETCURSEL,value<unsigned(count)?value:0,0);}
        SendMessageW(child(Cursor),BM_SETCHECK,GetPrivateProfileIntW(L"ScreenCapture",L"Cursor",1,ini().c_str())?BST_CHECKED:BST_UNCHECKED,0);enumerate();SetTimer(window,1,250,nullptr);return 0;}
    case WM_TIMER:{
        if(!validationError.empty())return 0;
        auto p=preview->snapshot();if(p.running&&p.frames){previewWidth=p.metrics.resolution.source.width;previewHeight=p.metrics.resolution.source.height;}
        auto s=active&&state?state():p;std::wstring text;
        if(s.failed)text=s.status;else if(s.transport==engine::TransportState::Opening)text=L"Opening target";
        else if(s.running){text=!s.sourceNotice.empty()?s.sourceNotice:std::format(L"{}  {} x {}  {:.1f} FPS",active?L"Capturing":L"Preview",s.metrics.resolution.source.width,s.metrics.resolution.source.height,s.fps);}
        else text=L"Stopped";
        SetWindowTextW(child(State),text.c_str());return 0;}
    case WM_COMMAND:{const int id=LOWORD(wp);validationError.clear();
        if(id==Refresh||(id==Kind&&HIWORD(wp)==CBN_SELCHANGE)){stopPreview();resetCrop();enumerate();return 0;}
        if(id==Target&&HIWORD(wp)==CBN_SELCHANGE){stopPreview();resetCrop();return 0;}
        if(id==Method&&HIWORD(wp)==CBN_SELCHANGE){stopPreview();methods();return 0;}
        if(id==Fit&&HIWORD(wp)==CBN_SELCHANGE){if(displayFit)displayFit(selection(Fit)==1);persist();return 0;}
        if(id==Fps&&HIWORD(wp)==CBN_SELCHANGE){persist();return 0;}
        if(id==ResetCrop){resetCrop();return 0;}
        if(id==Stop){stopPreview();if(stop)stop();active=false;return 0;}
        if(id==Start||id==PreviewStart){source::ScreenCaptureOptions value;if(!options(value,id==Start))return 0;persist();
            if(id==PreviewStart){if(stop)stop();active=false;engine::PlayerOptions settings;preview->open(child(Preview),value.uri(),settings);}
            else{stopPreview();if(start)start(value.uri());if(displayFit)displayFit(selection(Fit)==1);active=true;}return 0;}
        break;}
    case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);auto old=font;font=makeFont(window);for(auto& p:placements)SendMessageW(p.window,WM_SETFONT,WPARAM(font),TRUE);DeleteObject(old);layout();return 0;}
    case WM_SIZE:layout();return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:return colors(message,wp,lp);
    case WM_CLOSE:persist();DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);preview.reset();if(outline)DestroyWindow(outline);outline=nullptr;DeleteObject(font);font=nullptr;panelWindow=nullptr;placements.clear();return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
}
void showScreenCapturePanel(HWND parent,std::function<void(const std::wstring&)> onStart,std::function<void()> onStop,std::function<engine::PlayerSnapshot()> getState,std::function<void(bool)> fit){
    start=std::move(onStart);stop=std::move(onStop);state=std::move(getState);displayFit=std::move(fit);if(panelWindow){SetForegroundWindow(panelWindow);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraScreenCapture";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=dip(parent,618),height=std::min(dip(parent,674),int(monitor.rcWork.bottom-monitor.rcWork.top));
    CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,L"Screen Capture",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,parent,nullptr,wc.hInstance,nullptr);
}
bool screenCaptureDialogMessage(MSG& message){return panelWindow&&IsDialogMessageW(panelWindow,&message);}
}
