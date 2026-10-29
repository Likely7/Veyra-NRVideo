# 三段式超分（单层）方案 2026-10-28

状态：**已在分支`claude/sr-stage-20261028`实施，未合并**，结果与偏差见[实施报告](SR_STAGE_REPORT_2026-10-28.md)。用户2026-10-28要求“先实现单层的，并且使用RTX视频超分测试，SR效果非常一般，先写方案”。

## 1. 目标与范围

把现有“一步超分”扩成单层三段：**先降到 → 超分到 → 再缩到**。

| 场景 | 片源 | 设置 | 实际尺寸 |
|---|---|---|---|
| 糊的4K（老游戏/老电影放大母版、PS5低内部分辨率输出） | 3840×2160 | 先降1080p · 超分到4K | 3840×2160 → 1920×1080 → VSR 3840×2160 |
| 同上，保留更多原细节 | 3840×2160 | 先降1440p · 超分到4K | → 2560×1440 → VSR 3840×2160（1.5x） |
| 原生2K仍不够清楚（超采样） | 2560×1440 | 超分到8K · 再缩到4K | → VSR 7680×4320（3x）→ 面积缩到3840×2160 |
| 原生2K，先降后超分 | 2560×1440 | 先降1080p · 超分到4K | → 1920×1080 → VSR 3840×2160 |
| 糊的4K + 超采样 | 3840×2160 | 先降1080p · 超分到8K · 再缩到4K | → 1920×1080 → VSR 7680×4320（4x）→ 3840×2160 |
| 21:9 | 3840×1600 | 先降1080p · 超分到4K | → 1920×800 → VSR 3840×1600 |

- **本阶段后端只做RTX视频超分（质量1–4）**，作为用户指定的测试后端。DLSS SR/FSR需要把光流运动矢量换算到先降后的尺寸，放到后续阶段；选这两个后端时两项新设置禁用并写明原因，不静默生效。
- 默认“先降：关闭 / 再缩：不缩”必须与现有main（2.1.1）**逐像素同行为**（含压缩视频原尺寸修复、采集卡原尺寸跳过）。
- 数据按“第0层”设计，以后加2–3层不改已保存格式的含义（见第8节）。
- 本阶段不做：细节保留混合、自动识别有效分辨率、descale、多层、DLSS/FSR三段式。

## 2. 现状（代码事实）

- `ResolutionPlan::make`（`include/veyra/pipeline/ResolutionPlan.h`）：仅当目标框大于片源才放大，`base`=超分输出=工作尺寸；`srApplied=sr&&base!=source`。4K片源+4K目标不放大。
- `GraphDescription.h:121-127`：RTX视频超分在原尺寸对**压缩视频**仍运行（`nativeVideoRestore`）；无压缩采集源原尺寸跳过（5090日志57：4K→4K VSR约8.7ms/帧）。
- `EffectChain.cpp:537-550`：链路编译时SR步骤`processing=output=srExtent`，片源已等于目标则不生成SR步骤；SR目录`maxInstances=1`。
- SR参数是全局项：`EnhancementSettings::srTarget/videoSrQuality/dlssSrModel`（`EnhancementSettings.h:339-341`），节点只带开关；`ChainGlobalSettings`（`EffectChain.h`）随列表/节点文档保存；非活动项在`EnhancementSettings.h:421-422`归一化（DLSS模型先例）。
- VSR执行（`EnhanceGraph.cpp:2519-2534`）：`ScaleBlit`把SR输入编码进`videoSrInput_`（R8G8B8A8，SDR模式1/HDR代理模式3）→ `VideoSrBackend::evaluate`（Rect/OutRect取自纹理尺寸）→ 解码回`workRgba_`：SDR用`ScaleBlit`模式-1，HDR用`HdrVideoSrRestore`（双线性取HDR底图与代理输入，`HdrRestore(base,Decode(proxy),Decode(proxyOut))`）。资源在`EnhanceGraph.cpp:723`按`srcW_×srcH_`/`workW_×workH_`创建。
- `shaders/NrDownsample.hlsl`：精确面积（box）缩小，支持非整数比例，可直接复用。
- 持久化：PresetStore当前最高v32；PresetLibrary v15；ChainSession v10。新版本只在用到新字段时写，并换文件后缀让旧版继续读自己的文件（现有规则）。
- 界面：列表模式`ProPage.qml:1313-1372`（超分算法/DLSS模型/目标尺寸/运动来源/质量），节点模式`NodePage.qml`约1539行的SR目标。
- VSR尺寸能力：SDK 1.1头文件只有输入/输出矩形，无能力表；2026-09-10方案已记录“没有足以承诺任意8K输出的能力表”。**5K–8K输出与3x以上倍率必须实测。**

