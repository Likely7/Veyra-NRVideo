#include "SettingsWindow.h"
#include "ui/Theme.h"
#include "ui/SettingHelp.h"
#include "ui/UiPreferenceStore.h"
#include <commdlg.h>
#include "veyra/engine/PresetStore.h"
#include "veyra/engine/ColorLookStore.h"
#include "veyra/engine/ColorLut.h"
#include "veyra/pipeline/ColorGradeTables.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/XessMfgUnlock.h"
#include <filesystem>
#include <format>
#include <sstream>
#include <iomanip>
#include <array>
#include <cmath>
#include <map>
#include <set>
namespace veyra::ui {
namespace {
HWND window=nullptr,body=nullptr;engine::EngineController* controller=nullptr;HFONT font=nullptr;
std::function<bool(engine::EnhancementSettings)> apply;
// Filled by AppShell: settings messages are shown in the player's bottom bar.
std::function<void(const std::wstring&)> statusSink;
engine::PresetStore store(runtime::localDataDirectory()/"user-presets.v1");
engine::PresetStore nrPresetStore(runtime::localDataDirectory()/"nr-presets.v1");
bool loaded=false,nrPresetsLoaded=false,dirty=false,populating=false,enhancementEnabled=true;int page=0,scroll=0,contentHeight=0;uint64_t displayedRevision=0;engine::EnhancementSettings configuredSettings;
std::wstring displayedBackendWarning;
engine::EnhancementSettings displayedSettings;
bool smoothMotionHelpExpanded=false;
constexpr auto smoothMotionHelp=L"Smooth Motion only\n"
    L"1. Set this page's frame generation multiplier to \"Frame generation off\". NR and super resolution work as usual.\n"
    L"2. NVIDIA App -> Graphics -> select the Veyra.exe you use -> AI frame generation -> On. Add it manually if the program isn't listed.\n"
    L"3. Restart the player after applying. Settings enabled for an older experimental EXE must be set again for the current program.\n\n"
    L"Switching and stacking\n"
    L"DLSS / XeSS only: turn off AI frame generation in the NVIDIA App, restart, then choose the frame generation method and multiplier here.\n"
    L"Both can be enabled at once. Stacking isn't verified and isn't guaranteed to be better; it may add ghosting, latency or GPU load, so drop one layer if it doesn't suit you.\n\n"
    L"Notes\n"
    L"- The software's \"Frame generation off\" and the master enhancement switch do not disable the driver's AI frame generation.\n"
    L"- The panel FPS, timing and queue don't include driver-generated frames, so don't use them to judge whether the driver is active, and don't just double the FPS.\n"
    L"- The driver's extra latency isn't measured, so audio/video sync must be tested in practice. Screenshots and exports don't include driver-generated frames; whether live recording captures them also needs separate testing.\n"
    L"- Feature availability depends on NVIDIA App, GPU and driver support.";
struct Item{HWND h;int page,x,y,w,height;bool hidden=false;};std::vector<Item> items;
struct RowReset {
    int slider,label,value;
    std::function<double(const engine::EnhancementSettings&)> get;
    std::function<void(engine::EnhancementSettings&)> reset;
    std::wstring tip;
};
std::map<int,RowReset> rowResets;
std::set<int> editDrafts;
void syncRowResets();
HWND item(int id){for(auto& entry:items)if(GetDlgCtrlID(entry.h)==id)return entry.h;return nullptr;}
LRESULT send(int id,UINT message,WPARAM w=0,LPARAM l=0){return SendMessageW(item(id),message,w,l);}
void putText(int id,const wchar_t* value){setText(item(id),value);}
void check(int id,UINT value){if(send(id,BM_GETCHECK)!=value)send(id,BM_SETCHECK,value);}
UINT checked(int id){return UINT(send(id,BM_GETCHECK));}
int viewportHeight(){RECT r{};GetClientRect(body,&r);return std::max(1,MulDiv(r.bottom,96,veyra::ui::layoutDpi(window)));}
void arrange(bool commitScroll=false);
LRESULT CALLBACK bodyProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);if(contentHeight>viewportHeight()){int thumb=std::max(dip(h,30),int(paint.rect.bottom*float(viewportHeight())/contentHeight));int y=int((paint.rect.bottom-thumb)*float(scroll)/std::max(1,contentHeight-viewportHeight()));RECT bar{paint.rect.right-dip(h,5),y,paint.rect.right-dip(h,2),y+thumb};roundRect(paint.dc,bar,line,dip(h,2));}return 0;}
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX||msg==WM_CTLCOLORBTN)return colors(msg,wp,lp);
    if(msg==WM_COMMAND||msg==WM_HSCROLL||msg==WM_MOUSEWHEEL||msg==WM_VSCROLL||msg==WM_NOTIFY)return SendMessageW(window,msg,wp,lp);
    if(msg==WM_LBUTTONDOWN){RECT r{};GetClientRect(h,&r);if(GET_X_LPARAM(lp)>=r.right-dip(h,12))SetCapture(h);}
    if((msg==WM_LBUTTONDOWN||msg==WM_MOUSEMOVE)&&GetCapture()==h){RECT r{};GetClientRect(h,&r);scroll=int(float(GET_Y_LPARAM(lp))/std::max(1L,r.bottom)*std::max(0,contentHeight-viewportHeight()));arrange(true);return 0;}
    if(msg==WM_LBUTTONUP&&GetCapture()==h){ReleaseCapture();return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}

const wchar_t* labels[]={L"Model strength",L"Local brightness",L"Local structure",L"Skin · Unverified",L"Style · Experimental",L"Auto mask · Experimental",L"UI correction · Unverified",L"Total change strength",L"Darkening change",L"Brightening change",L"Color change",L"Luminance change"};
void loadStore(){if(!loaded){store.load();loaded=true;}}
void loadNrPresetStore(){if(!nrPresetsLoaded){nrPresetStore.load();nrPresetsLoaded=true;}}

