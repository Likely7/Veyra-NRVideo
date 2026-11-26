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

本地 CMake `project(veyra VERSION)` 为 2.2.1。最近一次手测构建来自整合前的 `74e9bf3b`。便携版验签复用已有 Chiaki 依赖中的 OpenSSL Crypto，美乐威 SDK 路径为 `E:/项目/Veyra/deps/magewell/3.3.1.1596`。GitHub 最新正式版标签为 v2.2.1。用户配置、运行组件与BGM分发边界按AGENTS及实际manifest执行。

## 应用内更新

设置 → 关于 → 检查更新只查询一个地址：`https://api.github.com/repos/Likely7/Veyra-NRVideo/releases/latest`。程序固定 `User-Agent`，限制响应大小和超时。默认「GitHub 直连」只跟随重定向到 `api.github.com` 上的这条路径。用户在「下载线路」里选了 `gh-proxy.com` 或 `ghfast.top` 时，才额外允许该镜像，并且路径必须是上述官方 URL 本身，或由该镜像前缀接上这条官方 URL。查询串、片段、用户信息和显式端口都拒绝。镜像失败后的回退请求只允许 `api.github.com`。这个接口返回单个正式版。正文如果是 release 列表，不把第一项当成最新版，也不跟随 `Link` 分页。不使用环境变量里的 GitHub token。资产名不符合发布文件名规则的条目会被忽略，不因此拒绝整份版本说明；清单和被选中的 ZIP 仍使用严格的名称与 SHA-256 规则。

版本必须是 `vX.Y.Z`（`v` 可省略）。多出来的预发布后缀、前导零、超过 65535 的段和其它畸形标签都判为无效，不会被当成更新。启动时检查默认关闭；用户打开后，启动只查询版本，不会下载或安装。

可安装的资产只有签名清单 `Veyra-portable-update-v1.json` 点名的 `Veyra-<版本>-NVIDIA-win64-portable.zip` 或同名 AMD 包。清单用编译进正式构建的 Ed25519 公钥校验，ZIP 用清单里的 SHA-256。若 GitHub 资产带有 `digest`，它必须是 `sha256:` 加 64 位小写十六进制，并且和清单一致，否则拒绝安装。没有这份已签名校验和时不安装。下载只走 HTTPS，重定向只允许 `release-assets.githubusercontent.com` 和 `objects.githubusercontent.com`。解压前拒绝绝对路径、盘符、`..` 和非常规 tar 成员。

当前公开发行包还没有 Authenticode 发布者签名。完整性根是 Ed25519 清单加 ZIP 的 SHA-256，不是“没有签名就算通过”。签名缺失不证明文件可信；签名存在但 `WinVerifyTrust` 判定无效时拒绝安装。安装过程不做在线证书吊销查询。

可写的安装目录把包放在该目录的 `.veyra-update`。只有用户点击「重启并安装」才会安装并重新打开。普通退出不安装，已校验的包保留到下次启动。安装在 Program Files 且目录不可写时，包改存由 `SHGetKnownFolderPath(FOLDERID_LocalAppData)` 得到的 `%LOCALAPPDATA%\Veyra\update-staging\<16位十六进制>`，不读取可能被继承进来的 `LOCALAPPDATA` 环境变量。只有用户点击「允许系统授权并安装」后，才用 `ShellExecuteExW` 的 `runas` 启动安装目录里的 `veyra_update_worker.exe`。Windows 自己显示 UAC，程序不自动同意，也不改系统授权策略。

提升后的进程不把用户可写的暂存目录当成可执行来源。它拒绝路径上任一重解析点，重新验证清单签名，并在不共享写、不共享删除的句柄上读取 ZIP。字节被复制到新建目录 `ProgramData\Veyra\update-install\<随机名>`：创建时就写入受保护的 DACL，只允许 SYSTEM 和管理员组，所有者为 Administrators。复制完成后在这个目录里重新计算 SHA-256，与清单一致才解压、探测并移入 `versions`。探测进程的工作目录、`TEMP` 和 `TMP` 都在该目录内。同一安装根上的第二份安装进程拿不到互斥量，会直接退出。播放器在提升后的安装尚未结束时不会退出，因此启动器一直持有运行锁，也不会因为普通退出再启动 `.veyra-update` 上的安装进程。已下载但未点击安装的 Program Files 包在下次启动时仍从同一暂存目录恢复，仍须再点一次。

安装失败不改 `current-version.txt`。新版本在启动后短时间内失败时，启动器退回上一版。

发布时仍用同一构建执行：

```powershell
py -3.11 -X utf8 -B -I scripts/package-portable-update.py wrap --version <version> --stage <vendor-stage> --build <build> --output <artifacts>
py -3.11 -X utf8 -B -I scripts/package-portable-update.py sign --version <version> --nvidia <nvidia-zip> --amd <amd-zip> --openssl <openssl-exe> --key <offline-ed25519-key> --notes <summary> --output <artifacts>
```

把两份 ZIP 和 `Veyra-portable-update-v1.json` 放进同一个非草稿、非预发布、标签为 `vX.Y.Z` 的 GitHub Release。资产名必须和清单里的 `asset` 完全一致。私钥不得进入仓库、日志或程序。更完整的包布局见[便携 ZIP 更新](PORTABLE_UPDATE_PLAN_2026-11-19.md)。

## 新增外部依赖（#136 / #133）

完整 Windows 构建默认启用 libdovi，必须提供 `VEYRA_LIBDOVI_ROOT`（include/libdovi、lib、bin、MIT LICENSE）；不得把贡献 PR 的 DLL/LIB 合进源码。固定来源 quietvoid/dovi_tool `d1abe0e27ff2c7ab3339614d06db9f8a058af6b2`，libdovi 3.4.0，Rust 1.88+；用其 cbindgen.toml 生成匹配头，`cargo rustc --manifest-path dolby_vision/Cargo.toml --lib --release --locked --features capi --crate-type cdylib -- -Cpanic=abort`。DLL 与 MIT LICENSE 跟随用户包，SDK/导入库不打包。仅精简开发构建可显式关闭 VEYRA_ENABLE_LIBDOVI，不能把关闭版当完整交付。

PS5 使用新的独立 Chiaki stage，按 `scripts/remoteplay/patches/0001..0005` 顺序打补丁并运行 verify-chiaki-stage.py；保留原 stage。nanopb 生成器需把依赖包的 protobuf/tools/protoc 放入**子进程** PATH。不得改全局 PATH 或复用未核验的生成缓存。

Linux 独立 OBS 模块与验证见 [OBS_FILTER.md](OBS_FILTER.md)。
