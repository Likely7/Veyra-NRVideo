# 开发与验证入口

> 2026-11-13全效果剔除修复：当前app仍tests/codex-fg-quality-labels-20261113/app，EXE00b39a4f…。补齐EffectExclusion并更新PresentBlit_ps，修复ANR遮罩合成提交顺序；88个build着色器逐SHA核验，原main入口/用户配置保持。[修复与真实像素/导出验证](EFFECT_EXCLUSION_REPAIR_2026-11-13.md)。

> 2026-11-13补帧标注：最新本地app为tests/codex-fg-quality-labels-20261113/app，源/build仍为下表main目录。原main启动器已指向新app并保留原manual-profile，用户原运行实例未中断。参见[标注改动与验证](FG_QUALITY_LABELS_2026-11-13.md)。

> 2026-11-13：当前本地main及独立验收入口见[整合记录](MAIN_CLAUDE_ANR_INTEGRATION_2026-11-13.md)。Claude原脏树和旧ANR入口保留作原样证据，不再把旧共享build当当前主线程序。公开发行包与本轮测试目录分开；ANR组件未获本轮公开分发授权。


## 路径

| 用途 | 当前入口 |
|---|---|
| 源码施工树 | `E:/项目/Veyra/worktrees/main-claude-anr-20261113` |
| 共用Git库 | `C:/Users/123/Desktop/Veyra DLSS Video Player/.git`，不可按旧副本删除 |
| 可复用构建 | `E:/项目/Veyra/build/codex-main-claude-anr-20261113` |
| 正式包/源码 | `E:/项目/Veyra/releases/codex-release-2.1.5-20261110`（含对应源码/摘要） |
| 当前完整运行供体 | `E:/项目/Veyra/releases/codex-release-2.1.5-20261110` 的不可变NVIDIA/AMD ZIP；不要复制正在使用的解压目录 |
| 基础依赖 | `E:/项目/Veyra/deps/core`；其他根看`E:/项目/Veyra/deps/DEPENDENCY_ROOTS.json` |
| Qt | `E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64` |


当前活动分支 `test/merge-213`（2.1.3 + 定制 + 修复）；`main` 与上游 `origin/main` 同步在 `8fa2d2fc`。

## 构建

改完代码后增量构建（约 2 秒起）：

```powershell
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-main-claude-anr-20261113 --root 'E:/项目/Veyra/worktrees/main-claude-anr-20261113' --build-dir 'E:/项目/Veyra/build/codex-main-claude-anr-20261113' --reconfigure --label 2.1.5-main-integration-test --jobs 2 veyra_qml_ui

```

首次配置或 CMakeLists 变更后需要重新 configure，参数是 2.1.3 定制版实际使用的组合（必须在 VS 2022 x64 环境里，先 `call vcvars64.bat`）：

```powershell
cmake -S D:\Program\Veyra\src -B D:\Program\Veyra\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DVEYRA_BUILD_QML_UI=ON `
  -DVEYRA_ENABLE_DEINTERLACE=ON `
  -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON `
  -DVEYRA_ENABLE_DLSS_SR=ON -DVEYRA_ENABLE_DLSS_FG=ON -DVEYRA_ENABLE_NVOF=ON `
  -DVEYRA_ENABLE_D3D12_DEBUG=ON `
  -DVEYRA_DLSS_SDK_ROOT=D:/Program/Veyra/src/third_party_local/nvidia/DLSS-SDK-310.7 `
  -DVEYRA_NVOF_SDK_ROOT=D:/Program/Veyra/src/third_party_local/nvidia/Optical_Flow_SDK_5.0.7 `
  -DVEYRA_CUDA_DRIVER_INCLUDE_DIR=D:/Program/Veyra/src/third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/cudart_header `
  -DVEYRA_FFMPEG_ROOT=D:/Program/Veyra/deps/ffmpeg/ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0 `
  -DCMAKE_PREFIX_PATH="D:/Program Files/Qt/6.8.3/msvc2022_64"
