# FSR 4.1 NVIDIA 与 RTX HDR 接入调研

日期：2026-09-18。基线：main `3c994e8`，Veyra 1.4.1 发布后的文档提交。

本轮范围是可行性调研和接入方案，没有实现功能、修改驱动配置、加载新 DLL、构建或执行 GPU 测试。上游测试均为上游报告，不是 Veyra 验收。用户本轮要求重新调查 NVIDIA FSR 4.1，不能用 09-16 的旧观察结论拒绝调查。

## 结论

| 目标 | 当前证据 | 建议 |
| --- | --- | --- |
| NVIDIA 上 FSR 4.1 AI 超分 | 找到 4.1.1 INT8 的开源研究 provider；有限离线验证与单机实跑反馈，尚非成熟产品 | 固定版本做隔离验证，不直接当正式超分选项交付 |
| RTX Video HDR | 本地官方 SDK 1.1.0 有 D3D12 TrueHDR API；NVEnc 已有视频处理集成 | 优先接入共享处理图，覆盖文件、采集、串流及视频导出 |
| 游戏 RTX HDR | NVIDIA App/驱动后处理，开源工具可读写相关应用配置；未找到等价的公开纹理处理 SDK | 作为外部显示兼容路线验证，不能冒充内部滤镜或导出功能 |

## FSR 4.1 的实际进展

AMD FidelityFX SDK 2.3.0 包含 FSR Upscaling 4.1.1，官方新增 RX 7000/RDNA3 独显支持，不能据此声称官方支持 NVIDIA。FSR 4.0.2 的社区跨厂商路径也不能改名为 4.1。

最接近目标的是 `int3rrobang/fsr4-int8-reverse-engineering`：

- 固定提交 `88635b94083965a7c3b5f64e099808b8ba2ce576`。
- 研究 NVIDIA Turing GTX 1650/TU117 上的 FSR INT8，含 FFX 兼容的 4.1.1 provider。
- README 报告 11 个重实现 kernel 在相同输入状态的离线 replay 中逐字节一致；Quality/Balanced/Performance 有一次用户报告的正常实时画面。
- 上游明确称 research preview：未完成整个端到端的逐像素对齐，也没有该实时反馈的性能测量。部分完整 GPU 测试依赖未公开的私有输入，不能直接复现为我们的通过证据。
- provider 构建需要官方 4.1.1 DLL：编译 11 个重实现 kernel，提取 16 个 shader container 和 128 KiB t18 初始化数据，另用自有近似自动曝光及 host 实现。不是直接拿一份纯源码即可完整构建的成熟替代品。
- 最新 README 已记录分辨率参数化修复，不能把较早的固定尺寸说明当成最新全部能力；仍须实测各输入尺寸。

许可证核对：AMD v2.3.0 `docs/license.md` 虽以二进制限制条款开头，但明确把 `Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll` 列入后面的 MIT 例外。不能笼统称该 DLL 禁止修改，也不能把例外扩大到 SDK 全部文件。研究仓库保留 AMD 通知并要求构建产物、提取数据留在本地。接入时按固定来源逐项归因，SDK、模型、生成二进制继续不进入 Veyra 源码 Git。

其他核对：

- OptiScaler `07d360b89e5a8c7aaaca95eea29c4d555ecbb732`，GPL-3.0。有 FSR4 provider/model 选择实现及 4.1.1 INT8 支持判断，但其兼容说明不能作为 NVIDIA 4.1 已成熟可用的证明。
- `Rolaand-Jayz/RE-of-FSR-4.1.0-Upscaling`，`b75f3482bfae948e46e0569d0fe0c2b2bc2392b1`。主要是静态逆向与参考重建，作者代码 MIT，不能将它当成已验证完整替代运行库。
- `Rolaand-Jayz/Temporal-Forge-Player`，`593b23f054abbb52f69c08f7786fce03af5bf26a`，Apache-2.0。2026-09-15 已结束以 FSR 为中心的视频研究，未获得可重复的画质优势。这是视频输入语义的风险证据，不证明 FSR 在任何视频上都无效。

Veyra 当前 `src/gfx/FsrSrBackend.cpp` 已有 FFX provider 查询和 dispatch，适合复用入口。但输入是估计光流、固定渲染深度、零 jitter，不是游戏引擎的真实深度、运动和抖动采样。把已采样视频平移并不能补回真实亚像素采样信息。即使推理成功，也必须与现有 SR、VSR 和普通缩放做动态画质比较。

