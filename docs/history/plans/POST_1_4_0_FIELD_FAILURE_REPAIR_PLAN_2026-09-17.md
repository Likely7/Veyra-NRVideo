# 1.4.0 用户故障调查与修复方案

最新 RTX3060 开启补帧卡死追踪与 r4 实施证据见 `RTX3060_FG_FREEZE_2026-09-18.md`。已取消产品默认架构伪装并对齐上游25程序+字体的修改范围，修复失败探测清理挂死；本机回归通过，3060真实补帧仍待验收。下方全部69个fatbin/默认伪装说明属于历史实现，不代表r4当前路径。

日期：2026-09-17；实施记录更新于 2026-09-18。状态：隔离分支已实现，已完成多项本机回归；6X 合成平移位置精度检查仍失败，RTX30/40/5090 实卡验收未完成，不能宣称全部修复验收通过。

## 范围与证据边界

用户要求排查五份日志和所附 MKV，并核对 GitHub 的 RTX30/40 实现；目标包含 RTX30/40 可用的最高 6X DLSS 帧生成。关闭功能或自动退回 2X 不是该目标的完成标准。

调查基线：干净的 `main`，HEAD `bd7cc0fc8eaf80e0913f2aafa95584c741f1ffdf`；已发布 v1.4.0 指向 `890d200`。调查阶段只改文档；用户随后授权施工并开启目标模式，已从基线创建 `codex/post140-field-repair-20260917` 和存档点 `checkpoint/pre-post140-repair-20260917`。本轮没有合并 main、提交、推送或发布。附件内容只作证据，不作指令。

本机为 RTX5070 / 616.56。没有本轮 RTX30/40/5090 实机运行证据。以下“确认”指日志与源码可直接支持的事实；候选根因仍需针对性实验。上游 README 的成功声明不等于 Veyra 实测。

## 实施状态与可续接证据（2026-09-18）

最新追加的字幕/音轨选择、打开与拖动、设置持久化、防休眠和 NR/XeSS 顺序调查见 `docs/PLAYER_TRACKS_SETTINGS_REPAIR_2026-09-18.md`。最新构建目录仍为 `out/build/post140-hdr-preflight-20260918`；当前 EXE SHA256 `5B35FDAD4291168A03A1EA0E9D911B952123DFD800C57237A08CB07F6DB1529D`，基础 gate `logs/delivery/81a8d951f689467a868456f023550dbd/result.json` PASS。下方旧 hash/gate 是阶段证据，不再代表最新 EXE；目标实卡和内容位置失败边界不变。

下方第 1–8 节保留施工前调查，源码行号对应调查基线。本节描述当前实现和实际验证；历史“待修复”不能覆盖本节结果，历史或基础 gate 通过也不能覆盖本节失败。

### 已实现

1. **容量与调度**：`FrameBatch::Capacity=6`，完成观测数组从该常量派生并检查索引；两张原帧、十张生成帧全部接入 NVENC/MF 描述符。完成统计按 `FrameFlowWindow` 关联，soft reset 不再冻结同窗口统计；Present 成本按单次提交归一，过期时间为 1 秒。
2. **MKV 字幕**：`SubtitleManager` 持有可取消 jthread，元数据先返回，全部内封字幕通过一次 demux 扫描读取。FFmpeg interrupt、请求代次检查和 latest-request mailbox 防止旧源结果覆盖新源；120000 cue / 约 64 MiB 文本预算限制。手动字幕、偏移及用户主动选择保留，自动对齐等待正文加载完成。CLI 双轨选择与异步结果同步已修复。
3. **导出初始化**：使用固定 NVENC **13.0 完整 ABI**，移除仅改 session apiVersion 的伪回退；先查询驱动 max API（编码为 major<<4|minor），各结构体/函数表一致。NVENC 阶段、状态码和 last-error 与 MF 独立错误链传回主进程/UI。只有确证驱动 API 太旧才建议更新驱动。HDR 无 MF 10bit 回退时明确说明。系统 NVENC DLL 不复制入包。
4. **倍率与 worker**：导出不再把请求 6X 静默改为 2X/关闭；present-sink FG 转 DLSS 导出路径有明确日志。主日志保留 jobId、PID、绝对 worker 日志路径和最终源帧/生成帧/hold/编码数；失败消息附 worker 路径。
5. **RTX40/30 初始化**：移植固定 MIT 上游 discovery、startup 和 Create gate 逻辑，建立独占 `FgCompatibilitySession`。程序/PTX 和 NVAPI 兼容在 NGX Init 前安装；绑定实际 device/LUID、provider identity、两张原帧与十张生成帧。Ada 保留原生 Available 判断；Ampere 有完整 SM86 程序路径和限定 startup/Create 门控。能力 5 只表示可尝试，实际 Create/Evaluate 必须成功。
6. **兼容生命周期**：patch 发布与还原检查页保护、字节一致性和指令缓存刷新；失败记录不会被抹掉。NGX Shutdown 后才还原补丁与释放资源；还原无法证明时进程标为不健康，拒绝后续 NGX，保留相关模块/资源到进程退出。测试失败注入通过。已确证恢复原指针的 patch 存储可释放，不能一概声称所有内部存储都永久保留。
7. **卡死隔离**：产品在兼容初始化前运行同 LUID/尺寸/倍率/HDR/provider 的子进程预检，真实执行三帧 Create/Evaluate。60 秒有界等待，可取消；受限句柄继承、先挂 job 再恢复线程、job 关闭杀进程。缓存只记录成功且包含驱动/provider hash。预检不能保证父进程后续调用永不挂死，不以其成功代替实卡画质检查。
8. **FG 参数与诊断**：零向量不再被指定为 invalid motion，使用不可用的 float 极大值作为哨兵。静态相机补齐正交方向基以及互逆的透视投影矩阵，与 near/far/FOV 一致；该修正未改变下述合成位置偏差，不能算作偏差已修复。直接 NGX `--fg-planar6` 诊断与图内检查都保留失败结果。
9. **HDR 兼容预检修正**：追加实际导出发现预检只设 `hdrOutput`、仍使用 SDR RGB 输入，必然触发 `HDR output requires an explicit HDR input contract`。现在 HDR 预检同时设置 `hdrInput`，使用显式 limited-range P010/PQ/BT.2020-NCL/BT.2020 测试帧，再进入 RGB10 FG。SDR 预检保留 RGBA 路径。修复前 `post140-ampere6-hdr-final` exit 1，子进程 PID35616/stage2/generated0；修复后强制 Ampere/Ada 的完整 HDR 6X 导出均通过。

