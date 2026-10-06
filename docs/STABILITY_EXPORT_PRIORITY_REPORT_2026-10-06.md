# 2.0.4-fix2 修复与验收记录

开工 2026-10-06，记录完成 2026-10-07（Asia/Taipei）。本地候选，未合入 main、未公开发布。起点 main `578d63c3a0143429b306e26eeb89137473b4d99f`，工作树 `E:/项目/Veyra/worktrees/stability-export-priority-20261006`，分支 `codex/stability-export-priority-20261006`。包含该起点已经合入的 PR19/20 及 fix1。

## 已确认的问题和修复

- **补帧入口错误**：通用“添加补帧”按当前选中的 DLSS 能力判断，缺少 NVIDIA 运行库时，即使 FSR/XeSS 可用也会被禁用。原发布 AMD 包在本机 RTX5070 上真实复现这一缺组件条件：DLSS disabled、FSR/XeSS available、通用项 disabled、添加返回失败；这不是模拟 AMD 实卡。候选版通用项按所有可用后端判断，旧无效选择可以切到可用后端；AMD/Intel 新配置默认 FSR。后端各自的硬件限制保持。
- **FSR 导出误换 DLSS**：`VideoExportJob.cpp` 强行把跨厂商补帧改成 DLSS，再用 NVIDIA-only 导出检查拒绝 AMD。`git blame` 显示替换语句来自 `6c70eb2b`（09-16），FSR 扩大匹配来自 `4fbc2b02`（10-01），都早于 2.0.3。不能把这一问题归咎于刚合的 PR19/20。现在 FSR 3.1/FSR 4 使用现有共享图的补帧纹理送编码器，保留后端和倍率，不另造播放器/导出循环。FSR 4 ML 的设备/provider 检查不被绕过。
- **XeSS 导出边界**：当前集成的 XeSS 管理 Present，不暴露可编码补帧纹理。界面和工作进程均明确提示选择 FSR、DLSS 或 VFG，拒绝时编码帧数为 0；不再悄悄改为 DLSS。此次未实现 XeSS 视频补帧导出。
- **AMD NR 的 4K 导出失效**：旧代码用片源尺寸判模型预算，离线 planner 又把 NR 强制 native。现在冻结全局/每层设置、固定执行计划和工作进程一致：AMD lmxxf 视频导出最高使用内部 1080p，保留原工作/输出尺寸做残差合成、编码；原生模型预算检查仍在。NVIDIA NR 导出继续 native，图片导出的原尺寸预算合同不被扩大。界面说明区分视频与图片。
- **显示同步**：2.0.3 和发布 2.0.4 的 Auto 均走 `Present(0,0)`；没有证据证明这一标志差异导致了新旧撕裂回归。此次把 Auto 明确改为同步显示（`Present(1,0)`），窗口和无边框全屏一致；手动允许撕裂仍走 `Present(0,ALLOW_TEARING)`。原有同步时队列深度 1 的策略保留，未增大队列。新增交换链/同步切换后的实际 Present 参数日志。提交帧率受屏幕刷新率限制，不能把软件提交率称为物理显示帧率。用户现场的撕裂/运动重建瑕疵尚未直接复现。
- **GPU 优先级**：查询到请求档位已生效时避免重复写入调度 API，保留用户既定默认实时档和手动选择。仅修改本进程，不修改驱动、全局系统设置或其他进程。此项没有经实测证明提速，不计优化收益。