// NR presets intentionally live in their own file. Applying one only copies
// the NR model/residual/protection/time-domain fields; SR, FG, pacing, audio,
// colour and capture settings remain untouched. This makes a named NR look
// safe to try while preserving the generic full-settings preset format.
void refreshNrPresets(const std::wstring& select={}){
    loadNrPresetStore();
    const auto combo=item(260);
    if(!combo)return;
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,LPARAM(L"(no NR preset selected)"));
    int selection=0;
    for(const auto& preset:nrPresetStore.entries()){
        SendMessageW(combo,CB_ADDSTRING,0,LPARAM(preset.name.c_str()));
        if(!select.empty()&&preset.name==select)selection=int(SendMessageW(combo,CB_GETCOUNT,0,0))-1;
    }
    SendMessageW(combo,CB_SETCURSEL,WPARAM(selection),0);
}
engine::EnhancementSettings nrPresetSettings(const engine::EnhancementSettings& current){
    auto s=current;
    s.nr=true;
    return s;
}
void applyNrPresetFields(engine::EnhancementSettings& target,const engine::EnhancementSettings& preset){
    target.nr=preset.nr;
    target.model=preset.model;
    target.residual=preset.residual;
    target.protection=preset.protection;
    target.nrTemporal=preset.nrTemporal;
}
// ---------------------------------------------------------------------------
// Colour page (plan v4). Sections collapse like an accordion; the rows are laid
// out sequentially in layoutColorPage() so a collapsed section simply skips its
// rows instead of relying on static y offsets. The fold mask is persisted in
// ui-preferences.v1 with the rest of the UI state.
// ---------------------------------------------------------------------------
constexpr int kColorSections=7;
void message(const std::wstring& text);
bool submit(engine::EnhancementSettings s);
uint32_t colorFoldMask=0;
std::wstring colorSectionName(int section){
    static const wchar_t* names[kColorSections]={L"Light",L"Color",L"Curves",L"Mixer",L"Color grading",L"Calibration",L"LUT"};
    return section>=0&&section<kColorSections?names[section]:L"Color";
}
// Which field of ColorSettings a row edits. Scalars use the member pointer;
// array-valued controls (mixer bands, grading wheels, calibration primaries)
// use the target + index pair.
enum class ColorTarget {
    Scalar,MixerHue,MixerSaturation,MixerLuminance,BlackWhiteMix,
    GradingHue,GradingSaturation,GradingLuminance,CalibrationHue,CalibrationSaturation
};
struct ColorParam{
    int section;
    const wchar_t* label;
    float min,max;
    ColorTarget target;
    float engine::ColorSettings::*field;
    int index;
    // Value a double-click / Home returns the row to (0 for every slider except
    // the LUT strength and the grading blend).
    float neutral=0.0f;
    // Lightroom-style rail: colour-relevant rows get a gradient track instead of
    // the plain rail, so the direction of the slider is readable at a glance.
    int gradient=0; // 0 none, 1 temperature, 2 tint, 3 saturation, 4 hue rainbow
    float value(const engine::ColorSettings& colour)const{
        switch(target){
        case ColorTarget::MixerHue:return colour.mixerHue[std::size_t(index)];
        case ColorTarget::MixerSaturation:return colour.mixerSaturation[std::size_t(index)];
        case ColorTarget::MixerLuminance:return colour.mixerLuminance[std::size_t(index)];
        case ColorTarget::BlackWhiteMix:return colour.blackWhiteMix[std::size_t(index)];
        case ColorTarget::GradingHue:return colour.grading[std::size_t(index)].hue;
        case ColorTarget::GradingSaturation:return colour.grading[std::size_t(index)].saturation;
        case ColorTarget::GradingLuminance:return colour.grading[std::size_t(index)].luminance;
        case ColorTarget::CalibrationHue:return colour.calibrationHue[std::size_t(index)];
        case ColorTarget::CalibrationSaturation:return colour.calibrationSaturation[std::size_t(index)];
        case ColorTarget::Scalar:break;
        }
        return field?colour.*field:0.0f;
    }
    void set(engine::ColorSettings& colour,float v)const{
        switch(target){
        case ColorTarget::MixerHue:colour.mixerHue[std::size_t(index)]=v;return;
        case ColorTarget::MixerSaturation:colour.mixerSaturation[std::size_t(index)]=v;return;
        case ColorTarget::MixerLuminance:colour.mixerLuminance[std::size_t(index)]=v;return;
        case ColorTarget::BlackWhiteMix:colour.blackWhiteMix[std::size_t(index)]=v;return;
        case ColorTarget::GradingHue:colour.grading[std::size_t(index)].hue=v;return;
        case ColorTarget::GradingSaturation:colour.grading[std::size_t(index)].saturation=v;return;
        case ColorTarget::GradingLuminance:colour.grading[std::size_t(index)].luminance=v;return;
        case ColorTarget::CalibrationHue:colour.calibrationHue[std::size_t(index)]=v;return;
        case ColorTarget::CalibrationSaturation:colour.calibrationSaturation[std::size_t(index)]=v;return;
        case ColorTarget::Scalar:break;
        }
        if(field)colour.*field=v;
    }
};
std::vector<ColorParam> colorParams;
// Defined with the mixer controls further down; the layout needs it to decide
// which of the 24 mixer rows is on screen.
int mixerModeOf(const ColorParam& param);
// id layout: master 800, reset 801, undo 802, header 810+s, label 830+i,
// edit 850+i, slider 870+i.
// The colour page owns 1200..1499: label/edit/slider per parameter, so the four
// remaining sections (mixer 8x3, grading, calibration, curves) fit without
// colliding with the enhancement page's ids.
constexpr int colorLabelId(int i){return 1200+i;}
constexpr int colorEditId(int i){return 1300+i;}
constexpr int colorSliderId(int i){return 1400+i;}
// Colour wheels (4 zones) and their class name; ids sit above the button block.
constexpr int colorWheelId(int zone){return 840+zone;}
constexpr const wchar_t* kColorWheelClass=L"VeyraColorWheel";
// Colour-mixer band strip and the "correction" dropdown that decide which of the
// 24 mixer rows is on screen (Lightroom shows one colour range at a time).
constexpr int colorMixerModeId=830,colorBandsId=831;
constexpr const wchar_t* kColorBandsClass=L"VeyraColorBands";
int colourMixerMode=0;   // 0 hue, 1 saturation, 2 luminance, 3 black & white
int colourMixerBand=0;   // 0..7, the eight colour ranges
bool colourBlackWhite=false;   // the mixer's black & white switch
// Tone-curve editor: which channel is being edited (0 RGB, 1 R, 2 G, 3 B) and
// which control point is being dragged.
constexpr int colorCurveCanvasId=850,colorCurveChannelId=851,colorCurveResetId=855;
constexpr const wchar_t* kColorCurveClass=L"VeyraToneCurve";
// Per-section bypass "eye" (plan T3). One small owner-drawn control per section,
// sitting at the right end of its header row.
constexpr int colorSectionEyeId(int section){return 860+section;}
constexpr const wchar_t* kColorEyeClass=L"VeyraColorEye";
int colourCurveChannel=0;
int colourCurveDragPoint=-1;
constexpr int kColorMaxParams=100;
engine::ColorSettings colourTarget(){
    return (enhancementEnabled?controller->snapshot().desired:configuredSettings).color;
}
// Multi-step undo/redo for the colour page (plan T3: "undo/redo"). The stack
// holds applied states, so both the one-click reset and every live edit are
// reversible. Pasting or holding "view original" never records history of its own.
std::vector<engine::ColorSettings> colourHistory;
int colourHistoryIndex=-1;
engine::ColorSettings colourClipboard;
bool colourClipboardValid=false;
engine::ColorSettings colourHoldSaved;
bool colourHolding=false;
bool colourHoldHadValue=false;
void colourHistoryReset(const engine::ColorSettings& current){
    colourHistory.assign(1,current);
    colourHistoryIndex=0;
}
void colourHistoryPush(const engine::ColorSettings& state){
    if(colourHistoryIndex>=0&&colourHistoryIndex<int(colourHistory.size())&&colourHistory[size_t(colourHistoryIndex)]==state)return;
    colourHistory.resize(size_t(colourHistoryIndex)+1);
    colourHistory.push_back(state);
    if(colourHistory.size()>32)colourHistory.erase(colourHistory.begin());
    colourHistoryIndex=int(colourHistory.size())-1;
}
int colorPageContentHeight=0;
bool syncingColour=false;
// Named colour looks + the .cube list shown in the colour page.
std::vector<std::wstring> colourLookNames;
std::vector<std::wstring> colourLutNames;
int selectedColourLook=-1;
// Rebuild the preset combo box. `select` re-selects a named look (used after
// saving/importing so the new preset stays highlighted instead of silently
// falling back to "no preset selected").
void refreshColourLooks(const std::wstring& select={}){
    engine::ColorLookStore lookStore(runtime::localDataDirectory());
    lookStore.load();
    colourLookNames.clear();
    // The controls live on the scrolling `body` panel, not directly on `window`,
    // so they must be addressed through their own handle (SendDlgItemMessageW
    // only walks direct children and silently left this combo empty).
    const auto combo=item(803);
    if(!window||!combo)return;
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,LPARAM(L"(no preset selected)"));
    int selection=0;
    for(const auto& look:lookStore.entries()){
        colourLookNames.push_back(look.name);
        SendMessageW(combo,CB_ADDSTRING,0,LPARAM(look.name.c_str()));
        if(!select.empty()&&look.name==select)selection=int(colourLookNames.size());
    }
    SendMessageW(combo,CB_SETCURSEL,WPARAM(selection),0);
    selectedColourLook=selection-1;
}
void refreshColourLuts(){
    engine::ColorLutStore lutStore(runtime::localDataDirectory());
    colourLutNames=lutStore.list();
    const auto combo=item(817);
    if(!window||!combo)return;
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,LPARAM(L"No LUT"));
    for(const auto& name:colourLutNames)SendMessageW(combo,CB_ADDSTRING,0,LPARAM(name.c_str()));
    int selection=0;
    const auto current=colourTarget().lutNameString();
    for(size_t i=0;i<colourLutNames.size();++i)if(colourLutNames[i]==current)selection=int(i)+1;
    SendMessageW(combo,CB_SETCURSEL,WPARAM(selection),0);
}
std::wstring windowText(HWND h){wchar_t buffer[256]{};GetWindowTextW(h,buffer,256);return buffer;}
void loadColourFoldState(){
    const auto preferences=veyra::ui::UiPreferenceStore(runtime::localDataDirectory()).load();
    colorFoldMask=preferences.colourFoldMask;
}
void saveColourFoldState(){
    // One load+save per click is unavoidable while the store is a whole-file
    // document; skip the write when the mask did not change.
    static uint32_t lastSavedMask=UINT32_MAX;
    if(lastSavedMask==colorFoldMask)return;
    veyra::ui::UiPreferenceStore store_(runtime::localDataDirectory());
    auto preferences=store_.load();
    preferences.colourFoldMask=colorFoldMask;
    if(!store_.save(preferences,nullptr))veyra::log::warn("color-ui","fold state not saved");else lastSavedMask=colorFoldMask;
}
void layoutColorPage(int bodyWidthDip){
    auto place=[&](int id,int y,int height,bool hidden,int x=-1,int w=-2){
        for(auto& entry:items)if(GetDlgCtrlID(entry.h)==id){entry.y=y;entry.height=height;entry.hidden=hidden;if(x>=0)entry.x=x;if(w!=-2)entry.w=w;return;}
    };
    int y=12;
    // Sticky-header bookkeeping (see the section loop below).
    int previousHeaderId=-1,previousHeaderY=0;
    place(800,y,36,false);y+=42;
    // Preset toolbar: pick a saved look, type a name, then save/apply/delete or
    // import/export. The combo and the name field used to both stretch to the
    // full width and cover each other, which is why users reported "there is no
    // place to save a preset".
    const int presetHalf=std::max(1,(bodyWidthDip-36)/2);
    place(803,y,30,false,12,presetHalf);
    place(804,y,30,false,12+presetHalf+12,presetHalf-6);
    y+=36;
    auto toolbar=[&](std::initializer_list<int> ids){
        const int available=std::max(1,bodyWidthDip-24);
        const int columns=std::clamp((available+6)/96,1,int(ids.size()));
        const int width=(available-6*(columns-1))/columns;
        int index=0;
        for(int id:ids){place(id,y+(index/columns)*38,32,false,12+(index%columns)*(width+6),width);++index;}
        y+=((index+columns-1)/columns)*38;
    };
    toolbar({805,806,807,808,809});
    toolbar({801,802,824,822,823});
    place(821,y,32,false);y+=42;
    for(int section=0;section<kColorSections;++section){
        const bool collapsed=(colorFoldMask>>section)&1u;
        // Sticky section headers (Lightroom does this too): a header never
        // scrolls under the top edge of the viewport - it is clamped to the
        // visible top and pushed up by the next header that reaches it. Without
        // the clamp a scrolled header was cut in half ("text left as half").
        int headerY=y;
        if(headerY<scroll)headerY=scroll;
        if(previousHeaderId>=0)place(previousHeaderId,std::min(previousHeaderY,headerY-36),36,false);
        place(810+section,headerY,36,false);
        place(colorSectionEyeId(section),headerY+5,26,false,bodyWidthDip-40,26);
        previousHeaderId=810+section;previousHeaderY=headerY;
        const auto title=std::format(L"{}  {}",collapsed?L"▸":L"▾",colorSectionName(section));
        putText(810+section,title.c_str());
        y+=38;
        // Four colour wheels in a 2x2 grid lead the colour-grading section, like
        // a professional grading panel; blending/balance keep their slider rows.
        if(section==4){
            // body width is not in scope here; the viewport width is enough to
            // split the wheel grid into two columns.
            // A folded section must not reserve the wheel grid's height - that is
            // what left huge gaps between collapsed headers.
            const int cellWidth=std::max<int>(dip(window,120),(bodyWidthDip-24-8)/2);
            const int wheelsTop=y+(collapsed?0:12);
            place(colorWheelId(0),wheelsTop,190,collapsed,12,cellWidth);
            place(colorWheelId(1),wheelsTop,190,collapsed,12+cellWidth+8,cellWidth);
            place(colorWheelId(2),wheelsTop+196,190,collapsed,12,cellWidth);
            place(colorWheelId(3),wheelsTop+196,190,collapsed,12+cellWidth+8,cellWidth);
            if(!collapsed)y+=412;
        }
        // Mixer header: correction dropdown + the eight colour ranges. The 24
        // rows stay in the table but only the selected range is on screen.
        if(section==3){
            // No correction dropdown: the three mixer rows are always on screen
            // for the selected colour range; the B&W row appears when the switch
            // is on (that switch IS the black & white mixer).
            place(colorMixerModeId,y,30,collapsed,12,bodyWidthDip-24);
            place(colorBandsId,y+(collapsed?0:34),32,collapsed,12,bodyWidthDip-24);
            if(!collapsed)y+=74;
        }
        // Tone curve: channel tabs + reset, then the grid canvas.
        if(section==2){
            const int tabWidth=std::max(dip(window,40),(bodyWidthDip-24-40-18)/4);
            for(int channel=0;channel<4;++channel)
                place(colorCurveChannelId+channel,y,30,collapsed,12+tabWidth*channel+6*channel,tabWidth);
            place(colorCurveResetId,y,30,collapsed,12+tabWidth*4+24,40);
            place(colorCurveCanvasId,y+36,bodyWidthDip-24-6,collapsed,12,bodyWidthDip-24-6);
            if(!collapsed)y+=36+bodyWidthDip-24+8;
        }
        for(size_t i=0;i<colorParams.size();++i){
            const auto& param=colorParams[i];
            if(param.section!=section)continue;
            const int mode=mixerModeOf(param);
            // Hue/saturation/luminance always visible for the selected band; the
            // black & white row only while the B&W switch is on.
            const bool hidden=collapsed||(mode>=0&&(param.index!=colourMixerBand||(mode==3&&!colourBlackWhite)));
            place(colorLabelId(int(i)),y,24,hidden);
            place(colorEditId(int(i)),y-2,26,hidden);
            place(colorSliderId(int(i)),y+24,16,hidden);
            if(!hidden)y+=46;
        }
        if(section==6){
            place(817,y,200,collapsed);y+=32;
            place(818,y,32,collapsed);y+=36;
            place(819,y,200,collapsed);y+=32;
        }
        // The black & white mixer is a mode, not a slider: one switch in the
        // mixer section turns the eight B&W rows on (HSL rows go inert, exactly
        // like Lightroom's B&W panel).
        if(section==3){place(820,y,220,collapsed);y+=32;}
        y+=8;
    }
    colorPageContentHeight=y+12;
}
void syncColorControls(){
    syncingColour=true;
    const auto colour=colourTarget();
    check(800,colour.enabled?BST_CHECKED:BST_UNCHECKED);
    for(size_t i=0;i<colorParams.size();++i){
        const float value=colorParams[i].value(colour);
        const auto text=std::format(L"{:.2f}",value);
        const int editId=colorEditId(int(i));
        if(auto edit=item(editId))if(!editDrafts.contains(editId)&&GetFocus()!=edit&&windowText(edit)!=text)putText(editId,text.c_str());
        if(auto slider=item(colorSliderId(int(i)))){
            const int scaled=int(std::lround(value*100.0f));
            if(SendMessageW(slider,TBM_GETPOS,0,0)!=scaled)SendMessageW(slider,TBM_SETPOS,TRUE,LPARAM(scaled));
        }
    }
    if(auto space=item(819))if(int(SendMessageW(space,CB_GETCURSEL,0,0))!=colour.lutInputSpace)SendMessageW(space,CB_SETCURSEL,WPARAM(colour.lutInputSpace),0);
    colourBlackWhite=colour.blackWhite;
    check(colorMixerModeId,colourBlackWhite?BST_CHECKED:BST_UNCHECKED);
    if(auto bands=item(colorBandsId))InvalidateRect(bands,nullptr,FALSE);
    if(auto canvas=item(colorCurveCanvasId))InvalidateRect(canvas,nullptr,FALSE);
    for(int channel=0;channel<4;++channel)if(auto tab=item(colorCurveChannelId+channel))selected(tab,channel==colourCurveChannel);
    for(int section=0;section<kColorSections;++section)if(auto eye=item(colorSectionEyeId(section)))InvalidateRect(eye,nullptr,FALSE);
    for(int zone=0;zone<engine::kColorGradingZones;++zone)if(auto wheel=item(colorWheelId(zone)))InvalidateRect(wheel,nullptr,FALSE);
    syncingColour=false;
    syncRowResets();
}
bool applyColour(const engine::ColorSettings& colour,bool autoEnable,bool record=true){
    auto settings=enhancementEnabled?controller->snapshot().desired:configuredSettings;
    const auto previous=settings.color;
    settings.color=colour;
    if(autoEnable)settings.color.enabled=true;
    if(!(settings.color==previous)&&!submit(settings))return false;
    if(record&&settings.color.enabled){
        if(colourHistoryIndex<0)colourHistoryReset(previous);
        colourHistoryPush(settings.color);
    }
    return true;
}
// Native file pickers for the colour page. They run modal on the UI thread, which
// is what a settings dialog is expected to do.
std::wstring pickColourFile(const wchar_t* filter,const wchar_t* title){
    wchar_t buffer[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=window;dialog.lpstrFilter=filter;dialog.lpstrFile=buffer;dialog.nMaxFile=32768;
    dialog.lpstrTitle=title;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog))return {};
    return buffer;
}
std::wstring pickColourSave(const wchar_t* filter,const wchar_t* title,const wchar_t* suggested){
    wchar_t buffer[32768]{};if(suggested)wcsncpy_s(buffer,suggested,_TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=window;dialog.lpstrFilter=filter;dialog.lpstrFile=buffer;dialog.nMaxFile=32768;
    dialog.lpstrTitle=title;dialog.lpstrDefExt=L"vpcolor";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&dialog))return {};
    return buffer;
}
// ---------------------------------------------------------------------------
// Colour wheel (colour-grading zones). One control per zone: a hue/saturation
// disc, a luminance bar under it and a numeric readout - the professional
// grading layout. GDI+ draws on the same alpha surface as the rest of the panel.
// ---------------------------------------------------------------------------
Gdiplus::Bitmap* hueDisc(int size){
    static std::map<int,Gdiplus::Bitmap*> cache;
    const auto found=cache.find(size);
    if(found!=cache.end())return found->second;
    auto* bitmap=new Gdiplus::Bitmap(size,size,PixelFormat32bppPARGB);
    const float radius=size*0.5f;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        const float dx=(float(x)+0.5f)-radius,dy=(float(y)+0.5f)-radius;
        const float distance=std::sqrt(dx*dx+dy*dy)/radius;
        if(distance>1.0f){bitmap->SetPixel(x,y,Gdiplus::Color(0,0,0,0));continue;}
        float hue=std::atan2(dy,dx)*57.2957795f;
        if(hue<0)hue+=360.0f;
        const float saturation=std::min(distance,1.0f);
        const float hp=hue/60.0f,c=saturation,k=c*(1.0f-std::abs(std::fmod(hp,2.0f)-1.0f));
        float r=0,g=0,b=0;
        if(hp<1){r=c;g=k;}else if(hp<2){r=k;g=c;}else if(hp<3){g=c;b=k;}
        else if(hp<4){g=k;b=c;}else if(hp<5){r=k;b=c;}else{r=c;b=k;}
        const float m=1.0f-c;
        bitmap->SetPixel(x,y,Gdiplus::Color(255,BYTE((r+m)*255.0f),BYTE((g+m)*255.0f),BYTE((b+m)*255.0f)));
    }
    cache[size]=bitmap;
    return bitmap;
}
struct WheelGeometry{int size,cx,cy,barLeft,barRight,barY;};
WheelGeometry wheelGeometry(HWND h,const RECT& r){
    WheelGeometry geometry{};
    const int width=int(r.right-r.left);
    // Room for the zone name above, and the luminance bar plus its readout below.
    geometry.size=std::max<int>(dip(h,56),std::min<int>(width-dip(h,22),int(r.bottom-r.top)-dip(h,76)));
    geometry.cx=r.left+width/2;
    geometry.cy=r.top+dip(h,20)+geometry.size/2;
    geometry.barLeft=r.left+dip(h,14);
    geometry.barRight=r.right-dip(h,14);
    geometry.barY=geometry.cy+geometry.size/2+dip(h,18);
    return geometry;
}
void wheelApply(int zone,float hue,float saturation,float luminance,bool record){
    auto colour=colourTarget();
    if(zone<0||zone>=engine::kColorGradingZones)return;
    if(hue>=0)colour.grading[std::size_t(zone)].hue=std::clamp(hue,0.0f,360.0f);
    if(saturation>=0)colour.grading[std::size_t(zone)].saturation=std::clamp(saturation,0.0f,100.0f);
    if(luminance>-999)colour.grading[std::size_t(zone)].luminance=std::clamp(luminance,-100.0f,100.0f);
    applyColour(colour,true,record);
}
void paintColorWheel(HWND h,HDC dc,RECT r){
    const int zone=GetDlgCtrlID(h)-colorWheelId(0);
    if(zone<0||zone>=engine::kColorGradingZones)return;
    const auto colour=colourTarget();
    const auto& wheel=colour.grading[std::size_t(zone)];
    fillSurface(dc,r,h);
    const auto geometry=wheelGeometry(h,r);
    AlphaGraphics drawing(dc);
    auto& graphics=drawing.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    if(auto* disc=hueDisc(geometry.size))graphics.DrawImage(disc,geometry.cx-geometry.size/2,geometry.cy-geometry.size/2,geometry.size,geometry.size);
    Gdiplus::Pen ring(color(RGB(58,60,63)),float(dip(h,1)));
    graphics.DrawEllipse(&ring,geometry.cx-geometry.size/2,geometry.cy-geometry.size/2,geometry.size,geometry.size);
    const float angle=wheel.hue*0.0174532925f,radius=wheel.saturation/100.0f*geometry.size*0.5f;
    const float dotX=geometry.cx+std::cos(angle)*radius,dotY=geometry.cy+std::sin(angle)*radius;
    const int dotRadius=dip(h,6);
    RECT dotRing{int(dotX)-dotRadius-1,int(dotY)-dotRadius-1,int(dotX)+dotRadius+1,int(dotY)+dotRadius+1};
    roundRect(dc,dotRing,RGB(14,15,16),dotRadius+1);
    RECT dot{int(dotX)-dotRadius,int(dotY)-dotRadius,int(dotX)+dotRadius,int(dotY)+dotRadius};
    roundRect(dc,dot,RGB(245,247,249),dotRadius);
    // Luminance bar: accent above the centre, neutral below, so the direction is
    // readable without labels.
    const int mid=(geometry.barLeft+geometry.barRight)/2;
    RECT track{geometry.barLeft,geometry.barY-dip(h,2),geometry.barRight,geometry.barY+dip(h,2)};
    roundRect(dc,track,line,dip(h,3));
    const float fraction=(std::clamp(wheel.luminance,-100.0f,100.0f)+100.0f)/200.0f;
    const int handle=geometry.barLeft+int((geometry.barRight-geometry.barLeft)*fraction);
    if(handle>mid){RECT fill{mid,geometry.barY-dip(h,2),handle,geometry.barY+dip(h,2)};roundRect(dc,fill,accent,dip(h,3));}
    else if(handle<mid){RECT fill{handle,geometry.barY-dip(h,2),mid,geometry.barY+dip(h,2)};roundRect(dc,fill,secondary,dip(h,3));}
    const int thumbRadius=dip(h,5);
    RECT thumb{handle-thumbRadius,geometry.barY-thumbRadius,handle+thumbRadius,geometry.barY+thumbRadius};
    roundRect(dc,thumb,RGB(242,244,246),thumbRadius);
    wchar_t name[64]{};GetWindowTextW(h,name,64);
    wchar_t readout[96]{};swprintf_s(readout,L"H %d°   S %d   L %+d",int(std::lround(wheel.hue)),int(std::lround(wheel.saturation)),int(std::lround(wheel.luminance)));
    const auto previous=SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);
    RECT title{r.left,r.top+dip(h,2),r.right,r.top+dip(h,17)};
    SetTextColor(dc,textColor);DrawTextW(dc,name,-1,&title,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT values{r.left,geometry.barY+dip(h,7),r.right,geometry.barY+dip(h,23)};
    SetTextColor(dc,secondary);DrawTextW(dc,readout,-1,&values,DT_CENTER|DT_SINGLELINE);
    SelectObject(dc,previous);
}
LRESULT CALLBACK colorWheelProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT||msg==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(wp));paintColorWheel(h,paint.dc,paint.rect);return 0;}
    if(msg==WM_LBUTTONDBLCLK){
        wheelApply(GetDlgCtrlID(h)-colorWheelId(0),0,0,0,true);
        syncColorControls();
        return 0;
    }
    if(msg==WM_LBUTTONDOWN||(msg==WM_MOUSEMOVE&&GetCapture()==h)||msg==WM_LBUTTONUP){
        RECT r{};GetClientRect(h,&r);
        const auto geometry=wheelGeometry(h,r);
        const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(msg==WM_LBUTTONDOWN){
            SetFocus(h);SetCapture(h);
            const float dx=float(x-geometry.cx),dy=float(y-geometry.cy);
            const bool inDisc=std::sqrt(dx*dx+dy*dy)<=geometry.size*0.5f+float(dip(h,6));
            const bool onBar=std::abs(y-geometry.barY)<=dip(h,10);
            SetPropW(h,L"veyra.wheelmode",HANDLE(uintptr_t(onBar&&!inDisc?2:(inDisc?1:0))));
        }
        const int mode=int(uintptr_t(GetPropW(h,L"veyra.wheelmode")));
        if(mode==1){
            const float dx=float(x-geometry.cx),dy=float(y-geometry.cy);
            float hue=std::atan2(dy,dx)*57.2957795f;if(hue<0)hue+=360.0f;
            const float saturation=std::min(1.0f,std::sqrt(dx*dx+dy*dy)/(geometry.size*0.5f))*100.0f;
            wheelApply(GetDlgCtrlID(h)-colorWheelId(0),hue,saturation,-999,false);
            InvalidateRect(h,nullptr,FALSE);
        }else if(mode==2){
            const float fraction=std::clamp(float(x-geometry.barLeft)/std::max(1,geometry.barRight-geometry.barLeft),0.0f,1.0f);
            wheelApply(GetDlgCtrlID(h)-colorWheelId(0),-1,-1,fraction*200.0f-100.0f,false);
            InvalidateRect(h,nullptr,FALSE);
        }
        if(msg==WM_LBUTTONUP){
            ReleaseCapture();
            RemovePropW(h,L"veyra.wheelmode");
            colourHistoryPush(colourTarget());
            InvalidateRect(h,nullptr,FALSE);
        }
        return 0;
    }
    if(msg==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_ARROW));return TRUE;}
    return DefWindowProcW(h,msg,wp,lp);
}
void registerColorWheelClass(){
    static bool registered=false;
    if(registered)return;
    registered=true;
    WNDCLASSW classDescription{};
    classDescription.style=CS_DBLCLKS;
    classDescription.lpfnWndProc=colorWheelProc;
    classDescription.hInstance=GetModuleHandleW(nullptr);
    classDescription.lpszClassName=kColorWheelClass;
    classDescription.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&classDescription);
}
// Which of the four mixer corrections a row belongs to (-1 = not a mixer row).
int mixerModeOf(const ColorParam& param){
    switch(param.target){
    case ColorTarget::MixerHue:return 0;
    case ColorTarget::MixerSaturation:return 1;
    case ColorTarget::MixerLuminance:return 2;
    case ColorTarget::BlackWhiteMix:return 3;
    default:break;
    }
    return -1;
}
const COLORREF kBandColours[engine::kColorMixerBands]={
    RGB(226,64,64),RGB(226,152,58),RGB(226,214,58),RGB(74,200,80),
    RGB(58,206,190),RGB(58,116,226),RGB(168,64,214),RGB(226,64,168)};
