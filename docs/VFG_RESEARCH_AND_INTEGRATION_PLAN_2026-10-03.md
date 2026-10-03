# VFG 调研与接入方案（2026-10-03）

## 结论与本轮范围

NVIDIA Video Frame Generation（VFG）确实已提供公开文档、MIT 示例和可下载的官方运行包。Veyra 接入在技术上可行，值得优先做 2X / 4X 的独立补帧后端；本机 RTX 5070 已成功运行 SDK 1.3.0、生成中间帧并通过切镜头旁路短测。当前只完成调研、独立调用和施工方案，没有把 VFG 加入播放器，不能将下表当作完整 Veyra 性能或真实影片画质验收。

源码基线 `a93af429930fd83563e0e4f1ae4e3c6a24623b25`，调研分支 `codex/vfg-research-20261003`。产品源码修改前建立 `checkpoint/pre-vfg-research-20261003` 和 `E:/项目/Veyra/archives/vfg-research-20261003/source-before-vfg.bundle`，verify 通过（71,358,976 bytes，SHA256 C5C5A64A401A3BC880B4715285313FF4327CD9A736BCB6ABFD31675A340AAFE2）。独立 SDK 探针先于此源码存档执行，期间没有修改产品；本方案是之后实施的依据。原五项修复分支仍为 a93af42，main、桌面旧目录和已交付候选未改。

## 官方开放程度与硬件

