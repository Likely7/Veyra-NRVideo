# 2.0.2 现场问题修复与 AMD NR 接入方案

用户 2026-10-03 请求五项工作：Xbox 无声音和运行后断开；接入 lmxxf 的 AMD RX 9000 NR；导出码率编辑回跳和设置无效；小飞机兼容模式背景透明；取消截图中的 NVIDIA App 插件检测。先存档、写方案、创建分支，再修改产品。此授权仅覆盖这些问题的必要后端、QML、构建和定向验收接点，不继续历史 UI migration 队列。

## 开工基线与隔离

- 基于已发布 2.0.2 的本地 main `66cd3e50590766e5a654528ab61e491dc4d30c83`，新分支 `codex/field-upgrade-20261003`。
- 工作区 `E:/项目/Veyra/worktrees/field-upgrade-20261003`。桌面旧迁移工作区及其它工作区保持原样。
- 开工存档 `E:/项目/Veyra/archives/field-upgrade-20261003-start`：全引用 Git bundle（已 verify）、桌面 tracked binary diff/index diff、未跟踪源码、状态和 SHA256 清单；标签 `checkpoint/pre-field-upgrade-20261003` 指向上述 main。首次 PowerShell 的 git --output 参数解析失败，已在产品修改前重新生成并核对 working patch 258615 bytes、index patch 0 bytes。
- 输入证据复制到 `E:/项目/Veyra/logs/field-upgrade-20261003/inputs`，原微信日志和截图不改、不提交。
- 新建构建、tests、logs、tmp、候选包都使用 `E:/项目/Veyra/<用途>/field-upgrade-20261003`。每个测试最多 300 秒，单次构建最多 900 秒；仅对子进程设置 TEMP/TMP。
- 旧桌面 scope guard 固定旧目录、旧分支和已过期 main；本轮独立授权与分支不使用它证明通过，也不修改旧 guard/baseline。每片修改和测试前检查新分支文件范围。
- 不派子 Agent；不合并 main、不 push、不发布、不删除旧 UI，不改变用户设置或 RTSS/OBS 配置。开工标签为用户明确要求的存档，代码先保留为可审查 diff。

## 已确认事实与待证假设

1. 日志 16/17 存在 Xbox `WNSError / WaitingForServerToRegister`，也有成功完成 ICE/DTLS/SCTP、握手和持续视频的会话。前者是服务端主机注册失败，不能靠本地重试隐藏成成功。日志 2 的文本只有 `device / nvppex.dll`，只证明出错时该模块在进程里，不能证明它造成错误。
2. Xbox 音频现有代码按解码样本数从零生成 PTS，视频按 90 kHz RTP 生成 PTS；需要核对共享实时音频入口是否要求视频时钟域。还需验证 RTCP 接收统计、反馈发送和关闭回调，不能只延长超时称稳定。
3. 导出 UI/bridge 接受 0–2000 Mbps，而 `EnhancementSettings::validate` 上限 300 Mbps；`VTextField` 只在 editingFinished 提交，exportChanged 会刷新文字绑定。两个缺陷都需真实交互复现；VBR 的实际平均码率不保证等于目标，不能单凭约 7000 kbps 断言编码器没接码率。
4. `Main.qml` 窗口透明；背景 `VBackdrop.qml` 使用 Qt Quick Shape/纹理填充。RTSS/OBS 兼容会切到软件 UI，必须检查其不支持的绘制节点，提供可见、不透明的背景路径，保留单一 D3D12 视频窗口。
5. NVIDIA App 注入模块目前会在设备失败/上次失败后推断原因并提示。按用户要求删除这类检测和因果提示；保留真实 D3D12 错误及 RTSS 必需的兼容模式选择。

## 施工顺序与验收

### A. 导出码率

