# 开发与验证入口

> **2026-11-07 迁移**：项目已整体搬到 `D:\Program\Veyra`（E 盘不存在了，桌面共用 Git 库也不存在）。本表是当前唯一有效的路径；`docs/` 下其他文档里的 `E:/项目/Veyra/...` 是当时记录，按日期理解。

## 路径

| 用途 | 当前入口 |
|---|---|
| 源码与 Git 库 | `D:\Program\Veyra\src`（`.git` 在仓库根） |
| 构建目录 | `D:\Program\Veyra\build`（Ninja，CMake 3.31，Release x64） |
| 依赖 | `D:\Program\Veyra\deps`（FFmpeg 等） |
| Qt | `D:\Program Files\Qt\6.8.3\msvc2022_64` |
| 部署（定制版 + 作者版双切换） | `D:\Program\Veyra-2.1.3-NVIDIA-win64-portable` |
| 历史文档快照 | `D:\Program\Veyra\md-backup-20261025`（10-25 的 5 份旧文档，仅存档） |

当前活动分支 `test/merge-213`（2.1.3 + 定制 + 修复）；`main` 与上游 `origin/main` 同步在 `8fa2d2fc`。

## 构建

改完代码后增量构建（约 2 秒起）：

```powershell
cmake --build D:\Program\Veyra\build --target veyra_qml_ui -j 8
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

- 先构建产品与改动相关目标，再跑相应 unit/contract/QML/实际启动检查；每进程 ≤300 秒。
- 桥接/QML 结构变更应覆盖 UI 合同、翻译、QtQuick、设置/预设保存恢复、采集对话框与私有配置启动，确保无绑定/创建错误。
- 处理图、补帧、音频、导出行为变更时再加对应 GPU/输出检查。
- 部署：`build\veyra_qml_ui.exe` → `D:\Program\Veyra-2.1.3-NVIDIA-win64-portable\veyra_qml_ui.exe` 与 `...\veyra_qml_ui.exe.custom`（双切换的定制版；`.orig` 是作者版，不要覆盖）。
- 启动验证：`veyra_qml_ui.exe --data-dir <私有目录> --exit-after 8000`，日志无 `Unexpected token` / `no root object` / `Type ... unavailable`。
- 实卡验证边界如实写明；不使用真实用户配置。

## 任务收尾

记录代码与文档变更、构建/测试、已验与未验。历史资料从[索引](history/INDEX.md)查找。push、Release、资产上传和代发消息按用户当前请求授权。

2.1.2 及更早的构建/发布流程（`build-recording-handoff.py`、`package-2.1.x.py` 等）硬编码 `E:/项目/Veyra`，在本机已不可用；需要复用时先把路径参数化。
