# Veyra 2.1.9 ·《迎接GTA6降临》

本次汇总 **公开版2.1.6之后** 的全部功能、修复和兼容调整。2.1.7/2.1.8未单独发布，直接升级至2.1.9。

### 💜 Community Support / 社区支持

**English**

A huge thank you to the Pump.fun community for supporting Veyra!

I've received creator fees from a community-created Veyra token. These contributions will help support the continued development of Veyra.

**中文**

非常感谢 Pump.fun 社区对 Veyra 的支持！

我已经从社区自行创建的 Veyra 代币中领取了创作者手续费。这些收入将帮助支持 Veyra 的持续开发。

**Token Contract Address (CA) / 代币合约地址：**

`i6Aop7z5qLaY47kseaoUt6kGq4Pi2ySg8j7cxRkpump`

**Disclaimer / 免责声明：** This token was independently created by the community and is not an official Veyra release or investment recommendation. Veyra remains free and open-source.

该代币由社区独立创建，并非 Veyra 官方发行的代币，也不构成投资建议。Veyra 仍然是免费开源软件。

---

## 日落界面、启动动画与音乐

- 新增默认的**日落配色**、新版霓虹手柄Logo与Veyra NR字标、分层启动动画；设置→外观可切回经典配色，也可单独关闭启动动画。经典配色保留原有取值。
- 启动音乐**每次打开软件播放一遍**，片头结束或跳过后继续，不循环；经典配色、关闭动画或减少动态效果也可播放。
- 打开视频、图片、采集或串流等媒体时，发起打开就停止音乐，打开失败也不重新响起。角落按钮只停止本次，重开软件仍会播放；设置里的“启动音乐”开关才是保存的长期选择。
- 修复角落停止按钮误写永久关闭，导致第二次启动没有音乐的问题。动画与音乐设置、四语说明同步。
- 音乐为用户提供并确认可随运行包分发的文件，放在`media/startup-music.mp3`，**不进入源码Git或源码ZIP**，也不纳入Veyra源码的GPL许可。

## 全效果剔除：区域终于看得见、拖得动

- 新增**“显示区域红框”**开关；绘制结束后可以持续显示已有区域。
- 在区域列表点“选中”，或直接在画面里点选后拖动位置；支持矩形和椭圆，移动保持大小并限制在画面内。选中区域有更粗边框与角标。
- 窗口/全屏、列表/节点/极简共用坐标变换，跟随缩放、平移、旋转和画面黑边。拖动先预览，松手后一次提交；Esc取消移动，切源或切换处理链会取消旧编辑。
- 隐藏红框不会删除区域或关闭剔除效果。红框是编辑显示层，**不写入截图、录制或导出**。

## 修复浏览器窗口捕获全屏后卡死

- 修复XeSS在窗口尺寸变化、交换链重建后累计呈现计数归零，却继续减旧计数引发无符号下溢、统计循环失控的问题。
- 提供方计数按会话/回退识别重置，统计窗口按批次计数，不再随外部计数逐次循环；保留实际生成/提交统计语义。
- 根据用户转储定位；本机真实WGC动态窗口＋XeSS2X/4X分别完成六次尺寸变化与正常停止。另一份日志中的NVIDIA冷启动JIT等待不属于这个死循环，未把它宣传为同时根治。

## 采集卡声音：监听和录制分开

- 将**音频输入**和**本机音频监听**拆开。关闭监听后本机不播放采集声音，录制、即时回放仍可取得游戏音频；麦克风选项保持独立。
- 原“不监听音频”实际关闭输入，现准确改名“不采集音频”。要静音录制，请先选采集卡音频输入，再关闭“本机音频监听”，不要选“不采集音频”。
- 采集对话框、专业音频面板和影院音频设置共用开关；首次打开、实时切换、恢复与重连均应用保存值。

## 捕获、G-SYNC和双显卡

