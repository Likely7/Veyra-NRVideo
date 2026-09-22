// SPDX-License-Identifier: GPL-3.0-only
#include "CaptureConfigBlock.h"
#include "Theme.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureFormatRank.h"
#include <format>
namespace veyra::ui {
namespace {
constexpr int kVideoDevice=1,kFormat=2,kAudioMonitor=3,kRefresh=5,kVideoLabel=6,kFormatLabel=7,kStatus=8,kAudioLabel=9,
    kColor=10,kColorLabel=11,kSdr=12,kIngress=13,kIngressLabel=14,kFlip=15,kBuffer=16,kBufferLabel=17,kFps=18,kFpsLabel=19;
}
void CaptureConfigBlock::create(HWND parent,HFONT font,int idBase,int timerId,Callbacks callbacks){
    parent_=parent;font_=font;idBase_=idBase;timerId_=timerId;callbacks_=std::move(callbacks);
    remembered_=CapturePreferenceStore(runtime::localDataDirectory()).load();
    auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style){
        auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,HMENU(INT_PTR(idBase_+id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,WPARAM(font_),TRUE);themeControl(c);return c;
    };
    add(L"EDIT",std::format(L"{:g}",remembered_.requestedFps).c_str(),kFps,ES_AUTOHSCROLL|WS_TABSTOP);
    SendDlgItemMessageW(parent_,idBase_+kFps,EM_SETLIMITTEXT,16,0);
    add(L"STATIC",L"设备帧率 FPS（0 = 默认，需重连）",kFpsLabel,0);
    for(int i:{kVideoDevice,kFormat,kAudioMonitor})add(L"COMBOBOX",L"",i,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"COMBOBOX",L"",kColor,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"输入颜色（变更需重连）",kColorLabel,0);
    for(auto name:{L"自动识别 SDR / HDR · 设备元数据",L"手动 HDR10 / PQ · BT.2020",L"手动 HLG · BT.2020 / 1000nit参考"})SendDlgItemMessageW(parent_,idBase_+kColor,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(parent_,idBase_+kColor,CB_SETCURSEL,0,0);
    add(L"BUTTON",L"转为 SDR 显示（所有预览，立即生效）",kSdr,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(parent_,idBase_+kSdr,BM_SETCHECK,callbacks_.readSdr&&callbacks_.readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",kIngress,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"采集音频（变更需重连）",kIngressLabel,0);
    for(auto name:{L"自动：优先线性 PCM，必要时 Dolby/DTS 位流解码",L"强制线性 PCM（不接受 Dolby/DTS 位流）",L"位流优先：优先直通给功放（无直通时解码为 PCM）"})SendDlgItemMessageW(parent_,idBase_+kIngress,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(parent_,idBase_+kIngress,CB_SETCURSEL,WPARAM(std::clamp(callbacks_.readAudioIngress?callbacks_.readAudioIngress():0,0,2)),0);
    add(L"BUTTON",L"画面上下翻转（采集画面倒置时勾选，立即生效）",kFlip,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(parent_,idBase_+kFlip,BM_SETCHECK,callbacks_.readFlip&&callbacks_.readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",kBuffer,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"设备缓冲（变更需重连）",kBufferLabel,0);
    for(auto name:{L"自动：1080p 及以下 2 帧，更高分辨率 3 帧",L"最小：1 帧缓冲（延迟最低，高负载可能丢帧）",L"驱动默认：不做建议，沿用设备自身设置"})SendDlgItemMessageW(parent_,idBase_+kBuffer,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(parent_,idBase_+kBuffer,CB_SETCURSEL,WPARAM(std::clamp(callbacks_.readBuffer?callbacks_.readBuffer():0,0,2)),0);
    add(L"BUTTON",L"刷新设备",kRefresh,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"视频输入设备",kVideoLabel,0);add(L"STATIC",L"设备实际支持的格式",kFormatLabel,0);
    add(L"STATIC",L"",kStatus,0);add(L"STATIC",L"音频监听（仅采集所选输入，默认关闭）",kAudioLabel,0);
    SetTimer(parent_,timerId_,100,nullptr);
    query(-1);
}
void CaptureConfigBlock::destroy(){
    if(parent_&&timerId_)KillTimer(parent_,timerId_);
    // A running enumeration owns no part of this object; drop its result by
    // moving the future into a detached reaper instead of blocking teardown.
    if(pending_.valid())std::thread([f=std::move(pending_)]{}).detach();
    pending_={};busy_=false;parent_=nullptr;
}
void CaptureConfigBlock::setVisible(bool visible){
    for(int id:{kVideoDevice,kFormat,kAudioMonitor,kRefresh,kVideoLabel,kFormatLabel,kStatus,kAudioLabel,kColor,kColorLabel,kSdr,kIngress,kIngressLabel,kFlip,kBuffer,kBufferLabel,kFps,kFpsLabel})
        if(auto h=item(id))ShowWindow(h,visible?SW_SHOW:SW_HIDE);
}
bool CaptureConfigBlock::selectionReady()const{
    if(busy_)return false;
    return SendDlgItemMessageW(parent_,idBase_+kFormat,CB_GETCURSEL,0,0)>=0
        &&SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_GETCURSEL,0,0)!=CB_ERR;
}
void CaptureConfigBlock::rebuildAudioList(int device){
    SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_RESETCONTENT,0,0);SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_ADDSTRING,0,LPARAM(L"不监听音频"));
    const bool videoSelected=device>=0&&size_t(device)<videoDevices_.size();
    if(videoSelected)SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_ADDSTRING,0,LPARAM(videoDevices_[size_t(device)].hasEmbeddedAudio?L"使用视频设备内置音频（已检测）":L"尝试视频设备内置音频"));
    for(auto& audio:audioDevices_){const auto label=std::format(L"[{}] {}",audio.wasapi?L"WASAPI":L"DirectShow",audio.name);SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_ADDSTRING,0,LPARAM(label.c_str()));}
    int restore=0;
    if(videoSelected&&videoDevices_[size_t(device)].path==remembered_.videoPath){
        if(remembered_.audioMode==source::kCaptureAudioFromVideoDevice)restore=1;
        else if(!remembered_.audioPath.empty()){
            restore=-1;
            for(size_t i=0;i<audioDevices_.size();++i)
                if(audioDevices_[i].path==remembered_.audioPath&&audioDevices_[i].wasapi==(remembered_.audioMode==source::kCaptureAudioWasapi))restore=int(i)+2;
        }
    }
    SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_SETCURSEL,restore,0);
}
int CaptureConfigBlock::selectedAudio(int device)const{
    const int selection=int(SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_GETCURSEL,0,0));if(selection<=0)return source::kCaptureAudioDisabled;
    const bool videoSelected=device>=0&&size_t(device)<videoDevices_.size();
    if(videoSelected&&selection==1)return source::kCaptureAudioFromVideoDevice;
    return selection-1-(videoSelected?1:0);
}
void CaptureConfigBlock::maybeShowFormatHint(){
    const int index=int(SendDlgItemMessageW(parent_,idBase_+kFormat,CB_GETCURSEL,0,0));
    if(index<0||size_t(index)>=formats_.size()||remembered_.formatHintDismissed)return;
    const auto& format=formats_[size_t(index)];
    const auto tier=static_cast<source::CaptureFormatTier>(format.tier);
    if(!source::captureFormatNeedsCostHint(tier))return;
    setStatusText(std::format(L"提示：{} 在高分辨率/高帧率下比低延迟格式多一道处理环节（{}）。建议优先选低延迟格式；此提示只出现一次。",format.label,source::captureFormatTierLabel(tier)));
    remembered_.formatHintDismissed=true;
    CapturePreferenceStore(runtime::localDataDirectory()).save(remembered_);
}
void CaptureConfigBlock::query(int device){
    if(busy_)return;busy_=true;queriedDevice_=device;queryStarted_=GetTickCount64();
    setStatusText(L"正在查询设备能力…当前播放继续");
    EnableWindow(item(kRefresh),FALSE);EnableWindow(item(kVideoDevice),FALSE);
    if(callbacks_.stateChanged)callbacks_.stateChanged();
    const std::wstring videoPath=device>=0&&size_t(device)<videoDevices_.size()?videoDevices_[size_t(device)].path:L"";
    pending_=std::async(std::launch::async,[device,videoPath]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);Query result;result.device=device;try{if(device<0){result.video=source::CaptureCardSource::deviceDetails();result.audio=source::CaptureCardSource::deviceDetails(true);}else result.formats=videoPath.empty()?source::CaptureCardSource::formats(unsigned(device)):source::CaptureCardSource::formatsByPath(videoPath);}catch(...){}CoUninitialize();return result;});
}
void CaptureConfigBlock::poll(){
    if(!parent_)return;
    SendDlgItemMessageW(parent_,idBase_+kSdr,BM_SETCHECK,callbacks_.readSdr&&callbacks_.readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    if(callbacks_.readFlip)SendDlgItemMessageW(parent_,idBase_+kFlip,BM_SETCHECK,callbacks_.readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    if(callbacks_.readBuffer)SendDlgItemMessageW(parent_,idBase_+kBuffer,CB_SETCURSEL,WPARAM(std::clamp(callbacks_.readBuffer(),0,2)),0);
    if(!busy_)return;
    if(pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready){
        if(GetTickCount64()-queryStarted_>5000)setStatusText(L"设备查询耗时较长。可以关闭此面板，当前播放不受影响。");
        return;
    }
    auto result=pending_.get();busy_=false;
    EnableWindow(item(kRefresh),TRUE);EnableWindow(item(kVideoDevice),TRUE);
    if(refreshAfterQuery_){refreshAfterQuery_=false;query(-1);return;}
    if(result.device<0){
        videoDevices_=std::move(result.video);audioDevices_=std::move(result.audio);
        SendDlgItemMessageW(parent_,idBase_+kVideoDevice,CB_RESETCONTENT,0,0);SendDlgItemMessageW(parent_,idBase_+kFormat,CB_RESETCONTENT,0,0);formats_.clear();
        for(auto& video:videoDevices_)SendDlgItemMessageW(parent_,idBase_+kVideoDevice,CB_ADDSTRING,0,LPARAM(video.name.c_str()));
        if(!videoDevices_.empty()){
            int restore=remembered_.videoPath.empty()?0:-1;
            for(size_t i=0;i<videoDevices_.size();++i)if(videoDevices_[i].path==remembered_.videoPath)restore=int(i);
            SendDlgItemMessageW(parent_,idBase_+kVideoDevice,CB_SETCURSEL,restore,0);rebuildAudioList(restore);
            if(restore>=0)query(restore);else setStatusText(L"上次采集设备未连接。插回后刷新，或手动选择其他设备。");
        }else{
            rebuildAudioList(-1);setStatusText(L"未找到采集设备。连接后点击刷新。");
        }
    }else{
        formats_=std::move(result.formats);SendDlgItemMessageW(parent_,idBase_+kFormat,CB_RESETCONTENT,0,0);
        for(auto& format:formats_)SendDlgItemMessageW(parent_,idBase_+kFormat,CB_ADDSTRING,0,LPARAM(format.label.c_str()));
        int restore=formats_.empty()?-1:0;
        const bool sameDevice=size_t(result.device)<videoDevices_.size()&&videoDevices_[size_t(result.device)].path==remembered_.videoPath;
        if(sameDevice&&!remembered_.formatKey.empty()){
            restore=-1;for(size_t i=0;i<formats_.size();++i)if(formats_[i].key==remembered_.formatKey)restore=int(i);
            SendDlgItemMessageW(parent_,idBase_+kColor,CB_SETCURSEL,remembered_.colorOverride,0);
        }else SendDlgItemMessageW(parent_,idBase_+kColor,CB_SETCURSEL,0,0);
        SetDlgItemTextW(parent_,idBase_+kFps,std::format(L"{:g}",sameDevice?remembered_.requestedFps:0).c_str());
        SendDlgItemMessageW(parent_,idBase_+kFormat,CB_SETCURSEL,restore,0);
        setStatusText(formats_.empty()?L"未读到有效的4K以内采集格式，或设备正被其他应用占用。":L"连接后使用当前增强设置。格式与音频变更需要重新连接。");
        if(!formats_.empty()&&restore<0)setStatusText(L"上次格式已不可用，请重新选择格式。");
        if(SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_GETCURSEL,0,0)==CB_ERR){setStatusText(L"上次音频设备未连接。请选择音频设备，或明确选择不监听音频。");}
    }
    if(callbacks_.stateChanged)callbacks_.stateChanged();
}
bool CaptureConfigBlock::handleCommand(WPARAM wp,LPARAM){
    const int notification=HIWORD(wp);
    switch(LOWORD(wp)-idBase_){
    case kSdr:
        if(notification==BN_CLICKED){
            const bool enabled=SendDlgItemMessageW(parent_,idBase_+kSdr,BM_GETCHECK,0,0)==BST_CHECKED;
            if(!callbacks_.setSdr||!callbacks_.setSdr(enabled))setStatusText(L"当前正在切换增强，请稍后再试。");
            SendDlgItemMessageW(parent_,idBase_+kSdr,BM_SETCHECK,callbacks_.readSdr&&callbacks_.readSdr()?BST_CHECKED:BST_UNCHECKED,0);
        }
        return true;
    case kFlip:
        if(notification==BN_CLICKED){
            const bool enabled=SendDlgItemMessageW(parent_,idBase_+kFlip,BM_GETCHECK,0,0)==BST_CHECKED;
            if(!callbacks_.setFlip||!callbacks_.setFlip(enabled))setStatusText(L"当前正在切换增强，请稍后再试。");
            if(callbacks_.readFlip)SendDlgItemMessageW(parent_,idBase_+kFlip,BM_SETCHECK,callbacks_.readFlip()?BST_CHECKED:BST_UNCHECKED,0);
        }
        return true;
    case kIngress:
        if(notification==CBN_SELCHANGE){
            const int mode=int(SendDlgItemMessageW(parent_,idBase_+kIngress,CB_GETCURSEL,0,0));
            if(mode>=0&&callbacks_.setAudioIngress&&!callbacks_.setAudioIngress(mode))setStatusText(L"当前正在切换增强或采集；请稍后再试。");
            if(callbacks_.readAudioIngress)SendDlgItemMessageW(parent_,idBase_+kIngress,CB_SETCURSEL,WPARAM(std::clamp(callbacks_.readAudioIngress(),0,2)),0);
        }
        return true;
    case kBuffer:
        if(notification==CBN_SELCHANGE){
            const int mode=int(SendDlgItemMessageW(parent_,idBase_+kBuffer,CB_GETCURSEL,0,0));
            if(mode>=0&&callbacks_.setBuffer&&!callbacks_.setBuffer(mode))setStatusText(L"当前正在切换增强或采集；请稍后再试。");
            if(callbacks_.readBuffer)SendDlgItemMessageW(parent_,idBase_+kBuffer,CB_SETCURSEL,WPARAM(std::clamp(callbacks_.readBuffer(),0,2)),0);
        }
        return true;
    case kVideoDevice:
        if(notification==CBN_SELCHANGE){
            const int device=int(SendDlgItemMessageW(parent_,idBase_+kVideoDevice,CB_GETCURSEL,0,0));rebuildAudioList(device);query(device);
            if(callbacks_.stateChanged)callbacks_.stateChanged();
        }
        return true;
    case kFormat:case kAudioMonitor:
        if(notification==CBN_SELCHANGE){
            if(callbacks_.stateChanged)callbacks_.stateChanged();
            if(LOWORD(wp)-idBase_==kFormat)maybeShowFormatHint();
        }
        return true;
    case kRefresh:query(-1);return true;
    }
    return LOWORD(wp)>=idBase_+kVideoDevice&&LOWORD(wp)<=idBase_+kFpsLabel;
}
std::wstring CaptureConfigBlock::capturePath(){
    const int device=int(SendDlgItemMessageW(parent_,idBase_+kVideoDevice,CB_GETCURSEL,0,0));
    const int format=int(SendDlgItemMessageW(parent_,idBase_+kFormat,CB_GETCURSEL,0,0));
    const int audio=selectedAudio(device);
    wchar_t rateText[32]{};GetDlgItemTextW(parent_,idBase_+kFps,rateText,32);double requestedFps=0;
    if(!source::parseCaptureFrameRate(rateText,requestedFps)){setStatusText(L"采集帧率请输入 1–1000 的数字（可带小数），或填 0 沿用设备默认。 ");return {};}
    const bool audioIndexValid=SendDlgItemMessageW(parent_,idBase_+kAudioMonitor,CB_GETCURSEL,0,0)!=CB_ERR&&(audio<0||size_t(audio)<audioDevices_.size());
    if(busy_||device!=queriedDevice_||format<0||format>=int(formats_.size())||device<0||size_t(device)>=videoDevices_.size()||!audioIndexValid)return {};
    const auto* audioDevice=audio>=0?&audioDevices_[size_t(audio)]:nullptr;
    const unsigned colorOverride=unsigned(SendDlgItemMessageW(parent_,idBase_+kColor,CB_GETCURSEL,0,0));
    const auto path=source::CaptureCardSource::makeCapturePath(unsigned(device),videoDevices_[size_t(device)],formats_[size_t(format)].index,audio,audioDevice,colorOverride,requestedFps);
    if(!path.empty()){
        remembered_={videoDevices_[size_t(device)].path,formats_[size_t(format)].key,audioDevice?audioDevice->path:L"",audioDevice?(audioDevice->wasapi?source::kCaptureAudioWasapi:0):audio,colorOverride,remembered_.formatHintDismissed,requestedFps};
        if(!CapturePreferenceStore(runtime::localDataDirectory()).save(remembered_))log::warn("capture","Failed to save capture selection");
    }
    return path;
}
void CaptureConfigBlock::arrange(int x,int y,int width){
    auto move=[&](int id,int cx,int cy,int w,int h){MoveWindow(item(id),dip(parent_,x+cx),dip(parent_,y+cy),dip(parent_,w),dip(parent_,h),TRUE);};
    move(kVideoLabel,16,14,width-32,24);move(kVideoDevice,16,44,width-32,180);
    move(kFormatLabel,16,84,width-32,24);move(kFormat,16,114,width-32,180);
    move(kAudioLabel,16,154,width-32,24);move(kAudioMonitor,16,184,width-32,180);
    move(kColorLabel,16,224,width-32,24);move(kColor,16,254,width-32,180);
    move(kSdr,16,294,width-32,24);
    move(kFpsLabel,16,342,width-184,24);move(kFps,width-156,338,136,32);
    move(kStatus,16,380,width-32,56);
    move(kRefresh,width-152,448,136,36);
    move(kIngressLabel,16,484,width-32,24);move(kIngress,16,514,width-32,180);
    move(kBufferLabel,16,542,width-32,24);move(kBuffer,16,566,width-32,180);
    move(kFlip,16,602,width-32,24);
}
int CaptureConfigBlock::arrangeCompact(int x,int y,int width){
    auto move=[&](int id,int cx,int cy,int w,int h){MoveWindow(item(id),dip(parent_,x+cx),dip(parent_,y+cy),dip(parent_,w),dip(parent_,h),TRUE);};
    const int left=0,rightW=width*2/5,leftW=width-rightW-12,right=leftW+12;
    move(kVideoLabel,left,0,leftW,24);move(kVideoDevice,left,26,width,180);
    move(kFormatLabel,left,64,leftW,24);move(kFormat,left,90,leftW,180);
    move(kColorLabel,right,64,rightW,24);move(kColor,right,90,rightW,180);
    move(kAudioLabel,left,128,leftW,24);move(kAudioMonitor,left,154,leftW,180);
    move(kIngressLabel,right,128,rightW,24);move(kIngress,right,154,rightW,180);
    move(kFpsLabel,left,192,leftW,24);move(kFps,left,218,leftW/2-6,32);move(kBufferLabel,right,192,rightW,24);move(kBuffer,right,218,rightW,180);
    move(kSdr,left,256,leftW+rightW/2,24);move(kFlip,left,284,leftW+rightW/2,24);
    move(kStatus,left,318,width,44);
    move(kRefresh,width-136,372,136,32);
    return 414;
}
std::vector<std::pair<int,const wchar_t*>> CaptureConfigBlock::helpEntries()const{
    return {
        {idBase_+kBuffer,L"采集卡往软件送帧用的驱动缓冲数量。缓冲越多，卡可以提前排队，但每多一帧就多一帧的等待；缓冲太少，驱动来不及取走就会丢帧。自动：1080p 及以下建议 2 帧、更高分辨率建议 3 帧；最小：1 帧，延迟最低但高负载可能丢帧；驱动默认：不干预。改这个要重新连接采集卡，连接日志里会写建议值和驱动实际给的值（协商未生效时如实标注）。"},
        {idBase_+kFlip,L"给方向声明和实际画面不一致的采集卡用：常见于某些 RGB24 格式——设备在媒体类型里写的是底行在前，实际送来的却是顶行在前，于是画面上下颠倒。勾选后把采集画面上下翻转一次，立即生效；只影响本机采集预览、截图和从这里导出的画面，不改设备也不动其他格式。画面正常时不要勾选；换成不颠倒的格式后记得取消。"},
        {idBase_+kIngress,L"采集卡把 Dolby Atmos / Dolby Audio / DTS 以位流送来时，这里决定用哪条路。设备本身的音频格式不受影响；改这个要重新连接采集卡。自动优先 PCM，PCM 不可用时才走位流解码；位流优先则反过来：先尝试把位流原样直通给功放/回音壁（独占输出，卡不经过软件解码，只有接收端才能解出 Atmos/DTS:X 的对象声场；需要输出设备支持，探测见日志 bitstream-out），没有支持的输出端点时自动回退到解码。适合 PS5 已设成 Dolby 输出、但设备同时提供 PCM 的情况。"},
        {idBase_+kSdr,L"收到HDR也转成SDR显示，不用改PS5或Windows。增强照常用；只改预览，视频导出不受影响。切换会短暂停顿，截图跟随当前画面。关闭后跟随显示器。"},
        {idBase_+kColor,L"设备没报HDR信息时手动指定，需选P010/P016。P010也可能装SDR，别给普通画面强戴HDR帽子。"},
        {idBase_+kVideoDevice,L"选采集卡的视频设备。别把摄像头误请来直播PS5。"},
        {idBase_+kFormat,L"选设备真实提供的分辨率、帧率和像素格式。清晰度、带宽和延迟都受它影响。"},
        {idBase_+kAudioMonitor,L"如果设备自带 HDMI 音频，会显示“使用视频设备内置音频”；否则选择独立音频设备，也可不采声音。"},
        {idBase_+kRefresh,L"重新扫描设备和格式。设备被其他软件占用时，刷新不一定能抢回来。"},
        {idBase_+kFps,L"直接请求采集卡按此帧率送帧，支持小数。0 沿用所选格式；更改后重新连接。设备不支持或返回其他帧率时会报错，不在软件中偷偷丢帧。采集卡不能判断哪些画面是游戏重复帧，降帧率不保证消除重复画面。"}};
}
}
