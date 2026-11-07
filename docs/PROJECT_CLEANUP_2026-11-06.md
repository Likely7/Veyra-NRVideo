# 项目安全清理实际记录（2026-11-06）

用户授权“做一下项目清理，现在硬盘又是要爆炸了”。清理前C盘剩余约104.5 GiB、E盘约876.0 GiB，并未接近满盘；项目仍存在大量旧产物。本轮不扩大到微信、游戏、个人下载和系统文件。

## 实际完成

- 删除 **122,372** 个核验文件，逻辑体积 **53.72 GiB**。
- E盘清理前后可用空间增加 **33.73 GiB**（36,221,321,216 bytes，约36.22 GB）。这是DriveInfo前后实测差值，可能包含同时发生的系统少量写入；不把逻辑体积冒充实际回收。
- 项目文件累计体积 **152.89 → 99.28 GiB**。此口径按路径累计、跳过Junction，仍包含硬链接的重复路径。初扫DirEntry.stat的链接数量字段不可靠，物理估算使用完整os.stat的文件ID与nlink；本轮没有创建新硬链接。
- E盘可用 **875.99 → 909.73 GiB**；C盘未删除个人或项目源码文件，前后约104.5 GiB（观测约4 MiB波动）。

| 项目类别 | 清理前 GiB | 清理后 GiB |
|---|---:|---:|
| build | 16.35 | 3.22 |
| tests | 26.46 | 8.05 |
| test-packages | 15.34 | 4.78 |
| tmp | 10.47 | 3.07 |
| logs | 20.93 | 16.70 |
| deps | 27.71 | 27.71 |
| releases | 11.50 | 11.50 |
| archives | 13.35 | 13.35 |
| downloads | 8.09 | 8.09 |
| worktrees | 2.07 | 2.07 |
| reports | 0.62 | 0.73 |

## 删除范围与保留

已清：旧构建可重新生成的exe/dll/lib/obj/pdb/shader产物、旧测试部署中SHA匹配的发布文件及临时重复运行组件、2.1.1/2.1.2未发布测试ZIP、任务私有CUDA/QML编译缓存、压缩实验副本、logs/perf-nr-20261004生成的旧RGBA/RGBA16F读回帧。旧PNG对照、视频输入、报告、文本日志保留。历史测试副本已退役，残留的未核实修改/配置不作为可用程序供应源。

保留：全部源码工作树、未提交修改、分支与Git历史；所有deps；C:/veyra-deps → E:/项目/Veyra/deps/core兼容Junction；Qt真实目录；全部releases正式包及对应源码ZIP；archives中的历史源码/脏文件与许可源码备份；当前共享主构建build/claude-screen-recording-20261022；NVAPI修复build/rdna3-nvapi-luid-20261103、lmxxf运行库构建、暂停Lecram构建；最新main验收app tests/codex-claude-merge-20261106/app；最新2.1.3测试包test-packages/codex-rebuild-2.1.3-20261106及尚需实卡反馈的小补丁包；个人user-data、profiles、截图及LUT不在删除清单。

34个关键文件（当前EXE/CMakeCache、NVAPI、Qt、依赖根清单及全部正式/源码ZIP）SHA256不变；19个工作树HEAD及原有tracked修改清单在记录性修改之前完全一致。保守排除315个修改或用户数据名称匹配的候选项。首次只读预检拦下20个ProfileStore等名称的编译obj，已从清单排除，未放宽数据守护。

## 方法与证据

所有路径在E:/项目/Veyra指定产物根下逐一GetFullPath/前缀与ReparsePoint检查，删除前逐文件SHA256再核对，Windows PowerShell仅Remove-Item -LiteralPath单文件及已为空目录；没有递归删除、跨Shell拼接、移动工作树或停止进程。执行0个缺失/变化/锁定跳过。先核对源树状态，再检查清理结果，不重编译或重复跑GPU测试制造新产物。

实际命令：reports/codex-project-cleanup-20261106/inventory.py、plan.py；powershell.exe -NoProfile -NonInteractive -File …/execute.ps1（预检），同命令加-Execute（实施），inventory-after.py（复扫）。DELETE-PLAN.json记录每个拟删路径/SHA，PREFLIGHT.json固定清单摘要，DELETED.jsonl记录实际删除，CLEANUP.json记录磁盘前后可用空间；WORKTREES/PROTECTED-FILES/PRESERVATION-VERIFIED/INVENTORY-AFTER.json保留证据。未删除原日志或变更已公开GitHub包。

## 一个保留项

已确认downloads/Veyra-1.4.0-win64-portable.zip内部manifest为1.4.0（约0.44 GiB），低于旧运行包保留门槛；针对该单文件的补充删除命令被自动审批以 **blocked by policy** 拒绝，整个命令未执行，原ZIP仍在，未换工具绕过。BLOCKED-LEGACY.json记载事实。

## 后续产物规则

优先复用当前共享build和一份当前验收app。新任务需隔离配置/日志，完整运行目录只在必要时创建，验收结束按清单退役旧副本。输入媒体与大模型从受控原件复用，不复制整工程作备份。历史脚本的旧test app/旧build路径可能只剩配置和证据；不能把目录存在当作构建仍可运行。重做旧实验先依据固定源码提交/日志，从releases固定ZIP建立受控新stage；不要以残留旧app作供体。保留当前已验程序、最终交付、对应源码与必要证据。
