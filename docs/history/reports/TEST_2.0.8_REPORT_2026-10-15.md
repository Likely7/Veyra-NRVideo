# Veyra 2.0.8 本地测试交付验收（2026-10-15）

接手本机 Claude 会话 `15638198-4910-4628-aab4-c96c62aebd18` 的剩余工作。用户最后要求：修好 B1–B5、直播伴侣兼容和 #38，将 Claude / Codex 当前修复全部合入本地 main，打 2.0.8 测试包。此轮不向 GitHub 推送或发布。用户已经验过 PS5 手柄控制，2K 格式反馈暂缓；不重开用户已撤下的历史问题。

## 交付内容与源码

- 两个完整便携包：`Veyra-2.0.8-test-NVIDIA-win64-portable.zip`、`Veyra-2.0.8-test-AMD-win64-portable.zip`。全部解压到新目录运行，不只替换旧 EXE。
- 目录：`E:/项目/Veyra/test-packages/claude-release-2.0.8-20261015`。`DELIVERY.json` 绑定源码提交、两个 ZIP 的 SHA256 和相同 EXE 的 SHA256；`cold-start.json` 记录最终 ZIP 解压后的实际启动检查。
- 产品/测试构建提交：`99435ae371f28f871a78748b319aa653046a336d`。EXE SHA256：`a4676e9a5bf08738dcc85c6c6909045f0593ebadcf857e6108904e90d4df2432`，PE 2.0.8.0，显示 2.0.8-test。后续只有文档、交付脚本变化，679 个构建输入须与回执逐文件相同。
- 原 Claude 集成点 `f074e40` 保留；接手分支 `codex/test-2.0.8-handoff-20261015`。集成的来源包括 `claude/fg-gpu-home-20261014`（6a97dc1，含 latency-min）、`claude/field-20261015`（18fa6f3）、`codex/issues36-37-bmd-20261013`（345f87f）、`codex/update-check-20261013`（4d944de）、`codex/latency-chain-audit-20261013`（f91c771）。本地 main 从 `7571919` 快进，所有以上提交必须在祖先链中。
- 源码包另附；依赖源码与 patched FFmpeg/dav1d/zlib 源码沿用已核验归档。没有把运行库、SDK、模型、凭据、配置、私有视频放进应用源码。

## 完成的修复

| 项目 | 实际改动 / 边界 |
|---|---|
| B1 效果参数滑块卡顿 | NR、Video HDR、调色与保护参数发细粒度信号；普通参数编辑不再广播整条效果链重建。节点/列表消费对应信号，仍保存设置。结构性移动节点依然需要链更新。 |
| B2 捕获窗口连续缩放冻结 | 连续改尺寸时保留原帧池大小，稳定约 300ms 后重建一次；持续拖动期间画面可能裁切/留边，最终切换仍有一次初始化停顿，不宣称零成本。 |
| B3 日志无限增长 | 启动超过 16MB 轮换为 `.old.log`；运行超过 64MB 轮换，只留一份旧日志。 |
| B4 开关失去设置绑定（#29） | VSwitch 点击只发 toggled 请求，由实际设置写回 checked，加载预设/跨页修改仍跟随。 |
| B5 组件页重复扫描 | 组件清单缓存，在打开设置页或明确刷新时更新，避免随播放快照重复读取磁盘和重建列表。 |
| 抖音直播伴侣兼容 | 检测伴侣相关进程：启动时采用软件界面；运行中将用户请求的实时 GPU 优先级暂时限制为高，伴侣退出后恢复请求值。GPU 失败日志记录并行软件/注入模块。实际伴侣未安装，本机只验进程识别和控制流程。 |
| #38 GC550 开音频后循环重连 | 使用采集卡内嵌音频时，不再强制系统时钟；只有音频、始终没有视频时，后续重连尝试另一种时钟。没有 GC550 实卡，不能称现场已修复。 |
| 设置回滚与半帧统计 | Claude 的 1c2a1f8 在新指标窗口形成后才发布 applied revision，并清除旧 FG 计数，避免新设置与旧统计短暂错配。对应回滚和 60→30 测试通过。 |

同时合入已经完成的：RTX 50 DLSS 性能模式、双卡运算选项、全屏 Home 菜单、屏幕捕获游戏模式、PS5 采集卡画面 + 手柄控制、显示同步低排队及帧率提示、可变刷新开关、启动更新提示、竖屏旋转、节点视频区域遮挡修复、BMD 实际端口枚举/记忆、过多采集格式导致的卡死修复、GPU 重置图标恢复/一次自动重连/DRED、常用帧率菜单、原尺寸 RTX Video SR 限制到压缩视频。各项原始证据见随包对应报告，不把分支合入当作所有硬件验收。

保留生产界面的 33ms / 250ms 刷新策略；没有增加全窗口重绘、额外帧请求或生产逐帧 CPU 等待。本次接手没有修改生产引擎、模型、着色器、默认优先级及用户真实设置。

