# 开发与验证入口

更新：2026-11-18。[当前状态](CURRENT_STATUS.md) · [本轮整合](MAIN_INTEGRATION_2026-11-18.md) · [执行规则](../AGENTS.md)。

| 用途 | 当前入口 |
|---|---|
| main源码 | `E:/项目/Veyra/worktrees/main-claude-anr-20261113` |
| 当前构建 | `E:/项目/Veyra/build/codex-main-integration-20261118` |
| 当前私有运行/配置 | `E:/项目/Veyra/tests/codex-main-integration-20261118` |
| 本轮日志与报告 | `E:/项目/Veyra/logs/codex-main-integration-20261118`、`reports/codex-main-integration-20261118` |
| 共用Git | `C:/Users/123/Desktop/Veyra DLSS Video Player/.git`，保留 |
| 正式2.1.9原包 | `E:/项目/Veyra/releases/codex-release-2.1.9-20261115` |
| 既有虚拟摄像机附加包 | `E:/项目/Veyra/releases/claude-release-2.1.9-virtual-camera-20261117` |
| 基础依赖 / Qt | `E:/项目/Veyra/deps/core` / `E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64` |

历史任务仍按自己的记录保留；不要继续把2.1.5/2.1.6或旧共享build当当前程序。[旧入口全文](history/reports/DEVELOPMENT_before_main_2026-11-18.md)只供追溯。

## 构建

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-main-integration-20261118 --root 'E:/项目/Veyra/worktrees/main-claude-anr-20261113' --label 2.1.9-main-integration-test --jobs 2 veyra_qml_ui
```

首次/变更CMake选项或显示版本传`--reconfigure`。保持构建并发≤2、单次≤900秒。脚本读取`logs/pr64-avermedia-20261019/build-20261019-094942/receipt.json`固定依赖参数，该小回执不能按旧日志删除。保留patched FFmpeg与实际对应源码/许可。

虚拟摄像机模块与安装器由产品目标依赖构建；x86模块通过独立Win32 CMake子工程构建。独立诊断目标为`veyra_virtualcam_transport_tests`、`veyra_virtualcam_probe`、`veyra_virtualcam_cadence`、`veyra_virtualcam_gpu_tests`。只在任务目录运行，用私有IPC命名空间和配置，避免影响用户生产者；不要为测试擅自更新系统组件。

## 验证与stage

- 每个测试进程≤300秒。按实际改动选择合同/CPU/QtQuick/GPU/真实产品回归，保留失败，不把历史结果当本次通过。
- 一个任务共享一份运行stage，配置/log按用例隔离。复制供体先核对原ZIP/manifest，所有可修改文件使用新文件+原子替换，禁止写穿硬链接。
- stage必须同步本次全部build着色器并逐SHA核对：`scripts/acceptance/sync-built-shaders.py --build-dir <build> --app-dir <app> --apply --report <任务回执>`；再不带`--apply`独立检查。不能继承旧shader掩盖漏打包。
- QtQuick/UI变更覆盖语言、设置保存与真实启动；摄像机覆盖动态帧序/时间戳/颜色/尺寸/停启/消费者。广告60FPS不等于60张不同画面，统计不能替代逐帧验证。
- 不停止用户进程，不自动连主机或抢设备。系统安装仍需本人Windows管理员确认；普通权限消费者与管理员OBS分开说明。

## 归档与交付

已合main且无在用进程/本地文件的干净工作树可在tip映射和verify过的bundle后关闭，恢复见[分支归档](BRANCH_ARCHIVE.md)。保留脏树、暂停研究、依赖与正式资产；不把路径字节相加冒充实际磁盘回收。

当前main构建仍是本地整合测试版，版本2.1.9。本轮没有新push/Release或改写已发布下载资产授权。用户配置、运行组件与BGM分发边界按AGENTS及实际manifest执行。
