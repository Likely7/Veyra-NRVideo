#include "BrandIcon.h"
#include "../resource.h"
#include "TransportLayout.h"
#include <windows.h>
#include "../SettingsWindow.h"
#include "Theme.h"
#include "WorkspaceChrome.h"
#include "LiveStatusPanel.h"
#include "WorkspaceTransition.h"
#include "UiSessionState.h"
#include "UiPreferenceStore.h"
#include "PlaybackPowerGuard.h"
#include "CapturePanel.h"
#include "ScreenCapturePanel.h"
#ifdef VEYRA_ENABLE_REMOTEPLAY
#include "RemotePlayPanel.h"
#include "veyra/remoteplay/ControllerInput.h"
#endif
#include "SubtitleOverlay.h"
#include "SubtitleSettingsPanel.h"
#include "ProtectionOverlay.h"
#include "SourceTitle.h"
#include "veyra/engine/ExportJobManager.h"
#include <future>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")
#include "../TelemetryWindow.h"
#include <objbase.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <format>
#include "veyra/engine/ColorLookStore.h"
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/Subtitles.h"
namespace {
constexpr DWORD ShellStyle=WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN;
// Daily-mode multiplier combo lists 2X/3X/4X/6X (index 0..3), which maps to
// kFgMultiplierChoices[1..4]; index-independent so 5X can stay unoffered.
uint32_t multiplierFromDailyIndex(int index){const int i=index+1;return (index>=0&&i<int(veyra::engine::kFgMultiplierChoiceCount))?veyra::engine::kFgMultiplierChoices[i]:2;}
// RetiredPresetSlot keeps the control-id numbering stable after the preset UI
// was deleted (2026-09-17). It is intentionally never created: named colour
// presets live in the colour page and carry look parameters only.
enum {Open=101,Play,Stop,Save,Nr,Sr,Fg,Seek,Info,Capture,Export,Realtime,Recent,Multiplier,Settings,OriginalHold,CompareToggle,Split,Reference,Fullscreen,ModeSwitch=220,Master,RetiredPresetSlot,Volume,Mute,Subtitle,SubtitleLoad,SubtitleSize,ImageOpen,InspectorDrawer,TabEnhance,TabFg,TabColor,TabExport,JobProgress,Details,Brand,MediaTitle,TimeLabel,EmptyTitle,EmptyHint,ProRailVideo,ProRailCapture,WindowMin,WindowMax,WindowClose,FpsLabel,RemotePlay,TabAudio,VideoSurface=1000};
// Settings-panel status text: it belongs to the player's bottom bar now, next to
// the submitted-FPS readout (id 64 is free in the main window's id space).
constexpr int ColourStatus=64;
constexpr int ScreenCapture=901;
bool screenFill=false;SIZE screenFitClient{},screenFitImage{};
veyra::engine::EngineController engine;
#ifdef VEYRA_ENABLE_REMOTEPLAY
veyra::remoteplay::ControllerInput remoteController;
bool remoteViewOnly=false;
#endif
veyra::engine::ExportJobManager exportJob;
veyra::ui::UiSessionState uiState;
veyra::ui::UiPreferenceStore preferences(veyra::runtime::localDataDirectory());veyra::ui::UiPreferences uiPreferences;
veyra::ui::PlaybackPowerGuard playbackPower;
HWND inspector=nullptr,metricLabel=nullptr,tooltips=nullptr,diagnosticPanel=nullptr,liveStatusPanel=nullptr;bool showDiagnostics=false,inspectorResizing=false;int proposedInspectorWidth=320;bool preferWatching=true,jobPaused=false;int subtitlePixels=22;
veyra::engine::EnhancementSettings jobExpected;uint64_t masterPendingRevision=0,masterPendingSession=0;bool masterPreviousEnabled=true;
std::wstring smokeView;bool smokeViewApplied=false;
std::wstring smokeDualOutput;bool smokeEmpty=false;DWORD modeGdiStart=0,modeHandlesStart=0;SIZE_T modePrivateStart=0;
bool smokeDual=false,smokeDualPause=false,smokeMaster=false,smokeMasterReject=false,smokeAudio=false,smokeJob=false,smokeJobCancel=false,smokeJobExit=false,smokeColor=false;int dualStep=0,masterStep=0,audioStep=0,jobStep=0,colorStep=0;double pausedPosition=-1,jobPosition=0;uint64_t pausedFrames=0;ULONGLONG jobPauseTick=0;HANDLE workerMapping=nullptr;
// Test/acceptance convenience: start in professional mode with the colour page
// open, so a reviewer lands straight on the panel instead of hunting for it.
bool openColourPageOnStart=false;
uint64_t colourLiveRevision=0;
uint64_t colourPausedBase=0;
void switchMode();void selectInspector(int);
bool applySettings(veyra::engine::EnhancementSettings s){
    if(masterPendingRevision)return false;
    if(uiState.enhanced){if(!engine.requestSettings(s))return false;}
    else {
        auto effective=engine.snapshot().desired;
        if(effective.captureCompatible!=s.captureCompatible||effective.forceSdrPreview!=s.forceSdrPreview||effective.captureFlipVertical!=s.captureFlipVertical||effective.captureBuffer!=s.captureBuffer||!(effective.color==s.color)){
            effective.captureCompatible=s.captureCompatible;effective.forceSdrPreview=s.forceSdrPreview;
            effective.captureFlipVertical=s.captureFlipVertical;effective.captureBuffer=s.captureBuffer;effective.color=s.color;
            if(!engine.requestSettings(effective))return false;
        }
        veyra::log::info("ui-settings","enhancement off: draft saved; presentation setting applied independently");
    }
    uiState.configured=s;
    veyra::ui::settingsEnabled(uiState.enhanced,uiState.configured);return true;
}

struct ToolbarItem{HWND hwnd;int width;};std::vector<ToolbarItem> toolbar;
bool full=false,holdOriginal=false,referenceBase=false;int compareMode=0;float compareSplit=.5f;WINDOWPLACEMENT windowPlacement{sizeof(windowPlacement)};
HWND mainWindow=nullptr,video=nullptr,statusBar=nullptr,seekBar=nullptr,playbackBar=nullptr;
veyra::ui::WorkspaceTransition transition;veyra::ui::GlassBackdrop backdrop;
bool fullControls=true,menuOpen=false;ULONGLONG pointerTick=0,dashboardTick=0;
void layout();
enum : UINT_PTR { TelemetryTimer=1, TransitionTimer=2, ControllerTimer=3 };
void startShellTimer(HWND window,UINT_PTR id,UINT interval){
    if(!SetTimer(window,id,interval,nullptr))veyra::log::error("ui-timer",std::format("start failed id={} error={}",id,GetLastError()));
}
void endTransition(){transition.finish();KillTimer(mainWindow,TransitionTimer);if(video)RemovePropW(video,L"Veyra.ResizeDeferUntil");}
veyra::ui::ChromeLayout chromeLayout(int w,int h){
    using namespace veyra::ui;ChromeLayout target(w,h,uiState.mode==Mode::Professional,uiState.drawer,uiPreferences.inspectorWidth);
    if(!transition.running){if(target.pro&&uiState.diagnostics){target.viewHeight-=132;target.bottom-=132;}return target;}
    ChromeLayout daily(w,h,false,false),pro(w,h,true,uiState.drawer,uiPreferences.inspectorWidth);if(uiState.diagnostics){pro.viewHeight-=132;pro.bottom-=132;}
    target=pro;target.left=transition.mix(daily.left,pro.left);target.top=transition.mix(daily.top,pro.top);target.viewWidth=transition.mix(daily.viewWidth,pro.viewWidth);target.viewHeight=transition.mix(daily.viewHeight,pro.viewHeight);target.bottom=transition.mix(daily.bottom,pro.bottom);target.right=transition.mix(w+20,pro.right);return target;
}
LRESULT CALLBACK barProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){veyra::ui::PaintBuffer paint(h);veyra::ui::fillSurface(paint.dc,paint.rect,h);return 0;}return DefWindowProcW(h,m,w,l);}
void pointerActivity(){if(!full)return;pointerTick=GetTickCount64();if(!fullControls){fullControls=true;layout();}SetCursor(LoadCursorW(nullptr,IDC_ARROW));}

std::wstring currentFile,autoInput;bool paused=false,dragging=false,closing=false;int smokeSeconds=0;ULONGLONG startTick=0;int resultCode=0;
std::wstring exportOutput;unsigned exportFrames=0;unsigned cancelAfterMs=0;bool exportHevc=false;veyra::engine::PlayerOptions initialOptions;
// Subtitle tracks: [0] is usually the external same-name file, the rest are
// embedded container tracks. Offsets live per track; the viewer's look settings
// live in uiState so they can be persisted.
std::vector<veyra::engine::SubtitleTrack> subtitleTracks;
std::unique_ptr<veyra::engine::SubtitleLoader> subtitleLoader;
uint64_t subtitleGeneration=0;
size_t subtitleLoadedTracks=0;
int subtitlePrimary=-1,subtitleSecondary=-1;
bool subtitlePrimaryChosen=false,subtitleSecondaryChosen=false;
HWND subtitleLabel=nullptr;
std::wstring subtitleStatus;
int subtitleRequestPrimary=INT_MIN,subtitleRequestSecondary=INT_MIN,subtitleRequestOffsetMs=0;
bool subtitleRequestAutoAlign=false;
std::string narrow(const std::wstring& value){
    if(value.empty())return {};
    const int length=WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(length<=0)return {};
    std::string text(size_t(length),'\0');
    WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),text.data(),length,nullptr,nullptr);
    return text;
}
const wchar_t* const kSubtitleFonts[]={L"Microsoft YaHei UI",L"SimHei",L"SimSun",L"DengXian",L"Arial",L"Segoe UI"};
constexpr int kSubtitleFontCount=int(sizeof(kSubtitleFonts)/sizeof(kSubtitleFonts[0]));
std::wstring subtitleFontName(){return kSubtitleFonts[std::clamp(uiState.subtitleFont,0,kSubtitleFontCount-1)];}
double subtitleScale(){return double(subtitlePixels)/22.0;}
bool subtitleIsChinese(const veyra::engine::SubtitleTrack& track){
    const auto haystack=track.name+L" "+track.language;
    for(const wchar_t* needle:{L"chi",L"zho",L"zh",L"中文",L"简",L"繁",L"chs",L"cht"})if(haystack.find(needle)!=std::wstring::npos)return true;
    return false;
}
void refreshSubtitleTracks(const std::wstring& media){
    veyra::ui::closeSubtitleSettings();
    subtitleTracks.clear();subtitlePrimary=-1;subtitleSecondary=-1;subtitleStatus.clear();
    subtitlePrimaryChosen=subtitleSecondaryChosen=false;
    subtitleLoadedTracks=0;
    if(!subtitleLoader)subtitleLoader=std::make_unique<veyra::engine::SubtitleLoader>();
    subtitleGeneration=subtitleLoader->request(media);
}
void pollSubtitleTracks(){
    if(!subtitleLoader||menuOpen)return;
    auto result=subtitleLoader->poll();if(!result||result->generation!=subtitleGeneration)return;
    const auto count=result->tracks.size();
    // Replace the loader-owned prefix; preserve manually loaded tracks and
    // offsets/choices made while embedded text was still being read.
    for(size_t i=0;i<std::min(subtitleLoadedTracks,count);++i)result->tracks[i].offsetMs=subtitleTracks[i].offsetMs;
    const int added=int(count)-int(subtitleLoadedTracks);
    if(subtitlePrimary>=int(subtitleLoadedTracks))subtitlePrimary+=added;
    if(subtitleSecondary>=int(subtitleLoadedTracks))subtitleSecondary+=added;
    subtitleTracks.erase(subtitleTracks.begin(),subtitleTracks.begin()+subtitleLoadedTracks);
    subtitleTracks.insert(subtitleTracks.begin(),std::make_move_iterator(result->tracks.begin()),std::make_move_iterator(result->tracks.end()));
    subtitleLoadedTracks=count;
    int firstUsable=-1,firstChinese=-1;
    for(size_t index=0;index<subtitleTracks.size();++index){
        if(!subtitleTracks[index].usable())continue;
        if(firstUsable<0)firstUsable=int(index);
        if(firstChinese<0&&subtitleIsChinese(subtitleTracks[index]))firstChinese=int(index);
    }
    if(!subtitlePrimaryChosen){
        subtitlePrimary=firstChinese>=0?firstChinese:firstUsable;
        if(subtitleRequestPrimary!=INT_MIN&&subtitleRequestPrimary<int(subtitleTracks.size()))subtitlePrimary=subtitleRequestPrimary;
        if(subtitleRequestOffsetMs!=0&&subtitlePrimary>=0&&size_t(subtitlePrimary)<subtitleTracks.size())subtitleTracks[size_t(subtitlePrimary)].offsetMs=subtitleRequestOffsetMs;
    }
    if(!subtitleSecondaryChosen&&subtitleRequestSecondary!=INT_MIN)subtitleSecondary=(subtitleRequestSecondary<int(subtitleTracks.size()))?subtitleRequestSecondary:-1;
    if(result->complete)for(const auto& track:subtitleTracks)veyra::log::info("subtitle",std::format("track name={} codec={} language={} embedded={} cues={} usable={} note={}",narrow(track.name),narrow(track.codec),narrow(track.language),track.embedded?1:0,track.cues.size(),track.usable()?1:0,narrow(track.note)));
    if(!subtitleSecondaryChosen&&subtitleRequestSecondary==INT_MIN&&uiState.subtitleSecondLanguage){
        int candidate=-1;
        for(size_t index=0;index<subtitleTracks.size();++index){
            if(!subtitleTracks[index].usable()||int(index)==subtitlePrimary)continue;
            candidate=int(index);break;
        }
        subtitleSecondary=candidate;
    }
    veyra::log::info("subtitle",std::format("tracks={} primary={} secondary={}",subtitleTracks.size(),subtitlePrimary,subtitleSecondary));
}
void setSubtitleOffset(int deltaMs){
    if(subtitlePrimary<0||size_t(subtitlePrimary)>=subtitleTracks.size())return;
    auto& track=subtitleTracks[size_t(subtitlePrimary)];
    track.offsetMs=std::clamp(track.offsetMs+deltaMs,-30000,30000);
    subtitleStatus=std::format(L"字幕延时 {} ms",track.offsetMs);
    veyra::log::info("subtitle",std::format("offset track={} offsetMs={}",narrow(track.name),track.offsetMs));
}
void cycleSubtitleTrack(bool secondary){
    if(!secondary)veyra::ui::closeSubtitleSettings();
    if(subtitleTracks.empty())return;
    (secondary?subtitleSecondaryChosen:subtitlePrimaryChosen)=true;
    int& slot=secondary?subtitleSecondary:subtitlePrimary;
    slot=(slot+2>int(subtitleTracks.size()))?-1:slot+1;   // -1 -> 0 -> 1 ... -> -1
    if(slot<0){
        if(secondary)subtitleStatus=L"副字幕已关闭";
        else{
            int first=-1;
            for(size_t index=0;index<subtitleTracks.size();++index)if(subtitleTracks[index].usable()){first=int(index);break;}
            slot=first;subtitleStatus=L"已切换主字幕轨";
        }
    }else{
        subtitleStatus=std::format(L"{}字幕：{}",secondary?L"副":L"主",subtitleTracks[size_t(slot)].name);
    }
    veyra::log::info("subtitle",std::format("cycle secondary={} primary={} secondaryTrack={}",secondary?1:0,subtitlePrimary,subtitleSecondary));
}
// Experimental constant-offset auto alignment. The worker decodes the file's
// audio, correlates speech activity with the cue timeline and hands the result
// back through atomics; the UI timer applies it on the UI thread.
std::jthread subtitleAlignWorker;
std::atomic<bool> subtitleAlignRunning{false},subtitleAlignReady{false};
std::atomic<int> subtitleAlignOffsetMs{0};
std::mutex subtitleAlignMutex;
std::wstring subtitleAlignDetail;
uint64_t subtitleAlignGeneration=0;
int subtitleAlignTrack=-1;
void startSubtitleAutoAlign(){
    if(subtitleAlignRunning.load()){
        subtitleStatus=L"字幕自动对齐：已经有一个分析在进行";
        return;
    }
    if(currentFile.empty()||subtitlePrimary<0||size_t(subtitlePrimary)>=subtitleTracks.size()||!subtitleTracks[size_t(subtitlePrimary)].usable()){
        subtitleStatus=L"没有可用的主字幕，无法自动对齐";
        return;
    }
    const std::wstring media=currentFile;
    const auto track=subtitleTracks[size_t(subtitlePrimary)];
    subtitleAlignGeneration=subtitleGeneration;subtitleAlignTrack=subtitlePrimary;
    subtitleAlignRunning=true;subtitleAlignReady=false;
    subtitleStatus=L"字幕自动对齐：正在分析音轨（最多取前 30 分钟）…";
    subtitleAlignWorker=std::jthread([media,track]{
        const auto result=veyra::engine::alignSubtitleToAudio(media,track,30);
        {
            std::lock_guard lock(subtitleAlignMutex);
            subtitleAlignDetail=result.detail;
        }
        subtitleAlignOffsetMs=result.ok?result.offsetMs:0;
        subtitleAlignReady=result.ok;
        subtitleAlignRunning=false;
    });
}
void pollSubtitleAutoAlign(){
    if(subtitleAlignRunning.load()&&subtitleStatus.empty())subtitleStatus=L"字幕自动对齐：正在分析音轨…";
    if(!subtitleAlignReady.exchange(false))return;
    if(subtitleAlignGeneration!=subtitleGeneration||subtitleAlignTrack<0||size_t(subtitleAlignTrack)>=subtitleTracks.size())return;
    std::wstring detail;
    {std::lock_guard lock(subtitleAlignMutex);detail=subtitleAlignDetail;}
    subtitleTracks[size_t(subtitleAlignTrack)].offsetMs=subtitleAlignOffsetMs.load();
    subtitleStatus=std::format(L"字幕自动对齐：{}（按 Z/X 可微调）",detail);
    veyra::log::info("subtitle",std::format("auto align applied offsetMs={}",subtitleAlignOffsetMs.load()));
}
HFONT font=nullptr,emptyFont=nullptr;bool smokeZoom=false,smokeHover=false;ULONGLONG hoverPostedTick=0;int zoomStep=0;veyra::engine::PlayerSnapshot zoomBefore;bool smokeRollback=false,smokeRollbackFlow=false,smokeUi=false;int uiStep=0;bool smokeSettings=false;int settingsStep=0;bool smokeControls=false;int smokeStep=0;std::wstring smokeSave;
std::wstring screenshotPath;ULONGLONG screenshotTick=0;bool screenshotPending=false,smokeScreenshot=false;int screenshotStep=0;
#include "SeekPreview.h"
void openFile(const std::wstring&);
void layout();
void updateComparison(){engine.comparison(holdOriginal?1:compareMode,holdOriginal?false:referenceBase,compareSplit);}
void toggleFullscreen(){endTransition();full=!full;if(tooltips){SendMessageW(tooltips,TTM_POP,0,0);SendMessageW(tooltips,TTM_ACTIVATE,full?FALSE:TRUE,0);}DWORD corner=full?1:2;DwmSetWindowAttribute(mainWindow,33,&corner,sizeof(corner));fullControls=true;pointerTick=GetTickCount64();if(full){GetWindowPlacement(mainWindow,&windowPlacement);SetWindowLongPtrW(mainWindow,GWL_STYLE,WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(mainWindow,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtrW(mainWindow,GWL_STYLE,ShellStyle|WS_VISIBLE);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&windowPlacement.rcNormalPosition,MONITOR_DEFAULTTONEAREST),&mi);auto& r=windowPlacement.rcNormalPosition;if(r.right<mi.rcWork.left||r.left>mi.rcWork.right||r.bottom<mi.rcWork.top||r.top>mi.rcWork.bottom){OffsetRect(&r,mi.rcWork.left-r.left,mi.rcWork.top-r.top);}SetWindowPlacement(mainWindow,&windowPlacement);SetWindowPos(mainWindow,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}SetWindowTextW(GetDlgItem(mainWindow,Fullscreen),full?L"退出全屏":L"全屏 F11");SetCursor(LoadCursorW(nullptr,IDC_ARROW));layout();SetFocus(mainWindow);veyra::log::info("ui-fullscreen",std::format("enabled={} video-only viewport; transport auto-hides",full));}
bool smokeProtection=false;int protectionStep=0;uint64_t protectionSession=0;bool protectionOverlayShown=false;
bool protectionArmed=false,protectionDragging=false;POINT protectionStart{};std::pair<float,float> protectionSourceStart;HWND protectionOverlay=nullptr;
void cancelProtection(){protectionArmed=protectionDragging=false;if(protectionOverlay)ShowWindow(protectionOverlay,SW_HIDE);if(video&&GetCapture()==video)ReleaseCapture();}
std::pair<float,float> protectionPoint(HWND h,POINT p){RECT r{};GetClientRect(h,&r);auto e=engine.snapshot().metrics.resolution.output;return engine.previewView().sourcePoint(float(p.x),float(p.y),float(r.right),float(r.bottom),float(e.width),float(e.height));}
LRESULT CALLBACK interaction(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    static POINT panPoint{};static bool panning=false;
    if(id==VideoSurface&&m==WM_LBUTTONDOWN)SetFocus(h);
    if(id==VideoSurface&&protectionArmed){
        if(m==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}
        if(m==WM_KEYDOWN&&w==VK_ESCAPE){cancelProtection();return 0;}
        if(m==WM_LBUTTONDOWN){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};auto uv=protectionPoint(h,p);if(uv.first<0||uv.first>1||uv.second<0||uv.second>1)return 0;
            protectionStart=p;protectionSourceStart=uv;protectionDragging=true;SetFocus(h);SetCapture(h);return 0;}
        if(m==WM_MOUSEMOVE&&protectionDragging){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            if(!protectionOverlay)protectionOverlay=veyra::ui::createSubtitleOverlay(h);
            veyra::ui::protectionOutline(protectionOverlay,h,{std::min(p.x,protectionStart.x),std::min(p.y,protectionStart.y),std::max(p.x,protectionStart.x),std::max(p.y,protectionStart.y)});return 0;}
        if(m==WM_LBUTTONUP&&protectionDragging){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};auto uv=protectionPoint(h,p);uv.first=std::clamp(uv.first,0.0f,1.0f);uv.second=std::clamp(uv.second,0.0f,1.0f);
            auto settings=uiState.enhanced?engine.snapshot().desired:uiState.configured;bool accepted=false;
            if(std::abs(p.x-protectionStart.x)>=3&&std::abs(p.y-protectionStart.y)>=3)for(auto& q:settings.protection.regions)if(q.empty()){
                q={std::min(uv.first,protectionSourceStart.first),std::min(uv.second,protectionSourceStart.second),std::max(uv.first,protectionSourceStart.first),std::max(uv.second,protectionSourceStart.second)};
                settings.protection.enabled=true;accepted=applySettings(settings);break;}
            cancelProtection();veyra::log::info("ui-protection",std::format("rectangle accepted={} source-normalized NR-only",accepted));return 0;}
        if(m==WM_CAPTURECHANGED&&protectionDragging){protectionDragging=false;protectionArmed=false;if(protectionOverlay)ShowWindow(protectionOverlay,SW_HIDE);}
        if(m==WM_MOUSEWHEEL||m==WM_MBUTTONDOWN||m==WM_RBUTTONUP)return 0;
    }

    if(id==VideoSurface&&uiState.mode==veyra::ui::Mode::Professional){
        if(m==WM_MOUSEWHEEL){
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r{};GetClientRect(h,&r);
            if(PtInRect(&r,p)){auto extent=engine.snapshot().metrics.resolution.output;auto view=engine.previewView();
                view.wheel(float(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA,float(p.x),float(p.y),float(r.right),float(r.bottom),float(extent.width),float(extent.height));engine.previewView(view);
                veyra::log::info("preview-view",std::format("zoom={} center={},{} presentation-only",view.zoom,view.centerX,view.centerY));return 0;}
        }
        if(m==WM_MBUTTONDOWN){panning=true;panPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(h);return 0;}
        if(m==WM_MOUSEMOVE&&panning){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};RECT r{};GetClientRect(h,&r);auto extent=engine.snapshot().metrics.resolution.output;auto view=engine.previewView();view.pan(float(p.x-panPoint.x),float(p.y-panPoint.y),float(r.right),float(r.bottom),float(extent.width),float(extent.height));engine.previewView(view);panPoint=p;return 0;}
        if(m==WM_RBUTTONUP){engine.previewView({});return 0;}
    }
    if(id==VideoSurface&&(m==WM_MBUTTONUP||m==WM_CAPTURECHANGED)){panning=false;if(m==WM_MBUTTONUP&&GetCapture()==h)ReleaseCapture();}
    if(id==VideoSurface&&m==WM_SETCURSOR&&full&&!fullControls){SetCursor(nullptr);return TRUE;}
    if(id==VideoSurface&&m==WM_ERASEBKGND)return 1;
    if(id==VideoSurface&&m==WM_PAINT){
        const auto state=engine.snapshot();
        if(state.running&&state.frames){PAINTSTRUCT paint{};BeginPaint(h,&paint);EndPaint(h,&paint);}
        else{veyra::ui::PaintBuffer paint(h);veyra::ui::opaqueBlack(paint.dc,paint.rect);}
        return 0;
    }
    if(id==OriginalHold){if(m==WM_LBUTTONDOWN){holdOriginal=true;SetCapture(h);updateComparison();return 0;}if(m==WM_LBUTTONUP||m==WM_CAPTURECHANGED){holdOriginal=false;if(GetCapture()==h)ReleaseCapture();updateComparison();return 0;}}
    if(id==VideoSurface&&compareMode==2&&(m==WM_LBUTTONDOWN||m==WM_MOUSEMOVE)&&(m==WM_LBUTTONDOWN||(w&MK_LBUTTON))){if(m==WM_LBUTTONDOWN)SetCapture(h);RECT r{};GetClientRect(h,&r);auto extent=engine.snapshot().metrics.resolution.output;auto view=engine.previewView();float contentWidth=float(r.right);if(extent.width&&extent.height)contentWidth=std::min(float(r.right),float(r.bottom)*extent.width/extent.height);contentWidth*=view.zoom;float left=r.right*.5f-contentWidth*view.centerX;compareSplit=std::clamp((float(GET_X_LPARAM(l))-left)/std::max(1.0f,contentWidth),0.0f,1.0f);updateComparison();return 0;}
    if(id==VideoSurface&&m==WM_LBUTTONUP&&GetCapture()==h){ReleaseCapture();return 0;}
    return DefSubclassProc(h,m,w,l);
}
veyra::engine::PlayerOptions options(){auto o=veyra::engine::PlayerOptions::from(engine.snapshot().desired);o.captureCpuUnpack=initialOptions.captureCpuUnpack;return o;}

