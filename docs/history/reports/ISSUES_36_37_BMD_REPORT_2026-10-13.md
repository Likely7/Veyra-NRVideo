# Issues 36/37 与 BMD 日志53修复结果

本轮在 `codex/issues36-37-bmd-20261013` 完成两项可复现的软件修复，以及 BMD 输入配置能力补齐；构建、本机窗口/旋转和控制逻辑回归通过。**没有受影响 BMD 实卡的恢复证据，不能宣称现场黑屏已根治。** 用户交给 Claude 的延迟工作、同步/队列/XeLL 策略未修改。没有合并 main、push、发布或制作交付包。

## #37：节点模式的视频遮挡操作区

[Issue 37](https://github.com/Likely7/Veyra-NRVideo/issues/37) 的正文/评论已读取存档；附件链接未能取得，未将其冒充已查看。直接使用原 2.0.7-r2 程序和 QML 重现：9 场完成，8 场视频 HWND 比实际视频区高出 24–77 DIP，全屏一场正常。

原因是 Main 固定减去 40 高度，而节点统计/状态栏会因窄窗口和状态文字增长至 64–117。实际视频子窗口因此盖到下方操作区。NodePage 现在提供与真实布局一致的 `videoViewport`，Main 使用同一结果；分隔条过高时也为下方操作区保留空间。

没有新增生产 Timer、改变现有遥测/UI 刷新节奏或强制全窗口重绘。原 EXE 仅替换修复 QML 已 9/9 通过；最终 EXE 再测 D3D12/软件 UI × 100%/150% 缩放，每组 9 场，共 **36/36**。覆盖宽/窄窗口、英语/日语、拖动视频高度、缩小窗口、全屏及返回；QML 视频区与实际 HWND 边界一致，遮挡为 0。专业模式另外两种 UI 各 4 场，**8/8**。

## #36：带 Display Matrix 的竖屏视频

[Issue 36](https://github.com/Likely7/Veyra-NRVideo/issues/36) 中的方向元数据此前已读取，但最终呈现没有应用到像素。修复将容器顺时针方向接入 PreviewView、最终 blit、原图对照及 XeSS 的显示运动映射。图内解码、NR、超分、光流和保护区域仍使用原编码坐标；鼠标选区通过反向映射落回源坐标。UI 重置视图保留源方向。

真实播放还发现一个遗漏：没有有效 SAR 的文件，窗口比例仍回退到原编码宽高，导致像素转正后竖屏画面挤在横屏窗口中央。QML 的源比例和原生像素比例现已使用同一个方向映射；已知 DAR 已含方向，不重复倒置。

验证使用 1920×1080、30fps、四灰阶象限 H.264 视频，只改变 Display Matrix，FFprobe 单独记录元数据与 SHA。旧版 90/180/270 三种方向均未旋转。修复版分别在无 SAR、SAR=1:1 两组文件中，连续播放 **0→90→180→270→0，共 10/10**：90/270 的预览比例 0.5625、原生视频窗口 720×1280，0/180 为 1.7778、720×405；实际窗口像素的象限顺序全部正确。

GPU 呈现测试 **18/18**，包括旋转、原图对照、拉伸、原生尺寸、缩放/平移、保留交换链和无效参考回退；显示运动测试 **8/8**，包括三个旋转方向及竖屏 contain。相同新测试程序配旧着色器会在 rotate90/运动 case4 失败，证明回归能识别旧缺陷。HDR/DeckLink 10 位 GPU 颜色回归另行通过。

这些测试证明呈现与运动映射正确，不等于已验证真实 NR/补帧的竖屏画质。旋转合并进现有最终采样，没有增加独立旋转 pass、生产像素回读或逐帧 CPU 等待。**离线导出/图输出截图的方向处理没有在本轮改变或验收。**

## BMD：软件输入配置差异与修复

原日志53固定在 `archives/issues36-37-bmd-20261013/log53.log`，原输入 SHA 保持。按 `decklink-negotiate` 重核，共 **5 次**成功进入原生协商（开工 WORKLOG 初记 6 次，已纠正），均为手动 4K25/23.976/30。EnableVideoInput、StartStreams 成功，但状态 `locked=0`、检测模式 unknown、回调 `noInput=true`；全部连接读数为 `0x1`。这说明原生采集没有取得有效输入帧，不能先归咎于后面的 NR 或显示链路。

本地 SDK 枚举明确定义 `0x1=SDI`、`0x2=HDMI`。OBS 的固定源码 [e7f0b0d / decklink-device-instance.cpp](https://github.com/obsproject/obs-studio/blob/e7f0b0d43c538f5d108aa4cbea473d57b3012a1a/plugins/decklink/decklink-device-instance.cpp) 显式设置所选 `bmdDeckLinkConfigVideoInputConnection`；旧 Veyra 只读驱动当前设置，用户没有明确选择入口。这是可确认的软件能力差异。**用户实际物理口和 OBS 具体来源设置仍未知，不能由日志断言用户接线错误，也不能断言输入口是唯一原因。** 驱动支持自动检测也不代表日志中的手动制式已经开启自动检测。

现在原生设备列表保留旧 Driver 身份，并按设备报告的支持位列出独立 HDMI、SDI 等输入，不按名称猜卡、不盲扫端口：

- 显式输入具有稳定设备 ID 后缀，沿用采集偏好保存；旧路径继续跟随驱动。真实 CapturePreferenceStore 的 HDMI/SDI 与 `decklink:auto` 往返保存已测。
- 按所选连接查询 SDK 的模式支持，过滤不可用模式；不将不支持的组合默默换成其它格式。
- 启用前 SetInt 并读回校验，记录 requested/actual/supported/verified；SetInt 成功但驱动忽略、读回失败等情况均不能算选择成功。
- 关闭时仅在驱动仍保持本程序设置时恢复原输入；外部程序已经改口则保留外部更改。不写持久驱动偏好、不改 EDID。
- 无有效信号的错误包含所选口，提示核对软件输入选择或用自动检测制式。COM 接口存在且成功不再被当作已经采集出画。

端口控制只发生在配置/启停阶段，没有加入逐帧切换、等待或新采集缓冲。设备/模式枚举的实卡耗时和实卡输入延迟本轮未测。BMD 单元/状态机测试 **43 项通过**，其中 16 项覆盖新端口路径、失败读回、重试保留原口、恢复和外部更改；真实 BMD 设备数为 0。普通采集设备枚举仅有 WebcastMate/OBS 虚拟摄像头，本轮没有把它们替代 USB 实卡验收。

受影响用户后续在原生模式选择与 OBS 相同的 HDMI/SDI 输入，并先选“自动检测输入制式”复测；不确定实际制式时避免继续手动猜 4K25/30。验收应同时检查有效帧、锁定/检测模式、真实画面和音频，不能只看启动成功。原 Driver 选项为兼容旧配置保留，仍可能跟随原来无信号的默认输入。

## 构建、证据与失败记录

全部产物在 `E:/项目/Veyra/`：本任务的 build/tests/logs/tmp/reports/archives，以及窗口回归的 `issues36-37-bmd-20261013-ui`、`issues36-37-bmd-20261013-pro` 子任务。使用现有外部 SDK 与已打补丁 FFmpeg；源码 Git 未加入 SDK、运行库、模型或测试媒体。

实际入口：

```text
python -X utf8 -B -I scripts/acceptance/test-2.0.7-build.py --task issues36-37-bmd-20261013 --label 2.0.7-issues-fix --extra-target veyra_preview_geometry_tests --extra-target veyra_experimental_backend_tests
python -X utf8 -B -I scripts/acceptance/pro-video-geometry-test.py after --page node --task issues36-37-bmd-20261013-ui --renderer d3d12 --scale 1 --donor-app <本轮product> --media <GTA VI 4K>
python -X utf8 -B -I scripts/acceptance/rotation-video-test.py after --task issues36-37-bmd-20261013 --stage-label d64d7af --donor-app <本轮product> --media-dir <四象限视频目录>
python -X utf8 -B -I E:/项目/Veyra/tmp/issues36-37-bmd-20261013/run-tests.py cpu-final
python -X utf8 -B -I E:/项目/Veyra/tmp/issues36-37-bmd-20261013/run-tests.py gpu
```

| 证据 | 最终结果/位置（logs 下的相应任务） |
| --- | --- |
| 首次完整构建 | 552 步、29 目标，126.5s，`build-20261013-224232/receipt.json` |
| 最终产品构建 | d64d7af、670 输入、29 目标，29.0s，`build-20261013-231312/receipt.json` |
| CPU 11 组 | `cpu-final-232222/receipt.json`，全部通过，绑定最终测试 EXE |
| GPU 与旧着色器负对照 | `gpu-232224/receipt.json`；18 呈现、8 运动及颜色回归通过；两项旧 shader 按预期失败 |
| 旧版实际旋转负对照 | `rotation-before-231023/receipt.json`，三种非零方向失败 |
| 修复后真实旋转 | `rotation-after-231453`、`rotation-after-232040`，各 5 项通过，PNG 实际检查 |
| 节点 36 场 | ui 任务的最终 D3D12/software × 1/1.5 回执；均通过 |
| 专业模式 8 场 | pro 任务 D3D12/software 回执；均通过 |
| 设备枚举 | `usb-232352/receipt.json`，仅两台虚拟摄像头；没有执行 USB/BMD 实卡率测试 |

失败未掩盖：首个 160×96 无损 H.264 样本为 High 4:4:4 Predictive/profile244，虽然像素格式为 yuv420p，FFmpeg 软解正常、本机硬解花屏。`rotation-before-230431` 的失败截图/日志和原文件保留；它不算旋转测试证据，也没有修复该 profile 的硬解/回退。后续改用常规 H.264 四象限文件。`rotation-after-231059` 暴露无 SAR 的窗口比例遗漏，补上 QML 映射后重建并通过两类文件测试。

软件 UI 首次 9 场几何均通过，但验收器错误地寻找 `interface renderer=software`，实际产品日志为 `interface=software` 与 OBS software UI 提示，导致回执失败。修正为两条实际标识共同确认后重跑，两种缩放全部通过，保留原失败回执。另修正既有显示运动测试向新版 16 常量 shader 传旧 zoom 常量的测试缺陷；新增独立 present-motion 入口避免把 SDK 初始化混入纯映射测试。启动前发现用户播放器时守卫拒绝测试，用户确认关闭后才继续 GPU/窗口回归。

最终审计校验器曾把 catalog 根对象误当数组而失败；修正为读取 `entries` 后，全量确认原 2245 条目（含重复键）原序保留，仅新增 8 条。这个错误未改变翻译数据或产品，失败说明保存在外部 `audit-validation-failure.json`。

最终私有 QA EXE SHA256：`7ce5f602b526c921a854bfbd3edce50443a8f8756713010af4dc9bbc2cb75258`。私有测试 stage 不是交付包，不能使用其沿用的旧包 manifest 声称已重新封包。原 r2 的 **1641 文件**按原 manifest 全部哈希/大小复核；73 旧工作树的 HEAD/status 和原日志保护通过。最终提交、源码 bundle、构建和各回执绑定见 `reports/issues36-37-bmd-20261013/final.json`。本轮未承诺新的全硬件性能、端到端延迟、HDR/NR/补帧竖屏画质或 BMD 现场通过。
