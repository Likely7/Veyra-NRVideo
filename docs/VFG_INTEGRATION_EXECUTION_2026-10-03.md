# VFG 全档位接入施工（2026-10-03）

## 授权与开工存档

用户明确要求接入 VFG，支持 8X，并追加“所有档位都加上”。范围为原生 C++ NVIDIA VFG 后端、预览及视频导出、2X/3X/4X/5X/6X/7X/8X、Low/Medium/High（默认 Medium）、列表/节点/会话/预设/导出任务保存，以及完成这些功能所必需的共享图、GPU 互操作和资源池。保留原后端的合法倍率，不将 VFG 的 8X 放开成 DLSS 8X。

源码起点 `1802f43565e07f3c3040d3fe745ca2e939aab00f`，工作树 `E:/项目/Veyra/worktrees/field-upgrade-20261003`，新分支 `codex/vfg-integration-20261003`。产品编辑前完成 `git bundle create ... HEAD`、`git bundle verify`、干净状态记录与 tag `checkpoint/pre-vfg-integration-20261003`。存档 `E:/项目/Veyra/archives/vfg-integration-20261003-start/source-before.bundle`，SHA256 `1F3154049591C069B6CD63D266FC4447A153480D41608086BEB1422092E8D6F0`。原修复和研究分支、main、桌面原工作树不改。

SDK 来源/固定版本/哈希/许可/研究证据见 [研究方案](VFG_RESEARCH_AND_INTEGRATION_PLAN_2026-10-03.md)。研究方案中的首期仅 2X/4X 被本次全部倍率授权覆盖。官方 2X/4X 生产档和 6X/8X 实验档的区别保留；3X/5X/7X 为公共倍率 API 可选项，不凭整数范围宣称已验，逐档实际测试。

## 实施与约束

1. P1 原生 API 和 GPU bridge：固定 SDK ABI，C++ 动态加载绝对路径和受限搜索；CUDA device LUID 与 D3D12 adapter 相同。共享 D3D12 buffer/fence 在创建时导入，GPU texture/buffer 拷贝，CUDA 流等待 producer、运行/保存每个结果、signal consumer；D3D12 queue 等待后使用拥有租约的生成纹理。无 Python 产品依赖、无逐帧 CPU fence wait、无生产像素 readback。初始化/退出可排空。
2. P2 共享图：稳定 backend ID 4，FG 最后、OSD/字幕之前；无需外部 guidance，其他 NR/SR 的 flow 保留。FrameBatch 8、生成池 14（2 parity × 7）、GPU 计时/状态数组/描述符/回收一致扩容。A/B 真实 PTS，首帧/seek/切镜/掉帧/resize/暂停恢复/源切换/device lost 原子 reset，避免旧帧跨 epoch。输出缓冲被重写之前保存，不以重复帧当生成帧。
3. P3 设置/UI：全部倍率及质量档后端专用；旧 ID/预设/默认不破坏。会话、预设、任务快照、worker serialization 相同；热切换失败明确回退并显示实际后端。6X–8X 明示实验；3X/5X 使用公共 API，在验收报告独立列证据。
4. P4 导出：保持 D3D12 NVENC 与冻结 settings，VFG 不进入 XeSS/FSR 替换 DLSS 分支。每个间隔 M−1 张真实生成帧与每张源帧进入 CFR 时间线；首尾 hold、切镜 bypass、失败/取消必须准确计数和记录，导出不丢源帧。
5. P5 验收：GPU bridge identity、2..8 全倍率输出/非重复/PTS/租约、三质量、reset/关闭/resize/失败，SDR RGBA8/10-bit 边界单测；真实产品预览/热切换/保存恢复及 NVENC 文件 frame count/rate/duration/audio。NR+SR 组合、1080p/4K、24/30/60fps 性能/显存分开记录；未接 RTX40/采集/主机、物理显示节奏/端到端延迟和 HDR 色度专项不得冒称通过。

仅必要范围，不重写 source/audio clocks/NR/SR。不依赖 Streamline/ReShade，不复制 proprietary SDK headers/models/runtime 到 Git，不改磁盘运行库或哈希锁。SDK 默认本机研发，公开分发需另做许可与组件审计；本轮无上传/发布授权。故障若只能传输或 UI 测试通过，不将其升级成 VFG 推理成功。

## 产物与续接状态

本任务 `E:/项目/Veyra/{build,tests,logs,tmp,test-packages}/vfg-integration-20261003`；已有 SDK 保持 `deps/vfg-python-20261003` 与 `deps/vfg-samples-20261003`。TEMP/TMP 仅设置子进程。

- P0 研究已完成；完整结果在研究方案。
- P1–P4 已实现，P5 原生720p/4K全组合、21种真实导出、GUI热切换/会话/worker/缺失组件回退、本机 NR+最高 SR4K+VFG8 已通过，详见 `VFG_INTEGRATION_ACCEPTANCE_2026-10-03.md`。
- 最终产品编译 `build-product-v4.log` exit0；设置/旧格式/QML回归 `unit-v4`、GUI `ui-v4`、组合 `combinations-v4` 通过。核心与早期失败证据均保留，不把短测当长稳或物理显示验收。
- 本地便携 staging 全GUI/worker验证 `ui-package-stage-v3` 已通过，含无环境变量/仅Windows PATH、DLSS6↔VFG8和High2实际worker冻结。首次打包根目录错误已修为 `runtime/nvidia-vfg`；原件十四DLL版本/签名/哈希审计通过。
- 最终ZIP组包及清洁解压检查在进行；没有 merge/push/Release。