// Win32 dispatch reenters during move/resize and modal dialogs. Large path
// buffers must be per-operation heap storage, never part of every WndProc frame.
std::wstring fileDialog(bool save){std::vector<wchar_t> name(32768);OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFile=name.data();ofn.nMaxFile=32768;
ofn.lpstrFilter=save?L"PNG图片\0*.png\0JPEG图片\0*.jpg\0":L"视频 / 图片\0*.mp4;*.mkv;*.mov;*.avi;*.ts;*.png;*.jpg;*.jpeg\0所有文件\0*.*\0";
ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);ofn.lpstrDefExt=save?L"png":nullptr;
return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?name.data():L"";}
void openFile(const std::wstring& file){if(file.empty())return;cancelProtection();auto openOptions=options();if(file!=currentFile){openOptions.settings.protection={};uiState.configured.protection={};}auto ext=std::filesystem::path(file).extension().wstring();for(auto& c:ext)c=towlower(c);if((ext==L".png"||ext==L".jpg"||ext==L".jpeg")&&uiState.mode==veyra::ui::Mode::Daily)switchMode();engine.previewView({});currentFile=file;refreshSubtitleTracks(file);paused=false;SetWindowTextW(GetDlgItem(mainWindow,Play),L"暂停");engine.open(video,file,openOptions);SetWindowTextW(mainWindow,veyra::ui::windowTitleForSource(file).c_str());if(smokeSeconds<=0&&!file.starts_with(L"screen:"))WritePrivateProfileStringW(L"Player",L"最近打开",file.c_str(),(veyra::runtime::localDataDirectory()/"veyra.ini").wstring().c_str());}
HWND control(const wchar_t* cls,const wchar_t* label,int id,DWORD style,int x,int y,int w,int h){auto hwnd=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style|(_wcsicmp(cls,L"STATIC")?WS_TABSTOP:0),x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);veyra::ui::themeControl(hwnd);if(id>=Open&&id<=Fullscreen&&id!=Seek)toolbar.push_back({hwnd,w});return hwnd;}
void selectInspector(int page){uiState.inspector=page;veyra::ui::settingsPage(page);for(int i=0;i<4;++i)veyra::ui::selected(GetDlgItem(mainWindow,TabEnhance+i),i==page);veyra::ui::selected(GetDlgItem(mainWindow,TabAudio),page==4);}
void switchMode(){
    cancelProtection();
    const auto start=std::chrono::steady_clock::now();const auto before=engine.snapshot();const auto host=video;
    if(uiState.mode==veyra::ui::Mode::Daily){uiState.mode=veyra::ui::Mode::Professional;compareMode=uiState.preferredComparison;}
    else{uiState.mode=veyra::ui::Mode::Daily;engine.previewView({});uiState.preferredComparison=compareMode;compareMode=0;holdOriginal=false;if(GetCapture())ReleaseCapture();}
    updateComparison();if(auto focused=GetFocus();focused&&IsChild(inspector,focused))SetFocus(GetDlgItem(mainWindow,ModeSwitch));SetWindowTextW(GetDlgItem(mainWindow,ModeSwitch),uiState.mode==veyra::ui::Mode::Daily?L"专业模式":L"返回日常模式");transition.start(uiState.mode==veyra::ui::Mode::Professional,GetTickCount64());if(full)endTransition();else{SetPropW(video,L"Veyra.ResizeDeferUntil",HANDLE(uintptr_t(GetTickCount64()+390)));startShellTimer(mainWindow,TransitionTimer,16);}layout();veyra::log::info("ui-transition",std::format("started target={} durationMs=240 from={}",uiState.mode==veyra::ui::Mode::Professional,transition.from));
    const auto after=engine.snapshot();veyra::log::info("ui-mode",std::format("professional={} hostSame={} sessionSame={} revisionSame={} positionBefore={} positionAfter={} commandMs={:.3f}",uiState.mode==veyra::ui::Mode::Professional,host==video,before.sessionId==after.sessionId,before.desired.revision==after.desired.revision,before.position,after.position,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));
}
void layout(){
    if(!mainWindow||!video)return;const auto layoutBegin=std::chrono::steady_clock::now();using namespace veyra::ui;
    RECT r{};GetClientRect(mainWindow,&r);int w=MulDiv(r.right,96,veyra::ui::layoutDpi(mainWindow)),h=MulDiv(r.bottom,96,veyra::ui::layoutDpi(mainWindow));
    const bool pro=uiState.mode==Mode::Professional;auto g=chromeLayout(w,h);auto target=engine.snapshot();
    auto pane=[&](int x,int y,int width,int height,int radius,BYTE tint){return GlassPane{{dip(mainWindow,x),dip(mainWindow,y),dip(mainWindow,x+width),dip(mainWindow,y+height)},dip(mainWindow,radius),tint};};
    std::vector<GlassPane> panes;
    if(full){if(fullControls)panes.push_back(pane(0,h-88,w,88,1,142));}
    else if(g.pro){panes.push_back(pane(4,4,64,h-8,22,24));panes.push_back(pane(g.left,g.bottom,g.viewWidth,h-g.bottom-20,20,32));if(g.panelWidth){panes.push_back(pane(g.right,g.top,g.panelWidth,g.statusTop-g.top-10,20,30));panes.push_back(pane(g.right,g.statusTop,g.panelWidth,h-g.statusTop-20,20,40));}}
    else{panes.push_back(pane(0,h-88,w,88,1,32));}
    backdrop.render(r.right,r.bottom,g.pro,full,panes);
    struct Placement{HWND child;int x,y,width,height;UINT flags;};std::vector<Placement> placements;
    auto pos=[&](HWND c,int x,int y,int width,int height,bool show=true){if(!c)return;UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS|(show?SWP_SHOWWINDOW:SWP_HIDEWINDOW);if(!show&&c!=subtitleLabel)flags|=SWP_NOMOVE|SWP_NOSIZE;
        Placement next{c,dip(mainWindow,x),dip(mainWindow,y),dip(mainWindow,std::max(1,width)),dip(mainWindow,std::max(1,height)),flags};auto existing=std::find_if(placements.begin(),placements.end(),[&](const auto& p){return p.child==c;});if(existing==placements.end())placements.push_back(next);else *existing=next;};
    auto put=[&](int id,int x,int y,int width,int height=36,bool show=true){pos(GetDlgItem(mainWindow,id),x,y,width,height,show);};
    for(auto entry:toolbar){int id=GetDlgCtrlID(entry.hwnd);if(id!=Open&&id!=Capture&&id!=Recent&&id!=Play&&id!=Stop&&id!=Fullscreen&&id!=OriginalHold&&id!=Split&&id!=CompareToggle&&id!=Reference&&id!=Info)pos(entry.hwnd,0,0,1,1,false);}
    const bool rightVisible=(pro||transition.running)&&g.panelWidth>0&&!full;settingsVisibility(rightVisible&&pro);
    pos(video,full?0:g.left,full?0:g.top,full?w:g.viewWidth,full?h:g.viewHeight,!(pro&&showDiagnostics&&!full));
    SetWindowRgn(video,full||!pro?nullptr:CreateRoundRectRgn(0,0,dip(mainWindow,g.viewWidth),dip(mainWindow,g.viewHeight),dip(mainWindow,30),dip(mainWindow,30)),FALSE);
    put(Brand,8,12,52,32,!full&&pro);surface(GetDlgItem(mainWindow,Brand),RGB(16,17,18));
    put(Capture,0,0,1,1,false);put(Recent,0,0,1,1,false);
    put(WindowMin,w-124,14,32,32,!full&&pro);put(WindowMax,w-88,14,32,32,!full&&pro);put(WindowClose,w-52,14,32,32,!full&&pro);
    for(int id:{WindowMin,WindowMax,WindowClose})surface(GetDlgItem(mainWindow,id),background);
    put(InspectorDrawer,w-384,14,76,32,!full&&pro&&w<960);
    put(Master,g.left+(w<960?120:190),14,116,32,!full&&pro);
    put(Save,g.left+(w>=1080?502:w>=960?318:244),14,76,32,!full&&pro);
    put(ProRailVideo,12,92,44,44,!full&&pro);put(ProRailCapture,12,148,44,44,!full&&pro);put(ImageOpen,12,204,44,44,!full&&pro);put(ScreenCapture,12,260,44,44,!full&&pro);
    for(int id:{ProRailVideo,ProRailCapture,ImageOpen})surface(GetDlgItem(mainWindow,id),RGB(16,17,18));
    const int tabs[]={TabEnhance,TabFg,TabAudio,TabColor,TabExport};for(int i=0;i<5;++i)put(tabs[i],g.right+12+i*((g.panelWidth-24)/5),g.top+12,(g.panelWidth-24)/5,32,rightVisible);
    pos(inspector,g.right+12,g.top+56,g.panelWidth-24,g.statusTop-g.top-68,rightVisible);
    pos(liveStatusPanel,g.right,g.statusTop,g.panelWidth,h-g.statusTop-20,rightVisible);
    const bool transport=!full||fullControls;int barTop=full?h-88:g.bottom;int tx=full?16:g.left+16,tw=full?w-32:g.viewWidth-32;
    pos(playbackBar,0,h-88,w,88,full&&fullControls);
    pos(seekBar,full?16:pro?tx:g.left,barTop-(full?10:5),full?w-32:pro?tw:g.viewWidth,full?20:12,transport&&!target.capture&&!target.image&&!(pro&&showDiagnostics&&!full));surface(seekBar,pro&&!full?panel:cinemaPanel);
    const bool daily=!pro&&!full&&!transition.running;TransportLayout controls(tw,daily);
    const int timeWidth=std::min(240,std::max(0,(tw-154)/2));
    put(TimeLabel,tx,barTop+10,timeWidth,20,transport);
    // Settings messages live here now (the panel no longer draws its own line).
    put(ColourStatus,tx+tw-154-446,barTop+10,440,20,transport);
    int remoteSpace=0;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    remoteSpace=daily?60:0;
    put(RemotePlay,daily?tx+timeWidth:12,daily?barTop+4:316,daily?56:44,daily?28:44,!full&&(daily||pro));
#endif
    put(MediaTitle,tx+timeWidth+8+remoteSpace,barTop+10,std::max(1,tw-timeWidth-170-remoteSpace),20,transport);
    put(FpsLabel,tx+tw-154,barTop+10,154,20,transport);
    auto slot=[&](int id,TransportSlot item,int height=34,int offset=36){put(id,tx+item.x,barTop+offset,item.width,height,transport&&item.width>0);};
    slot(Open,controls.open);slot(Capture,controls.capture);slot(Recent,controls.recent);if(!full&&pro)put(Recent,12,372,44,44);
    if(daily)slot(Master,controls.master);slot(Sr,controls.sr);
    slot(Play,controls.play,44,30);slot(Stop,controls.stop);slot(Mute,controls.mute);slot(Volume,controls.volume,18,44);slot(Subtitle,controls.subtitle);slot(Fullscreen,controls.fullscreen);
    if(daily){slot(ModeSwitch,controls.mode);slot(WindowMin,controls.minimize);slot(WindowClose,controls.close);}
    else put(ModeSwitch,w-292,14,160,32,!full);
    icon(GetDlgItem(mainWindow,Open),Icon::Video,controls.captions);icon(GetDlgItem(mainWindow,Capture),Icon::Capture,controls.captions);
    icon(GetDlgItem(mainWindow,Master),Icon::Enhance,pro||controls.captions);icon(GetDlgItem(mainWindow,Sr),Icon::Upscale,true);
    icon(GetDlgItem(mainWindow,ModeSwitch),pro?Icon::PanelClose:Icon::PanelOpen,pro||controls.captions);
for(int id:std::initializer_list<int>{Open,Capture,Recent,Master,Save,Sr,Play,Stop,Mute,Volume,Subtitle,Fullscreen,TimeLabel,MediaTitle,FpsLabel,ColourStatus,ModeSwitch})surface(GetDlgItem(mainWindow,id),pro&&!full?panel:cinemaPanel);
    put(OriginalHold,tx,barTop+160,96,32,!full&&pro);put(Split,tx+102,barTop+160,92,32,!full&&pro);put(CompareToggle,tx+200,barTop+160,96,32,!full&&pro&&tw>=600);
    put(Reference,tx+(tw>=600?302:200),barTop+160,std::min(172,tw-(tw>=600?302:200)-88),180,!full&&pro&&tw>=500);
    put(Details,tx+tw-76,barTop+160,76,32,!full&&pro);put(Info,12,h-66,44,44,!full&&pro);pos(metricLabel,tx,barTop+212,tw,120,!full&&pro&&uiState.diagnostics);
    pos(statusBar,0,0,1,1,false);pos(diagnosticPanel,g.left,g.top,g.viewWidth,h-g.top-20,!full&&pro&&showDiagnostics);
    const bool empty=currentFile.empty();put(EmptyTitle,0,0,1,1,false);put(EmptyHint,0,0,1,1,false);
    for(int id:{EmptyTitle,EmptyHint})surface(GetDlgItem(mainWindow,id),background);
    pos(subtitleLabel,full?0:g.left,full?0:g.top,full?w:g.viewWidth,full?h:g.viewHeight,uiState.subtitles&&GetWindowTextLengthW(subtitleLabel)>0);
    put(JobProgress,tx,barTop+10,std::min(300,tw),24,exportJob.poll().state!=veyra::engine::ExportState::Idle&&!full);
    // Each HWND enters the batch once. Mixing HIDE/SHOW entries for a child
    // causes DeferWindowPos to preserve the earlier hide flag on Windows.
    auto batch=BeginDeferWindowPos(int(placements.size()));for(const auto& p:placements){if(!batch)break;batch=DeferWindowPos(batch,p.child,nullptr,p.x,p.y,p.width,p.height,p.flags);}
    if(batch)EndDeferWindowPos(batch);else for(const auto& p:placements)SetWindowPos(p.child,nullptr,p.x,p.y,p.width,p.height,p.flags);
    auto front=[&](HWND child){if(child&&IsWindowVisible(child))SetWindowPos(child,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOREDRAW);};
    front(playbackBar);front(seekBar);front(GetDlgItem(mainWindow,RemotePlay));for(int id:{Open,Capture,Recent,Master,Save,Sr,Play,Stop,Mute,Volume,Subtitle,Fullscreen,WindowMin,WindowClose,TimeLabel,MediaTitle,FpsLabel,ModeSwitch,JobProgress,EmptyTitle,EmptyHint})front(GetDlgItem(mainWindow,id));front(inspector);front(subtitleLabel);if(showDiagnostics&&pro&&!full)front(diagnosticPanel);
    if(auto focused=GetFocus();focused&&IsChild(mainWindow,focused)&&!IsWindowVisible(focused))SetFocus(mainWindow);
    RedrawWindow(mainWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    if(smokeDual)veyra::log::info("ui-layout-timing",std::format("frameMs={:.3f} animation={}",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-layoutBegin).count(),transition.running));
}

