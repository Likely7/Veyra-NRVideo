# VFG 全档位接入验收（2026-10-03，本地候选）

## 已实现与实际验证

`codex/vfg-integration-20261003` 接入 NVIDIA Video Effects SDK 1.3.0 原生 VFG，
包含 2X/3X/4X/5X/6X/7X/8X、Low/Medium/High（默认 Medium）。列表与节点模式、
预设、重启会话、实际预览和进程隔离的视频导出全部接入。VFG 不借用 DLSS 生成结果。
保留此前五项现场修复和 AMD NR 0.39 资源；这些功能的实机边界仍见原验收报告。

产品代码使用 C++ 原生 API、D3D12/CUDA shared buffer/fence；NVENC 接收 GPU texture，
没有新增生产路径像素回读、逐 pass CPU fence wait、Python/Streamline/ReShade 依赖。
FrameBatch=8，生成池14，输出池16，present/encoder 描述符按池尺寸推导；
DLSS 兼容原十张全尺寸纹理仍保留。FG 位于增强之后、OSD/字幕之前，源 PTS/epoch
和租约保持真实；每个子帧独立保存，不把最后一张输出重复七次。

本机：Windows x64 / RTX 5070 / 驱动 **616.56**，不是反馈者的 616.92。
所有证据在 `E:/项目/Veyra/logs/vfg-integration-20261003`；媒体/画像/会话在同任务 tests。

| 检查 | 结果与证据 |
| --- | --- |
| 最终产品编译 | `build-product-v4.log`，MSVC/Ninja 51 步 exit0，含 QML、engine、native/导出探针和回归 |
| 原生 720p | `native-720-v2.log/json`，463 checks/0 failures，RGBA8、RGB10A2 各21种组合 |
| 原生 4K | `native-4k-v1.log/json`，463 checks/0 failures，同样全部倍率/质量；D3D12 debug errors=0 |
| 真输出与切镜 | 8X 每组七张不同哈希，移动图块中间位置有序；manual cut 与当前输入 byte error=0；无效 slot/9X 拒绝 |
| 设置与旧格式 | `unit-v4/summary.json`，VFG 331 checks、旧 PresetLibrary/RepairPreset/RepairContract/EffectChain、Qt Quick 39 项均 exit0 |
| 21 种真实导出 | `export-all-v2/summary.json`，720p30、2..8 × 三质量，NVENC HEVC；每8张源帧输出16..64，60..240 fps，视频时长/音轨保持 |
| 2K AVI + NR + 4K SR + 8X | `combinations-v4/2k30-avi-nr-highest-sr4k-vfg8.log`，2560×1440/30fps AVI，最高 SR 档、原生3840×2160 NR、4K VFG Medium8，24源 + 161生成 + 7尾 hold = 192输出，3840×2160/240fps/0.8s |
| 24/60fps、取消与失败 | `combinations-v4/summary.json`，24→192fps、60→480fps；取消不提升最终文件；缺失 SDK 导出明确失败，未冒充成功 |
| 实际 GUI | `ui-v4/summary.json`，全部倍率热切换、8X 三质量、暂停/seek/resize/resume、节点/列表独立质量、两预设保存与重启通过 |
| worker 冻结 | 同 GUI 测试：启动8X Medium后父进程改2X Low，worker仍输出64帧/240fps；不读运行中的新设置 |
| 缺失运行库预览 | `ui-v4/missing.log`，fgActive=false、fgEnabled=false、multiplier=1，9.1667s进度继续，警告明确点名 VFG |
| 便携 staging 实跑 | `ui-package-stage-v3/summary.json`，PATH仅Windows、无VFG环境变量；全倍率/质量、真正DLSS6↔VFG8切换、保存恢复、缺失组件回退通过；SDK从 `runtime/nvidia-vfg` 加载 |
| 非默认质量 worker 冻结 | `ui-package-stage-v3/export-worker-29212.log`，父进程启动8X High后改2X Low，worker SDK `multiplier=8 quality=2`，输出64帧；日志 `vfgQuality=2`，不依靠默认Medium制造通过 |

21种导出的首尾填充是现有 CFR 策略：N张源、(N−1)(M−1)张生成、M−1张尾 hold。
日志分别计数；尾 hold 不计作算法生成帧。输出倍率不等于每张视频图像都应互不相同。

## 性能与使用边界

原生测试的 `groupWallMs` 包含 CPU 提交、GPU copy、最终诊断同步/读回准备，
不能称纯模型时间或屏幕延迟。720p Medium8 约22.0–23.3ms/组，High8约105.8–107.2ms；
4K Medium8约37.0–38.5ms，High8约123.8–131.0ms。测试是合成移动图块短测。