RECT bandDotRect(HWND h,const RECT& area,int band){
    const int count=engine::kColorMixerBands;
    const int cellWidth=std::max(1,int(area.right-area.left)/count);
    const int radius=std::max(dip(h,7),std::min(dip(h,12),cellWidth/2-dip(h,3)));
    const int centreX=area.left+cellWidth*band+cellWidth/2;
    const int centreY=(area.top+area.bottom)/2;
    return {centreX-radius,centreY-radius,centreX+radius,centreY+radius};
}
void paintColorBands(HWND h,HDC dc,RECT r){
    fillSurface(dc,r,h);
    AlphaGraphics drawing(dc);
    auto& graphics=drawing.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for(int band=0;band<engine::kColorMixerBands;++band){
        const auto dot=bandDotRect(h,r,band);
        const float radius=float(dot.right-dot.left)*0.5f;
        Gdiplus::SolidBrush brush(color(kBandColours[band]));
        graphics.FillEllipse(&brush,float(dot.left),float(dot.top),radius*2,radius*2);
        if(band==colourMixerBand){
            Gdiplus::Pen ring(color(RGB(245,247,249)),float(dip(h,2)));
            graphics.DrawEllipse(&ring,float(dot.left)-radius*0.35f,float(dot.top)-radius*0.35f,radius*2.7f,radius*2.7f);
        }
    }
}
void registerColorBandsClass(){
    static bool registered=false;
    if(registered)return;
    registered=true;
    WNDCLASSW classDescription{};
    classDescription.style=CS_DBLCLKS;
    classDescription.lpfnWndProc=[](HWND h,UINT msg,WPARAM wp,LPARAM lp)->LRESULT{
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_PAINT||msg==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(wp));paintColorBands(h,paint.dc,paint.rect);return 0;}
        if(msg==WM_LBUTTONDOWN){
            RECT r{};GetClientRect(h,&r);
            const int cellWidth=std::max(1,int(r.right-r.left)/engine::kColorMixerBands);
            const int band=std::clamp(GET_X_LPARAM(lp)/cellWidth,0,engine::kColorMixerBands-1);
            if(band!=colourMixerBand){
                colourMixerBand=band;
                veyra::log::info("color-ui",std::format("mixer band={}",band));
            }
            InvalidateRect(h,nullptr,FALSE);
            arrange();
            return 0;
        }
        return DefWindowProcW(h,msg,wp,lp);
    };
    classDescription.hInstance=GetModuleHandleW(nullptr);
    classDescription.lpszClassName=kColorBandsClass;
    classDescription.hCursor=LoadCursorW(nullptr,IDC_HAND);
    RegisterClassW(&classDescription);
}
// ---------------------------------------------------------------------------
// Tone curve editor (plan section 3.3): a real grid canvas with draggable
// control points for the RGB / R / G / B curves, drawn with the same
// piecewise-linear response the bake uses so the preview cannot lie.
// ---------------------------------------------------------------------------
engine::ColorCurve& curveForChannel(engine::ColorSettings& colour,int channel){
    return colour.curves[std::size_t(std::clamp(channel,0,3))];
}
RECT curveCanvasRect(HWND h,const RECT& area){
    // Inset by the control-point radius so the (0,0) and (1,1) anchors are not
    // clipped by the canvas border.
    const int inset=dip(h,7);
    const int size=std::max(dip(h,120),std::min(int(area.right-area.left)-inset*2,int(area.bottom-area.top)-inset*2));
    return {area.left+inset,area.top+inset,area.left+inset+size,area.top+inset+size};
}
POINT curvePointToScreen(HWND h,const RECT& canvas,const engine::ColorCurvePoint& point){
    return {canvas.left+int(point.x*float(canvas.right-canvas.left)),
            canvas.bottom-int(point.y*float(canvas.bottom-canvas.top))};
}
void paintToneCurve(HWND h,HDC dc,RECT r){
    fillSurface(dc,r,h);
    const auto colour=colourTarget();
    const auto canvas=curveCanvasRect(h,r);
    AlphaGraphics drawing(dc);
    auto& graphics=drawing.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    // Backdrop + quarter grid.
    Gdiplus::SolidBrush backdrop(color(RGB(18,19,20)));
    graphics.FillRectangle(&backdrop,Gdiplus::RectF(float(canvas.left),float(canvas.top),float(canvas.right-canvas.left),float(canvas.bottom-canvas.top)));
    Gdiplus::Pen grid(color(RGB(52,54,57)),1.0f);
    for(int division=1;division<4;++division){
        const int x=canvas.left+(canvas.right-canvas.left)*division/4;
        const int y=canvas.top+(canvas.bottom-canvas.top)*division/4;
        graphics.DrawLine(&grid,Gdiplus::PointF(float(x),float(canvas.top)),Gdiplus::PointF(float(x),float(canvas.bottom)));
        graphics.DrawLine(&grid,Gdiplus::PointF(float(canvas.left),float(y)),Gdiplus::PointF(float(canvas.right),float(y)));
    }
    Gdiplus::Pen border(color(line),1.0f);
    graphics.DrawRectangle(&border,Gdiplus::RectF(float(canvas.left),float(canvas.top),float(canvas.right-canvas.left-1),float(canvas.bottom-canvas.top-1)));
    Gdiplus::Pen identity(color(RGB(70,72,76)),1.0f);
    graphics.DrawLine(&identity,Gdiplus::PointF(float(canvas.left),float(canvas.bottom)),Gdiplus::PointF(float(canvas.right),float(canvas.top)));
    const COLORREF curveColours[4]={RGB(236,238,240),RGB(232,88,88),RGB(88,214,120),RGB(96,150,244)};
    for(int channel=0;channel<4;++channel){
        const auto& curve=colour.curves[std::size_t(channel)];
        const bool active=channel==colourCurveChannel;
        Gdiplus::Pen pen(color(curveColours[channel],active?255:70),active?2.0f:1.0f);
        // Seed the polyline with the curve's real value at x=0. Seeding it with
        // the canvas corner drew a fake near-vertical segment at the shadow end
        // (the "why is there an angle down here" report) that the highlight end
        // never showed, because the loop happens to end exactly at x=1.
        int previousX=canvas.left;
        int previousY=canvas.bottom-int(std::clamp(veyra::pipeline::ColorGradeTables::curveValue(curve,0.0f),0.0f,1.0f)*float(canvas.bottom-canvas.top));
        for(int step=1;step<=64;++step){
            const float t=float(step)/64.0f;
            const float value=veyra::pipeline::ColorGradeTables::curveValue(curve,t);
            const int x=canvas.left+int(t*float(canvas.right-canvas.left));
            const int y=canvas.bottom-int(std::clamp(value,0.0f,1.0f)*float(canvas.bottom-canvas.top));
            graphics.DrawLine(&pen,Gdiplus::PointF(float(previousX),float(previousY)),Gdiplus::PointF(float(x),float(y)));
            previousX=x;previousY=y;
        }
    }
    // Control points of the active channel.
    const auto& active=colour.curves[std::size_t(colourCurveChannel)];
    for(int index=0;index<active.count;++index){
        const auto screen=curvePointToScreen(h,canvas,active.points[std::size_t(index)]);
        const int radius=dip(h,5);
        Gdiplus::SolidBrush brush(color(index==colourCurveDragPoint?accent:RGB(240,242,245)));
        graphics.FillEllipse(&brush,float(screen.x-radius),float(screen.y-radius),float(radius*2),float(radius*2));
        Gdiplus::Pen outline(color(RGB(14,15,16)),1.0f);
        graphics.DrawEllipse(&outline,float(screen.x-radius),float(screen.y-radius),float(radius*2),float(radius*2));
    }
    // Readout for the dragged (or last) point, in 0..255 units.
    const int reference=colourCurveDragPoint>=0?colourCurveDragPoint:std::min(1,active.count-1);
    if(reference>=0&&reference<active.count){
        const auto& point=active.points[std::size_t(reference)];
        wchar_t text[96]{};swprintf_s(text,L"Input %d   Output %d",int(std::lround(point.x*255.0f)),int(std::lround(point.y*255.0f)));
        const auto previous=SelectObject(dc,font);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,secondary);
        RECT textRect{canvas.left,canvas.bottom+dip(h,3),canvas.right,canvas.bottom+dip(h,19)};
        DrawTextW(dc,text,-1,&textRect,DT_CENTER|DT_SINGLELINE);
        SelectObject(dc,previous);
    }
}
void registerToneCurveClass(){
    static bool registered=false;
    if(registered)return;
    registered=true;
    WNDCLASSW classDescription{};
    classDescription.style=CS_DBLCLKS;
    classDescription.hInstance=GetModuleHandleW(nullptr);
    classDescription.lpszClassName=kColorCurveClass;
    classDescription.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    classDescription.lpfnWndProc=[](HWND h,UINT msg,WPARAM wp,LPARAM lp)->LRESULT{
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_PAINT||msg==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(wp));paintToneCurve(h,paint.dc,paint.rect);return 0;}
        const auto hitTest=[&](POINT point,int& index){
            RECT r{};GetClientRect(h,&r);
            const auto canvas=curveCanvasRect(h,r);
            const auto colour=colourTarget();
            const auto& curve=colour.curves[std::size_t(colourCurveChannel)];
            index=-1;int best=dip(h,9);
            for(int i=0;i<curve.count;++i){
                const auto screen=curvePointToScreen(h,canvas,curve.points[std::size_t(i)]);
                const int distance=std::max(std::abs(screen.x-point.x),std::abs(screen.y-point.y));
                if(distance<=best){best=distance;index=i;}
            }
            return canvas;
        };
        if(msg==WM_LBUTTONDOWN||(msg==WM_MOUSEMOVE&&GetCapture()==h)||msg==WM_LBUTTONUP){
            RECT r{};GetClientRect(h,&r);
            const auto canvas=curveCanvasRect(h,r);
            POINT cursor{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(msg==WM_LBUTTONDOWN){
                int index=-1;hitTest(cursor,index);
                if(index<0){
                    // Clicking the canvas inserts a point on the spot (Lightroom).
                    auto colour=colourTarget();
                    auto& curve=curveForChannel(colour,colourCurveChannel);
                    if(curve.count<engine::kColorCurvePoints){
                        engine::ColorCurvePoint added{};
                        added.x=std::clamp(float(cursor.x-canvas.left)/float(std::max<int>(1,canvas.right-canvas.left)),0.0f,1.0f);
                        added.y=std::clamp(float(canvas.bottom-cursor.y)/float(std::max<int>(1,canvas.bottom-canvas.top)),0.0f,1.0f);
                int slot=curve.count;
                        for(int i=0;i<curve.count;++i)if(curve.points[std::size_t(i)].x>added.x){slot=i;break;}
                        for(int i=curve.count;i>slot;--i)curve.points[std::size_t(i)]=curve.points[std::size_t(i-1)];
                        curve.points[std::size_t(slot)]=added;++curve.count;
                        index=slot;
                        applyColour(colour,true,false);
                    }
                }
                colourCurveDragPoint=index;
                SetFocus(h);SetCapture(h);
                veyra::log::info("color-ui",std::format("curve channel={} point={} count={}",colourCurveChannel,index,colourTarget().curves[std::size_t(colourCurveChannel)].count));
            }else if(msg==WM_MOUSEMOVE&&colourCurveDragPoint>=0){
                auto colour=colourTarget();
                auto& curve=curveForChannel(colour,colourCurveChannel);
                const int index=std::clamp(colourCurveDragPoint,0,curve.count-1);
                colourCurveDragPoint=index;
                const float y=std::clamp(float(canvas.bottom-cursor.y)/float(std::max<int>(1,canvas.bottom-canvas.top)),0.0f,1.0f);
                // Endpoints move freely too (Lightroom's black/white points can
                // be pulled inward), they are only bounded by their neighbour so
                // the control points stay sorted left to right.
                const float rawX=float(cursor.x-canvas.left)/float(std::max<int>(1,canvas.right-canvas.left));
                const float lower=index>0?curve.points[std::size_t(index-1)].x+0.01f:0.0f;
                const float upper=index<curve.count-1?curve.points[std::size_t(index+1)].x-0.01f:1.0f;
                curve.points[std::size_t(index)]={std::clamp(rawX,lower,upper),y};
                applyColour(colour,true,false);
            }else if(msg==WM_LBUTTONUP){
                ReleaseCapture();
                if(colourCurveDragPoint>=0)colourHistoryPush(colourTarget());
                colourCurveDragPoint=-1;
            }
            InvalidateRect(h,nullptr,FALSE);
            return 0;
        }
        if(msg==WM_LBUTTONDBLCLK){
            POINT cursor{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            int index=-1;hitTest(cursor,index);
            // Double-click on a point deletes it; the two anchors stay.
            if(index>0){
                auto colour=colourTarget();
                auto& curve=curveForChannel(colour,colourCurveChannel);
                if(curve.count>2&&index<curve.count-1){
                    for(int i=index;i<curve.count-1;++i)curve.points[std::size_t(i)]=curve.points[std::size_t(i+1)];
                    --curve.count;
                    if(applyColour(colour,true))veyra::log::info("color-ui",std::format("curve point deleted channel={} count={}",colourCurveChannel,curve.count));
                }
                colourHistoryPush(colourTarget());
                InvalidateRect(h,nullptr,FALSE);
            }
            return 0;
        }
        if(msg==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}
        return DefWindowProcW(h,msg,wp,lp);
    };
    RegisterClassW(&classDescription);
}
// Section bypass eye: open = active, struck through + dimmed = bypassed.
void paintSectionEye(HWND h,HDC dc,RECT r){
    const int section=GetDlgCtrlID(h)-colorSectionEyeId(0);
    const bool bypassed=(colourTarget().groupBypassMask&(1u<<unsigned(std::max(0,section))))!=0;
    fillSurface(dc,r,h);
    AlphaGraphics drawing(dc);
    auto& graphics=drawing.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float centreX=float(r.right-r.left)*0.5f,centreY=float(r.bottom-r.top)*0.5f;
    const float halfWidth=float(std::min(r.right-r.left,r.bottom-r.top))*0.38f,halfHeight=halfWidth*0.58f;
    const COLORREF tint=bypassed?RGB(96,99,104):RGB(214,218,224);
    Gdiplus::Pen outline(color(tint),1.6f);
    Gdiplus::GraphicsPath path;
    path.AddBezier(centreX-halfWidth,centreY,centreX-halfWidth*0.35f,centreY-halfHeight,
                   centreX+halfWidth*0.35f,centreY-halfHeight,centreX+halfWidth,centreY);
    path.AddBezier(centreX+halfWidth,centreY,centreX+halfWidth*0.35f,centreY+halfHeight,
                   centreX-halfWidth*0.35f,centreY+halfHeight,centreX-halfWidth,centreY);
    path.CloseFigure();
    graphics.DrawPath(&outline,&path);
    Gdiplus::SolidBrush pupil(color(tint));
    const float pupilRadius=halfHeight*0.55f;
    graphics.FillEllipse(&pupil,centreX-pupilRadius,centreY-pupilRadius,pupilRadius*2,pupilRadius*2);
    if(bypassed){
        Gdiplus::Pen strike(color(RGB(230,122,49)),1.8f);
        graphics.DrawLine(&strike,centreX-halfWidth,centreY+halfHeight*1.15f,centreX+halfWidth,centreY-halfHeight*1.15f);
    }
}
void registerSectionEyeClass(){
    static bool registered=false;
    if(registered)return;
    registered=true;
    WNDCLASSW classDescription{};
    classDescription.hInstance=GetModuleHandleW(nullptr);
    classDescription.lpszClassName=kColorEyeClass;
    classDescription.hCursor=LoadCursorW(nullptr,IDC_HAND);
    classDescription.lpfnWndProc=[](HWND h,UINT msg,WPARAM wp,LPARAM lp)->LRESULT{
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_PAINT||msg==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(wp));paintSectionEye(h,paint.dc,paint.rect);return 0;}
        if(msg==WM_LBUTTONDOWN){
            const int section=GetDlgCtrlID(h)-colorSectionEyeId(0);
            auto colour=colourTarget();
            colour.groupBypassMask^=1u<<unsigned(std::max(0,section));
            if(applyColour(colour,true)){
                veyra::log::info("color-ui",std::format("section bypass section={} mask={}",section,colour.groupBypassMask));
                message(colour.groupBypassMask&(1u<<unsigned(section))
                    ?std::format(L"\"{}\" group temporarily disabled (values kept; uncheck to restore).",colorSectionName(section))
                    :std::format(L"\"{}\" group enabled.",colorSectionName(section)));
            }
            syncColorControls();
            return 0;
        }
        return DefWindowProcW(h,msg,wp,lp);
    };
    RegisterClassW(&classDescription);
}
bool colourFieldEdited(int index,float value){
    if(index<0||size_t(index)>=colorParams.size())return false;
    auto colour=colourTarget();
    if(!std::isfinite(value)||value<colorParams[size_t(index)].min||value>colorParams[size_t(index)].max){
        message(L"Value out of range; keeping last valid value");syncColorControls();return false;
    }
    colorParams[size_t(index)].set(colour,value);
    if(!applyColour(colour,true)){
        veyra::log::warn("color-ui",std::format("colour edit rejected index={} value={:.3f}",index,value));
        syncColorControls();
        return false;
    }
    veyra::log::info("color-ui",std::format("colour edit applied index={} value={:.3f}",index,value));
    syncColorControls();
    return true;
}
void message(const std::wstring& text){static std::wstring last;if(text==last)return;last=text;if(statusSink)statusSink(text);else putText(401,text.c_str());}
bool submit(engine::EnhancementSettings s){if(!apply(s)){dirty=true;message(L"Change not accepted; check the status bar or log. If the master enhancement is switching, try again shortly.");return false;}dirty=false;return true;}
void syncProtection(const engine::ProtectionSettings& protection){
    const bool wasPopulating=populating;populating=true;
    check(206,protection.enabled?BST_CHECKED:BST_UNCHECKED);unsigned count=0;for(auto q:protection.regions)count+=!q.empty();
    putText(206,(L"NR exclusion zone · "+std::to_wstring(count)+L"/4").c_str());
    // The feather value is stored in working-extent pixels (see
    // NrResidualComposite.hlsl); the panel only converts it for display.
    const float feather=std::clamp(protection.featherPixels,0.0f,64.0f);
    if(auto slider=item(622))SendMessageW(slider,TBM_SETPOS,TRUE,LPARAM(std::lround(feather)));
    if(item(222)&&!editDrafts.contains(222)&&GetFocus()!=item(222))putText(222,std::to_wstring(int(std::lround(feather))).c_str());
    if(auto label=item(1123)){
        const unsigned extent=controller?controller->snapshot().metrics.resolution.base.height:0u;
        if(extent>0)putText(1123,std::format(L"Feather {} px · ~{:.1f}% of frame height",int(std::lround(feather)),feather*100.0f/float(extent)).c_str());
        else putText(1123,std::format(L"Feather {} px (working-resolution pixels)",int(std::lround(feather))).c_str());
    }
    populating=wasPopulating;
}
void selectDiscrete(int group,int value){for(int j=0;j<(group==0?3:2);++j){auto h=item(700+group*10+j);if(j==value)SetPropW(h,L"veyra.selected",HANDLE(1));else RemovePropW(h,L"veyra.selected");InvalidateRect(h,nullptr,FALSE);}}
// Multiplier list is capability-driven: the DLSS runtime reports how many
// generated frames it supports (1 = 2X only on Ada, 5 = 6X on Blackwell), and
// the XeSS unlock path raises its own ceiling. Unknown capability offers the
// full list; initialization validates a newly requested XeSS context after
// applying its unlock, rather than treating the current 2X mode as a limit.
int multiplierChoiceCount(engine::FrameGenerationBackend backend){
    int cap=6;
    if(backend==engine::FrameGenerationBackend::XeSS){
        // Stock provider is 2X only; the audited OptiScaler unlock raises it to
        // 4X. Hash the provider once, and prefer the ceiling an actual session
        // already reported.
        static const bool providerAudited=[](){
            const auto path=veyra::runtime::localDataDirectory()/L"intel"/L"experimental"/L"libxess_fg.dll";
            return veyra::gfx::XessMfgUnlock::providerIsAudited(path.wstring());
        }();
        cap=providerAudited?4:2;
        if(controller){const auto snapshot=controller->snapshot();if(snapshot.xessMaxInterpolatedFrames>1)cap=std::clamp(snapshot.xessMaxInterpolatedFrames+1,2,4);}
    }
    else if(backend==engine::FrameGenerationBackend::Fsr){
        // AMD 3.1.x frame generation delivers one generated frame per present;
        // tools/fsr_probe measured the same count for 2/3/4 requested frames.
        cap=2;
        if(controller){const auto snapshot=controller->snapshot();if(snapshot.fsrMaxGeneratedFrames>0)cap=std::clamp(int(snapshot.fsrMaxGeneratedFrames)+1,2,2);}
    }
    else if(controller){const auto snapshot=controller->snapshot();if(snapshot.fgMultiFrameMax>0)cap=std::clamp(snapshot.fgMultiFrameMax+1,2,6);}
    int count=1;
    for(size_t i=1;i<engine::kFgMultiplierChoiceCount;++i)if(int(engine::kFgMultiplierChoices[i])<=cap)++count;
    return count;
}
int multiplierChoiceIndex(uint32_t multiplier){for(size_t i=0;i<engine::kFgMultiplierChoiceCount;++i)if(engine::kFgMultiplierChoices[i]==multiplier)return int(i);return 0;}
void populate(engine::EnhancementSettings s){
    populating=true;
    float v[]={s.model.intensity,s.model.tone,s.model.structure,s.model.skin,float(s.model.style),float(s.model.autoMask),float(s.model.uiCorrection),s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance};
    for(int i=0;i<12;++i){std::wostringstream o;o<<std::setprecision(7)<<v[i];if(i>=4&&i<=6){selectDiscrete(i-4,int(v[i]));continue;}if(!editDrafts.contains(100+i)&&GetFocus()!=item(100+i))putText(100+i,o.str().c_str());send(600+i,TBM_SETPOS,TRUE,LPARAM(v[i]*100));}
    syncProtection(s.protection);
    check(200,enhancementEnabled&&s.nr?BST_CHECKED:BST_UNCHECKED);
    check(201,enhancementEnabled&&s.sr?BST_CHECKED:BST_UNCHECKED);
    for(int j=0;j<3;++j){auto h=item(730+j);if(j==int(s.srTarget))SetPropW(h,L"veyra.selected",HANDLE(1));else RemovePropW(h,L"veyra.selected");InvalidateRect(h,nullptr,FALSE);}
    const int multiplierCount=multiplierChoiceCount(s.frameGenerationBackend);
    if(send(202,CB_GETCOUNT)!=multiplierCount){send(202,CB_RESETCONTENT);const wchar_t* choices[]={L"Frame generation off",L"2X · one interpolated frame",L"3X · two interpolated frames",L"4X · three interpolated frames",L"6X · five interpolated frames"};for(int i=0;i<multiplierCount;++i)send(202,CB_ADDSTRING,0,LPARAM(choices[i]));}
    send(207,CB_SETCURSEL,s.videoSrQuality,0);send(202,CB_SETCURSEL,multiplierChoiceIndex(s.multiplier),0);
    // 1.4.0: the AMD FSR frame-generation entry is gone from the panel. An old
    // last-applied preset (or a command line) that still asks for it must not
    // strand the session on a backend the UI can no longer change - fall back to
    // DLSS, tell the user, and write it to the log. The engine-side backend is
    // untouched (headless probes and --fg-fsr still exercise it).
    if(s.frameGenerationBackend==engine::FrameGenerationBackend::Fsr){
        s.frameGenerationBackend=engine::FrameGenerationBackend::Dlss;
        if(controller)controller->requestSettings(s);
        message(L"AMD FSR frame generation was removed from the UI in 1.4.0 (backend kept): switched back to DLSS frame generation this time.");
        veyra::log::warn("settings","AMD FSR frame generation is no longer selectable in the UI; falling back to DLSS");
    }
    send(208,CB_SETCURSEL,int(s.frameGenerationBackend),0);send(203,CB_SETCURSEL,int(s.nrPolicy),0);
    send(508,CB_SETCURSEL,int(engine::exportBitrateIndex(s.exportBitrateMbps)),0);
    send(218,CB_SETCURSEL,int(s.nrRuntime));
    check(219,s.captureCompatible?BST_CHECKED:BST_UNCHECKED);check(220,s.lowLatency?BST_CHECKED:BST_UNCHECKED);
    check(223,s.nrTemporal?BST_CHECKED:BST_UNCHECKED);
    check(230,enhancementEnabled&&s.videoHdr.enabled?BST_CHECKED:BST_UNCHECKED);
    const unsigned hdrValues[]={s.videoHdr.contrast,s.videoHdr.saturation,s.videoHdr.middleGray,s.videoHdr.peakNits};
    for(int i=0;i<4;++i){send(631+i,TBM_SETPOS,TRUE,hdrValues[i]);putText(1131+i,(std::to_wstring(hdrValues[i])+(i==3?L" nit":L"")).c_str());}
    send(204,CB_SETCURSEL,int(s.flow),0);send(205,CB_SETCURSEL,int(s.content),0);
    send(209,CB_SETCURSEL,int(s.opticalFlowBackend),0);
    check(215,s.amdFlowHalfResolution?BST_CHECKED:BST_UNCHECKED);
    check(210,s.fgStrictAdmission?BST_CHECKED:BST_UNCHECKED);
    send(216,CB_SETCURSEL,int(s.audioSync));
    if(!editDrafts.contains(217)&&GetFocus()!=item(217))putText(217,std::to_wstring(s.audioOffsetMs).c_str());
    EnableWindow(item(217),s.audioSync==engine::AudioSyncMode::Manual);
    EnableWindow(item(204),s.opticalFlowBackend==engine::OpticalFlowBackend::Nvidia);
    EnableWindow(item(215),s.opticalFlowBackend==engine::OpticalFlowBackend::AmdFidelityFx);
    displayedRevision=s.revision;displayedSettings=s;populating=false;dirty=false;
    syncColorControls();
}
bool read(engine::EnhancementSettings& s,bool allPages=false){s=enhancementEnabled?controller->snapshot().desired:configuredSettings;float v[12]{};for(int i=0;i<12;++i){if(i>=4&&i<=6){v[i]=float(i==4?s.model.style:i==5?s.model.autoMask:s.model.uiCorrection);continue;}wchar_t b[64]{};GetWindowTextW(item(100+i),b,64);wchar_t* end=nullptr;v[i]=wcstof(b,&end);if(end==b||*end||!std::isfinite(v[i])){message(L"Enter a complete, finite value; settings not submitted");return false;}}
    for(int i=4;i<7;++i)if(v[i]!=std::floor(v[i])||v[i]<0||v[i]>(i==4?2:1)){message(L"Style/mask/UI correction must be integers");return false;}
    s.model={v[0],v[1],v[2],v[3],int(v[4]),int(v[5]),int(v[6])};s.residual={v[7],v[8],v[9],v[10],v[11]};if(enhancementEnabled){s.nr=checked(200)==BST_CHECKED;s.sr=checked(201)==BST_CHECKED;}s.videoSrQuality=uint32_t(send(207,CB_GETCURSEL,0,0));s.nrPolicy=static_cast<pipeline::NrSizePolicy>(send(203,CB_GETCURSEL,0,0));if(allPages){
        const auto multiplier=send(202,CB_GETCURSEL,0,0),generation=send(208,CB_GETCURSEL,0,0),flowBackend=send(209,CB_GETCURSEL,0,0),flowQuality=send(204,CB_GETCURSEL,0,0),content=send(205,CB_GETCURSEL,0,0);
        if(multiplier==CB_ERR||generation==CB_ERR||flowBackend==CB_ERR||flowQuality==CB_ERR||content==CB_ERR){message(L"Settings controls not fully initialized; settings not saved");return false;}
        s.multiplier=(multiplier>=0&&multiplier<int(engine::kFgMultiplierChoiceCount))?engine::kFgMultiplierChoices[multiplier]:1;s.frameGenerationBackend=static_cast<engine::FrameGenerationBackend>(generation);s.opticalFlowBackend=static_cast<engine::OpticalFlowBackend>(flowBackend);s.amdFlowHalfResolution=checked(215)==BST_CHECKED;s.flow=static_cast<engine::FlowQuality>(flowQuality);s.content=static_cast<engine::ContentRate>(content);
        {const int bitrate=send(508,CB_GETCURSEL,0,0);if(bitrate==CB_ERR||bitrate<0||bitrate>=int(engine::kExportBitrateChoiceCount)){message(L"Export bitrate control not fully initialized; settings not saved");return false;}s.exportBitrateMbps=engine::kExportBitrateChoices[bitrate];}
        s.audioSync=static_cast<engine::AudioSyncMode>(send(216,CB_GETCURSEL));
        s.nrRuntime=static_cast<engine::NrRuntime>(send(218,CB_GETCURSEL));
        s.captureCompatible=checked(219)==BST_CHECKED;s.lowLatency=checked(220)==BST_CHECKED;s.nrTemporal=checked(223)==BST_CHECKED;
        wchar_t offset[32]{};GetWindowTextW(item(217),offset,32);wchar_t* offsetEnd=nullptr;const auto parsed=wcstol(offset,&offsetEnd,10);
        if(offsetEnd==offset||*offsetEnd||parsed<-250||parsed>250){message(L"Audio offset must be between -250 and 250 ms");return false;}s.audioOffsetMs=int(parsed);
        for(int j=0;j<3;++j)if(GetPropW(item(730+j),L"veyra.selected")){s.srTarget=static_cast<pipeline::SrTarget>(j);break;}
        if(engine::presentSinkFrameGeneration(s.frameGenerationBackend)){
            const int choices=multiplierChoiceCount(s.frameGenerationBackend);
            const size_t index=size_t(std::clamp(choices-1,1,int(engine::kFgMultiplierChoiceCount)-1));
            s.multiplier=std::min(s.multiplier,engine::kFgMultiplierChoices[index]);
        }
    }if(!s.validate().empty()){message(L"Values out of range; not submitted. Hover a value box to see the allowed range.");return false;}return true;}

