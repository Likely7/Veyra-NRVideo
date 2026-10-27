# 发布审查 P1 修复 — 2026-09-13

用户授权：仅修复 `RELEASE_AUDIT_2026-09-13.md` 的 F1/F2/F3（三个 P1）。F4/F5 两个 P2 不在本次范围，不发布、不替换 NVIDIA/FFmpeg 运行组件。

基线 `f037f49`；开工保留审查报告与 WORKLOG 未提交改动，存档 tag `checkpoint/release-p1-2026-09-13`，施工分支 `codex/release-p1-repair`。

## 最小修复闭环

1. F1：过载追赶跨 `read()` 前持有候选帧自己的 AVFrame 引用；复用现有缓存，不增加像素复制、GPU 回读或无界队列，保留最后有效 PTS 与尾帧。
2. F2：共享图在任何像素转换/提交前校验实际帧尺寸与格式。文件源遇到尺寸变化明确返回错误并停止，固定尺寸导出拒绝此输入；保留已有 PS5 尺寸变化重建路径。
3. F3：区分解码待输入、正常 EOF、硬错误；文件源错误保持到 seek/reopen，拒绝损坏 packet/frame。导出预检和执行阶段传播源错误，结束时逐帧验证编码输出的数量、尺寸、CFR 时间线，并保留取消能力；仅成功时提升正式文件。预检失败、尚未创建 partial 时不假称已保留 partial。

## 验证门槛

- 针对三个 P1 增加持久回归，用真实文件/实际产品图验证，不能只测试模拟返回值。
- 复用审查合成素材并添加 B 帧、晚于预检窗口的坏尾帧/尺寸变化；正常文件解码/导出保持完整。
- 正常/欠速播放到尾部、暂停 seek 尾部后恢复；错误后 seek/reopen 能重新开始。
- 直接向图传入错误尺寸和空格式，必须在访问像素和提交 GPU 工作前拒绝，再处理有效帧仍成功。
- 独立构建，运行适用软件/硬解/采集/HDR 回归及 `scripts/gates/delivery.ps1`；每次测试 ≤300 秒。
- 保留 patched FFmpeg avcodec SHA256 `0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`。

## 当前状态

三个 P1 已完成本地修复与回归。F4/F5 两个 P2 未修；未推送、发布或替换既有便携包。旧审查构建与旧日志保留供对照。

## 实际修改

- `src/engine/EngineController.cpp`：复用现有缓存持有跨 read 的候选帧引用，检查 clone 失败，保留尾帧和原始 PTS；文件错误显示具体原因。没有修改音频调度和预览跳帧政策。
- `src/pipeline/EnhanceGraph.cpp`：所有入口在 CPU 像素读取、swscale 和 command slot 获取前校验实际 extent/像素格式；错误输入不触碰纹理。
- `include/veyra/media/FFmpegVideoDecoder.h`、`src/media/FFmpegVideoDecoder.cpp`：暴露 NeedInput/Frame/EndOfStream/Error 四态，记录 receive 硬错误，open/seek/close 清状态；原 borrowed-frame 寿命契约不变。
- `include/veyra/source/MediaFileSource.h`、`src/source/MediaFileSource.cpp`：损坏 packet、硬解码错误、标记损坏的帧和尺寸变化都停止读取，错误保持到成功 seek/reopen。尺寸变化不再偷偷缩放或写旧容量。
- `src/engine/VideoExportJob.cpp`：源错误覆盖预检和编码阶段；音频 demux 硬错误不当作 EOF；取消/边界中止不能成为短片成功。编码完成后完整解码输出，逐帧核对 CFR PTS、尺寸和编码帧总数，正常 EOF 后才提升输出；区分尚未创建 partial 与保留 partial。
- `src/engine/ExportJobManager.cpp`：子进程退出后保留具体失败说明，避免预检失败被覆盖成“partial 已保留”；沿用现有 Finishing 状态消息，取消监视线程继续有效。
- `tests/integration/FileSafetyTests.cpp`、`scripts/gates/release-p1.py`、`CMakeLists.txt`：增加真实产品库/进程回归及独有目录素材生成器。

## 本轮验证与真实结果

最终 EXE：`out/release-p1-20260913/build/veyra.exe`。

SHA256：`B116AB6855D69CDEB29927AB3AD80422D54051339FCE47D7EE5939CAA987519D`。