源码范围：`src/engine/{EngineController,ExportJobManager,VideoExportJob,Subtitles,FgCompatibilityProbe}.cpp`，`src/ngx/{DlssFgBackend,AdaMfgUnlock,AmpereMfgUnlock,NvapiArchSpoof,FgCompatibilitySession}.cpp` 与 `src/ngx/compat/`，`src/pipeline/EnhanceGraph.cpp`，三个编码器文件，`D3D12DeviceContext`，对应 include，AppShell/main，CMake，单元/集成测试与 `tools/fg_harness/`。新增适配源码的来源、固定提交与许可证见 `THIRD_PARTY_NOTICES.md`，不是从 NVIDIA SDK 拷贝实现。

### 本机证据

所有下列 `post140-*` 日志前缀均在 `out/logs/`，标准输出/错误分别为 `.stdout.log` / `.stderr.log`。测试使用 `scripts/run-short-test.ps1`，单项 timeout 不超过 300 秒。强制 Ada/Ampere 只在独立测试进程通过环境变量开启，**不代表 RTX30/40 实卡**。

| 检查 | 结果与日志 |
| --- | --- |
| 构建 | `post140-build-publication.log`、`post140-build-camera.log`、`post140-build-direct-planar.log` 均 exit 0；目录 `out/build/post140-field-repair-20260917` |
| 容量/soft-reset/Present 过期 | `post140-contract`、`post140-scheduler` PASS |
| 32 轨大 MKV | `post140-subtitle-dual-final` PASS：32 usable tracks、15720 cues，primary=3/secondary=5/offset=700ms 保留，扫描 2576.748ms，播放 425 帧，failed=false；较早冷缓存扫描约 9.4–10.3s，不能把热缓存值当固定耗时 |
| 字幕取消/源切换 | `post140-subtitle-cancel-final` PASS；首次 MKV 测试 first decode 44ms / first valid 120ms，未等完整字幕扫描 |
| 12 个编码输入槽 | `post140-pool-{h264,hevc,mf-h264,mf-hevc}` PASS；实际逐槽写不同灰度、编码、解码比对，D3D12 debug 检查 |
| NVENC 错误分流 | `post140-nvenc-invalid-version`（注入 status 15）、`post140-nvenc-unsupported`（注入非版本错误）PASS，MF SDR 回退可编码；不是模拟降低版本后当真机通过 |
| 2–6X 反复创建/reset/分辨率变化/退出 | 原生 `post140-native-lifecycle`；强制兼容 `post140-ada-publication-lifecycle`、`post140-ampere-publication-lifecycle` PASS；倍率序列 6,2,5,3,4,6、尺寸 640×360/368 |
| 回滚失败 | `post140-rollback-publication-final` PASS：强制 Ampere + rollback-fail，失败后保留必要资源且拒绝 reinit |
| 子进程卡住 | `post140-probe-timeout` PASS：测试专用 2s timeout，产品为 60s；`post140-worker-probe-reject`、`post140-worker-probe-cancel` PASS |
| 6X SDR 完整导出 | `post140-ampere6-cow` H264、`post140-ada6-final-hevc` HEVC PASS（均为 5070 强制路径）；12 source + 55 generated + 5 明示 CFR holds = 72 encoded，解码 72 帧，1920×1080/360fps/.2s，AAC mono/.213333s |
| 6X HDR 完整导出 | `post140-hdr6-hevc` PASS（5070 原生）：HEVC Main10/yuv420p10le/BT2020nc/PQ/BT2020，1920×1080/180fps/72 decoded/.4s，AAC 6ch 5.1；5 个 CFR holds 不计作插值 |
| 6X HDR 兼容预检与导出 | `post140-ampere6-hdr-repaired`、`post140-ada6-hdr-repaired` PASS（5070 强制路径）：预检 PID37608/34920 均 exit0/stage4/generated10；随后各 12 source + 55 generated + 5 holds = 72 encoded。`ffprobe -count_frames` 实际解码各 72 帧，Main10/PQ/BT2020/1920×1080/180fps/.4s，AAC 6ch 5.1/.405333s；兼容补丁恢复成功 |
| worker 暂停/继续/取消 | `post140-worker6` PASS：120 source + 595 generated + 5 holds = 720 encoded，前台继续播放；`post140-worker-cancel-final` PASS |
| 初始化失败实际消息链 | `post140-worker-error-tagged` PASS，PID40072：`OpenD3D12Session status=15` + `HDR 10bit 不支持此系统编码路径（仅 8bit NV12）`，source/generated/encoded 均为 0，没有输出文件，主日志有完整 worker 路径。最初 `post140-worker-error-final` FAIL 是测试误用了未标记 HDR 的素材，MF 正常成功；保留该失败，不当作产品失败 |
| 基础 delivery | 最终产品代码 `logs/delivery/2c9372ba16774e3d9f7529825f623fa3/result.json` PASS，62.0575379s；EXE SHA256 `DD117A0F3D484E8DAF6101B3E877A04B4884F1CE733E26A0D32415993330FACB` |
| 2X 正弦纹理对照 | `post140-native2-planar-control` PASS，11/11 生成帧满足原定位置门槛 |
| 6X 非周期纹理对照 | `post140-native6-planar-detail`、`post140-ada6-planar-detail`、`post140-ampere6-planar-detail`、`post140-native6-planar-detail-nvof` 全部 PASS，分别 55/55；前三项精确 motion，最后一项产品 NVOF。仍是 5070 本机，不是目标实卡 |

