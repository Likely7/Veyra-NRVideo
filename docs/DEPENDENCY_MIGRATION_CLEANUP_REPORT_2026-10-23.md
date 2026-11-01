# 2026-10-23 依赖迁移与历史产物清理结果

用户授权C盘Veyra-deps迁往E盘、记录AGENTS，并清理历史测试/包，仅保留2.0.7及以后正式发布。主迁移和清理已完成；另补查的C盘五项旧测试产物被自动审批拒绝，仍保留。没有合并、推送、发布或远端删除。

## 依赖与会话入口

- 实体依赖：`E:\项目\Veyra\deps\core`。149125普通文件、15139684399字节（14.10 GiB逻辑），从C盘复制后逐一SHA256核验，源大小/mtime未变化；旧C实体已删除，迁移备份目录已不存在。
- `C:\veyra-deps` 仅为到上述E盘目录的Junction，供历史脚本/CMake缓存使用。不是C/E双份，不是跨盘文件硬链接。新任务用E盘路径，不要删除兼容入口或在C盘重建依赖实体。
- Qt没有重复复制：原 `C:\veyra-deps\qt-veyra` 是连接；新 `E:\项目\Veyra\deps\core\qt-veyra` 指向原有 `E:\项目\Veyra\deps\qt`，Qt版本目录 `6.8.3\msvc2022_64`。
- 共享规则 `E:\项目\Veyra\AGENTS.md`、机器清单 `E:\项目\Veyra\deps\DEPENDENCY_ROOTS.json`，以及当前 `screen-recording-fix-20261022`、旧桌面和 `main-merge-20261002` 三个主要源码入口的AGENTS均有新记录。没有批量改101个源码树；旧任务仍可通过兼容入口找到相同实体。

## 已执行的历史清理

绝对路径白名单在 `E:\项目\Veyra\reports\dependency-migration-cleanup-20261023\approved-cleanup-plan.json`；原生PowerShell脚本 `Cleanup-History.ps1` 先预览校验，再执行，逐项回执 `cleanup-executed.jsonl`。

| E盘类别 | 删除顶层历史项 | 文件路径数 | 逻辑GiB（含硬链接重复） |
| --- | ---: | ---: | ---: |
| tests | 218 | 798418 | 206.15 |
| verify | 39 | 86465 | 24.41 |
| test-packages | 64 | 102077 | 72.42 |
| releases | 18 | 31941 | 34.20 |
| 合计 | 339 | 1018901 | 337.18 |

另外385个目录连接先非递归移除，不遍历其目标；随后只删除独立批准的旧产物根目录。回执中的targetPreserved字段表示unlink没有沿连接操作，并非保证目标目录永久保留：若目标自身也在旧产物清单，仍按它自己的批准路径删除。源码工作树/SDK不在清单。

主删除窗口E盘可用空间从806181445632升至991469101056字节，净增加 **172.56 GiB**。这是磁盘快照净变化，可能夹有后台I/O，不能把337.18 GiB逻辑合计当物理释放量，也不把本轮开始前其它清理算入。当前C/E空闲以外部 `disks-final.json` 为准。

## 保留内容