// Each notification changes one field on the latest desired settings. Hidden
// controls and incomplete numeric text can never overwrite another field.
bool liveField(int id){
    const bool numeric=(id>=100&&id<=111)||id==217||id==222;
    if(numeric){editDrafts.insert(id);syncRowResets();}
    if(id==202){
        const auto index=send(id,CB_GETCURSEL);if(index==CB_ERR)return false;
        const uint32_t requested=(index>=0&&index<int(engine::kFgMultiplierChoiceCount))?engine::kFgMultiplierChoices[index]:1;
        const bool accepted=SendMessageW(GetParent(window),WM_APP+44,202,requested)!=0;
        populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);
        message(accepted?L"Frame generation requested; no need to enable NR first.":L"The master enhancement is switching; wait for the current transaction to finish.");return accepted;
    }
    auto s=enhancementEnabled?controller->snapshot().desired:configuredSettings;
    if(id>=100&&id<=111){
        wchar_t b[64]{};GetWindowTextW(item(id),b,64);wchar_t* end=nullptr;float v=wcstof(b,&end);
        if(end==b||*end||!std::isfinite(v)){message(L"Value incomplete; keeping last valid value");return false;}
        switch(id){case 100:s.model.intensity=v;break;case 101:s.model.tone=v;break;case 102:s.model.structure=v;break;case 103:s.model.skin=v;break;
        case 107:s.residual.total=v;break;case 108:s.residual.darken=v;break;case 109:s.residual.brighten=v;break;case 110:s.residual.color=v;break;case 111:s.residual.luminance=v;break;default:return false;}
    }else switch(id){
        case 219:s.captureCompatible=checked(id)==BST_CHECKED;break;
        case 218:s.nrRuntime=static_cast<engine::NrRuntime>(send(id,CB_GETCURSEL));break;
        case 203:s.nrPolicy=static_cast<pipeline::NrSizePolicy>(send(id,CB_GETCURSEL));break;
        case 204:s.flow=static_cast<engine::FlowQuality>(send(id,CB_GETCURSEL));break;
        case 205:s.content=static_cast<engine::ContentRate>(send(id,CB_GETCURSEL));break;
        case 208:{s.frameGenerationBackend=static_cast<engine::FrameGenerationBackend>(send(id,CB_GETCURSEL));if(engine::presentSinkFrameGeneration(s.frameGenerationBackend)){const int choices=multiplierChoiceCount(s.frameGenerationBackend);const size_t index=size_t(std::clamp(choices-1,1,int(engine::kFgMultiplierChoiceCount)-1));s.multiplier=std::min(s.multiplier,engine::kFgMultiplierChoices[index]);}break;}
        case 209:s.opticalFlowBackend=static_cast<engine::OpticalFlowBackend>(send(id,CB_GETCURSEL));break;
        case 220:s.lowLatency=checked(id)==BST_CHECKED;break;
        case 223:s.nrTemporal=checked(id)==BST_CHECKED;break;

        case 631:s.videoHdr.contrast=unsigned(send(id,TBM_GETPOS));break;
        case 632:s.videoHdr.saturation=unsigned(send(id,TBM_GETPOS));break;
        case 633:s.videoHdr.middleGray=unsigned(send(id,TBM_GETPOS));break;
        case 634:s.videoHdr.peakNits=unsigned(send(id,TBM_GETPOS));break;
        case 215:s.amdFlowHalfResolution=checked(id)==BST_CHECKED;break;
        case 210:s.fgStrictAdmission=checked(id)==BST_CHECKED;break;
        case 216:s.audioSync=static_cast<engine::AudioSyncMode>(send(id,CB_GETCURSEL));break;
        case 217:{wchar_t value[32]{};GetWindowTextW(item(id),value,32);wchar_t* end=nullptr;const auto parsed=wcstol(value,&end,10);if(end==value||*end||parsed<-250||parsed>250){message(L"Audio offset must be between -250 and 250 ms");return false;}s.audioOffsetMs=int(parsed);break;}
        case 222:{wchar_t value[32]{};GetWindowTextW(item(id),value,32);wchar_t* end=nullptr;const auto parsed=wcstol(value,&end,10);if(end==value||*end||parsed<0||parsed>64){message(L"Exclusion-zone feather must be 0 to 64 pixels");return false;}s.protection.featherPixels=float(parsed);break;}
        case 207:s.videoSrQuality=uint32_t(send(id,CB_GETCURSEL));break;
        case 508:{const int index=send(id,CB_GETCURSEL,0,0);if(index==CB_ERR||index<0||index>=int(engine::kExportBitrateChoiceCount))return false;s.exportBitrateMbps=engine::kExportBitrateChoices[index];break;}
        case 700:case 701:case 702:s.model.style=id-700;break;
        case 710:case 711:s.model.autoMask=id-710;break;
        case 720:case 721:s.model.uiCorrection=id-720;break;
        case 730:case 731:case 732:s.srTarget=static_cast<pipeline::SrTarget>(id-730);break;
        default:return false;
    }
    if(!s.validate().empty()){message(L"Value out of range; keeping last valid value");return false;}
    if(!submit(s)){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);return false;}
    if(numeric)editDrafts.erase(id);
    populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);
    message(enhancementEnabled?L"Live · reflects the applied version":L"Enhancement off · settings saved and pending enable");return true;
}
LRESULT CALLBACK scrollOnly(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(msg==WM_MOUSEWHEEL||msg==WM_MOUSEHWHEEL){if(window)SendMessageW(window,WM_MOUSEWHEEL,wp,lp);return 0;}
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,scrollOnly,id);
    return DefSubclassProc(h,msg,wp,lp);
}
// Slider rows (plan section 3.3): double-click or Home returns the row to its
// neutral value, exactly like Lightroom's double-click reset.
LRESULT CALLBACK colourSliderKeys(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(msg==WM_PAINT||msg==WM_PRINTCLIENT){
        // Lightroom-style rail: the colour-relevant rows show a gradient track
        // (blue->yellow for temperature, green->magenta for tint, blue->green->
        // red for saturation, a rainbow for the hue rows); everything else keeps
        // the plain rail with the accent fill up to the thumb.
        const int index=GetDlgCtrlID(h)-colorSliderId(0);
        PaintBuffer paint(h,reinterpret_cast<HDC>(wp));
        HDC dc=paint.dc;RECT r=paint.rect;
        fillSurface(dc,r,h);
        const int minimum=int(SendMessageW(h,TBM_GETRANGEMIN,0,0)),maximum=int(SendMessageW(h,TBM_GETRANGEMAX,0,0));
        const float progress=float(SendMessageW(h,TBM_GETPOS,0,0)-minimum)/std::max(1,maximum-minimum);
        const int margin=dip(h,8),y=r.bottom/2;
        RECT rail{margin,y-dip(h,2),r.right-margin,y+dip(h,2)};
        const int gradient=(index>=0&&index<int(colorParams.size()))?colorParams[size_t(index)].gradient:0;
        struct Stop{float at;COLORREF colour;};
        auto stopsFor=[&](int kind,std::vector<Stop>& out){
            switch(kind){
            case 1:out={{0,RGB(30,110,205)},{1,RGB(255,206,110)}};break;
            case 2:out={{0,RGB(52,180,105)},{1,RGB(222,68,158)}};break;
            case 3:out={{0,RGB(38,86,214)},{0.33f,RGB(58,190,92)},{0.66f,RGB(232,198,58)},{1,RGB(228,86,58)}};break;
            case 4:out={{0,RGB(226,64,64)},{0.17f,RGB(226,190,58)},{0.33f,RGB(70,200,80)},{0.5f,RGB(58,200,206)},{0.67f,RGB(58,110,226)},{0.83f,RGB(190,64,214)},{1,RGB(226,64,64)}};break;
            default:break;
            }
        };
        std::vector<Stop> stops;stopsFor(gradient,stops);
        const int x=margin+int((r.right-margin*2)*progress);
        if(stops.empty()){
            RECT fill=rail;fill.right=x;
            roundRect(dc,rail,line,dip(h,3));
            if(fill.right>fill.left)roundRect(dc,fill,IsWindowEnabled(h)?accent:secondary,dip(h,3));
        }else{
            // Paint the gradient as thin columns; the panel is tiny compared to a
            // full frame, so this stays a few hundred GDI calls at most.
            const int width=std::max<int>(1,r.right-margin-rail.left);
            for(int i=0;i<width;++i){
                const float t=float(i)/float(std::max<int>(1,width-1));
                size_t band=0;while(band+2<stops.size()&&t>stops[band+1].at)++band;
                const auto& a=stops[band];const auto& b=stops[std::min(band+1,stops.size()-1)];
                const float span=std::max(1e-4f,b.at-a.at),local=std::clamp((t-a.at)/span,0.0f,1.0f);
                const COLORREF colour=RGB(int(GetRValue(a.colour)+(GetRValue(b.colour)-GetRValue(a.colour))*local),
                                          int(GetGValue(a.colour)+(GetGValue(b.colour)-GetGValue(a.colour))*local),
                                          int(GetBValue(a.colour)+(GetBValue(b.colour)-GetBValue(a.colour))*local));
                RECT column{rail.left+i,rail.top,rail.left+i+1,rail.bottom};
                HBRUSH brush=CreateSolidBrush(colour);FillRect(dc,&column,brush);DeleteObject(brush);
            }
        }
        const int radius=dip(h,6);
        RECT ring{x-radius-1,y-radius-1,x+radius+1,y+radius+1};roundRect(dc,ring,RGB(16,17,18),radius+1);
        RECT dot{x-radius,y-radius,x+radius,y+radius};roundRect(dc,dot,IsWindowEnabled(h)?RGB(244,246,248):line,radius);
        return 0;
    }
    if(msg==WM_LBUTTONDBLCLK||(msg==WM_KEYDOWN&&wp==VK_HOME)){
        const int index=GetDlgCtrlID(h)-colorSliderId(0);
        if(index>=0&&index<int(colorParams.size())){
            auto colour=colourTarget();
            colorParams[size_t(index)].set(colour,colorParams[size_t(index)].neutral);
            if(applyColour(colour,true))veyra::log::info("color-ui",std::format("slider reset index={} neutral={:.3f}",index,colorParams[size_t(index)].neutral));
            syncColorControls();
        }
        return 0;
    }
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,colourSliderKeys,id);
    return DefSubclassProc(h,msg,wp,lp);
}
// "Hold to see the original" (section 9): mouse-down swaps in a neutral grade
// (which the graph guarantees renders exactly like no grading) and mouse-up puts
// the user's values back. No rebuild, no history entry.
LRESULT CALLBACK holdOriginalProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(msg==WM_LBUTTONDOWN){
        colourHoldSaved=colourTarget();colourHoldHadValue=true;colourHolding=true;
        SetCapture(h);
        auto neutral=engine::ColorSettings{};neutral.enabled=true;
        if(applyColour(neutral,false,false))syncColorControls();
        return 0;
    }
    if(msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED){
        if(colourHolding){
            colourHolding=false;
            if(colourHoldHadValue)applyColour(colourHoldSaved,false,false);
            syncColorControls();
        }
        if(GetCapture()==h)ReleaseCapture();
        return 0;
    }
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,holdOriginalProc,id);
    return DefSubclassProc(h,msg,wp,lp);
}

