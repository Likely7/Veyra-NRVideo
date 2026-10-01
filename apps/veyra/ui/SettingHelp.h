#pragma once
#include "veyra/Log.h"
// V2 size also works without a Common Controls v6 activation context.
namespace veyra::ui {
inline void installDialogHelp(HWND root,std::initializer_list<std::pair<int,const wchar_t*>> entries){
    auto tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    SetWindowTheme(tip,L"",L"");SendMessageW(tip,TTM_SETTIPBKCOLOR,RGB(28,31,33),0);SendMessageW(tip,TTM_SETTIPTEXTCOLOR,RGB(225,230,228),0);
    SendMessageW(tip,TTM_SETMAXTIPWIDTH,0,360);SendMessageW(tip,TTM_SETDELAYTIME,TTDT_INITIAL,550);SendMessageW(tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);
    for(auto [id,text]:entries){auto child=GetDlgItem(root,id);if(!child)continue;TOOLINFOW info{TTTOOLINFOW_V2_SIZE};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=root;info.uId=UINT_PTR(child);info.lpszText=const_cast<wchar_t*>(text);if(!SendMessageW(tip,TTM_ADDTOOLW,0,LPARAM(&info)))veyra::log::error("ui-help","Tooltip registration failed");}
}
inline const wchar_t* settingHelp(int id){
    static const wchar_t* model[]={
        L"How strong NR is. 0-1, start small; maxed out doesn't always look better.",
        L"Adjusts NR's local light/dark bias. 0-1, a colorist's knob, not screen brightness.",
        L"Adjusts NR's local structure bias. 0-1, detail may stand out more, just don't invite the noise up on stage too.",
        L"Skin-texture experimental parameter: -1 uses the default, otherwise 0-2. The effect is unproven, no guaranteed one-tap skin smoothing.",
        L"Toggles between three experimental style values 0/1/2. Try them to taste; the number isn't a quality ranking.",
        L"Auto-mask experimental toggle. Whether it improves the current footage needs real testing, it's not a cure-all.",
        L"UI-fix experimental toggle, effect unproven. To keep subtitles, prefer marking an NR exclusion region.",
        L"Controls how much of NR's change is finally mixed in. 0 keeps the base image, higher adds more; range 0-2.",
        L"Controls how much NR darkens the darker parts. 0-2, don't push the shadows all the way down at once.",
        L"Controls how much NR brightens the brighter parts. 0-2, don't mistake a light bulb for the sun.",
        L"Controls the color change NR introduces. 0-2, even heavy taste, watch skin tones.",
        L"Controls the luminance change NR introduces. 0-2, balance between keeping detail and changing the look."};
    if(id>=100&&id<=111)return model[id-100];
    if(id>=600&&id<=611)return model[id-600];
    if(id>=700&&id<=702)return model[4];if(id==710||id==711)return model[5];if(id==720||id==721)return model[6];
    if(id>=730&&id<=732)return L"Choose the super-resolution output cap: 2K / 4K / 8K, keeps aspect ratio, won't upscale targets below 1x. More pixels means a hungrier GPU.";
    switch(id){
    case 200:return L"This is the experimental DLSS5 NR we integrated: it rebuilds image detail with a neural network. It's not a native game integration; results depend on the footage and on your GPU.";
    case 201:return L"Upscales the image to the chosen target size. It doesn't magically recover all detail, and the GPU isn't a wishing well.";
    case 202:return L"Frame-gen multiplier: 2X inserts 1 frame, 3X inserts 2, 4X inserts 3. The target frame rate goes up, but the GPU may not keep up, and ghosting may join the party too.";
    case 203:return L"Choose NR's internal processing cap, scaled by the image ratio, then filled back into the output. Lower saves compute, higher keeps detail; native makes the GPU work overtime. Video export still uses the full size.";
    case 204:return L"Optical-flow quality tier: estimates how objects move. The quality tier costs more work, and if motion is estimated wrong, frame gen can go astray too.";
    case 205:return L"Choose the content cadence. Capture 60->30 suits 30-fps game content repeated within a 60Hz signal; don't force real 60 fps in half. Timestamps are still preserved.";
    case 206:return L"NR changes inside an exclusion region are suppressed and the original is kept, up to 4 regions. It only affects NR; it won't guard super resolution or frame gen.";
    case 207:return L"Choose DLSS SR or RTX Video Super Resolution, and the video super-resolution quality. The two paths each have their quirks, so try them and watch quality and timing.";
    case 208:return L"Choose DLSS frame gen or experimental XeSS frame gen. AMD FSR frame gen was removed from the UI in 1.4.0 (switching back to DLSS tended to stall, and results were mediocre): the engine backend is kept, but the panel no longer offers it; if old settings or an old preset stored FSR, opening it auto-switches back to DLSS and notes it in the log. XeSS generated frames exist only in the display swap chain and the export pipeline can't get them; export automatically switches to DLSS frame gen, and if the GPU doesn't support it, frame gen is turned off.";
    case 508:return L"Export bitrate: only affects export file size and quality, not the preview. Auto = the encoder's constant-quality mode (NVENC CONSTQP / system encoder quality mode). H.264 is usually fine at 10-40 Mbps; 4K or high-motion suggests 60-150; HEVC can go lower for the same quality.";
    case 209:return L"Choose the motion-estimation backend. NVOF, FidelityFX, and GPU DIS handle detecting how objects move. DIS is an experimental algorithm that runs on general compute units, may compete with NR for resources, and isn't guaranteed faster; choosing it won't change your frame-gen method, nor unlock AMD NR.";
    case 210:return L"Frame gen's admission cadence. Off (default) = the 1.4.0 rule: this group of generated frames is generated as long as the whole group can meet the last frame's deadline. On = stricter: if the first frame in the group misses its own deadline, the whole group is skipped, giving a steadier cadence but fewer generated frames.";
    case 211:return L"Restores enhancement parameters to the built-in defaults. If you got lost tuning, come here; the quality parameters return to factory state immediately.";
    case 213:return L"Drag a box on the image to add an NR exclusion region, Esc to cancel. Fence off the areas you don't want NR to change.";
    case 214:return L"Clears the current NR exclusion boxes. It only removes the fences; it won't delete the video.";
    case 222:return L"Transition width at the edge of an exclusion region, in working-resolution pixels: 0 is a hard edge, 12 is about 0.5% of a 4K image's height; larger is softer. Takes effect immediately.";
    case 800:return L"Master color-grading switch. When on, grading runs before all effects (NR/super res/frame gen) with a little extra overhead; when off, this chain doesn't exist at all, zero overhead, and the image returns to its ungraded state.";
    case 801:return L"Resets all parameters on the color page to neutral (without changing the master switch). The values before the reset are kept for one undo.";
    case 802:return L"Restores the values from before the last one-tap reset.";
    case 215:return L"Halves the width and height of the AMD optical-flow input to compute fewer pixels. Speed may improve, but fine motion may be harder to see.";
    case 216:return L"Auto-estimate audio compensation, or switch to a manual offset. The goal is for lips and sound to arrive together, so characters don't come with built-in dub delay.";
    case 217:return L"-250 to 250ms: positive delays the sound, negative reduces existing compensation; it can't conjure future sound.";
    case 218:return L"Choose the NR runtime file. The original targets RTX50; the community-modified build attempts RTX40/50 compatibility. Switching rebuilds the pipeline; a brief pause is normal.";
    case 219:return L"Toggles the live-broadcast compatible display path. It gives capture software a leg up; it won't suddenly make every capture method work.";
    case 220:return L"By default the order is super res -> NR (DLSS5) -> frame gen. When on, NR -> super res -> frame gen, which may be faster but may add more ghosting or edge artifacts. Off by default, preview only; requires both NR and super resolution enabled. The export order is unchanged.";
    case 500:return L"H.264 is broadly compatible; HEVC is usually more space-efficient. HDR video must use HEVC, saved as Main10 / PQ; don't cram HDR into ordinary H.264. The encoder is chosen automatically by GPU: NVIDIA uses NVENC (zero-copy, fastest), AMD/Intel use system hardware encoding (the driver's built-in H.264/HEVC encoder); HDR export still requires NVIDIA.";
    case 501:return L"Choose a save location and export the video. Parameters are frozen at the start and NR is processed at native; the low-latency preview order isn't carried into export.";
    case 502:return L"Saves the current image or video frame. Keep that great moment.";
    case 503:return L"Pauses or resumes the export task, not the video you're watching.";
    case 504:return L"Cancels the current export. An unfinished output won't pass itself off as a finished result.";
    case 505:return L"Prioritizes smooth viewing during export, at the cost of a slower export. Both sides compete for the GPU, so someone has to yield.";
    default:return nullptr;
    }
}
}
