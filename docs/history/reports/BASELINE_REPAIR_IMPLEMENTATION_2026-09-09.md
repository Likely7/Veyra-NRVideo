# 原生采集颜色与实时调度修复 — 2026-09-09

状态：本机修复候选已构建；Phase 7 仍 in_progress。本文是本次实际实现记录，取代此前方案中的“修复尚未实施”。不将软件短测当作实卡画质、长期稳定或与其他软件同条件性能验收。

## 用户最新决定

用户先准备了实卡，随后明确“**不用测试这个，可以确定就是 YUY2 的转换**”。本轮据此停止实卡 A/B，不打开或占用设备，直接改造已确认不受控的转换路径；用构造像素和文件回放验证实现。不能据此写成已经逐段实测证明全部色带只有一个根因。

导出完整性检查、现有 SR/NR/FG 后端和用户默认画质决定保留。不加入深度模型、OCR、新 SDK 或前端重做；不修改 NVIDIA DLL；无远端发布操作。

## 已修改的生产路径

### 1. 原生采集进入共享图

旧的未压缩采集也强制经过 DirectShow 的 RGB32 转换器。现在 YUY2、NV12、RGB32 都根据驱动实际返回的媒体类型，用自有 DirectShow 接收端及 `ConnectDirect` 连接。YUY2 不先转 RGB32，也不先压成 NV12；压缩 MJPEG 仍走明确记录的解码/RGB32 兼容路径。诊断入口可以显式选择旧 RGB 路径，产品 UI 不启用这个诊断开关。

接收端解析 VideoInfo/VideoInfo2、行距、平面偏移、方向、采样时间和颜色范围/矩阵。YUV 对两种 biHeight 符号均按 top-down 读，RGB 保留 DIB bottom-up 规则。剔除行尾 padding，短样本在复制前拒绝；连接后的不兼容动态格式要求重开，不使用旧尺寸继续读。驱动第一次样本重复声明同一媒体类型正常接收。入口保留容量 1 mailbox，不增加无界缓存。

颜色来源有明确标记：显式 full/limited、BT.601/BT.709 优先；缺失信息使用标记为 assumed 的默认值。SDR 采集按显示端编码值做 sRGB 工作空间往返，避免把 YUV 解出的显示颜色再按摄像机 OETF 改亮。未支持的显式颜色类型拒绝，不假称支持 HDR。真实驱动是否提供正确范围仍需用户现场判断。

### 2. 一次 GPU YUY2 转换

`Yuy2ToLinear.hlsl` 直接读 Y0/U/Y1/V，维持 4:2:2 色度，到 linear FP16 工作纹理后沿用共享 SR → NR → FG → 输出路径。范围归一化和 YUV 矩阵只在此做一次，不经中间 8-bit RGB 量化。最终仍有 SDR 8-bit 输出的正常量化，并非端到端 10-bit。

同尺寸且无 SR 时，source/work 共用纹理；source/base 对比参考共用已留存的同帧纹理，跳过重复 blit/copy。同尺寸 NR 输入也共用 work，跳过身份缩放。仍保留保护异步呈现的历史纹理、lease、barrier 和 fence，不删除真正需要的同步。

RGB 显式 limited 范围也补上 16–235 归一化。通用颜色解析不再覆盖 source 已解析、但标记 assumed 的颜色契约。

### 3. 采集处理与显示截止时间分开

采集新增独立呈现 worker，容量为两个批次（含正在显示的批次）。等待子帧显示时间不会直接占住取帧/处理线程。满载时先等可用 lease，再读取 mailbox 中最新输入；历史中断、暂停、设置切换、保存和退出先取消/排空再改资源。

GPU 队列和 ring 的操作仍由 mutex 串行保护。`graph.process` 内部等待和 FRUC 互通仍可能阻塞呈现提交；这是有限的流水化改进，不能宣称已实现完全并行，或已达到其他软件的 GPU 占用。文件播放保留音频时钟及源帧顺序；导出未采用丢帧策略。

### 4. 统计与日志

- 统计窗口在 applied revision 真正改变后清空；源帧、生成帧和提交计数采用同一 revision 基数。GPU timestamp 不再被旧 source ID 阻止收集，旧 revision 的 GPU 样本显示 pending。
- 呈现 worker 每批完成时直接累计统计窗口，避免 producer 读取较慢丢掉中间样本。取消保留已提交子帧计数；没有实际提交不生成假的 0ms 延迟样本。窗口零尺寸跳过 Present 不覆盖最后有效提交时间。
- CPU 图提交时间修正计算方向。采集“callback 到 Present 返回”按 worker 的实际提交时间计算，仍不是 HDMI 到显示器光子的物理延迟。
- P95 采用最多 1200 个样本、250ms 缓存，避免每帧完整排序。文件日志按字节数/下次写入达到时间阈值批量 flush，警告、错误及显式 flush 立即刷新；不是有后台定时器保证闲置时 250ms 落盘。

