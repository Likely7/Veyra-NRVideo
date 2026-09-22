#include "RemotePlayPanel.h"
#include "Theme.h"
#include "SettingHelp.h"
#include "veyra/remoteplay/PsnAuth.h"
#include <shellapi.h>
#include "veyra/Log.h"
#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/remoteplay/Discovery.h"
#include "veyra/RuntimePaths.h"
#include <atomic>
#include <cctype>
#include <format>
#include <mutex>
#include <thread>
namespace veyra::ui {
namespace {
HWND window=nullptr;HFONT font=nullptr;
enum {Host=1,Account,PairPin,Quality,CodecChoice,Pair,Connect,Cancel,Scan,Wake,LoginPin,SendPin,StatusText,Help,Bitrate,Forget,ViewOnly,Calibrate,DecodeChoice,LoginPsn,CompletePsn,ForgetPsn,SamplingChoice,UseCapture};
constexpr int CaptureHeader=108;
constexpr int CaptureIdBase=600,CaptureTimerId=2;
CaptureConfigBlock captureBlock;
bool captureExpanded=false;
uint64_t psnLoginStarted=0,psnRefreshAfter=0;
std::function<bool()> calibrate;
std::function<void(source::RemotePlayConnectDesc)> connect;
std::function<void(source::RemotePlayConnectDesc,const std::wstring&)> connectCapture;
CaptureConfigBlock::Callbacks captureSettings;
std::function<void(std::string)> login;
std::function<RemotePlayPanelStatus()> connectionStatus;std::function<void()> disconnect;bool watching=false;
std::jthread worker;std::atomic<bool> done=false;bool busy=false,closing=false;
struct Outcome {std::wstring message;std::vector<remoteplay::DiscoveredConsole> hosts;std::filesystem::path savedPath;std::optional<remoteplay::AccountId> account;};
std::mutex mutex;Outcome outcome;
struct SavedProfile {std::string host;std::filesystem::path path;};
unsigned profileReadFailures=0;
std::vector<SavedProfile> profiles;std::filesystem::path currentProfile;
constexpr uint32_t bitrates[]={5000,10000,15000,20000,30000,50000,80000,100000};
auto profilePath(){return currentProfile;}
std::filesystem::path pathForHost(std::string_view host,const remoteplay::AccountId& account){
    for(const auto& profile:profiles)if(profile.host==host){auto saved=remoteplay::loadProfile(profile.path);if(saved&&saved->credentials.accountId==account)return profile.path;}
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))return {};
    std::string id;const auto* bytes=reinterpret_cast<const unsigned char*>(&guid);
    for(size_t i=0;i<sizeof(guid);++i)id+=std::format("{:02x}",bytes[i]);
    return remoteplay::profileDirectory()/(id+".dat");
}
void loadSelection(){
    if(auto saved=remoteplay::loadProfile(currentProfile)){
        SetDlgItemTextW(window,Host,std::wstring(saved->host.begin(),saved->host.end()).c_str());
        CheckDlgButton(window,ViewOnly,saved->viewOnly?BST_CHECKED:BST_UNCHECKED);
        auto id=remoteplay::accountIdToBase64(saved->credentials.accountId);SetDlgItemTextW(window,Account,std::wstring(id.begin(),id.end()).c_str());
        SendDlgItemMessageW(window,Quality,CB_SETCURSEL,(saved->video.height==1080?2:0)+(saved->video.fps==60?1:0),0);
        SendDlgItemMessageW(window,CodecChoice,CB_SETCURSEL,int(saved->video.codec),0);
        const auto found=std::find(std::begin(bitrates),std::end(bitrates),saved->video.bitrateKbps);
        SendDlgItemMessageW(window,Bitrate,CB_SETCURSEL,found==std::end(bitrates)?2:found-std::begin(bitrates),0);
    }
}
void refreshProfiles(){
    if(currentProfile.empty()){
        wchar_t last[80]{};GetPrivateProfileStringW(L"RemotePlay",L"LastProfile",L"",last,80,(remoteplay::profileDirectory()/L"settings.ini").c_str());
        std::wstring name=last;if(!name.empty()&&name.find_first_of(L"/\\:")==name.npos)currentProfile=remoteplay::profileDirectory()/name;
    }
    profileReadFailures=0;profiles.clear();SendDlgItemMessageW(window,Host,CB_RESETCONTENT,0,0);
    auto add=[](const std::filesystem::path& path){remoteplay::ProfileLoadError error;auto saved=remoteplay::loadProfile(path,&error);if(saved)profiles.push_back({saved->host,path});else if(error!=remoteplay::ProfileLoadError::Missing){++profileReadFailures;veyra::log::warn("remoteplay-profile",std::format("Cannot load saved profile: category={} (no credentials logged)",int(error)));}};
    add(remoteplay::profileDirectory()/"remoteplay-profile.dat");
    std::error_code ec;const auto directory=remoteplay::profileDirectory();
    size_t scanned=0;
    for(std::filesystem::directory_iterator it(directory,ec),end;!ec&&it!=end&&profiles.size()<64&&scanned<256;it.increment(ec),++scanned){
        const auto id=it->path().stem().wstring();
        if(it->path().extension()==".dat"&&id.size()==32&&std::all_of(id.begin(),id.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))add(it->path());
    }
    int selected=-1;for(size_t i=0;i<profiles.size();++i){auto& p=profiles[i];SendDlgItemMessageW(window,Host,CB_ADDSTRING,0,LPARAM(std::wstring(p.host.begin(),p.host.end()).c_str()));if(p.path==currentProfile)selected=int(i);}
    if(selected<0&&!profiles.empty()){selected=0;currentProfile=profiles[0].path;}
    if(selected>=0){SendDlgItemMessageW(window,Host,CB_SETCURSEL,selected,0);loadSelection();}
    else currentProfile.clear();
}
std::wstring text(int id){auto h=GetDlgItem(window,id);std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.resize(wcslen(s.c_str()));return s;}
std::string ascii(int id){auto s=text(id);if(std::any_of(s.begin(),s.end(),[](wchar_t c){return c>127;}))return {};std::string result;result.reserve(s.size());for(auto c:s)result.push_back(static_cast<char>(c));return result;}
remoteplay::VideoProfile video(){
    remoteplay::VideoProfile p;const auto q=SendDlgItemMessageW(window,Quality,CB_GETCURSEL,0,0);
    p.width=q<2?1280:1920;p.height=q<2?720:1080;p.fps=(q==0||q==2)?30:60;
    p.codec=static_cast<remoteplay::Codec>(std::clamp<int>(int(SendDlgItemMessageW(window,CodecChoice,CB_GETCURSEL,0,0)),0,2));const auto rate=SendDlgItemMessageW(window,Bitrate,CB_GETCURSEL,0,0);p.bitrateKbps=bitrates[std::clamp<int>(int(rate),0,7)];return p;
}
void buttons(){for(int id:{Pair,Connect,Scan,Wake,Host,Account,PairPin,Quality,CodecChoice,Bitrate,SamplingChoice,Forget,LoginPsn,CompletePsn,ForgetPsn})EnableWindow(GetDlgItem(window,id),!busy);EnableWindow(GetDlgItem(window,Cancel),busy);
    // The combined capture mode uses a fixed low keepalive profile; the video
    // request controls are meaningless there and stay disabled.
    if(captureExpanded)for(int id:{Quality,CodecChoice,Bitrate,DecodeChoice,SamplingChoice})EnableWindow(GetDlgItem(window,id),FALSE);}
