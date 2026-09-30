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
        L"NR出手有多重。0–1，先小口尝，拉满不等于一定更好看。",
        L"调节NR局部明暗倾向。0–1，光影师的旋钮，不是屏幕亮度。",
        L"调节NR局部结构倾向。0–1，细节可能更明显，别把噪点也请上台。",
        L"肤质实验参数：-1用默认，其余0–2。效果尚未证实，不保证一键磨皮。",
        L"切换0/1/2三个实验风格值。口味自己试，编号不是画质排名。",
        L"自动遮罩实验开关。是否改善当前素材需要实测，不是万能遮羞布。",
        L"UI修正实验开关，效果尚未证实。想保住字幕，优先框选NR剔除区。",
        L"控制NR改动最终混入多少。0保留底图，往上加料；范围0–2。",
        L"控制NR把画面变暗的那部分变化。0–2，暗部别一口气踩到底。",
        L"控制NR把画面变亮的那部分变化。0–2，别把灯泡当太阳。",
        L"控制NR带来的色彩变化。0–2，口味再重也要留意肤色。",
        L"控制NR带来的明度变化。0–2，保留细节和改变观感之间找平衡。"};
    if(id>=100&&id<=111)return model[id-100];
    if(id>=600&&id<=611)return model[id-600];
    if(id>=700&&id<=702)return model[4];if(id==710||id==711)return model[5];if(id==720||id==721)return model[6];
    if(id>=730&&id<=732)return L"选择超分输出上限：2K / 4K / 8K，保持比例，不放大小于1倍的目标。像素越多，显卡的饭量越大。";
    switch(id){
    case 200:return L"这玩意就是我们接入的实验性DLSS5 NR：用神经网络重建画面细节。不是游戏原生集成，效果看素材，也看显卡。";
    case 201:return L"把画面放大到所选目标尺寸。不是凭空找回所有细节，显卡也不是许愿池。";
    case 202:return L"补帧倍率：2X插1张，3X插2张，4X插3张。目标帧率涨了，显卡不一定跟得上；拖影也可能来凑热闹。";
    case 203:return L"选择NR内部处理上限，按画面比例缩放，再回填输出。低档省算力，高档留细节；原生档让显卡加班。视频导出仍用完整尺寸。";
    case 204:return L"光流质量档：估计物体怎么移动。质量档更费工，运动估错了，补帧也可能跟着跑偏。";
    case 205:return L"选择内容节奏。采集60→30适合60Hz信号里重复的30帧游戏内容；真60帧别硬砍半。时间戳仍保留。";
    case 206:return L"剔除区里的NR改动会被抑制、保留原图，最多4块。只管NR，不给超分和补帧当保镖。";
    case 207:return L"选择DLSS SR或RTX视频超分，以及视频超分质量。两条路线各有脾气，试着看画质和耗时。";
    case 208:return L"选择DLSS补帧或实验XeSS补帧。AMD FSR补帧已在1.4.0从界面移除（切回DLSS容易卡住、效果也一般）：引擎后端保留，但面板不再提供，旧设置或旧预设里若存了FSR，打开时会自动切回DLSS并在日志里写明。XeSS的生成帧只存在于显示交换链，导出管线拿不到；导出时会自动改用DLSS补帧，显卡不支持则关闭补帧。";
    case 508:return L"导出码率：只决定导出文件的大小与画质，不影响预览。自动＝编码器恒定质量档（NVENC CONSTQP / 系统编码器质量模式）。H.264 一般 10-40 Mbps 够用，4K 或高动态建议 60-150；HEVC 同等画质可以取更低。";
    case 209:return L"选择运动估算后端。NVOF、FidelityFX、GPU DIS负责看出物体怎么动。DIS是实验算法，走通用计算单元，可能和NR抢活干，不保证更快；选它不会换掉你的补帧方式，也不会解锁AMD NR。";
    case 210:return L"补帧的准入节奏。关闭（默认）＝1.4.0 的规则：这一组生成帧只要整组能赶上最后一张的期限就生成。开启＝更严格：组里第一张赶不上自己的期限就整组不生成，节奏更整齐，但生成的帧更少。";
    case 211:return L"把增强参数恢复为内建默认。调迷路了就走这里，画质参数立即回到出厂状态。";
    case 213:return L"在画面上拖框添加NR剔除区，Esc取消。把不想被NR改的地方圈出来。";
    case 214:return L"清空当前NR剔除框。只是撤掉围栏，不会删除视频。";
    case 222:return L"剔除区边缘的过渡宽度，单位是工作分辨率像素：0 就是硬边，12 约等于4K画面高度的0.5%，越大过渡越柔和。改完立即生效。";
    case 800:return L"调色总开关。开着时调色在所有效果器（NR/超分/补帧）之前执行，会有一点额外开销；关掉后这条链完全不存在，零开销，画面回到未调色的状态。";
    case 801:return L"把色彩页所有参数还原为中性（不改变总开关）。还原前的数值会留一次撤销机会。";
    case 802:return L"找回上一次“一键还原”之前的数值。";
    case 215:return L"把AMD光流输入宽高各减半，少算一些像素。速度可能更好，细小运动可能看不清。";
    case 216:return L"自动估算声音补偿，或改用手动偏移。目标是嘴和声音一起到，别让角色自带配音延迟。";
    case 217:return L"-250到250ms：正值让声音更晚，负值减少已有补偿，不能把未来声音变出来。";
    case 218:return L"选择NR运行文件。默认版为社区Lecram 310.8.3，面向RTX50，画面与NVIDIA原版一致且略省功耗；社区修改版尝试兼容RTX40/50；RTX 30兼容版为社区SF-v2（自带RTX 20/30的FP16路线，30系实际效果待持卡验证）。切换会重建链路，短暂停顿正常。";
    case 219:return L"切换直播兼容显示路径。给捕获软件搭个台阶，不是让所有捕获方式都突然开窍。";
    case 220:return L"默认先超分→NR（DLSS5）→补帧。打开后NR→超分→补帧，可能更快，也可能多些鬼影或边缘瑕疵。默认关闭，仅预览；需同时开启NR和超分。导出顺序不变。";
    case 500:return L"H.264兼容性广，HEVC通常更省空间。HDR视频须选HEVC，保存为Main10 / PQ；别把HDR硬塞进普通H.264。编码器按显卡自动选择：N卡用NVENC（零拷贝最快），A卡/I卡用系统硬件编码（驱动自带的H.264/HEVC编码器），HDR导出仍需N卡。";
    case 501:return L"选保存位置并导出视频。参数在开始时冻结，NR按原生处理；低延迟预览顺序不带进导出。";
    case 502:return L"保存当前图片或视频帧。精彩的一瞬间，留下来。";
    case 503:return L"暂停或继续导出任务，不是暂停你正在看的视频。";
    case 504:return L"取消当前导出。未完成的输出不会冒充成功成片。";
    case 505:return L"导出时优先保证观看，导出慢一点。两边同时抢显卡，总得有人礼让。";
    default:return nullptr;
    }
}
}