## 本机验证

机器：RTX 5070，Windows，当前 HDR 桌面。全部用私有 QA 程序副本/配置；没有连接配对的 PS5、Xbox、Moonlight 主机。

1. 构建 35 目标通过（已有 fresh CMake 目录上最终增量 21.9s，不能称全工程冷构建 21.9s）。命令：`chcp 65001` 后 `python -X utf8 -B -I scripts/acceptance/test-2.0.8-build.py --task claude-release-2.0.8-20261015 --label 2.0.8-test`。
2. `regress_cases.py file-continuity rollback half-rate telemetry` 全部通过。文件播放连续 100 源帧、音频等待计数不增加、媒体/墙钟误差 <0.15s 的原要求保留。采集半帧、设置回滚与 33ms 遥测检查通过。
3. 另 40 场：38 场无失败通过；VFG 首轮失败（见下一节）；屏幕捕获有 5 项 RGB 阈值失败且原 2.0.7 同样失败（见下一节）。通过项涵盖字幕/PGS、播放列表、效果链、设置、HDR、格式/DeckLink逻辑、更新检查、QML滑块/开关/导出页/菜单/翻译、显示同步、呈现与文件播放/无效效果/seek/endpoint。没有把包装器对屏幕捕获的 expectedExit=1 算成真实全绿。
4. 六效果器、无媒体的实际 GUI 脚本：节点页 NR 单步设置 4.95ms，专业页 5.05ms。Claude 同机原脚本旧结果约 65–80ms；这只说明 UI 参数响应，不能宣传成视频计算或整体 FPS 提升。移动节点仍为 61.35 / 64.45ms，未消除其结构性重建成本。
5. 实际窗口捕获 + NR + DLSS FG 连续拖动目标尺寸约 12s：日志只有初始图和稳定后新尺寸图两次初始化，生成帧持续呈现；尺寸稳定后约 0.3s 应用新尺寸，切换初始化仍约 0.5s。无崩溃或 GPU/QML 错误。
6. 直播伴侣进程名模拟（本机 Windows PING.EXE 私有副本）：启动 software 界面，请求实时=5 时实际限制为高=4，模拟进程退出后恢复实际=5。只验证检测/策略，没有真实伴侣 GPU 并行负载，不能称 TDR 根因已定位。
7. 真实应用日志轮换：测试专用阈值分别 300000 / 60000 字节；启动旧文件 404624 字节完整移入 old，运行中 60204 字节触发轮换，新文件 6990 字节；两场正常退出。生产 16MB / 64MB 阈值未改。
8. DLSS 性能模式和 VFG Low / 4X 实际播放通过：分别呈现 1233 / 4768 张生成帧，无 GPU/QML 错误；DLSS 日志确认 lite state=1，VFG 日志确认 native 初始化、实际 applied NVIDIA-VFG 和 4X。见 `logs/test-2.0.8-handoff-20261015/playback-smokes-165533/receipt-reviewed.json`。这是呈现提交计数，不是屏幕实际刷新或逐像素画质证明。

## 如实保留的失败

- Telemetry 原夹具在 120ms 动画中固定 wait50ms 后要求值严格居中，调度延迟可能已越过结束点。99435ae 明确验证 33ms 定时器、确定的数学中点、真实进展与收敛，同时保留高频更新次数上限；没有改生产频率。
- file-continuity 原夹具把 NGX 约 8s 冷启动卡在通用 8s 超时，并将首个音频释放混入稳态窗口。原 2.0.7 也有该超时证据；只给此用例启动阶段 30s、启动失败即返回。当前版本修正后通过；没有重编 2.0.7 的修正夹具，不宣称完整双版本性能比较。
- **VFG：40场批跑中首次 RGBA8 / Low 模型部分首子帧质心和切镜/混合档参考比较异常，共22项失败，没有 API/GPU 错误。独立重跑同一 EXE 后 2425 / 2425 通过；另用未改生产库、人工延迟200ms的 GPU 输入依赖诊断 189 / 189 通过。VFG 后端和对应测试相对原 main 没有变化。现有证据不能确定是 SDK、执行时序还是夹具问题，未定位根因，也未宣称已修复；没有靠降低阈值、加生产阻塞或改模型消除失败。** 这是测试交付限制，包的 releaseReady=false。
- 屏幕捕获 5 项严格 RGB 阈值失败：HDR 桌面红条跨通道读数达到 110/115，原阈值要求 <110。用原 2.0.7 测试、原头文件、原静态库重新链接到外部诊断 EXE，完整原着色器配齐后，得到完全相同 5 项失败；旧库文件哈希保持。不算本版新回归，但也不称屏幕颜色全项通过。最初基线探针漏着色器的失败保留，不用那次结果作为证据。
- 私有 QA 复制脚本曾误删 QtQuick.Controls/Layouts 导入，两个 GUI 启动失败；仅修外部 QA 副本生成脚本后重新验证，产品打包脚本始终保留 Qt 模块。所有失败原日志保留。
- 实际 VFG 播放检查最初错匹配不存在的 `vfg-gpu` 日志标签，导致后端 active=false；实际标签是 `[vfg]`。按初始化、applied 后端与 4X 三个真实标识重新解析同一份日志后通过，不重跑、不删除原失败回执。这与上面的合成像素校验22项失败是两件事，后者没有被解析器修复。
- 首次 stage 的签名检查受宿主 PowerShell 7 的 PSModulePath 影响：子进程 Windows PowerShell 5.1 自动加载了不兼容的 Security 模块。明确从子进程自己的 PSHOME 导入系统 Security 模块并启用失败即停后，原件实际签名为 Valid；没有跳过签名断言或改变运行库。首次自有半成品保留为 `stage-interrupted-NVIDIA-20261015` 后重新 stage。

