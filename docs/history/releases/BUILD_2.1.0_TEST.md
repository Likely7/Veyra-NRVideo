# 2.1.0 本地测试构建

2026-10-19 用户要求把此前所有修复（含 Codex 的）合并到 main 并打 2.1.0 测试包，供发布前测试。整合分支
`claude/release-2.1.0-20261019`（工作树 `E:/项目/Veyra/worktrees/release-2.1.0-20261019`），从 main `1dc8c18`
（PR64 之后）依次合并：

| 来源 | 内容 |
|---|---|
| `codex/fullscreen-input-20261018`（含 `codex/amd-nr-044-20261018`） | 专业模式全屏点击误暂停、极简全屏指针隐藏；AMD NR lmxxf 0.44（Small / 原模型） |
| `claude/network-buffer-48-20261017` | #48 网络播放缓冲：HLS 最佳码率、直播重连、隔行损坏容忍 |
| `claude/beauty-20261017`（含 `claude/aa-53-20261017`、`claude/effects-infra-20261017`） | E0 效果器扩展、#53 抗锯齿、美颜（人脸识别、跟踪、468 点人脸网格） |
| `claude/sponsor-20261019` | 首页标题“Veyra NR Video”、设置“赞助作者”页 |

没有合并：`codex/beauty-node-plan-20261016`（只有方案文档，已由美颜实现取代）、`claude/rtss-compat-20261003`（两份 10-03
规划文档，RTSS 兼容本身早已在 main）、`codex/beta-latency-ab-20260918`、`codex/smooth-motion-experiment`、
`codex/github-source-archive`（旧实验或旧归档）。合并前存档：标签 `checkpoint/pre-release-2.1.0-20261019`（main 1dc8c18）、
`E:/项目/Veyra/archives/claude-release-2.1.0-20261019/{source-before.bundle,start.json}`。

## 构建

Windows x64、MSVC 14.44、CMake/Ninja、Qt 6.8.3，选项沿用 main 最近一次已验构建
（`logs/pr64-avermedia-20261019/build-20261019-094942/receipt.json`），只把路径指向本工作树，并设
`VEYRA_DISPLAY_VERSION=2.1.0-test`。项目版本 2.1.0（`CMakeLists.txt`）。命令：
`python E:/项目/Veyra/tmp/claude-release-2.1.0-20261019/tools/build.py [--check-clean] <targets>`，回执在
`E:/项目/Veyra/logs/claude-release-2.1.0-20261019/build-*/receipt.json`。

FFmpeg 与 2.0.9 相同：PS5 slices256、dav1d、zlib 字幕补丁，去隔行用独立 LGPL avfilter-12 模块
（`deps/ffmpeg-filter-issues41-45-20261016`）。

## 包内容

以已交付的 2.0.9-test 两包为底（`test-packages/nr-sf-310916-20261017`），运行库字节不变，再放入：

- 本次构建的 `veyra_qml_ui.exe`、全部 QML（含赞助页的 SVG / PNG）、着色器与文档。
- 美颜模型：`runtime/models/face_detection_yunet_2023mar.onnx`（OpenCV Zoo YuNet 47534e27，MIT，SHA256
  `8f2383e4…2fa4`）与 `runtime/models/face_landmark.tflite`（MediaPipe v0.8.9，Apache-2.0，SHA256 `c603fa61…26d1`），
  许可证在 `licenses/`。打包检查原本禁止 `.onnx`，`scripts/package-qml-release.py` 只对这两个路径按内容哈希放行。
- NVIDIA 包：NR 仍是 SF 310.9.16（`nr-ampere`）与 NVIDIA 原版 310.8.0.0（`nr-original`）。
- AMD 包：`runtime/amd-nr` 由 lmxxf 0.39 换成 Codex 审计的 0.44 运行库（`deps/amd-nr-044-20261018/runtime`，
  Small `LmxxfNrRuntime.dll` 与原模型 `LmxxfNrRuntime-Original.dll`，按 `scripts/package-vendor-release.py` 的布局与行）。
  对应源码 `archives/amd-nr-044-20261018/lmxxf-044-corresponding-source.zip` 随包放在上一级。

打包：`python E:/项目/Veyra/tmp/claude-release-2.1.0-20261019/tools/package210.py stage|seal|cold`，即审计过的
`scripts/acceptance/test-2.0.7-package.py` 加 2.1.0 适配（同 2.0.9 的做法）。输出
`E:/项目/Veyra/test-packages/claude-release-2.1.0-20261019`，各包 `package-manifest.json`、
`release-runtime-manifest.json` 与同级 `DELIVERY.json` 绑定源码、EXE 与 ZIP。运行时清单供审计，不锁定用户自行替换的 DLL。
便携运行需完整解压，不能只替换 EXE。