## 修改文件

| 范围 | 文件 |
| --- | --- |
| 采集 | `include/veyra/source/CaptureMediaType.h`、`NativeCaptureSink.h`、`IFrameSource.h`；`src/source/NativeCaptureSink.cpp`、`CaptureCardSource.cpp` |
| GPU 颜色与资源 | `shaders/Yuy2ToLinear.hlsl`、`RgbToLinear.hlsl`；`include/veyra/pipeline/ColorMetadata.h`、`EnhanceGraph.h`；`src/pipeline/EnhanceGraph.cpp` |
| 调度/统计 | `include/veyra/engine/PresentationWorker.h`、`TimingWindow.h`、`EngineController.h`、`VideoPresenter.h`；`include/veyra/diagnostics/GpuTimer.h`；`src/engine/EngineController.cpp` |
| 日志 | `include/veyra/Log.h`、`src/base/Log.cpp` |
| FRUC 依赖 | `scripts/package-portable.ps1`：为 FRUC 添加固定身份的 cudart 依赖白名单，仅修改脚本，未执行打包 |
| 构建/验证 | `CMakeLists.txt`；新增 CaptureColorContract、Yuy2Color、PresentationWorker、LivePresentation 测试；调整原 CaptureSource 测试；`tools/capture_color_probe/main.cpp` |

## 实际验证

证据目录：`logs/baseline-repair-20260909/`。GPU 测试在本机 RTX 5070 实际执行，没有使用实卡。每项通过独立子进程执行，外部 timeout=290 秒，满足单次不超过 300 秒。

构建命令（最终生产二进制对应 `final-build4.log`，exit 0）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root 'C:\Users\123\Desktop\Veyra DLSS Video Player' -Preset x64-release
```

| 实际运行目标/参数 | 结果 | 日志 |
| --- | --- | --- |
| `veyra_capture_color_tests.exe` | 26 项通过：生产接收端、COM allocator、方向、stride、NV12、短样本、动态格式 | `final-capture-contract.log` |
| `veyra_presentation_worker_tests.exe` | 11 项通过：双批次上限、取消、恢复、失败传播、统计重置 | `final-worker-contract.log` |
| `veyra_yuy2_color_tests.exe` | 601/709 × full/limited 四组，最大误差 1/255；显示误差 0 | `final-yuy2-pixels.log` |
| 同上 `--rgb` | full/limited 通道及端点，最大误差 1/255；显示误差 0 | `final-rgb-pixels.log` |
| 同上 `--4k` | 实际 3840×2160，四组最大误差 1/255；显示误差 0 | `final-yuy2-4k.log` |
| 同上 `--nr` | 1080p YUY2 真实 NR 执行、非黑输出、显示误差 0 | `final-yuy2-nr.log` |
| 同上 `--temporal` | 8 帧 NR，NVOF Execute 7 次、NR motion 7 帧，显示误差 0 | `final-yuy2-temporal2.log` |
| `veyra_live_presentation_tests.exe loop/local/fixed_clips/test_av_1080p.mp4 logs/baseline-repair-20260909/final-live-replay2` | 10 项通过：实际 live worker、NR/2X/4X、暂停改设置/恢复、关闭后的统计、退出 | `final-live-replay2.log` |
| 同上测试目标 `logs/video-sdk-trial-20260909/known-pan15.mp4 logs/baseline-repair-20260909/final-live-fruc-pan --fruc` | 15fps 已知平移输入，FRUC 后端的同样 10 项通过 | `final-live-fruc-pan.log` |
| `veyra_preview_geometry_tests.exe logs/baseline-repair-20260909/final-preview` | fit/zoom/pan/reference/reset 像素回归通过 | `final-preview.log` |

NR 是修改画面的处理，NR 模式里的 sourceError 不是基础颜色误差验收值；这些 NR 测试检查实际执行、非黑输出及显示是否再改色，不能把 NR 后误差较大解读成 bypass 出错，也不能据此证明 NR 主观画质优秀。

真实运行日志含 NR `CreateFeature result=0x1 (NVSDK_NGX_Result_Success)`、DLSSG `Evaluate ... result=0x1 ... seh=0`，temporal 的 `nvOFDestroy status=0 executes=7`。CPU 契约测试注入动态格式错误 `0x80040200` 是预期拒绝，最终 failures=0。

失败如实保留：早期编译缺 d3d9 类型/std::abs 头文件已补齐；第一次 temporal 测试漏开测试描述的 `enableNvofStandalone`，执行计数为 0 而失败，按产品配置补齐测试设置后为 7/7。第一次 delivery 的 23 项功能检查全过，但末尾 `Get-FileHash` 因启动环境未加载模块而 exit 1，不能冒充整套 PASS。仅用忽略目录中的 `delivery-env.ps1` 从 `$PSHOME` 导入系统 Utility 模块后重跑，未修改验收脚本或控制 hash。

## 独立复核与边界

独立只读 reviewer `review_native_capture` 已检查接收端、颜色、资源别名、双批次 lease、reset/drain/析构及统计。发现的动态媒体类型、RGB limited 和统计边界已修；最终限定意见：本轮所提问题均关闭，所审范围无其他未修 P0/P1/P2。Reviewer 核对了真实日志但未自行运行 GPU，也未验收实卡。

既有 `AGENTS.md`/`README.md` 相对 CONTROL_HASHES 的漂移保留，本轮没有改控制面或声明 preflight 已通过。完整 Phase 7 不因此改为 complete。尚未完成：新路径在用户设备上的连接兼容性和主观颜色确认、同尺寸/同配置下的性能对照、FRUC 重建成本、间歇节奏问题及长期稳定性。

下一条任务：让用户运行本机新版本确认原生 YUY2 实际画面；不自动恢复已取消的实卡 A/B。若继续做性能优化，先保存相同输入/输出尺寸、NR 档位、FG 后端/倍率的阶段与等待时间，再决定下一项改造，不承诺 GPU 占用的未测降幅。

当前可执行文件：`out/build/x64-release/veyra.exe`。根目录 `Veyra.cmd` 仍指向它。没有更新已发布的 v0.0.1 Release。

## 最终短测汇总

实际命令为 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File logs/baseline-repair-20260909/delivery-env.ps1 -Root 'C:\Users\123\Desktop\Veyra DLSS Video Player'`。包装只导入该 PowerShell 自带的 Utility 模块，然后调用原 `scripts/gates/delivery.ps1`。