void layoutEnhancePage(int width){
    auto place=[&](int id,int x,int y,int w,int height=0){
        for(auto& entry:items)if(GetDlgCtrlID(entry.h)==id){entry.x=x;entry.y=y;entry.w=w;if(height)entry.height=height;break;}
    };
    int y=12;
    auto row=[&](int id,int height){place(id,12,y,-1);y+=height+8;};
    // Combo height is the dropdown extent, not its collapsed row height.
    row(200,36);row(1117,24);row(218,36);row(203,36);
    row(220,36);row(223,36);row(201,36);
    const int third=(width-40)/3;
    for(int i=0;i<3;++i)place(730+i,12+i*(third+8),y,third);
    y+=44;row(207,36);
    row(206,36);
    const int half=(width-32)/2;
    place(213,12,y,half);place(214,20+half,y,half);y+=44;
    place(1109,12,y,-1,48);y+=56;
    place(1123,12,y,176,28);place(222,202,y,-1,28);place(622,12,y+32,-1,16);y+=62;
    row(1100,24);
    for(int i=0;i<12;++i){
        if(i==7)row(1101,24);
        place(1000+i,12,y,176,28);
        if(i>=4&&i<=6){
            const int count=i==4?3:2;
            for(int j=0;j<count;++j)place(700+(i-4)*10+j,12+j*(third+8),y+28,third);
            y+=72;
        }else{place(100+i,202,y,-1,28);place(600+i,12,y+32,-1,16);y+=62;}
    }
    place(1102,12,y,-1,70);y+=78;
    row(1124,24);row(260,36);place(261,12,y,-1,28);y+=36;
    for(int i=0;i<3;++i)place(262+i,12+i*(third+8),y,third);
    y+=52;row(230,32);place(1145,12,y,-1,52);y+=60;
    for(int i=0;i<4;++i){
        place(1141+i,12,y,160,24);place(1131+i,182,y,-1,24);
        place(631+i,12,y+26,-1,24);y+=58;
    }
}