## 运行组件与最终包核验

NVIDIA 包新增已获准的 `runtime/experimental/dlssg-lite/nvngx_dlssg.dll`：310.9.16.0、7506544 字节、SHA256 `CADEE1AA6DB3907BABDC3285DA2AEBC4553D92B89C38BA3EAE8ABD8BBA1BEA72`、Valid NVIDIA 签名；只用于 RTX 50 可选性能模式，默认关闭，AMD 包不含它。不修改运行库原件，不锁定用户兼容替换。

交付流程：`python -X utf8 -B -I scripts/acceptance/test-2.0.8-package.py stage` → `seal` → `cold --cold-run handoff-final`。原 NVIDIA / AMD donor 清单 1641 / 2081 文件逐一复核，PE 导入闭包、厂商分离、许可证、patched FFmpeg 与对应源码、源码禁止项、ZIP CRC/逐文件哈希均通过。cold 从最终 ZIP 解压，用只有 Windows 的 PATH 分别跑 D3D12 / software 界面，GTA 4K 原片每场 14s，四场实际渲染器、realPresented≥100、GPU/QML 无错误、独立配置 sentinel 均通过：NVIDIA 397 / 403，AMD 401 / 404 张原帧呈现提交。具体执行结果见外部 `DELIVERY.json`、`cold-start.json`、`reports/test-2.0.8-handoff-20261015/final.json`。

封包时 main / 源码提交为 `80a3d7cc3b66e9b49cd64f97c9552fe879098912`；源码 ZIP SHA256 `4ca39a70797c6080ef02646e1d2aec0a392346ea3594206c8809f188a64d3d99`。NVIDIA ZIP 718608815 字节 / SHA256 `e89972d234fc79167c08b2183b42347c9ace606663b9ac363e2d09a547afe0ba`；AMD ZIP 355816850 字节 / SHA256 `1740d7dbc6c8307d0dd8deefb9aaafac4398ed0e40022b1e40e66a86092ecda9`。封包后仅追加本报告与 WORKLOG 的实际结果，产品679输入及35EXE不变，最终 main 和封包提交差异由 final.json 明确记录。无需为文档更新重新编译或改变已验 ZIP。

AMD/Intel 增强、GC550/BMD 硬件、真实直播伴侣负载还需对应用户实测；AMD 包在本机启动成功也不等于 AMD NR/FG 实卡已验收。此次不关闭远端 issue，不发布正式 Release。

## 原始证据与保护

- 最终构建：`logs/claude-release-2.0.8-20261015/build-20261015-160951/receipt.json`（679输入/35EXE）。
- 定向4场、批跑40场、VFG独立重跑：同根 `tests-20261015-161215`、`tests-20261015-161814`、`tests-20261015-163019`。
- GUI：同根 `handoff-node-fixed-164020`、`handoff-pro-fixed-164032`、`handoff-resize-fx-164901`、`handoff-douyin-165329`、`log-rotation-165437`。
- 原版屏幕颜色基线与延迟输入 VFG：`logs/test-2.0.8-handoff-20261015/baseline-screen-164400`、`vfg-delayed-capture-164407`。
- 启动保护快照/源码 bundle：`archives/test-2.0.8-handoff-20261015`，覆盖77旧工作树、509脏文件及原Claude会话。只按授权更新当前源码树和main；其它工作树、原包、运行库原件与用户配置保持。
- 当前 QA 保留供 VFG 复查，恢复为未注入测试 QML 的原始 Main.qml。首次中断 stage 与最终解压验证副本暂保留：自动审批拒绝了递归删除命令（只返回 blocked by policy），没有执行删除或换其它方式绕过。验证副本3749文件、1947013415逻辑字节，路径 `verify/claude-release-2.0.8-20261015/handoff-final`；最终 ZIP、源码、可用构建和日志均保留。