最终 exit 0，23 项通过，42.569 秒。结果：`logs/delivery/37bd4fedc5604642a2a2afeeca46c99e/result.json`。1080p 60 次 NR、59 次 NVOF、GPU validation 错误 0、正常路径回读 0；native4K 12 次 NR；4K 输入实时档播放 240 源帧/239 生成帧，观察到绝对 lateness P95=1.50ms（不是物理延迟）；图像原生4K保存、H.264/HEVC 4K 2X CFR 各24输出帧且保留音频、取消导出不晋升完整文件均通过。

最终生产 EXE SHA256：`61618AF18F7B985BD4C1FECA06EBE7A07C30EAA01B2CC0682A217E89EAD3C372`。根目录与 runtime_local 中 NR SHA256 均仍为固定 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`。

### FRUC 额外修复及失败记录

1. 新 live replay 使用 FRUC 时，第一次初始化因本机开发运行目录缺 `NvOFFRUC.dll` 拒绝。旧发布路径改为相对 runtime_local 后，本机没有同步放入该文件。
2. 从已安装 Optical Flow SDK 5.0.7 的 `NvOFFRUC/NvOFFRUCSample/bin/win64` 复制固定文件，先校验 SHA256 `5A0B6701D30709E25E7E5B92CA46B18AAB1459160CECD4F629872369D85C8B0A` 及有效 NVIDIA 签名。随后初始化报 `LoadLibrary error=126`。
3. MSVC `dumpbin /dependents runtime_local/nvidia/NvOFFRUC.dll` 确认直接依赖 `cudart64_110.dll`。同 SDK 目录的这个文件 SHA256 为 `EDC35E7D0FA3F257BBEDFA7888911080C5696ACDD40B6187B6DD0173F20759AD`、签名 Valid/NVIDIA；本地复制到同一 runtime 目录后实际 Create/Register/Process result=0、seh=0。两个文件都被 gitignore，未提交源码。打包白名单同步补齐固定 cudart 项，仅语法检查通过，未执行打包；已发布的旧包没有因此改变，后续发版需带上此依赖。
4. 通用 test_av_1080p 素材下 FRUC 返回 repeated=true，无法满足测试“有效生成大于0”的断言；保留 `final-live-fruc3.log` 失败，不把重复帧改计为生成。换用既有、明确有平移运动的 `known-pan15.mp4`，同一测试所有断言通过。此处是测试素材与生成断言匹配，不是降低断言或隐藏产品错误。

FRUC 三份前置失败日志 `final-live-fruc.log`、`final-live-fruc2.log`、`final-live-fruc3.log` 均保留。最终 `final-live-fruc-pan.log` exit 0，约10秒；仍不代表自然场景总体插帧画质验收。