void arrange(bool commitScroll){
    if(!window||!body)return;RECT r{};GetClientRect(window,&r);int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window)),height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(window));
    const int sticky=128,viewport=std::max(1,height-sticky);contentHeight=0;
    // The child still has its previous size until SetWindowPos below.
    if(page==2)layoutColorPage(width);
    if(page==0)layoutEnhancePage(width);
    if(page==1)for(auto& entry:items){
        const auto id=GetDlgCtrlID(entry.h);
        if(id==244){entry.x=12;entry.w=std::max(80,width-68);}
        if(id==1147){entry.x=width-48;entry.w=36;}
    }
    for(const auto& [id,row]:rowResets){
        auto findItem=[&](int control)->Item&{return *std::find_if(items.begin(),items.end(),[&](const Item& entry){return GetDlgCtrlID(entry.h)==control;});};
        auto& value=findItem(row.value);auto& reset=findItem(id);auto& label=findItem(row.label);
        reset.x=width-40;reset.y=value.y;reset.hidden=value.hidden;
        value.x=std::max(100,width-126);value.w=80;
        label.w=std::max(1,value.x-label.x-8);
    }
    int helpHeight=0;
    if(smoothMotionHelpExpanded){auto dc=GetDC(window);auto old=SelectObject(dc,font);RECT textRect{0,0,dip(window,std::max(1,width-24)),0};DrawTextW(dc,smoothMotionHelp,-1,&textRect,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);helpHeight=MulDiv(textRect.bottom,96,layoutDpi(window))+16;}
    // The Smooth Motion explainer unfolds between the frame-generation rows and
    // the pacing rows, so everything below it moves down by the measured text
    // height. The control keeps its own y; only the rows after it shift.
    const auto helpOffset=[&](const Item& entry){const auto id=GetDlgCtrlID(entry.h);return entry.page==1&&(id==1114||id==205||id==1110||id==240||id==241||id==242||id==243||id==244||id==1147||id==1150)?helpHeight:0;};
    for(auto& entry:items){if(GetDlgCtrlID(entry.h)==1120)entry.height=helpHeight;
        if(entry.page==page&&!entry.hidden&&(GetDlgCtrlID(entry.h)!=1120||smoothMotionHelpExpanded)){wchar_t cls[32]{};GetClassNameW(entry.h,cls,32);contentHeight=std::max(contentHeight,entry.y+helpOffset(entry)+(_wcsicmp(cls,L"COMBOBOX")==0?36:entry.height)+12);}}
    if(page==2)contentHeight=std::max(contentHeight,colorPageContentHeight);
    scroll=std::clamp(scroll,0,std::max(0,contentHeight-viewport));
    SetWindowPos(body,nullptr,0,dip(window,sticky),r.right,dip(window,viewport),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
    auto batch=BeginDeferWindowPos(int(items.size()));
    for(auto& entry:items){
        const int id=GetDlgCtrlID(entry.h);
        const bool fixed=entry.page==-1;
        int w=entry.w<0?width-entry.x-12:entry.w;
        int x=entry.x;
        if(fixed&&id==211){w=116;}
        if(fixed&&id==219){x=134;w=width-146;}
        const int y=fixed?(id==400?0:(id==211||id==219)?86:42):entry.y+helpOffset(entry)-scroll;
        if(fixed){SetWindowPos(entry.h,nullptr,dip(window,x),dip(window,y),dip(window,std::max(1,w)),dip(window,42),SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW);continue;}
        wchar_t cls[32]{};GetClassNameW(entry.h,cls,32);
        const int height=_wcsicmp(cls,L"COMBOBOX")==0?36:entry.height;
        // Rows that scrolled completely out of the viewport are hidden instead of
        // being parked outside the clip region, so a wheel notch only repaints
        // the rows that are actually on screen.
        const bool visible=!entry.hidden&&entry.page==page&&(id!=1120||smoothMotionHelpExpanded)&&y+height>-4&&y<viewport+4;
        batch=DeferWindowPos(batch,entry.h,nullptr,dip(window,entry.x),dip(window,y),dip(window,std::max(1,w)),dip(window,entry.height),SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCOPYBITS|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
    }
    EndDeferWindowPos(batch);
    // The panel scrolls by moving child windows, so the window is only correct
    // once every moved row has repainted. A scroll commits that here, inside the
    // input message: a deferred repaint is what shows up as ghosted or
    // duplicated rows while the wheel is spinning.
    RedrawWindow(body,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|(commitScroll?RDW_UPDATENOW:0));
    InvalidateRect(window,nullptr,FALSE);
}

HWND add(const wchar_t* cls,const wchar_t* text,int id,DWORD style,int group,int x,int y,int width,int height){auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,0,0,1,1,group==-1?window:body,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,WPARAM(font),TRUE);themeControl(h);if(group!=-1)SetWindowSubclass(h,scrollOnly,950,0);items.push_back({h,group,x,y,width,height});return h;}
void button(const wchar_t* title,int id,int group,int x,int y,int width=-1){add(L"BUTTON",title,id,BS_PUSHBUTTON|WS_TABSTOP,group,x,y,width,36);}
void combo(int id,int group,int y,std::initializer_list<const wchar_t*> names){auto h=add(L"COMBOBOX",L"",id,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,group,12,y,-1,200);for(auto name:names)SendMessageW(h,CB_ADDSTRING,0,LPARAM(name));}
void syncRowResets(){
    if(!controller)return;
    const auto settings=enhancementEnabled?controller->snapshot().desired:configuredSettings;
    const engine::EnhancementSettings defaults{};
    for(const auto& [id,row]:rowResets){
        const bool changed=editDrafts.contains(row.value)||std::abs(row.get(settings)-row.get(defaults))>0.00001;
        if(bool(IsWindowEnabled(item(id)))!=changed)EnableWindow(item(id),changed);
    }
}
void createRowResets(){
    const auto addReset=[&](int slider,int label,int value,auto access,const ColorParam* colourParam=nullptr){
        const int id=2000+slider;
        RowReset row{slider,label,value,
            [access](const engine::EnhancementSettings& s){auto copy=s;return double(access(copy));},
            [access](engine::EnhancementSettings& s){engine::EnhancementSettings defaults{};access(s)=access(defaults);}};
        if(colourParam){
            const auto param=*colourParam;
            row.get=[param](const auto& s){return double(param.value(s.color));};
            row.reset=[param](auto& s){param.set(s.color,param.value(engine::ColorSettings{}));};
        }
        wchar_t name[128]{};GetWindowTextW(item(label),name,128);
        if(slider==622)wcscpy_s(name,L"Feather");
        row.tip=std::format(L"Reset {} · Default {:g}",name,row.get(engine::EnhancementSettings{}));
        auto& stored=rowResets.emplace(id,std::move(row)).first->second;
        const auto valueItem=*std::find_if(items.begin(),items.end(),[&](const Item& entry){return entry.h==item(value);});
        auto h=add(L"BUTTON",stored.tip.c_str(),id,BS_PUSHBUTTON|WS_TABSTOP,valueItem.page,0,valueItem.y,28,28);
        icon(h,Icon::Reset);ghost(h);SetPropW(h,L"veyra.reset",HANDLE(1));
        SetPropW(h,L"veyra.tip",HANDLE(stored.tip.c_str()));
        SetWindowLongPtrW(item(label),GWL_STYLE,GetWindowLongPtrW(item(label),GWL_STYLE)|SS_ENDELLIPSIS);
    };
    addReset(600,1000,100,[](auto& s)->auto&{return s.model.intensity;});
    addReset(601,1001,101,[](auto& s)->auto&{return s.model.tone;});
    addReset(602,1002,102,[](auto& s)->auto&{return s.model.structure;});
    addReset(603,1003,103,[](auto& s)->auto&{return s.model.skin;});
    addReset(607,1007,107,[](auto& s)->auto&{return s.residual.total;});
    addReset(608,1008,108,[](auto& s)->auto&{return s.residual.darken;});
    addReset(609,1009,109,[](auto& s)->auto&{return s.residual.brighten;});
    addReset(610,1010,110,[](auto& s)->auto&{return s.residual.color;});
    addReset(611,1011,111,[](auto& s)->auto&{return s.residual.luminance;});
    addReset(622,1123,222,[](auto& s)->auto&{return s.protection.featherPixels;});
    addReset(631,1141,1131,[](auto& s)->auto&{return s.videoHdr.contrast;});
    addReset(632,1142,1132,[](auto& s)->auto&{return s.videoHdr.saturation;});
    addReset(633,1143,1133,[](auto& s)->auto&{return s.videoHdr.middleGray;});
    addReset(634,1144,1134,[](auto& s)->auto&{return s.videoHdr.peakNits;});
    for(size_t i=0;i<colorParams.size();++i){
        // Colour arrays and scalars share the same field descriptor as editing.
        addReset(colorSliderId(int(i)),colorLabelId(int(i)),colorEditId(int(i)),[](auto& s)->auto&{return s.color.exposure;},&colorParams[i]);
    }
}
// Win32 synchronously reenters this callback during layout and control painting.
// Allocate snapshots only in the message handlers that actually need them.
__declspec(noinline) LRESULT createSettingsWindow(HWND h,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);items.clear();displayedBackendWarning.clear();smoothMotionHelpExpanded=false;
    WNDCLASSW bodyClass{};bodyClass.lpfnWndProc=bodyProc;bodyClass.hInstance=GetModuleHandleW(nullptr);bodyClass.lpszClassName=L"VeyraInspectorBody";bodyClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&bodyClass);
    // Composite only the scrolling controls, never the video/swapchain window.
    body=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_COMPOSITED,bodyClass.lpszClassName,L"Scrollable parameters",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,88,300,300,h,nullptr,bodyClass.hInstance,nullptr);
    add(L"BUTTON",L"Experimental NVIDIA NR enhancement",200,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,12,-1,36);
    add(L"BUTTON",L"Super resolution",201,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,56,-1,36);
    combo(203,0,104,{L"1080p NR · real-time default",L"Native NR · high performance cost",L"480p NR",L"720p NR",L"900p NR",L"1440p NR"});
    add(L"STATIC",L"Model parameters",1100,0,0,12,146,-1,24);
    for(int i=0;i<12;++i){const int y=174+i*62+(i>=7?32:0);add(L"STATIC",labels[i],1000+i,0,0,12,y,176,28);
        if(i>=4&&i<=6){const int group=i-4,count=group==0?3:2;for(int j=0;j<count;++j){const auto title=group==0?std::to_wstring(j):j==0?std::wstring(L"Off"):std::wstring(L"On");button(title.c_str(),700+group*10+j,0,12+j*88,y+28,80);}continue;}
        add(L"EDIT",L"",100+i,ES_AUTOHSCROLL|WS_TABSTOP|ES_RIGHT,0,202,y,-1,28);
        const wchar_t* range=i<3?L"Range: 0-1":i==3?L"-1 means default; otherwise range 0-2 (effect unverified)":L"Range: 0-2";SetPropW(item(100+i),L"veyra.tip",HANDLE(range));
        auto slider=add(TRACKBAR_CLASSW,L"",600+i,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,12,y+32,-1,16);SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(i==3?-100:0,i<3?100:200));}
    add(L"STATIC",L"Enhancement change amount",1101,0,0,12,606,-1,24);
    add(L"STATIC",L"Skin / Style / Mask / UI keys are local experimental parameters. Unverified effects aren't marked as available capabilities.",1102,0,0,12,956,-1,70);
    button(L"Restore defaults",211,-1,12,86,88);
    add(L"BUTTON",L"Live-stream compatible · Experimental",219,BS_AUTOCHECKBOX|WS_TABSTOP,-1,108,86,-1,36);
    SetPropW(item(219),L"veyra.tip",HANDLE(L"Live-stream compatibility mode: switches the display swap chain, causing a brief pause; doesn't change the enhancement algorithm or export. Not guaranteed to work with every capture method."));
    add(L"STATIC",L"Frame generation and motion estimation",1103,0,1,12,12,-1,30);
    add(L"STATIC",L"Frame generation method",1111,0,1,12,50,-1,24);
    // 1.4.0: the AMD FSR *frame-generation* entry is gone from the panel. Users
    // could get stuck on it (switching back did not restore the DLSS path) and
    // the result was not good enough to ship. The backend stays in the engine and
    // is still reachable through --fg-fsr for diagnostics; only the UI entry is
    // removed. FSR *upscaling* (id 207) is a different feature and stays.
    combo(208,1,78,{L"DLSS frame generation",L"Intel XeSS · experimental display frame generation 2X-4X"});
    add(L"STATIC",L"Frame generation multiplier",1112,0,1,12,122,-1,24);
    combo(202,1,150,{L"Frame generation off",L"2X · one interpolated frame",L"3X · two interpolated frames",L"4X · three interpolated frames"});
    add(L"BUTTON",L"Strict frame generation pacing (off by default)",210,BS_AUTOCHECKBOX|WS_TABSTOP,1,12,366,-1,36);
    add(L"STATIC",L"Motion estimation",1113,0,1,12,234,-1,24);
    combo(209,1,262,{L"NVIDIA NVOF optical flow",L"AMD FidelityFX optical flow · Experimental",L"GPU DIS optical flow · FAST Experimental"});
    add(L"BUTTON",L"AMD performance tier · halve optical flow width and height",215,BS_AUTOCHECKBOX|WS_TABSTOP,1,12,306,-1,36);
    combo(204,1,350,{L"NR / DLSS optical flow · Performance",L"NR / DLSS optical flow · Balanced",L"NR / DLSS optical flow · Quality"});
    add(L"STATIC",L"Content cadence",1114,0,1,12,394,-1,24);
    combo(205,1,422,{L"Use source timestamps",L"Auto-detect content cadence",L"Detect 30fps content cadence",L"Detect 50fps content cadence",L"Detect 60fps content cadence",L"Capture 60->30fps processing (PS5 30 frames)"});
    add(L"STATIC",L"AMD FidelityFX is for motion estimation, not AMD NR. XeSS is an experimental preview 2X and doesn't support export.",1110,0,1,12,426,-1,72);
    add(L"STATIC",L"Capture audio sync",1115,0,1,12,608,-1,26);
    combo(216,1,644,{L"Auto sync · software estimate",L"Manual audio offset",L"Compensation off"});
    add(L"STATIC",L"Audio offset ms",1116,0,1,12,692,160,28);
    add(L"EDIT",L"0",217,ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,1,182,692,-1,28);
    SetPropW(item(217),L"veyra.tip",HANDLE(L"-250 to 250 ms; positive values make audio later. Negative values can only reduce existing latency; actual compensation bottoms out at 0."));

    // Page 2 is the colour page. The old preset page (ids 300/301/310-315,
    // statics 1105/1106) was deleted on purpose on 2026-09-17: named colour
    // presets replace it and carry only look parameters, never NR/SR/FG.
    loadColourFoldState();
    add(L"BUTTON",L"Color master switch (when off this pipeline doesn't exist, zero overhead)",800,BS_AUTOCHECKBOX|WS_TABSTOP,2,12,12,-1,36);
    button(L"One-click reset (back to neutral)",801,2,12,56,200);button(L"Undo reset",802,2,216,56,120);
    {
        struct Definition{int section;const wchar_t* label;float engine::ColorSettings::*field;float min,max;};
        const Definition definitions[]={
            {0,L"Exposure (EV)",&engine::ColorSettings::exposure,-5,5},
            {0,L"Contrast",&engine::ColorSettings::contrast,-100,100},
            {0,L"Highlights",&engine::ColorSettings::highlights,-100,100},
            {0,L"Shadows",&engine::ColorSettings::shadows,-100,100},
            {0,L"Whites",&engine::ColorSettings::whites,-100,100},
            {0,L"Blacks",&engine::ColorSettings::blacks,-100,100},
            {1,L"Temperature (relative)",&engine::ColorSettings::temperature,-100,100},
            {1,L"Tint",&engine::ColorSettings::tint,-100,100},
            {1,L"Vibrance",&engine::ColorSettings::vibrance,-100,100},
            {1,L"Saturation",&engine::ColorSettings::saturation,-100,100},
        };
        for(const auto& definition:definitions)
            colorParams.push_back({definition.section,definition.label,definition.min,definition.max,ColorTarget::Scalar,definition.field,0,0.0f,
                _wcsicmp(definition.label,L"Temperature (relative)")==0?1:_wcsicmp(definition.label,L"Tint")==0?2:
                (_wcsicmp(definition.label,L"Saturation")==0||_wcsicmp(definition.label,L"Vibrance")==0)?3:0});
        // Composite labels are built once; reserve keeps the c_str() pointers
        // stable for the lifetime of the panel.
        static std::vector<std::wstring> colorLabelStorage;
        colorLabelStorage.clear();colorLabelStorage.reserve(256);
        auto composed=[&](const std::wstring& text){colorLabelStorage.push_back(text);return colorLabelStorage.back().c_str();};
        // Mixer: eight hue bands, each with hue / saturation / luminance, then
        // the same eight bands for the black & white mixer.
        {
            static const wchar_t* bands[engine::kColorMixerBands]={L"Red",L"Orange",L"Yellow",L"Green",L"Light green",L"Blue",L"Purple",L"Magenta"};
            for(int band=0;band<engine::kColorMixerBands;++band){
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · Hue"),-100,100,ColorTarget::MixerHue,nullptr,band,0.0f,4});
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · Saturation"),-100,100,ColorTarget::MixerSaturation,nullptr,band});
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · Luminance"),-100,100,ColorTarget::MixerLuminance,nullptr,band});
            }
            for(int band=0;band<engine::kColorMixerBands;++band)
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · B&W"),-100,100,ColorTarget::BlackWhiteMix,nullptr,band});
        }
        // Colour grading: four zones with hue / saturation / luminance, plus the
        // blending and balance controls.
        {
            static const wchar_t* zones[engine::kColorGradingZones]={L"Shadows",L"Midtones",L"Highlights",L"Global"};
            registerColorWheelClass();
            // The thirty-six zone sliders (4 zones x hue/sat/lum) are replaced by
            // four colour wheels - the professional grading layout. The model,
            // the shader and the preset schema are unchanged; only the control
            // that edits them is.
            for(int zone=0;zone<engine::kColorGradingZones;++zone)
                add(L"VeyraColorWheel",zones[zone],colorWheelId(zone),0,2,12,0,-1,208);
            colorParams.push_back({4,L"Blending",0,100,ColorTarget::Scalar,&engine::ColorSettings::gradingBlending,0,50.0f});
            colorParams.push_back({4,L"Balance",-100,100,ColorTarget::Scalar,&engine::ColorSettings::gradingBalance,0});
        }
        // Calibration: shadow tint plus the three primaries.
        {
            colorParams.push_back({5,L"Shadow tint",-100,100,ColorTarget::Scalar,&engine::ColorSettings::calibrationShadowTint,0});
            static const wchar_t* primaries[3]={L"Red primary",L"Green primary",L"Blue primary"};
            for(int primary=0;primary<3;++primary){
                colorParams.push_back({5,composed(std::wstring(primaries[primary])+L" · Hue"),-100,100,ColorTarget::CalibrationHue,nullptr,primary});
                colorParams.push_back({5,composed(std::wstring(primaries[primary])+L" · Saturation"),-100,100,ColorTarget::CalibrationSaturation,nullptr,primary});
            }
        }
        // LUT: the .cube selection, its strength and the input-space choice. The
        // strength row is a normal parameter; the two combos are placed by
        // layoutColorPage().
        colorParams.push_back({6,L"LUT strength",0,100,ColorTarget::Scalar,&engine::ColorSettings::lutStrength,0,100.0f});
        // Undo/redo/copy/paste/hold-to-compare row (plan T3 + section 9).
        button(L"Undo",802,2,12,0,72);button(L"Redo",824,2,12,0,72);button(L"Copy",822,2,12,0,72);button(L"Paste",823,2,12,0,72);
        button(L"Hold to view original",821,2,12,0,140);
        SetWindowSubclass(item(821),holdOriginalProc,970,0);
        SetPropW(item(802),L"veyra.tip",HANDLE(L"Undo the last color change (up to 32 steps)."));
        SetPropW(item(824),L"veyra.tip",HANDLE(L"Redo the change you just undid."));
        SetPropW(item(822),L"veyra.tip",HANDLE(L"Copy the current color settings to paste onto another preset or the next clip."));
        SetPropW(item(823),L"veyra.tip",HANDLE(L"Paste the color settings you just copied."));
        SetPropW(item(821),L"veyra.tip",HANDLE(L"Hold down: temporarily show the original without grading; release to restore. Implemented with neutral grading, without rebuilding the pipeline."));
        // Mixer header: the eight colour ranges plus the black & white switch.
        // The selected range's hue/saturation/luminance rows are always shown;
        // its B&W row is added while the switch is on.
        registerColorBandsClass();
        add(L"BUTTON",L"",colorMixerModeId,BS_AUTOCHECKBOX|WS_TABSTOP,2,12,0,dip(window,26),26);check(colorMixerModeId,BST_UNCHECKED);
        add(kColorBandsClass,L"",colorBandsId,WS_TABSTOP,2,12,0,-1,32);
        SetPropW(item(colorMixerModeId),L"veyra.tip",HANDLE(L"Black & white mixer: when on, the image turns monochrome and the \"B&W\" sliders below control the grayscale brightness per color range (the hue/saturation rows are inactive then)."));
        SetPropW(item(colorBandsId),L"veyra.tip",HANDLE(L"Click a color dot to switch the range you're adjusting; the sliders below affect only the selected range."));
        // Tone curve: channel tabs (RGB / R / G / B), a flatten button and the
        // grid canvas itself.
        registerToneCurveClass();
        for(int channel=0;channel<4;++channel)
            add(L"BUTTON",channel==0?L"RGB":channel==1?L"R":channel==2?L"G":L"B",colorCurveChannelId+channel,BS_PUSHBUTTON|WS_TABSTOP,2,12,0,0,30);
        button(L"Flatten",colorCurveResetId,2,12,0,40);
        add(kColorCurveClass,L"",colorCurveCanvasId,WS_TABSTOP,2,12,0,-1,260);
        SetPropW(item(colorCurveCanvasId),L"veyra.tip",HANDLE(L"Left-click on the grid to add a point, drag to move; double-click a control point to delete it (the two endpoints stay); \"Flatten\" restores the identity curve."));
        for(int section=0;section<kColorSections;++section)add(L"BUTTON",L"",810+section,BS_PUSHBUTTON|WS_TABSTOP,2,12,12,-1,32);
        registerSectionEyeClass();
        for(int section=0;section<kColorSections;++section){
            add(kColorEyeClass,L"",colorSectionEyeId(section),0,2,12,12,26,26);
            SetPropW(item(colorSectionEyeId(section)),L"veyra.tip",HANDLE(L"Click to temporarily disable this group (values kept), click again to restore: use it to compare what a group actually does."));
        }
        // Preset toolbar.
        combo(803,2,0,{});add(L"EDIT",L"",804,ES_AUTOHSCROLL|WS_TABSTOP,2,12,0,-1,26);send(804,EM_SETLIMITTEXT,48,0);
        send(804,EM_SETCUEBANNER,TRUE,LPARAM(L"Preset name"));
        button(L"Save as preset",805,2,12,0,110);button(L"Apply",806,2,12,0,80);button(L"Delete",807,2,12,0,80);
        button(L"Export",808,2,12,0,80);button(L"Import",809,2,12,0,80);
        SetPropW(item(804),L"veyra.tip",HANDLE(L"Name the current color settings and click \"Save preset\" to store them; Export creates a .vpcolor file you can send to others to import."));
        SetPropW(item(805),L"veyra.tip",HANDLE(L"Save the current color settings as a named preset. A duplicate name overwrites."));
        SetPropW(item(806),L"veyra.tip",HANDLE(L"Apply the selected preset to the current image (color only; NR/super resolution/frame generation unchanged)."));
        SetPropW(item(807),L"veyra.tip",HANDLE(L"Delete the selected color preset."));
        // LUT section: choose an imported .cube, import a new one, pick its input
        // space (the strength row is registered as a normal parameter).
        combo(817,2,0,{});button(L"Import .cube",818,2,12,0,140);combo(819,2,0,{L"Cineon Log (creator LUT default)",L"sRGB display reference",L"PQ (HDR)"});
        SetPropW(item(817),L"veyra.tip",HANDLE(L"Choose a .cube already imported into runtime_local/luts. Switching rebuilds the pipeline, so a brief pause is normal."));
        SetPropW(item(818),L"veyra.tip",HANDLE(L"Import a .cube from disk: after validation it's copied to runtime_local/luts and written to the manifest (with SHA-256)."));
        SetPropW(item(819),L"veyra.tip",HANDLE(L"The input space the LUT expects. Most creator LUTs are Cineon Log; sRGB display reference is for SDR content; PQ is for HDR. A wrong choice is flagged and logged."));
        refreshColourLooks();refreshColourLuts();
        for(size_t i=0;i<colorParams.size();++i){
            const auto& param=colorParams[i];
            add(L"STATIC",param.label,colorLabelId(int(i)),0,2,12,0,180,24);
            add(L"EDIT",L"0",colorEditId(int(i)),ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,2,202,0,-1,26);
            // Lightroom prints the value as text rather than as a boxed field.
            SetPropW(item(colorEditId(int(i))),L"veyra.flat",HANDLE(1));
            surface(item(colorEditId(int(i))),panel);
            auto slider=add(TRACKBAR_CLASSW,L"",colorSliderId(int(i)),TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,2,12,0,-1,16);
            SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(int(std::lround(param.min*100.0f)),int(std::lround(param.max*100.0f))));
            SetWindowSubclass(slider,colourSliderKeys,960+i,0);
            SetPropW(item(colorEditId(int(i))),L"veyra.tip",HANDLE(L"You can type a number directly and press Enter to apply; dragging the slider applies instantly."));
        }
    }
    add(L"STATIC",L"Native-quality export",1107,0,3,12,12,-1,32);combo(500,3,60,{L"H.264 · MP4",L"HEVC · MP4"});send(500,CB_SETCURSEL,0,0);
    add(L"STATIC",L"Freezes the whole parameter set at start; NR processes at native size. Compatible audio tracks kept. VFR isn't rewritten to CFR; subtitles aren't burned in.",1108,0,3,12,108,-1,94);
    button(L"Choose location and export video",501,3,12,212);marked(item(501));button(L"Save current image / video frame",502,3,12,256);
    button(L"Pause / Resume export",503,3,12,310);button(L"Cancel export",504,3,12,354);
    add(L"BUTTON",L"Prioritize viewing · lower export load",505,BS_AUTOCHECKBOX|WS_TABSTOP,3,12,406,-1,36);check(505,BST_CHECKED);
    add(L"STATIC",L"",506,0,3,12,458,-1,120);add(L"STATIC",L"",507,0,3,12,588,-1,80);
    // Export bitrate row, inserted between the codec selector and everything
    // below it (the page scrolls, so the shift keeps the reading order).
    for(auto& entry:items)if(entry.page==3&&entry.y>=108)entry.y+=72;
    add(L"STATIC",L"Export bitrate",1121,0,3,12,104,-1,24);
    combo(508,3,132,{L"Auto · constant quality",L"6 Mbps",L"10 Mbps",L"16 Mbps",L"24 Mbps",L"40 Mbps",L"60 Mbps",L"100 Mbps",L"150 Mbps",L"200 Mbps"});
    add(L"STATIC",L"",400,0,-1,12,900,-1,92);add(L"STATIC",L"",401,0,-1,12,996,-1,86);
    for(auto& entry:items)if(entry.page==0&&entry.y>=146)entry.y+=48;
    add(L"BUTTON",L"NR exclusion zone",206,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,146,-1,36);
    button(L"Draw exclusion box",213,0,12,188,140);button(L"Clear exclusion zone",214,0,162,188);
    add(L"STATIC",L"Up to 4 zones, left-drag to box, Esc to cancel. Suppresses NR change only; cleared when the source changes.",1109,0,0,12,232,-1,48);
    for(auto& entry:items)if(entry.page==0&&entry.y>=104)entry.y+=48;
    combo(207,0,100,{L"DLSS SR",L"RTX Video Super Resolution · Low",L"RTX Video Super Resolution · Medium",L"RTX Video Super Resolution · High",L"RTX Video Super Resolution · Highest",L"AMD FSR super resolution · 3.1.x (works on NVIDIA cards)"});
    for(auto& entry:items)if(entry.page==0&&entry.y>=100)entry.y+=44;
    button(L"2K",730,0,12,100,80);button(L"4K",731,0,100,100,80);button(L"8K",732,0,188,100,80);
    for(auto& entry:items)if(entry.page==0&&entry.y>=56)entry.y+=24;
    add(L"STATIC",L"NR runtime version",1117,0,0,12,56,-1,24);
    combo(218,0,84,{L"NVIDIA official · RTX 50",L"Community compatible · RTX 40/50 Experimental",L"RTX 30 compatible · Experimental"});
    SetPropW(item(218),L"veyra.tip",HANDLE(L"The community build is a modified runtime. The RTX 30 tier needs a separate component; performance and compatibility await verification on real cards; it doesn't unlock DLSS frame generation. Switching rebuilds the pipeline; on failure the original settings are restored."));
    // Final layout in reading order; existing control IDs and bindings stay intact.
    for(auto& entry:items){
        const int id=GetDlgCtrlID(entry.h);
        if(id==203)entry.y=128;
        if(id==201)entry.y=176;
        if(id>=730&&id<=732)entry.y=220;
        if(id==207)entry.y=264;
        // The capture/stream audio sync controls are created here but read as a
        // separate audio page.
        switch(id){
        case 1115:entry.page=4;entry.y=12;break;
        case 216:entry.page=4;entry.y=50;break;
        case 1116:case 217:entry.page=4;entry.y=100;break;
        }
    }
    setText(item(1103),L"Optical flow and frame generation");
    // Three independent controls. The old mode selector (low queue / even /
    // Reflex) was removed: all three measured identical (2026-09-22).
    add(L"BUTTON",L"Low-latency queue (less queuing; can't be verified on this machine)",240,BS_AUTOCHECKBOX|WS_TABSTOP,1,12,550,-1,32);
    combo(242,1,590,{L"Display sync: allow tearing",L"Display sync: vertical sync",L"Display sync: auto"});
    combo(243,1,634,{L"Output cap: off",L"Output cap: follow monitor",L"Output cap: custom"});
    add(L"EDIT",L"60",244,ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,1,182,678,90,28);
    add(L"STATIC",L"FPS",1147,0,1,276,678,40,28);
    add(L"STATIC",L"",1150,SS_NOPREFIX,1,12,720,-1,134);
    {const auto p=controller->snapshot().presentation;check(240,p.enabled?BST_CHECKED:BST_UNCHECKED);send(242,CB_SETCURSEL,unsigned(p.display));send(243,CB_SETCURSEL,unsigned(p.outputRate));putText(244,std::format(L"{:.3f}",p.customFps).c_str());EnableWindow(item(241),p.enabled);EnableWindow(item(242),p.enabled);EnableWindow(item(243),true);EnableWindow(item(244),p.outputRate==engine::OutputRateMode::Custom);}
    button(L"Smooth Motion · how to enable ▾",221,1,12,358);
    ghost(item(221));
    SetPropW(item(221),L"veyra.tip",HANDLE(L"See how to enable the NVIDIA App's AI frame generation. This only provides guidance; it doesn't modify the driver or restrict stacked frame generation."));
    add(L"STATIC",smoothMotionHelp,1120,SS_NOPREFIX,1,12,400,-1,1);

    // Reading order for the frame-generation page, and the single source of
    // truth for its geometry. Every row keeps the same 8 dip rhythm the other
    // pages use; the strict-cadence switch owns the row directly under the
    // multiplier selector (it used to be created at y=190, which placed it on
    // top of the optical-flow combo *and* under the "frame generation method" label), and the
    // Smooth Motion help moved to the end so expanding it can never overlap the
    // controls above it.
    // The Smooth Motion explainer sits directly under the frame-generation
    // rows; expanding it shifts every row below by the measured text height
    // (see the frame page help offset in arrange()).
    for(auto& entry:items){
        switch(GetDlgCtrlID(entry.h)){
        case 1113:entry.y=50;break;case 209:entry.y=82;break;
        case 215:entry.y=126;break;case 204:entry.y=170;break;
        case 1111:entry.y=214;break;case 208:entry.y=246;break;
        case 1112:entry.y=290;break;case 202:entry.y=322;break;
        case 210:entry.y=370;break;
        case 221:entry.y=418;break;case 1120:entry.y=462;break;
        case 1114:entry.y=470;break;case 205:entry.y=502;break;
        case 1110:entry.y=546;break;
        case 240:entry.y=626;break;case 242:entry.y=666;break;
        case 243:entry.y=710;break;
        case 244:case 1147:entry.y=754;break;
        case 1150:entry.y=790;break;
        }
    }
    setText(item(1113),L"Optical flow · motion estimation");
    setText(item(1115),L"Capture / stream audio sync");
    add(L"STATIC",L"Adjusts audio compensation for the live input without changing the frame generation multiplier. Positive values make audio later; auto mode is estimated by software.",1118,0,4,12,148,-1,90);
    for(auto& entry:items)if(entry.page==0&&entry.y>=176)entry.y+=44;
    add(L"BUTTON",L"Low-latency mode · Experimental",220,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,172,-1,36);
    SetPropW(item(220),L"veyra.tip",HANDLE(L"By default super resolution runs first, then NR (DLSS5). When on, NR runs first, then super resolution, then frame generation: less work moved around, possibly faster, but possibly more ghosting or edge artifacts. Preview only; export keeps the order. Needs both NR and super resolution enabled to have any effect."));
    for(auto& entry:items)if(entry.page==0&&entry.y>=216)entry.y+=44;
    add(L"BUTTON",L"NR temporal anti-flicker · Experimental",223,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,212,-1,36);
    SetPropW(item(223),L"veyra.tip",HANDLE(L"Uses the previous frame's NR residual, aligned by optical flow, to suppress flicker. Off by default; old history is dropped automatically on fast motion or cuts, and it may add a little GPU overhead."));
    // NR exclusion-zone feather lands directly under the zone help text; every
    // later block moves down by the same amount so nothing overlaps.
    for(auto& entry:items)if(entry.page==0&&entry.y>=530)entry.y+=80;
    add(L"STATIC",L"",1123,0,0,12,530,-1,24);
    add(L"EDIT",L"12",222,ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,0,202,530,-1,28);
    auto featherSlider=add(TRACKBAR_CLASSW,L"",622,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,12,562,-1,16);
    SendMessageW(featherSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,64));
    SendMessageW(featherSlider,TBM_SETPOS,TRUE,12);
    SetPropW(item(222),L"veyra.tip",HANDLE(L"The transition width at the exclusion-zone edge, in working-resolution pixels. 0 is a hard edge; at 4K, 12 px is about 0.5% of the frame height, and larger values soften the edge more."));
    // Named NR looks are deliberately separate from the generic user preset
    // file. They are selectable, named and removable, but applying one only
    // changes the NR-related fields (see settingsCommand below).
    int nrPresetTop=0;for(const auto& entry:items)if(entry.page==0)nrPresetTop=std::max(nrPresetTop,entry.y+entry.height);
    nrPresetTop+=20;
    add(L"STATIC",L"NR named presets",1124,0,0,12,nrPresetTop,-1,24);
    combo(260,0,nrPresetTop+30,{});
    add(L"EDIT",L"",261,ES_AUTOHSCROLL|WS_TABSTOP,0,202,nrPresetTop+30,170,28);send(261,EM_SETLIMITTEXT,48,0);
    button(L"Save NR",262,0,12,nrPresetTop+66,112);
    button(L"Apply",263,0,132,nrPresetTop+66,92);
    button(L"Delete",264,0,232,nrPresetTop+66,92);
    SetPropW(item(260),L"veyra.tip",HANDLE(L"Saves only the NR model, residual, exclusion zone and temporal anti-flicker; applying one doesn't change super resolution, frame generation, color or audio."));
    SetPropW(item(261),L"veyra.tip",HANDLE(L"Type a name, then click \"Save NR\"; a duplicate name overwrites, up to 48 characters."));
    for(const auto& entry:items)if(auto help=settingHelp(GetDlgCtrlID(entry.h)))SetPropW(entry.h,L"veyra.tip",HANDLE(help));
    int hdrTop=0;for(const auto& entry:items)if(entry.page==0)hdrTop=std::max(hdrTop,entry.y+entry.height);
    hdrTop+=24;
    add(L"BUTTON",L"RTX Video HDR",230,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,hdrTop,-1,32);
    SetPropW(item(230),L"veyra.tip",HANDLE(L"Converts SDR video to HDR; preview needs a Windows HDR display, but export isn't affected by the display mode. Native HDR isn't converted again."));
    add(L"STATIC",L"Waiting for preview status",1145,SS_NOPREFIX,0,12,hdrTop+36,-1,52);
    const wchar_t* hdrLabels[]={L"Contrast",L"Saturation",L"Middle gray",L"Peak brightness"};
    const int hdrMin[]={0,0,10,400},hdrMax[]={200,200,100,2000};
    for(int i=0;i<4;++i){const int y=hdrTop+96+i*58;
        add(L"STATIC",hdrLabels[i],1141+i,0,0,12,y,160,24);
        add(L"STATIC",L"",1131+i,SS_RIGHT,0,182,y,-1,24);
        auto slider=add(TRACKBAR_CLASSW,L"",631+i,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,12,y+26,-1,24);
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(hdrMin[i],hdrMax[i]));
    }
    createRowResets();
    loadStore();loadNrPresetStore();populate(controller->snapshot().desired);refreshNrPresets();message(store.error().empty()?nrPresetStore.error():store.error());SetTimer(h,1,250,nullptr);arrange();
    // Layout evidence for UI work: VEYRA_DUMP_SETTINGS_LAYOUT=1 prints the
    // resolved position of every control once, in DIP units.
    if(GetEnvironmentVariableW(L"VEYRA_DUMP_SETTINGS_LAYOUT",nullptr,0))
        for(const auto& entry:items)veyra::log::info("settings-layout",std::format("id={} page={} x={} y={} w={} h={}",
            GetDlgCtrlID(entry.h),entry.page,entry.x,entry.y,entry.w,entry.height));
    return 0;}
}return DefWindowProcW(h,msg,wp,lp);
}