void subtitleTrackMenu(bool secondary){
    using namespace veyra::ui;
    std::vector<PopupOption> choices{{-1,L"关闭",Icon::Subtitle,(secondary?subtitleSecondary:subtitlePrimary)<0}};
    for(size_t i=0;i<subtitleTracks.size();++i){
        const auto& t=subtitleTracks[i];
        choices.push_back({int(i),std::format(L"{} · {} [{}]{}",i+1,t.name,t.language,t.note.empty()?L"":L" · "+t.note),Icon::Subtitle,(secondary?subtitleSecondary:subtitlePrimary)==int(i)});
    }
    menuOpen=true;
    const int selected=popupSelector(GetDlgItem(mainWindow,Subtitle),choices,(secondary?subtitleSecondary:subtitlePrimary)+1,true,secondary?L"副字幕":L"主字幕");
    menuOpen=false;
    if(selected<0||size_t(selected)>=choices.size())return;
    const int track=choices[selected].command;
    (secondary?subtitleSecondaryChosen:subtitlePrimaryChosen)=true;
    (secondary?subtitleSecondary:subtitlePrimary)=track;
    if(secondary)uiState.subtitleSecondLanguage=track>=0;
    if(track>=0){uiState.subtitles=true;subtitleStatus=std::format(L"{}：{}",secondary?L"副字幕":L"主字幕",subtitleTracks[track].name);}
    veyra::log::info("subtitle",std::format("selected secondary={} track={} stream={} enabled={}",secondary,track,track>=0?subtitleTracks[track].streamIndex:-1,uiState.subtitles));
    layout();
}
void audioTrackMenu(){
    using namespace veyra::ui;
    const auto state=engine.snapshot();
    auto wide=[](const std::string& text){const int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);std::wstring result(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),result.data(),n);return result;};
    std::vector<PopupOption> choices;int current=0;
    for(const auto& t:state.audioTracks){
        if(t.streamIndex==state.selectedAudioTrack)current=int(choices.size());
        choices.push_back({t.streamIndex,std::format(L"{} · {} · {} · {} 声道{}",choices.size()+1,wide(t.language.empty()?"und":t.language),wide(t.codec),t.channels,t.title.empty()?L"":L" · "+wide(t.title)),Icon::None,t.streamIndex==state.selectedAudioTrack});
    }
    if(choices.empty())return;
    menuOpen=true;const int selected=popupSelector(GetDlgItem(mainWindow,Subtitle),choices,current,true,L"音轨");menuOpen=false;
    if(selected>=0&&size_t(selected)<choices.size())engine.selectAudioTrack(state.sessionId,choices[selected].command);
}
void subtitleMenu(){
    using namespace veyra::ui;
    closeSubtitleSettings();
    std::vector<PopupOption> options;std::vector<std::function<void()>> actions;
    auto add=[&](int command,std::wstring label,Icon icon,bool checked,std::function<void()> action){options.push_back({command,std::move(label),icon,checked});actions.push_back(std::move(action));};
    add(Subtitle,L"音轨…",Icon::None,false,[]{audioTrackMenu();});
    add(Subtitle,L"主字幕…",Icon::Subtitle,subtitlePrimary>=0,[]{subtitleTrackMenu(false);});
    add(Subtitle,L"副字幕…",Icon::Subtitle,subtitleSecondary>=0,[]{subtitleTrackMenu(true);});
    add(Subtitle,uiState.subtitles?L"字幕 · 已开启  (B)":L"字幕 · 已关闭  (B)",Icon::Subtitle,uiState.subtitles,[&]{uiState.subtitles=!uiState.subtitles;layout();});
    add(SubtitleLoad,L"载入外部字幕（SRT/ASS/SSA/VTT）…",Icon::Load,false,[&]{
        std::vector<wchar_t> name(32768);OPENFILENAMEW d{sizeof(d)};d.hwndOwner=mainWindow;d.lpstrFile=name.data();d.nMaxFile=32768;
        d.lpstrFilter=L"字幕文件\0*.srt;*.ass;*.ssa;*.vtt\0SubRip (*.srt)\0*.srt\0ASS/SSA (*.ass;*.ssa)\0*.ass;*.ssa\0WebVTT (*.vtt)\0*.vtt\0所有文件\0*.*\0";
        d.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&d))return;
        auto track=veyra::engine::loadSubtitleFile(name.data());
        if(!track.usable()){MessageBoxW(mainWindow,L"这个字幕文件读不出任何字幕（格式不支持或为空）。",L"字幕",MB_OK|MB_ICONINFORMATION);return;}
        track.rebuildIndex();
        track.name=std::format(L"外挂 · {}",std::filesystem::path(name.data()).filename().wstring());
        subtitleTracks.push_back(std::move(track));
        subtitlePrimary=int(subtitleTracks.size())-1;
        subtitlePrimaryChosen=true;
        uiState.subtitles=true;subtitleStatus=std::format(L"已载入 {}",subtitleTracks.back().name);layout();
    });
    add(SubtitleLoad,L"重新扫描内嵌字幕轨",Icon::Load,false,[&]{refreshSubtitleTracks(currentFile);layout();});
    add(SubtitleSize,L"字号、位置与延时…",Icon::Type,false,[]{
        const bool valid=subtitlePrimary>=0&&size_t(subtitlePrimary)<subtitleTracks.size();
        const int initialOffset=valid?subtitleTracks[subtitlePrimary].offsetMs:0;
        showSubtitleSettings(mainWindow,{subtitlePixels,uiState.subtitleMargin,initialOffset,uiPreferences.subtitleLines,uiState.subtitleFont,uiState.subtitleOutline,uiState.subtitleBackground},[lastOffset=initialOffset](const SubtitleSettings& s) mutable {
            subtitlePixels=s.pixels;uiState.subtitleMargin=s.margin;uiPreferences.subtitleLines=s.lines;
            uiState.subtitleFont=s.font;uiState.subtitleOutline=s.outline;uiState.subtitleBackground=s.background;
            if(s.offset!=lastOffset&&subtitlePrimary>=0&&size_t(subtitlePrimary)<subtitleTracks.size())subtitleTracks[subtitlePrimary].offsetMs=s.offset;
            lastOffset=s.offset;
        });
    });
    add(SubtitleSize,L"自动对齐到音轨（实验）",Icon::Type,false,[]{startSubtitleAutoAlign();});
    menuOpen=true;const int selected=popupSelector(GetDlgItem(mainWindow,Subtitle),options,0,true,L"字幕与音轨");menuOpen=false;
    if(selected>=0&&size_t(selected)<actions.size()&&actions[size_t(selected)])actions[size_t(selected)]();
    pointerActivity();
}
void takeScreenshot(){
    const auto state=engine.snapshot();
    if(!state.running||!state.frames||state.failed||state.applying||screenshotPending)return;
    PWSTR pictures=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_Pictures,KF_FLAG_DEFAULT,nullptr,&pictures))){
        MessageBoxW(mainWindow,L"无法找到图片文件夹。",L"截图",MB_OK|MB_ICONERROR);return;
    }
    const auto folder=std::filesystem::path(pictures)/L"Veyra Screenshots";CoTaskMemFree(pictures);
    std::error_code error;std::filesystem::create_directories(folder,error);
    if(error){MessageBoxW(mainWindow,L"无法创建截图文件夹，请检查写入权限。",L"截图",MB_OK|MB_ICONERROR);return;}
    SYSTEMTIME now{};GetLocalTime(&now);
    screenshotPath=(folder/std::format(L"Veyra-{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}-{}.png",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetTickCount64())).wstring();
    if(smokeScreenshot&&!smokeSave.empty())screenshotPath=smokeSave;
    screenshotPending=true;screenshotTick=GetTickCount64();
    engine.saveFrame(screenshotPath);SetWindowTextW(GetDlgItem(mainWindow,Save),L"保存中…");
}
void startVideoExport(bool hevc){
    const auto s=engine.snapshot();
    const wchar_t* reason=nullptr;
    if(currentFile.empty()||s.capture||s.image)reason=L"请先打开一个本地视频。";
    else if(exportJob.poll().active())reason=L"已有导出任务，请先完成或取消当前任务。";
    if(reason){veyra::log::warn("export-dialog","request rejected by source/settings/job state");MessageBoxW(mainWindow,reason,L"暂时无法导出",MB_OK|MB_ICONINFORMATION);return;}
    // The worker initializes its own graph from the current requested settings.
    std::vector<wchar_t> name(32768);
    const auto suggested=std::filesystem::path(currentFile).stem().wstring()+L"-Veyra.mp4";
    wcsncpy_s(name.data(),name.size(),suggested.c_str(),_TRUNCATE);
    OPENFILENAMEW d{sizeof(d)};d.hwndOwner=mainWindow;d.lpstrFile=name.data();d.nMaxFile=DWORD(name.size());
    d.lpstrFilter=L"MP4视频\0*.mp4\0";d.lpstrDefExt=L"mp4";
    d.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
    veyra::log::info("export-dialog","opening destination dialog");
    if(GetSaveFileNameW(&d)){
        // Never imply overwrite support: the worker deliberately preserves
        // existing files, including a recoverable partial from an earlier job.
        if(std::filesystem::exists(name.data())||std::filesystem::exists(std::wstring(name.data())+L".partial")){
            MessageBoxW(mainWindow,L"这个名称的文件或 partial 已存在。为保留原文件，请使用新名称。",L"请选择新名称",MB_OK|MB_ICONINFORMATION);return;
        }
        exportJob.start(currentFile,name.data(),uiState.effective(),hevc,0,s.selectedAudioTrack);jobPaused=false;layout();
    }else if(const auto error=CommDlgExtendedError()){
        veyra::log::error("export-dialog",std::format("GetSaveFileNameW failed code=0x{:08X}",error));
        MessageBoxW(mainWindow,std::format(L"无法打开保存位置窗口，错误 0x{:08X}。详见诊断日志。",error).c_str(),L"导出窗口错误",MB_OK|MB_ICONERROR);
    }else veyra::log::info("export-dialog","destination dialog cancelled");
}