## 3. 设计

### 3.1 设置（第0层）

```cpp
// ResolutionPlan.h —— 追加，不改现有ID
enum class SrPrescale : uint32_t { Off, P720, P900, P1080, P1440, Manual=200 }; // 手动=100+百分比，50..90
enum class SrOutput   : uint32_t { Target, Source, Qhd, Uhd4K, Uhd5K, Uhd6K, Uhd7K }; // Target=2.1.1行为
```

- `EnhancementSettings`、`ChainGlobalSettings`各加`srPrescale=Off`、`srOutput=Target`，紧跟`srTarget`。两者仍为平凡可复制，导出worker共享内存照常（`ExportJobManager.cpp:54`静态断言）。
- 校验：非法枚举拒绝（“invalid SR prescale/output”）。归一化：`!sr`或后端不是RTX视频超分（质量1–4）时两项按默认值进入活动设置，保存值保留——与`dlssSrModel`同一做法，界面同时禁用并写原因。

### 3.2 尺寸规划

`ResolutionPlan`新增`srInput`（先降后）、`srOutput`（VSR实际输出），`base`仍是SR之后整条链的工作尺寸：

1. `srInput=source`；先降生效时按16:9高度框（720/900/1080/1440，与NR尺寸档同一等比算法）或手动百分比缩小，偶数对齐；结果不小于片源则**先降未生效**。
2. `srOutput`=把`srInput`等比放进超分目标框（只放大）。先降关闭时等于现在的`base`，保证回归。
3. `base=srOutput`；“再缩到”框小于`srOutput`时把它等比缩进框内，“片源尺寸”直接取`source`。框不小于`srOutput`则**再缩未生效**。
4. `srApplied=sr&&srOutput!=srInput`。先降后已不小于目标框（如手动90%的4K配2K目标）时，先降视为未生效，整段按现有规则处理（与“已达目标不超分”同义），状态写明原因，不报错打断播放。界面对固定高度的无效组合直接置灰。
5. `nr/flow/fg/output`沿用现有公式，以新`base`为准；NR先行（低延迟）路径不变：NR仍在片源尺寸，先降属于SR步骤内部。

链路编译`compileChainExecutionPlan`改用同一函数：SR步骤`input=cursor`、`processing=srOutput`、`output=base`；只有“先降未生效且无放大且无再缩”才省略SR步骤。`ChainExecutionRequest`加两项。`GraphDescription`把`srInput/srOutput`写入`EnhanceGraphDesc`；`nativeVideoRestore`只在先降未生效时适用，先降生效时采集卡源也执行（用户显式选择）。

### 3.3 GPU流程（只改VSR分支）

```text
SR输入(片源尺寸, 线性FP16)
  └─[先降] NrDownsample面积缩小 → srPrescaled_ (srInput, FP16)
      └─ ScaleBlit编码(SDR=1 / HDR代理=3) → videoSrInput_ (srInput, RGBA8)
          └─ VSR evaluate → videoSrOutput_ (srOutput, RGBA8)
              ├─ srOutput==base：现有解码 / HdrVideoSrRestore（底图改用srPrescaled_）
              └─ srOutput>base ：SrResolve —— 逐SR像素解码/HDR还原后按面积权重平均 → workRgba_ (base)
```