__declspec(noinline) LRESULT settingsCommand(HWND h,UINT msg,WPARAM wp,LPARAM lp){

    if(msg==WM_COMMAND&&!populating&&HIWORD(wp)==BN_CLICKED){
        if(const auto found=rowResets.find(LOWORD(wp));found!=rowResets.end()){
            auto settings=enhancementEnabled?controller->snapshot().desired:configuredSettings;
            found->second.reset(settings);
            const bool accepted=found->second.slider>=colorSliderId(0)?applyColour(settings.color,false):submit(settings);
            if(!accepted)return 0;
            editDrafts.erase(found->second.value);
            if(GetFocus()==item(found->second.value))SetFocus(body);
            populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);
            return 0;
        }
    }
    if(msg==WM_COMMAND&&!populating&&HIWORD(wp)==BN_CLICKED&&LOWORD(wp)>=262&&LOWORD(wp)<=264){
        loadNrPresetStore();
        const int id=LOWORD(wp);
        const int selected=int(SendMessageW(item(260),CB_GETCURSEL,0,0));
        if(id==262){
            wchar_t name[64]{};GetWindowTextW(item(261),name,64);
            if(std::wstring(name).find_first_not_of(L" \t\r\n")==std::wstring::npos){
                SetFocus(item(261));message(L"Enter an NR preset name, then click Save.");return 0;
            }
            const auto current=enhancementEnabled?controller->snapshot().desired:configuredSettings;
            if(nrPresetStore.put(name,nrPresetSettings(current),true)){
                refreshNrPresets(name);message(L"NR preset saved: "+std::wstring(name));
                veyra::log::info("nr-preset","saved named NR preset");
            }else message(L"Failed to save NR preset: "+nrPresetStore.error());
        }else if(id==263){
            if(selected<=0||size_t(selected-1)>=nrPresetStore.entries().size()){message(L"Select an NR preset from the list first.");return 0;}
            const auto current=enhancementEnabled?controller->snapshot().desired:configuredSettings;
            auto next=current;applyNrPresetFields(next,nrPresetStore.entries()[size_t(selected-1)].settings);
            if(submit(next)){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);message(L"NR preset applied (super resolution, frame generation, color and audio unchanged).");}
            else message(L"Failed to apply NR preset: settings are currently switching.");
        }else{
            if(selected<=0||size_t(selected-1)>=nrPresetStore.entries().size()){message(L"Select the NR preset to delete from the list first.");return 0;}
            const auto name=nrPresetStore.entries()[size_t(selected-1)].name;
            if(nrPresetStore.erase(size_t(selected-1))){refreshNrPresets();message(L"NR preset deleted: "+name);}
            else message(L"Failed to delete NR preset: "+nrPresetStore.error());
        }
        return 0;
    }
    if(msg==WM_COMMAND&&!populating&&((LOWORD(wp)==240&&HIWORD(wp)==BN_CLICKED)||((LOWORD(wp)==242||LOWORD(wp)==243)&&HIWORD(wp)==CBN_SELCHANGE)|| (LOWORD(wp)==244&&HIWORD(wp)==EN_KILLFOCUS))){
        auto setting=controller->snapshot().presentation;setting.enabled=checked(240)==BST_CHECKED;
        setting.display=engine::DisplaySync(send(242,CB_GETCURSEL));
        setting.outputRate=static_cast<engine::OutputRateMode>(send(243,CB_GETCURSEL));
        if(setting.outputRate==engine::OutputRateMode::Custom){wchar_t fps[64]{};GetWindowTextW(item(244),fps,64);wchar_t* end=nullptr;const auto value=wcstod(fps,&end);if(end==fps||*end||!std::isfinite(value)||value<1||value>1000){message(L"Custom frame cap must be 1-1000 FPS");return 0;}setting.customFps=value;}
        if(setting.valid()){controller->requestPresentation(setting);EnableWindow(item(244),setting.outputRate==engine::OutputRateMode::Custom);SendMessageW(GetParent(window),WM_APP+46,0,0);}return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==221&&HIWORD(wp)==BN_CLICKED){smoothMotionHelpExpanded=!smoothMotionHelpExpanded;putText(221,smoothMotionHelpExpanded?L"Smooth Motion · collapse help ▴":L"Smooth Motion · how to enable ▾");arrange();return 0;}
    if(msg==WM_COMMAND&&!populating&&(LOWORD(wp)==220||LOWORD(wp)==223)&&HIWORD(wp)==BN_CLICKED){liveField(LOWORD(wp));return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==230&&HIWORD(wp)==BN_CLICKED){SendMessageW(GetParent(window),WM_APP+44,230,checked(230));return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==219&&HIWORD(wp)==BN_CLICKED){liveField(219);return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==218&&HIWORD(wp)==CBN_SELCHANGE){liveField(218);return 0;}
    if(msg==WM_COMMAND&&!populating&&((LOWORD(wp)==216&&HIWORD(wp)==CBN_SELCHANGE)||(LOWORD(wp)==217&&HIWORD(wp)==EN_CHANGE))){liveField(LOWORD(wp));return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==222&&HIWORD(wp)==EN_CHANGE){liveField(222);return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==222&&HIWORD(wp)==EN_KILLFOCUS){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);return 0;}
    // --- colour page (plan v4) ---------------------------------------------
    if(msg==WM_COMMAND&&LOWORD(wp)==800&&HIWORD(wp)==BN_CLICKED){
        auto settings=enhancementEnabled?controller->snapshot().desired:configuredSettings;
        settings.color.enabled=SendMessageW(item(800),BM_GETCHECK,0,0)==BST_CHECKED;
        if(!submit(settings)){syncColorControls();return 0;}
        message(settings.color.enabled?L"Color enabled: the chain runs before all effects, adding a little overhead.":L"Color disabled: this chain doesn't exist at all, zero overhead.");
        return 0;
    }
    // Reset / undo / redo / copy / paste / hold-to-compare (plan T3 + section 9).
    // Explicit ids: 820 sits inside this range but is the black & white switch,
    // which has its own handler below (a range test silently swallowed it and the
    // following sync reset the checkbox).
    if(msg==WM_COMMAND&&HIWORD(wp)==BN_CLICKED&&(LOWORD(wp)==801||LOWORD(wp)==802||LOWORD(wp)==822||LOWORD(wp)==823||LOWORD(wp)==824)){
        const int id=LOWORD(wp);
        if(id==801){
            auto neutral=engine::ColorSettings{};neutral.enabled=true;
            if(applyColour(neutral,false))message(L"Reset to neutral; you can click \"Undo\" to step back.");
        }else if(id==802){
            if(colourHistoryIndex>0){
                --colourHistoryIndex;
                if(applyColour(colourHistory[size_t(colourHistoryIndex)],false,false))message(L"Undid the last step.");
            }else message(L"No steps to undo.");
        }else if(id==824){
            if(colourHistoryIndex>=0&&colourHistoryIndex+1<int(colourHistory.size())){
                ++colourHistoryIndex;
                if(applyColour(colourHistory[size_t(colourHistoryIndex)],false,false))message(L"Redone.");
            }else message(L"No steps to redo.");
        }else if(id==822){
            colourClipboard=colourTarget();colourClipboardValid=true;
            message(L"Copied the current color settings; you can paste onto another preset or the next clip.");
        }else if(id==823){
            if(colourClipboardValid&&applyColour(colourClipboard,true))message(L"Color settings pasted.");
            else if(!colourClipboardValid)message(L"There are no color settings on the clipboard yet; click \"Copy\" first.");
            else message(L"Settings are switching; try again shortly.");
        }
        syncColorControls();arrange();return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==colorMixerModeId&&HIWORD(wp)==BN_CLICKED){
        colourBlackWhite=SendMessageW(item(colorMixerModeId),BM_GETCHECK,0,0)==BST_CHECKED;
        auto colour=colourTarget();
        if(colour.blackWhite!=colourBlackWhite){
            colour.blackWhite=colourBlackWhite;
            if(!applyColour(colour,true)){
                veyra::log::warn("color-ui","mixer correction switch rejected");
                syncColorControls();
                return 0;
            }
        }
        veyra::log::info("color-ui",std::format("mixer blackWhite={}",colourBlackWhite?1:0));
        message(colourBlackWhite?L"Black & white mixer on: image turns monochrome and the \"B&W\" sliders appear below.":L"Back to HSL mixing (hue/saturation/luminance).");
        syncColorControls();
        arrange();
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)>=colorCurveChannelId&&LOWORD(wp)<=colorCurveChannelId+3&&HIWORD(wp)==BN_CLICKED){
        colourCurveChannel=LOWORD(wp)-colorCurveChannelId;
        veyra::log::info("color-ui",std::format("curve channel selected={}",colourCurveChannel));
        if(auto canvas=item(colorCurveCanvasId))InvalidateRect(canvas,nullptr,FALSE);
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==colorCurveResetId&&HIWORD(wp)==BN_CLICKED){
        auto colour=colourTarget();
        curveForChannel(colour,colourCurveChannel).reset();
        if(applyColour(colour,true)){
            message(L"This channel's curve has been flattened.");
            veyra::log::info("color-ui",std::format("curve flattened channel={}",colourCurveChannel));
        }
        if(auto canvas=item(colorCurveCanvasId))InvalidateRect(canvas,nullptr,FALSE);
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)>=810&&LOWORD(wp)<810+kColorSections&&HIWORD(wp)==BN_CLICKED){
        const int section=LOWORD(wp)-810;
        colorFoldMask^=1u<<section;
        saveColourFoldState();
        arrange();
        return 0;
    }
    // --- colour presets + the .cube picker (T5-b) ---------------------------
    if(msg==WM_COMMAND&&LOWORD(wp)>=805&&LOWORD(wp)<=809&&HIWORD(wp)==BN_CLICKED){
        const int id=LOWORD(wp);
        engine::ColorLookStore lookStore(runtime::localDataDirectory());
        if(id==805){
            wchar_t name[64]{};GetWindowTextW(item(804),name,64);
            if(std::wstring(name).find_first_not_of(L" \t\r\n")==std::wstring::npos){
                SetFocus(item(804));
                MessageBoxW(window,L"Enter a preset name, then click Save.",L"Save color preset",MB_OK|MB_ICONINFORMATION);
                return 0;
            }
            if(!lookStore.load()){
                MessageBoxW(window,lookStore.error().c_str(),L"Save failed",MB_OK|MB_ICONERROR);return 0;
            }
            const bool exists=std::any_of(lookStore.entries().begin(),lookStore.entries().end(),[&](const auto& entry){return entry.name==name;});
            if(exists&&MessageBoxW(window,L"A preset with this name already exists. Overwrite?",name,MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
            auto colour=colourTarget();colour.enabled=true;
            if(lookStore.put(name,colour,true)){
                refreshColourLooks(name);
                message(L"Color preset saved: "+std::wstring(name));
                veyra::log::info("color-ui","named preset saved to disk and selected");
            }else MessageBoxW(window,lookStore.error().c_str(),L"Save failed",MB_OK|MB_ICONERROR);
        }else if(id==806){
            const int index=int(SendMessageW(item(803),CB_GETCURSEL,0,0));
            if(lookStore.load()&&index>=1&&size_t(index-1)<lookStore.entries().size()){
                const auto& colour=lookStore.entries()[size_t(index-1)].color;
                const bool applied=applyColour(colour,true);
                veyra::log::info("color-ui",std::format("preset apply index={} exposure={:.3f} lut={} accepted={}",
                    index,colour.exposure,colour.lutNameString().empty()?0:1,applied));
                if(applied){
                    syncColorControls();refreshColourLuts();
                    message(L"Color preset applied (color only; NR/super resolution/frame generation unchanged).");
                }else message(L"Settings are switching; try again shortly.");
            }else{
                veyra::log::info("color-ui",std::format("preset apply rejected index={} entries={}",index,lookStore.entries().size()));
                message(L"Select a preset from the list first.");
            }
        }else if(id==807){
            const int index=int(SendMessageW(item(803),CB_GETCURSEL,0,0));
            if(lookStore.load()&&index>=1&&lookStore.erase(size_t(index-1))){
                refreshColourLooks();
                message(L"Color preset deleted.");
            }else message(L"Delete failed: "+lookStore.error());
        }else if(id==808){
            const int index=int(SendMessageW(item(803),CB_GETCURSEL,0,0));
            const auto path=pickColourSave(L"Veyra color preset (*.vpcolor)\0*.vpcolor\0All files (*.*)\0*.*\0\0",L"Export color preset",L"look.vpcolor");
            if(!path.empty()&&lookStore.load()&&lookStore.exportFile(size_t(std::max(0,index-1)),path))message(L"Exported .vpcolor.");
            else if(!path.empty())message(L"Export failed: "+lookStore.error());
        }else{
            const auto path=pickColourFile(L"Veyra color preset (*.vpcolor)\0*.vpcolor\0All files (*.*)\0*.*\0\0",L"Import color preset");
            if(!path.empty()){
                std::wstring name;
                if(lookStore.load()&&lookStore.importFile(path,name)){refreshColourLooks(name);message(L"Preset imported: "+name);}
                else message(L"Import failed: "+lookStore.error());
            }
        }
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==817&&HIWORD(wp)==CBN_SELCHANGE){
        const int index=int(SendMessageW(item(817),CB_GETCURSEL,0,0));
        auto colour=colourTarget();colour.enabled=true;
        if(index<=0)colour.clearLut();
        else if(size_t(index-1)<colourLutNames.size()&&!colour.setLutName(colourLutNames[size_t(index-1)])){message(L"Invalid LUT name.");return 0;}
        if(index>0&&colour.lutStrength<=0.0f)colour.lutStrength=100.0f;
        if(!applyColour(colour,false))message(L"Settings are switching; try again shortly.");
        else message(index<=0?L"LUT disabled.":L"LUT selected; the pipeline rebuilds once, so a brief pause is normal.");
        syncColorControls();
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==819&&HIWORD(wp)==CBN_SELCHANGE){
        const int index=int(SendMessageW(item(819),CB_GETCURSEL,0,0));
        auto colour=colourTarget();
        colour.lutInputSpace=std::clamp(index,0,2);
        if(applyColour(colour,true))message(L"Switched the LUT input space (logged).");
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==818&&HIWORD(wp)==BN_CLICKED){
        const auto path=pickColourFile(L"Cube LUT (*.cube)\0*.cube\0All files (*.*)\0*.*\0\0",L"Import .cube LUT");
        if(!path.empty()){
            engine::ColorLutStore lutStore(runtime::localDataDirectory());
            std::wstring name;std::string error;
            if(lutStore.importFile(path,name,error)&&!name.empty()){
                auto colour=colourTarget();colour.enabled=true;
                colour.setLutName(name);
                if(colour.lutStrength<=0.0f)colour.lutStrength=100.0f;
                refreshColourLuts();
                if(!applyColour(colour,false))message(L"LUT imported, but settings are switching; just reselect it later.");
                else message(L"Imported and selected LUT: "+name+L" (written to manifest).");
            }else message(L"Import failed: "+std::wstring(error.begin(),error.end()));
        }
        return 0;
    }
    if(msg==WM_COMMAND&&!populating&&!syncingColour&&LOWORD(wp)>=colorEditId(0)&&LOWORD(wp)<colorEditId(0)+kColorMaxParams&&HIWORD(wp)==EN_CHANGE){
        const int index=LOWORD(wp)-colorEditId(0);
        if(index<int(colorParams.size())){
            editDrafts.insert(LOWORD(wp));
            wchar_t buffer[64]{};GetWindowTextW(item(LOWORD(wp)),buffer,64);
            wchar_t* end=nullptr;const float value=wcstof(buffer,&end);
            if(end==buffer||*end||!std::isfinite(value))message(L"Value incomplete; keeping last valid value");
            else if(colourFieldEdited(index,value))editDrafts.erase(LOWORD(wp));
            syncRowResets();
        }
        return 0;
    }
    if(msg==WM_COMMAND&&!populating&&((LOWORD(wp)==209&&HIWORD(wp)==CBN_SELCHANGE)||((LOWORD(wp)==215||LOWORD(wp)==210)&&HIWORD(wp)==BN_CLICKED))){liveField(LOWORD(wp));return 0;}
switch(msg){
case WM_COMMAND:{const int id=LOWORD(wp);if(!populating&&((id>=202&&id<=205||id==207||id==208)&&HIWORD(wp)==CBN_SELCHANGE||(id>=700&&id<=732)&&HIWORD(wp)==BN_CLICKED)){liveField(id);return 0;}if((id==206||id==213||id==214)&&HIWORD(wp)==BN_CLICKED){const auto accepted=SendMessageW(GetParent(h),WM_APP+45,id,checked(206));message(accepted?(id==213?L"Left-drag on the image to draw a box; Esc to cancel.":L"NR exclusion zone update requested."):L"Couldn't do that: open the image first, or clear the 4 zones that are already full.");return 0;}if((id==200||id==201)&&HIWORD(wp)==BN_CLICKED){const bool accepted=SendMessageW(GetParent(h),WM_APP+44,id,checked(id))!=0;message(accepted?L"Toggle requested; takes effect after the frame boundary result is confirmed.":L"The master enhancement is switching; wait for the current transaction to finish.");return 0;}if(HIWORD(wp)==EN_SETFOCUS){for(auto& item:items)if(GetDlgCtrlID(item.h)==id&&item.page==page){RECT r{};GetClientRect(h,&r);int height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(h))-128;if(item.y<scroll)scroll=item.y;if(item.y+item.height>scroll+height)scroll=item.y+item.height-height;arrange(true);break;}}if(!populating&&id>=100&&id<=111&&HIWORD(wp)==EN_CHANGE){liveField(id);return 0;}
    if(!populating&&id>=100&&id<=111&&HIWORD(wp)==EN_KILLFOCUS){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);return 0;}
    engine::EnhancementSettings s;
    if(id==211){SetFocus(body);s={};if(submit(s)){editDrafts.clear();populate(s);}message(L"Restored built-in defaults.");}
    else if(id>=501&&id<=505)SendMessageW(GetParent(h),WM_APP+41,id,id==501?send(500,CB_GETCURSEL,0,0):id==505?checked(505):0);
    return 0;}
}return DefWindowProcW(h,msg,wp,lp);
}

