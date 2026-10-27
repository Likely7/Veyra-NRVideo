# 项目清理与 main 合并回执（2026-10-08）

当前 VFG 修复已合入本地 main。实际删除阶段 E 盘增加 **183.488 GiB（197.019 GB）** 可用空间，48 套过时构建和 56,889 个重复文件全部删除成功。包含本轮新增保全归档/扫描清单后的整轮净增约 **183.084 GiB**；最终 E 盘可用约 **885.54 GiB（950.84 GB）**。

## 合并与源码

main 从 `96a7c8de36bc195240161de6814739ad810722f1` 快进到已验 `71723e0249a17764696796fd204a6eb69d6da276`，包含 VFG 降档组预算和严重过载恢复探测修复；产品 Git 树与候选完全一致，原算法/运行库未另改。前后标签为 `checkpoint/pre-merge-vfg-next-20261008`、`checkpoint/merged-vfg-next-20261008`。后续 main 提交只记录本轮授权/方案/报告/WORKLOG，最终 HEAD 在外部 closure.json。

重用已验证候选构建，没有声称本轮重新编译。587 项编译输入的原始 SHA256 与隔离源码匹配；main 工作树十份文件仅 CRLF/LF 不同，已逐字节规范化比较及 Git 树比较。相关原始记录在 protected-files.json。新跑 VFG 设置/预算测试 **353 检查、0 失败**。

本轮不包含远端推送、新 Release、运行组件替换或显存问题修复；现有正式发布资产保持原样。

## 占用原因及变化

E 盘项目按路径累计 **572.09→193.11 GiB**；按文件 ID 去重后的文件存储估计 **367.20→183.25 GiB**。共享硬链接使目录累计明显高于实际存储，不能把删除的 379.28 GiB 逻辑文件量当成实际释放量。卷可用空间差值才是本轮实际回收读数。

主要原因是逐用例复制完整 Qt/DLL/NR/VFG 模型目录而未及时收尾，另有几十套旧构建及大量逐像素抓帧。不是源码本身占了几百 G：E 盘源码工作树总计约 3.10 GiB，35 个工作树全部保留。

下表为按路径累计的逻辑 GiB，便于定位；跨目录硬链接的唯一分配归属随遍历顺序变化，不据分目录归属计算物理收益。GetCompressedFileSize/文件 ID 估计不包含目录/MFT，扫描期间也有本轮少量诊断写入。

| 目录 | 清理前逻辑 GiB | 清理后逻辑 GiB |
| --- | ---: | ---: |
| tests | 333.96 | 68.22 |
| test-packages | 73.75 | 34.99 |
| logs | 41.60 | 11.87 |
| verify | 31.45 | 9.05 |
| build | 24.60 | 8.00 |
| tmp | 9.77 | 3.72 |
| releases | 34.28 | 34.28 |
| deps | 8.55 | 8.55 |
| downloads | 5.95 | 5.95 |
| archives | 5.01 | 5.23 |
| worktrees | 3.10 | 3.10 |

C 盘桌面旧仓库、SDK/依赖、用户素材/捕获和旧发布目录只盘点、不清理；它们仍有现有构建引用，例如 patched FFmpeg、Xbox 依赖及 Chiaki 源码。原有 junction 只记录、不跟随、不删除。两次有效扫描均无读取错误，408 个 reparse point 跳过。

## 清理范围与保全

- 删除 48 个确认可重建且无 reparse point 的旧 CMake 构建目录；保留 VFG-next、正式 2.0.6、AMD provider 构建及不满足安全清理条件的目录。归档 2,190 份旧产品 EXE/构建参数/源码片段，ZIP 244,941,265 字节，CRC 与每份内容 SHA256 复核通过。
- 测试/验证/历史测试包/临时目录仅删除有完全相同 SHA256 donor 的 DLL、模型和翻译；不改 donor 原字节。NR 性能日志中重复的 RGBA 输出同样保留一份原字节与全部原路径映射。共 658 个 canonical donor 清理后再次校验通过，没有创建新硬链接。
- 正式发行 ZIP/源码、所有工作树/原引用、495 份未提交修改、依赖/SDK/运行库原件、原始用户媒体保留。独立 start 与 dirty-files.zip 在 archives/project-cleanup-20261008，start SHA256 `258ad5e5c4ff7fb67d2fceb73a744cce3bc1c9737a2d6d68f20190b783fa8834`。主分支仅按用户授权推进；其它引用和工作区逐项核对。
- 正式 2.0.6 NVIDIA/AMD 完整运行目录、最新 VFG 完整候选/ZIP/源码、当前可用构建和必要调查记录保留。57 份发行归档/二进制身份、587 项编译输入与保留 donor 核对通过。