实际构建命令：

```powershell
& cmd.exe /c 'out\build\veyra-build-x64-release.cmd' *> out/logs/post140-build-direct-planar.log
& scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/post140-field-repair-20260917
& scripts/run-short-test.ps1 -Exe out/build/post140-field-repair-20260917/veyra_export_worker_failure_tests.exe -Arguments @('encoder','out/hdr-audio-fixtures/pq-tagged-51.mp4','out/logs/post140-worker-error-tagged.mp4') -TimeoutSeconds 40 -LogPrefix out/logs/post140-worker-error-tagged
& scripts/run-short-test.ps1 -Exe out/build/post140-field-repair-20260917/veyra_repair_fg_tests.exe -Arguments @('mfg6-exact-rgb-planar') -TimeoutSeconds 90 -LogPrefix out/logs/post140-native6-camera-planar
& scripts/run-short-test.ps1 -Exe out/build/post140-field-repair-20260917/veyra_fg_harness.exe -Arguments @('--fg-planar6') -TimeoutSeconds 60 -LogPrefix out/logs/post140-native6-direct-planar
```

回滚测试实际命令在设置 `VEYRA_TEST_FORCE_AMPERE_UNLOCK=1` 的独立 shell 中运行 `run-short-test.ps1 -Exe out/build/post140-field-repair-20260917/veyra_fg_lifecycle_tests.exe -Arguments @('rollback-fail') -TimeoutSeconds 60 -LogPrefix out/logs/post140-rollback-publication-final`，finally 删除该环境变量。普通 RTX50 无强制变量时不安装兼容补丁。

### 未通过项，不作完成声明

- **6X 位置精度 FAIL**：精确 16px/源帧平移的合成纹理，目标位移 `[2.667,5.333,8,10.667,13.333]`，测得 `[2.25,7.75,8,11.25,12.25]`。55 张生成帧彼此不同、不同于两端原帧、不是线性混合，API/禁用标记正常，但只有 33/55 满足 ±1px 原定门槛。普通方块 39/55、纹理方块 45/55 也保留原结果。
- 原生 5070、强制 Ada/Ampere 都复现；RGB 直入、精确运动向量、NVOF/零/反向 motion、诊断串行等待、FrameID 类型对照和相机矩阵对照没有解除偏差。串行等待/指针 FrameID 的临时产品开关已移除。
- **直接 NGX 隔离也 FAIL**：`post140-native6-direct-planar` 记录 `directNGX=true generated=55 distinct=55 contentValid=33 graphUsed=false`，Create/Evaluate/Release/Shutdown 均 success。这排除了该实验中的播放器调度、图帧池、颜色转换与 NVOF；仍不能区分 `DlssFgBackend` 参数/资源契约与运行库内部插值行为，也不能声称 NVIDIA 模型就是根因。没有放宽精度阈值，没有用 2X 或重复帧冒充 6X。
- **追加对照限制了上述结论的范围**：非周期双线性格点纹理保持 16px/帧平移、同一 ±1px 门槛和 PTS/真实插值/lease 检查，原生、强制 Ada/Ampere、产品 NVOF 均 55/55 通过。代表位移 `[3.25,5.5,8,10.5,12.75]`。因此存在可通过的真实 6X 内容路径，不能把正弦图案的失败概括为所有 6X 都时序错误；同样不能用新通过项覆盖旧正弦/方块失败。当前结论是内容相关的多帧位置误差，具体机理仍未证实。追加仅改测试 fixture，构建日志 `post140-build-detail-fixture.log` exit 0，未改产品 EXE。
- 30/40/5090 真机未执行。子进程预检/生命周期和 5070 强制路径只能证明本地接线及部分内存安全；不能证明 SM86/SM89 在目标硬件的程序执行、VRAM、速度或画质。
- 用户导出 worker 原始文件仍缺失，无法倒推其首错。现在已修 ABI 和诊断缺陷，不能据此称该用户机器实测修好。`PresentSink.cpp` 已有设备失败后的 DRED breadcrumbs/page fault 查询；尚未确认强制采集启用及受影响机器的有效证据。字幕烧录导出不是本轮新增能力，不声明已通过。

**下一项唯一任务**：以直接 NGX 的 6X 合成复现继续核对资源/输入契约及 upstream 行为，保留原生 5070 基线与所有失败证据；随后取得 RTX30/40/5090 实卡复测和用户导出 worker 首错。没有这些证据前不发布“30/40 全型号 6X 已验证”结论。

追加对照命令：`scripts/run-short-test.ps1 -Exe out/build/post140-field-repair-20260917/veyra_repair_fg_tests.exe -Arguments @('mfg6-exact-rgb-planar-detail') -TimeoutSeconds 90 -LogPrefix out/logs/post140-native6-planar-detail`。强制兼容分别在单独 shell 设置 `VEYRA_TEST_FORCE_ADA_UNLOCK=1` / `VEYRA_TEST_FORCE_AMPERE_UNLOCK=1`，使用对应日志前缀，finally 删除变量。NVOF 对照去掉模式中的 `-exact`，其日志记录 `nvOFDestroy status=0 executes=11`。原有正弦 fixture 未更改，仍可独立复现失败。变更/新增文件二进制扩展名及本地依赖目录审计 PASS；已跟踪/未跟踪文件空白检查 PASS。