__declspec(noinline) LRESULT settingsTimer(HWND h,UINT msg,WPARAM wp,LPARAM lp){
switch(msg){
case WM_TIMER:{auto s=controller->snapshot();putText(1150,s.presentationStatus.c_str());
    // XeSS/FSR owns Present, so grey out what cannot reach its frames. The
    // flag comes from the engine, never from parsing the status text.
    {const bool owned=s.presentationProviderOwned;
     for(int id:{240,243})EnableWindow(item(id),!owned);
     EnableWindow(item(244),!owned&&s.presentation.outputRate==engine::OutputRateMode::Custom);}
syncProtection(enhancementEnabled?s.desired.protection:configuredSettings.protection);if(enhancementEnabled&&!dirty&&displayedSettings!=s.desired)populate(s.desired);syncColorControls();check(200,enhancementEnabled&&s.desired.nr?BST_CHECKED:BST_UNCHECKED);check(201,enhancementEnabled&&s.desired.sr?BST_CHECKED:BST_UNCHECKED);std::wostringstream o;if(!s.running&&!s.frames&&s.transport!=engine::TransportState::Opening)o<<L"No media open · settings pending enable\n";else{
    o<<L"Desired version "<<s.desired.revision<<L" / Applied "<<s.applied.revision<<(s.applying?L" · applying":L"");
    const wchar_t* backend=s.applied.frameGenerationBackend==engine::FrameGenerationBackend::XeSS?L"XeSS":s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Fsr?L"AMD FSR":L"DLSS";
    o<<L"\n"<<backend<<L" · "<<(s.applied.multiplier<=1?L"Frame generation off":s.fgActive?L"Frame generation running":L"Waiting for valid frame generation");
    if(s.applied.multiplier>1&&s.previewFgMultiplier>1&&s.previewFgMultiplier<s.applied.multiplier)
        o<<L" · Target "<<s.applied.multiplier<<L"X / Current "<<s.previewFgMultiplier<<L"X";
    if(!s.backendWarning.empty())message(s.backendWarning);
    else if(!displayedBackendWarning.empty())message(L"Settings applied");
    displayedBackendWarning=s.backendWarning;
}setText(item(400),o.str());
    const wchar_t* hdrStatus=s.failed?L"Playback failed, HDR preview unavailable":
        s.transport==engine::TransportState::Opening?L"Waiting for first frame, HDR status pending":
        s.applying?L"Applying settings, HDR status pending":
        !s.running&&!s.frames?L"No media open, HDR settings pending apply":
        s.videoHdrStatus.empty()?L"Waiting for preview status":s.videoHdrStatus.c_str();
    putText(1145,hdrStatus);
    return 0;}
}return DefWindowProcW(h,msg,wp,lp);
}

LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
case WM_CREATE:return createSettingsWindow(h,msg,wp,lp);
case WM_SIZE:arrange();return 0;
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);return 0;}
case WM_VSCROLL:{switch(LOWORD(wp)){case SB_LINEUP:scroll-=40;break;case SB_LINEDOWN:scroll+=40;break;case SB_PAGEUP:scroll-=240;break;case SB_PAGEDOWN:scroll+=240;break;case SB_THUMBTRACK:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(h,SB_VERT,&si);scroll=si.nTrackPos;break;}}arrange(true);return 0;}
case WM_MOUSEWHEEL:scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*36;arrange(true);return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_COMMAND:return settingsCommand(h,msg,wp,lp);
case WM_HSCROLL:{
    if(msg==WM_HSCROLL&&!populating&&lp){const auto id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));if(id>=631&&id<=634){liveField(id);return 0;}}
int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));if(id>=600&&id<612){int index=id-600;float v=float(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0))/(index>=4&&index<=6?1:100);if(index==3&&v<0)v=-1;std::wostringstream o;o<<std::setprecision(4)<<v;putText(100+index,o.str().c_str());}
    else if(id>=colorSliderId(0)&&id<colorSliderId(0)+kColorMaxParams){
        const int index=id-colorSliderId(0);
        if(index<int(colorParams.size())){
            float value=float(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0))/100.0f;
            // Alt+drag = fine adjust (plan section 3.3): the thumb may jump, but
            // the applied value only moves a tenth of the way towards it, and the
            // sync below pulls the thumb back so repeated Alt-drags stay fine.
            if((GetKeyState(VK_MENU)&0x8000)!=0){
                const float current=colorParams[size_t(index)].value(colourTarget());
                value=current+(value-current)*0.1f;
            }
            if(colourFieldEdited(index,value)){editDrafts.erase(colorEditId(index));syncColorControls();}
        }
    }
    else if(id==622){// Feather slider: the edit box owns the value, its EN_CHANGE applies it.
        const int value=std::clamp(int(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0)),0,64);putText(222,std::to_wstring(value).c_str());}
    return 0;}
case WM_TIMER:return settingsTimer(h,msg,wp,lp);
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;body=nullptr;items.clear();rowResets.clear();editDrafts.clear();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
engine::EnhancementSettings defaultSettings(){loadStore();return store.defaultSettings();}
HWND settingsControlForTest(int id){return item(id);}
// Exported wrapper: the sink itself lives in this translation unit's anonymous
// namespace, so the function has to be defined out here to link.
void settingsStatusSink(std::function<void(const std::wstring&)> sink){statusSink=std::move(sink);}
bool settingsColorWheelTestPoint(int zone,float hue,float saturation,POINT& out){
    const auto wheel=item(colorWheelId(zone));
    if(!wheel)return false;
    RECT r{};GetClientRect(wheel,&r);
    const auto geometry=wheelGeometry(wheel,r);
    const float angle=hue*0.0174532925f;
    const float radius=std::clamp(saturation,0.0f,100.0f)/100.0f*geometry.size*0.5f;
    out.x=LONG(geometry.cx+std::cos(angle)*radius);
    out.y=LONG(geometry.cy+std::sin(angle)*radius);
    return true;
}
bool settingsColorWheelTestBarPoint(int zone,float luminance,POINT& out){
    const auto wheel=item(colorWheelId(zone));
    if(!wheel)return false;
    RECT r{};GetClientRect(wheel,&r);
    const auto geometry=wheelGeometry(wheel,r);
    const float fraction=(std::clamp(luminance,-100.0f,100.0f)+100.0f)/200.0f;
    out.x=LONG(geometry.barLeft+(geometry.barRight-geometry.barLeft)*fraction);
    out.y=LONG(geometry.barY);
    return true;
}
int colourWheelControlId(int zone){return colorWheelId(zone);}
int colourBandsControlId(){return colorBandsId;}
int colourCurveCanvasControlId(){return colorCurveCanvasId;}
int colourSectionEyeControlId(int section){return colorSectionEyeId(std::clamp(section,0,kColorSections-1));}
void settingsColorSectionForTest(int section,bool expanded){
    if(section<0||section>=kColorSections)return;
    if(expanded)colorFoldMask&=~(1u<<section);
    else colorFoldMask|=1u<<section;
    saveColourFoldState();
    arrange();
}
bool settingsCurveTestPoint(float x,float y,POINT& out){
    const auto canvasControl=item(colorCurveCanvasId);
    if(!canvasControl)return false;
    RECT r{};GetClientRect(canvasControl,&r);
    const auto canvas=curveCanvasRect(canvasControl,r);
    out.x=LONG(canvas.left+std::clamp(x,0.0f,1.0f)*float(canvas.right-canvas.left));
    out.y=LONG(canvas.bottom-std::clamp(y,0.0f,1.0f)*float(canvas.bottom-canvas.top));
    return true;
}
void settingsColorScrollToTest(int id){
    if(!window||!body)return;
    for(auto& entry:items)if(GetDlgCtrlID(entry.h)==id&&entry.page==2){
        RECT r{};GetClientRect(window,&r);
        const int height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(window))-128;
        if(entry.y<scroll)scroll=entry.y;
        if(entry.y+entry.height>scroll+height)scroll=entry.y+entry.height-height;
        arrange(true);
        veyra::log::info("color-ui",std::format("scroll-to id={} y={} height={} viewport={} scroll={} content={}",id,entry.y,entry.height,height,scroll,colorPageContentHeight));
        return;
    }
}
void settingsColorMixerModeForTest(int mode){
    colourBlackWhite=mode==3;
    check(colorMixerModeId,colourBlackWhite?BST_CHECKED:BST_UNCHECKED);
    auto colour=colourTarget();
    bool applied=true;
    if(colour.blackWhite!=colourBlackWhite){colour.blackWhite=colourBlackWhite;applied=applyColour(colour,true);}
    veyra::log::info("color-ui",std::format("mixer blackWhiteAsked={} applied={}",colourBlackWhite?1:0,applied?1:0));
    syncColorControls();
    arrange();
}
void settingsColorBandForTest(int band){
    colourMixerBand=std::clamp(band,0,engine::kColorMixerBands-1);
    if(auto bands=item(colorBandsId))InvalidateRect(bands,nullptr,FALSE);
    arrange();
}
int colourParamEditId(const wchar_t* label){
    if(!label)return -1;
    if(std::wcscmp(label,L"混合")==0)label=L"Blending";
    if(std::wcscmp(label,L"LUT 强度")==0)label=L"LUT strength";
    for(size_t i=0;i<colorParams.size();++i)if(std::wcscmp(colorParams[i].label,label)==0)return colorEditId(int(i));
    return -1;
}
HWND createSettingsPanel(HWND parent,engine::EngineController& engine,std::function<bool(engine::EnhancementSettings)> callback){controller=&engine;apply=std::move(callback);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraInspector";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Professional parameters",WS_CHILD|WS_CLIPCHILDREN,0,0,328,500,parent,nullptr,wc.hInstance,nullptr);}
void settingsVisibility(bool visible){if(window&&!visible&&IsChild(window,GetFocus()))SetFocus(GetParent(window));}
void settingsPage(int value){if(window&&IsChild(window,GetFocus()))SetFocus(body);page=std::clamp(value,0,4);scroll=0;arrange();}
void settingsEnabled(bool enabled,const engine::EnhancementSettings& configured){enhancementEnabled=enabled;configuredSettings=configured;if(window)syncProtection(enabled?controller->snapshot().desired.protection:configured.protection);if(window&&!enabled&&!dirty&&displayedRevision!=configured.revision)populate(configured);if(window){auto desired=controller->snapshot().desired;check(200,enabled&&desired.nr?BST_CHECKED:BST_UNCHECKED);check(201,enabled&&desired.sr?BST_CHECKED:BST_UNCHECKED);if(!enabled)message(L"Enhancement is off. Value changes are saved to the pending-enable config; click NR / super resolution to enable directly.");}}
void settingsDpi(){if(!window)return;auto old=font;font=makeFont(window);for(auto& item:items)SendMessageW(item.h,WM_SETFONT,WPARAM(font),TRUE);DeleteObject(old);arrange();}
void exportPanelStatus(const engine::ExportJobSnapshot& job,bool canExport,bool canSave){if(!window)return;setText(item(506),job.state==engine::ExportState::Idle?L"No export job yet":job.message+L"\n"+std::to_wstring(int(job.progress*100))+L"% · Source frames "+std::to_wstring(job.sourceFrames)+L" / Encoded "+std::to_wstring(job.encoded)+L"\nGenerated "+std::to_wstring(job.generated)+L" / CFR holds "+std::to_wstring(job.holds)+L"\nJob "+std::to_wstring(job.jobId)+L" · Frozen version "+std::to_wstring(job.frozenRevision));setText(item(507),job.output.empty()?L"Destination chosen in the save dialog":L"Output: "+std::filesystem::path(job.output).filename().wstring());EnableWindow(item(501),canExport&&!job.active());EnableWindow(item(502),canSave);EnableWindow(item(503),job.state==engine::ExportState::Running||job.state==engine::ExportState::Paused);EnableWindow(item(504),job.active());}
}