- 新资源：`srPrescaled_`（仅先降时），`videoSrInput_`改为`srInput`尺寸，`videoSrOutput_`改为`srOutput`尺寸；新ComputePass：`srPrescalePass_`（复用`NrDownsample.dxil`，独立描述符）、`srResolvePass_`（新`SrResolve.hlsl`）。
- `SrResolve.hlsl`：面积权重沿用`NrDownsample`的精确算法；SDR解码与HDR还原放进共享`VideoSrRestore.hlsli`，`HdrVideoSrRestore.hlsl`改为include它且数学不变（GPU测试比对改前改后输出一致）。融合解码+缩小，免掉8K FP16中间纹理（253.1MiB）。
- HDR：还原所用底图与代理输入必须同为`srInput`尺寸（先降后的线性底图+`videoSrInput_`），否则高光还原错位。
- 全部在现有命令列表内：无像素回读、无逐pass CPU等待；`GpuStage::Sr`计时覆盖先降+编码+VSR+解码/再缩。
- `nrBeforeSr`/`sourcePostOutput_`选输入的现有分支（描述符18/4）保持，先降读的就是`srStageInput()`。
- 日志：`[video-sr] selected quality=Q source=… prescale=… evaluate=… output=…`；Create/Evaluate失败沿用NGX错误码和`FailedBackend::Sr`，实时重配失败保留旧图与参数（现有applied/回滚）。

### 3.4 显存（RGBA8=4B/像素，FP16=8B/像素）

| 组合 | 新增/变化 |
|---|---|
| 4K→先降1080p→4K | `srPrescaled_` 15.8MiB；`videoSrInput_` 31.6→7.9MiB；其余不变 |
| 2K→VSR 8K→再缩4K | `videoSrOutput_` 126.6MiB；NR/FG/输出纹理全部按4K，而现有“目标8K”整条链都是8K |
| 先降+再缩都关 | 与2.1.1相同 |

### 3.5 界面与状态

列表“超分辨率”卡片和节点SR检查器各加两行，顺序即处理顺序：

- **先降到**：关闭 / 720p / 900p / 1080p / 1440p / 自定义（50–90%）。提示：“片源本身糊时，先缩小再超分，VSR更容易重建；会损失原有的真实细节（如原生4K的HUD、字幕）。”
- **超分到**：现有“目标尺寸”改名，选项不变；提示补充“开启先降到后，片源已达目标也会执行”。
- **再缩到**：不缩 / 片源尺寸 / 2K / 4K / 5K / 6K / 7K，只开放小于“超分到”的选项。提示：“超分到更高再缩回，相当于超采样；8K超分耗时显著增加。”
- 后端不是RTX视频超分时两行禁用，写“目前仅RTX视频超分支持”。
- 卡片摘要示例：“RTX 视频超分 · 1080p→4K · 质量 3”。“实际尺寸”一行取引擎已应用的计划，例：`3840×2160 → 1920×1080 → 3840×2160`，未生效时写原因。不新增刷新定时器，不改33ms/250ms刷新。
- `i18n/catalog.json`补zh-TW/en/ja。导出对话框、录制、`PlayerUiFacade`设置日志里的输出尺寸改用新计划（现有`srTarget`触点：bridge `Export.cpp`/`Recording.cpp`/`Chain.cpp`、`QmlPlayerBridge_p.h`、`SettingsWindow.cpp`），保证显示尺寸=实际文件尺寸。

### 3.6 持久化

- PresetStore v33、PresetLibrary v16（globals）、ChainSession v11各在行尾追加两项；只有非默认值才写新版本并换新后缀（如`.sr-stage`），2.1.1继续读自己的旧文件；读取超出版本/非法值拒绝并保留原文件。
- 实时重配：重建条件加入两项（吸取2026-09-09“后端切换未触发重建”的教训），以已应用revision+实际`[video-sr]`尺寸日志双重确认。

## 4. 实施步骤（每步可单独审查、构建、回归）

1. 设置/尺寸规划/链路编译/校验与归一化 + 单元测试。
2. 三种存储格式 + 往返、旧版本、非法值测试。
3. GPU：资源、先降、`SrResolve`/共享hlsli、VSR分支、日志 + GPU测试。
4. 引擎重配、bridge、QML两种编辑器、i18n、导出/录制尺寸显示 + 界面验收脚本。
5. 画质A/B、性能/显存实测，写报告、WORKLOG与FINAL回执。

分支：自main `7b0b64b`新建`claude/sr-stage-20261028`，与未合并的`claude/capture-fps-audio-20261027`分开审查；需要一起出测试包时在本地合并，不push、不Release。产物进`build/claude-screen-recording-20261022`和`tests|logs|tmp|reports/claude-sr-stage-20261028`。单Agent、构建并发2/≤900秒、单测试≤300秒、私有data-dir/TEMP。

