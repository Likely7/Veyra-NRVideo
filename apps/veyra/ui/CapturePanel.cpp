#include "CapturePanel.h"
#include "Theme.h"
#include "SettingHelp.h"
#include "CapturePreferenceStore.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/CaptureFormatRank.h"
#include <future>
#include <format>
namespace veyra::ui {
namespace {
HWND window=nullptr;HFONT font=nullptr;std::function<void(const std::wstring&)> start;
struct Query {int device=-1;std::vector<source::CaptureDevice> video,audio;std::vector<source::CaptureFormat> formats;};
std::future<Query> pending;bool busy=false,refreshAfterQuery=false;int queriedDevice=-1;ULONGLONG queryStarted=0;
std::vector<source::CaptureFormat> formats;
std::vector<source::CaptureDevice> videoDevices,audioDevices;
CapturePreferences remembered;
std::function<bool()> readSdr;std::function<bool(bool)> setSdr;std::function<int()> readAudioIngress;std::function<bool(int)> setAudioIngress;
std::function<bool()> readFlip;std::function<bool(bool)> setFlip;
std::function<int()> readBuffer;std::function<bool(int)> setBuffer;
void rebuildAudioList(HWND h,int device){
    SendDlgItemMessageW(h,3,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(L"不监听音频"));
    const bool videoSelected=device>=0&&size_t(device)<videoDevices.size();
    if(videoSelected)SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(videoDevices[size_t(device)].hasEmbeddedAudio?L"使用视频设备内置音频（已检测）":L"尝试视频设备内置音频"));
    for(auto& audio:audioDevices){const auto label=std::format(L"[{}] {}",audio.wasapi?L"WASAPI":L"DirectShow",audio.name);SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(label.c_str()));}
    int restore=0;
    if(videoSelected&&videoDevices[size_t(device)].path==remembered.videoPath){
        if(remembered.audioMode==source::kCaptureAudioFromVideoDevice)restore=1;
        else if(!remembered.audioPath.empty()){
            restore=-1;
            for(size_t i=0;i<audioDevices.size();++i)
                if(audioDevices[i].path==remembered.audioPath&&audioDevices[i].wasapi==(remembered.audioMode==source::kCaptureAudioWasapi))restore=int(i)+2;
        }
    }
    SendDlgItemMessageW(h,3,CB_SETCURSEL,restore,0);
}
int selectedAudio(HWND h,int device){
    const int selection=int(SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0));if(selection<=0)return source::kCaptureAudioDisabled;
    const bool videoSelected=device>=0&&size_t(device)<videoDevices.size();
    if(videoSelected&&selection==1)return source::kCaptureAudioFromVideoDevice;
    return selection-1-(videoSelected?1:0);
}
void maybeShowFormatHint(HWND h){
    const int index=int(SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0));
    if(index<0||size_t(index)>=formats.size()||remembered.formatHintDismissed)return;
    const auto& format=formats[size_t(index)];
    const auto tier=static_cast<source::CaptureFormatTier>(format.tier);
    if(!source::captureFormatNeedsCostHint(tier))return;
    SetDlgItemTextW(h,8,std::format(L"提示：{} 在高分辨率/高帧率下比低延迟格式多一道处理环节（{}）。建议优先选低延迟格式；此提示只出现一次。",format.label,source::captureFormatTierLabel(tier)).c_str());
    remembered.formatHintDismissed=true;
    CapturePreferenceStore(runtime::localDataDirectory()).save(remembered);
}
void query(int device){if(busy)return;busy=true;queriedDevice=device;queryStarted=GetTickCount64();SetDlgItemTextW(window,8,L"正在查询设备能力…当前播放继续");EnableWindow(GetDlgItem(window,4),FALSE);EnableWindow(GetDlgItem(window,5),FALSE);EnableWindow(GetDlgItem(window,1),FALSE);
    const std::wstring videoPath=device>=0&&size_t(device)<videoDevices.size()?videoDevices[size_t(device)].path:L"";
    pending=std::async(std::launch::async,[device,videoPath]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);Query result;result.device=device;try{if(device<0){result.video=source::CaptureCardSource::deviceDetails();result.audio=source::CaptureCardSource::deviceDetails(true);}else result.formats=videoPath.empty()?source::CaptureCardSource::formats(unsigned(device)):source::CaptureCardSource::formatsByPath(videoPath);}catch(...){}CoUninitialize();return result;});
}
void arrange(){RECT r{};GetClientRect(window,&r);const int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window));const int ys[]={0,44,114,184,448,448,14,84,380,154,254,224,294,514,484,602,566,542,338,342};for(int id=1;id<=19;++id){int x=id==5?width-152:id==18?width-156:16;int w=id==4?width-184:id==5||id==18?136:id==19?width-184:width-32;MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,ys[id]),dip(window,w),dip(window,(id<=3||id==10||id==13||id==16)?180:id==8?56:id==4||id==5?36:id==18?32:24),TRUE);}}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);titleTheme(h);auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style){auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);};
    remembered=CapturePreferenceStore(runtime::localDataDirectory()).load();
    add(L"EDIT",std::format(L"{:g}",remembered.requestedFps).c_str(),18,ES_AUTOHSCROLL|WS_TABSTOP);
    SendDlgItemMessageW(h,18,EM_SETLIMITTEXT,16,0);
    add(L"STATIC",L"设备帧率 FPS（0 = 默认，需重连）",19,0);
    refreshAfterQuery=busy;
    for(int i=1;i<=3;++i)add(L"COMBOBOX",L"",i,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"COMBOBOX",L"",10,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"输入颜色（变更需重连）",11,0);
    for(auto name:{L"自动识别 SDR / HDR · 设备元数据",L"手动 HDR10 / PQ · BT.2020",L"手动 HLG · BT.2020 / 1000nit参考"})SendDlgItemMessageW(h,10,CB_ADDSTRING,0,LPARAM(name));SendDlgItemMessageW(h,10,CB_SETCURSEL,0,0);
    add(L"BUTTON",L"转为 SDR 显示（所有预览，立即生效）",12,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",13,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"采集音频（变更需重连）",14,0);
    for(auto name:{L"自动：优先线性 PCM，必要时 Dolby/DTS 位流解码",L"强制线性 PCM（不接受 Dolby/DTS 位流）",L"位流优先：优先直通给功放（无直通时解码为 PCM）"})SendDlgItemMessageW(h,13,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(h,13,CB_SETCURSEL,WPARAM(std::clamp(readAudioIngress?readAudioIngress():0,0,2)),0);
    add(L"BUTTON",L"画面上下翻转（采集画面倒置时勾选，立即生效）",15,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip&&readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",16,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"设备缓冲（变更需重连）",17,0);
    for(auto name:{L"自动：1080p 及以下 2 帧，更高分辨率 3 帧",L"最小：1 帧缓冲（延迟最低，高负载可能丢帧）",L"驱动默认：不做建议，沿用设备自身设置"})SendDlgItemMessageW(h,16,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer?readBuffer():0,0,2)),0);
    add(L"BUTTON",L"连接并开始观看",4,BS_PUSHBUTTON|WS_TABSTOP);marked(GetDlgItem(h,4));add(L"BUTTON",L"刷新设备",5,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"视频输入设备",6,0);add(L"STATIC",L"设备实际支持的格式",7,0);add(L"STATIC",L"",8,0);add(L"STATIC",L"音频监听（仅采集所选输入，默认关闭）",9,0);
    installDialogHelp(h,{{16,L"采集卡往软件送帧用的驱动缓冲数量。缓冲越多，卡可以提前排队，但每多一帧就多一帧的等待；缓冲太少，驱动来不及取走就会丢帧。自动：1080p 及以下建议 2 帧、更高分辨率建议 3 帧；最小：1 帧，延迟最低但高负载可能丢帧；驱动默认：不干预。改这个要重新连接采集卡，连接日志里会写建议值和驱动实际给的值（协商未生效时如实标注）。"},{15,L"给方向声明和实际画面不一致的采集卡用：常见于某些 RGB24 格式——设备在媒体类型里写的是底行在前，实际送来的却是顶行在前，于是画面上下颠倒。勾选后把采集画面上下翻转一次，立即生效；只影响本机采集预览、截图和从这里导出的画面，不改设备也不动其他格式。画面正常时不要勾选；换成不颠倒的格式后记得取消。"},{13,L"采集卡把 Dolby Atmos / Dolby Audio / DTS 以位流送来时，这里决定用哪条路。设备本身的音频格式不受影响；改这个要重新连接采集卡。自动优先 PCM，PCM 不可用时才走位流解码；位流优先则反过来：先尝试把位流原样直通给功放/回音壁（独占输出，卡不经过软件解码，只有接收端才能解出 Atmos/DTS:X 的对象声场；需要输出设备支持，探测见日志 bitstream-out），没有支持的输出端点时自动回退到解码。适合 PS5 已设成 Dolby 输出、但设备同时提供 PCM 的情况。"},{12,L"收到HDR也转成SDR显示，不用改PS5或Windows。增强照常用；只改预览，视频导出不受影响。切换会短暂停顿，截图跟随当前画面。关闭后跟随显示器。"},{10,L"设备没报HDR信息时手动指定，需选P010/P016。P010也可能装SDR，别给普通画面强戴HDR帽子。"},{1,L"选采集卡的视频设备。别把摄像头误请来直播PS5。"},{2,L"选设备真实提供的分辨率、帧率和像素格式。清晰度、带宽和延迟都受它影响。"},{3,L"如果设备自带 HDMI 音频，会显示“使用视频设备内置音频”；否则选择独立音频设备，也可不采声音。"},{4,L"按当前格式连接采集卡，并应用当前增强设置。"},{5,L"重新扫描设备和格式。设备被其他软件占用时，刷新不一定能抢回来。"}});
    installDialogHelp(h,{{18,L"直接请求采集卡按此帧率送帧，支持小数。0 沿用所选格式；更改后重新连接。设备不支持或返回其他帧率时会报错，不在软件中偷偷丢帧。采集卡不能判断哪些画面是游戏重复帧，降帧率不保证消除重复画面。"}});
    EnableWindow(GetDlgItem(h,4),FALSE);arrange();SetTimer(h,1,100,nullptr);if(!busy)query(-1);return 0;}