### HDR 预检追加构建（2026-09-18）

旧目录的 `veyra.exe` 被正在运行的 PID9468 占用，构建日志 `out/logs/post140-build-probe-hdr.log` 记录 LNK1104。保留该窗口，改为独立构建目录 `out/build/post140-hdr-preflight-20260918`。完整构建 exit0，日志 `out/logs/post140-build-probe-hdr-isolated.log`；新 EXE SHA256 `880A3D585C3C01E574F5840C86AFF53F5B4D4F271D8264656057EF2BABED47CC`。旧目录 EXE 不包含本次 HDR 预检修正。新目录执行 `scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/post140-hdr-preflight-20260918` 再次 PASS，57.483224 秒，报告 `logs/delivery/ff9bea6b541d4934a52d0f6c0826f5bc/result.json`。最新完整基础 gate 以此为准，仍不覆盖目标实卡或未通过的内容位置检查。

```powershell
& scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/build/post140-hdr-preflight-20260918 -FfmpegRoot C:/veyra-deps/ffmpeg-ps5-dav1d-installed -RemotePlay -ChiakiCheckout C:/veyra-deps/chiaki-source -ChiakiStage 'C:/Users/123/Desktop/Veyra DLSS Video Player/out/remoteplay/chiaki-msvc-stage' -RemotePlayPrefixPath C:/veyra-deps/remoteplay-installed/x64-windows-static -ProtocPath C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe -PkgConfigPath C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe
$env:VEYRA_TEST_FORCE_AMPERE_UNLOCK='1'
try {
    & scripts/run-short-test.ps1 -Exe out/build/post140-hdr-preflight-20260918/veyra.exe -Arguments @('out/hdr-audio-fixtures/pq-tagged-51.mp4','--export-out','out/logs/post140-ampere6-hdr-repaired.mp4','--hevc','--max-frames','12','--fg-multiplier','6','--fg-dlss','--no-nr','--no-sr') -TimeoutSeconds 90 -LogPrefix out/logs/post140-ampere6-hdr-repaired
} finally { Remove-Item Env:VEYRA_TEST_FORCE_AMPERE_UNLOCK -ErrorAction SilentlyContinue }
```

Ada 用独立 shell 设置 `VEYRA_TEST_FORCE_ADA_UNLOCK=1` 并替换输出/日志前缀为 `post140-ada6-hdr-repaired`，finally 清理。两项输出都用 `ffprobe -v error -count_frames -show_entries stream=codec_type,codec_name,profile,width,height,pix_fmt,color_space,color_transfer,color_primaries,avg_frame_rate,duration,nb_read_frames,channels,channel_layout -of json <output>` 核对。仍未执行 RTX30/40 实卡，新增通过项不覆盖上述内容位置失败。

追加审计：已跟踪与新增文件的 `diff --check` 通过；修改列表没有 DLL/SDK/模型/媒体。再次计算 DLSSG/NR SHA256 与前述原件一致。没有合并、提交、推送或发布。

### 播放与设置切换追加验证（2026-09-18）

所附 32 轨 MKV 完整播放短测：`post140-ampere6-mkv-preview`、`post140-ada6-mkv-preview` 均 exit0，分别 170 source / 840 generated、196 source / 970 generated，failed=false；没有 backend-recovery 降档。Ada 终态 `generatedPresented=965 generatedExpiredAfterEval=0 presentSubmitFps=144`。这是提交统计，不是屏幕实测刷新率。Ampere 本次全字幕扫描 10790.259ms，播放未等全扫。命令为 `run-short-test.ps1 -Exe out/build/post140-hdr-preflight-20260918/veyra.exe -Arguments @(<所附 MKV 绝对路径>,'--smoke-seconds','12','--fg-multiplier','6','--fg-dlss','--no-nr','--no-sr') -TimeoutSeconds 90`，分别在独立 shell 设置对应强制兼容变量并 finally 清理。

新增 `tests/integration/FgSettingsTests.cpp` 检查完整播放器设置工作流，发现关闭补帧启动后再开启，底层已生成帧但能力 snapshot 仍为 0。修复前日志 `post140-ampere-settings` FAIL：`desired=2 applied=2 frames=1571 generated=1558 cap=0 failed=0`。原因是 `EngineController.cpp` 仅在首次打开时发布 capability。已提取局部 `publishFrameGenerationCapabilities`，在首次打开、设置重建/恢复旧图、运行错误恢复和处理失败回滚后更新真实 provider 能力，删除过时的 Ada=2X 注释。

