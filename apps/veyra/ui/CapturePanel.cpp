#include "CapturePanel.h"
#include "Theme.h"
#include "SettingHelp.h"
#include "CapturePreferenceStore.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/CaptureFormatRank.h"
#include <future>
#include <thread>
#include <format>
namespace veyra::ui {
namespace {
HWND window=nullptr;HFONT font=nullptr;std::function<void(const std::wstring&)> start;
struct Query {int device=-1;std::vector<source::CaptureDevice> video,audio;std::vector<source::CaptureFormat> formats;};
std::future<Query> pending;bool busy=false,refreshAfterQuery=false;int queriedDevice=-1;ULONGLONG queryStarted=0;
std::vector<source::CaptureFormat> formats;
std::vector<source::CaptureDevice> videoDevices,audioDevices;
CapturePreferences remembered;
unsigned selectedColor(HWND h){return source::captureColorOverride(unsigned(SendDlgItemMessageW(h,10,CB_GETCURSEL,0,0)),unsigned(SendDlgItemMessageW(h,20,CB_GETCURSEL,0,0)));}
void saveDeviceColor(HWND h){
    const int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));
    if(device<0||size_t(device)>=videoDevices.size())return;
    remembered.deviceColors[videoDevices[size_t(device)].path]=selectedColor(h);
    if(!CapturePreferenceStore(runtime::localDataDirectory()).save(remembered))log::warn("capture","Failed to save capture color preference");
}
std::function<bool()> readSdr;std::function<bool(bool)> setSdr;std::function<int()> readAudioIngress;std::function<bool(int)> setAudioIngress;
std::function<bool()> readFlip;std::function<bool(bool)> setFlip;
std::function<int()> readBuffer;std::function<bool(int)> setBuffer;
void rebuildAudioList(HWND h,int device){
    SendDlgItemMessageW(h,3,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(L"Do not monitor audio"));
    const bool videoSelected=device>=0&&size_t(device)<videoDevices.size();
    if(videoSelected)SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(videoDevices[size_t(device)].hasEmbeddedAudio?L"Use the video device's built-in audio (detected)":L"Try the video device's built-in audio"));
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
    SetDlgItemTextW(h,8,std::format(L"Tip: at high resolution/high frame rate, {} adds an extra processing step compared with low-latency formats ({}). Prefer a low-latency format; this tip appears only once.",format.label,source::captureFormatTierLabel(tier)).c_str());
    remembered.formatHintDismissed=true;
    CapturePreferenceStore(runtime::localDataDirectory()).save(remembered);
}
void query(int device){if(busy)return;busy=true;queriedDevice=device;queryStarted=GetTickCount64();SetDlgItemTextW(window,8,L"Querying device capabilities... current playback continues");EnableWindow(GetDlgItem(window,4),FALSE);EnableWindow(GetDlgItem(window,5),FALSE);EnableWindow(GetDlgItem(window,1),FALSE);
    const std::wstring videoPath=device>=0&&size_t(device)<videoDevices.size()?videoDevices[size_t(device)].path:L"";
    EnableWindow(GetDlgItem(window,10),FALSE);EnableWindow(GetDlgItem(window,20),FALSE);
    pending=std::async(std::launch::async,[device,videoPath]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);Query result;result.device=device;try{if(device<0){result.video=source::CaptureCardSource::deviceDetails();result.audio=source::CaptureCardSource::deviceDetails(true);}else result.formats=videoPath.empty()?source::CaptureCardSource::formats(unsigned(device)):source::CaptureCardSource::formatsByPath(videoPath);}catch(...){}CoUninitialize();return result;});
}
void arrange(){RECT r{};GetClientRect(window,&r);const int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window));const int ys[]={0,44,114,184,448,448,14,84,380,154,254,224,294,514,484,606,566,542,338,342,254,224};for(int id=1;id<=21;++id){int x=id==5?width-152:id==18?width-156:(id==20||id==21)?width-180:16;int w=id==4?width-184:id==5||id==18?136:id==19?width-184:(id==10||id==11)?width-212:(id==20||id==21)?164:width-32;MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,ys[id]),dip(window,w),dip(window,(id<=3||id==10||id==13||id==16||id==20)?180:id==8?64:id==4||id==5?36:id==18?32:(id==12||id==15)?30:24),TRUE);}}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);titleTheme(h);auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style){auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);};
    remembered=CapturePreferenceStore(runtime::localDataDirectory()).load();
    add(L"EDIT",std::format(L"{:g}",remembered.requestedFps).c_str(),18,ES_AUTOHSCROLL|WS_TABSTOP);
    SendDlgItemMessageW(h,18,EM_SETLIMITTEXT,16,0);
    add(L"STATIC",L"Device frame rate FPS (0 = default, requires reconnect)",19,0);
    refreshAfterQuery=busy;
    for(int i=1;i<=3;++i)add(L"COMBOBOX",L"",i,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"COMBOBOX",L"",10,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Input color (change requires reconnect)",11,0);
    for(auto name:{L"Automatic · device metadata",L"Rec.2100 PQ · HDR10",L"Rec.2100 HLG",L"Rec.709 · SDR"})SendDlgItemMessageW(h,10,CB_ADDSTRING,0,LPARAM(name));SendDlgItemMessageW(h,10,CB_SETCURSEL,0,0);
    add(L"COMBOBOX",L"",20,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Input range",21,0);
    for(auto name:{L"Automatic",L"Limited",L"Full"})SendDlgItemMessageW(h,20,CB_ADDSTRING,0,LPARAM(name));SendDlgItemMessageW(h,20,CB_SETCURSEL,0,0);
    add(L"BUTTON",L"Convert to SDR display (all previews, takes effect immediately)",12,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",13,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Capture audio (change requires reconnect)",14,0);
    for(auto name:{L"Automatic: prefer linear PCM, decode Dolby/DTS bitstream when needed",L"Force linear PCM (do not accept Dolby/DTS bitstream)",L"Bitstream first: prefer passthrough to the receiver (decode to PCM when passthrough is unavailable)"})SendDlgItemMessageW(h,13,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(h,13,CB_SETCURSEL,WPARAM(std::clamp(readAudioIngress?readAudioIngress():0,0,2)),0);
    add(L"BUTTON",L"Flip image vertically (check when the capture image is upside down, takes effect immediately)",15,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip&&readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",16,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Device buffering (change requires reconnect)",17,0);
    for(auto name:{L"Automatic: 2 frames at 1080p and below, 3 frames at higher resolutions",L"Minimum: 1-frame buffer (lowest latency, may drop frames under heavy load)",L"Driver default: no recommendation, use the device's own setting"})SendDlgItemMessageW(h,16,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer?readBuffer():0,0,2)),0);
    add(L"BUTTON",L"Connect and start watching",4,BS_PUSHBUTTON|WS_TABSTOP);marked(GetDlgItem(h,4));add(L"BUTTON",L"Refresh devices",5,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"Video input device",6,0);add(L"STATIC",L"Formats the device actually supports",7,0);add(L"STATIC",L"",8,0);add(L"STATIC",L"Audio monitoring (only captures the selected input, off by default)",9,0);
    installDialogHelp(h,{{16,L"The number of driver buffers the capture card uses to deliver frames to the software. More buffers let the card queue ahead, but every extra frame adds a frame of wait; too few buffers and the driver drops frames when it cannot fetch them in time. Automatic: recommends 2 frames at 1080p and below, 3 frames at higher resolutions; Minimum: 1 frame, lowest latency but may drop frames under heavy load; Driver default: no intervention. Changing this requires reconnecting the capture card; the connection log records the recommended value and the value the driver actually returned (marked honestly when negotiation did not take effect)."},{15,L"For capture cards whose orientation declaration does not match the actual image: common with some RGB24 formats, where the device declares bottom-row-first in the media type but actually delivers top-row-first, so the image is upside down. When checked, the capture image is flipped vertically once, takes effect immediately; only affects the local capture preview, screenshots and images exported from here, and does not change the device or other formats. Do not check it when the image is normal; remember to uncheck it after switching to a non-inverted format."},{13,L"When the capture card delivers Dolby Atmos / Dolby Audio / DTS as a bitstream, this decides which path to use. The device's own audio format is not affected; changing this requires reconnecting the capture card. Automatic prefers PCM and only uses bitstream decoding when PCM is unavailable; Bitstream first is the opposite: it first tries to pass the bitstream through unchanged to the receiver/soundbar (exclusive output, no software decoding by the card; only the receiver can render the Atmos/DTS:X object audio; requires output-device support, see the bitstream-out log), and automatically falls back to decoding when no supported output endpoint exists. Suitable when the PS5 is set to Dolby output but the device also provides PCM."},{12,L"Also convert incoming HDR to SDR display, without changing the PS5 or Windows. Enhancement runs as usual; only the preview changes, video export is unaffected. Switching causes a brief pause; screenshots follow the current image. When off, it follows the display."},{10,L"Specify manually when the device does not report HDR information; requires selecting P010/P016. P010 can also carry SDR, so do not force an HDR label onto ordinary content."},{1,L"Select the capture card's video device. Do not accidentally pick a webcam to stream the PS5."},{2,L"Select the resolution, frame rate and pixel format the device actually provides. Sharpness, bandwidth and latency all depend on it."},{3,L"If the device has built-in HDMI audio, \"Use the video device's built-in audio\" is shown; otherwise select a separate audio device, or capture no audio."},{4,L"Connect the capture card with the current format and apply the current enhancement settings."},{5,L"Rescan devices and formats. When the device is in use by another program, a refresh may not reclaim it."}});
    installDialogHelp(h,{{18,L"Directly request the capture card to deliver frames at this rate, decimals supported. 0 uses the selected format; reconnect after changing. When the device does not support it or returns a different rate, an error is shown; frames are never silently dropped in the software. The capture card cannot tell which frames are duplicate game frames, so lowering the rate does not guarantee removing duplicate frames."}});
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
                if(restore>=0)query(restore);else SetDlgItemTextW(h,8,L"The last capture device is not connected. Reconnect it and refresh, or manually select another device.");
            }else{
                rebuildAudioList(h,-1);SetDlgItemTextW(h,8,L"No capture device found. Connect one and click Refresh.");
            }
        }else{
            formats=std::move(result.formats);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);
            for(auto& format:formats)SendDlgItemMessageW(h,2,CB_ADDSTRING,0,LPARAM(format.label.c_str()));
            int restore=formats.empty()?-1:0;
            const bool sameDevice=size_t(result.device)<videoDevices.size()&&videoDevices[size_t(result.device)].path==remembered.videoPath;
            if(sameDevice&&!remembered.formatKey.empty()){
                restore=-1;for(size_t i=0;i<formats.size();++i)if(formats[i].key==remembered.formatKey)restore=int(i);
            }
            const auto color=size_t(result.device)<videoDevices.size()?remembered.colorForDevice(videoDevices[size_t(result.device)].path):0;
            SendDlgItemMessageW(h,10,CB_SETCURSEL,source::captureColorSpace(color),0);
            SendDlgItemMessageW(h,20,CB_SETCURSEL,source::captureColorRange(color),0);
            EnableWindow(GetDlgItem(h,10),TRUE);EnableWindow(GetDlgItem(h,20),TRUE);
            SetDlgItemTextW(h,18,std::format(L"{:g}",sameDevice?remembered.requestedFps:0).c_str());
            SendDlgItemMessageW(h,2,CB_SETCURSEL,restore,0);EnableWindow(GetDlgItem(h,4),restore>=0);
            SetDlgItemTextW(h,8,formats.empty()?L"No valid capture format at or below 4K was read, or the device is in use by another application.":L"On connect, the current enhancement settings are used. Format and audio changes require a reconnect.");
            if(!formats.empty()&&restore<0)SetDlgItemTextW(h,8,L"The last format is no longer available; please reselect a format.");
            if(SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)==CB_ERR){EnableWindow(GetDlgItem(h,4),FALSE);SetDlgItemTextW(h,8,L"The last audio device is not connected. Please select an audio device, or explicitly choose not to monitor audio.");}
        }
    }else if(busy&&GetTickCount64()-queryStarted>5000)SetDlgItemTextW(h,8,L"The device query is taking a while. You can close this panel; current playback is unaffected.");
    return 0;
