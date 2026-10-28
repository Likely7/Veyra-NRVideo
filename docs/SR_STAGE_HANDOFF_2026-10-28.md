# 单层三段式超分接手记录（2026-10-28）

> 后续用户在8K最终输出＋NR＋VFG组合遇到整机卡死；本记录仅保留当时短测事实，不能作为该组合稳定的依据。当前测试目录已更新为防护版，证据、限制与验证见[整机卡死后续记录](SR_STAGE_FREEZE_2026-10-28.md)。

用户要求接手 Claude 未完成的超分功能。已阅读其对话与现场改动，按最后确认的范围完成：**单层、RTX 视频超分，先降到 → 超分到 → 再缩到**。没有把本轮扩成多层、多次 AI 推理、DLSS/FSR 三段式或细节混合。分支 `claude/sr-stage-20261028`，基线 `7b0b64b`；本轮不合并 main、不推送、不发布。

## 功能与使用

- 专业列表的超分卡片、节点模式的超分节点共用 `SrStageEditor.qml`。
- 先降：关闭、720p、900p、1080p、1440p，或手动50–90%（滑条5%步进，松开提交）；按片源比例处理。
- 超分：保留2K、4K、5K、6K、7K、8K目标。再缩：不缩、片源尺寸、2K至7K；无效尺寸组合给出原因。
- 两个例子：糊4K → 先降1080p → RTX视频超分4K；原生2K → RTX视频超分8K → 再缩4K。
- 界面显示实际已应用的尺寸；预设、模式会话、独立导出传递这两个新参数。非RTX视频超分后端禁用两行，并保留保存值供切回。
- 默认关闭先降、再缩，保持原单步规则。新功能会丢掉先降阶段被过滤的原生细节；HUD、字幕或清晰4K素材不保证改善。

## Claude 已完成的部分

设置与尺寸规划、处理图资源和GPU pass、预设格式、桥接/QML、翻译、图片A/B及首轮测试已写好，但尚未提交和收尾。原方案与原测试报告保留于 [方案](SR_STAGE_PLAN_2026-10-28.md)、[Claude实施报告](SR_STAGE_REPORT_2026-10-28.md)。原报告的GPU耗时、PSNR/SSIM是Claude当时的数据，不能当成本轮独立性能测量。

接手前33个改动文件及binary patch按SHA保全至 `E:/项目/Veyra/archives/codex-sr-stage-handoff-20261028/claude-uncommitted.zip`，已逐项复核。起点标签 `checkpoint/pre-codex-sr-stage-handoff-20261028`。其他分支及用户数据未改。

## 接手修复

1. **NR先行与缩小后输出的衔接**：旧全局校验要求NR尺寸不超过最终工作尺寸。新流程能把4K片源最终输出为2K，但先行NR仍在4K处理；因此合法组合被拒绝。新增真实GPU边界测试先复现 `invalid NR extent`，修为按 `desc.nrFullExtent(0)` 校验，不降低NR分辨率、不绕过每层尺寸校验。修复后同一组合6帧实际处理通过。
2. **导出进程协议**：`EnhancementSettings`追加字段改变共享内存布局，独立导出协议由11升级为12，防止旧worker按旧布局解释新参数。真实D3D12 NVENC导出验证冻结值、实际SR尺寸和完整帧数。
3. 补充24个RTX视频超分尺寸/质量组合、NR先行、最终尺寸小于源、非整数面积缩回的GPU测试；真实界面验收覆盖节点模式。

界面测试首次扩展误以为进入节点模式会带入列表的SR；产品合同实际是首次进入空图。已保留失败回执，修正测试夹具先通过 `addEffect("sr")` 添加节点后复验，不修改产品的空图行为。第一次只终止本轮明确识别的测试进程，没有停止用户软件。

## 本轮验证

复用 `E:/项目/Veyra/build/claude-screen-recording-20261022`，MSVC/Ninja并发2，每次构建≤900秒；测试每进程≤300秒。主要命令：

```text
python E:/项目/Veyra/tmp/claude-sr-stage-20261028/build/build.py veyra_qml_ui veyra_sr_quality_tests veyra_export_workflow_tests
python scripts/acceptance/sr-stage-ui.py --task codex-sr-stage-handoff-20261028
python scripts/i18n/extract.py --check
```

其余定向测试由 `reports/codex-sr-stage-handoff-20261028/run.py` 启动；准确参数、退出码、耗时在同目录JSON，日志在 `logs/codex-sr-stage-handoff-20261028`。

| 检查 | 本轮结果 |
|---|---|
| EffectChain、RepairPreset、PresetLibrary、RepairContract、UI i18n、QtQuick | 六组退出0；契约272项0失败，翻译缺失0 |
| `stage-boundaries` | 修复前失败、修复后退出0；源4K先行原生NR/最终2K，及非整数缩回实际处理通过 |
| `stage` | SDR先降面积参考、SDR/HDR再缩、HDR底图/亮度检查退出0 |
| `stage-capabilities` | 24个组合通过：质量1–4，每档5K/6K/7K/8K后缩4K、720p→4K、21:9 |
| 原有单步SR集成测试 | 退出0，覆盖原尺寸视频修复、质量差异、前置调色、DLSS与FSR路径 |
| 独立导出 | 两个真实NVENC任务各4源帧/4编码帧；输出分别2560×1440、3840×2160，ffprobe独立检查 |
| 实际界面 | 列表/节点操作、手动比例、无效项提示、后端切换、关闭恢复；最终回执见FINAL.json |

GPU四组首次runner使用同一stdout日志名，前几组stdout被后组覆盖；各组退出码/末尾标记保留在 `gpu.json`，各组独立GPU事件日志仍在对应tests目录。runner已改用用例序号，不重复运行测试来填补旧输出。

## 交付与边界

共享测试运行目录：`E:/项目/Veyra/tests/claude-sr-stage-20261028/app`，可运行其中 `veyra_qml_ui.exe`；本轮替换EXE/QML时先创建新文件，避免写穿正式供体硬链接。最终确认运行目录组件与供体manifest一致、测试注入的Main.qml已恢复。

本轮未占用采集卡或连接真实主机，未重做显存峰值、长时动态画质、端到端延迟、main二进制逐像素对照，也未扩展为全显卡支持。Claude图片A/B表明有些2K→8K→4K裁切过锐较轻；这是有限样本，不能承诺所有老游戏/电影变清晰。先降导致的原生HUD细节损失保留明确提示。

最终机器回执：`E:/项目/Veyra/reports/codex-sr-stage-handoff-20261028/FINAL.json`。当前正式2.1.1包和GitHub Release保持不变。