template<class Work> void launch(Work work){
    if(busy)return;if(worker.joinable())worker.join();busy=true;done=false;buttons();SetDlgItemTextW(window,StatusText,L"正在处理…可随时取消");
    worker=std::jthread([work=std::move(work)](std::stop_token stop)mutable{
        Outcome result;
        try{result=work(stop);}catch(...){result.message=L"操作失败；请检查网络、输入和可用磁盘空间。";}
        {std::lock_guard lock(mutex);outcome=std::move(result);}done=true;
    });
}
void arrange(){
    auto move=[](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,y),dip(window,w),dip(window,h),TRUE);};
    move(LoginPsn,20,620,145,30);move(CompletePsn,175,620,200,30);move(ForgetPsn,385,620,167,30);
    move(Host,160,18,270,180);move(Scan,440,18,112,28);
    move(Account,160,62,392,28);move(PairPin,160,106,180,28);move(Pair,352,106,200,30);
    move(Quality,160,150,220,150);move(CodecChoice,392,150,160,150);
    move(Connect,160,199,180,36);move(Wake,352,199,200,36);
    move(LoginPin,160,250,180,28);move(SendPin,352,250,200,30);
    move(Bitrate,160,295,180,180);move(Forget,352,295,80,32);move(Cancel,440,295,112,32);move(StatusText,20,341,530,64);
    move(ViewOnly,20,408,400,28);move(UseCapture,20,436,530,28);move(Calibrate,432,408,120,28);
    move(DecodeChoice,160,470,392,150);move(SamplingChoice,160,514,392,120);move(Help,20,547,530,68);
    // Combined mode puts the capture configuration in a right-hand column:
    // the window widens instead of growing a long bottom tail.
    move(CaptureHeader,600,20,370,24);
    if(captureExpanded)captureBlock.arrangeColumn(600,44,370);
}
void applyCaptureExpanded(HWND h){
    captureBlock.setVisible(captureExpanded);
    if(auto header=GetDlgItem(h,CaptureHeader))ShowWindow(header,captureExpanded?SW_SHOW:SW_HIDE);
    EnableWindow(GetDlgItem(h,ViewOnly),!captureExpanded&&!busy);
    if(captureExpanded)CheckDlgButton(h,ViewOnly,BST_UNCHECKED);
    SetWindowPos(h,nullptr,0,0,dip(h,captureExpanded?990:590),dip(h,709),SWP_NOMOVE|SWP_NOZORDER);
    arrange();
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp)try{switch(msg){
case WM_CREATE:{window=h;closing=false;font=makeFont(h);titleTheme(h);
    auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x=0,int y=0,int w=1,int height=1){
        auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,dip(h,x),dip(h,y),dip(h,w),dip(h,height),h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);return c;
    };
    add(L"BUTTON",L"仅观看（手柄连接 PS5；更改后需重新连接）",ViewOnly,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"BUTTON",L"启用采集卡（画面与声音走采集卡，PS5 远程仅作控制；更改后需重新连接）",UseCapture,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"BUTTON",L"校准陀螺仪",Calibrate,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"解码（重连生效）",106,0,20,445,138,25);
    add(L"COMBOBOX",L"",DecodeChoice,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(auto label:{L"自动 · 优先硬解",L"CPU 软件解码",L"D3D12VA 硬件解码"})SendDlgItemMessageW(h,DecodeChoice,CB_ADDSTRING,0,LPARAM(label));
    SendDlgItemMessageW(h,DecodeChoice,CB_SETCURSEL,std::min(2u,GetPrivateProfileIntW(L"RemotePlay",L"DecodeMode",0,(remoteplay::profileDirectory()/L"settings.ini").c_str())),0);
    add(L"STATIC",L"采样（重连生效）",107,0,20,489,138,25);
    add(L"COMBOBOX",L"",SamplingChoice,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(auto label:{L"兼容采样 · 原有方式",L"精细采样 · 色度重建与双三次缩放"})SendDlgItemMessageW(h,SamplingChoice,CB_ADDSTRING,0,LPARAM(label));
    SendDlgItemMessageW(h,SamplingChoice,CB_SETCURSEL,std::min(1u,GetPrivateProfileIntW(L"RemotePlay",L"FineSampling",1,(remoteplay::profileDirectory()/L"settings.ini").c_str())),0);
    add(L"STATIC",L"PS5 地址",100,0,20,21,130,25);add(L"STATIC",L"PSN Account ID",101,0,20,65,130,25);
    add(L"STATIC",L"8 位配对码",102,0,20,109,130,25);add(L"STATIC",L"格式（重连生效）",103,0,20,153,130,25);add(L"STATIC",L"登录 PIN（可选）",104,0,20,253,135,25);
    add(L"STATIC",L"码率请求",105,0,20,298,130,25);
    add(L"STATIC",L"采集卡 · 画面与声音来源（组合模式）",CaptureHeader,0,600,20,370,24);ShowWindow(GetDlgItem(h,CaptureHeader),SW_HIDE);add(L"COMBOBOX",L"",Host,CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_TABSTOP);SendDlgItemMessageW(h,Host,CB_LIMITTEXT,253,0);
    for(int id:{Account,PairPin,LoginPin}){add(L"EDIT",L"",id,WS_TABSTOP|ES_AUTOHSCROLL|((id==PairPin||id==LoginPin)?ES_PASSWORD:0));SendDlgItemMessageW(h,id,EM_SETLIMITTEXT,id==Host?253:id==Account?24:8,0);}
    add(L"COMBOBOX",L"",Quality,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"720p · 30 fps",L"720p · 60 fps",L"1080p · 30 fps",L"1080p · 60 fps"})SendDlgItemMessageW(h,Quality,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,Quality,CB_SETCURSEL,3,0);
    add(L"COMBOBOX",L"",CodecChoice,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"H.264 · SDR",L"H.265 · SDR",L"H.265 · HDR（实验）"})SendDlgItemMessageW(h,CodecChoice,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,CodecChoice,CB_SETCURSEL,0,0);
    add(L"COMBOBOX",L"",Bitrate,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto rate:bitrates){auto label=std::format(L"{} Mbps",rate/1000);SendDlgItemMessageW(h,Bitrate,CB_ADDSTRING,0,LPARAM(label.c_str()));}SendDlgItemMessageW(h,Bitrate,CB_SETCURSEL,2,0);
    add(L"BUTTON",L"登录 PSN",LoginPsn,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"从剪贴板提交登录结果",CompletePsn,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"退出 PSN",ForgetPsn,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"删除配对",Forget,BS_PUSHBUTTON|WS_TABSTOP);
    for(auto [id,label]:{std::pair{Pair,L"配对并保存"},{Connect,L"连接并观看"},{Cancel,L"取消操作"},{Scan,L"查找主机"},{Wake,L"唤醒已配对 PS5"},{SendPin,L"提交登录 PIN"}})add(L"BUTTON",label,id,BS_PUSHBUTTON|WS_TABSTOP);
    marked(GetDlgItem(h,Connect));
    add(L"STATIC",L"首次使用先配对，之后直接连接。",StatusText,0);
    add(L"STATIC",L"PS5：设置 → 系统 → 远程游玩 → 启用远程游玩 → 关联设备。\nAccount ID 填账号数字 ID 或对应 8 字节 Base64，不是昵称或密码。\n电脑与 PS5 先连接同一局域网。配对信息仅在本机加密保存。\n登录 PIN 仅在 PS5 提示时填写；取消操作不会关闭当前视频。",Help,0);
    const auto migration=remoteplay::migrateProfiles(runtime::localDataDirectory());
    refreshProfiles();
    if(profileReadFailures)SetDlgItemTextW(h,StatusText,L"有配对存档无法读取或解密，详情见日志。请使用原Windows账户；不要删除原件。");
    else if(migration.failed)SetDlgItemTextW(h,StatusText,L"部分旧配对无法迁移；原件已保留，请检查用户权限。不要重复配对覆盖原件。");
    else if(!currentProfile.empty())SetDlgItemTextW(h,StatusText,L"已加载保存的主机，直接点“连接并观看”，无需重新填写8位配对码。");
    installDialogHelp(h,{
        {LoginPsn,L"打开Sony登录页。登录后复制浏览器回调地址，再点提交登录结果；软件不接触密码。"},
        {CompletePsn,L"读取你复制的Sony登录回调URL，获取并加密保存授权和Account ID。不会读取其他剪贴板格式。"},
        {ForgetPsn,L"只删除PSN授权，已有局域网主机配对仍然保留。"},
        {Host,L"PS5的局域网IP或已保存主机。找不到时可手填，别把PSN昵称填这里。"},
        {Account,L"配对用的Base64 PSN Account ID，不是在线昵称。名字相同不代表身份证号码相同。"},
        {PairPin,L"PS5远程游玩页面显示的8位配对码，有时效。过期了再领一张票。"},
        {Quality,L"选择PS5发送的分辨率和帧率。更改后点击应用设置并重连；当前生效值看下方状态，不要被下拉框骗了。"},
        {CodecChoice,L"H.264兼容性好，H.265通常更省码率；更改后重连生效。HDR需要PS5实际输出HDR和Windows HDR开启；增强保留HDR基底并处理映射副本，NR本身不是原生HDR模型。SDR显示器会映射为SDR。"},
        {Pair,L"用账户ID和8位码注册主机，凭据在本机加密保存。"},
        {Connect,L"使用保存的配对信息连接PS5，把画面送进当前增强链路。"},
        {Cancel,L"取消当前操作或断开串流，让连接安静收工。"},
        {Scan,L"在局域网里寻找主机。同网段、防火墙和PS5设置都可能影响结果。"},
        {Wake,L"尝试唤醒待机主机；需要已配对且PS5允许联网唤醒。关机不是待机，叫不醒别硬喊。"},
        {LoginPin,L"主机要求登录PIN时填写。它不是前面的8位配对码。"},
        {SendPin,L"把登录PIN发给当前串流会话。"},
        {Bitrate,L"这是发给PS5的带宽请求，重连后生效。主机会按画面和网络决定实际码率，选100不代表必须跑满100。下方分别显示本次请求和实收视频码率。"},
        {Forget,L"删除本机保存的这台主机配对信息；以后需要重新配对。"},
        {ViewOnly,L"只观看，不向PS5转发电脑手柄输入。不能保证同账号手柄仍能直连主机，这是PS5会话规则。"},
        {Calibrate,L"手柄放稳后校准陀螺仪，让镜头别自己散步。"},
        {DecodeChoice,L"自动优先硬解，失败回软件；强制硬解失败会报错。软件解码主要用CPU，硬解用视频解码单元。重连生效。"},
        {SamplingChoice,L"精细模式按位置还原颜色采样，放大时用双三次插值并限制边缘光晕；1:1保留原像素。它不改变PS5码率，也不是AI超分：源头丢掉的细节，不能凭空变回来。会增加GPU工作；兼容模式可切回原有采样作对比。重连生效。"},
        {UseCapture,L"组合模式：勾选后用采集卡的HDMI画面和声音，PS5远程连接只负责把手柄输入发给主机。连接顺序是先连PS5再拉起采集画面；串流会以720p/30fps/H.264/5Mbps低规格保活（协议无法彻底关闭视频，本机收到后直接丢弃，不宣称零带宽）。取消勾选恢复完整串流。"}});
    auto blockHelp=captureBlock.helpEntries();installDialogHelp(h,{blockHelp.begin(),blockHelp.end()});
    captureBlock.create(h,font,CaptureIdBase,CaptureTimerId,captureSettings);
    captureBlock.setVisible(false);
    captureExpanded=GetPrivateProfileIntW(L"RemotePlay",L"UseCapture",0,(remoteplay::profileDirectory()/L"settings.ini").c_str())!=0;
    CheckDlgButton(h,UseCapture,captureExpanded?BST_CHECKED:BST_UNCHECKED);
    watching=connectionStatus().active;buttons();applyCaptureExpanded(h);SetTimer(h,1,100,nullptr);
    if(auto auth=remoteplay::loadPsnAuthorization()){
        psnRefreshAfter=GetTickCount64()+300000;
        const bool fillAccount=currentProfile.empty();
        launch([fillAccount](std::stop_token stop){auto result=remoteplay::refreshPsn(stop);Outcome out;
            out.message=result.ok?L"PSN授权已就绪；已配对主机直接连接，无需8位配对码。":std::format(L"PSN授权刷新失败（{}）；局域网配对仍可连接，必要时重新登录PSN。",result.error);
            if(fillAccount)out.account=result.account;
            return out;});
    }
    return 0;}