构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd`，`post140-build-settings-repaired.log`、`post140-build-settings-final.log` exit0。最终测试要求每次切换后至少新增 12 张源帧，补帧开启时新增至少 10 张生成帧，防止旧累计计数造成假通过。`post140-native-settings-final`、`post140-ada-settings-final`、`post140-ampere-settings-final` 全部 exit0：同进程 1→2→6→1→6、两次暂停 seek 后 6X 恢复、最终排空停止。每次 6X 的 requested/applied 都为 6，能力为 5 张生成帧。真实子进程预检 Create/Evaluate 成功，仍是 RTX5070 上的强制兼容测试。

```powershell
& scripts/run-short-test.ps1 -Exe out/build/post140-hdr-preflight-20260918/veyra_fg_settings_tests.exe -Arguments @('C:/Users/123/Desktop/Money.Heist.2019.S03E02.V2.1080p.NF.WEB-DL.H264.DDP5.1-LeagueNF.mkv','out/logs/post140-native-settings-final') -TimeoutSeconds 240 -LogPrefix out/logs/post140-native-settings-final
```

Ada/Ampere 分别设置 `VEYRA_TEST_FORCE_ADA_UNLOCK=1` / `VEYRA_TEST_FORCE_AMPERE_UNLOCK=1`，替换对应输出/日志前缀，finally 删除变量。最新 EXE 仍在 `out/build/post140-hdr-preflight-20260918/veyra.exe`，SHA256 已更新为 `231ECD897E19B7A45295EA3AE4694BD5717BA03113F33DD4E81E69CEBC84909A`，上节 HDR 修复时的 hash 是历史版本。该切换测试不替代内容位置检查或目标显卡验收。

最终基础交付检查：`scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/post140-hdr-preflight-20260918` PASS，61.7707815 秒；报告 `logs/delivery/f0de8f4ef17a46418be3ce8733ebca54/result.json`，EXE hash 与本节一致。此报告取代上节作为最新基础 gate，未通过项仍以“未通过项，不作完成声明”为准。已跟踪/新增文件空白检查与源码/二进制隔离审计通过。首轮审计脚本把 `git diff --no-index --check` 的差异退出码 1 误判为失败；重新同时检查诊断输出和文件名单后通过，无文件内容修正。

当前本地没有新的已证实根因可继续定点修复。下一项是让受影响的 RTX30/40/5090 使用上述构建复测并取得完整主日志、对应 `logs/export-worker-*.log` 和 `logs/fg-probe-*.log`；这将区分目标硬件的 Create/Evaluate、调度与编码首错。内容位置偏差保留为未解决问题，不以重复本机成功测试覆盖，也不在目标卡无证据时宣称 6X 全型号可用。未提交、合并、推送或发布。

## 1. 先修：6X 路径存在确定的数组越界

- `include/veyra/pipeline/FrameBatch.h:37` 已有 6 个 `BatchFrame`。
- `src/engine/EngineController.cpp:466` 的 `CompletionWatch::frameReadyObserved` 仍只有 4 项。
- 同文件 `530-531` 按 `batch.batch.count` 遍历；`1137` 按 `s.next` 访问。5X/6X 批次会访问数组外内存。已用 `git show 890d200:src/engine/EngineController.cpp` 确认发布版同样存在。

这是内存安全缺陷，必须在继续解锁实验前修复。但不能倒推它导致了所有附件中的 device lost：4060 的 DLSS 实际退回 2X；30 系另一处设备丢失发生时 FG 已关闭。

修复：统一批次容量定义，让观测数组由同一容量派生；审计所有帧数、描述符、fence、生成帧索引和队列上限，区分“6 个输出”和“5 个生成帧”。对批次计数建立边界检查。添加覆盖 2-6X、取消、部分完成、reset 的容量测试，使用可用的 ASan/边界检查验证。不要仅把这一处 4 改成 6 就宣称整条链已安全。

## 2. RTX5090：调度长期拒绝补帧，呈现统计被软重置冻结

### 直接证据

`5090显卡补帧受限，显卡占用仅为百分之30.log:43903`，revision=12：

```text
sourceAccepted=7430 realSubmitted=7430
fgCandidate=22290 fgSkippedBeforeEval=22287 fgEvaluated=3 fgWarmup=3
fgReadyValid=0 generatedPresented=0 presentSubmitFps=30
epoch=87 resetEpoch=80
```

同段持续约四分钟。末次 admission：`remainingDeadlineMs=22.359 predictedMs=25.396 elapsedMs=1.285 presentP95Ms=27.265 admitted=false`。`displaySubmits=3` 长期冻结，但 `presentationCompletedReal` 继续增加；另一路 Present CPU P95 为 3.159ms。

### 源码对应与判断

`EngineController.cpp:929-932` 只在 hard reset 时推进 `metricsWindowEpoch`；Drop/预览跳帧是 soft reset，图的历史 epoch 仍改变。`1035-1045` 仅在统计窗口重开时更新 `liveStats.identity`。`1204/1214` 却要求每个完成批次的图 epoch 与该旧 identity 相等，之后的新批次统计因此被丢弃。

`945-960` 的准入继续读取 `livePresent.p95()`。`TimingWindow` 只有样本数量上限，没有墙钟过期；FG 预算自身 GPU 样本的过期无法使这个外部 Present 成本过期。由此形成“旧高耗时一直保留、没有新统计纠正、继续拒绝 FG”的反馈问题。这是与日志高度吻合的代码缺陷，尚未通过修复前后实机对照闭环。

修复：统计会话/设置身份与时序历史 epoch 分开管理；拒绝旧会话完成事件，同时接纳同一统计窗口内新的 soft-reset epoch。Present 样本增加时间戳与有效期。拆分冷启动、实际 Present CPU 工作、节拍等待和 ready 等待；`1151` 的 `s.presentMs` 是整批累计值，必须核对预算按批还是按单次使用，不能混用。保留有界、受 deadline 约束的恢复探测。

验收：构造一次 Drop 后继续呈现，验证统计继续更新；注入暂时高 Present 成本后解除，验证 FG 在有限时间恢复；再测真实欠速时仍能守住音频一倍速及预览延迟。比较生成有效帧、实际提交节奏、过期帧和 CPU/GPU 阶段耗时。文件名中的“30% 占用”没有对应硬件监测数据，不能据此断定 GPU 算力或驱动限制。

## 3. 大 MKV 打开卡住：字幕逐轨整文件扫描阻塞 UI

`src/engine/Subtitles.cpp:391-469` 对每条字幕轨循环 `av_read_frame`，直到文件末尾，再 seek 到开头扫下一条。`apps/veyra/ui/AppShell.cpp:125-140,292` 在 `engine.open()` 之前同步执行加载。

合并日志中，HEVC 文件字幕轨 2-39 的首末完成记录相隔 85.110 秒，随后 D3D11VA 解码成功；Money Heist 的轨 18-49 相隔 64.066 秒。这些是首末字幕完成记录间隔，尚未包含第一轨扫描之前的等待，不能当作完整打开耗时。

所附 4,272,359,225 字节 MKV 实为 H.264 1080p24、17 条 E-AC-3 音轨、32 条 SubRip 字幕，时长 2542.88 秒。系统 FFmpeg 8.1.1 读取前 5 秒视频 exit=0、0.974 秒；单次整文件 demux 提取首条字幕 exit=0、1.998 秒。这说明样本可读，也支持重复扫描造成大量额外工作；这些不是 Veyra 或随包 patched FFmpeg 的硬解测试。

修复：元数据枚举与字幕正文加载分离；播放器启动不等待整文件字幕扫描。可取消 worker 用一次 demux 分发主/副轨，或在明确内存上限内缓存所有文本轨。使用源 generation 标识，切源后的旧结果不得写回；长读操作设 interrupt callback，seek/切轨/关闭可取消。主副字幕、时间偏移、ASS 样式及导出共享一致的轨道快照，禁止后台线程直接操作 UI 或播放 demuxer。

验收：此文件的打开耗时不再随 32 轨重复全扫增长；UI 可响应；读取次数证明确为单次；切源/关闭期间取消安全；主副轨、拖动、偏移与导出字幕一致。首次画面和字幕就绪时间分开记录。

## 4. RTX40：补丁命中不等于 NGX 已发布 6X 能力

合并日志同时含 RTX4070 Laptop 和 RTX4060 Laptop 会话，不能全部归于 4060。4060 段记录：Ada 架构门控 2 项、count/index gate 1 项、descriptor slots 8 项、kernel 1 项，`identityVerified=1 applied=1`；之后仍是 `FG.Available=true MultiFrameCountMax=1`。请求 6X 被应用层自动改成 2X。2X 段确有 real/generated 各 124 帧的记录。

`src/pipeline/EnhanceGraph.cpp:865` 先 `coreHost_->initialize()`，`877/880` 才调用 Ada/Ampere 补丁，再查询 capability。初始化阶段已缓存旧能力、补丁目标与实际活跃 provider 不一致，都是必须验证的候选；目前没有证据足以从中选定唯一根因。现有“不是已审计运行库”的警告不能由 `max=1` 单独推出，尤其同段已经记录 `identityVerified=1`。

修复实验：

1. 记录核心/provider 的绝对路径、SHA256、模块基址、导出函数归属、load generation、适配器 LUID，以及补丁前/后和初始化前/后的能力及原始返回码。
2. 对照上游的 early-provider 生命周期，在确认模块依赖与加载顺序后，将需要提前生效的处理移到正确阶段。不要只机械地移动两行调用。
3. 单独验证 NGX requirements/capability 与 provider count/index/kernel 门控。上游明确说明 count/index gate 不是 NGX 的共享 feature-support 查询。
4. 已验证 provider/适配器/资源容量之后，按上游契约提供受限启动能力处理；真实 Create/Evaluate 仍必须成功。禁止全局写 `Available=true/max=5` 后就报通过。
5. 在真实 Ada 上分别测 2X/3X/4X/5X/6X 和重复初始化。UI 同时呈现 requested/applied/reason；2X 回退保留为运行失败兜底，不计入 6X 完成。

## 5. RTX30：缺失完整启动兼容路径，现有拒绝不是“不可能”的证明

两份日志涉及 RTX3070 Ti 桌面及 Laptop。架构 spoof、fatbin 重建和预检命中后仍有：

```text
FG.Available=false
FeatureInitResult=18446744072548777995
MultiFrameCountMax Get=0xFFFFFFFFBAD00010
```

第一个数低 32 位是 `0xBAD0000B`，即 `UnableToInitializeFeature`；第二个是 `UnsupportedParameter`，不是“查到最大帧数为 0”的成功查询。失败在这些会话的 Create 之前。Laptop 日志 `HwSchMode=-1` 表示未得到该注册表值，不能直接解释成 HAGS 关闭。

Veyra 主要移植了架构门控、GPU 程序重建与 spoof；当前本地上游 `ampere_backend.cpp:1555,1604,1643,2541` 还包括 requirements、启动 capability bootstrap、真实适配器/provider/load generation 校验，以及 Create 时 caller/缓冲分配验证。它并非简单取消一个 `Available` 检查。该上游路径包含游戏 wrapper/swapchain 前置条件，Veyra 必须映射到自己的直接 NGX host 和输出资源，不能生搬 wrapper 的判断。

实施方向：基于有源码、可归因的上游，把 provider 生命周期、requirements/启动能力、sm_86 程序、创建参数和输出缓冲这几部分作为一条完整路径接入。保留直接 NGX，不加入 Streamline/ReShade。先用独立子进程探针定位失败阶段，设置超时，避免未知 Create 卡住播放器；实际产品创建/恢复策略依据探针结果制定。先建立真实 2X 的 Create/Evaluate 闭环，再逐项验证 3-6X；这只是定位次序，交付目标仍是最高 6X。

过去“5070 强制 Ampere 路径通过”只能证明该路径在 5070 上运行，不证明 GA10x CUDA 程序、驱动和资源在 30 系可用。也不能从一次 3060 Create 失败推广到整个 30 系永远不可用。

## 6. GitHub 核对（固定来源）

| 项目 | 核对提交 | 当前结论 |
| --- | --- | --- |
| [dashdogy/RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock/tree/b77e6e55c9211faf58932fc67eaaad9feb9df934) | `b77e6e55c9211faf58932fc67eaaad9feb9df934`，v1.3.3 Hotfix 1 | MIT；40 系最高 6X；30 系标为 very early experimental / DX12。可用源码作为主要移植来源，但不能宣传所有机器已验证。 |
| [sdli1995/dlssg_for_sm86](https://github.com/sdli1995/dlssg_for_sm86/tree/126d0f768f2293dad6f0da9d3b8fd6961ed88bdc) | `126d0f768f2293dad6f0da9d3b8fd6961ed88bdc`，v0.3.2 | README 声称在 3080Ti/3070 开发、310.9 支持 6X；当前树主要为二进制与文档，未见可移植实现源码或 LICENSE.md；README 声称项目源码 GPLv3，不能按旧说明当成 MIT。 |
| [ImDreamt/MFGAdaUnlock-RenoDx](https://github.com/ImDreamt/MFGAdaUnlock-RenoDx/tree/a8aa0d9289471facc1e32013d17fd9c1195bc2cd) | `a8aa0d9289471facc1e32013d17fd9c1195bc2cd` | MIT，现有 notices 已归因；仅采用许可源码，不加载其 RenoDX add-on。 |

本地 RTX40MFG-Unlock clone 固定于 `33b41835dc39c5d8ab1ef93efb2449be31139c09`，比核对 HEAD 少一个 hotfix commit。compare 和 README 显示该次更新主要涉及启动/菜单/诊断，provider policy/GPU payload 没有因此更换；“更新上游就好”没有证据。

上游 [issue 38](https://github.com/dashdogy/RTX40MFG-Unlock/issues/38) 仍有 4070 3X/4X 卡住、2X 正常，也有 4080 3X 成功反馈，说明存在配置差异。[issue 28](https://github.com/dashdogy/RTX40MFG-Unlock/issues/28) 的 4060 Laptop 游戏问题不能直接作为 Veyra 同根因证明。

`dlssg_for_sm86` 新版已转向 proxy 路径，旧原生 host 的兼容性结论需要重查。它是 30 系多帧可行性的上游证据，但当前不能直接复制缺失的源码；优先移植上表 MIT 实现，后续若拿到该项目源码/明确接口再比较。不得为追版本擅自提取或替换 NVIDIA 二进制；新 runtime 的本地/分发授权分别遵守 AGENTS。

## 7. 导出：错误被分流到未提供的 worker 日志

`导出问题.log` 仅记录启动 PID `43964/44892/44228/46492/38640`。`src/engine/ExportJobManager.cpp:76` 将实际执行写入独立 `logs/export-worker-<PID>.log`；`poll()` 读取退出码和共享错误消息，但没有把终态持久化回主日志。附件和本地已查位置没有这些对应 worker 日志，因此尚不能判断 NVENC、CFR、复用器或源解码哪个阶段失败。

主日志在 13:16:27 有 `Present FAILED hr=0x887A0005 removedReason=0x887A0005`，之后 NGX 出现 `BAD00002`（PlatformError）。能确认 device lost 及后续失败，不能从主进程 Present 错误确定独立导出进程的首个失败。30 Laptop 另一处 device lost 前已有数百帧 `nr=0/fg=0/nvof=0`，不能归咎正在运行的 MFG。

先补诊断：启动记录 jobId、PID、worker 绝对日志路径；结束回写退出码、最后阶段、原始错误码与错误文本；诊断导出聚合 worker 文件。补齐 DRED breadcrumbs/page fault、各队列错误与首个 device removal 记录，在丢失设备后按现有会话边界重建，禁止继续用失效资源循环 Create。DRED 是后续取证方案，不是已经得到的崩溃原因。

收到对应 worker 日志后再针对首错修复；现有 NVENC 回退/CFR 补帧代码不等于本次已定位。预览使用 present-sink FG 时，导出后端的替代/降档也必须明确记录，不能将成功输出 2X 当作请求 6X 已完成。

### 截图追加取证：泛化提示与 NVENC 回退缺陷

用户随后提供截图 `C:/Users/123/AppData/Local/Temp/codex-clipboard-4a7bb8d7-aa77-442b-a59e-51dd0d4e223e.png`，显示“NVIDIA NVENC 与系统硬件编码器都无法初始化；请更新显卡驱动后重试；未生成输出文件”，进度 0%、源帧 0 / 编码 0；用户反馈驱动已经最新。是否最新尚未独立核实，既有日志记录的是 RTX5070 Laptop / 32.0.16.1664。

`src/sink/VideoEncoderFactory.cpp:47` 证实这句提示仅由 NVENC/MF 两条 `open()` 均失败触发，没有任何驱动过旧判断。失败可来自 codec/尺寸/HDR/资源/会话/着色器等阶段；`MfVideoEncoder.cpp:198` 在 HDR 下直接拒绝，所以这条提示甚至不能证明系统硬件编码器实际尝试过激活。截图定位到正式编码开始前的初始化失败，不能再把本次截图对应失败归于末帧/CFR 收尾。

进一步确认当前版本回退实现的问题（`src/sink/NvencD3D12Encoder.cpp:45-79`）：

- CMake 引用的本地头文件为 API 13.1。真实首开失败后，循环用 `major==NVENCAPI_MAJOR_VERSION` 跳过同主版本候选，导致 13.1 失败时连 13.0 也跳过，只试 12.0/11.0；它没有比较完整 major/minor。尚未独立确定用户发布包编译时头文件的 minor 版本，不能据此宣称这就是该用户首错。
- `VEYRA_TEST_NVENC_FIRST_OPEN_FAILS=1` 使上述跳过条件失效。前次测试因此会尝试 13.0，无法证明真实失败路径的候选序列正确。PowerShell 对现有分支的枚举结果为：真实路径 `12.0,11.0`，测试路径 `13.0,13.0,12.0,11.0`；这只是控制流验证，未调用 NVENC。
- 只改 `open.apiVersion`，函数表、open 及后续配置等结构体仍用编译期版本；这些版本宏包含 API major/minor，部分结构体布局也有演进。因此尚未实现有依据的跨版本 ABI 兼容。前置 `CreateInstance` 若失败，会在回退之前直接返回。
- 当前没有调用 `NvEncodeAPIGetMaxSupportedVersion`，也没有按失败状态筛选版本回退；任何首开失败都会尝试降低版本，无法正确区分版本不兼容与其他错误。

把编码器初始化修复加入 P5：先输出实际加载 DLL 路径/身份、驱动支持 API、应用 API、每阶段状态和 `nvEncGetLastErrorString`；保留 NVENC/MF 两条独立错误链，并传给 UI/主日志。查询 max-supported 时按头文件约定解析 major/minor（与 `NVENCAPI_VERSION` 的编码不同）。兼容层采用已验证的结构体/接口版本组合或兼容基线，不盲目改结构体版本位；测试必须覆盖真实返回 `INVALID_VERSION` 的分支及非版本失败。提示只在证据明确支持时建议升级驱动，HDR 无 MF 回退时明确说明。用户机器首错仍待对应 worker 日志，不能宣称上述实现缺陷已被证明是这台机器的根因。

## 8. 施工顺序与验收

| 顺序 | 最小闭环 | 完成证据 |
| --- | --- | --- |
| P0 | 统一 6X 容量并修越界 | 容量/取消/reset 测试，无越界；真实输出后再测生命周期 |
| P1 | 软重置统计与 FG 准入恢复 | Drop 后统计继续更新；临时高成本消退后恢复；欠速时不拖慢声音 |
| P2 | MKV 字幕元数据/正文分离，异步单遍读取 | 所附 32 字幕样本首帧不等全扫；取消/双字幕/seek/导出正确 |
| P3 | RTX40 provider/能力初始化闭环 | 真实 Ada 2-6X Create/Evaluate 和输出证据，requested=applied |
| P4 | RTX30 完整兼容路径 | 真实 GA10x 2-6X，逐型号/驱动记录；不以 50 系代验 |
| P5 | 导出 worker 首错取证及定点修复 | 指定失败样本重现后修复，音轨/PTS/末帧/调色/字幕/倍率验证 |

施工前计划：导出日志聚合可以在前面阶段先做，不必等到 P5 才开始收集；当时下一项任务是 P0。当前实现、失败与下一步以文首实施状态为准。

统一要求：单次测试不超过 300 秒；按变更范围构建与运行针对性测试，交付前执行适用的 `scripts/gates/delivery.ps1`。RTX50 路径不安装 Ada/Ampere 补丁，并对 NR/SR、原生 FG、切换及退出做负向回归。若需要分次长时稳定性测试，分别记录，不能伪装一次连续长测。

6X 成功必须在运动素材上证明每个有效源间隔有 5 个有效、时间位置正确的生成帧和原帧，检查输出内容、时序和 fence，不能靠 UI 数字、成功 counter、重复帧或线性混合交差。静止画面相同像素不能用于证明或否定插值。显示刷新率不足以展示全部输出时，分别报告生成/提交/实际显示，不混为一个 FPS。诊断离线抓帧可用于验收，正常播放/导出不得新增 GPU→CPU 回读。

所有移植固定提交、逐文件记录来源/许可证/修改，并更新 `THIRD_PARTY_NOTICES.md`。运行库只做进程内处理，源码 Git 不纳入 DLL/SDK/媒体；本轮没有新增运行组件或发布授权。

## 附录：输入身份与本轮命令

原始输入位于 `C:/Users/123/Desktop/`，不复制进 Git。

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| 5090显卡补帧受限，显卡占用仅为百分之30.log | 13248726 | `4DAFFAB5222BE64EF09F54A0342AA4EB5D56C6AF9F953A8E1823218FD76213BE` |
| 大MKV卡死+4060无法6x，选了自动跳2x.log | 322438 | `94C1BCECF71D3B65631EC6A7B4FBF74BF69D7298BF3C8685C557461C7080EC1D` |
| 30系列 dlss.log | 3375244 | `2A64F693FD3B3CC571E528CB571F960CF45A218D1B0B89B225F14958C6C50058` |
| 30系列dlss2.log | 1149877 | `F7D11D1ACA15CB42681712F9CE9F02BC3FFB1B198D2DBF9C64CB4C2FFE9269E9` |
| 导出问题.log | 1661137 | `612D01986FF186099F8746AF3A6E96F302D96E360BD11ABFF706D2ACB7AE5FEB` |
| Money.Heist.2019.S03E02.V2.1080p.NF.WEB-DL.H264.DDP5.1-LeagueNF.mkv | 4272359225 | `D4F41DDE491E0F8CDEB3C18D75E91F16B0ED0CFF80523B0CAF2C40398E464C9E` |

已执行 `git status --short`、`git rev-parse HEAD`、`git show 890d200:src/engine/EngineController.cpp`、`rg` 源码/日志定位、`Get-FileHash -Algorithm SHA256`、`gh api` 查询上述仓库提交/tree/README/issue/compare，以及以下样本检查（`<sample>` 表示上表完整绝对路径）：

```text
ffprobe -v error -show_entries stream=index,codec_name,codec_type,width,height,r_frame_rate:format=duration,size -of json <sample>
ffmpeg -v error -nostdin -i <sample> -map 0:v:0 -an -sn -t 5 -f null -
ffmpeg -v error -nostdin -i <sample> -map 0:s:0 -c copy -f null -
gh api repos/dashdogy/RTX40MFG-Unlock/compare/33b41835dc39c5d8ab1ef93efb2449be31139c09...b77e6e55c9211faf58932fc67eaaad9feb9df934
gh api repos/sdli1995/dlssg_for_sm86/git/trees/126d0f768f2293dad6f0da9d3b8fd6961ed88bdc
```

以上为施工前调查命令：当时 FFmpeg/gh 输出保留于工具记录，未执行 Veyra GPU 测试。用户授权施工后的实际 Create/Evaluate、构建和测试日志列在文首；不能将调查读日志等同于本机运行通过。