- 正式包及源码只保留2.0.7、2.0.8、2.0.9、2.1.0，共15个GitHub正式资产，全部与GitHub API资产SHA256核对，清理后再次复核。保存在 `E:\项目\Veyra\releases\release-<版本>-<日期>`；2.0.7源码文件名带test-r2仍是正式已发布资产，不能按名字删除。来源页：[2.0.7](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.7)、[2.0.8](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.8)、[2.0.9](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.9)、[2.1.0](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0)，对应源码页在 `formal-retention-assets.json`。
- 正式2.1.0两ZIP原样解出唯一活动供体 `E:\项目\Veyra\releases\release-2.1.0-20261019\runtime-base\NVIDIA`、`AMD`，manifest分别1728/2286文件逐一验证。当前五个接手/旋转验收、打包脚本改用此路径；旋转打包脚本显式携带接手报告，不依赖已清掉的rec2/Bilibili临时供体。
- 当前未合入正式版的rot1双包单独保留在 `E:\项目\Veyra\test-packages\codex-picture-rotation-20261023`。两ZIP与1739/2292个目录文件清理前后SHA256一致，冷启动后再次复核；不冒充正式发布。test-packages只剩这一任务目录。tests只剩本轮迁移回归的私有配置；verify已空。
- `releases\article-2.1.0-20261019` 和 `promo-2.0.0` 是宣传文稿/素材，保留，不是旧应用包。源码、101工作树、全部分支、SDK、downloads、logs、reports及原有archives保留；Lecram仍暂停。
- 旧包中独有的依赖对应源码按内容去重，保留 `archives\dependency-migration-cleanup-20261023\dependency-corresponding-source` 的2.0.0/2.0.3/2.0.6三个依赖源码ZIP，其余与正式保留源资产相同，不重复复制。许可和重建材料不因旧应用包被删而丢失。
- 32396份小型文本验收/脚本以原相对路径打包为 `archives\dependency-migration-cleanup-20261023\historical-test-evidence.zip`，199490970字节，全部条目SHA复核。73个历史模型路径均匹配保留的SDK/正式供体/当前交付中的模型，没有丢独有权重。所有个人盘外素材和真实配置保持。

## 验证、失败及边界

证据统一在 `E:\项目\Veyra\reports\dependency-migration-cleanup-20261023`，构建/回归日志 `E:\项目\Veyra\logs\codex-dependency-migration-20261023`。

1. `robocopy C:\veyra-deps E:\项目\Veyra\deps\core /E /COPY:DAT /DCOPY:DAT /XJ /SL /R:1 /W:1`；全量清单/目标SHA验证零差异，Qt另建连接。第一次收尾守卫把本机PowerShell的字符串Target当数组，创建兼容入口后停止，未删除旧备份；检查两根路径实际状态后修正字符串判断继续，旧备份成功删除，没有丢文件或中断依赖路径。证据 `deps-source-manifest.json`、`deps-verification.json`、`migration-result.json`。
2. `python -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-dependency-migration-20261023 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.0-rec veyra_qml_ui veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_repair_contract_tests`：现有同源增量构建通过，1.2秒，限-j2；不是全新全量编译。
3. Repair272检查零失败，UI契约（含384布局）通过，i18n2660条零失败；QtQuick109通过/零失败。第一次Quick控制台没提供文本，追加显式-o文件输出；文件使用Windows编码，UTF8读取失败，随后按mbcs读取同一份已通过日志，不改测试结果。NVIDIA/AMD旋转包各一次私有配置冷启动通过。qmake6.8.3可执行，四个代表性头文件/库/工具的C旧路径和E新路径 `os.path.samefile` 为真。脚本语法检查通过。没有产品代码/运行组件/驱动修改，不重新做无关画质或硬件验收。
4. 101工作树存在，清理前111份脏文件SHA匹配；此快照在本轮AGENTS更新之后取得，不能据此声称AGENTS未修改。与开工状态比较，只有本轮AGENTS、当前验收脚本和文档的预期变化，未发现其它源码变化。证明 `preservation-check.json`、`dirty-source-before-cleanup.json`、`worktrees-before.json`、`worktrees-after-cleanup.json`。
5. 旧 `reports\storage-cleanup-20261023` 的删除/恢复供体清单已被本轮保留策略取代，不再据旧测试包路径执行；该轮压缩证据仍保留。本轮未清理历史构建缓存/SDK/混有源码的C盘out目录整体，不能把它们当纯垃圾递归删。

补查C盘旧桌面仓库下 `veyra_preset_library_tests`、`veyra_qml_easing_tests` 两个生成构建目录，以及out下三个 `veyra-51-audio-test-20260917*.zip`：共409779162字节（0.38 GiB），已核验无Git跟踪，102份小型脚本/配置留证。但实际删除调用被自动审批 **blocked by policy** 拒绝，未给更具体原因，五项均原样保留，未换工具或移动绕过。`policy-rejection.json` 记录；供用户手动审阅的 `Cleanup-C-Remaining.ps1` 默认预览，-Execute仅限这五项；只做AST语法检查，Agent没有执行此手动脚本。

机器结果 `FINAL.json` 区分已完成和这五项受阻；不以计划代替执行事实。