case WM_TIMER:
    if(wp==CaptureTimerId){captureBlock.poll();return 0;}
    if(busy&&done){if(worker.joinable())worker.join();busy=false;Outcome result;{std::lock_guard lock(mutex);result=std::move(outcome);}if(!result.savedPath.empty()){currentProfile=result.savedPath;WritePrivateProfileStringW(L"RemotePlay",L"LastProfile",currentProfile.filename().c_str(),(remoteplay::profileDirectory()/L"settings.ini").c_str());refreshProfiles();}buttons();SetDlgItemTextW(h,StatusText,result.message.c_str());if(result.account){auto id=remoteplay::accountIdToBase64(*result.account);SetDlgItemTextW(h,Account,std::wstring(id.begin(),id.end()).c_str());}if(!result.hosts.empty()){
        auto selected=remoteplay::loadProfile(currentProfile);bool matched=false;
        if(selected){for(auto& found:result.hosts){
            std::transform(found.consoleId.begin(),found.consoleId.end(),found.consoleId.begin(),[](unsigned char c){return char(std::tolower(c));});
            const bool sameId=!selected->consoleId.empty()&&selected->consoleId==found.consoleId;
            const bool legacyMatch=selected->consoleId.empty()&&selected->host==found.host;
            if(sameId||legacyMatch){
                selected->host=found.host;
                if(found.consoleId.size()==12)selected->consoleId=found.consoleId;
                if(remoteplay::saveProfile(currentProfile,*selected)){refreshProfiles();matched=true;}
                else SetDlgItemTextW(h,StatusText,L"找到主机，但保存新地址失败；原配对仍保留。");
                break;
            }
        }}
        if(!selected&&result.hosts.size()==1)SetDlgItemTextW(h,Host,std::wstring(result.hosts[0].host.begin(),result.hosts[0].host.end()).c_str());
        else if(selected&&!matched)SetDlgItemTextW(h,StatusText,L"发现主机，但无法确认是已配对的这一台。保留原地址，可核对后手动修改。");
        else if(!selected&&result.hosts.size()>1)SetDlgItemTextW(h,StatusText,L"发现多台PS5，请填写要配对的主机IP，避免选错主机。");
    }if(closing)DestroyWindow(h);}
    if(window&&!busy&&GetTickCount64()>psnRefreshAfter){
        psnRefreshAfter=GetTickCount64()+300000;
        if(remoteplay::loadPsnAuthorization())launch([](std::stop_token stop){auto result=remoteplay::refreshPsn(stop);Outcome out;out.message=result.ok?L"PSN授权已就绪；已配对主机直接连接。":std::format(L"PSN刷新失败（{}）；局域网配对不受影响。",result.error);return out;});
    }
    if(window&&watching&&!busy){auto state=connectionStatus();SetDlgItemTextW(h,Connect,state.active?L"应用设置并重连":L"连接并观看");SetDlgItemTextW(h,StatusText,state.message.c_str());SetDlgItemTextW(h,Cancel,state.active?L"断开连接":L"取消操作");EnableWindow(GetDlgItem(h,Cancel),state.active);if(!state.active)watching=false;}return 0;