- [官方功能文档（固定 SDK 1.3.0）](https://docs.nvidia.com/maxine/vfx/1.3.0/Filters/VideoFrameGeneration.html)：两张同尺寸帧输入，每次返回一个中间帧；倍率 API 为 2–8，也能指定 `(0,1)` 中的任意时间位置，提供 Low / Medium / High。输出缓冲会被下一次调用覆盖，需要及时消费或复制。
- [官方 AI for Media 页面](https://developer.nvidia.com/topics/ai/generative-ai/ai-for-media)：2X / 4X 标为生产使用，6X / 8X 标为实验/评估。[R22 公告](https://forums.developer.nvidia.com/t/ai-for-media-r22-rel/382943)说明 6X 以上早期体验。公开包本机接受 6X、8X 的短调用，不据此把它们升级为产品验收。
- Windows x64 的 VFG 官方支持 Ada / Blackwell，即 RTX 40 / 50 系；RTX 20 / 30、AMD / Intel 不在该功能官方范围。SDK 总体支持的显卡更多，不能拿总体支持表替代 VFG 的专项表。
- [Windows SDK 前提](https://docs.nvidia.com/maxine/vfx/1.3.0/WindowsVFXSDK/GetStartedonWindows.html)：SDK 通用 x64 驱动基线为 570.65，具体功能仍需实际初始化。本机 RTX 5070 / 616.56 实际创建、加载、执行成功；用户 616.92 未直接测试。
- [NGC VFG](https://catalog.ngc.nvidia.com/orgs/nvidia/maxine/collections/nvvfxvideoframegeneration/-)、[Core](https://catalog.ngc.nvidia.com/orgs/nvidia/maxine/resources/vfx_sdk_core/-)提供下载入口，Core 原生头文件与安装工具需 NGC 访问权限。正确 API 的匿名版本请求返回 401；最初旧 URL 404 只是路径错误。
- [NVIDIA Python 包 0.2.0.0](https://pypi.org/project/nvidia-vfx/0.2.0.0/)于 2026-09-30 发布，内部 SDK 1.3.0.0；官方 `https://pypi.nvidia.com/nvidia-vfx/` 可匿名取得 Windows wheel，已实测。无需为本次独立调用向用户索要 NGC API key。

## 已固定的来源与身份

官方样例 [NVIDIA-Maxine/VFX-SDK-Samples](https://github.com/NVIDIA-Maxine/VFX-SDK-Samples)，commit `52011f89c1741d06b40ea312af1f20be8be9ec62`（2026-09-30，v1.3.0.1）。MIT 原文、VFG README、CMake 和调用示例保存在 `E:/项目/Veyra/deps/vfg-samples-20261003`；它们展示 API 配置，不将样例的 OpenCV CPU 来回搬运路径用于正常播放。

官方包 `nvidia_vfx-0.2.0.0-cp311-cp311-win_amd64.whl`，435,809,365 bytes，SHA256 `5aaf6a42bc6b6dbbf52fcb714194c994a6893cbbf7ada38bc2165a1f83e4a6fc`，与 NVIDIA index 一致。下载 `E:/项目/Veyra/downloads/vfg-research-20261003`；解压 `E:/项目/Veyra/deps/vfg-python-20261003`，35 文件、562,709,656 bytes，没有覆盖旧 0.1.0.1 / 1.2.0 审计目录。

关键文件均 Authenticode Valid / NVIDIA：

| 文件 | 大小 | SHA256 |
|---|---:|---|
| NVVideoEffects.dll（1.3.0.0） | 104560 | C6B042FC57BDB6BEA88C9C52CB951582377D3F1643ACF52A52D197195A1EA8F4 |
| NVCVImage.dll | 2694256 | 689A64586C804468127647B7ED24ACC36A9A9780502FFE95AD11AA4FE3286A39 |
| nvVFXVideoFrameGeneration.dll | 207321712 | 270CF4FFF9329908F9770D306CB51B910EE04323AF5520CD02A98A2E22ABA795 |
| cudart64_12.dll | 584304 | 663B5FE65AA6C654B8B085A879F49DEB19F2CB033B8F2FF8DFFD06CDEFE0D1DB |

样例 MIT、运行库的 NVIDIA 软件许可、模型的 NVIDIA Open Model License 分开记录。NGC 标注可商用/非商用；能下载和本地研发不直接证明每种包的再分发条件。正式包需核对实际组件许可证和源码许可证关系，不把 runtime/模型放源码 Git，不把这批新文件混进原白名单，不擅自上传。本次 wheel 内附许可仍是其实际提供的版本；网页更新条款及 Developer Program 的开发用途限制需在选择公开分发渠道时核实。

## 本机短测结果

探针 `E:/项目/Veyra/tests/vfg-research-20261003/{first-probe,benchmark,benchmark-varying}.py`，日志 `E:/项目/Veyra/logs/vfg-research-20261003`；TEMP/TMP、CUDA/CuPy cache 均在 E 盘本任务 tmp。单探针超时分别 120 / 240 / 180 秒，实际正常结束。

- first-probe：1280×720 Medium / 2X 产生有限数值的真实输出；手动 shot_change 输出当前帧，最大误差 0.001961，在 RGB8 量化范围内。加载约 143ms、第一帧约 7.70ms，首次/热身成本与稳态分开。
- benchmark：720p / 1080p / 4K Medium、1080p Low / High、1080p RGB10A2 各 2X / 4X，12 组全部正常返回。10-bit 仅证明打包输入调用，不据此宣称 PQ/HLG/scRGB 颜色或 HDR 画质通过。
- benchmark-varying：3 套预分配的移动图块帧对轮换，热身后每档 12 个帧对；统计 CPU 提交到 CUDA deviceSynchronize 完成的整组耗时，包含 Python 包转换/提交，不是裸神经网络 GPU 时间。4X 为生成三张帧的合计，不是单张时间。

| 模式 | 2X 整组中位数 | 4X 整组中位数 |
|---|---:|---:|
| 1080p Medium | 2.89ms | 8.46ms |
| 4K Medium | 3.88ms | 11.97ms |
| 1080p High | 14.59ms | 44.05ms |

初次固定帧对的 1080p Low 为 0.73 / 2.18ms，RGB10A2 Medium 为 2.69 / 8.20ms；这两项没有轮换输入复测。初次 JSON 的 `allocatedCudaBytes` 字段实际为 `memGetInfo` 的 free/total，不能当作进程分配显存；轮换报告改为准确名称 `cudaFreeAndTotalBytes`。

4X 的三个输出 SHA256 不同，红块质心随 t=0.25/0.5/0.75 从 714.65→722.00→729.81 推进；显式 t=0.4 输出正常，6X / 8X 公共 API 接受短调用。没有真实影片 AB、与 DLSS/XeSS/FSR 的同素材比较、Veyra NR+SR 叠加、D3D12/CUDA 互操作、呈现节奏或屏幕/操作延迟验收。最高档 1080p / 4X 单阶段约 44ms，已超过 30fps 的 33.3ms 源帧预算，不能默认全开。

## 接入方式

1. **独立 backend、共享图。** 新增 C++ VfgBackend，仅在支持的 NVIDIA adapter 上从绝对路径加载对应库，使用原生 NvVFX / NvCVImage API。先按 MIT 样例的 CreateEffect、SetCudaStream、SetU32(InputWidth/Height/Mode)、SetImage(previous/current/output)、Load、FrameMultiplier/FrameIndex、Run 顺序验证，不把 Python/CuPy 加为产品依赖。缺库、CUDA adapter LUID 不同、初始化失败时明确失败并保留可运行的已选后端。
2. **GPU 互操作。** 现有 NVOF 是 D3D12 原生路径，不能当作已有 CUDA bridge。使用 [CUDA 外部资源互操作](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)共享 D3D12 buffer / fence，借 GPU 拷贝将输出纹理映射到 NvCVImage 的线性 CUDA buffer；producer signal→CUDA wait→Run/复制每个结果→CUDA signal→D3D12 queue wait，资源创建时注册、销毁时排空，不逐帧 CPU fence 等待，不做正常播放像素回读。
3. **帧时序与资源。** 放在现有所有增强之后、字幕/OSD 之前，保留一套 A/B 源 PTS 与 FrameBatch/FrameLease、现有有界池和 consumer fence。每个生成帧按 A+(B-A)×t 标记时间与当前 epoch/revision，先消费/复制复用输出缓冲再调用下一张。VFG 从两帧推断运动，不需要外部深度、游戏运动向量或强制 NVOF；NR/SR 若仍需要光流，就保留其原有执行。
4. **颜色与 reset。** 输入支持 RGBA/BGRA u8 或 RGB10A2，同一帧对三张图的尺寸/编码家族一致。利用图中现有最终编码边界，不能将 linear FP16 直接伪装为 u8 / PQ，也不能把 10-bit 等同 HDR 已验。seek、断点、丢帧、源切换、resize、暂停恢复、device-lost 清空 A/B 和租约，切镜头手动 ShotChange 只覆盖对应帧对，避免持续残留。
5. **设置与导出。** 为 VFG 追加稳定 backend ID，原 0..3 不重排；保存 Low/Medium/High 与 2X/4X，列表/节点/会话/预设/导出任务快照同源。VFG 与其它 FG 共用当前最后一个补帧节点。导出使用返回的生成纹理和原 NVENC D3D12 路径，不靠录屏、不落到 CPU raw pipe；复核旧 crossVendorFrameGeneration 的替换分支，避免新 VFG 被误换成 DLSS。FrameBatch 当前容量 6，首期 2X/4X 可复用；8X 要单独扩大 pool、lease、倍率验证和测试，不能只加菜单项。

## 实施切片与验收

- **P0 已完成：** 官方来源/API/硬件/许可调查、固定包与样例身份、独立 GPU 生成、档位预算短测；产品接入尚未实施。
- **P1：** 原生 C++ SDK 头文件/依赖固定与 GPU bridge；先做传输 identity 和独立 VFG 输出，验证同 LUID、异步跨队列 fence、alpha/行距/格式、关闭/错误/resize，不能用假后端当实际推理通过。
- **P2：** 共享图和列表/节点 UI、2X/4X、默认 Medium、参数保存与热切换。多来源保留媒体时间；NR/SR 支持既有链路，输出过载走现有预览调度策略。
- **P3：** 视频导出接入与真实文件验证（源帧/生成帧/hold 计数、PTS/音频、24/30/60fps、1080p/4K、HDR 专项、取消/恢复）；不得以出口数量当生成质量。
- **P4：** 同素材对照 DLSS/XeSS/FSR，快摇镜头、遮挡、细线、内嵌字幕/HUD、切镜头和真实采集/主机输入，低/中/高分开测；实机 cadence、持续播放、显存与操作延迟通过后再决定默认推荐。6X/8X 与任意目标帧率转换另验。

VFG 要等后一帧 B 到达，30fps 的源间隔是 33.3ms，60fps 是 16.7ms；这是两帧插值的前提，不是本机屏幕延迟测量，也不能假定它比现有 A/B 补帧再多一个源帧。文件可预读，实时采集/串流必须测完整延迟。基于目前证据，建议进入 P1/P2，保留已支持的后端；最终画质优劣和性能收益由同素材实测决定。