#include "UiRepairChecks.h"
#include "TransportChecks.h"
#include "FgOnlyChecks.h"
// Keep large snapshots out of reentrant sizing/paint/default callbacks.
// noinline prevents Release/LTCG from restoring a large common WndProc frame.
__declspec(noinline) LRESULT systemCommand(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_SYSCOMMAND:
    if((wp&0xfff0)==SC_SCREENSAVE||(wp&0xfff0)==SC_MONITORPOWER){
        const auto s=engine.snapshot();
        if(s.running&&!s.failed&&!s.image&&s.transport==veyra::engine::TransportState::Playing)return 0;
    }break;
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT createWindow(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_CREATE:{mainWindow=hwnd;backdrop.attach(hwnd);font=veyra::ui::makeFont(hwnd);veyra::ui::titleTheme(hwnd);uiState.configured=initialOptions.snapshot();uiState.enhanced=uiPreferences.enhancementEnabled<0?(initialOptions.nr||initialOptions.sr||initialOptions.fg||initialOptions.settings.videoHdr.enabled):uiPreferences.enhancementEnabled!=0;
engine.requestSettings(uiState.effective());
#ifdef VEYRA_ENABLE_REMOTEPLAY
control(L"BUTTON",L"PS5",RemotePlay,BS_PUSHBUTTON,0,0,56,28);veyra::ui::ghost(GetDlgItem(hwnd,RemotePlay));SetPropW(GetDlgItem(hwnd,RemotePlay),L"veyra.tip",HANDLE(L"PS5 串流 · 配对与连接"));
#endif
control(L"BUTTON",L"打开",Open,BS_PUSHBUTTON,10,10,150,30);control(L"BUTTON",L"播放",Play,BS_PUSHBUTTON,168,10,68,30);control(L"BUTTON",L"停止",Stop,BS_PUSHBUTTON,244,10,62,30);control(L"BUTTON",L"截图",Save,BS_PUSHBUTTON,314,10,95,30);
control(L"BUTTON",L"DLSS5 NR",Nr,BS_AUTOCHECKBOX,425,10,110,30);control(L"BUTTON",L"SR",Sr,BS_AUTOCHECKBOX,518,10,78,30);control(L"BUTTON",L"补帧",Fg,BS_AUTOCHECKBOX,600,10,78,30);CheckDlgButton(hwnd,Nr,initialOptions.nr?BST_CHECKED:BST_UNCHECKED);
control(L"BUTTON",L"采集",Capture,BS_PUSHBUTTON,690,10,75,30);control(L"BUTTON",L"导出视频",Export,BS_PUSHBUTTON,773,10,103,30);control(L"BUTTON",L"性能/诊断",Info,BS_PUSHBUTTON,884,10,100,30);
control(L"BUTTON",L"实时NR档",Realtime,BS_AUTOCHECKBOX,952,10,100,30);
control(L"BUTTON",L"最近打开",Recent,BS_PUSHBUTTON,1072,10,75,30);
auto mult=control(L"COMBOBOX",L"",Multiplier,CBS_DROPDOWNLIST,1150,10,70,180);for(auto label:{L"2X",L"3X",L"4X",L"6X"})SendMessageW(mult,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));{int sel=0;for(size_t i=1;i<veyra::engine::kFgMultiplierChoiceCount;++i)if(veyra::engine::kFgMultiplierChoices[i]==initialOptions.fgMultiplier)sel=int(i)-1;SendMessageW(mult,CB_SETCURSEL,sel,0);}
control(L"BUTTON",L"参数与色彩",Settings,BS_PUSHBUTTON,1225,10,100,30);
auto original=control(L"BUTTON",L"按住原图 V",OriginalHold,BS_PUSHBUTTON,0,0,110,30);SetWindowSubclass(original,interaction,OriginalHold,0);
control(L"BUTTON",L"原图/增强切换",CompareToggle,BS_PUSHBUTTON,0,0,120,30);control(L"BUTTON",L"分屏拖动",Split,BS_PUSHBUTTON,0,0,95,30);
auto ref=control(L"COMBOBOX",L"",Reference,CBS_DROPDOWNLIST,0,0,170,180);for(auto label:{L"对比：输入原画",L"对比：增强前底图"})SendMessageW(ref,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(ref,CB_SETCURSEL,0,0);control(L"BUTTON",L"全屏",Fullscreen,BS_PUSHBUTTON,0,0,108,30);

WNDCLASSW barClass{};barClass.lpfnWndProc=barProc;barClass.hInstance=GetModuleHandleW(nullptr);barClass.lpszClassName=L"VeyraPlaybackBar";barClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&barClass);
playbackBar=CreateWindowExW(0,barClass.lpszClassName,L"播放控制条",WS_CHILD|WS_CLIPSIBLINGS,0,0,1,1,hwnd,nullptr,barClass.hInstance,nullptr);
subtitleLabel=veyra::ui::createSubtitleOverlay(hwnd);
video=control(L"STATIC",L"",VideoSurface,SS_BLACKRECT|SS_NOTIFY,8,52,1100,610);seekBar=control(TRACKBAR_CLASSW,L"",Seek,TBS_HORZ,8,675,1100,24);SendMessageW(seekBar,TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));statusBar=control(L"STATIC",L"打开本地视频或PNG/JPEG。本地实验运行时，非NVIDIA官方产品。",0,SS_LEFT,12,708,1080,36);
SetWindowSubclass(video,interaction,VideoSurface,0);
auto brand=control(L"STATIC",L"",Brand,SS_CENTER,0,0,100,30);
SetWindowSubclass(brand,veyra::ui::brandIconProc,Brand,0);
control(L"BUTTON",L"专业模式",ModeSwitch,BS_PUSHBUTTON,0,0,164,36);
control(L"BUTTON",L"增强已开启",Master,BS_PUSHBUTTON,0,0,126,36);
control(L"BUTTON",L"视频",ProRailVideo,BS_PUSHBUTTON,0,0,64,44);control(L"BUTTON",L"采集",ProRailCapture,BS_PUSHBUTTON,0,0,64,44);control(L"BUTTON",L"图片",ImageOpen,BS_PUSHBUTTON,0,0,64,44);control(L"BUTTON",L"参数",InspectorDrawer,BS_PUSHBUTTON,0,0,84,36);
control(L"BUTTON",L"屏幕采集",ScreenCapture,BS_PUSHBUTTON,0,0,44,44);veyra::ui::icon(GetDlgItem(hwnd,ScreenCapture),veyra::ui::Icon::ScreenCapture);veyra::ui::ghost(GetDlgItem(hwnd,ScreenCapture));SetPropW(GetDlgItem(hwnd,ScreenCapture),L"veyra.tip",HANDLE(L"屏幕采集 · 窗口 / 显示器"));
control(L"BUTTON",L"音频",TabAudio,BS_PUSHBUTTON,0,0,60,36);
const wchar_t* tabs[]={L"增强",L"运动",L"色彩",L"导出"};for(int i=0;i<4;++i)control(L"BUTTON",tabs[i],TabEnhance+i,BS_PUSHBUTTON,0,0,76,36);
control(L"BUTTON",L"音量",Mute,BS_PUSHBUTTON,0,0,54,36);auto vol=control(TRACKBAR_CLASSW,L"音量",Volume,TBS_HORZ|TBS_NOTICKS,0,0,80,26);SendMessageW(vol,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(vol,TBM_SETPOS,TRUE,100);
control(L"BUTTON",L"字幕",Subtitle,BS_PUSHBUTTON,0,0,62,36);control(L"STATIC",L"尚未打开媒体",MediaTitle,SS_LEFT|SS_ENDELLIPSIS,0,0,250,26);control(L"STATIC",L"00:00 / 00:00",TimeLabel,SS_LEFT,0,0,200,20);
control(L"STATIC",L"处理 0.0 fps",FpsLabel,SS_RIGHT,0,0,154,20);
control(L"STATIC",L"",ColourStatus,SS_RIGHT|SS_ENDELLIPSIS,0,0,440,20);
control(L"STATIC",L"开始观看",EmptyTitle,SS_CENTER,0,0,500,48);emptyFont=veyra::ui::makeFont(hwnd,26,FW_NORMAL);SendDlgItemMessageW(hwnd,EmptyTitle,WM_SETFONT,WPARAM(emptyFont),TRUE);control(L"STATIC",L"打开本地视频，或连接采集卡\n精细调整与原生导出在专业模式中",EmptyHint,SS_CENTER,0,0,500,68);
control(L"BUTTON",L"性能  ▾",Details,BS_PUSHBUTTON,0,0,112,32);control(L"BUTTON",L"",JobProgress,BS_PUSHBUTTON,0,0,280,32);metricLabel=control(L"STATIC",L"",0,SS_LEFT,0,0,500,132);
control(L"BUTTON",L"最小化",WindowMin,BS_PUSHBUTTON,0,0,32,32);control(L"BUTTON",L"最大化",WindowMax,BS_PUSHBUTTON,0,0,32,32);control(L"BUTTON",L"关闭窗口",WindowClose,BS_PUSHBUTTON,0,0,32,32);
using veyra::ui::Icon;veyra::ui::icon(GetDlgItem(hwnd,WindowMin),Icon::Minimize);veyra::ui::icon(GetDlgItem(hwnd,WindowMax),Icon::Maximize);veyra::ui::icon(GetDlgItem(hwnd,WindowClose),Icon::Close);
veyra::ui::icon(GetDlgItem(hwnd,Play),Icon::Play);veyra::ui::icon(GetDlgItem(hwnd,Stop),Icon::Stop);veyra::ui::icon(GetDlgItem(hwnd,Mute),Icon::Volume);veyra::ui::icon(GetDlgItem(hwnd,Subtitle),Icon::Subtitle);veyra::ui::icon(GetDlgItem(hwnd,Fullscreen),Icon::Fullscreen);
veyra::ui::icon(GetDlgItem(hwnd,ProRailVideo),Icon::Video);veyra::ui::icon(GetDlgItem(hwnd,ProRailCapture),Icon::Capture);veyra::ui::icon(GetDlgItem(hwnd,ImageOpen),Icon::Image);veyra::ui::icon(GetDlgItem(hwnd,Info),Icon::Subtitle);
veyra::ui::icon(GetDlgItem(hwnd,Recent),Icon::Recent);veyra::ui::icon(GetDlgItem(hwnd,Open),Icon::Video,true);veyra::ui::icon(GetDlgItem(hwnd,Capture),Icon::Capture,true);
for(int id:{Open,Capture,Recent,Master,Save,Sr,ModeSwitch,WindowMin,WindowMax,WindowClose,Stop,Mute,Subtitle,Fullscreen,ProRailVideo,ProRailCapture,ImageOpen,Info})veyra::ui::ghost(GetDlgItem(hwnd,id));
SetPropW(GetDlgItem(hwnd,Open),L"veyra.tip",HANDLE(L"打开视频 / 图片 · Ctrl+O"));SetPropW(GetDlgItem(hwnd,Capture),L"veyra.tip",HANDLE(L"连接采集卡"));SetPropW(GetDlgItem(hwnd,Sr),L"veyra.tip",HANDLE(L"超分辨率 · 专业面板选择 DLSS / RTX 视频超分"));
inspector=veyra::ui::createSettingsPanel(hwnd,engine,applySettings);
// Settings messages go to the player's bottom bar instead of a line inside the panel.
veyra::ui::settingsStatusSink([](const std::wstring& text){SetWindowTextW(GetDlgItem(mainWindow,ColourStatus),text.c_str());});
liveStatusPanel=veyra::ui::createLiveStatusPanel(hwnd,engine);selectInspector(uiPreferences.inspector);SendDlgItemMessageW(hwnd,Volume,TBM_SETPOS,TRUE,LPARAM(uiPreferences.volume*100));veyra::ui::marked(GetDlgItem(hwnd,Play));
for(auto [id,help]:std::initializer_list<std::pair<int,const wchar_t*>>{
 {Nr,L"实验性DLSS5 NR增强：重建画面细节，效果看素材，不是游戏原生集成。"},
 {Fg,L"开关补帧。专业模式可选倍率和后端；数字翻倍，显卡工作量也会涨。"},
 {Realtime,L"实时档降低NR内部处理尺寸，减轻负担；原生档更费算力。"},
 {Multiplier,L"选择补帧倍率。帧数不是越多越好，跟不上时会跳过过期机会。"},
 {Save,L"保存处理后的完整画面到系统图片文件夹的 Veyra Screenshots。SDR为PNG，HDR为保留高亮的JXR；只拍画面，不拍工具栏或驱动补帧。"},
 {Master,L"总增强开关。关闭后保留设置，重新开启不用重调配方。"},
 {Volume,L"播放音量，不改变音画同步偏移。"},{Mute,L"静音或恢复声音，让耳朵休息一下。"},
 {Subtitle,L"显示或隐藏字幕。字幕在增强后叠加，不让算法给字加戏。"},{SubtitleLoad,L"加载本地字幕文件。对白太快，给眼睛加个帮手。"},{SubtitleSize,L"调整字幕字号，不改变导出视频尺寸。"},
 {OriginalHold,L"查看原始画面对照，松开回到增强效果。眼见为实。"},{Reference,L"选择对照底图。低延迟NR先行时，NR前底图为原图缩放，不是独立超分对照。"},{CompareToggle,L"切换画面对比，方便看清到底改了哪里。"},{Split,L"拖动对比边界，两边当面对质。"},
 {Seek,L"拖动跳转；松手后等待解码和增强预热。跳转中保持目标位置，不会故意弹回。"}})SetPropW(GetDlgItem(hwnd,id),L"veyra.tip",HANDLE(help));
SetPropW(video,L"veyra.tip",HANDLE(L"专业模式：滚轮缩放 · 中键拖动画面 · 右键恢复适应窗口。缩放不影响增强或保存尺寸。"));tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);SetWindowTheme(tooltips,L"",L"");SendMessageW(tooltips,TTM_SETTIPBKCOLOR,RGB(28,31,33),0);SendMessageW(tooltips,TTM_SETTIPTEXTCOLOR,RGB(225,230,228),0);SendMessageW(tooltips,TTM_SETDELAYTIME,TTDT_INITIAL,550);SendMessageW(tooltips,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,360);EnumChildWindows(hwnd,[](HWND child,LPARAM context)->BOOL{auto tip=reinterpret_cast<HWND>(context);wchar_t cls[32]{};GetClassNameW(child,cls,32);if(child==video||_wcsicmp(cls,L"BUTTON")==0||_wcsicmp(cls,L"EDIT")==0||_wcsicmp(cls,L"COMBOBOX")==0||_wcsicmp(cls,TRACKBAR_CLASSW)==0){TOOLINFOW info{TTTOOLINFOW_V2_SIZE};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=GetParent(child);info.uId=UINT_PTR(child);auto help=GetPropW(child,L"veyra.tip");info.lpszText=help?reinterpret_cast<wchar_t*>(help):LPSTR_TEXTCALLBACKW;if(!SendMessageW(tip,TTM_ADDTOOLW,0,LPARAM(&info)))veyra::log::error("ui-help","Tooltip registration failed");}return TRUE;},LPARAM(tooltips));
if(smokeSeconds>0)startTick=GetTickCount64();diagnosticPanel=veyra::ui::createTelemetryPanel(hwnd,engine);DragAcceptFiles(hwnd,TRUE);startShellTimer(hwnd,TelemetryTimer,100);layout();return 0;}
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT paintWindow(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_PAINT:{veyra::ui::PaintBuffer paint(hwnd);auto g=chromeLayout(MulDiv(paint.rect.right,96,veyra::ui::layoutDpi(hwnd)),MulDiv(paint.rect.bottom,96,veyra::ui::layoutDpi(hwnd)));veyra::ui::paintChrome(hwnd,paint.dc,g,engine.snapshot(),full);return 0;}

}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT savePresentation(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_APP+46:{uiPreferences.presentation=engine.snapshot().presentation;if(smokeSeconds<=0&&!preferences.save(uiPreferences,nullptr))veyra::log::warn("preferences","Could not persist presentation settings");return 0;}
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT protectionCommand(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_APP+45:{if(masterPendingRevision)return 0;auto settings=uiState.enhanced?engine.snapshot().desired:uiState.configured;
    if(wp==213){auto current=engine.snapshot();if(uiState.mode!=veyra::ui::Mode::Professional||!current.running||!current.frames)return 0;bool space=false;for(auto q:settings.protection.regions)space|=q.empty();if(!space)return 0;cancelProtection();protectionArmed=true;SetFocus(video);return 1;}
    if(wp==214){cancelProtection();settings.protection={};}else if(wp==206)settings.protection.enabled=lp==BST_CHECKED;else if(wp==222)settings.protection.featherPixels=float(std::clamp(int(lp),0,64));else return 0;
    return applySettings(settings)?1:0;}
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT enhancementCommand(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_APP+44:{if(masterPendingRevision)return 0;auto setting=uiState.enhanced?engine.snapshot().desired:uiState.configured;const bool enabled=wp==202?lp>1:lp==BST_CHECKED;if(wp==200)setting.nr=enabled;else if(wp==201)setting.sr=enabled;else if(wp==230)setting.videoHdr.enabled=enabled;else if(wp==202){bool allowed=false;for(auto m:veyra::engine::kFgMultiplierChoices)if(uint32_t(lp)==m)allowed=true;if(!allowed)return 0;setting.multiplier=uint32_t(lp);if(!setting.validate().empty())return 0;}else return 0;
    const bool enablesMaster=enabled&&!uiState.enhanced;if(enablesMaster){masterPreviousEnabled=false;uiState.enhanced=true;}
    if(!applySettings(setting)){if(enablesMaster)uiState.enhanced=false;veyra::ui::settingsEnabled(uiState.enhanced,uiState.configured);return 0;}
    if(auto pending=engine.snapshot();enablesMaster&&pending.running){masterPendingRevision=pending.desired.revision;masterPendingSession=pending.sessionId;}veyra::log::info("ui-feature",std::format("click={} enabled={} requestedRevision={} draft-independent=true",wp==200?"NR":wp==201?"SR":wp==230?"VideoHDR":"FG",enabled,engine.snapshot().desired.revision));return 1;}
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT windowCommand(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_COMMAND:switch(LOWORD(wp)){
case Open:case ProRailVideo:case ImageOpen:openFile(fileDialog(false));layout();break;
case ScreenCapture:veyra::ui::showScreenCapturePanel(hwnd,[](const std::wstring& path){openFile(path);layout();},[]{engine.stop();},[]{return engine.snapshot();},[](bool fill){screenFill=fill;screenFitClient={};screenFitImage={};engine.previewView({});});break;

case ModeSwitch:switchMode();break;
case WindowMin:ShowWindow(hwnd,SW_MINIMIZE);break;case WindowMax:toggleFullscreen();break;case WindowClose:SendMessageW(hwnd,WM_CLOSE,0,0);break;
case Master:if(masterPendingRevision)break;masterPreviousEnabled=uiState.enhanced;if(uiState.enhanced)uiState.configured=engine.snapshot().desired;uiState.enhanced=!uiState.enhanced;veyra::ui::settingsEnabled(uiState.enhanced,uiState.configured);engine.requestSettings(uiState.effective());if(auto pending=engine.snapshot();pending.running){masterPendingRevision=pending.desired.revision;masterPendingSession=pending.sessionId;}break;
case InspectorDrawer:uiState.drawer=!uiState.drawer;layout();break;
case TabAudio:selectInspector(4);break;
case TabEnhance:case TabFg:case TabColor:case TabExport:selectInspector(LOWORD(wp)-TabEnhance);break;
case JobProgress:if(uiState.mode==veyra::ui::Mode::Daily)switchMode();uiState.drawer=true;selectInspector(3);layout();break;
case Details:uiState.diagnostics=!uiState.diagnostics;SetWindowTextW(GetDlgItem(hwnd,Details),uiState.diagnostics?L"性能  ▴":L"性能  ▾");layout();break;
case Mute:{auto s=engine.snapshot();engine.setVolume(s.volume,!s.muted);break;}
case Subtitle:if(lp)subtitleMenu();else{uiState.subtitles=!uiState.subtitles;layout();}break;
case SubtitleLoad:{std::vector<wchar_t> name(32768);OPENFILENAMEW d{sizeof(d)};d.hwndOwner=hwnd;d.lpstrFile=name.data();d.nMaxFile=32768;d.lpstrFilter=L"字幕文件\0*.srt;*.ass;*.ssa;*.vtt\0所有文件\0*.*\0";d.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&d)){auto track=veyra::engine::loadSubtitleFile(name.data());if(track.usable()){track.rebuildIndex();track.name=std::format(L"外挂 · {}",std::filesystem::path(name.data()).filename().wstring());subtitleTracks.push_back(std::move(track));subtitlePrimary=int(subtitleTracks.size())-1;subtitlePrimaryChosen=true;uiState.subtitles=true;}else subtitleStatus=L"这个字幕文件读不出任何字幕";layout();}break;}
case SubtitleSize:subtitlePixels=subtitlePixels==22?28:subtitlePixels==28?34:22;break;
case Recent:{std::vector<wchar_t> recent(32768);GetPrivateProfileStringW(L"Player",L"最近打开",L"",recent.data(),32768,(veyra::runtime::localDataDirectory()/"veyra.ini").wstring().c_str());openFile(recent.data());break;}
case Play:{auto s=engine.snapshot();if(s.capture||s.image||s.transport==veyra::engine::TransportState::Opening||s.transport==veyra::engine::TransportState::Stopping)break;if(!currentFile.empty()&&(!s.running||s.transport==veyra::engine::TransportState::Ended)){openFile(currentFile);break;}if(seekPreview.active){seekPreview.resume=!seekPreview.resume;paused=!seekPreview.resume;break;}paused=s.transport==veyra::engine::TransportState::Playing;engine.pause(paused);SetWindowTextW(GetDlgItem(hwnd,Play),paused?L"播放":L"暂停");break;}
case Stop:seekPreview={};engine.stop();break;
case Save:takeScreenshot();break;
case Sr:SendMessageW(hwnd,WM_APP+44,201,IsDlgButtonChecked(hwnd,Sr));break;
case Multiplier:case Realtime:case Nr:case Fg:{auto changed=engine.snapshot().desired;const auto id=LOWORD(wp);
if(id==Nr)changed.nr=IsDlgButtonChecked(hwnd,Nr)==BST_CHECKED;
if(id==Sr)changed.sr=IsDlgButtonChecked(hwnd,Sr)==BST_CHECKED;
if(id==Realtime)changed.nrPolicy=IsDlgButtonChecked(hwnd,Realtime)==BST_CHECKED?veyra::pipeline::NrSizePolicy::Realtime:veyra::pipeline::NrSizePolicy::Native;
if(id==Fg)changed.multiplier=IsDlgButtonChecked(hwnd,Fg)==BST_CHECKED?multiplierFromDailyIndex(int(SendDlgItemMessageW(hwnd,Multiplier,CB_GETCURSEL,0,0))):1;
if(id==Multiplier&&HIWORD(wp)==CBN_SELCHANGE&&changed.multiplier>1)changed.multiplier=multiplierFromDailyIndex(int(SendDlgItemMessageW(hwnd,Multiplier,CB_GETCURSEL,0,0)));
engine.requestSettings(changed);break;}

case Fullscreen:toggleFullscreen();break;
case VideoSurface:if(HIWORD(wp)==STN_DBLCLK)toggleFullscreen();break;
case CompareToggle:compareMode=compareMode==1?0:1;updateComparison();break;
case Split:compareMode=compareMode==2?0:2;updateComparison();break;
case Reference:referenceBase=SendDlgItemMessageW(hwnd,Reference,CB_GETCURSEL,0,0)==1;updateComparison();break;
case Settings:if(uiState.mode==veyra::ui::Mode::Daily)switchMode();selectInspector(0);break;
#ifdef VEYRA_ENABLE_REMOTEPLAY
case RemotePlay:{
    veyra::ui::CaptureConfigBlock::Callbacks captureCallbacks;
    captureCallbacks.readSdr=[]{return (uiState.enhanced?engine.snapshot().desired:uiState.configured).forceSdrPreview;};
    captureCallbacks.setSdr=[](bool enabled){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.forceSdrPreview=enabled;return applySettings(s);};
    captureCallbacks.readAudioIngress=[]{return int((uiState.enhanced?engine.snapshot().desired:uiState.configured).captureAudio);};
    captureCallbacks.setAudioIngress=[](int mode){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.captureAudio=static_cast<veyra::engine::CaptureAudioIngress>(std::clamp(mode,0,2));return applySettings(s);};
    captureCallbacks.readFlip=[]{return (uiState.enhanced?engine.snapshot().desired:uiState.configured).captureFlipVertical;};
    captureCallbacks.setFlip=[](bool enabled){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.captureFlipVertical=enabled;return applySettings(s);};
    captureCallbacks.readBuffer=[]{return int((uiState.enhanced?engine.snapshot().desired:uiState.configured).captureBuffer);};
    captureCallbacks.setBuffer=[](int mode){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.captureBuffer=static_cast<veyra::source::CaptureBufferMode>(std::clamp(mode,0,2));return applySettings(s);};
    veyra::ui::showRemotePlayPanel(hwnd,[](veyra::source::RemotePlayConnectDesc desc){cancelProtection();remoteViewOnly=desc.request.viewOnly;if(!desc.request.viewOnly){if(!remoteController.start())veyra::log::warn("remoteplay-input","SDL gamepad initialization failed");startShellTimer(mainWindow,ControllerTimer,8);}else{KillTimer(mainWindow,ControllerTimer);remoteController.stop();}currentFile=L"remoteplay:";refreshSubtitleTracks(currentFile);paused=false;engine.previewView({});engine.openRemotePlay(video,std::move(desc),options());SetWindowTextW(mainWindow,L"Veyra — PS5 Remote Play");layout();},[](veyra::source::RemotePlayConnectDesc desc,const std::wstring& capturePath){cancelProtection();remoteViewOnly=false;if(!remoteController.start())veyra::log::warn("remoteplay-input","SDL gamepad initialization failed");startShellTimer(mainWindow,ControllerTimer,8);currentFile=capturePath;refreshSubtitleTracks(currentFile);paused=false;engine.previewView({});engine.openRemotePlayCapture(video,std::move(desc),capturePath,options());SetWindowTextW(mainWindow,L"Veyra — 采集卡 · PS5 控制");layout();},[](std::string pin){engine.remotePlayLoginPin(std::move(pin));},[]{auto s=engine.snapshot();veyra::ui::RemotePlayPanelStatus result;
    if(s.remoteControl){
        // Combined mode: the PS5 link is control-only; picture and audio come
        // from the capture card run.
        result.active=true;
        if(s.remotePlayState==int(veyra::remoteplay::SessionState::Failed)){
            // The control link died (console unreachable, refused or timed
            // out); never claim the combined link is working.
            result.message=(s.remoteRecoveryMessage.empty()?std::wstring(L"PS5 控制通道未连上或已断开。"):s.remoteRecoveryMessage)
                +L"\n采集画面不受影响。确认主机已开机（必要时用“唤醒已配对 PS5”），再点“连接并观看”重建控制通道。";
        }else result.message=s.remoteRecovering?s.remoteRecoveryMessage:s.capture&&s.running?L"生效：采集卡画面 · PS5 控制已连接。手柄输入发送到 PS5；画面与声音来自采集卡，不受串流画质限制。":L"PS5 控制通道存活，采集画面未在播放；点“连接并观看”重新拉起，或断开连接结束控制。";
        const auto c=remoteController.capabilities();
        if(!c.connected)result.message+=L"\n未检测到电脑手柄。";
        else result.message+=std::format(L"\n陀螺仪 {} · 触摸板 {} · 扳机 {} · 触觉 {}{}",c.gyro&&c.accel?L"已启用":L"不可用",c.touch?L"可用":L"不可用",c.triggers?L"已接入":L"不可用",c.haptics?L"端点已打开":L"未打开",c.calibrating?L" · 校准中（返回播放器静置）":L"");
        return result;
    }
    result.active=s.remotePlay&&(s.running||s.transport==veyra::engine::TransportState::Opening||s.transport==veyra::engine::TransportState::Stopping);
    result.message=s.failed?s.status:s.remoteRecovering?s.remoteRecoveryMessage:s.remotePlay&&s.running?L"PS5 画面已进入播放；关闭此面板不停止串流。":result.active?s.status:L"PS5 串流已停止，可以重新连接。";
    if(s.remotePlay&&s.running){
        if(!s.failed&&!s.remoteRecovering){const auto& r=s.remoteStream;const auto& p=r.requestedProfile;
            result.message=std::format(L"生效：{}p / {} fps / {} / 请求 {} Mbps\n实收视频 {:.1f} Mbps · 格式/码率更改后需点重连",p.height,p.fps,p.codec==veyra::remoteplay::Codec::H264?L"H.264":p.codec==veyra::remoteplay::Codec::H265Hdr?L"H.265 HDR":L"H.265",p.bitrateKbps/1000,r.videoMbps);}
        const auto c=remoteController.capabilities();
        if(remoteViewOnly)result.message+=L"\n仅观看：电脑输入已关闭，手柄由 PS5 处理。";
        else if(!c.connected)result.message+=L"\n未检测到电脑手柄。";
        else result.message+=std::format(L"\n陀螺仪 {} · 触摸板 {} · 扳机 {} · 触觉 {}{}",c.gyro&&c.accel?L"已启用":L"不可用",c.touch?L"可用":L"不可用",c.triggers?L"已接入":L"不可用",c.haptics?L"端点已打开":L"未打开",c.calibrating?L" · 校准中（返回播放器静置）":L"");
    }return result;
},[]{engine.stopRemotePlayControl();engine.stop();},[]{return remoteController.calibrate();},std::move(captureCallbacks));break;}
#endif
case Capture:case ProRailCapture:veyra::ui::showCapturePanel(hwnd,[](const std::wstring& path){openFile(path);layout();},[]{return (uiState.enhanced?engine.snapshot().desired:uiState.configured).forceSdrPreview;},[](bool enabled){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.forceSdrPreview=enabled;return applySettings(s);},[]{return int((uiState.enhanced?engine.snapshot().desired:uiState.configured).captureAudio);},[](int mode){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.captureAudio=static_cast<veyra::engine::CaptureAudioIngress>(std::clamp(mode,0,2));return applySettings(s);},[]{return (uiState.enhanced?engine.snapshot().desired:uiState.configured).captureFlipVertical;},[](bool enabled){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.captureFlipVertical=enabled;return applySettings(s);},[]{return int((uiState.enhanced?engine.snapshot().desired:uiState.configured).captureBuffer);},[](int mode){auto s=uiState.enhanced?engine.snapshot().desired:uiState.configured;s.captureBuffer=static_cast<veyra::source::CaptureBufferMode>(std::clamp(mode,0,2));return applySettings(s);});break;
case Export:if(uiState.mode==veyra::ui::Mode::Professional)startVideoExport(false);break;
case Info:showDiagnostics=!showDiagnostics;layout();break;
}return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT scrollCommand(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==GetDlgItem(hwnd,Volume)){engine.setVolume(float(SendDlgItemMessageW(hwnd,Volume,TBM_GETPOS,0,0))/100,false);return 0;}if(reinterpret_cast<HWND>(lp)==seekBar&&LOWORD(wp)!=TB_ENDTRACK){dragging=LOWORD(wp)==TB_THUMBTRACK;auto s=engine.snapshot();if(s.duration>0)seekPreview.update(s.duration*SendMessageW(seekBar,TBM_GETPOS,0,0)/10000.0,!dragging);}return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT windowTimer(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_TIMER:
#ifdef VEYRA_ENABLE_REMOTEPLAY
if(wp==ControllerTimer){const auto state=engine.snapshot();if((state.remotePlay&&(state.running||state.transport==veyra::engine::TransportState::Opening))||state.remoteControl){const bool focused=GetForegroundWindow()==hwnd;engine.remotePlayController(remoteController.poll(focused));remoteController.feedback(engine.remotePlayFeedback(),focused);}else{engine.remotePlayController({});remoteController.stop();KillTimer(hwnd,ControllerTimer);}return 0;}
#endif
{if(wp==TransitionTimer){transition.sample(GetTickCount64());if(!transition.running){endTransition();veyra::log::info("ui-transition","completed; final layout and swapchain resize released");}layout();return 0;}
if(full&&fullControls&&!menuOpen&&!veyra::ui::popupSelectorOpen()&&!GetCapture()&&GetTickCount64()-pointerTick>1600){POINT p{};GetCursorPos(&p);ScreenToClient(hwnd,&p);RECT r{};GetClientRect(hwnd,&r);if(p.y<r.bottom-veyra::ui::dip(hwnd,98)||p.x<0||p.x>=r.right||p.y>=r.bottom){fullControls=false;layout();if(GetForegroundWindow()==hwnd)SetCursor(nullptr);veyra::log::info("ui-fullscreen","controls hidden; video and subtitles only");}}
if(closing){if(engine.idle()&&!exportJob.poll().active()){KillTimer(hwnd,TelemetryTimer);DestroyWindow(hwnd);}return 0;}if(!autoInput.empty()){auto file=autoInput;autoInput.clear();openFile(file);startTick=GetTickCount64();if(openColourPageOnStart){openColourPageOnStart=false;if(uiState.mode==veyra::ui::Mode::Daily)switchMode();selectInspector(2);layout();}}if(smokeControls&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(smokeStep==0&&elapsed>2200){engine.pause(true);smokeStep=1;}
if(smokeStep==1&&elapsed>2600){engine.seek(1.0);smokeStep=2;}
if(smokeStep==2&&elapsed>3200){engine.pause(false);smokeStep=3;}
if(smokeStep==3&&elapsed>4100&&!smokeSave.empty()){engine.saveFrame(smokeSave);smokeStep=4;}
if(smokeStep==4&&elapsed>5200&&GetEnvironmentVariableW(L"VEYRA_TEST_LARGE_IMAGE_SAVE_THROW",nullptr,0)){const auto retained=engine.snapshot();if(!retained.failed&&retained.running&&retained.frames>0){engine.saveFrame(smokeSave);smokeStep=5;veyra::log::info("image-save-test","retry after injected allocation failure; result/session retained");}}
}auto s=engine.snapshot();playbackPower.update(!closing&&s.running&&!s.failed&&!s.image&&s.transport==veyra::engine::TransportState::Playing);pollSubtitleTracks();if(subtitleRequestAutoAlign&&subtitlePrimary>=0){subtitleRequestAutoAlign=false;startSubtitleAutoAlign();}pollSubtitleAutoAlign();
if(screenFill&&currentFile.starts_with(L"screen:")&&s.running){
    RECT r{};GetClientRect(video,&r);auto extent=s.metrics.resolution.output;
    if(r.right>0&&r.bottom>0&&extent.width&&extent.height&&(screenFitClient.cx!=r.right||screenFitClient.cy!=r.bottom||screenFitImage.cx!=LONG(extent.width)||screenFitImage.cy!=LONG(extent.height))){
        screenFitClient={r.right,r.bottom};screenFitImage={LONG(extent.width),LONG(extent.height)};
        const float x=float(r.right)/extent.width,y=float(r.bottom)/extent.height;veyra::engine::PreviewView view;view.zoom=std::max(x,y)/std::min(x,y);engine.previewView(view);
    }
}
{
    std::vector<veyra::ui::SubtitleLine> draw;
    const bool showSubtitles=uiState.subtitles&&!subtitleTracks.empty()&&!(showDiagnostics&&uiState.mode==veyra::ui::Mode::Professional&&!full);
    if(showSubtitles){
        auto append=[&](int index,bool secondary){
            if(index<0||size_t(index)>=subtitleTracks.size())return;
            const auto& track=subtitleTracks[size_t(index)];
            if(!track.usable())return;
            const double position=s.position+double(track.offsetMs)/1000.0;
            for(const auto* cue:veyra::engine::cuesAt(track,position,3)){
                veyra::ui::SubtitleLine line;
                line.text=cue->text;
                line.bitmap=cue->bitmap;
                line.style=track.styles.empty()?veyra::engine::SubtitleStyle{}:track.styles[size_t(std::clamp(cue->style,0,int(track.styles.size())-1))];
                if(cue->alignOverride)line.style.alignment=cue->alignOverride;
                line.alignOverride=cue->alignOverride;
                // Normalised 0..1 so the overlay can scale \pos to its own size.
                line.posX=cue->posX>=0?cue->posX/std::max(1.0,track.scriptWidth):-1.0;
                line.posY=cue->posY>=0?cue->posY/std::max(1.0,track.scriptHeight):-1.0;
                line.secondary=secondary;
                draw.push_back(std::move(line));
            }
        };
        append(subtitleSecondary,true);   // secondary first: the painter stacks bottom-up
        append(subtitlePrimary,false);
    }
    veyra::ui::SubtitleView view;
    view.scale=subtitleScale();
    view.fontOverride=uiState.subtitleFont>0?subtitleFontName():std::wstring{};
    view.outline=uiState.subtitleOutline;
    view.background=uiState.subtitleBackground;
    view.bottomMargin=uiState.subtitleMargin;
    view.targetLines=uiPreferences.subtitleLines;
    if(full&&fullControls)view.bottomMargin+=84;
    view.preview=engine.previewView();
    view.videoWidth=s.metrics.resolution.output.width;
    view.videoHeight=s.metrics.resolution.output.height;
    veyra::ui::updateSubtitleOverlay(subtitleLabel,draw,view);
    static std::wstring lastSubtitleLog;
    if(!draw.empty()){
        std::wstring joined;
        for(const auto& line:draw){if(!joined.empty())joined+=L" | ";joined+=line.text;}
        if(joined!=lastSubtitleLog){lastSubtitleLog=joined;veyra::log::info("subtitle-overlay",std::format("text={}",narrow(joined)));}
    }else if(!lastSubtitleLog.empty())lastSubtitleLog.clear();
    if(!subtitleStatus.empty()){
        SetWindowTextW(statusBar,subtitleStatus.c_str());
        veyra::log::info("subtitle-status",narrow(subtitleStatus));
        subtitleStatus.clear();
    }
}
const double submittedFps=s.submissionFps.value_or(0.0);
const bool xessRate=s.applied.multiplier>1&&veyra::engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
veyra::ui::setText(GetDlgItem(hwnd,FpsLabel),s.running&&!s.image&&s.transport==veyra::engine::TransportState::Playing&&!s.metrics.flow.rateWindowReady?std::wstring(L"帧率采样中"):std::format(L"{} {:.1f} fps",xessRate?L"SDK提交":L"显示提交",xessRate?s.metrics.flow.xessSdkSubmitFps:s.metrics.flow.presentSubmitFps));
auto text=s.remotePlay?std::format(L"{}\r\nPS5 接收 {:.1f} / 解码 {:.1f} / 处理 {:.1f} fps | 解码后跳过 {} | 入队丢弃 {}",s.status,s.remoteReceivedFps,s.remoteDecodedFps,s.fps,s.remotePlaySkipped,s.remoteIngressDropped):s.capture?std::format(L"{}\r\n输入 {:.1f} / 已处理 {:.1f} / 显示提交 {:.1f}fps | 回调至Present返回p95 {:.1f}ms（非总延迟）| 丢弃 {} | 有效生成 {}",s.status,s.captureFps,s.fps,submittedFps,s.captureAgeP95Ms,s.captureDropped,s.generated):std::format(L"{}\r\n{:.1f} / {:.1f}秒  已处理 {:.1f}fps  提交迟到 {:+.1f}ms  源帧 {} / 有效生成 {}",s.status,s.position,s.duration,s.fps,s.lateMs,s.frames,s.generated);
if(smokeZoom&&startTick&&s.frames>0){const auto elapsed=GetTickCount64()-startTick;
    if(zoomStep==0&&elapsed>1300){if(uiState.mode==veyra::ui::Mode::Daily)switchMode();zoomBefore=s;RECT r{};GetWindowRect(video,&r);const LPARAM point=MAKELPARAM(r.left+(r.right-r.left)*7/10,r.top+(r.bottom-r.top)/2);if(smokeHover){SetFocus(GetDlgItem(hwnd,ModeSwitch));PostMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA*3),point);hoverPostedTick=GetTickCount64();zoomStep=4;}else{SendMessageW(video,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA*3),point);zoomStep=std::abs(engine.previewView().zoom-1.728f)<.001f?1:-1;}}
    if(zoomStep==4&&GetTickCount64()-hoverPostedTick>150){zoomStep=std::abs(engine.previewView().zoom-1.728f)<.001f?1:-1;veyra::log::info("preview-hover-test",std::format("root-message routed={} focusIsButton={}",zoomStep==1,GetFocus()==GetDlgItem(hwnd,ModeSwitch)));}
    if(zoomStep==1&&elapsed>3000){const bool same=s.sessionId==zoomBefore.sessionId&&s.applied.revision==zoomBefore.applied.revision&&s.metrics.resolution.output==zoomBefore.metrics.resolution.output&&(!s.image||s.nrEvaluated==zoomBefore.nrEvaluated);SendMessageW(video,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(100,100));SendMessageW(video,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(130,110));SendMessageW(video,WM_MBUTTONUP,0,MAKELPARAM(130,110));zoomStep=same?2:-1;}
    if(zoomStep==2&&elapsed>6200){SendMessageW(video,WM_RBUTTONUP,0,0);bool reset=engine.previewView()==veyra::engine::PreviewView{};switchMode();RECT r{};GetWindowRect(video,&r);SendMessageW(video,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(r.left+10,r.top+10));bool daily=engine.previewView()==veyra::engine::PreviewView{};switchMode();zoomStep=reset&&daily?3:-1;veyra::log::info("preview-test",std::format("pass={} unchangedSession={} unchangedRevision={} source={}x{} NR={} presentation-only zoom/pan/reset/daily",zoomStep==3,s.sessionId==zoomBefore.sessionId,s.applied.revision==zoomBefore.applied.revision,s.metrics.resolution.output.width,s.metrics.resolution.output.height,s.nrEvaluated));}
}
if(compareMode||holdOriginal)text+=L"\r\n真实帧对比：两侧同一源帧；此模式不展示生成帧。";
const auto problem=veyra::Logger::instance().latestProblem();if(!problem.empty()){int n=MultiByteToWideChar(CP_UTF8,0,problem.data(),int(problem.size()),nullptr,0);std::wstring detail(n,0);MultiByteToWideChar(CP_UTF8,0,problem.data(),int(problem.size()),detail.data(),n);text+=L"\r\n最近问题："+detail.substr(0,100)+L"（详见诊断）";}
veyra::ui::setText(statusBar,text);
using namespace veyra::ui;
if(masterPendingRevision&&!s.applying){if(s.sessionId==masterPendingSession&&s.rejectedRevision==masterPendingRevision&&s.desired.revision<masterPendingRevision){uiState.enhanced=masterPreviousEnabled;settingsEnabled(uiState.enhanced,uiState.configured);veyra::log::warn("ui-master","transaction rolled back; UI restored to actual enabled state");}masterPendingRevision=0;}
if(smokeScreenshot&&screenshotStep==0&&s.frames>20){SendMessageW(hwnd,WM_COMMAND,Save,0);screenshotStep=1;}
if(screenshotPending){
    std::error_code ec;
    if(s.status.starts_with(L"HDR截图已保存")){auto path=std::filesystem::path(screenshotPath);path.replace_extension(L".jxr");screenshotPath=path.wstring();}
    if((s.status.starts_with(L"图片已保存")||s.status.starts_with(L"HDR截图已保存"))&&std::filesystem::exists(screenshotPath,ec)){
        screenshotPending=false;screenshotTick=GetTickCount64();setText(GetDlgItem(hwnd,Save),L"已保存");
        if(smokeScreenshot){screenshotStep=2;veyra::log::info("screenshot-test","toolbar handler saved processed screenshot");}
    }else if(s.status.find(L"保存失败")!=std::wstring::npos||s.status.find(L"保存异常")!=std::wstring::npos||s.status.find(L"截图尚未支持")!=std::wstring::npos||!s.running){
        screenshotPending=false;screenshotTick=GetTickCount64();setText(GetDlgItem(hwnd,Save),L"保存失败");
        MessageBoxW(hwnd,s.status.c_str(),L"截图未保存",MB_OK|MB_ICONINFORMATION);
    }
}
if(!screenshotPending&&screenshotTick&&GetTickCount64()-screenshotTick>2500){setText(GetDlgItem(hwnd,Save),L"截图");screenshotTick=0;}
EnableWindow(GetDlgItem(hwnd,Save),s.running&&s.frames>0&&!s.failed&&!s.applying&&!screenshotPending);
setText(GetDlgItem(hwnd,Master),masterPendingRevision?L"正在应用…":uiState.enhanced?L"增强已开启":L"增强已关闭");selected(GetDlgItem(hwnd,Master),uiState.enhanced);
CheckDlgButton(hwnd,Sr,(uiState.enhanced&&s.desired.sr)?BST_CHECKED:BST_UNCHECKED);EnableWindow(GetDlgItem(hwnd,Sr),!masterPendingRevision);EnableWindow(GetDlgItem(hwnd,Master),!masterPendingRevision);
veyra::ui::icon(GetDlgItem(hwnd,Play),s.transport==veyra::engine::TransportState::Playing?veyra::ui::Icon::Pause:veyra::ui::Icon::Play);veyra::ui::icon(GetDlgItem(hwnd,Mute),s.muted?veyra::ui::Icon::Muted:veyra::ui::Icon::Volume);
setText(GetDlgItem(hwnd,Play),s.transport==veyra::engine::TransportState::Playing?L"暂停":L"播放");
setText(GetDlgItem(hwnd,Mute),!s.audioAvailable?L"无音轨":s.muted?L"静音":L"音量");EnableWindow(GetDlgItem(hwnd,Volume),s.audioAvailable);EnableWindow(GetDlgItem(hwnd,Mute),s.audioAvailable);
if(GetCapture()!=GetDlgItem(hwnd,Volume))SendDlgItemMessageW(hwnd,Volume,TBM_SETPOS,TRUE,int(std::lround(s.volume*100)));
EnableWindow(seekBar,s.running&&!s.capture&&!s.image&&s.duration>0);ShowWindow(seekBar,(!full||fullControls)&&!s.capture&&!s.image&&!(showDiagnostics&&uiState.mode==veyra::ui::Mode::Professional&&!full)?SW_SHOW:SW_HIDE);EnableWindow(GetDlgItem(hwnd,Play),!currentFile.empty()&&!s.capture&&!s.image);EnableWindow(GetDlgItem(hwnd,Stop),s.running);
setText(GetDlgItem(hwnd,MediaTitle),currentFile.empty()?L"尚未打开媒体":s.remotePlay?L"PS5 · LIVE":s.capture?L"采集卡 · LIVE":std::filesystem::path(currentFile).filename().wstring());
auto stamp=[](double v){int seconds=std::max(0,int(v));return std::format(L"{:02}:{:02}:{:02}",seconds/3600,seconds/60%60,seconds%60);};
setText(GetDlgItem(hwnd,TimeLabel),s.remotePlay?(s.remoteRatesReady?std::format(L"接收 {:.1f} · 解码 {:.1f} fps",s.remoteReceivedFps,s.remoteDecodedFps):std::wstring(L"PS5 帧率采样中")):s.capture?std::format(L"输入 {:.1f} fps{}",s.captureFps,s.captureHalfRate?L" · 60→30":s.applied.content==veyra::engine::ContentRate::Capture60To30?L" · 不适用":L""):s.image?L"静态图片":stamp(s.seekPresented<s.seekRequested&&!s.failed?s.seekTarget:s.position)+L"  /  "+stamp(s.duration)+(s.seekPresented<s.seekRequested&&!s.failed?L"  ·  跳转中…":L"")+(s.transport==veyra::engine::TransportState::Opening?L"  ·  正在打开…":s.failed?L"  ·  发生错误，详见专业诊断":L""));
std::wstring metric=std::format(L"源帧处理 {:.1f} fps   ·   显示提交 {:.1f} fps   ·   有效生成 {}\n提交迟到 p95 {:.1f} ms   ·   GPU / CPU 时间分别统计\n{}\n实际扫描率与光子延迟：未测",s.fps,submittedFps,s.generated,s.lateP95Ms,s.status);setText(metricLabel,metric);
if(uiState.mode==Mode::Professional&&!full&&!transition.running&&GetTickCount64()-dashboardTick>=250){dashboardTick=GetTickCount64();RECT client{};GetClientRect(hwnd,&client);RECT dashboard{0,client.bottom-dip(hwnd,232),client.right,client.bottom};InvalidateRect(hwnd,&dashboard,FALSE);}
exportJob.watching(preferWatching&&s.running&&s.transport==veyra::engine::TransportState::Playing);auto job=exportJob.poll();exportPanelStatus(job,!currentFile.empty()&&!s.capture&&!s.image,s.running&&s.frames>0);setText(GetDlgItem(hwnd,JobProgress),job.message+std::format(L"  {}%  · 查看",int(job.progress*100)));ShowWindow(GetDlgItem(hwnd,JobProgress),job.state==veyra::engine::ExportState::Idle||full?SW_HIDE:SW_SHOW);
setText(GetDlgItem(hwnd,EmptyTitle),s.failed?L"播放已停止":L"开始观看");setText(GetDlgItem(hwnd,EmptyHint),s.failed?s.status:L"打开本地视频，或连接采集卡\n精细调整与原生导出在专业模式中");ShowWindow(GetDlgItem(hwnd,EmptyTitle),SW_HIDE);ShowWindow(GetDlgItem(hwnd,EmptyHint),SW_HIDE);
if(dragging&&GetCapture()!=seekBar){dragging=false;seekPreview.released=true;}
seekPreview.tick();
if(!dragging&&s.duration>0){int position=int((s.seekPresented<s.seekRequested&&!s.failed?s.seekTarget:s.position)/s.duration*10000);if(SendMessageW(seekBar,TBM_GETPOS,0,0)!=position)SendMessageW(seekBar,TBM_SETPOS,TRUE,position);}
if(!smokeViewApplied&&!smokeView.empty()&&(s.frames>0||smokeEmpty)){smokeViewApplied=true;if(smokeView==L"small")SetWindowPos(hwnd,nullptr,0,0,800,600,SWP_NOMOVE|SWP_NOZORDER);if(smokeView!=L"daily"&&smokeView!=L"empty"&&uiState.mode==veyra::ui::Mode::Daily)switchMode();if(smokeView==L"export")selectInspector(3);if(smokeView==L"diagnostics"){showDiagnostics=true;layout();}if(smokeView==L"fullscreen")toggleFullscreen();}
if(smokeDualPause&&pausedPosition<0&&s.frames>20){engine.pause(true);pausedPosition=s.position;pausedFrames=s.frames;}
if(smokeDual&&startTick&&s.frames>10&&dualStep<40&&GetTickCount64()-startTick>ULONGLONG(1500+dualStep*200)){if(dualStep==4){modeGdiStart=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);GetProcessHandleCount(GetCurrentProcess(),&modeHandlesStart);PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof(pm);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),sizeof(pm));modePrivateStart=pm.PrivateUsage;}switchMode();
if(smokeDualPause&&pausedPosition>=0&&s.transport==veyra::engine::TransportState::Paused){auto after=engine.snapshot();if(after.position!=s.position||after.frames!=s.frames)dualStep=-100;}++dualStep;if(dualStep==40){DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof(pm);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),sizeof(pm));veyra::log::info("ui-resources",std::format("gdiStart={} gdiEnd={} handlesStart={} handlesEnd={} privateStart={} privateEnd={}",modeGdiStart,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),modeHandlesStart,handles,modePrivateStart,pm.PrivateUsage));}}
if((smokeDual||smokeJob||smokeJobCancel||smokeJobExit)&&!smokeDualOutput.empty()&&s.frames>20){jobExpected=s.applied;jobExpected.nrPolicy=veyra::pipeline::NrSizePolicy::Native;exportJob.start(currentFile,smokeDualOutput,s.applied,false,(smokeJob||smokeJobCancel||smokeJobExit)?120:24);smokeDualOutput.clear();jobStep=1;jobPosition=s.position;}
if((smokeJobCancel||smokeJobExit)&&jobStep==1&&job.state==veyra::engine::ExportState::Running&&job.sourceFrames>=10){if(smokeJobExit){veyra::log::info("ui-job-test","parent close with active worker; cooperative cancellation requested");resultCode=0;PostMessageW(hwnd,WM_CLOSE,0,0);}else exportJob.cancel();jobStep=2;}
if(smokeJobCancel&&jobStep==2&&job.state==veyra::engine::ExportState::Cancelled){jobStep=4;veyra::log::info("ui-job-test",std::format("cancelled=true foregroundContinues={} source={} encoded={}",s.running,job.sourceFrames,job.encoded));}
if(smokeJob&&jobStep==1&&job.state==veyra::engine::ExportState::Running){exportJob.pause(true);jobStep=2;jobPauseTick=GetTickCount64();auto changed=s.desired;changed.model.intensity=.65f;engine.requestSettings(changed);}
if(smokeJob&&jobStep==2&&job.state==veyra::engine::ExportState::Paused&&GetTickCount64()-jobPauseTick>650){veyra::log::info("ui-job-test",std::format("paused=true foregroundAdvanced={} position={} startPosition={} session={}",s.position>jobPosition,s.position,jobPosition,s.sessionId));exportJob.pause(false);jobStep=s.position>jobPosition?3:-1;}
if(smokeJob&&jobStep==3&&job.state==veyra::engine::ExportState::Succeeded){const bool frozenSame=job.frozen==jobExpected&&job.frozenRevision==jobExpected.revision&&s.applied.model.intensity!=jobExpected.model.intensity;jobStep=frozenSame?4:-1;veyra::log::info("ui-job-test",std::format("success=true foregroundFrames={} changedAppliedIntensity={} jobFrozenRevisionIndependent={} frozenRevision={} frozenIntensity={} source={} generated={} holds={} encoded={}",s.frames,s.applied.model.intensity,frozenSame,jobExpected.revision,jobExpected.model.intensity,job.sourceFrames,job.generated,job.holds,job.encoded));}
if(smokeMaster&&startTick){if(masterStep==0&&s.frames>20){SendMessageW(hwnd,WM_COMMAND,Master,0);masterStep=1;}else if(smokeMasterReject&&masterStep==1&&!s.applying&&!masterPendingRevision&&s.rejectedRevision&&uiState.enhanced&&s.applied.nr){masterStep=3;veyra::log::info("ui-master-test","rejected bypass restored actual enabled UI");}else if(masterStep==1&&!s.applying&&!s.applied.nr&&!s.applied.sr&&s.applied.multiplier==1){SendMessageW(hwnd,WM_COMMAND,Master,0);masterStep=2;}else if(masterStep==2&&!s.applying&&s.applied.nr==uiState.configured.nr&&s.applied.model==uiState.configured.model&&s.applied.residual==uiState.configured.residual){masterStep=3;veyra::log::info("ui-master-test","full bypass applied and all configured parameters restored");}}
if(smokeAudio&&startTick&&s.audioAvailable){auto elapsed=GetTickCount64()-startTick;if(audioStep==0&&elapsed>1500){engine.setVolume(.5f,false);audioStep=1;}else if(audioStep==1&&elapsed>2500){engine.setVolume(0,false);audioStep=2;}else if(audioStep==2&&elapsed>3500){engine.setVolume(.8f,true);audioStep=3;}else if(audioStep==3&&elapsed>4500){engine.setVolume(1,false);audioStep=4;}}
if(smokeRollback&&startTick){if(settingsStep==0&&s.frames>20){auto changed=s.desired;if(smokeRollbackFlow)changed.nr=false;else{changed.model.style=2;changed.model.intensity=.35f;}engine.requestSettings(changed);settingsStep=1;}if(settingsStep==1&&s.frames>50&&s.desired.model.style==0&&s.applied.model.style==0&&!s.applying&&(!smokeRollbackFlow||(s.applied.nr&&s.nvofExecuted>30))){settingsStep=3;veyra::log::info("rollback-test",std::format("whole snapshot restored revision={} position={} style={} intensity={}",s.applied.revision,s.position,s.applied.model.style,s.applied.model.intensity));}}
if(smokeUi&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(uiStep==0&&elapsed>2300){SendMessageW(hwnd,WM_KEYDOWN,VK_F11,0);uiStep=1;}
if(uiStep==1&&elapsed>2800){const bool entered=full&&(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_POPUP);SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);veyra::log::info("ui-test",std::format("F11 entered={} Esc restored={}",entered,!full));uiStep=entered&&!full?2:-1;}
if(uiStep==2&&elapsed>3300){SendMessageW(hwnd,WM_SYSKEYDOWN,VK_RETURN,1LL<<29);uiStep=3;}
if(uiStep==3&&elapsed>3800){const bool entered=full;SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(VideoSurface,STN_DBLCLK),LPARAM(video));veyra::log::info("ui-test",std::format("AltEnter entered={} doubleClick restored={}",entered,!full));uiStep=entered&&!full?4:-1;}
if(uiStep==4&&elapsed>4200){compareMode=2;compareSplit=.4f;updateComparison();uiStep=5;}
if(uiStep==5&&elapsed>4800){referenceBase=true;updateComparison();uiStep=6;}
if(uiStep==6&&elapsed>5500){holdOriginal=true;updateComparison();uiStep=7;}
if(uiStep==7&&elapsed>5900){holdOriginal=false;compareMode=0;updateComparison();veyra::log::info("ui-test",std::format("completed={} NR evaluations={} (no fullscreen NR re-create expected)",true,s.nrEvaluated));uiStep=8;}
}
// Colour page acceptance (T3): master switch, slider->engine wiring, one-click
// reset with a single undo, accordion folding and its persistence.
if(smokeColor&&startTick){const auto elapsed=GetTickCount64()-startTick;
const auto colourSnapshot=engine.snapshot();
auto colourControl=[](int id){return veyra::ui::settingsControlForTest(id);};
// Progress heartbeat: a stalled UI tick shows up as a gap in these lines.
{static uint64_t colourHeartbeat=0;if(elapsed/2000!=colourHeartbeat){colourHeartbeat=elapsed/2000;veyra::log::info("color-ui-test",std::format("tick elapsed={} step={} exposure={:.3f}",elapsed,colorStep,colourSnapshot.desired.color.exposure));}}
if(colorStep==0&&elapsed>1500){
    if(uiState.mode==veyra::ui::Mode::Daily)switchMode();
    selectInspector(2);layout();
    const auto master=colourControl(800);
    const bool off=master&&SendMessageW(master,BM_GETCHECK,0,0)==BST_UNCHECKED;
    colorStep=off?1:-1;
    veyra::log::info("color-ui-test",std::format("page2 masterPresent={} defaultOff={} step={}",master?1:0,off,colorStep));
}else if(colorStep==1&&colourSnapshot.desired.color.enabled){colorStep=2;veyra::log::info("color-ui-test","master switch applied");}
else if(colorStep==1){if(elapsed>2500)SendMessageW(colourControl(800),BM_CLICK,0,0);}
else if(colorStep==2){
    if(auto edit=colourControl(1300))SetWindowTextW(edit,L"1.00");
    // The graph must actually receive it: `desired` alone is what let the
    // "sliders do nothing until NR is toggled" bug ship.
    colorStep=(colourSnapshot.desired.color.exposure==1.0f&&colourSnapshot.applied.color.exposure==1.0f)?9:(elapsed>5000?-1:2);
    if(colorStep==9)veyra::log::info("color-ui-test",std::format("exposure 1.00 reached the engine and the running graph (applied, revision={})",colourSnapshot.applied.revision));
}else if(colorStep==9){
    // Second edit: same revision, so it must go through the live uniform path -
    // no rebuild, no history reset, and the graph must show it.
    colourLiveRevision=colourSnapshot.applied.revision;
    if(auto edit=colourControl(1300))SetWindowTextW(edit,L"0.75");
    colorStep=10;
}else if(colorStep==10){
    const bool live=colourSnapshot.applied.color.exposure==0.75f&&colourSnapshot.applied.revision==colourLiveRevision&&colourSnapshot.desired.color.exposure==0.75f;
    colorStep=live?11:(elapsed>9000?-1:10);
    if(colorStep==11)veyra::log::info("color-ui-test",std::format("live parameter update reached the graph without a rebuild (revision stays {})",colourLiveRevision));
}else if(colorStep==11){
    // Put the value back so the reset/undo expectations below stay meaningful.
    if(auto edit=colourControl(1300))SetWindowTextW(edit,L"1.00");
    colorStep=(colourSnapshot.desired.color.exposure==1.0f&&colourSnapshot.applied.color.exposure==1.0f)?12:(elapsed>11000?-1:11);
}
// Paused adjustments must reach the picture too: pause, edit, and require the
// engine to re-render the cached frame (counter + log), then resume.
else if(colorStep==12){
    colourPausedBase=colourSnapshot.pausedFrameRefreshes;
    engine.pause(true);
    if(auto edit=colourControl(1300))SetWindowTextW(edit,L"0.50");
    colorStep=13;
}
else if(colorStep==13){
    const bool refreshed=colourSnapshot.applied.color.exposure==0.50f&&colourSnapshot.pausedFrameRefreshes>colourPausedBase;
    colorStep=refreshed?14:(elapsed>13000?-1:13);
    if(colorStep==14)veyra::log::info("color-ui-test",std::format("paused adjustment re-rendered the frame (refreshes={})",colourSnapshot.pausedFrameRefreshes));
}
else if(colorStep==14){
    engine.pause(false);
    if(auto edit=colourControl(1300))SetWindowTextW(edit,L"1.00");
    colorStep=3;
}else if(colorStep==3){SendMessageW(colourControl(801),BM_CLICK,0,0);colorStep=30;}
// The snapshot is taken once per tick, so the reset/undo results are observed on
// the following tick instead of in the same one that clicked the button.
else if(colorStep==30){colorStep=(colourSnapshot.desired.color.exposure==0.0f)?31:(elapsed>6000?-1:30);
    if(colorStep==31)veyra::log::info("color-ui-test","one-click reset returned the grade to neutral");}
else if(colorStep==31){SendMessageW(colourControl(802),BM_CLICK,0,0);colorStep=40;}
else if(colorStep==40){colorStep=(colourSnapshot.desired.color.exposure==1.0f)?5:(elapsed>8000?-1:40);
    if(colorStep==5)veyra::log::info("color-ui-test","undo restored the pre-reset values");}
// T4 rows: the colour-grading "blending" slider belongs to a section that was
// added after the first framework cut, so prove the composed rows reach the
// engine too.
else if(colorStep==5){const int editId=veyra::ui::colourParamEditId(L"混合");if(auto edit=colourControl(editId))SetWindowTextW(edit,L"77.00");colorStep=editId>0?50:-1;}
else if(colorStep==50){colorStep=(colourSnapshot.desired.color.gradingBlending==77.0f)?51:(elapsed>9500?-1:50);
    if(colorStep==51)veyra::log::info("color-ui-test","T4 the colour grading row reached the engine");}
// Black & white mixer (T4): the switch must reach the engine and the per-band
// row must then change the monochrome result instead of being an inert slider.
else if(colorStep==51){
    // The mixer's 黑白 correction is the black & white switch now; the green
    // range is selected so the row under test is the one on screen.
    veyra::ui::settingsColorMixerModeForTest(3);
    veyra::ui::settingsColorBandForTest(3);
    veyra::ui::settingsColorScrollToTest(veyra::ui::colourBandsControlId());
    veyra::log::info("color-ui-test","mixer set to 黑白 correction with the green range selected");
    colorStep=45;
}
else if(colorStep==45&&colourSnapshot.desired.color.blackWhite){colorStep=46;
    veyra::log::info("color-ui-test","T4 the black and white mixer switch reached the engine");}
else if(colorStep==45&&elapsed>11500){colorStep=-1;}
else if(colorStep==46){
    const int editId=veyra::ui::colourParamEditId(L"绿色 · 黑白");
    if(auto edit=colourControl(editId))SetWindowTextW(edit,L"60.00");
    colorStep=editId>0?47:-1;
}
else if(colorStep==47){
    colorStep=(colourSnapshot.desired.color.blackWhiteMix[3]==60.0f)?52:(elapsed>12500?-1:47);
    if(colorStep==52)veyra::log::info("color-ui-test","T4 the black and white band row reached the engine");}
// Named colour presets: save the current look, clear a value, apply the preset,
// then delete it - the same three actions the panel exposes.
else if(colorStep==52){
    veyra::ui::settingsColorScrollToTest(805);
    bool hit=true;
    for(int id:{805,806,807,808,809}){
        const auto control=colourControl(id);RECT rect{};GetWindowRect(control,&rect);
        POINT point{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};ScreenToClient(GetParent(control),&point);
        hit&=ChildWindowFromPointEx(GetParent(control),point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED)==control;
    }
    veyra::log::info("color-ui-test",std::format("preset toolbar buttons reachable={}",hit));
    if(!hit){colorStep=-1;return 0;}
    if(auto edit=colourControl(804))SetWindowTextW(edit,L"烟测预设");
    SendMessageW(colourControl(805),BM_CLICK,0,0);
    colorStep=53;
}
else if(colorStep==53){
    veyra::engine::ColorLookStore store(veyra::runtime::localDataDirectory());
    store.load();
    const bool saved=!store.entries().empty()&&store.entries().back().name==L"烟测预设";
    if(auto edit=colourControl(1300))SetWindowTextW(edit,L"0.00");
    colorStep=saved?54:-1;
    veyra::log::info("color-ui-test",std::format("preset saved={} count={} step={}",saved,store.entries().size(),colorStep));
}
else if(colorStep==54&&colourSnapshot.desired.color.exposure==0.0f){SendMessageW(colourControl(806),BM_CLICK,0,0);colorStep=55;}
else if(colorStep==54&&elapsed>9000){
    veyra::log::info("color-ui-test",std::format("preset apply waiting: desiredExposure={:.3f} configuredExposure={:.3f} elapsed={}",
        colourSnapshot.desired.color.exposure,uiState.configured.color.exposure,elapsed));
    colorStep=-1;
}
else if(colorStep==55){colorStep=(colourSnapshot.desired.color.exposure==1.0f)?56:(elapsed>14000?-1:55);
    if(colorStep==56)veyra::log::info("color-ui-test","preset applied: exposure came back through the engine");}
else if(colorStep==56){SendMessageW(colourControl(807),BM_CLICK,0,0);colorStep=57;}
else if(colorStep==57){
    veyra::engine::ColorLookStore store(veyra::runtime::localDataDirectory());
    store.load();
    const bool deleted=store.entries().empty();
    colorStep=deleted?58:(elapsed>16000?-1:57);
    if(colorStep==58)veyra::log::info("color-ui-test","preset deleted; colour page back to no stored looks");
}
else if(colorStep==58){
    colorStep=59;
}
// Colour grading wheels (professional layout): dragging inside the disc must set
// hue+saturation, the luminance bar must set luminance, and a double-click must
// put the zone back to neutral.
else if(colorStep==59){
    POINT point{};
    const auto wheel=colourControl(veyra::ui::colourWheelControlId(0));
    veyra::ui::settingsColorScrollToTest(veyra::ui::colourWheelControlId(0));
    if(wheel&&veyra::ui::settingsColorWheelTestPoint(0,120.0f,80.0f,point)){
        SendMessageW(wheel,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        SendMessageW(wheel,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        colorStep=60;
    }else{
        veyra::log::info("color-ui-test","colour wheel control missing");
        colorStep=-1;
    }
}
else if(colorStep==60){
    const auto& grading=colourSnapshot.desired.color.grading[0];
    const bool dialed=std::abs(grading.hue-120.0f)<6.0f&&std::abs(grading.saturation-80.0f)<8.0f;
    veyra::log::info("color-ui-test",std::format("colour wheel drag hue={:.1f} saturation={:.1f} pass={}",grading.hue,grading.saturation,dialed));
    colorStep=dialed?61:-1;
}
else if(colorStep==61){
    POINT point{};
    const auto wheel=colourControl(veyra::ui::colourWheelControlId(0));
    if(wheel&&veyra::ui::settingsColorWheelTestBarPoint(0,-50.0f,point)){
        SendMessageW(wheel,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        SendMessageW(wheel,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
    }
    colorStep=62;
}
else if(colorStep==62){
    const auto& grading=colourSnapshot.desired.color.grading[0];
    const bool shifted=std::abs(grading.luminance+50.0f)<8.0f;
    veyra::log::info("color-ui-test",std::format("colour wheel luminance bar={:.1f} pass={}",grading.luminance,shifted));
    colorStep=shifted?63:-1;
}
else if(colorStep==63){
    const auto wheel=colourControl(veyra::ui::colourWheelControlId(0));
    if(wheel)SendMessageW(wheel,WM_LBUTTONDBLCLK,0,MAKELPARAM(6,6));
    colorStep=64;
}
else if(colorStep==64){
    const auto& grading=colourSnapshot.desired.color.grading[0];
    const bool reset=grading.hue==0.0f&&grading.saturation==0.0f&&grading.luminance==0.0f;
    veyra::log::info("color-ui-test",std::format("colour wheel double-click reset pass={}",reset));
    colorStep=reset?66:-1;
}
// Tone curve (professional layout): pick the red channel, click the grid to add a
// control point, then flatten the channel again.
else if(colorStep==66){
    veyra::ui::settingsColorSectionForTest(2,true);
    if(auto tab=colourControl(852))SendMessageW(tab,BM_CLICK,0,0);
    veyra::ui::settingsColorScrollToTest(veyra::ui::colourCurveCanvasControlId());
    POINT point{};
    const auto canvasControl=colourControl(veyra::ui::colourCurveCanvasControlId());
    if(canvasControl&&veyra::ui::settingsCurveTestPoint(0.5f,0.75f,point)){
        SendMessageW(canvasControl,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        SendMessageW(canvasControl,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        colorStep=67;
    }else{
        veyra::log::info("color-ui-test","curve canvas missing");
        colorStep=-1;
    }
}
else if(colorStep==67){
    const auto& curve=colourSnapshot.desired.color.curves[1];
    const bool added=curve.count==3&&std::abs(curve.points[1].x-0.5f)<0.05f&&std::abs(curve.points[1].y-0.75f)<0.05f;
    veyra::log::info("color-ui-test",std::format("curve point added count={} x={:.2f} y={:.2f} pass={}",curve.count,curve.points[1].x,curve.points[1].y,added));
    if(auto flatten=colourControl(855))SendMessageW(flatten,BM_CLICK,0,0);
    colorStep=added?68:-1;
}
else if(colorStep==68){
    const auto& curve=colourSnapshot.desired.color.curves[1];
    const bool flattened=curve.identity();
    veyra::log::info("color-ui-test",std::format("curve flatten restored the identity ramp pass={} count={}",flattened,curve.count));
    colorStep=flattened?69:-1;
}
// Hold the curve view steady for a moment: acceptance screenshots and a human
// can actually see the editor before the fold checks scroll the page.
else if(colorStep==69&&elapsed>9000){colorStep=70;}
// Section bypass eyes (plan T3): one click stops a group from affecting the
// picture while its numbers stay in the panel.
else if(colorStep==70){
    veyra::ui::settingsColorScrollToTest(veyra::ui::colourSectionEyeControlId(0));
    if(auto eye=colourControl(veyra::ui::colourSectionEyeControlId(0)))SendMessageW(eye,WM_LBUTTONDOWN,0,MAKELPARAM(4,4));
    colorStep=71;
}
else if(colorStep==71){
    const bool bypassed=(colourSnapshot.desired.color.groupBypassMask&1u)!=0u;
    veyra::log::info("color-ui-test",std::format("section eye bypass pass={} mask={}",bypassed,colourSnapshot.desired.color.groupBypassMask));
    if(auto eye=colourControl(veyra::ui::colourSectionEyeControlId(0)))SendMessageW(eye,WM_LBUTTONDOWN,0,MAKELPARAM(4,4));
    colorStep=bypassed?72:-1;
}
else if(colorStep==72){
    const bool restored=(colourSnapshot.desired.color.groupBypassMask&1u)==0u;
    veyra::log::info("color-ui-test",std::format("section eye restore pass={} mask={}",restored,colourSnapshot.desired.color.groupBypassMask));
    colorStep=restored?73:-1;
}
else if(colorStep==73){
    auto edit=colourControl(1300);
    const bool beforeVisible=edit&&IsWindowVisible(edit);
    const bool beforeMask=(preferences.load().colourFoldMask&1u)!=0u;
    // The section header is sticky now, so the geometry probe follows a real row
    // of the next section instead (its first parameter label).
    RECT before{};GetWindowRect(colourControl(1206),&before);
    SendMessageW(colourControl(810),BM_CLICK,0,0);
    RECT after{};GetWindowRect(colourControl(1206),&after);
    const bool afterVisible=edit&&IsWindowVisible(edit);
    const auto persistedPreferences=preferences.load();
    const bool afterMask=(persistedPreferences.colourFoldMask&1u)!=0u;
    const bool collapsed=afterMask;
    // Which section it starts in depends on what the last run persisted, so the
    // check is "the state flipped and the geometry followed the flip". Window
    // visibility is not asserted: the acceptance gate runs the app hidden, where
    // IsWindowVisible is false for every child.
    const bool consistent=(afterMask!=beforeMask);
    const bool geometryOk=collapsed?(after.top<before.top):(after.top>before.top);
    colorStep=(consistent&&geometryOk)?6:-1;
    veyra::log::info("color-ui-test",std::format("fold collapsed={} persisted={} mask={} visibleBefore={} visibleAfter={} geometryOk={} step={}",collapsed,consistent,persistedPreferences.colourFoldMask,beforeVisible,afterVisible,geometryOk,colorStep));
}else if(colorStep==6){SendMessageW(colourControl(810),BM_CLICK,0,0);SendMessageW(colourControl(800),BM_CLICK,0,0);colorStep=7;}
else if(colorStep==7&&!colourSnapshot.desired.color.enabled){colorStep=8;veyra::log::info("color-ui-test","unfolded and master off; colour chain back to the zero-cost default");}
else if(colorStep==7&&elapsed>9000){colorStep=-1;}
}
if(smokeSettings&&startTick){const auto elapsed=GetTickCount64()-startTick;
if(settingsStep==0&&s.frames>20){auto change=s.desired;change.model.intensity=.6f;change.model.tone=.7f;change.residual.total=.4f;change.residual.color=.8f;engine.requestSettings(change);settingsStep=1;}
if(settingsStep==1&&s.applied.model.intensity==.6f&&elapsed>3300){auto change=s.desired;change.sr=true;engine.requestSettings(change);settingsStep=2;}
if(settingsStep==2&&s.applied.sr&&elapsed>4500){auto invalid=s.desired;invalid.model.intensity=2;const bool rejected=!engine.requestSettings(invalid);veyra::log::info("settings-test",std::format("invalid-rejected={} position={} revision={} settingsStep={}",rejected,s.position,s.applied.revision,settingsStep));settingsStep=rejected?3:-1;}
// NR exclusion feather (id 222) must reach the engine through the panel's own
// live-edit path, not just the shader: this is the control users complained was
// missing while the hard 2 px default made the zones unusable.
if(settingsStep==3&&elapsed>6000){SetWindowTextW(veyra::ui::settingsControlForTest(222),L"48");settingsStep=4;}
if(settingsStep==4&&uiState.configured.protection.featherPixels==48){veyra::log::info("settings-test",std::format("exclusion-feather draft={} desired={} applied={} position={}",uiState.configured.protection.featherPixels,s.desired.protection.featherPixels,s.applied.protection.featherPixels,s.position));settingsStep=5;}
}
tickRepairChecks(hwnd,s);
tickTransportChecks(hwnd,s);
tickFgOnlyChecks(hwnd,s);
if(smokeProtection&&startTick&&GetTickCount64()-startTick>1500){
    auto indicator=[&](bool enabled,const wchar_t* count){wchar_t text[128]{},draft[64]{};auto control=veyra::ui::settingsControlForTest(206);GetWindowTextW(control,text,128);GetWindowTextW(veyra::ui::settingsControlForTest(100),draft,64);return (SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED)==enabled&&std::wstring(text).find(count)!=std::wstring::npos&&std::wstring(draft)==L"0.42";};
    if(protectionStep==0&&s.frames){if(uiState.mode==veyra::ui::Mode::Daily)switchMode();if(!transition.running){
        SetWindowTextW(veyra::ui::settingsControlForTest(100),L"0.42");protectionSession=s.sessionId;auto view=engine.previewView();view.zoom=2;engine.previewView(view);RECT r{};GetClientRect(video,&r);auto e=s.metrics.resolution.output;
        const float scale=std::min(float(r.right)/e.width,float(r.bottom)/e.height)*view.zoom;
        auto pixel=[&](float u,float v){return POINT{LONG(r.right*.5f+(u-view.centerX)*e.width*scale),LONG(r.bottom*.5f+(v-view.centerY)*e.height*scale)};};
        auto a=pixel(.4f,.4f),b=pixel(.6f,.6f);SendMessageW(veyra::ui::settingsControlForTest(213),BM_CLICK,0,0);SendMessageW(video,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.x,a.y));SendMessageW(video,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(b.x,b.y));
        protectionOverlayShown=protectionOverlay&&IsWindowVisible(protectionOverlay);SendMessageW(video,WM_LBUTTONUP,0,MAKELPARAM(b.x,b.y));protectionStep=1;
    }}else if(protectionStep==1&&s.applied.protection.enabled&&indicator(true,L"1/4")){auto q=s.applied.protection.regions[0];bool pass=s.sessionId==protectionSession&&std::abs(q.left-.4f)<.01f&&std::abs(q.top-.4f)<.01f&&std::abs(q.right-.6f)<.01f&&std::abs(q.bottom-.6f)<.01f&&protectionOverlayShown;
        veyra::log::info("protection-ui-test",std::format("zoom2 rectangle={} overlay={} unchangedSession={} bounds={},{},{},{}",pass,protectionOverlayShown,s.sessionId==protectionSession,q.left,q.top,q.right,q.bottom));
        SendMessageW(veyra::ui::settingsControlForTest(214),BM_CLICK,0,0);protectionStep=pass?2:-1;
    }else if(protectionStep==2&&!s.applied.protection.enabled&&indicator(false,L"0/4")){SendMessageW(veyra::ui::settingsControlForTest(213),BM_CLICK,0,0);SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);bool pass=!protectionArmed&&!protectionDragging;protectionStep=pass?3:-1;veyra::log::info("protection-ui-test",std::format("clear/cancel dirty-draft-preserved pass={}",pass));if(pass)SendMessageW(hwnd,WM_COMMAND,Master,0);}
    else if(protectionStep==3&&!uiState.enhanced&&!masterPendingRevision){auto c=uiState.configured;const auto revision=c.revision;c.protection.enabled=true;c.protection.regions[0]={.1f,.2f,.8f,.9f};bool pass=applySettings(c)&&indicator(true,L"1/4");c.protection={};pass=applySettings(c)&&indicator(false,L"0/4")&&c.revision==revision&&pass;
        protectionStep=pass?4:-1;veyra::log::info("protection-ui-test",std::format("master-off unchanged-revision dirty-draft-preserved pass={}",pass));SendMessageW(hwnd,WM_COMMAND,Master,0);}

}
if(smokeSeconds>0&&startTick&&GetTickCount64()-startTick>ULONGLONG(smokeSeconds)*1000){veyra::log::info("app",std::format("smoke frames={} generated={} failed={} latenessMs={:.2f} absLatenessP95Ms={:.2f} controlsStep={} capture={} processedFps={:.2f} callbackFps={:.2f} captureDropped={} callbackToPresentReturnP95Ms={:.3f} schedulingWaitP95Ms={:.3f} processCpuP95Ms={:.3f} presentCpuP95Ms={:.3f} nrEvaluated={} nvofExecuted={}",s.frames,s.generated,s.failed,s.lateMs,s.lateP95Ms,smokeStep,s.capture,s.fps,s.captureFps,s.captureDropped,s.captureAgeP95Ms,s.schedulingWaitP95Ms,s.processCpuP95Ms,s.presentCpuP95Ms,s.nrEvaluated,s.nvofExecuted));resultCode=(s.frames>0||smokeEmpty)&&!s.failed&&(!smokeZoom||zoomStep==3)&&(!smokeProtection||protectionStep==4)&&(!smokeRepair||repairStep==10)&&(!smokeTransport||transportStep==10)&&(!smokeFgOnly||fgOnlyStep==4)&&(!smokeSettings||settingsStep==5)&&(!smokeColor||colorStep==8)&&(!smokeRollback||settingsStep==3)&&(!smokeUi||uiStep==8)&&(!smokeDual||dualStep==40)&&(!smokeMaster||masterStep==3)&&(!smokeAudio||audioStep==4)&&(!(smokeJob||smokeJobCancel)||jobStep==4)&&(!smokeScreenshot||screenshotStep==2)?0:1;PostMessageW(hwnd,WM_CLOSE,0,0);}return 0;}
}return DefWindowProcW(hwnd,msg,wp,lp);
}

__declspec(noinline) LRESULT closeWindow(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_CLOSE:endTransition();if(!closing&&smokeSeconds<=0&&exportJob.poll().active()&&MessageBoxW(hwnd,L"导出尚未完成。取消导出并退出？\n选择“否”返回播放器继续导出。",L"退出 Veyra",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return 0;if(!closing&&smokeSeconds<=0){auto snapshot=engine.snapshot();WINDOWPLACEMENT placement{sizeof(placement)};if(full)placement=windowPlacement;else GetWindowPlacement(hwnd,&placement);auto r=placement.rcNormalPosition;uiPreferences.width=MulDiv(r.right-r.left,96,veyra::ui::layoutDpi(hwnd));uiPreferences.height=MulDiv(r.bottom-r.top,96,veyra::ui::layoutDpi(hwnd));uiPreferences.x=r.left;uiPreferences.y=r.top;uiPreferences.positioned=true;uiPreferences.volume=snapshot.volume;uiPreferences.muted=snapshot.muted;uiPreferences.subtitles=uiState.subtitles;uiPreferences.subtitleSize=subtitlePixels;uiPreferences.subtitleOutline=uiState.subtitleOutline;uiPreferences.subtitleBackground=uiState.subtitleBackground;uiPreferences.subtitleSecondLanguage=uiState.subtitleSecondLanguage;uiPreferences.subtitleMargin=uiState.subtitleMargin;uiPreferences.subtitleFont=uiState.subtitleFont;uiPreferences.inspector=uiState.inspector;uiPreferences.colourFoldMask=preferences.load().colourFoldMask;uiPreferences.enhancementEnabled=uiState.enhanced?1:0;auto saved=uiState.enhanced&&snapshot.running&&snapshot.frames>0&&!snapshot.applying&&!snapshot.failed?snapshot.applied:uiState.configured;if(!preferences.save(uiPreferences,&saved))veyra::log::warn("ui-preferences","preferences save failed; existing corrupt original preserved");else veyra::log::info("ui-preferences","UI and enhancement preferences saved");}playbackPower.update(false);exportJob.cancel();closing=true;engine.stop();SetWindowTextW(statusBar,L"正在释放当前任务资源…");return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);
}

bool dpiRefreshPending=false;
__declspec(noinline) LRESULT refreshWindowDpi(HWND hwnd){
    if(!dpiRefreshPending)return 0;
    auto oldFont=font;font=veyra::ui::makeFont(hwnd);
    EnumChildWindows(hwnd,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,WPARAM(f),FALSE);return TRUE;},LPARAM(font));
    DeleteObject(oldFont);DeleteObject(emptyFont);emptyFont=veyra::ui::makeFont(hwnd,26);
    SendDlgItemMessageW(hwnd,EmptyTitle,WM_SETFONT,WPARAM(emptyFont),FALSE);
    veyra::ui::settingsDpi();veyra::ui::telemetryDpi();layout();
    dpiRefreshPending=false;
    RemovePropW(video,L"Veyra.DpiTransition");
    veyra::log::info("ui-display","DPI transition complete");return 0;
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_APP+91:return refreshWindowDpi(hwnd);
case WM_SYSCOMMAND:return systemCommand(hwnd,msg,wp,lp);
case WM_CREATE:return createWindow(hwnd,msg,wp,lp);
case WM_NCCALCSIZE:if(wp)return 0;break;
case WM_NCACTIVATE:return DefWindowProcW(hwnd,msg,wp,-1);
case WM_NCPAINT:return 0;
case WM_ERASEBKGND:return 1;
case WM_NCHITTEST:{LRESULT hit=DefWindowProcW(hwnd,msg,wp,lp);if(hit==HTCLIENT&&!full){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);RECT r{};GetClientRect(hwnd,&r);int edge=veyra::ui::dip(hwnd,7);bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;if((uiState.mode==veyra::ui::Mode::Professional&&p.y<veyra::ui::dip(hwnd,54))||(uiState.mode==veyra::ui::Mode::Daily&&p.y>r.bottom-veyra::ui::dip(hwnd,88)))return HTCAPTION;}return hit;}
case WM_PAINT:return paintWindow(hwnd,msg,wp,lp);
case WM_LBUTTONDOWN:{if(uiState.mode==veyra::ui::Mode::Professional&&!full){RECT r{};GetClientRect(hwnd,&r);int w=MulDiv(r.right,96,veyra::ui::layoutDpi(hwnd)),x=MulDiv(GET_X_LPARAM(lp),96,veyra::ui::layoutDpi(hwnd));if(w>=1180&&abs(x-(w-uiPreferences.inspectorWidth-27))<8){inspectorResizing=true;proposedInspectorWidth=uiPreferences.inspectorWidth;SetCapture(hwnd);return 0;}}break;}
case WM_MOUSEMOVE:if(inspectorResizing){RECT r{};GetClientRect(hwnd,&r);proposedInspectorWidth=std::clamp(MulDiv(r.right-GET_X_LPARAM(lp),96,veyra::ui::layoutDpi(hwnd))-27,296,420);InvalidateRect(hwnd,nullptr,FALSE);return 0;}break;
case WM_LBUTTONUP:if(inspectorResizing){inspectorResizing=false;ReleaseCapture();uiPreferences.inspectorWidth=proposedInspectorWidth;layout();return 0;}break;
case WM_CAPTURECHANGED:if(inspectorResizing){inspectorResizing=false;InvalidateRect(hwnd,nullptr,FALSE);}break;
case WM_NOTIFY:{auto header=reinterpret_cast<NMHDR*>(lp);if(header->code==TTN_GETDISPINFOW){auto info=reinterpret_cast<NMTTDISPINFOW*>(lp);HWND child=reinterpret_cast<HWND>(header->idFrom);if(auto tip=GetPropW(child,L"veyra.tip"))info->lpszText=reinterpret_cast<wchar_t*>(tip);else{static wchar_t value[512];GetWindowTextW(child,value,512);info->lpszText=value;}return 0;}break;}
case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={veyra::ui::dip(hwnd,720),veyra::ui::dip(hwnd,540)};return 0;}
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:return veyra::ui::colors(msg,wp,lp);
case WM_APP+46:return savePresentation(hwnd,msg,wp,lp);
case WM_APP+45:return protectionCommand(hwnd,msg,wp,lp);
case WM_APP+44:return enhancementCommand(hwnd,msg,wp,lp);
case WM_APP+43:showDiagnostics=false;layout();return 0;
case WM_APP+41:switch(wp){case 501:startVideoExport(lp!=0);break;case 502:{auto path=fileDialog(true);if(!path.empty())engine.saveFrame(path);break;}case 503:jobPaused=!jobPaused;exportJob.pause(jobPaused);break;case 504:exportJob.cancel();break;case 505:preferWatching=lp==BST_CHECKED;break;}return 0;
case WM_ENTERSIZEMOVE:
    endTransition();
    SetPropW(video,L"Veyra.InteractiveMove",HANDLE(1));
    veyra::log::info("ui-display","interactive move begin; retaining presentation buffers");
    return 0;