case WM_COMMAND:
    if(LOWORD(wp)>=CaptureIdBase&&LOWORD(wp)<CaptureIdBase+40){captureBlock.handleCommand(wp,lp);return 0;}
    switch(LOWORD(wp)){
    case UseCapture:if(HIWORD(wp)==BN_CLICKED){
        // Pure UI toggle: safe while a pairing/PSN worker runs; only the
        // connect path itself is gated on busy.
        captureExpanded=IsDlgButtonChecked(h,UseCapture)==BST_CHECKED;
        WritePrivateProfileStringW(L"RemotePlay",L"UseCapture",captureExpanded?L"1":L"0",(remoteplay::profileDirectory()/L"settings.ini").c_str());
        applyCaptureExpanded(h);buttons();
        SetDlgItemTextW(h,StatusText,captureExpanded?L"组合模式：连接后手柄控制 PS5，画面与声音走采集卡。串流仅保留低码率保活通道，格式/码率设置已停用。":L"已切回完整串流模式：连接后使用上方的画面与码率设置。");
    }break;
    case LoginPsn:{if(busy)break;auto url=remoteplay::psnLoginUrl();if(INT_PTR(ShellExecuteW(h,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32){psnLoginStarted=GetTickCount64();SetDlgItemTextW(h,StatusText,L"在Sony网页完成登录，复制最终回调URL，再点“从剪贴板提交登录结果”。");}else SetDlgItemTextW(h,StatusText,L"无法打开浏览器，请检查默认浏览器设置。");break;}
    case CompletePsn:{
        if(busy)break;
        if(!psnLoginStarted||GetTickCount64()-psnLoginStarted>600000){SetDlgItemTextW(h,StatusText,L"请先点登录PSN，完成网页授权后再提交。");break;}
        std::wstring callback;
        if(OpenClipboard(h)){auto data=GetClipboardData(CF_UNICODETEXT);if(data){auto size=GlobalSize(data);if(size&&size<=16386){auto chars=static_cast<const wchar_t*>(GlobalLock(data));if(chars){size_t count=0;while(count<size/sizeof(wchar_t)&&chars[count])++count;if(count<size/sizeof(wchar_t))callback.assign(chars,count);GlobalUnlock(data);}}}CloseClipboard();}
        if(!remoteplay::validPsnCallback(callback)){SecureZeroMemory(callback.data(),callback.size()*sizeof(wchar_t));SetDlgItemTextW(h,StatusText,L"剪贴板不是有效的Sony授权回调地址。请复制登录完成后的完整URL。");break;}
        psnLoginStarted=0;
        launch([callback=std::move(callback)](std::stop_token stop)mutable{auto result=remoteplay::authorizePsn(std::move(callback),stop);Outcome out;out.account=result.account;out.message=result.ok?L"PSN登录已保存，Account ID已填入。首次仍需配对；已配对主机直接连接。":std::format(L"PSN授权失败（{}）；原有配对不受影响，可重新登录。",result.error);return out;});break;
    }
    case ForgetPsn:if(!busy){SetDlgItemTextW(h,StatusText,remoteplay::forgetPsnAuthorization()?L"PSN授权已删除，局域网配对保留。":L"删除授权失败，请检查目录权限。");}break;
    case Calibrate:if(calibrate&&calibrate())MessageBoxW(h,L"将手柄平放并保持静止，关闭此提示后返回播放器。\n采集 120 个稳定样本完成校准；10 秒内不稳定则保留原校准。",L"陀螺仪校准",MB_OK);else MessageBoxW(h,L"请先连接串流及带陀螺仪的电脑手柄。仅观看模式不使用电脑手柄。",L"无法校准",MB_OK);break;
    case Host:if(HIWORD(wp)==CBN_SELCHANGE&&!busy){const auto index=SendDlgItemMessageW(h,Host,CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<profiles.size()){currentProfile=profiles[size_t(index)].path;WritePrivateProfileStringW(L"RemotePlay",L"LastProfile",currentProfile.filename().c_str(),(remoteplay::profileDirectory()/L"settings.ini").c_str());loadSelection();}}break;
    case Forget:if(!busy&&!currentProfile.empty()){std::error_code ec;const bool removed=std::filesystem::remove(currentProfile,ec);if(removed){currentProfile.clear();refreshProfiles();if(profiles.empty()){SetDlgItemTextW(h,Account,L"");SetDlgItemTextW(h,Host,L"");}SetDlgItemTextW(h,StatusText,L"配对已删除。现有串流会保留；下次连接需选择其他主机或重新配对。");}else SetDlgItemTextW(h,StatusText,L"删除失败，请检查文件权限。");}break;
    case Pair:{if(busy)break;auto host=ascii(Host),id=ascii(Account),pin=ascii(PairPin);auto account=remoteplay::accountIdFromBase64(id);if(!account)account=remoteplay::accountIdFromDecimal(id);
        if(!remoteplay::validHost(host)||!account||!remoteplay::parsePairingPin(pin)){SetDlgItemTextW(h,StatusText,L"请填写有效主机地址、Account ID 和 8 位配对码。");break;}
        auto targetPath=pathForHost(host,*account);if(targetPath.empty()){SetDlgItemTextW(h,StatusText,L"无法分配配对存档，请重试。");break;}auto format=video();SetDlgItemTextW(h,PairPin,L"");
        launch([host=std::move(host),account=*account,pin=std::move(pin),format,targetPath](std::stop_token stop)mutable{
            auto result=remoteplay::pairLocalPs5(host,account,pin,stop);SecureZeroMemory(pin.data(),pin.size());Outcome out;
            if(result.result.ok&&!stop.stop_requested()){remoteplay::NativeConnectRequest request;request.host=host;request.video=format;request.credentials=std::move(result.credentials);for(auto byte:result.mac)request.consoleId+=std::format("{:02x}",byte);if(request.consoleId=="000000000000")request.consoleId.clear();if(remoteplay::saveProfile(targetPath,request)){out.savedPath=targetPath;out.message=L"配对成功并已加密保存，可以连接。";}else out.message=L"配对成功，但保存失败。请检查目录权限后重试。";}
            else out.message=result.canceled||stop.stop_requested()?L"已取消配对":std::format(L"配对失败（{}），检查 PS5 配对码、Account ID 与网络。",result.result.code);return out;});break;}
    case Connect:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());auto host=ascii(Host);if(!saved||!remoteplay::validHost(host)){SetDlgItemTextW(h,StatusText,L"请先完成配对，并填写有效主机地址。");break;}saved->host=host;
        const bool combined=IsDlgButtonChecked(h,UseCapture)==BST_CHECKED;
        // The combined mode runs a fixed low keepalive profile; the saved
        // profile keeps the user's normal streaming choices untouched.
        if(combined)saved->viewOnly=false;
        else{saved->video=video();saved->viewOnly=IsDlgButtonChecked(h,ViewOnly)==BST_CHECKED;}
        if(!remoteplay::saveProfile(currentProfile,*saved)){SetDlgItemTextW(h,StatusText,L"保存连接设置失败，请检查目录权限。");break;}source::RemotePlayConnectDesc desc;desc.request=std::move(*saved);const auto decode=std::clamp<int>(int(SendDlgItemMessageW(h,DecodeChoice,CB_GETCURSEL,0,0)),0,2);desc.decodeMode=static_cast<source::RemotePlayConnectDesc::DecodeMode>(decode);WritePrivateProfileStringW(L"RemotePlay",L"DecodeMode",std::to_wstring(decode).c_str(),(remoteplay::profileDirectory()/L"settings.ini").c_str());desc.highQualitySampling=SendDlgItemMessageW(h,SamplingChoice,CB_GETCURSEL,0,0)==1;WritePrivateProfileStringW(L"RemotePlay",L"FineSampling",desc.highQualitySampling?L"1":L"0",(remoteplay::profileDirectory()/L"settings.ini").c_str());
        if(combined){
            desc.request.video=remoteplay::VideoProfile{};
            desc.request.video.width=1280;desc.request.video.height=720;desc.request.video.fps=30;
            desc.request.video.codec=remoteplay::Codec::H264;desc.request.video.bitrateKbps=5000;
            desc.request.viewOnly=false;
            auto capturePath=captureBlock.capturePath();
            if(capturePath.empty()){SetDlgItemTextW(h,StatusText,(captureBlock.statusText().empty()?std::wstring(L"请先选择采集卡的视频设备与格式，再点连接。"):captureBlock.statusText()).c_str());break;}
            connectCapture(std::move(desc),std::move(capturePath));
            watching=true;EnableWindow(GetDlgItem(h,Cancel),TRUE);
            SetDlgItemTextW(h,StatusText,L"组合模式连接已开始：先连 PS5 控制通道，成功后自动拉起采集画面。需要登录 PIN 时在下方提交。关闭面板不停止串流。");break;
        }
        connect(std::move(desc));watching=true;EnableWindow(GetDlgItem(h,Cancel),TRUE);SetDlgItemTextW(h,StatusText,L"连接已开始。需要登录 PIN 时在下方提交。关闭面板不停止串流。");break;}
    case Cancel:if(busy&&worker.joinable())worker.request_stop();else if(watching)disconnect();break;
    case Scan:launch([](std::stop_token stop){Outcome out;auto report=remoteplay::discoverLocalPs5(stop);for(const auto& line:report.diagnostics)veyra::log::info("remoteplay-discovery",line);out.hosts=std::move(report.hosts);out.message=stop.stop_requested()?L"已取消查找":out.hosts.empty()?(report.error?std::format(L"主机搜索发生错误（{}），详情见日志；可手填 IP。",report.error):L"搜索完成，未收到 PS5 回应；可手填 IP，检查主机开机和局域网。"):L"已填入发现的 PS5 地址。多台主机可手工输入目标 IP。";return out;});break;
    case Wake:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());if(!saved){SetDlgItemTextW(h,StatusText,L"需要先配对才能唤醒。");break;}saved->host=ascii(Host);launch([request=std::move(*saved)](std::stop_token stop){Outcome out;if(stop.stop_requested()){out.message=L"已取消唤醒";return out;}auto r=remoteplay::wakeLocalPs5(request);out.message=r.ok?L"已发送唤醒请求，等待 PS5 启动后点击连接。":std::format(L"唤醒请求失败（{}）",r.code);return out;});break;}
    case SendPin:{auto pin=ascii(LoginPin);if(pin.empty()||!std::all_of(pin.begin(),pin.end(),[](char c){return c>='0'&&c<='9';})){SetDlgItemTextW(h,StatusText,L"登录 PIN 必须为数字。");break;}login(std::move(pin));SetDlgItemTextW(h,LoginPin,L"");break;}
    }return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_CLOSE:if(busy){closing=true;worker.request_stop();SetDlgItemTextW(h,StatusText,L"正在取消并清理连接…");}else DestroyWindow(h);return 0;
