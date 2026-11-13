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

当前任务的工作目录、日志和TEMP均在E盘。不要复制整个仓库作为备份，不为每个用例永久复制完整运行包。

## 构建

从施工树运行现有入口，复用已验证的MSVC/Ninja及依赖参数；此脚本不下载依赖：

```powershell
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-main-claude-anr-20261113 --root 'E:/项目/Veyra/worktrees/main-claude-anr-20261113' --build-dir 'E:/项目/Veyra/build/codex-main-claude-anr-20261113' --reconfigure --label 2.1.5-main-integration-test --jobs 2 veyra_qml_ui
```

后续任务更换`--task`，保持`codex-`或`claude-`前缀；构建目录只有在准备独立配置时才新建。首次/变更CMake或QML资源清单时传`--reconfigure`。构建限并发2、≤900秒；无需修改全局PATH/TEMP。

脚本使用`logs/pr64-avermedia-20261019/build-20261019-094942/receipt.json`的固定依赖参数，因此该小型回执是活动配置输入，不能当旧日志随手删。FFmpeg使用包含PS5 slices256、dav1d和压缩PGS zlib的已验证树；保留其对应源码与许可证。

## 验证

- 先构建产品与改动相关目标，再运行相应unit/contract/QML/实际启动检查；每进程≤300秒。
- 桥接/QML结构变更应覆盖UI合同、翻译、QtQuick、设置/预设保存恢复、采集对话框与实际私有配置启动，确保无绑定/创建错误。公有QML属性、方法、信号及配置格式保持。
- 处理图、补帧、音频、导出发生行为变更时，再加对应GPU/输出检查。没有相关变更不要用重复全量测试代替推进。
- 产品使用任务共享的测试运行目录；更换EXE/QML时先创建新文件再替换，避免通过硬链接覆盖正式供体原件。运行库/模型原字节不改。
- 从历史包/旧测试目录继承运行组件后，必须用本次实际build的着色器同步并校验，不能继承供体的旧shaders当本轮产物。目标程序退出后执行 `py -3.11 -X utf8 -B -I scripts/acceptance/sync-built-shaders.py --build-dir <本次build> --app-dir <本次app> --apply --report <E盘本任务回执>`，再去掉`--apply`独立检查，所有build着色器SHA一致才可验收/交付。正常package-qml-release.py本来会复制build/shaders；手工私有stage也必须遵守。
- 不使用真实用户配置，不自动连接PS5/PC/Xbox或采集卡，不停止他人进程。实卡验证边界如实写明。

本轮实际命令和测试结果继续记录在[执行记录](PROJECT_MAINTENANCE_EXECUTION_2026-10-27.md)和`E:/项目/Veyra/reports/project-maintenance-execution-20261027`；历史通过不自动等于当前通过。

## 任务收尾

记录代码与文档变更、构建/测试、已验与未验、保留的运行供体、待清理项和空间变化。合并后关闭已完成的工作树/分支；只保留main、正在做的任务和明确暂停项。历史资料从[索引](history/INDEX.md)查找；旧路径见[映射](history/PATH_MAP.json)，退休环境见[分支恢复](BRANCH_ARCHIVE.md)。

push、Release、资产上传和代发消息仍按用户当前请求授权；本轮只本地整理与整合。
