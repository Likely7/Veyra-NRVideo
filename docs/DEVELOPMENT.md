# 开发与验证入口

更新：2026-11-19。[当前状态](CURRENT_STATUS.md) · [本轮修复](PS5_SDR_CODE_VALUES_2026-11-19.md) · [执行规则](../AGENTS.md)。

| 用途 | 当前入口 |
|---|---|
| main源码 | `E:/项目/Veyra/worktrees/main-claude-anr-20261113` |
| 当前构建 | `E:/项目/Veyra/build/codex-ps5-sdr-code-values-20261119` |
| 当前私有运行/配置 | `E:/项目/Veyra/tests/codex-ps5-sdr-code-values-20261119` |
| 本轮日志与报告 | `E:/项目/Veyra/logs/codex-ps5-sdr-code-values-20261119`、`reports/codex-ps5-sdr-code-values-20261119` |
| 共用Git | `C:/Users/123/Desktop/Veyra DLSS Video Player/.git`，保留 |
| 正式2.1.9原包 | `E:/项目/Veyra/releases/codex-release-2.1.9-20261115` |
| 既有虚拟摄像机附加包 | `E:/项目/Veyra/releases/claude-release-2.1.9-virtual-camera-20261117` |
| 基础依赖 / Qt | `E:/项目/Veyra/deps/core` / `E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64` |

历史任务仍按自己的记录保留；不要继续把2.1.5/2.1.6或旧共享build当当前程序。[旧入口全文](history/reports/DEVELOPMENT_before_main_2026-11-18.md)只供追溯。

## 构建

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-ps5-sdr-code-values-20261119 --build-dir E:/项目/Veyra/build/codex-ps5-sdr-code-values-20261119/main-app --root 'E:/项目/Veyra/worktrees/main-claude-anr-20261113' --label 2.1.9-ps5-sdr-test --jobs 2 --option VEYRA_LIBDOVI_ROOT=E:/项目/Veyra/deps/codex-pr-integration-20261118/libdovi --option VEYRA_RP_CHIAKI_SOURCE_DIR=E:/项目/Veyra/deps/codex-pr-integration-20261118/chiaki-stage --option VEYRA_RP_CHIAKI_VERIFY_DIR=E:/项目/Veyra/deps/core/chiaki-source veyra_qml_ui
```

本轮已验证的 `build/codex-ps5-sdr-code-values-20261119` 来自保留的隔离树 `worktrees/ps5-sdr-code-values-20261119`，完成后本地合main。上面的 main 重建示例使用独立 `main-app` 输出，避免复用不同源码树的 CMake cache；构建脚本会拒绝 source-root 不匹配。

首次/变更CMake选项或显示版本传`--reconfigure`。保持构建并发≤2、单次≤900秒。脚本读取`logs/pr64-avermedia-20261019/build-20261019-094942/receipt.json`固定依赖参数，该小回执不能按旧日志删除。保留patched FFmpeg与实际对应源码/许可。

虚拟摄像机模块与安装器由产品目标依赖构建；x86模块通过独立Win32 CMake子工程构建。独立诊断目标为`veyra_virtualcam_transport_tests`、`veyra_virtualcam_probe`、`veyra_virtualcam_cadence`、`veyra_virtualcam_gpu_tests`。只在任务目录运行，用私有IPC命名空间和配置，避免影响用户生产者；不要为测试擅自更新系统组件。

Qt 主程序是 `veyra_qml_ui.exe`。默认开发构建可通过 `Veyra.cmd` 启动。便携包和安装器使用 `scripts/package-portable-update.py` 与 `scripts/package-windows-installer.py`，包根目录的 `Veyra.exe` 是启动器。旧 `run-test.cmd` 和 Win32 打包脚本已停用。导出计时和损坏输入门禁使用 `veyra_export_probe`，生成帧导出另由 VFG 回归覆盖。

## 验证与stage

- 每个测试进程≤300秒。按实际改动选择合同/CPU/QtQuick/GPU/真实产品回归，保留失败，不把历史结果当本次通过。
- 一个任务共享一份运行stage，配置/log按用例隔离。复制供体先核对原ZIP/manifest，所有可修改文件使用新文件+原子替换，禁止写穿硬链接。
- stage必须同步本次全部build着色器并逐SHA核对：`scripts/acceptance/sync-built-shaders.py --build-dir <build> --app-dir <app> --apply --report <任务回执>`；再不带`--apply`独立检查。不能继承旧shader掩盖漏打包。
- QtQuick/UI变更覆盖语言、设置保存与真实启动；摄像机覆盖动态帧序/时间戳/颜色/尺寸/停启/消费者。广告60FPS不等于60张不同画面，统计不能替代逐帧验证。
- 不停止用户进程，不自动连主机或抢设备。系统安装仍需本人Windows管理员确认；普通权限消费者与管理员OBS分开说明。

## 归档与交付

已合main且无在用进程/本地文件的干净工作树可在tip映射和verify过的bundle后关闭，恢复见[分支归档](BRANCH_ARCHIVE.md)。保留脏树、暂停研究、依赖与正式资产；不把路径字节相加冒充实际磁盘回收。

本地 main 源码版本为 2.2.0。最近一次手测构建来自整合前的 `74e9bf3b`。便携版验签复用已有 Chiaki 依赖中的 OpenSSL Crypto，美乐威 SDK 路径为 `E:/项目/Veyra/deps/magewell/3.3.1.1596`。GitHub 的公开版本为 2.1.9。用户配置、运行组件与BGM分发边界按AGENTS及实际manifest执行。

## 新增外部依赖（#136 / #133）

完整 Windows 构建默认启用 libdovi，必须提供 `VEYRA_LIBDOVI_ROOT`（include/libdovi、lib、bin、MIT LICENSE）；不得把贡献 PR 的 DLL/LIB 合进源码。固定来源 quietvoid/dovi_tool `d1abe0e27ff2c7ab3339614d06db9f8a058af6b2`，libdovi 3.4.0，Rust 1.88+；用其 cbindgen.toml 生成匹配头，`cargo rustc --manifest-path dolby_vision/Cargo.toml --lib --release --locked --features capi --crate-type cdylib -- -Cpanic=abort`。DLL 与 MIT LICENSE 跟随用户包，SDK/导入库不打包。仅精简开发构建可显式关闭 VEYRA_ENABLE_LIBDOVI，不能把关闭版当完整交付。

PS5 使用新的独立 Chiaki stage，按 `scripts/remoteplay/patches/0001..0005` 顺序打补丁并运行 verify-chiaki-stage.py；保留原 stage。nanopb 生成器需把依赖包的 protobuf/tools/protoc 放入**子进程** PATH。不得改全局 PATH 或复用未核验的生成缓存。

Linux 独立 OBS 模块与验证见 [OBS_FILTER.md](OBS_FILTER.md)。