建议的隔离验证顺序：

1. 固定官方 4.1.1 DLL/SDK 和上述 provider 提交，记录哈希、许可证及所有改动。
2. 在独立验证进程中构建、创建、持续 dispatch；确认真正在运行 4.1.1 ML，而非静默回退 3.1。
3. 测试 720p/1080p/2160p、不同宽高比、切换尺寸、seek/reset、分配失败和退出，记录 GPU 耗时、显存及设备移除错误。
4. 同一视频同一时间戳做静态和动态 A/B，特别观察文字、头发、快速运动、遮挡和场景切换。验证完成前不宣传“提升画质”。
5. 本机通过后再分别验收 RTX 30/40/50；不能以 GTX 1650 的上游反馈或本机 RTX 50 代替所有显卡。

## 两种 RTX HDR 的区别

RTX Video HDR 是直接处理视频纹理的 TrueHDR 功能，可取回输出供播放器显示和编码器导出。游戏 RTX HDR 主要经 NVIDIA App/Overlay/驱动对最终显示画面处理；公开资料没有证明它与视频版完全相同，也没有证明游戏版画质必然更好。

采集卡、PS5 串流即使来自游戏，进入 Veyra 后也是视频帧，可以使用 Video HDR。为这些入口实现 SDR 转 HDR 不依赖先接通游戏版。

### Video HDR 的本地基础

已检查既有 `third_party_local/nvidia/RTX_Video_SDK_1.1.0`：

- `include/nvsdk_ngx_defs_truehdr.h`、`nvsdk_ngx_helpers_truehdr.h` 提供直接 NGX D3D12 API；Feature 为 TrueHDR/Reserved14。
- 查询 `TrueHDR.Available`、`NeedsUpdatedDriver`、最低驱动版本与初始化结果，不依据显卡名称假定创建成功。
- 支持 SDR RGBA8/BGRA8/RGB10 输入；输出可走 RGB10 PQ/BT.2020 或 FP16 linear scRGB/BT.709。当前图的 linear FP16 不能原样当作 SDR 输入送入。
- 可调对比度 0-200、饱和度 0-200、中灰 10-100、峰值亮度 400-2000 nits。UI 是否需要完整开放应结合实际画面和屏幕能力决定。
- SDK 指南明确 VSR 先于 TrueHDR；若使用既有 SR/NR，也应先处理 SDR，再执行 SDR 转 HDR。

现有 release DLL 的只读身份核对：

```text
bin/Windows/x64/rel/nvngx_truehdr.dll
SHA256: 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B
Size: 3955752 bytes
Version: 1,1,0,0
Signature: Valid, NVIDIA
```

这只证明文件身份，本轮没有加载或验证功能。SDK 指南第 38 页说明发布使用 rel DLL，只带所用功能；本地许可证第 1-2 节提供应用集成分发条件，第 4.e 节限制让 SDK 受开源许可证约束。应保持运行库独立许可，发布前核对与 Veyra GPL 源码的结合及对应源码义务，不能把私有 SDK 重新标为 GPL/MIT。现行官网已转为 NVIDIA AI for Media，不能把官网新产品条款直接套用到既有 1.1.0 文件。

成熟应用参考：`rigaya/NVEnc`，固定 `4cb3101f451e3f932334c994323e743808502995`，相关作者代码 MIT。已有 `--vpp-ngx-truehdr`、参数和 PQ/BT.2020 导出处理。其当前实现用 CUDA；参考 API 行为与色彩约定，Veyra 沿用现有 D3D12 后端，避免增加逐帧跨 API 拷贝。

### Veyra 接入位置与必要改动

当前 `EnhanceGraph` 拒绝 SDR 输入直接声明 HDR 输出，而且多个位置将 `hdrOutput` 同时用于工作图判断。只删除检查会破坏颜色解释；必须分清源传递函数、工作空间、SDR 转 HDR 模式和输出编码。

建议先验证以下处理顺序：

```text
SDR 解码 -> 现有 NR/SR/调色 -> 明确的 SDR 编码纹理
         -> TrueHDR（每个源帧一次）-> HDR FG
         -> 受控白电平的字幕/OSD -> HDR 显示或 P010/HEVC Main10 导出
```

NR/SR 的先后保持现有模式规则；光流仍属于 guidance 支路。TrueHDR 放在 FG 前可避免 6X 时对每张生成帧重复 AI 转换，但此顺序及 HDR FG 的兼容性必须实测，不能以 SDR 6X 已通过代替。