case WM_COMMAND:
    if((LOWORD(wp)==10||LOWORD(wp)==20)&&HIWORD(wp)==CBN_SELCHANGE){saveDeviceColor(h);return 0;}
    if(LOWORD(wp)==12&&HIWORD(wp)==BN_CLICKED){
        const bool enabled=SendDlgItemMessageW(h,12,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!setSdr(enabled))SetDlgItemTextW(h,8,L"Enhancement is currently switching; please try again shortly.");
        SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    }else if(LOWORD(wp)==15&&HIWORD(wp)==BN_CLICKED){
        const bool enabled=SendDlgItemMessageW(h,15,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!setFlip||!setFlip(enabled))SetDlgItemTextW(h,8,L"Enhancement is currently switching; please try again shortly.");
        if(readFlip)SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    }else if(LOWORD(wp)==13&&HIWORD(wp)==CBN_SELCHANGE){
        const int mode=int(SendDlgItemMessageW(h,13,CB_GETCURSEL,0,0));
        if(mode>=0&&setAudioIngress&&!setAudioIngress(mode))SetDlgItemTextW(h,8,L"Enhancement or capture is currently switching; please try again shortly.");
        if(readAudioIngress)SendDlgItemMessageW(h,13,CB_SETCURSEL,WPARAM(std::clamp(readAudioIngress(),0,2)),0);
    }else if(LOWORD(wp)==16&&HIWORD(wp)==CBN_SELCHANGE){
        const int mode=int(SendDlgItemMessageW(h,16,CB_GETCURSEL,0,0));
        if(mode>=0&&setBuffer&&!setBuffer(mode))SetDlgItemTextW(h,8,L"Enhancement or capture is currently switching; please try again shortly.");
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
        if(!source::parseCaptureFrameRate(rateText,requestedFps)){SetDlgItemTextW(h,8,L"For the capture frame rate, enter a number from 1 to 1000 (decimals allowed), or 0 to use the device default. ");SetFocus(GetDlgItem(h,18));return 0;}
        const bool audioIndexValid=SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)!=CB_ERR&&(audio<0||size_t(audio)<audioDevices.size());
        if(!busy&&device==queriedDevice&&format>=0&&size_t(format)<formats.size()&&device>=0&&size_t(device)<videoDevices.size()&&audioIndexValid){
            const auto* audioDevice=audio>=0?&audioDevices[size_t(audio)]:nullptr;
            const auto path=source::CaptureCardSource::makeCapturePath(unsigned(device),videoDevices[size_t(device)],formats[size_t(format)].index,audio,audioDevice,selectedColor(h),requestedFps,formats[size_t(format)].key);
            if(!path.empty()){
                auto colors=remembered.deviceColors;colors[videoDevices[size_t(device)].path]=selectedColor(h);
                remembered={videoDevices[size_t(device)].path,formats[size_t(format)].key,audioDevice?audioDevice->path:L"",audioDevice?(audioDevice->wasapi?source::kCaptureAudioWasapi:0):audio,selectedColor(h),remembered.formatHintDismissed,requestedFps,std::move(colors)};
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
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;
    // Do not block window destruction on a device enumeration still running:
    // park the future so its destructor waits on a detached helper instead.
    if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready){std::thread([f=std::move(pending)]()mutable{try{(void)f.get();}catch(...){}}).detach();}
    return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showCapturePanel(HWND parent,std::function<void(const std::wstring&)> callback,std::function<bool()> read,std::function<bool(bool)> write,std::function<int()> readIngress,std::function<bool(int)> writeIngress,std::function<bool()> readFlipped,std::function<bool(bool)> writeFlipped,std::function<int()> readBuffered,std::function<bool(int)> writeBuffered){start=std::move(callback);readSdr=std::move(read);setSdr=std::move(write);readAudioIngress=std::move(readIngress);setAudioIngress=std::move(writeIngress);readFlip=std::move(readFlipped);setFlip=std::move(writeFlipped);readBuffer=std::move(readBuffered);setBuffer=std::move(writeBuffered);if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Capture card · Connection settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,620),dip(parent,710),parent,nullptr,wc.hInstance,nullptr);}
}
