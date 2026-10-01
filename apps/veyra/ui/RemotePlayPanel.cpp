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
enum {Host=1,Account,PairPin,Quality,CodecChoice,Pair,Connect,Cancel,Scan,Wake,LoginPin,SendPin,StatusText,Help,Bitrate,Forget,ViewOnly,Calibrate,DecodeChoice,LoginPsn,CompletePsn,ForgetPsn,SamplingChoice};
uint64_t psnLoginStarted=0,psnRefreshAfter=0;
std::function<bool()> calibrate;
std::function<void(source::RemotePlayConnectDesc)> connect;
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
void buttons(){for(int id:{Pair,Connect,Scan,Wake,Host,Account,PairPin,Quality,CodecChoice,Bitrate,SamplingChoice,Forget,LoginPsn,CompletePsn,ForgetPsn})EnableWindow(GetDlgItem(window,id),!busy);EnableWindow(GetDlgItem(window,Cancel),busy);}
template<class Work> void launch(Work work){
    if(busy)return;if(worker.joinable())worker.join();busy=true;done=false;buttons();SetDlgItemTextW(window,StatusText,L"Processing... you can cancel at any time");
    worker=std::jthread([work=std::move(work)](std::stop_token stop)mutable{
        Outcome result;
        try{result=work(stop);}catch(...){result.message=L"Operation failed; check the network, input and available disk space.";}
        {std::lock_guard lock(mutex);outcome=std::move(result);}done=true;
    });
}
void arrange(){
    auto move=[](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,y),dip(window,w),dip(window,h),TRUE);};
    move(LoginPsn,20,640,135,30);move(CompletePsn,162,640,245,30);move(ForgetPsn,415,640,175,30);
    move(Host,170,18,290,180);move(Scan,470,18,120,28);
    move(Account,170,62,420,28);move(PairPin,170,106,190,28);move(Pair,370,106,220,30);
    move(Quality,170,150,230,150);move(CodecChoice,410,150,180,150);
    move(Connect,170,199,190,36);move(Wake,370,199,220,36);
    move(LoginPin,170,250,190,28);move(SendPin,370,250,220,30);
    move(Bitrate,170,295,190,180);move(Forget,370,295,90,32);move(Cancel,470,295,120,32);
    move(StatusText,20,341,570,64);move(ViewOnly,20,408,440,28);move(Calibrate,470,408,120,28);
    move(DecodeChoice,170,442,420,150);move(SamplingChoice,170,486,420,120);move(Help,20,529,570,96);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp)try{switch(msg){
case WM_CREATE:{window=h;closing=false;font=makeFont(h);titleTheme(h);
    auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x=0,int y=0,int w=1,int height=1){
        auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,dip(h,x),dip(h,y),dip(h,w),dip(h,height),h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);return c;
    };
    add(L"BUTTON",L"View only (controller connects to PS5; reconnect required after changes)",ViewOnly,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"BUTTON",L"Calibrate gyro",Calibrate,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"Decode (applies on reconnect)",106,0,20,445,145,25);
    add(L"COMBOBOX",L"",DecodeChoice,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(auto label:{L"Auto · prefer hardware decode",L"CPU software decode",L"D3D12VA hardware decode"})SendDlgItemMessageW(h,DecodeChoice,CB_ADDSTRING,0,LPARAM(label));
    SendDlgItemMessageW(h,DecodeChoice,CB_SETCURSEL,std::min(2u,GetPrivateProfileIntW(L"RemotePlay",L"DecodeMode",0,(remoteplay::profileDirectory()/L"settings.ini").c_str())),0);
    add(L"STATIC",L"Sampling (applies on reconnect)",107,0,20,489,145,25);
    add(L"COMBOBOX",L"",SamplingChoice,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(auto label:{L"Compatible sampling · legacy method",L"Fine sampling · chroma reconstruction and bicubic scaling"})SendDlgItemMessageW(h,SamplingChoice,CB_ADDSTRING,0,LPARAM(label));
    SendDlgItemMessageW(h,SamplingChoice,CB_SETCURSEL,std::min(1u,GetPrivateProfileIntW(L"RemotePlay",L"FineSampling",1,(remoteplay::profileDirectory()/L"settings.ini").c_str())),0);
    add(L"STATIC",L"PS5 address",100,0,20,21,145,25);add(L"STATIC",L"PSN Account ID",101,0,20,65,145,25);
    add(L"STATIC",L"8-digit pairing code",102,0,20,109,145,25);add(L"STATIC",L"Format (applies on reconnect)",103,0,20,153,145,25);add(L"STATIC",L"Login PIN (optional)",104,0,20,253,145,25);
    add(L"STATIC",L"Bitrate request",105,0,20,298,145,25);add(L"COMBOBOX",L"",Host,CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_TABSTOP);SendDlgItemMessageW(h,Host,CB_LIMITTEXT,253,0);
    for(int id:{Account,PairPin,LoginPin}){add(L"EDIT",L"",id,WS_TABSTOP|ES_AUTOHSCROLL|((id==PairPin||id==LoginPin)?ES_PASSWORD:0));SendDlgItemMessageW(h,id,EM_SETLIMITTEXT,id==Host?253:id==Account?24:8,0);}
    add(L"COMBOBOX",L"",Quality,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"720p · 30 fps",L"720p · 60 fps",L"1080p · 30 fps",L"1080p · 60 fps"})SendDlgItemMessageW(h,Quality,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,Quality,CB_SETCURSEL,3,0);
    add(L"COMBOBOX",L"",CodecChoice,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto label:{L"H.264 · SDR",L"H.265 · SDR",L"H.265 · HDR (experimental)"})SendDlgItemMessageW(h,CodecChoice,CB_ADDSTRING,0,LPARAM(label));SendDlgItemMessageW(h,CodecChoice,CB_SETCURSEL,0,0);
    add(L"COMBOBOX",L"",Bitrate,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto rate:bitrates){auto label=std::format(L"{} Mbps",rate/1000);SendDlgItemMessageW(h,Bitrate,CB_ADDSTRING,0,LPARAM(label.c_str()));}SendDlgItemMessageW(h,Bitrate,CB_SETCURSEL,2,0);
    add(L"BUTTON",L"Log in to PSN",LoginPsn,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"Submit login result from clipboard",CompletePsn,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"Log out of PSN",ForgetPsn,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"Delete pairing",Forget,BS_PUSHBUTTON|WS_TABSTOP);
    for(auto [id,label]:{std::pair{Pair,L"Pair and save"},{Connect,L"Connect and watch"},{Cancel,L"Cancel"},{Scan,L"Find host"},{Wake,L"Wake paired PS5"},{SendPin,L"Submit login PIN"}})add(L"BUTTON",label,id,BS_PUSHBUTTON|WS_TABSTOP);
    marked(GetDlgItem(h,Connect));
    add(L"STATIC",L"Pair on first use, then connect directly afterward.",StatusText,0);
    add(L"STATIC",L"PS5: Settings -> System -> Remote Play -> Enable Remote Play -> Link Device.\nAccount ID takes the account's numeric ID or the corresponding 8-byte Base64, not a nickname or password.\nConnect the PC and PS5 to the same LAN first. Pairing info is stored encrypted on this machine only.\nLogin PIN is only entered when the PS5 prompts for it; cancelling does not close the current video.",Help,0);
    const auto migration=remoteplay::migrateProfiles(runtime::localDataDirectory());
    refreshProfiles();
    if(profileReadFailures)SetDlgItemTextW(h,StatusText,L"Some pairing files can't be read or decrypted; see the log for details. Use the original Windows account; do not delete the original files.");
    else if(migration.failed)SetDlgItemTextW(h,StatusText,L"Some old pairings couldn't be migrated; the originals are kept, please check user permissions. Do not re-pair and overwrite the originals.");
    else if(!currentProfile.empty())SetDlgItemTextW(h,StatusText,L"Loaded a saved host; just click \"Connect and watch\", no need to re-enter the 8-digit pairing code.");
    installDialogHelp(h,{
        {LoginPsn,L"Opens the Sony login page. After logging in, copy the browser callback address, then click Submit login result; the software never touches your password."},
        {CompletePsn,L"Reads the Sony login callback URL you copied, then fetches and encrypts the authorization and Account ID. It won't read other clipboard formats."},
        {ForgetPsn,L"Only deletes the PSN authorization; existing LAN host pairings are kept."},
        {Host,L"The PS5's LAN IP or a saved host. If not found you can type it in manually; don't put a PSN nickname here."},
        {Account,L"The Base64 PSN Account ID used for pairing, not an online nickname. The same name doesn't mean the same ID number."},
        {PairPin,L"The 8-digit pairing code shown on the PS5 Remote Play page; it expires. If it expires, get a fresh one."},
        {Quality,L"Choose the resolution and frame rate the PS5 sends. After changing, click Apply settings and reconnect; check the status below for the value in effect, don't be fooled by the dropdown."},
        {CodecChoice,L"H.264 is more compatible; H.265 usually saves more bitrate; changes apply on reconnect. HDR requires the PS5 to actually output HDR and Windows HDR to be on; enhancement keeps the HDR base and processes a mapped copy, NR itself is not a native HDR model. An SDR display maps to SDR."},
        {Pair,L"Registers the host with the account ID and 8-digit code; credentials are stored encrypted on this machine."},
        {Connect,L"Connects to the PS5 using the saved pairing info and feeds the image into the current enhancement pipeline."},
        {Cancel,L"Cancels the current operation or disconnects the stream, letting the connection wind down quietly."},
        {Scan,L"Searches for hosts on the LAN. The same subnet, firewall and PS5 settings can all affect the result."},
        {Wake,L"Tries to wake a standby host; requires it to be paired and the PS5 to allow network wake. Powered off is not standby, so don't keep shouting if it won't wake."},
        {LoginPin,L"Enter it when the host asks for a login PIN. It's not the 8-digit pairing code from earlier."},
        {SendPin,L"Sends the login PIN to the current stream session."},
        {Bitrate,L"This is a bandwidth request sent to the PS5, effective on reconnect. The host decides the actual bitrate based on the image and network; choosing 100 doesn't mean it must run at a full 100. The requested and actually received video bitrates are shown separately below."},
        {Forget,L"Deletes this host's pairing info stored on this machine; you'll need to pair again later."},
        {ViewOnly,L"Watch only, don't forward the PC controller input to the PS5. It can't guarantee a same-account controller can still connect directly to the host; that's a PS5 session rule."},
        {Calibrate,L"Calibrate the gyro with the controller resting still, so the view doesn't wander on its own."},
        {DecodeChoice,L"Auto prefers hardware decode and falls back to software on failure; forced hardware decode reports an error on failure. Software decode mainly uses the CPU, hardware decode uses the video decode unit. Applies on reconnect."},
        {SamplingChoice,L"Fine mode reconstructs color sampling by position, uses bicubic interpolation when upscaling and limits edge halos; 1:1 keeps the original pixels. It doesn't change the PS5 bitrate, and it's not AI super resolution: detail lost at the source can't be recovered from nothing. It increases GPU work; compatible mode can switch back to the original sampling for comparison. Applies on reconnect."}});
    watching=connectionStatus().active;buttons();arrange();SetTimer(h,1,100,nullptr);
    if(auto auth=remoteplay::loadPsnAuthorization()){
        psnRefreshAfter=GetTickCount64()+300000;
        const bool fillAccount=currentProfile.empty();
        launch([fillAccount](std::stop_token stop){auto result=remoteplay::refreshPsn(stop);Outcome out;
            out.message=result.ok?L"PSN authorization is ready; paired hosts connect directly, no 8-digit pairing code needed.":std::format(L"PSN authorization refresh failed ({}); LAN pairings can still connect, log in to PSN again if needed.",result.error);
            if(fillAccount)out.account=result.account;
            return out;});
    }
    return 0;}
case WM_TIMER:if(busy&&done){if(worker.joinable())worker.join();busy=false;Outcome result;{std::lock_guard lock(mutex);result=std::move(outcome);}if(!result.savedPath.empty()){currentProfile=result.savedPath;WritePrivateProfileStringW(L"RemotePlay",L"LastProfile",currentProfile.filename().c_str(),(remoteplay::profileDirectory()/L"settings.ini").c_str());refreshProfiles();}buttons();SetDlgItemTextW(h,StatusText,result.message.c_str());if(result.account){auto id=remoteplay::accountIdToBase64(*result.account);SetDlgItemTextW(h,Account,std::wstring(id.begin(),id.end()).c_str());}if(!result.hosts.empty()){
        auto selected=remoteplay::loadProfile(currentProfile);bool matched=false;
        if(selected){for(auto& found:result.hosts){
            std::transform(found.consoleId.begin(),found.consoleId.end(),found.consoleId.begin(),[](unsigned char c){return char(std::tolower(c));});
            const bool sameId=!selected->consoleId.empty()&&selected->consoleId==found.consoleId;
            const bool legacyMatch=selected->consoleId.empty()&&selected->host==found.host;
            if(sameId||legacyMatch){
                selected->host=found.host;
                if(found.consoleId.size()==12)selected->consoleId=found.consoleId;
                if(remoteplay::saveProfile(currentProfile,*selected)){refreshProfiles();matched=true;}
                else SetDlgItemTextW(h,StatusText,L"Found the host, but failed to save the new address; the original pairing is kept.");
                break;
            }
        }}
        if(!selected&&result.hosts.size()==1)SetDlgItemTextW(h,Host,std::wstring(result.hosts[0].host.begin(),result.hosts[0].host.end()).c_str());
        else if(selected&&!matched)SetDlgItemTextW(h,StatusText,L"Found a host, but couldn't confirm it's the paired one. Keeping the original address; verify and edit it manually if needed.");
        else if(!selected&&result.hosts.size()>1)SetDlgItemTextW(h,StatusText,L"Found multiple PS5s; enter the IP of the host you want to pair to avoid choosing the wrong one.");
    }if(closing)DestroyWindow(h);}
    if(window&&!busy&&GetTickCount64()>psnRefreshAfter){
        psnRefreshAfter=GetTickCount64()+300000;
        if(remoteplay::loadPsnAuthorization())launch([](std::stop_token stop){auto result=remoteplay::refreshPsn(stop);Outcome out;out.message=result.ok?L"PSN authorization is ready; paired hosts connect directly.":std::format(L"PSN refresh failed ({}); LAN pairings are unaffected.",result.error);return out;});
    }
    if(window&&watching&&!busy){auto state=connectionStatus();SetDlgItemTextW(h,Connect,state.active?L"Apply settings and reconnect":L"Connect and watch");SetDlgItemTextW(h,StatusText,state.message.c_str());SetDlgItemTextW(h,Cancel,state.active?L"Disconnect":L"Cancel");EnableWindow(GetDlgItem(h,Cancel),state.active);if(!state.active)watching=false;}return 0;
case WM_COMMAND:switch(LOWORD(wp)){
    case LoginPsn:{if(busy)break;auto url=remoteplay::psnLoginUrl();if(INT_PTR(ShellExecuteW(h,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32){psnLoginStarted=GetTickCount64();SetDlgItemTextW(h,StatusText,L"Complete the login on the Sony web page, copy the final callback URL, then click \"Submit login result from clipboard\".");}else SetDlgItemTextW(h,StatusText,L"Couldn't open the browser; check your default browser settings.");break;}
    case CompletePsn:{
        if(busy)break;
        if(!psnLoginStarted||GetTickCount64()-psnLoginStarted>600000){SetDlgItemTextW(h,StatusText,L"Click Log in to PSN first, complete the web authorization, then submit.");break;}
        std::wstring callback;
        if(OpenClipboard(h)){auto data=GetClipboardData(CF_UNICODETEXT);if(data){auto size=GlobalSize(data);if(size&&size<=16386){auto chars=static_cast<const wchar_t*>(GlobalLock(data));if(chars){size_t count=0;while(count<size/sizeof(wchar_t)&&chars[count])++count;if(count<size/sizeof(wchar_t))callback.assign(chars,count);GlobalUnlock(data);}}}CloseClipboard();}
        if(!remoteplay::validPsnCallback(callback)){SecureZeroMemory(callback.data(),callback.size()*sizeof(wchar_t));SetDlgItemTextW(h,StatusText,L"The clipboard isn't a valid Sony authorization callback address. Copy the full URL after login completes.");break;}
        psnLoginStarted=0;
        launch([callback=std::move(callback)](std::stop_token stop)mutable{auto result=remoteplay::authorizePsn(std::move(callback),stop);Outcome out;out.account=result.account;out.message=result.ok?L"PSN login saved and Account ID filled in. First time still needs pairing; paired hosts connect directly.":std::format(L"PSN authorization failed ({}); existing pairings are unaffected, you can log in again.",result.error);return out;});break;
    }
    case ForgetPsn:if(!busy){SetDlgItemTextW(h,StatusText,remoteplay::forgetPsnAuthorization()?L"PSN authorization deleted; LAN pairings are kept.":L"Failed to delete the authorization; check directory permissions.");}break;
    case Calibrate:if(calibrate&&calibrate())MessageBoxW(h,L"Lay the controller flat and keep it still; return to the player after closing this prompt.\nCalibration completes after collecting 120 stable samples; if it's unstable within 10 seconds the existing calibration is kept.",L"Gyro calibration",MB_OK);else MessageBoxW(h,L"Connect a stream and a PC controller with a gyro first. View-only mode doesn't use the PC controller.",L"Can't calibrate",MB_OK);break;
    case Host:if(HIWORD(wp)==CBN_SELCHANGE&&!busy){const auto index=SendDlgItemMessageW(h,Host,CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<profiles.size()){currentProfile=profiles[size_t(index)].path;WritePrivateProfileStringW(L"RemotePlay",L"LastProfile",currentProfile.filename().c_str(),(remoteplay::profileDirectory()/L"settings.ini").c_str());loadSelection();}}break;
    case Forget:if(!busy&&!currentProfile.empty()){std::error_code ec;const bool removed=std::filesystem::remove(currentProfile,ec);if(removed){currentProfile.clear();refreshProfiles();if(profiles.empty()){SetDlgItemTextW(h,Account,L"");SetDlgItemTextW(h,Host,L"");}SetDlgItemTextW(h,StatusText,L"Pairing deleted. Existing streams are kept; next connection you'll need to pick another host or pair again.");}else SetDlgItemTextW(h,StatusText,L"Deletion failed; check file permissions.");}break;
    case Pair:{if(busy)break;auto host=ascii(Host),id=ascii(Account),pin=ascii(PairPin);auto account=remoteplay::accountIdFromBase64(id);if(!account)account=remoteplay::accountIdFromDecimal(id);
        if(!remoteplay::validHost(host)||!account||!remoteplay::parsePairingPin(pin)){SetDlgItemTextW(h,StatusText,L"Enter a valid host address, Account ID and 8-digit pairing code.");break;}
        auto targetPath=pathForHost(host,*account);if(targetPath.empty()){SetDlgItemTextW(h,StatusText,L"Couldn't allocate a pairing file; please try again.");break;}auto format=video();SetDlgItemTextW(h,PairPin,L"");
        launch([host=std::move(host),account=*account,pin=std::move(pin),format,targetPath](std::stop_token stop)mutable{
            auto result=remoteplay::pairLocalPs5(host,account,pin,stop);SecureZeroMemory(pin.data(),pin.size());Outcome out;
            if(result.result.ok&&!stop.stop_requested()){remoteplay::NativeConnectRequest request;request.host=host;request.video=format;request.credentials=std::move(result.credentials);for(auto byte:result.mac)request.consoleId+=std::format("{:02x}",byte);if(request.consoleId=="000000000000")request.consoleId.clear();if(remoteplay::saveProfile(targetPath,request)){out.savedPath=targetPath;out.message=L"Pairing succeeded and was saved encrypted; you can connect now.";}else out.message=L"Pairing succeeded, but saving failed. Check directory permissions and try again.";}
            else out.message=result.canceled||stop.stop_requested()?L"Pairing cancelled":std::format(L"Pairing failed ({}); check the PS5 pairing code, Account ID and network.",result.result.code);return out;});break;}
    case Connect:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());auto host=ascii(Host);if(!saved||!remoteplay::validHost(host)){SetDlgItemTextW(h,StatusText,L"Complete pairing first, and enter a valid host address.");break;}saved->host=host;saved->video=video();saved->viewOnly=IsDlgButtonChecked(h,ViewOnly)==BST_CHECKED;if(!remoteplay::saveProfile(currentProfile,*saved)){SetDlgItemTextW(h,StatusText,L"Failed to save connection settings; check directory permissions.");break;}source::RemotePlayConnectDesc desc;saved->viewOnly=IsDlgButtonChecked(h,ViewOnly)==BST_CHECKED;desc.request=std::move(*saved);const auto decode=std::clamp<int>(int(SendDlgItemMessageW(h,DecodeChoice,CB_GETCURSEL,0,0)),0,2);desc.decodeMode=static_cast<source::RemotePlayConnectDesc::DecodeMode>(decode);WritePrivateProfileStringW(L"RemotePlay",L"DecodeMode",std::to_wstring(decode).c_str(),(remoteplay::profileDirectory()/L"settings.ini").c_str());desc.highQualitySampling=SendDlgItemMessageW(h,SamplingChoice,CB_GETCURSEL,0,0)==1;WritePrivateProfileStringW(L"RemotePlay",L"FineSampling",desc.highQualitySampling?L"1":L"0",(remoteplay::profileDirectory()/L"settings.ini").c_str());connect(std::move(desc));watching=true;EnableWindow(GetDlgItem(h,Cancel),TRUE);SetDlgItemTextW(h,StatusText,L"Connection started. Submit the login PIN below when required. Closing the panel doesn't stop the stream.");break;}
    case Cancel:if(busy&&worker.joinable())worker.request_stop();else if(watching)disconnect();break;
    case Scan:launch([](std::stop_token stop){Outcome out;auto report=remoteplay::discoverLocalPs5(stop);for(const auto& line:report.diagnostics)veyra::log::info("remoteplay-discovery",line);out.hosts=std::move(report.hosts);out.message=stop.stop_requested()?L"Search cancelled":out.hosts.empty()?(report.error?std::format(L"An error occurred during host search ({}); see the log for details; you can type an IP manually.",report.error):L"Search complete, no PS5 responded; you can type an IP manually, check the host is on and on the LAN."):L"Filled in the discovered PS5 address. For multiple hosts you can type the target IP manually.";return out;});break;
    case Wake:{if(busy)break;auto saved=remoteplay::loadProfile(profilePath());if(!saved){SetDlgItemTextW(h,StatusText,L"You need to pair before you can wake.");break;}saved->host=ascii(Host);launch([request=std::move(*saved)](std::stop_token stop){Outcome out;if(stop.stop_requested()){out.message=L"Wake cancelled";return out;}auto r=remoteplay::wakeLocalPs5(request);out.message=r.ok?L"Wake request sent; wait for the PS5 to start, then click Connect.":std::format(L"Wake request failed ({})",r.code);return out;});break;}
    case SendPin:{auto pin=ascii(LoginPin);if(pin.empty()||!std::all_of(pin.begin(),pin.end(),[](char c){return c>='0'&&c<='9';})){SetDlgItemTextW(h,StatusText,L"The login PIN must be numeric.");break;}login(std::move(pin));SetDlgItemTextW(h,LoginPin,L"");break;}
    }return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_CLOSE:if(busy){closing=true;worker.request_stop();SetDlgItemTextW(h,StatusText,L"Cancelling and cleaning up the connection...");}else DestroyWindow(h);return 0;
case WM_DESTROY:if(worker.joinable()){worker.request_stop();worker.join();}KillTimer(h,1);DeleteObject(font);window=nullptr;busy=false;return 0;
case WM_SIZE:arrange();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
catch(...){veyra::log::error("remoteplay-ui","Operation failed; no credentials logged");if(msg==WM_CREATE)return -1;SetDlgItemTextW(h,StatusText,L"Operation failed; check user directory permissions; existing pairing files won't be deleted automatically.");return 0;}
}
void showRemotePlayPanel(HWND parent,std::function<void(source::RemotePlayConnectDesc)> start,std::function<void(std::string)> pin,std::function<RemotePlayPanelStatus()> status,std::function<void()> stop,std::function<bool()> calibration){
    calibrate=std::move(calibration);
    connect=std::move(start);login=std::move(pin);connectionStatus=std::move(status);disconnect=std::move(stop);if(window){SetForegroundWindow(window);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraRemotePlaySetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"PS5 · Remote Play",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,630),dip(parent,740),parent,nullptr,wc.hInstance,nullptr);
}
}
