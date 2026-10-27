# Veyra 1.1.0

## 中文

下载 **Veyra-1.1.0-win64-portable.zip**，完整解压后运行 **Veyra.exe**，无需安装 SDK。另附的源码包供开发者使用。

- **支持 NVIDIA App Smooth Motion（AI 插帧）使用方式**：专业模式的补帧倍率下方新增可展开教程，使用现有界面主题。由 NVIDIA 驱动完成补帧，不额外下载或打包驱动文件。
- **自由组合补帧**：只用 Smooth Motion 时，软件选择“关闭补帧”；也允许与 DLSS / XeSS 同时开启，不强制互斥。叠加效果尚未验证，不保证更好。
- **扩展采集卡格式**：原生输入支持 RGB24、RGB32、ARGB32、RGB555、RGB565、YUY2、UYVY、YVYU、NV12、NV21、I420、IYUV、YV12、P010、P016。修复 P010 / RGB 等格式显示为 GUID 的问题，补齐步长、行对齐、UV 顺序和高位深转换处理。
- 保留 PS5 串流及重连恢复、采集音频缓冲修复、NR / 超分 / 内部补帧、截图与导出功能。

### Smooth Motion 开启方法

1. 软件内将补帧倍率设为 **关闭补帧**；NR、超分可继续使用。
2. **NVIDIA App → 图形 → 添加/选择当前 Veyra.exe → AI 插帧 → 开**，应用后重启播放器。
3. 只想用 DLSS / XeSS 时，到 NVIDIA App 关闭 AI 插帧并重启，然后选择软件补帧。也可以自行尝试两者叠加。

软件的“关闭补帧”和总增强开关不会关闭驱动功能。软件 FPS、耗时及队列不含驱动生成部分；截图和导出不含驱动中间帧，音画同步与直播捕获需实测。用户已反馈本机 Smooth Motion 有效稳定，其他显卡/驱动及叠加组合未逐一验证。升级到新路径或从旧实验 EXE 切换时，请确认 NVIDIA App 选中了当前程序。

采集格式转换已通过布局与合成颜色检查，反馈者的具体采集设备尚未实卡验收；P010/P016 支持不等于承诺采集卡原生 HDR 全链路。

退出旧版再升级；PS5 配对保留在当前 Windows 用户目录。七个增强运行文件沿用上版，社区 NR 明确标记为修改版，允许用户替换 DLL。保留 patched FFmpeg 与对应源码、构建记录和许可证；实验功能边界不因版本号变化而取消。

## English

Download **Veyra-1.1.0-win64-portable.zip**, extract it completely and run **Veyra.exe**. No SDK installation is needed. The additional source archives are for developers.

- **NVIDIA App Smooth Motion usage support** with an expandable setup guide below the professional frame-generation multiplier. Generation runs in the installed NVIDIA driver; no driver files are bundled or downloaded.
- **Optional stacking**: turn internal generation off to use Smooth Motion alone, or try it alongside DLSS / XeSS without an application-level block. Stacked quality and performance have not been validated.
- **Expanded capture formats**: RGB24, RGB32, ARGB32, RGB555, RGB565, YUY2, UYVY, YVYU, NV12, NV21, I420, IYUV, YV12, P010 and P016. Fixed GUID labels for P010 / RGB formats and added handling for stride, row alignment, UV order and high-bit-depth conversion.
- Retains PS5 streaming/reconnection recovery, capture-audio buffering repairs, enhancement, internal generation, screenshots and export.

### Enable Smooth Motion

1. Set Veyra's internal frame-generation multiplier to **Off**. NR and super resolution can remain enabled.
2. In **NVIDIA App → Graphics**, add/select the current **Veyra.exe**, enable **Smooth Motion**, apply and restart the player.
3. For internal DLSS / XeSS alone, disable Smooth Motion in NVIDIA App and restart. Stacking both is also permitted for experimentation.

Veyra's generation and master-enhancement switches do not disable driver generation. Software FPS, timings and queues exclude driver-generated work; screenshots and exports do not contain driver-generated intermediate frames. A/V timing and recording capture need separate verification. The user reports effective, stable operation on this machine; other hardware/drivers and stacked combinations have not all been tested. Check the program entry after changing executable paths or upgrading from the separate experiment.

Capture conversion passed layout and synthetic color checks; the reporting user's physical capture device remains unverified. P010/P016 support is not a promise of a full native-HDR capture pipeline.

Close the old version before upgrading. PS5 pairing remains in the Windows user profile. The same seven enhancement runtimes, clearly marked modified community NR, user-replaceable DLLs, patched FFmpeg, matching source and licenses are retained. Existing experimental feature boundaries remain.