**旧测试目录经 payload 去重后不再保证可直接运行。** 其唯一 EXE、QML、配置、脚本、普通日志和输出仍在；需要复跑时，用 delete-manifest.json 中原路径→donor/SHA256 映射复制回缺失文件，或从同版本 ZIP 重建任务专用 app。旧构建可由保存的源码和构建参数重建，旧产品 EXE 另有归档。历史 guard 引用已清理路径时先恢复或为新任务建立独立基线，不篡改旧 start/guard。

## 清理后运行核对

使用保留的完整 VFG 候选，独立 E 盘 profile/日志/TEMP/CUDA cache，未复制新的完整运行目录。原 GTA6 4K 视频、NVIDIA 原版单层 1080NR、Medium VFG4、普通 GPU 优先级：实际 GUI 运行 **21.485 秒**、exit0；NR 内部尺寸与有效 VFG 生成日志通过，26 个 Qt/codec 模块均从候选包内加载，无 ERROR/FATAL/QML 错误。运行前后候选全部 manifest payload SHA256 匹配。本项只证明清理后运行正常，不是新的性能对比或长稳验收，不代表 AMD 实卡通过。

命令与详细回执都在 `E:/项目/Veyra/reports/project-cleanup-20261008` 和 `E:/项目/Veyra/logs/project-cleanup-20261008`：audit.py snapshot/scan、plan-cleanup.py、protect.py prepare/verify、execute-cleanup.ps1 预检及 -Execute、post-cleanup-smoke.py。删除前核对绝对路径/父链、mtime/大小、活动进程、归档与清单 SHA；仅原生 PowerShell Remove-Item -LiteralPath 执行删除。清单 SHA256 `03277812bf9c67f6118437efae7ab906872d8b7143bf12b0d30dac514d230246`。

## 排查中的失败与纠正

首次扫描采用 Python 3.11 Windows DirEntry.stat，dev/ino/nlink 为零；逻辑计数可用，硬链接/唯一存储无效，未用于删除。改为 os.stat 并要求非零身份，重新完成 before-fixed 有效扫描；首次记录保留用于说明。首次 planner 的逐目录匹配过慢，在未生成清单或归档、未删除任何内容前终止，改为常量时间类别索引后完整重跑。资产保护初次把 main CRLF checkout 与候选 LF 原字节直接比较而失败，查明十份文件仅行尾差异后，以候选原 SHA + main 规范化字节/Git 树双核对纠正；不修改产品或原验收证据来制造通过。

后续同版本测试共享任务专用 app，逐用例仅隔离 profile/日志/cache；确需修改 app 时临时复制，用毕清理。该收尾规则已补入 AGENTS.md，禁止继续永久堆积逐用例完整运行目录。

## 附加缓存清理限制

主要清理、校验、文档提交和运行短测完成后，最后尝试删除本轮 `E:/项目/Veyra/tmp/project-cleanup-20261008` 缓存的原生 PowerShell 命令，在创建进程前被自动审批拒绝；工具只返回 `blocked by policy`，没有进一步理由。命令未执行，没有重试、包装为其它脚本或改用其它 shell 绕过。只读核对该目录为 70 文件 / 10,015,907 字节（约 10 MB），现保留。此附加限制与前述 48 构建/56,889 重复文件成功删除的零失败回执分别记录，不影响约 197 GB 已实际回收的空间。


## 后续桌面清理

用户追加“桌面这个也清一清”后，另以独立基线清理桌面 Veyra 项目的历史产物。上述“C 盘只盘点”描述仅指前一阶段；追加阶段范围、跨盘归档成本与实际回收见 [桌面清理回执](DESKTOP_STORAGE_CLEANUP_2026-10-08.md)，不修改前一阶段清单/基线/回执。