case WM_TIMER:
    SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    if(readFlip)SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    if(readBuffer)SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer(),0,2)),0);
    if(busy&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        auto result=pending.get();busy=false;EnableWindow(GetDlgItem(h,5),TRUE);EnableWindow(GetDlgItem(h,1),TRUE);
        if(refreshAfterQuery){refreshAfterQuery=false;query(-1);return 0;}
        if(result.device<0){
            videoDevices=std::move(result.video);audioDevices=std::move(result.audio);
            SendDlgItemMessageW(h,1,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);formats.clear();
            for(auto& video:videoDevices)SendDlgItemMessageW(h,1,CB_ADDSTRING,0,LPARAM(video.name.c_str()));
            if(!videoDevices.empty()){
                int restore=remembered.videoPath.empty()?0:-1;
                for(size_t i=0;i<videoDevices.size();++i)if(videoDevices[i].path==remembered.videoPath)restore=int(i);
                SendDlgItemMessageW(h,1,CB_SETCURSEL,restore,0);rebuildAudioList(h,restore);
                if(restore>=0)query(restore);else SetDlgItemTextW(h,8,L"上次采集设备未连接。插回后刷新，或手动选择其他设备。");
            }else{
                rebuildAudioList(h,-1);SetDlgItemTextW(h,8,L"未找到采集设备。连接后点击刷新。");
            }
        }else{
            formats=std::move(result.formats);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);
            for(auto& format:formats)SendDlgItemMessageW(h,2,CB_ADDSTRING,0,LPARAM(format.label.c_str()));
            int restore=formats.empty()?-1:0;
            const bool sameDevice=size_t(result.device)<videoDevices.size()&&videoDevices[size_t(result.device)].path==remembered.videoPath;
            if(sameDevice&&!remembered.formatKey.empty()){
                restore=-1;for(size_t i=0;i<formats.size();++i)if(formats[i].key==remembered.formatKey)restore=int(i);
                SendDlgItemMessageW(h,10,CB_SETCURSEL,remembered.colorOverride,0);
            }else SendDlgItemMessageW(h,10,CB_SETCURSEL,0,0);
            SetDlgItemTextW(h,18,std::format(L"{:g}",sameDevice?remembered.requestedFps:0).c_str());
            SendDlgItemMessageW(h,2,CB_SETCURSEL,restore,0);EnableWindow(GetDlgItem(h,4),restore>=0);
            SetDlgItemTextW(h,8,formats.empty()?L"未读到有效的4K以内采集格式，或设备正被其他应用占用。":L"连接后使用当前增强设置。格式与音频变更需要重新连接。");
            const int ajaDevice=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));
            if(ajaDevice>=0&&size_t(ajaDevice)<videoDevices.size()&&videoDevices[size_t(ajaDevice)].path.starts_with(L"aja:"))
                SetDlgItemTextW(h,8,L"AJA：选择已接线的 HDMI 端口，自动跟随输入；先关闭 Control Room。当前为 8-bit SDR，内嵌音频仅线性 PCM。");
            if(!formats.empty()&&restore<0)SetDlgItemTextW(h,8,L"上次格式已不可用，请重新选择格式。");
            if(SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)==CB_ERR){EnableWindow(GetDlgItem(h,4),FALSE);SetDlgItemTextW(h,8,L"上次音频设备未连接。请选择音频设备，或明确选择不监听音频。");}
        }
    }else if(busy&&GetTickCount64()-queryStarted>5000)SetDlgItemTextW(h,8,L"设备查询耗时较长。可以关闭此面板，当前播放不受影响。");
    return 0;