```

**已知构建坑（2026-11-07 修复）**：CMake 的 Ninja 生成器曾不写 `CMakeFiles/rules.ninja`，ninja 报 `unknown build rule CXX_COMPILER__*_unscanned_Release`。根因是 `CMakeLists.txt` 的 MSVC include 前缀探测在中文 Windows 上拿到 GBK 前缀、CMake 以 UTF-8 写规则文件失败。现在只转发 ASCII 前缀。若再出现，先确认 `build/CMakeFiles/rules.ninja` 存在。

并发上限 `-j 8`（16 核机器）；单次构建 ≤900 秒。

## 验证

- 先构建产品与改动相关目标，再运行相应unit/contract/QML/实际启动检查；每进程≤300秒。
- 桥接/QML结构变更应覆盖UI合同、翻译、QtQuick、设置/预设保存恢复、采集对话框与实际私有配置启动，确保无绑定/创建错误。公有QML属性、方法、信号及配置格式保持。
- 处理图、补帧、音频、导出发生行为变更时，再加对应GPU/输出检查。没有相关变更不要用重复全量测试代替推进。
- 产品使用任务共享的测试运行目录；更换EXE/QML时先创建新文件再替换，避免通过硬链接覆盖正式供体原件。运行库/模型原字节不改。
- 从历史包/旧测试目录继承运行组件后，必须用本次实际build的着色器同步并校验，不能继承供体的旧shaders当本轮产物。目标程序退出后执行 `py -3.11 -X utf8 -B -I scripts/acceptance/sync-built-shaders.py --build-dir <本次build> --app-dir <本次app> --apply --report <E盘本任务回执>`，再去掉`--apply`独立检查，所有build着色器SHA一致才可验收/交付。正常package-qml-release.py本来会复制build/shaders；手工私有stage也必须遵守。
- 不使用真实用户配置，不自动连接PS5/PC/Xbox或采集卡，不停止他人进程。实卡验证边界如实写明。


```powershell
powershell -ExecutionPolicy Bypass -File D:\Program\Veyra\src\scripts\verify-build.ps1
# 只跑检查、不重新构建：
powershell -ExecutionPolicy Bypass -File D:\Program\Veyra\src\scripts\verify-build.ps1 -SkipBuild
```

脚本会从 `CMakeCache.txt` 的 `VEYRA_FFMPEG_ROOT` 取 FFmpeg `bin` 注入 PATH（部分测试动态链接 FFmpeg，缺 DLL 会以 0xC0000135 退出），并在构建目录内运行测试。它只判 `[ERROR]` 为失败，`[WARN]` 计数后交人工判断。


- 先构建产品与改动相关目标，再跑相应 unit/contract/QML/实际启动检查；每进程 ≤300 秒。
- 桥接/QML 结构变更应覆盖 UI 合同、翻译、QtQuick、设置/预设保存恢复、采集对话框与私有配置启动，确保无绑定/创建错误。
- 处理图、补帧、音频、导出行为变更时再加对应 GPU/输出检查。
- 部署：`build\veyra_qml_ui.exe` → `D:\Program\Veyra-2.1.3-NVIDIA-win64-portable\veyra_qml_ui.exe` 与 `...\veyra_qml_ui.exe.custom`（双切换的定制版；`.orig` 是作者版，不要覆盖）。
- 启动验证：`veyra_qml_ui.exe --data-dir <私有目录> --exit-after 8000`，日志无 `Unexpected token` / `no root object` / `Type ... unavailable`。
- 实卡验证边界如实写明；不使用真实用户配置。

## 任务收尾

记录代码与文档变更、构建/测试、已验与未验。历史资料从[索引](history/INDEX.md)查找。push、Release、资产上传和代发消息按用户当前请求授权。

2.1.2 及更早的构建/发布流程（`build-recording-handoff.py`、`package-2.1.x.py` 等）硬编码 `E:/项目/Veyra`，在本机已不可用；需要复用时先把路径参数化。