## 5. 验证

| 层次 | 内容 | 通过标准 |
|---|---|---|
| 尺寸单测 | 第1节全部例子；先降/再缩关闭时对全部片源×6个目标与现有计划相等；21:9/4:3/奇数尺寸；未生效判定；NR先行；导出 | 全部相等/符合 |
| 设置/存储 | DLSS/FSR下归一化；非法值；v33/v16/v11往返；默认值仍写旧版本 | 通过且原文件保留 |
| GPU正确性（扩展`SrQualityTests`） | 实际Create/Evaluate、纹理尺寸、非黑无NaN；先降/再缩与CPU面积参考对比；HDR代理路径；`HdrVideoSrRestore`改include前后输出一致 | 误差在FP16/8bit量化范围内；改include前后逐像素一致 |
| 默认回归 | 功能关闭时同帧输出与main构建逐像素一致（SDR、HDR、压缩视频原尺寸修复、采集源跳过） | 一致 |
| VSR能力 | 1080p→4K、1440p→4K、1440p→8K、1080p→8K、720p→4K、21:9各质量1–4 | 记录实际成功/失败；不支持的组合界面禁用并写原因，不偷换成普通缩放 |
| 实时切换 | 播放中先降关→1080p→关、再缩不缩→4K | 每次都有新applied与对应尺寸日志，失败回滚 |
| 画质A/B | 见下 | 用户目视判断为准，数值只作参考 |
| 性能/显存 | 4K60文件播放30秒：每组合`GpuStage::Sr`中位/P95、整帧GPU、显存峰值；含/不含实时NR | 记录；超出60fps预算的组合在提示里说明 |

**画质A/B**：
- 本机现有`tests/nr-manual-scale-20261024/media/nr-4k30.mp4`是`testsrc2`合成画面，只能验尺寸和正确性，**不能判断画质**。需要用户提供自然素材：一段“糊的4K”（老游戏/老电影）和一段原生2K；或经用户同意用采集卡录PS5画面。另用系统已装FFmpeg从清晰4K母版生成“4K→1080p→4K+压缩”的退化版，以母版作参考算PSNR/SSIM。
- 对照组（VSR质量4与2）：关闭；现有单步（4K原尺寸修复/2K→4K）；先降1440p→4K；先降1080p→4K；2K→8K→4K；先降1080p→8K→4K。
- 输出：同一暂停帧无损PNG（静态细节、文字/HUD、运动三类场景）+ 10秒片段看时域闪烁；并排100%/200%裁切交给用户看。PSNR可能下降而观感更清楚，数值不当结论。

## 6. 风险与未验证边界

- VSR是为压缩视频放大训练的；“先降再超”是否优于现有4K原尺寸修复取决于片源。若A/B看不出提升，如实报告，不硬上多层。
- 先降会丢掉片源里真实存在的高分辨率细节（原生4K HUD/字幕/UI），第一阶段无法避免，提示里写明；第二阶段用细节保留混合解决。
- 线性光面积平均与NR一致；若A/B出现细亮线变粗或亮度偏移，再评估编码域平均，不预先加开关。
- 8K输出与4x以上倍率能否运行、耗时多少均未知；5070上8K VSR可能超出60fps预算，只适合30fps或导出。
- 1080p→4K的VSR可能比现在4K→4K原尺寸修复更省（输入少4倍），待实测，不预先宣称。
- 采集卡实测需用户同意后才占用设备；未测前不声称采集场景可用。

## 7. 后续阶段（不在本次范围）

1. 细节保留混合：按局部高频能量把原片真实细节（HUD/文字）混回；自动识别有效分辨率并推荐“先降到”。
2. Descale：已知放大核与原生分辨率的片源（动画/部分老片）做逆放大，复用成熟开源实现前核对许可证并记入THIRD_PARTY_NOTICES。
3. DLSS SR/FSR三段式（运动矢量按`srInput`换算）。
4. 多层：`srLayers[]`（每层先降/超分到/再缩/后端/质量），第0层就是本次的平铺字段，SR目录`maxInstances`放开；每加一层先实测耗时与伪影累积再决定上限。