真实 GUI 的短窗口提交读数（ui-v4）：2/3/4X Medium=60/90/120次每秒；
8X Low约241、Medium约118、High约60且明确显示“调度降档”。窗口短读数有抖动，
不是物理屏幕 FPS、稳定性能保证或端到端延迟。高档8X离线可完整生成；实时播放仍执行
现有一倍速音频/PTS预算，允许跳过预览增强机会或降低生成工作，日志不会掩盖。

Windows 官方硬件范围为 Ada/Blackwell（RTX40/50）；本轮只验证5070。6X–8X 明示实验。
3X/5X/7X 来自公共倍率 API，实际逐项验证过，不称官方游戏生产档。
RGBA8/RGB10A2 bridge 支持不等于 HDR 色度专项验收；scRGB half-float 不在 VFG
原生编码范围，初始化拒绝时预览关闭该效果并提示。影片画质 AB、RTX40、616.92、
采集卡/主机 VFG、长稳、VRR/物理显示节奏与屏幕/手柄端到端延迟均未实测。

## 保存、回退与对抗自查

稳定 backend ID4、独立 vfgQuality 0..2；旧 ID/默认和后端倍率上限保留。PresetStore28、
PresetLibrary7、ChainSession5、worker ABI8，旧数据默认 Medium；新 library/session
采用 `.vfg` 旁文件，旧数据保留。只应用颜色的预设不覆盖补帧质量。任务冻结包括 VFG 质量。

缺失 SDK 曾暴露实际 bug：engine multiplier 已降到1，UI仍enabled/8X。`ui-missing-diagnostic`
记录 active=false/enabled=true/position=9.2；已发布拒绝 revision，并在 facade 按最新请求
同步后端/倍率/质量，旧 rejection 不覆盖更晚选择；ui-v4 验证 UI 与 engine 一致。
VFG 不使用外部 motion，即使从显式 OpticalFlow 的 DLSS 预设切换也不额外启动无用光流；
NR/SR 的外部 guidance 仍保留。不可访问 runtime 路径以 error_code 判断，组件页不会抛 filesystem 异常。

自查覆盖：最高 subframe/pool/SRV offset、GPU handoff/故障后 signal、借用 CUDA memory
所有权、销毁顺序、非重复输出、PTS/尾 hold、旧 schema、较新 UI 请求与失败回退。
这是本 Agent 自查与真实自动测试，不宣称独立 Reviewer 或未发生的硬件验收。

早期失败保留：native-v1 的 void ABI/packed enum、product-v1 的 private 测试 API、
unit-v1 的缺失 argv、unit-v2 的旧非法枚举值、export-v1 的过严微秒 fps断言和错误日志路径。
GUI ui-v1 与 Qt Quick 并行时出现暂停；GPU正常排空，无卡死证据。焦点/按键干扰是推断，
串行 ui-v2/v4 正常。不能把上述首次失败记录当最终通过。

便携 staging 首次缺失 SDK：包实际以 `runtime/experimental` 为现有根目录，最初放在
`runtime/nvidia/vfg` 无法找到；已改放自动发现的 sibling `runtime/nvidia-vfg`。这次独立
运行测试发现的是实际打包错误，不以开发环境变量测试代替最终包测试。旧DLSS热切换
测试曾只检查pending设置；已改为等真正“DLSS 6X”运行标签和出帧再返回VFG8。

## 来源与本地交付状态

NVIDIA 官方 wheel0.2.0.0/SDK1.3.0，435809365 bytes，SHA256
`5aaf6a42bc6b6dbbf52fcb714194c994a6893cbbf7ada38bc2165a1f83e4a6fc`。
MIT样例固定 `52011f89c1741d06b40ea312af1f20be8be9ec62`，改造/来源见 THIRD_PARTY_NOTICES。
闭源 runtime 与样例许可证不同；十四个必需 DLL 原样保存在本地候选 `runtime/nvidia-vfg`，
独立 `vfg-runtime-manifest.json`/许可证，不进入源码 Git，不增加用户运行库哈希锁。

产品代码存档 `0d891731ba6556c47563a090c5746bac803cabe4` /
`checkpoint/vfg-code-20261003`；之后仅修复组包/验收脚本与文档，产品源文件未变。
VFG十四 DLL 已逐项核验原件 SHA256/版本/Valid NVIDIA 签名，原43运行组件与patched
FFmpeg沿用既有身份。两者均是组包审计，不是用户替换运行库的加载锁。

本地候选目录 `E:/项目/Veyra/test-packages/vfg-integration-20261003/`
`Veyra-2.0.2-vfg-20261003-win64-portable` 的完整GUI与worker实跑已通过。
此件记录组包前验收；最终ZIP额外做完整文件哈希核验、独立解压实跑和双渲染器六页检查，
结果放在同目录交付报告与 `.sha256` 文件，源码仓库的本文件随后更新。未 merge main、push 或公开发布。