case WM_EXITSIZEMOVE:
    RemovePropW(video,L"Veyra.InteractiveMove");
    layout();
    veyra::log::info("ui-display","interactive move end; presentation resize released");
    return 0;
case WM_SIZE:cancelProtection();if(wp!=SIZE_MINIMIZED&&!dpiRefreshPending){endTransition();layout();}return 0;
case WM_DROPFILES:{std::vector<wchar_t> file(32768);DragQueryFileW(reinterpret_cast<HDROP>(wp),0,file.data(),32768);DragFinish(reinterpret_cast<HDROP>(wp));openFile(file.data());layout();return 0;}
case WM_COMMAND:return windowCommand(hwnd,msg,wp,lp);
case WM_HSCROLL:return scrollCommand(hwnd,msg,wp,lp);
case WM_KEYDOWN:if(wp==VK_ESCAPE&&protectionArmed){cancelProtection();return 0;}if(wp==VK_SPACE){SendMessageW(hwnd,WM_COMMAND,Play,0);return 0;}if(wp==VK_F11){toggleFullscreen();return 0;}if(wp==VK_ESCAPE&&full){toggleFullscreen();return 0;}if(wp=='V'&&uiState.mode==veyra::ui::Mode::Professional){holdOriginal=true;updateComparison();return 0;}break;
case WM_KEYUP:if(wp=='V'){holdOriginal=false;updateComparison();return 0;}break;
case WM_SYSKEYDOWN:if(wp==VK_RETURN&&(lp&(1LL<<29))){toggleFullscreen();return 0;}break;
case WM_THEMECHANGED:veyra::ui::glassTextTheme().reset();[[fallthrough]];
case WM_DWMCOMPOSITIONCHANGED:backdrop.configure();RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);return 0;
case WM_ACTIVATEAPP:if(!wp){holdOriginal=false;updateComparison();}break;
case WM_APP+90:
    if(smokeSeconds<=0||!(wp==0||wp==96||wp==144||wp==192))return 0;
    veyra::ui::smokeLayoutDpi=UINT(wp);
    veyra::log::info("ui-dpi-test",std::format("syntheticLayoutDpi={} windowsDpi={}",wp,GetDpiForWindow(hwnd)));
    [[fallthrough]];