| 项目 | 实际结果 | 证据 |
| --- | --- | --- |
| 独立 Remote Play Release 构建 | 初次 446 步成功；后续增量及最终构建成功 | `logs/release-p1-20260913/build.log`、`rebuild.log`、`final-build.log` |
| P1 专用回归 | 29 次进程调用及 12 条文件/时长断言通过，29 次包含素材生成与 ffprobe；内部 C++ 断言另保存在各日志 | `logs/release-p1-20260913/verified/result.json` |
| F1 | 正常/150ms 注入欠速均到 Ended，尾帧位置 ≥1.96 秒；确实出现 previewSkipped；暂停 seek 到尾部后恢复再次到 EOF；退出 0 | `verified/play-eof.stdout.log`、`play-overload-eof.stdout.log` |
| F2 | 大/小/零尺寸和空格式在无像素缓冲条件下被拒绝；随后有效帧输出可读取且非黑；第 151 帧变尺寸时源/导出报错，保留 partial，不生成正式文件 | `verified/graph-contract.stdout.log`、`source-resize.stdout.log`、`export-resize.stdout.log` |
| F3 输入损坏 | 2 秒坏尾帧在预检失败、未创建输出；8 秒文件在第 120 帧预检后遇坏尾帧仍失败，只保留 partial；退出 1，非成功/取消 | `verified/export-bad-early.*.log`、`export-bad-late.*.log` |
| F3 输出损坏 | 编码后故意破坏最后一个视频 packet，完整验证读到 59/60 帧，passed=false；未提升正式文件 | `verified/export-corrupt-output.stdout.log` |
| 取消/子进程 UI | 验证阶段取消、帧边界中止均保留 partial 且不成功；真实独立 worker 预检错误传到父进程，明确“未生成输出文件” | `verified/export-cancel-verify.stdout.log`、`export-abort-boundary.stdout.log`、`export-worker-failure.stdout.log` |
| 正常数据 | 2 秒 60 帧、8 秒 240 帧及对应音轨/时长完整；4K 24 张 B 帧与单线程基准逐像素/PTS 一致，seek 后也通过 | `verified/result.json`、`threaded-bframes-reference.stdout.log` |
| 共享路径 | 硬解导入、Main10/HDR 文件组合、采集回放、NR→SR、文件欠速/FG恢复、图片尺寸、source fidelity 均退出 0 | `logs/release-p1-20260913/cross/result.json` |
| 非整数帧率 | 30000/1001 视频完整验证 90/90 帧，CFR 校验通过 | `cross/export-ntsc.stdout.log`、`inspect-ntsc.stdout.log` |
| RTX 欠速尾部 | 实际 NR 16 次、NVOF 2 次、生成 1 帧，failed=false，退出 0；不是零迟到证明 | `cross/nr-fg-overloaded-tail.stdout.log` |
| Delivery | 23 项全部通过，含 native 4K H.264/HEVC NVENC、音轨、完整短片解码及取消 | `logs/delivery/7490c0d852a04aadbe96eced2106524f/result.json` |

表中 `verified/`、`cross/` 均相对 `logs/release-p1-20260913/`。单次调用 ≤180 秒，符合项目单项 ≤300 秒要求。

RTX5070/驱动616.56 实际日志：

```text
snippet CreateFeature id=18 result=0x1 (NVSDK_NGX_Result_Success) handle=non-null seh=0
fg-backend: Create DLSSG 640x360 ... result=0x1 ... seh=0
smoke frames=16 generated=1 failed=false ... nrEvaluated=16 nvofExecuted=2
fg-backend: ReleaseFeature result=0x1 ... evaluates=3 resets=2
HDR_MAIN10_NR mode=1 sr=12 generated=11 hardware=1 confirmed=1 frames=12 evaluate=12 pass=1
[export-verify] decoded=59 expected=60 eof=false cancelled=false passed=false
[export-verify] decoded=90 expected=90 eof=true cancelled=false passed=true
```

初轮 `regression/result.json` 总结果为 false，因为脚本把实际失败码 1 误写成取消码 3。产品当时已正确失败、文件状态断言通过；修正期望后重跑通过。原记录保留。第二轮 `regression-final/` 通过后，自查清理重复状态初始化并补上提升输出前的取消检查，最终构建再跑 `verified/` 全部通过。没有修改 gate 门槛掩盖产品故障。

## 实际命令

在项目根目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/release-p1-20260913/build -RemotePlay -ChiakiCheckout C:/veyra-deps/chiaki-source -ChiakiStage out/remoteplay/chiaki-msvc-stage -RemotePlayPrefixPath C:/veyra-deps/remoteplay-installed/x64-windows-static -ProtocPath C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe -PkgConfigPath C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe
python scripts/gates/release-p1.py --root . --build-directory out/release-p1-20260913/build --output-directory logs/release-p1-20260913/verified
python out/release-p1-20260913/cross_checks.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/release-p1-20260913/build
git diff --check
```

delivery 命令由 cross runner 调用一次；完整命令、耗时和退出码保存于两个 result.json。重跑 P1 gate 要选择新的 output-directory，脚本拒绝覆盖旧证据。

## 边界与后续

普通文件中途改变尺寸会明确停止，本次没有增加自动变分辨率播放/导出功能。导出结束增加全片 CPU 解码验证，会延长验证阶段；这不是增强路径 GPU→CPU 像素回读，验证可取消。

patched FFmpeg hash 与开工一致；未修改 SDK、NVIDIA 运行时、模型、凭据、个人配置或旧发布资产。实机 PS5/PSN、HDR 显示器、长时稳定性、4K60 采集卡和多 GPU 验收未执行；本轮 HDR/采集回放不能代替它们。

下一步唯一事项：使用本轮新构建做用户试用验收；已运行的旧构建不会被自动替换。两个 P2 保留在审查报告中，不自动修复或发布。