逐键提交有效输入，防止无关 exportChanged 覆盖正在编辑的文字；范围与后端 1–300 Mbps 一致，空输入/非数字不得静默生成无效设置。开始导出前使用同一份已提交参数冻结任务；错误显示具体验证原因。检查输入后切编码/容器/分辨率/质量/预设、直接点击开始、离开页面重回、CBR/VBR/CQ 切换、300 边界及超界输入。用产品短片导出记录 worker 实际 rateControl/bitrateMbps，再用 ffprobe 核对输出，区分目标和实测码率。

### B. RTSS 背景和 NVIDIA 检测

软件 UI 提供普通 Rectangle/Image 背景，不依赖 GPU Shape/MultiEffect；各页面和极简/全屏、不同背景强度不出现桌面透出。GPU UI 保持原有圆角背景。删除 NVIDIA App 插件检测、旧 marker 引发的该类提示和“已知会崩溃”的归因文字，真实 GPU 故障仍按实际错误报告。RTSS Auto/Off 和 OBS 兼容开关做交互截图；不更改外部软件配置。

### C. Xbox 音频和会话

先从日志建立会话时间线，再对照已固定的 Greenlight/libdatachannel 源码查协议。音频加入包计数、Opus 错误、配置/输出计数和时钟域诊断；修正已证实的 RTP/实时音频域或处理链缺陷。网络失败、服务端拒绝、视频处理失败分开记录；RTCP/心跳/反馈必须依据协议并用真实本地 peer 验证。保留有界队列、关闭期间不抛异常、不回调已释放对象，音频不因 GPU 慢而停摆。自动重连若必要，限制次数并显式显示，不无界重试。单测试 300 秒以内；真实 Xbox 长稳和声音须由持机反馈验证，不冒充本机已验。

### D. AMD RX 9000 NR

已定位上游 `https://github.com/lmxxf/dlss5-on-amd-9070xt-porting`（用户写作 Imxxf/MTI）。获取官方源码到 E 盘 deps，固定 commit 和 MIT 原文；核对 `include/LmxxfNrApi.h` 的 standalone Runtime API、GPU 架构、D3D12/HIP 互操作、输入颜色/codec、异步 fence 与会话销毁规则。优先调用上游独立 Runtime，禁止依赖 ReShade/游戏注入，禁止另造网络。源码 MIT 不代表 NVIDIA 衍生权重也属 MIT；权重、HIP 模块/SDK/Runtime 均不进源码 Git，也不未经身份和来源记录加入公开包。

AMD 后端接入共享 NR 实例和图，列表/节点/图片/视频导出一致；保留 NVIDIA 路线，按 GPU/Runtime 能力选择。RX 9000 的 gfx1200/gfx1201、同一 D3D12 设备与 HIP 设备对应、缺失 DLL/模型、API 版本不匹配、创建/执行失败、resize/reset/多实例/源切换都需显式处理。不让 AMD 卡尝试 NVIDIA NGX/NVOF；既有 FSR SR/FG 保持。若上游契约无法满足现有同步/颜色/授权边界，记录具体缺口，不用接口桩或 UI 开关冒充接入。当前只有 RTX 5070，AMD 真正推理和性能/画质必须标未验，软件模拟 ABI 测试仅证明宿主契约。

## 交付记录

每片在本文件和 WORKLOG 记录修改符号、实际命令、结果、失败修复与证据路径。构建统一当前分支产物，针对性 unit/QML/本地 WebRTC/短片 GPU 导出及非 AMD 回归通过后生成独立本地候选。结束核对 source Git 没有 SDK/DLL/模型、main/桌面原状态未改，保存最终 diff 和源码证据。未运行或无对应硬件的项目明确列出，禁止将编译、上游声明或本地 peer 当作 Xbox/RX 9000 实机通过。

当前状态：必要代码与本机回归已完成。Xbox 输出线程与有界重连已实现；导出与
真实 RTSS 软件绘制通过；AMD 独立运行时和用户 0.39 完整资产已接入。RX9000
推理、真实 Xbox 有声/长稳仍待实机；原生 4K AMD NR/HDR 未支持。证据见
`FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md` 和 `AMD_NR_INTEGRATION_2026-10-03.md`。
只生成本地候选，不发布。