- 复用 `VideoSrBackend` 的直接 NGX 生命周期风格，新增专用 TrueHDR backend 并接入共享 EnhanceGraph，不复制三套入口。
- 复用 command slots/队列/fence；不得照搬样例的逐帧 CPU 阻塞等待，也不做像素 GPU->CPU 回读。
- 原生 HDR 不经过 SDR 转 HDR；对比画面的原始 SDR 需要按白电平映射进 HDR 容器，不能错误套 PQ 或额外 AI 化。
- `VideoPresenter` 已有 HDR 交换链，`NvencD3D12Encoder` 已有 P010/Main10 基础；补齐 SDR 源启用后的颜色元数据和资源格式合同。
- 开关、参数保存、暂停即时刷新、源切换、resize、seek、显示器/Windows HDR 切换及退出均需验证。
- HDR 显示需要合适显示器与 Windows HDR；离屏 HDR 导出应单独查询能力，不能仅因当前显示器 SDR 就拦截导出。
- AI 重新分配亮度不会恢复 SDR 素材中已经被剪掉的真实高光细节。

### 游戏版的边界

`Orbmu2k/nvidiaProfileInspector`，固定 `2f50c388b3a4d661cade66b32746bec096d1eee1`，MIT，提供应用配置项参考：RTX HDR 开关 `0x00DD48FB`，峰值/中灰/对比度/饱和度 `0x00DD48FC` 至 `0x00DD48FF`，driver flags `0x00432F84`。其注释指出某些 driver 路线不支持 Overlay 路线的自定义亮度/对比度/饱和度，不能把能写配置等同于效果已生效。

先验证 Veyra SDR 交换链是否实际被 RTX HDR 处理；若未来加配置入口，仅操作 Veyra 应用项并提供备份恢复。本轮没有写任何驱动项。该模式只承诺经验证的显示效果，内部截图和导出不能宣称含驱动处理结果。内部 Video HDR 与外部游戏 HDR 避免重复转换；与 Smooth Motion/内部 FG 的组合需独立实测。

## 建议施工顺序与证据

优先完成 Video HDR 最小可用链路与 HDR 导出，再做 FSR 4.1.1 NVIDIA provider 的隔离画质/性能验证；游戏 RTX HDR 作为外部兼容性支线验证。没有成熟实现的部分不以 UI 选项或 provider 名称冒充完成。

后续新产物统一放 `E:/项目/Veyra/` 的 `deps`、`worktrees`、`build`、`tests`、`logs` 子目录。本轮网络资料只读入内存，PDF 只读提取，未创建外部产物。未修改 THIRD_PARTY_NOTICES，因为尚未移植任何源码；移植发生时补充逐文件归因。

本轮实际检查：`git status --short --branch`；`rg`/PowerShell 读取现有 FSR、NGX、图、呈现及编码代码；`gh api` 和 `Invoke-WebRequest` 读取固定提交/发行说明；`Get-FileHash`、Authenticode/版本读取；用 bundled Python `pypdf.PdfReader` 只读检查 SDK 指南和许可证。系统 Python 缺 pypdf 后使用已有 bundled runtime，未安装依赖。PDF 有对象偏移警告但成功提取所需页。构建、Create/Evaluate、GPU 性能和画质检查均未执行。

## 来源

- AMD 2.3.0: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.3.0
- AMD 逐文件许可: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.3.0/docs/license.md
- NVIDIA INT8 研究: https://github.com/int3rrobang/fsr4-int8-reverse-engineering/tree/88635b94083965a7c3b5f64e099808b8ba2ce576
- OptiScaler 兼容说明: https://github.com/optiscaler/OptiScaler/wiki/FSR4-Compatibility-List
- 视频研究结论: https://github.com/Rolaand-Jayz/Temporal-Forge-Player/tree/593b23f054abbb52f69c08f7786fce03af5bf26a
- NVIDIA SDK 入口: https://developer.nvidia.com/rtx-video-sdk
- NVIDIA App HDR 说明: https://www.nvidia.com/en-us/geforce/news/nvidia-app-download-and-features/
- NVEnc: https://github.com/rigaya/NVEnc/tree/4cb3101f451e3f932334c994323e743808502995
- NVIDIA Profile Inspector: https://github.com/Orbmu2k/nvidiaProfileInspector/tree/2f50c388b3a4d661cade66b32746bec096d1eee1