同步说明依据 [Microsoft Present 接口](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present) 与 [NVIDIA 交换链建议](https://developer.nvidia.com/blog/advanced-api-performance-swap-chains/)。交换链模式、合成器和驱动设置会影响实际显示路径；本次测试验证 API、像素和生命周期，没有测量面板扫描或端到端延迟。

## 性能疑点：不能宣称完全没有回退

本机 RTX5070，驱动 616.56。同一 M2 4K60 视频，SHA256 `19482f441ee53f8ed35b2b95da343b18d2fce5764181aefb45bb4c32c46fa943`；同一 NVIDIA 原版 NR 310.8.0.0，SHA256 `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`。单层内部 1080p，风格 0、模型强度 1、总变化 1，自动画面调控/抗闪烁/时域关闭，SR/FG/HDR/调色关闭，硬解自动，窗口 1280×800、声音静音。用户未给出旧测试的全部画质开关，上述是明确的控制条件。

每轮普通播放 35–40 秒，串行，无竞争/压力负载。表中毫秒是**一秒 GPU 平均读数的中位数**，不是逐帧 P95，不是屏幕延迟。优先级数值来自 D3DKMT 查询。Qt active 是界面记录；外部 foregroundOwned 查询与 Qt 状态存在不一致，不能把该查询宣传成完全受控的 Windows 前台证据。显卡时钟和其他系统活动未锁定，日志检测到 NVIDIA Overlay 注入，未修改/关闭用户覆盖层。

| 测量记录 | 请求 GPU 档位 | NR ms | Qt active/观察 | 提交 FPS 中位数 |
|---|---|---:|---:|---:|
| released203-normal | 普通 | 5.976 | 33/33 | 60 |
| released204-normal | 普通 | 6.826 | 5/31 | 60 |
| released203-normal-explicit | 普通，诊断中显式设置一次 | 6.475 | 27/27 | 60 |
| released204-normal-focused | 普通 | 6.417 | 25/25 | 60 |
| released203-normal-focused | 普通 | 6.294 | 25/25 | 60 |
| candidate-normal-first | 普通，尚未改 Auto 的中间构建 | 6.730 | 25/26 | 60 |
| released204-high-focused | 高 | 6.422 | 13/26 | 60 |
| fix2-normal-final | 普通，Auto 同步 | 6.932 | 26/26 | 60 |
| released203-bracket-final | 普通 | 6.472 | 27/27 | 60 |
| fix2-tearing-control | 普通，允许撕裂 | 6.803 | 26/26 | 60 |

第一对前台状态差异明显，不能以其约 14% 差值断言算法回退。Qt active 相同的早期对照差约 2%；末轮旧版 6.472 vs 候选 6.932 差 **7.12%**。候选同步/允许撕裂两轮相差约 **1.90%**，因此同步行为不能解释全部差异。单轮和系统状态波动不足以确定因果，但也不能抹掉约 7% 的疑点，或把调高优先级当作已经修复。新包普通档约 6.9ms、60FPS 通过；此条件下没有复现“必须高/实时才能达标”，不等于所有设置/多程序负载已恢复 2.0.3 性能。真正的剩余工作是更严格分离界面 GPU 负载、系统调度和 NR 计时，当前不宣称性能回退根因已解决。

原发布包 EXE：2.0.3 `998d42617e0455523b6f39741ec82206c4685f043794a5235c2853f8c5a98154`；2.0.4 `d01329aa6d9fbd6ac2d1eee294782f4cb9d233490595ed2e3e1a9ea12c16b2d6`。最终候选 EXE `21e8e89c37d36f2cc43e7c619382185459ba36f76edefd602666050d6a20285b`。中间构建和所有失败/无效对照均保留，不挑选最佳一轮冒充固定收益。

## 构建和真实验证

日志根目录 `E:/项目/Veyra/logs/stability-export-priority-20261006`，测试根目录同名 `tests`，构建在 `build/.../standard`。实际运行命令以脚本参数和对应 JSON/console 收据为准；Python 为 `C:/Users/123/AppData/Local/Programs/Python/Python311/python.exe -B`。

| 命令/记录 | 验证内容 | 结果 |
|---|---|---|
| stability-export-priority-build.py build-final-product ... | 标准配置，现有 patched FFmpeg、Qt/libass、串流依赖；最终产品 | 通过，EXE 同上 |
| build-fg-final / build-export-test-final | 新测试最终目标 | 通过；仅测试变动，产品 EXE 不变 |
| stability-export-priority-tests.py units-final units | 效果链 264、修复契约 246、预设 482、能力 56、呈现时序 78、i18n | 0 失败 |
| gui-amd-before AMD before / gui-amd-after-fixed AMD catalog | 发布包真实入口错误与修复后选择、Chain+FG+Flow 列表预设 | 旧错误复现；修复通过 |
| gui-fsr-export-second NVIDIA export | 4K60 输入 FSR2X 窗口/全屏/回窗口，真实 UI 导出 | 120 提交 FPS；720p30 的 60 源帧编码为 120 帧 |
| gui-fsr-restore NVIDIA restore | 重启恢复 FSR、倍率、启用状态、列表预设和 Flow | 通过 |
| fsr-no-sr-final fsr-export | 无 NR/SR 的真实 FSR→D3D12 NVENC H.264 | 60 源 +59 生成 +1 保持=120 编码，1280×720 |
| fsr-sr-nr-first fsr-sr-nr | FSR SR→NVIDIA 原版 native NR→FSR FG→H.264 | 120 编码，2560×1440 |
| fsr-hevc-final fsr-sr-nr-hevc | 同链 HEVC | 120 编码，2560×1440 |
| nr-native-stack-final field-stack-hevc | 单独两层原版 NR，预览尺寸/旧汇总字段不同的冻结链 | 60 编码，native 1280×720；参数/链一致 |
| export-timestamps.json | 四个真实导出文件全部帧解码、PTS 检查 | 120 帧，0–1.983333 秒，严格 60fps，文件时长 2 秒 |
| xess-reject-final reject-xess | 未暴露纹理后端拒绝 | 明确 XeSS 原因，0 编码、无最终输出，不替换后端 |
| amd-4k-final graph | 真实共享 GPU 图、恒等提供器；1/2/4 层、时域/reset，另加 4K 输出/内部 1080p | 10 例，0 失败；4K 1/2 层 RGB 最大误差 0，实际 evaluate 3/6 次 |
| display-third display-sync | 窗口/无边框全屏×低延迟开关×Auto/Vsync/Tearing | 12 例，实际 sync/flags/像素正确，D3D12 错误 0 |
| fg-pixels-second presentation | 实际 DLSS 4X/6X/4X、resize/reset、独立呈现队列、跳过生成帧时原帧等待、双缓冲复用 | 114/190/114 生成帧，最大 RGB 误差 0，D3D12 错误 0 |
| stability-export-priority-cold.py cold-loader-paths | AMD/NVIDIA 两包新 profile，Windows-only PATH，实际 loader 模块路径 | 均通过；各 26 个 Qt/FFmpeg 模块来自本包 |

本轮不运行仅支持旧 Win32 的 `scripts/gates/delivery.ps1`，使用实际 QML 入口和以上针对性验证。每个测试进程 ≤300 秒，构建 ≤900 秒。此表不把历史验收算作本轮通过，PR19 HDR 字段/曲线和 OBS fix1 源码未改；不据此宣称物理 HDR、采集设备或 AMD 编码已验收。

## 失败记录和对抗检查

- build-second：新 AMD 4K 测试块误放到引用变量声明前，C2065；调整测试块顺序后 build-third 通过。
- display-first：测试在原生共享队列未排空时释放 presenter，导致已删除资源和 DEVICE_HUNG。正式引擎相应路径已经 drain；修正测试按正式生命周期执行。display-second 又错误把累计提交数当单例计数；改为前后差值。最终 12 例及生产 GPU 独立呈现测试无错误。原错误不当作用户故障根因。
- gui-amd-after / gui-fsr-export：夹具先只选 Chain 但断言 FG/Flow 恢复，以及重复添加已有单例 FG；按真实选择范围 21（Chain+FG+Flow）和现有列表修正后通过。
- fg-pixels-final：夹具传了包内不存在的旧 runtime_local/nvidia 配置路径，NGX 配置缺失；改用实际 runtime/experimental，保留错误日志，最终真实 FG 通过。
- 首个 fsr-export 夹具用子串 sr 匹配到 fsr，实际包含 SR；该轮保留为 QHD 导出证据，修正为 -sr 匹配后新增无 SR 的成功检查，不冒充先前测试。
- cold-stage：psutil mapped section 路径记录了先前硬链接别名，造成 NVIDIA 来源误判。使用同一自有进程 EnumProcessModulesEx/GetModuleFileNameExW 的 loader 路径复查，两包来自自身目录。未修改包/DLL 来迎合测试。
- 增加冻结前的实例上限检查，防止 normalization 掩盖坏交易；NVIDIA native、AMD 模型预算、每层强度/风格、后端/倍率、旧持久化版本、生产不回读像素及有界队列分别复核。

## 交付与未验证项

完整 AMD/NVIDIA 便携包及对应应用源码：`E:/项目/Veyra/test-packages/stability-export-priority-20261006`，文件名 `Veyra-2.0.4-fix2-{AMD,NVIDIA}-win64-portable.zip` 与 `Veyra-2.0.4-fix2-source.zip`。封包收据 `DELIVERY.json` 记录最终源码提交、EXE/ZIP SHA 和逐文件校验；仅收据存在且通过才代表压缩包完成。

全部沿用 fix1 已批准运行组件，AMD/NVIDIA 分别 694/215 项逐文件身份保持。patched FFmpeg 与 libass/Qt 对应依赖源继续使用已发布 2.0.4 的固定 SHA `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`，不提交 runtime/SDK/模型到源码，不修改任何 proprietary DLL。开工封存的 25 工作树及 14 发布文件继续由本轮 control 核验，main/tag/Release 不变。

**仍未验证/未完成**：真实 AMD HIP 的高分辨率离线推理与 AMD Media Foundation 编码、FSR4 ML 实卡导出、用户显示器上的实际撕裂/VRR/覆盖层、所有来源/设置下的稳定性，以及上述约 7% 性能差的确定根因。用户此前 RX9000 NR 可用反馈保留，不能扩展成此次导出已通过。请在新目录解压测试，不混装旧运行库；AMD 图片超预算与 XeSS FG 导出仍有明确限制。本地候选用于复测，当前不满足宣称所有反馈已根治或直接正式发布的条件。