case WM_DPICHANGED:{
    endTransition();veyra::ui::cancelPopupSelector();
    veyra::log::info("ui-display",std::format("DPI transition begin dpi={} synthetic={} moving={}",veyra::ui::layoutDpi(hwnd),msg!=WM_DPICHANGED,GetPropW(video,L"Veyra.InteractiveMove")!=nullptr));
    SetPropW(video,L"Veyra.DpiTransition",HANDLE(1));
    // SetWindowPos synchronously reenters WM_SIZE and child layout. Coalesce
    // the font/layout refresh after the native DPI transition has returned.
    const bool queueRefresh=!dpiRefreshPending;dpiRefreshPending=true;
    if(msg==WM_DPICHANGED&&!full){auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);}
    if(queueRefresh&&!PostMessageW(hwnd,WM_APP+91,0,0))return refreshWindowDpi(hwnd);
    return 0;}

case WM_TIMER:return windowTimer(hwnd,msg,wp,lp);
case WM_CLOSE:return closeWindow(hwnd,msg,wp,lp);
case WM_DESTROY:
playbackPower.update(false);
subtitleLoader.reset();
#ifdef VEYRA_ENABLE_REMOTEPLAY
remoteController.stop();KillTimer(hwnd,ControllerTimer);
#endif
backdrop.detach();DeleteObject(font);DeleteObject(emptyFont);PostQuitMessage(resultCode);return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);}
}
int runVeyraApp(HINSTANCE instance,int show){
Gdiplus::GdiplusStartupInput graphicsInput;ULONG_PTR graphicsToken=0;Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr);struct GraphicsCleanup{ULONG_PTR token;~GraphicsCleanup(){Gdiplus::GdiplusShutdown(token);}} graphicsCleanup{graphicsToken};
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
initialOptions=veyra::engine::PlayerOptions::from(veyra::ui::defaultSettings());

