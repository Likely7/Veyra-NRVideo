# DLSS 4.5 SR 接手结果（2026-10-11）

已完成 Claude 未完的 DLSS SR 模型选择接入。保留 K 默认，增加 M、L；列表和节点页都可选择，可随列表/节点预设、独立模式会话和节点草稿保存、恢复。使用当前已批准的 DLSS 310.7 原件，不下载或修改 DLL。实际画质由用户比较，本报告不宣称画质或性能提升。

## 基线与改动

- 本机 Claude 会话 `56393e4a-57bd-464c-a461-310bf74a3ad2` 最后用户要求只做新超分接入、实际画面由用户测试；Claude 留下9份未提交文件。原树 `E:/项目/Veyra/worktrees/dlss45-sr-20261011` 保持原样。
- 独立分支 `codex/dlss45-sr-handoff-20261011`，基线 main `049902bd941785d63199b025bb5b414da50bd53f`。原补丁、9份文件与57个旧工作树/504份脏文件身份封存于 `E:/项目/Veyra/archives/dlss45-sr-handoff-20261011`；复用并核验 Claude 已有 source-before.bundle，不复制完整工程。
- 共享 settings → chain globals → graph → NGX 传递模型。六个 unsigned render-preset hint 明确设置为 K=11 / M=13 / L=12，模型变更走原有图重建。关闭 SR 或使用 RTX Video SR/FSR 时，未生效的模型编辑不会要求重建。拒绝非法模型值。
- 完成旧 PresetStore v31、PresetLibrary v12、ChainSession v7 编解码；K 默认仍可使用旧格式。新模型库/session 写入 `.dlss-sr` 旁文件，保留旧文件；回到 K 后继续更新同一旁文件，防止下次错误恢复旧 M/L。
- 专业列表与节点页增加「DLSS 模型」选择。仅新增两条文案的简中/繁中/英文/日文翻译，不删除旧翻译。没有改 UI 绘制频率、Timer、GPU 优先级、NR、FG 或其它超分算法。独立 VFG mixed 分支未整合。

## 构建与验证证据

所有产物位于 `E:/项目/Veyra/{build,tests,logs,tmp,archives}/dlss45-sr-handoff-20261011`。

构建命令：`python -B -I E:/项目/Veyra/tmp/dlss45-sr-handoff-20261011/build.py`。按改动头文件重编完整依赖闭包34项，并链接GUI及相关测试9个EXE；保留 DeckLink SDK 与现有 PS5 slice / dav1d / subtitle FFmpeg。成功构建 `build-20261011-204653` 用时48.7秒；修正测试夹具后的最终构建 `build-20261011-205014` 用时19.4秒，产品源未再变化。这是复用已核验主线对象的增量构建，不冒称 clean 全量重编。

最终 GUI：`E:/项目/Veyra/tests/dlss45-sr-handoff-20261011/app/veyra_qml_ui.exe`；显示版本 `2.0.6-DLSS45-test-20261011`；SHA256 `b064968ae42b1bb21175b137c9d1ceebc4915fc34acfd6e3e492440ccd6afe43`。

| 验证 | 结果与证据 |
| --- | --- |
| 共享设置、激活/关闭模型编辑、图与导出参数合同 | `harness/regress.py chain contracts` 通过，日志 `dlss45/tests-20261011-204842` |
| 旧配置及 K/M/L 编解码 | 同轮 `presets` 通过；`library` 最终复测通过，日志 `dlss45/tests-20261011-205141`。包含列表/节点/草稿独立模型、预设应用、导入导出、旧文件保护和无效数据原子拒绝 |
| RTX 5070 实际 NGX | `regress.py models`，用户 GTA 原4K素材第12秒起降至1080p，1080p→4K，每模型32个连续移动帧。三次 Create、96次 Evaluate、三次 Release 均返回 `0x1 / Success`，SEH=0；三个模型各输出 f1/f8/f32 PNG |
| 输出完整性 | `verify-output.py` 检查9张 PNG 均3840×2160、非黑且非恒定；`models/output-integrity.json` 保存像素统计与SHA。人工查看 M-f32 仅确认有实际画面，不据此评价画质 |
| 正常 GUI 的列表/节点入口 | `ui-smoke.py` 在私有运行目录临时注入 QML 驱动实际生产 bridge；切 M→L、保存/恢复列表M、连接节点SR、切L→K并恢复节点L、回到列表M全部完成。`ui-20261011-205634/selection`，30.3秒，实际1080p→4K、SR active |
| 重启会话 | `ui-smoke.py restart`，`ui-20261011-205851`，5.2秒；恢复列表M及独立节点L、两个预设，并回到列表M。生产 Main.qml 字节已恢复，测试 Loader 已移除 |
| 原件与工作区保护 | 测试前后 donor 1626文件SHA全部不变；`protect.py verify` 逐项核对原57树/504脏文件、Claude patch和9文件原件通过。新增文件均为文本源码/文档，不含SDK、DLL、模型或媒体 |

当前 DLSS runtime：310.7.0.0，58,977,904字节，Authenticode Valid，SHA256 `BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E`。测试运行目录复用现有原件硬链接；EXE/QML/shader替换使用新文件原子置换，未向原件硬链接写入。

## 失败记录与限制

1. 第一次构建失败于新增测试误把返回 void 的 `PresetLibrary::apply` 当作带 accepted 字段的结果；已修正夹具、重建。第一次 library 回归误用默认禁止覆盖的 `put(entry)` 更新已有条目；改为明确 `put(entry,true)` 后复测通过。失败回执保留，原批次不标为全通过。
2. 初次 GUI restart 驱动等待 `applying=false`，但无片源时有待应用设置，所以停在驱动 state0；这不是模型丢失。驱动改为无片源时直接核对会话，独立重试通过，产品未因此修改。
3. GUI 首次切至 K 时，Create耗时8.84秒；8.14秒处现有看门狗报告无进展，栈位于 `nvptxJitCompiler64.dll` / NVIDIA object loader，随后 Create成功、画面恢复、后续L/M切换与退出正常。说明本次驱动冷编译可造成停顿及误导性的无响应提示；没有隐藏诊断或提高超时来制造通过，也不能据此保证所有冷启动快速。最终供用户测试以 M 启动。
4. 接口返回成功证明 preset hint 被接受并产出画面，无法读取未公开的运行库内部模型状态。其它显卡、真实采集卡、HDR、长时间显存稳定性及主观画质未在本轮验收；本轮未做性能对照，不报告提速百分比。

## 交付方式

正常启动私有 app，使用 `--data-dir E:/项目/Veyra/tests/dlss45-sr-handoff-20261011/profile --page pro` 和现有 `gta6-1080p30-60s.mp4`，当前列表 M、4K目标、光流运动输入，仅SR开启。配置与缓存独立，不更改用户原配置。启动及最终保护回执见本轮 `logs` 下的 `launch` / `final.json`。

没有合并 main、push、打包、发布或关机。后续如用户认可画质，再按用户授权整合。
