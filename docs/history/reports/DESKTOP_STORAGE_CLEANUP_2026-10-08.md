# 桌面项目存储清理回执（2026-10-08）

用户追加“桌面这个也清一清”，仅清理 `C:/Users/123/Desktop/Veyra DLSS Video Player` 的历史研发产物，桌面其它文件未处理。桌面项目按路径累计从 **19.631→12.267 GiB**。删除阶段 C 卷可用空间实际增加 **7.328 GiB（7.868 GB）**；写入 E 盘的三份历史归档共 **2.028 GiB**，加上本轮独立备份、扫描清单/日志等，本轮 E 空间成本约 **2.105 GiB**，两卷整轮净增加可用空间约 **5.046 GiB**。卷空间会受其它系统进程写入影响，不把目录逻辑字节冒充实际收益，也不把跨盘归档全部算成全机净节省。

## 清理与保全

- 删除 **17,395 个文件路径**，包括 logs 的 14,841 份历史测试日志/视频/截图/抓帧、8 套过时 CMake 构建中的 2,449 文件，以及旧 1.2.0 便携包解压副本的 105 文件；9 个明确目录移除，零失败。logs 空目录保留，未对整个桌面、整个 out 或工作树作递归删除。
- 历史 logs 按 SHA256 合并成 9,070 个独立内容条目，lossless ZIP **2,031,323,655 字节**，全部条目 CRC/内容 SHA256 复核通过，每个原文件路径在清单中映射到 `sha256/<digest>`。历史记录未丢弃，恢复时需按映射重新展开到指定路径。
- 8 套旧构建保留 320 份产品/测试 EXE、构建参数、日志和源码片段，归档 **146,414,904 字节**；可重建的 obj/lib 等缓存删除。构建目录只选 scheduling-audit-20260918、media-codec-dav1d、smooth-motion/product、scene-first-config、scene-first-runtime-config、scene-no-nr-first-config、release-1.0.1-bin、release-audit-20260913/core。
- 旧 1.2.0 解压目录的 104 文件与留在原处的 ZIP 全部 SHA256 一致；多出一份启动生成的 logs/veyra-app.log，单独压缩保存并校验后才删除桌面副本。原 ZIP/manifest 保留。
- **66,680 个保留文件逐一做清理前后原字节 SHA256、大小、mtime 和文件 ID 核对通过。** 包括 SDK/运行库原件、out/remoteplay 的当前 Chiaki 依赖、两个 out 内 Git 工作树、captures、loop 固定素材、根目录媒体/NR 原件和未提交测试文件。9 个 junction/reparse 跳过且路径保留；共享 .git 保留，由正常 main 文档提交产生的新 Git 对象不纳入静态文件清单。
- 35 个工作树和 495 份未提交文件原字节/状态保留。独立 start SHA256 `9d617d9b1d18878320d083661252a5e27c6e6a2f4bed0eacd35b9099a8b45e49`；dirty-files.zip 保存其中不超过 16MiB 的原文件，大文件仅记录哈希且保持原位。未覆盖历史 guard/start。

## 核对与限制

57 份正式/候选归档及二进制、587 项已编译源码输入、上轮 658 个 canonical donor 再次 SHA256 核对通过；main 产品 Git 树仍与 VFG 已验 71723e0 一致。当前 `build/vfg-next-20261007` 的 Ninja `-n veyra_qml_ui` 返回 0，输出见日志。这是构建依赖/计划的干运行检查；本轮没有重新编译、运行 GPU 播放或性能测试，不声称新的功能/实卡验收。

清理前核对清单和归档哈希、每个目标内容与元数据、Git ignore/非 tracked/非 dirty、明确绝对路径/父链、活动进程、递归目录精确内容及无 reparse。仅执行原生 PowerShell `Remove-Item -LiteralPath`；清单 SHA256 `61ffd80fd77c8e16aea32a9d775befb0f02d19f4b9e195978401388f224ea57a`。没有新自动审批拒绝，不尝试重删上轮被拒绝的 E 盘 10 MB 缓存。

首次预检发现旧便携 ZIP 不包含新增的启动日志，准备脚本停下且未删除文件；核查仅该日志额外，另存原字节压缩归档后重新完成准备。既有日志/旧构建归档只读复用并重新校验，未覆盖证据来制造通过。

日志原路径及旧构建缓存移除后，历史脚本若引用这些路径，需按 `delete-manifest.json` 中的归档/entry/哈希还原，或从保存的源码/构建参数重建；不能声称所有旧测试目录仍可直接运行。当前正式 2.0.6 双显卡包、最新 VFG 候选及构建保持原样。

## 输出路径与命令

- `E:/项目/Veyra/reports/desktop-cleanup-20261008/`：独立方案、扫描、prepare.py、delete-manifest.json、plan-summary.json、deletion-results.jsonl、deletion-summary.json、protected-desktop-files.jsonl.gz、verified.json。
- `E:/项目/Veyra/archives/desktop-cleanup-20261008/`：start.json、dirty-files.zip、desktop-logs-content.zip、desktop-old-build-records.zip、desktop-package-extra-records.zip。
- `E:/项目/Veyra/logs/desktop-cleanup-20261008/current-build-dry-run.log`。
- 实际命令：`audit-desktop.py snapshot`、`audit-desktop.py scan before`、`prepare.py`；PowerShell `execute-desktop-cleanup.ps1` 只读预检，随后 `-Execute`；`verify-desktop.py`、`audit-desktop.py scan after`、`finish.py`。清理文档仅本地 main 提交，无产品修改/push/Release/关机。