int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--export-worker"&&i+1<argc)workerMapping=reinterpret_cast<HANDLE>(_wcstoui64(argv[++i],nullptr,10));else if(arg==L"--smoke-view"&&i+1<argc)smokeView=argv[++i];else if(arg==L"--smoke-fg-only")smokeFgOnly=true;else if(arg==L"--smoke-transport")smokeTransport=true;else if(arg==L"--smoke-repair-ui")smokeRepair=true;else if(arg==L"--smoke-repair-ui-reject")smokeRepair=smokeRepairReject=true;else if(arg==L"--smoke-hover")smokeZoom=smokeHover=true;else if(arg==L"--smoke-protection")smokeProtection=true;else if(arg==L"--smoke-zoom")smokeZoom=true;else if(arg==L"--smoke-empty")smokeEmpty=true;else if(arg==L"--smoke-dual")smokeDual=true;else if(arg==L"--smoke-dual-pause"){smokeDual=smokeDualPause=true;}else if(arg==L"--smoke-master-reject")smokeMaster=smokeMasterReject=true;else if(arg==L"--smoke-master")smokeMaster=true;else if(arg==L"--smoke-audio")smokeAudio=true;else if(arg==L"--smoke-job-cancel")smokeJobCancel=true;else if(arg==L"--smoke-job-exit")smokeJobExit=true;else if(arg==L"--smoke-job")smokeJob=true;else if(arg==L"--smoke-dual-export"&&i+1<argc)smokeDualOutput=argv[++i];else if(arg==L"--smoke-seconds"&&i+1<argc)smokeSeconds=std::clamp(_wtoi(argv[++i]),1,240);else if(arg==L"--export-out"&&i+1<argc)exportOutput=argv[++i];else if(arg==L"--max-frames"&&i+1<argc)exportFrames=std::max(1,_wtoi(argv[++i]));else if(arg==L"--cancel-after-ms"&&i+1<argc)cancelAfterMs=std::clamp(_wtoi(argv[++i]),1,240000);else if(arg==L"--hevc")exportHevc=true;else if(arg==L"--bitrate-mbps"&&i+1<argc)initialOptions.settings.exportBitrateMbps=uint32_t(std::clamp(_wtoi(argv[++i]),0,300));else if(arg==L"--subtitle-primary"&&i+1<argc)subtitleRequestPrimary=std::clamp(_wtoi(argv[++i]),-1,64);else if(arg==L"--subtitle-secondary"&&i+1<argc)subtitleRequestSecondary=std::clamp(_wtoi(argv[++i]),-1,64);else if(arg==L"--subtitle-offset-ms"&&i+1<argc)subtitleRequestOffsetMs=std::clamp(_wtoi(argv[++i]),-30000,30000);else if(arg==L"--subtitle-font-size"&&i+1<argc)subtitlePixels=std::clamp(_wtoi(argv[++i]),16,56);else if(arg==L"--subtitle-no-outline")uiState.subtitleOutline=false;else if(arg==L"--subtitle-background")uiState.subtitleBackground=true;else if(arg==L"--subtitle-auto-align")subtitleRequestAutoAlign=true;else if(arg==L"--smoke-rollback-flow"){smokeRollback=true;smokeRollbackFlow=true;}else if(arg==L"--smoke-rollback")smokeRollback=true;else if(arg==L"--smoke-ui")smokeUi=true;else if(arg==L"--video-hdr")initialOptions.settings.videoHdr.enabled=true;else if(arg==L"--no-fg")initialOptions.fg=false;else if(arg==L"--smoke-settings")smokeSettings=true;else if(arg==L"--smoke-color")smokeColor=true;else if(arg==L"--smoke-screenshot")smokeScreenshot=true;else if(arg==L"--smoke-controls")smokeControls=true;else if(arg==L"--smoke-save"&&i+1<argc)smokeSave=argv[++i];else if(arg==L"--native")initialOptions.realtime=false;else if(arg==L"--realtime")initialOptions.realtime=true;else if(arg==L"--fg")initialOptions.fg=true;else if(arg==L"--fg-multiplier"&&i+1<argc){initialOptions.fgMultiplier=std::clamp(_wtoi(argv[++i]),2,6);initialOptions.fg=true;}else if(arg==L"--capture-audio"&&i+1<argc)initialOptions.settings.captureAudio=static_cast<veyra::engine::CaptureAudioIngress>(std::clamp(_wtoi(argv[++i]),0,2));else if(arg==L"--flow-amd")initialOptions.settings.opticalFlowBackend=veyra::engine::OpticalFlowBackend::AmdFidelityFx;else if(arg==L"--flow-gpudis")initialOptions.settings.opticalFlowBackend=veyra::engine::OpticalFlowBackend::GpuDis;else if(arg==L"--video-sr"&&i+1<argc){initialOptions.settings.videoSrQuality=std::clamp(_wtoi(argv[++i]),1,5);initialOptions.sr=true;}else if(arg==L"--nr-ampere")initialOptions.settings.nrRuntime=veyra::engine::NrRuntime::Ampere;else if(arg==L"--nr-community")initialOptions.settings.nrRuntime=veyra::engine::NrRuntime::Community;else if(arg==L"--nr-original")initialOptions.settings.nrRuntime=veyra::engine::NrRuntime::Original;else if(arg==L"--sr")initialOptions.sr=true;else if(arg==L"--no-nr")initialOptions.nr=false;else if(arg==L"--nr")initialOptions.nr=true;else if(arg==L"--no-sr")initialOptions.sr=false;else autoInput=arg;}LocalFree(argv);
// Diagnostic: force the colour grade on with a fixed exposure (EV). The grade is
// fused into the ingest dispatch, so this flag is how the graded cost is measured
// against the grade-off baseline on the same clip.
{int gradeArgc=0;auto gradeArgv=CommandLineToArgvW(GetCommandLineW(),&gradeArgc);for(int i=1;i<gradeArgc;++i){if(!wcsncmp(gradeArgv[i],L"--color-grade=",14)){initialOptions.settings.color.enabled=true;initialOptions.settings.color.exposure=float(_wtof(gradeArgv[i]+14));}}if(gradeArgv)LocalFree(gradeArgv);}
{int pageArgc=0;auto pageArgv=CommandLineToArgvW(GetCommandLineW(),&pageArgc);for(int i=1;i<pageArgc;++i)if(!_wcsicmp(pageArgv[i],L"--colour-page"))openColourPageOnStart=true;if(pageArgv)LocalFree(pageArgv);}
// The legacy argument clamp caps --fg-multiplier at 4X. Re-read the flag here so
// 6X (and 2X/3X/4X) can be exercised from the command line for diagnostics and
// smoke tests; the interactive UI uses the capability-driven list instead.
{int fgArgc=0;auto fgArgv=CommandLineToArgvW(GetCommandLineW(),&fgArgc);for(int i=1;i<fgArgc;++i){if(!_wcsicmp(fgArgv[i],L"--fg-multiplier")&&i+1<fgArgc){const int value=_wtoi(fgArgv[i+1]);if(value>=2&&value<=6){initialOptions.fgMultiplier=uint32_t(value);initialOptions.fg=true;}else if(value==1){initialOptions.fg=false;initialOptions.fgMultiplier=2;}}else if(!_wcsicmp(fgArgv[i],L"--fg-xess")){initialOptions.settings.frameGenerationBackend=veyra::engine::FrameGenerationBackend::XeSS;if(initialOptions.fgMultiplier<2)initialOptions.fgMultiplier=2;initialOptions.fg=true;}else if(!_wcsicmp(fgArgv[i],L"--fg-fsr")){initialOptions.settings.frameGenerationBackend=veyra::engine::FrameGenerationBackend::Fsr;if(initialOptions.fgMultiplier<2)initialOptions.fgMultiplier=2;initialOptions.fg=true;}else if(!_wcsicmp(fgArgv[i],L"--fg-dlss")){initialOptions.settings.frameGenerationBackend=veyra::engine::FrameGenerationBackend::Dlss;}}if(fgArgv)LocalFree(fgArgv);}
// Backend flags must not be mistaken for the input path: the pass above leaves
// the last unrecognised token in autoInput, so "clip --fg-xess --fg-multiplier 4"
// tried to open a file named "--fg-xess". Re-derive the positional argument with
// every value-taking flag known, so --fg-xess / --fg-fsr / --fg-dlss work
// anywhere on the command line instead of only at the end.
{
    // --capture-flip: diagnostics/smoke only; the interactive path uses the
    // capture panel checkbox and persists captureFlipVertical with presets.
    {int flipArgc=0;auto flipArgv=CommandLineToArgvW(GetCommandLineW(),&flipArgc);for(int i=1;i<flipArgc;++i){if(!_wcsicmp(flipArgv[i],L"--capture-flip"))initialOptions.settings.captureFlipVertical=true;else if(!wcsncmp(flipArgv[i],L"--capture-buffer=",17)){const wchar_t* value=flipArgv[i]+17;if(!_wcsicmp(value,L"auto"))initialOptions.settings.captureBuffer=veyra::source::CaptureBufferMode::Auto;else if(!_wcsicmp(value,L"minimum"))initialOptions.settings.captureBuffer=veyra::source::CaptureBufferMode::Minimum;else if(!_wcsicmp(value,L"driver"))initialOptions.settings.captureBuffer=veyra::source::CaptureBufferMode::DriverDefault;}else if(!_wcsicmp(flipArgv[i],L"--capture-cpu-unpack"))initialOptions.captureCpuUnpack=true;}LocalFree(flipArgv);}
    int positionalArgc=0;auto positionalArgv=CommandLineToArgvW(GetCommandLineW(),&positionalArgc);std::wstring positional;
   const wchar_t* valueFlags[]={L"--export-worker",L"--smoke-view",L"--smoke-dual-export",L"--smoke-seconds",L"--export-out",L"--max-frames",L"--cancel-after-ms",L"--smoke-save",L"--fg-multiplier",L"--capture-audio",L"--video-sr",L"--bitrate-mbps",L"--subtitle-primary",L"--subtitle-secondary",L"--subtitle-offset-ms",L"--subtitle-font-size"};
    for(int i=1;i<positionalArgc;++i){const std::wstring a=positionalArgv[i];
        bool takesValue=false;for(const wchar_t* flag:valueFlags)if(a==flag){takesValue=true;break;}
        if(takesValue){++i;continue;}
        if(a.starts_with(L"--"))continue;
        positional=a;}
    if(!positional.empty())autoInput=positional;
    if(positionalArgv)LocalFree(positionalArgv);
}
if(workerMapping){const int code=veyra::engine::runExportWorker(workerMapping);CoUninitialize();return code;}
if(smokeSeconds<=0&&exportOutput.empty()){uiPreferences=preferences.load();initialOptions=veyra::engine::PlayerOptions::from(preferences.startup(initialOptions.snapshot()));engine.setVolume(uiPreferences.volume,uiPreferences.muted);uiState.subtitles=uiPreferences.subtitles;subtitlePixels=uiPreferences.subtitleSize;uiState.subtitleOutline=uiPreferences.subtitleOutline;uiState.subtitleBackground=uiPreferences.subtitleBackground;uiState.subtitleSecondLanguage=uiPreferences.subtitleSecondLanguage;uiState.subtitleMargin=uiPreferences.subtitleMargin;uiState.subtitleFont=uiPreferences.subtitleFont;}
engine.requestPresentation(uiPreferences.presentation);
engine.requestSettings(initialOptions.snapshot());
if(!exportOutput.empty()){std::atomic<bool> cancel{false},finished{false};std::thread cancelTimer;if(cancelAfterMs)cancelTimer=std::thread([&]{const auto start=GetTickCount64();while(!finished&&GetTickCount64()-start<cancelAfterMs)std::this_thread::sleep_for(std::chrono::milliseconds(5));if(!finished)cancel=true;});bool ok=veyra::engine::exportVideo(autoInput,exportOutput,initialOptions,exportHevc,cancel,[](double,const std::wstring& s){OutputDebugStringW(s.c_str());},exportFrames);finished=true;if(cancelTimer.joinable())cancelTimer.join();veyra::log::info("app",std::format("export result={}",ok));CoUninitialize();return ok?0:cancel?3:1;}
WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"VeyraApp";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=veyra::ui::bgBrush();
wc.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_VEYRA),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
wc.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_VEYRA),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
RegisterClassExW(&wc);
auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Veyra — 本地实验版",ShellStyle,CW_USEDEFAULT,CW_USEDEFAULT,std::min(MulDiv(uiPreferences.width,GetDpiForSystem(),96),GetSystemMetrics(SM_CXSCREEN)),std::min(MulDiv(uiPreferences.height,GetDpiForSystem(),96),GetSystemMetrics(SM_CYSCREEN)-40),nullptr,nullptr,instance,nullptr);if(!hwnd)return 1;if(uiPreferences.positioned&&smokeSeconds<=0){RECT target{uiPreferences.x,uiPreferences.y,uiPreferences.x+MulDiv(uiPreferences.width,GetDpiForSystem(),96),uiPreferences.y+MulDiv(uiPreferences.height,GetDpiForSystem(),96)};MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST),&mi);int width=std::min(target.right-target.left,mi.rcWork.right-mi.rcWork.left),height=std::min(target.bottom-target.top,mi.rcWork.bottom-mi.rcWork.top);SetWindowPos(hwnd,nullptr,std::clamp(target.left,mi.rcWork.left,mi.rcWork.right-width),std::clamp(target.top,mi.rcWork.top,mi.rcWork.bottom-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);}CheckDlgButton(hwnd,Nr,initialOptions.nr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Sr,initialOptions.sr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Fg,initialOptions.fg?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,Realtime,initialOptions.realtime?BST_CHECKED:BST_UNCHECKED);ShowWindow(hwnd,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(full&&GetAncestor(msg.hwnd,GA_ROOT)==hwnd&&(msg.message==WM_MOUSEMOVE||msg.message==WM_LBUTTONDOWN||msg.message==WM_KEYDOWN)){static POINT previous{-9999,-9999};POINT now{};GetCursorPos(&now);if(msg.message!=WM_MOUSEMOVE||now.x!=previous.x||now.y!=previous.y)pointerActivity();previous=now;}if(GetAncestor(msg.hwnd,GA_ROOT)==hwnd&&(msg.message==WM_KEYDOWN||msg.message==WM_KEYUP||msg.message==WM_SYSKEYDOWN)){
wchar_t focusedClass[32]{};GetClassNameW(GetFocus(),focusedClass,32);const bool editing=_wcsicmp(focusedClass,L"Edit")==0||_wcsicmp(focusedClass,L"ComboBox")==0;
const bool adjustingSlider=_wcsicmp(focusedClass,TRACKBAR_CLASSW)==0&&GetFocus()!=seekBar;
if(!editing&&!adjustingSlider&&_wcsicmp(focusedClass,L"ListBox")!=0&&!veyra::ui::popupSelectorOpen()&&msg.message==WM_KEYDOWN&&(msg.wParam==VK_UP||msg.wParam==VK_DOWN)&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){
    const auto state=engine.snapshot();engine.setVolume(std::clamp(state.volume+(msg.wParam==VK_UP?.05f:-.05f),0.0f,1.0f),false);continue;
}
if(!editing&&!veyra::ui::popupSelectorOpen()&&msg.message==WM_KEYDOWN&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){
    const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    switch(msg.wParam){
    case 'B':uiState.subtitles=!uiState.subtitles;subtitleStatus=uiState.subtitles?L"字幕：开":L"字幕：关";continue;
    case 'Z':setSubtitleOffset(shift?-1000:-50);continue;
    case 'X':setSubtitleOffset(shift?1000:50);continue;
    case 'T':cycleSubtitleTrack(false);continue;
    case 'Y':cycleSubtitleTrack(true);continue;
    default:break;
    }
}
if(!editing&&(!adjustingSlider||full)&&!veyra::ui::popupSelectorOpen()&&msg.message==WM_KEYDOWN&&(msg.wParam==VK_LEFT||msg.wParam==VK_RIGHT)&&
   !(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){
    const auto state=engine.snapshot();
    if(state.running&&!state.capture&&!state.image&&state.duration>0){
        const double origin=state.seekPresented<state.seekRequested?state.seekTarget:state.position;
        const double target=std::clamp(origin+(msg.wParam==VK_RIGHT?10.0:-10.0),0.0,state.duration);
        engine.seek(target);
        veyra::log::info("ui-seek-key",std::format("direction={} origin={:.3f} target={:.3f} fullscreen={}",msg.wParam==VK_RIGHT?"right":"left",origin,target,full));
        if(full)pointerActivity();continue;
    }
}
if(!editing&&msg.message==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&msg.wParam=='O'){SendMessageW(hwnd,WM_COMMAND,Open,0);continue;}
if(!editing&&msg.message==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&msg.wParam=='E'){if(uiState.mode==veyra::ui::Mode::Daily)switchMode();selectInspector(3);continue;}
const bool key=(!editing)&&(msg.wParam==VK_F11||msg.wParam==VK_ESCAPE||(msg.wParam==VK_SPACE&&(GetFocus()==hwnd||GetFocus()==video))||msg.wParam=='V'||(msg.wParam==VK_RETURN&&msg.message==WM_SYSKEYDOWN));if(key){SendMessageW(hwnd,msg.message,msg.wParam,msg.lParam);continue;}}
if(msg.message==WM_MOUSEWHEEL&&GetAncestor(msg.hwnd,GA_ROOT)==hwnd&&!veyra::ui::popupSelectorOpen()){
    POINT p{GET_X_LPARAM(msg.lParam),GET_Y_LPARAM(msg.lParam)};const HWND hovered=WindowFromPoint(p);
    if(hovered==video&&uiState.mode==veyra::ui::Mode::Professional){SendMessageW(video,msg.message,msg.wParam,msg.lParam);continue;}
    if(hovered==video||hovered==GetDlgItem(hwnd,Volume)){
        static int wheelRemainder=0;wheelRemainder+=GET_WHEEL_DELTA_WPARAM(msg.wParam);const int steps=wheelRemainder/WHEEL_DELTA;wheelRemainder%=WHEEL_DELTA;
        if(steps){const auto state=engine.snapshot();engine.setVolume(std::clamp(state.volume+steps*.05f,0.0f,1.0f),false);}continue;
    }
}
if(veyra::ui::subtitleSettingsDialogMessage(msg)||veyra::ui::screenCaptureDialogMessage(msg)||IsDialogMessageW(hwnd,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}CoUninitialize();return int(msg.wParam);
}