- 合入[PR #104](https://github.com/Likely7/Veyra-NRVideo/pull/104)：屏幕捕获可列出并选择**其他Windows虚拟桌面**上的窗口，当前桌面的窗口优先显示。
- 合入[PR #108](https://github.com/Likely7/Veyra-NRVideo/pull/108)：新增Veyra自己的**NVIDIA G-SYNC配置检查、修复和恢复**。仅点击相应按钮时修改应用驱动配置，写入前先备份。
- 增加捕获目标显示器所属显卡、Veyra处理显卡及跨卡路径识别。跨卡时显示两张卡的名称、复制代价提示，并记录日志。
- 指定副卡：设置→性能→“GPU占用监控”选择显卡→开启“同时用这块显卡运算”→重新打开媒体。增强、补帧和导出一起使用所选显卡，暂不支持将NR和补帧分别分配两卡。
- **本次双卡相关新增是路径识别与提示，跨卡掉帧未宣称根治。** 既有RX7000＋核显设备选择修复继续保留并自动生效。

## AMD补帧

- 新增**AMD FRC · 视频补帧 · 2X**，通过AMD驱动自带AMF FRC运行库，在GPU上处理前后帧，不依赖游戏引擎运动矢量。仅AMD显卡且对应驱动运行库可用时提供选项；AMF SDK和驱动DLL不随包分发。
- RX9000新配置优先使用已有的**FSR4 ML 2X**；明确保存的FSR3.1选择保留。ML提供方不可用、创建失败或预览运行失败时，改用FSR3.1并保持2X，界面提示实际后端。
- 导出仍严格使用所选后端，不静默替换。FSR4 ML本身仍缺RX9000实卡验证；AMF本机核显结果不能扩展成所有AMD独显或所有画质场景通过。

## 验证数据与已有功能

| 检查与条件 | 本机结果 |
|---|---|
| QtQuick组件回归 | 209通过、0失败、0跳过；音乐后续修复另做实际程序重启验证 |
| 同一配置连续启动、临时停止/长期关闭/重新开启 | 8次实际进程全部通过 |
| 原BGM完整播放 | 约115.8秒自然结束一次，无循环；音频位置跨过约3.26秒片头 |
| 区域编辑 | 四语实际程序；窗口/全屏各真实鼠标拖动一次，区域大小保持；截图和14个抽样录制帧检查不带编辑红边 |
| WGC＋XeSS2X/4X动态尺寸切换 | 每组6次，共12次；计数重置与停止回收正常 |
| AMF：本机AMD核显，640×360合成移动条 | 48输入帧，历史重置后45张生成帧；45/45中间位置检查通过，最大误差0.5像素，D3D12调试层0错误 |
| 采集音频零增益 | 本机输出静音；录制/回放消费者分别取得52800/105600帧、非零声音、时间戳单调 |

这些数据来自对应模块与阶段，不相加当独立用例总数，不代表游戏FPS提升或物理屏幕端到端延迟。[详细条件和限制](RELEASE_VALIDATION_2.1.9.md)。用户已确认本轮本地测试“没毛病”；其他硬件仍按已测范围说明。

**自动帧率/去重、60→30、NV12回调优化、即时回放/F9、麦克风混音/双轨、GPU优先级默认高和VFG自动调控默认关闭，2.1.6已包含，本次保留，不重复算新增。** [上版完整更新与性能数据](RELEASE_NOTES_2.1.6.md)。ANR自写适配保留，公开包继续不带上游禁止Beta再分发的DLL/模型/程序包。

## 下载与升级

- 按Veyra实际使用的显卡选择NVIDIA或AMD完整便携包，完整解压到新的可写目录，运行`veyra_qml_ui.exe`；只需一个运行包，源码包用于重编译。
- 原有明确保存的设置继续保留。启动音乐：角落按钮只停止本次；要一直关闭，请到设置→外观关闭启动音乐。
- 最新交流群换为**13群**，原图标注于**2026-11-23前有效**；赞助、Discord、作者主页和英中教程保留，英文视频从零开始播放。

## English

**Veyra 2.1.9 · Welcoming GTA6** collects changes since the public 2.1.6 release. Versions 2.1.7 and 2.1.8 were not published separately.

- **Dusk appearance:** new default sunset palette, logo/wordmark and layered intro. Classic colors remain selectable; intro animation has its own switch.
- **One-shot startup music:** plays once on every launch, continuing after the intro or when the intro is skipped/disabled. Opening media immediately stops it, even if opening fails. The corner button stops only this launch; the saved Appearance setting controls future launches. Fixed the corner button accidentally disabling music permanently. The user-authorized music is included only in runnable packages, excluded from Git/source archives and the application source license.
- **Editable exclusion regions:** persistent red-outline toggle, list/picture selection and dragging, size-preserving bounds, shared transforms across window/fullscreen/list/node/cinema views. Dragging previews first and commits on release; hiding outlines preserves the mask. Editor outlines are excluded from screenshots, recordings and exports.
- **XeSS capture hang:** fixed unsigned counter underflow following resize/swapchain recreation and bounded rate-statistics work. User dumps established the affected loop; real WGC resize tests passed at 2X/4X. Separate NVIDIA cold JIT waits are not claimed fixed.
- **Capture audio:** select audio input independently of local monitoring. Muting monitoring keeps game audio available to recording/replay; “No audio input” truly disables capture. Live toggles, reopen and recovery use the saved monitoring preference.
- **Virtual desktops / G-SYNC:** merged PRs #104 and #108 for windows on other Windows virtual desktops and explicit check/repair/restore of Veyra's NVIDIA G-SYNC application profile, with backup before writes.
- **Multi-GPU diagnostics:** identify the display and processing GPUs and warn about cross-adapter copies. Choose a GPU under Settings → Performance → GPU usage monitoring, enable “Also use this GPU for processing”, then reopen media. Enhancement, FG and export use that GPU together; split NR/FG devices and a complete cross-GPU stutter fix are not implemented. Existing RX7000/iGPU device-selection repairs remain automatic.
- **AMD:** added driver-backed AMF FRC 2X. New RX9000 configurations prefer FSR4 ML; unavailable/failed ML falls back to FSR3.1 2X in preview with an explicit notice. Saved FSR3.1 choices remain; export does not silently substitute backends. RX9000 ML and discrete AMD hardware coverage remain limited.
- **Evidence:** component tests 209/0/0; eight same-profile launch cycles; original audio ends once around 115.8 s; 12 WGC resize transitions; actual region dragging and screenshot/record checks; AMD iGPU synthetic FRC midpoint checks 45/45, maximum 0.5 px error. These are module/stage checks, not a general game-FPS or optical-latency claim.

Choose one complete NVIDIA/AMD portable package for the GPU used by Veyra and extract into a new writable folder. All 88 shaders, notices/manifests and runtime identities are checked. Source assets and fixed dependency-source links are provided separately. ANR beta binaries remain excluded. Previous 2.1.6 dedup/60→30/NV12, replay/microphone and default-setting changes are preserved, not counted again as new work. Community QR updated to group 13, valid before 2026-11-23.