case WM_DESTROY:captureBlock.destroy();if(worker.joinable()){worker.request_stop();worker.join();}KillTimer(h,1);DeleteObject(font);window=nullptr;busy=false;return 0;
case WM_SIZE:arrange();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
catch(...){veyra::log::error("remoteplay-ui","Operation failed; no credentials logged");if(msg==WM_CREATE)return -1;SetDlgItemTextW(h,StatusText,L"操作失败，请检查用户目录权限；原有配对文件不会自动删除。");return 0;}
}
void showRemotePlayPanel(HWND parent,std::function<void(source::RemotePlayConnectDesc)> start,std::function<void(source::RemotePlayConnectDesc,const std::wstring&)> startWithCapture,std::function<void(std::string)> pin,std::function<RemotePlayPanelStatus()> status,std::function<void()> stop,std::function<bool()> calibration,CaptureConfigBlock::Callbacks captureSettingsCallbacks){
    calibrate=std::move(calibration);
    connect=std::move(start);connectCapture=std::move(startWithCapture);captureSettings=captureSettingsCallbacks;
    // The embedded block re-reads the live settings through these pairs on
    // every timer tick, so copy rather than move keeps them valid.
    login=std::move(pin);connectionStatus=std::move(status);disconnect=std::move(stop);if(window){SetForegroundWindow(window);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraRemotePlaySetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"PS5 · Remote Play",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,590),dip(parent,709),parent,nullptr,wc.hInstance,nullptr);
}
}
