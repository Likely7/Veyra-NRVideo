# ANR FG 本地完整接入（2026-11-12）

本次实现上游现有 ANR FG 的完整本地接入：2X、3X、4X、5X、6X，SDR/HDR，列表/节点编辑，预设与会话恢复，NR/SR 共存，热切换、暂停恢复、尺寸/历史重置、错误回退，以及产品导出执行器。不是仅添加菜单或 SDR 2X 演示。ANR SR/RR 尚无可用组件，不放占位选项。节点模式沿用 Veyra 现有预览能力；所有后端的节点图导出当前都明确拒绝，本轮没有伪称支持它。

源码分支 `codex/anr-fg-experiment-20261112`，起点 `6e3747a66534f46231755367b1bf5c3653702f81`，标签 `checkpoint/pre-anr-fg-20261112`。工作树 `E:/项目/Veyra/worktrees/anr-fg-experiment-20261112`；构建/日志/测试/报告/临时文件分别在 `E:/项目/Veyra/{build,logs,tests,reports,tmp}/codex-anr-fg-20261112`。Claude 的另一棵脏树和共享构建保持独立；未合 main、push、发布或关机。

## 上游与接口事实

固定研究提交 `20a5e7731d899eca3a289904929879588448664d`，官方 [FG-V.B15](https://github.com/apex-compute/ANR/releases/tag/FG-V.B15)。本地使用已核验的官方 addon，19 个原始文件记录在报告 `ANR-RUNTIME.json`；DLL SHA-256 为 `ae7424b87d13a0e3a9a99dd02d1ea35b9de96c2caa9cd4300d73bac5bb0cf4e8`，3706880 字节，NotSigned。它是 ANR 的运行库，不能冒称 NVIDIA 原版。

核心实现未公开，明确的核心集成/分发许可未找到；full 下载包中的 OptiScaler GPL 许可不能代替 ANR 核心许可。本轮按用户要求只做本地实验，不公开分发。上游提供的 5090 跑分不作为 Veyra 本机性能证明。参见 `THIRD_PARTY_NOTICES.md` 和独立研究报告 `E:/项目/Veyra/reports/codex-anr-research-20261112/FINDINGS.md`。

自主编写 `AnrProvider` 适配器，绝对路径加载独立 DLL，检查必要导出、初始化结果和实际倍率能力，复用现有 SDK 参数写入器与 Veyra 共享增强图；不复制无许可 ANR/OptiScaler 算法或安装器，不引入 Streamline。ANR 自身 Windows 二进制内部有 Vulkan 桥接和队列 hook，不能宣称它是完全不含 hook 的原生 D3D12 算法。

每张图独立参数和 Feature；同设备/路径的初始化模块共享持有，最后一个使用者才 shutdown。最后释放与新建在同一锁内串行，避免换设置时旧图销毁把新图关闭。NVIDIA NR/SR/FG 保留自己的模块和生命周期。

两个实测发现必须在消费者边界转换：

- 原生 DLSSG 使用归一化反向运动；ANR B15 使用 current→previous 像素运动。直接套用 DLSS 参数时 API 成功却没有正确中间位置。用精确平移真值比较五种参数后固定 ANR scale=(1,1)，不改共享光流。
- ANR B15 Vulkan 桥接对 depth/motion 的进入/退出状态为 COPY_DEST；共享图是 NON_PIXEL_SHADER_RESOURCE。首次调试层报27项错误，适配器现在包围调用转换并恢复，原生 DLSS 的资源契约不变。

B15 实际模型覆盖的工作尺寸为640×360至7680×4320。超范围在创建前明确失败；可按需要先启用超分达到工作尺寸。模型/运行库不兼容时预览关闭 ANR 并继续基础播放，界面同步回退为1X且提示原因；导出则明确失败，不偷偷降低用户所选倍率。

## 已完成的真实检查

本机 NVIDIA RTX5070 / 驱动32.0.16.2101。以下都是本机软件/GPU证据，不等于其他显卡、PS5动态游戏、物理刷新或屏幕光学延迟验收。无独立 Reviewer；本轮单Agent自审。

| 检查 | 证据与结果 |
| --- | --- |
| SDR 全倍率真实像素 | `PIXEL-MATRIX.json`：估算光流2X及精确光流2–6X，176个生成帧全部通过；不同于前后端点、彼此不同、不是简单混合、运动位置与subframe PTS正确 |
| 纵向/HDR运动 | `SECONDARY-DIAGNOSIS.json`：真实P010/PQ输入，横/纵向各11个中间位置正确；SDR纵向另11个 |
| D3D12严格状态 | `FINAL-PIXELS-DEBUG.json`及最终共存复测：2X/6X、估算光流、HDR皆0调试错误。日志中的frame-pool拒绝覆盖是有意保留租约的负例，且断言通过 |
| 共存/销毁 | `FINAL-COEXIST.json`：DLSS前台＋ANR、ANR前台＋DLSS、双ANR，销毁其中一图后另一图继续；实际像素和调试层均通过 |
| HDR全倍率＋NR＋SR | `HDR-FIRST.json`/`HDR-MATRIX.json`：P010原生HDR，20次NR和20次SR/例，合计270个生成子帧，约998.9nit亮部保留，JXR与GPU读回比较通过；最终6X再次90帧通过 |
| 设置落盘 | `CPU-FINAL.json`：ANR设置48、合同301、预设251、VFG设置683、时序230项通过。保存/恢复包括5X、列表和节点，不改变已有后端ID或首次默认值 |
| UI真实热切换 | `UI-LATEST.json`：ANR5X→暂停改6X→恢复→DLSS2X→ANR2X→节点5X、保存预设和退出重启通过；软件提交约300/360FPS不是面板刷新率 |
| UI真实失败恢复 | `UI-ROUTES.json`：320×180源触发尺寸拒绝，播放继续约60提交FPS，ANR活动false、倍率1、明确ANR提示 |
| 生命周期 | `LIFECYCLE.json`及最终复测：同进程6→2→5→3→4→6，尺寸360/368切换，重置与重建均通过 |
| 实时回放 | `LIVE-LATEST.json`：变帧率自动识别、每17次输入注入丢失、60→30、NR＋SR＋ANR，均无已观察新画面误丢/重复决策，内容PTS单调且不借未来时间；不能把人为/真实不等帧间隔都叫卡顿 |
| 60→30节奏 | 实测约59.95提交FPS，Present返回间隔P99=17.35ms，半速采样间隔P99=34ms；NR＋SR自动30→60约59.83提交FPS、P99=17.41ms，仍有一次33.45ms间隔，不宣称零停顿 |
| 完整60秒动态测试片 | `final-full60-anr.json`：3600个输入采样决策完整覆盖，0个已观察新画面误丢/重复、PTS合法，2084个生成候选；包含变帧率和静止/恢复段 |
| 产品导出/取消 | `EXPORT-MATRIX.json`和最终复测：60源帧分别输出120/300/360帧，保留边界hold计数；NR＋4K SR导出和取消无最终残留文件通过；不使用raw pipe |
| GUI导出子进程 | `UI-ROUTES.json`：ANR5X真正经过设置冻结/跨进程传递，输出300帧、236生成、0任务失败 |
| HDR导出 | 最终270个HDR源帧输出1350帧（ANR5X），自动HEVC Main10/PQ。独立解码计数/时间戳/像素验证见最终 `EXPORT-INDEPENDENT.json` |

构建命令：

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-anr-fg-20261112 --root E:/项目/Veyra/worktrees/anr-fg-experiment-20261112 --label 2.1.5-anr-test --jobs 2 veyra_qml_ui veyra_repair_fg_tests veyra_live_presentation_tests veyra_hdr_enhancement_tests veyra_vfg_export_probe veyra_fg_lifecycle_tests
py -3.11 -X utf8 -B -I scripts/i18n/extract.py --check
```

初次完整产品构建235.3秒；最终产品增量构建6.7秒，6目标通过。翻译2796条，zh-TW/en/ja无缺失或占位错误。旧 `delivery.ps1` 是legacy-only，不拿旧Win32脚本冒充当前QML验收；本轮使用真实QML接口探针和上述相关产品检查。可复现驱动脚本保存在任务报告目录，不含核心实现。

## 失败保留与边界

旧错误运动参数、调试层27项错误、非法SDR→HDR测试设置和低于最小尺寸的创建失败日志均保留；修正契约后重测，不删失败来制造通过。另有测试脚本漏传VFG单测目录、HDR夹具缺少out目录的调用失败，修正后通过，均不是产品崩溃。

文件、DirectShow采集、PS5/其他串流消费同一增强图和ANR适配器；不各写一套补帧。实际网络主机/AMD/Intel/其他NVIDIA型号未在本轮验证。原用户日志中的外部输入长断档原因、旧停顿栈并未因此得到证明，不能说换ANR就根治上游断流。ANR质量是否优于DLSS，需要真实素材对照；本轮精确运动/颜色正确性不等于所有游戏遮挡、HUD和快速运动质量都更好。

独立启动入口 `E:/项目/Veyra/tests/codex-anr-fg-20261112/启动ANR补帧测试.cmd` 使用本任务独立配置，不自动连接主机或抢采集设备；当前程序版本2.1.5-anr-test。运行组件和QML最终完整性、启动器实启退出、实卡短测边界及本地源码提交以外部 `FINAL.json` 与本页收尾记录为准。

## 收尾验证

T911实卡额外三组通过：4K60 NV12＋1080p NR＋ANR2X，普通输入约120提交FPS，暂停/恢复/停止后重开正常；自动识别和60→30另各一组。自动组751条输入决策显示同一picture，maxDiff=0，证明当前采集画面静态；这不算动态游戏画质通过。三组无ERROR、无日志丢失，见 `PHYSICAL.json` 与对应实卡 receipt。

独立FFmpeg/ffprobe解码六个输出（含GUI子进程、4K NR+SR和HDR5X）均通过：全部帧数正确、PTS严格递增，与源PTS分段插值的最大差≤0.667微秒，抽样画面非黑，HDR为HEVC Main10/BT.2020/PQ。首次验证错误要求“间隔必须绝对恒定”，但MKV源PTS本来按毫秒交替33/34ms；已改用独立源PTS真值比较，没有修改产品时间轴，失败回执保留。

最终97个既有运行二进制逐SHA与供体一致，ANR19原件逐SHA不变；ANR运行时自行生成的Vulkan loader/log另列，不冒充原始下载文件。104个QML逐文件与本工作树一致，探针恢复；Claude脏树的branch/head/diff SHA/status与开工记录完全一致。独立配置实启确认ANR2X并重启恢复，启动器真正启动子进程且正常退出。程序SHA-256 `ffee8327c84e86b70b37ac42fbe7f0c95a2911360458e1ce40e60a256223a752`，19587584字节。