case WM_COMMAND:
    if(LOWORD(wp)==12&&HIWORD(wp)==BN_CLICKED){
        const bool enabled=SendDlgItemMessageW(h,12,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!setSdr(enabled))SetDlgItemTextW(h,8,L"当前正在切换增强，请稍后再试。");
        SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    }else if(LOWORD(wp)==15&&HIWORD(wp)==BN_CLICKED){
        const bool enabled=SendDlgItemMessageW(h,15,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!setFlip||!setFlip(enabled))SetDlgItemTextW(h,8,L"当前正在切换增强，请稍后再试。");
        if(readFlip)SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    }else if(LOWORD(wp)==13&&HIWORD(wp)==CBN_SELCHANGE){
        const int mode=int(SendDlgItemMessageW(h,13,CB_GETCURSEL,0,0));
        if(mode>=0&&setAudioIngress&&!setAudioIngress(mode))SetDlgItemTextW(h,8,L"当前正在切换增强或采集；请稍后再试。");
        if(readAudioIngress)SendDlgItemMessageW(h,13,CB_SETCURSEL,WPARAM(std::clamp(readAudioIngress(),0,2)),0);
    }else if(LOWORD(wp)==16&&HIWORD(wp)==CBN_SELCHANGE){
        const int mode=int(SendDlgItemMessageW(h,16,CB_GETCURSEL,0,0));
        if(mode>=0&&setBuffer&&!setBuffer(mode))SetDlgItemTextW(h,8,L"当前正在切换增强或采集；请稍后再试。");
        if(readBuffer)SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer(),0,2)),0);
    }else if(LOWORD(wp)==1&&HIWORD(wp)==CBN_SELCHANGE){
        const int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));rebuildAudioList(h,device);query(device);
    }else if((LOWORD(wp)==2||LOWORD(wp)==3)&&HIWORD(wp)==CBN_SELCHANGE){
        EnableWindow(GetDlgItem(h,4),!busy&&SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0)!=CB_ERR&&SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)!=CB_ERR);
        if(LOWORD(wp)==2)maybeShowFormatHint(h);
    }else if(LOWORD(wp)==5){
        query(-1);
    }else if(LOWORD(wp)==4){
        const int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));
        const int format=int(SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0));
        const int audio=selectedAudio(h,device);
        wchar_t rateText[32]{};GetDlgItemTextW(h,18,rateText,32);double requestedFps=0;
        if(!source::parseCaptureFrameRate(rateText,requestedFps)){SetDlgItemTextW(h,8,L"采集帧率请输入 1–1000 的数字（可带小数），或填 0 沿用设备默认。 ");SetFocus(GetDlgItem(h,18));return 0;}
        const bool audioIndexValid=SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)!=CB_ERR&&(audio<0||size_t(audio)<audioDevices.size());
        if(!busy&&device==queriedDevice&&format>=0&&size_t(format)<formats.size()&&device>=0&&size_t(device)<videoDevices.size()&&audioIndexValid){
            const auto* audioDevice=audio>=0?&audioDevices[size_t(audio)]:nullptr;
            const auto path=source::CaptureCardSource::makeCapturePath(unsigned(device),videoDevices[size_t(device)],formats[size_t(format)].index,audio,audioDevice,unsigned(SendDlgItemMessageW(h,10,CB_GETCURSEL,0,0)),requestedFps);
            if(!path.empty()){
                remembered={videoDevices[size_t(device)].path,formats[size_t(format)].key,audioDevice?audioDevice->path:L"",audioDevice?(audioDevice->wasapi?source::kCaptureAudioWasapi:0):audio,unsigned(SendDlgItemMessageW(h,10,CB_GETCURSEL,0,0)),remembered.formatHintDismissed,requestedFps};
                if(!CapturePreferenceStore(runtime::localDataDirectory()).save(remembered))log::warn("capture","Failed to save capture selection");
                start(path);DestroyWindow(h);
            }
        }
    }
    return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_SIZE:arrange();return 0;
case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);auto old=font;font=makeFont(h);EnumChildWindows(h,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();return 0;}
case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(h);return 0;}break;
case WM_CLOSE:DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showCapturePanel(HWND parent,std::function<void(const std::wstring&)> callback,std::function<bool()> read,std::function<bool(bool)> write,std::function<int()> readIngress,std::function<bool(int)> writeIngress,std::function<bool()> readFlipped,std::function<bool(bool)> writeFlipped,std::function<int()> readBuffered,std::function<bool(int)> writeBuffered){start=std::move(callback);readSdr=std::move(read);setSdr=std::move(write);readAudioIngress=std::move(readIngress);setAudioIngress=std::move(writeIngress);readFlip=std::move(readFlipped);setFlip=std::move(writeFlipped);readBuffer=std::move(readBuffered);setBuffer=std::move(writeBuffered);if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"采集卡 · 连接设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,560),dip(parent,680),parent,nullptr,wc.hInstance,nullptr);}
}
