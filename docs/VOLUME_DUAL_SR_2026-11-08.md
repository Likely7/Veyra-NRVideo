# 音量记忆与全后端双重超分（2026-11-08）

## 范围和交接

用户交给 Claude 的原始任务：音量每次重启回到 100%，以及让 DLSS SR、FSR 也能参与双重超分。Claude 在实现和主要验收后额度耗尽，Codex 阅读最新聊天记录后完成启动器、独立复核、提交和交付记录。

- 分支：`claude/volume-dual-sr-20261108`，基线 `0079ed9`，产品提交 `356013b6e7dec7e6200602ea9b997673e024206e`。
- 源码：`E:/项目/Veyra/worktrees/claude-volume-dual-sr-20261108`。
- Claude 原有 20 个修改文件逐 SHA256 对照接手快照，内容原样保存。本轮没有另改产品逻辑。
- 本任务未合 main、未推送、未发布。独立 NR 推理复用分支 `claude/nr-reuse-20261107` 保持原状态。

## 行为

### 音量

将音量保存到当前配置目录的 `qml-preferences.v1.json`，键 `volume`，范围 0–1。拖动结束 500 ms 后写入；正常退出时同步保存尚未到期的变动。启动先恢复引擎音量及 UI 快照，缺失、非法或越界的值不恢复，沿用默认 100%。静音仍为会话状态。

### 双重超分

流程为：源画面 → 基础超分 → 第一次结果 → 线性光面积降采样 → 第二次超分 → 原有后续效果。列表和节点共用 `SrStageEditor`，在超分下面开启“双重超分”，展开第二次算法、降低尺寸／百分比和目标尺寸。

两次可分别选择 DLSS SR、FSR 3.1、RTX 视频超分标准／高码率／流媒体，并独立选择 RTX 档位。两次 DLSS SR 共用界面所选 K/M/L 模型，但 GPU 后端实例及历史分开；两次 FSR 使用独立上下文。运动矢量按各次输入／输出合同缩放，重置、生命周期、显存估算及恢复检查同时覆盖两次。

继续执行各后端既有条件：DLSS/RTX 受 NVIDIA 能力限制；FSR 的原有 HDR 和静态图片限制保留；只有 RTX 视频超分提供单次“超分后缩回到”选项。基础阶段不需放大时仍可能按原策略复制输入，这类用例不能将阶段计数称为两次实际 AI 推理。双重超分增加耗时和显存，不保证画质必然提升，也不突破持续 GPU 吞吐上限。

普通单次 8K 规则保持。没有重跑历史上导致整机卡死的“8K＋NR＋VFG”组合。

## 配置兼容

第二算法为 DLSS SR／FSR 时使用新格式：设置预设 v36、预设库 v19、模式会话 v14；库／会话写入 `.dual-sr-any` 旁文件，旧版文件保留。原 RTX-only 设置保持既有格式路径。能力不支持的第二算法按实际可用后端修复或拒绝，不承诺旧版可读取新设置。

## 构建与验收

构建标签 `2.1.3-volume-dual-sr-test`，本机 RTX 5070，复用任务现有 build，无新增整工程副本。

最终命令：

```powershell
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task claude-volume-dual-sr-20261108 --root E:/项目/Veyra/worktrees/claude-volume-dual-sr-20261108 --label 2.1.3-volume-dual-sr-test --jobs 2 veyra_qml_ui veyra_video_ai_settings_tests veyra_effect_chain_tests veyra_preset_library_tests veyra_repair_preset_tests veyra_ui_contract_tests veyra_sr_quality_tests veyra_video_ai_export_probe veyra_qml_quick_tests veyra_ui_i18n_tests veyra_export_workflow_tests veyra_settings_transaction_tests
```

12 目标通过，干净产品提交增量构建 1.0 秒；初次编译由 Claude 完成，305.9 秒。最终回执 `E:/项目/Veyra/logs/claude-volume-dual-sr-20261108/build-20261108-023124/receipt.json`，主程序 SHA256 `91b93b45618d85bd119a6e5d8101f7315b36bb4415fc9766bb0a344c4a836d6b`，与先前实际验收程序一致。

Claude 已执行、Codex 核对记录的项目：

- GPU 双重超分 25 用例通过，包含不同后端组合、两种运动来源、不放大首段及 HDR FSR 拒绝；原 RTX 双重／超采样回归通过。
- 全套软件 QtQuick：172 通过、0 失败、1 跳过。跳过项为需显式图像输出目录的菜单栅格测试，Codex 后续单独传入目录执行成功并保存六张图；不将两次运行冒称一次完整 173/0。
- 真实程序 `sr-families`、`dual-sr` UI 夹具通过；设置事务、导出配置冻结与实际处理回归通过。
- 六组实际 graph＋NVENC 导出：DLSS/DLSS、DLSS/FSR、FSR/标准、标准/DLSS、高码率/FSR、FSR/FSR；每组 60 帧，720p→4K→1080p→4K。
- 音量 37% 保存并重启恢复；相关预设、合同、翻译检查通过。

Codex 独立复核：

- 设置与兼容 274 项零失败；效果链、预设库全通过。
- 真实程序不打开媒体：改到 23% 并立即退出（500 ms 定时器未触发），仍写入；重启恢复 23%、静音不继承；越界存储值 2 不恢复。上述启动保持 QML 编译缓存开启。
- 六组既有导出重新逐帧与源相邻帧比较，360 帧中 0 帧错位。此检查只能支持帧对齐，不能证明所有内容都无伪影。
- 修正测试启动器，实际 cmd 启动、独立配置建立及定时退出通过，无 engine ERROR；恢复过所有测试注入的 Main.qml。

没有 AMD 实卡、本轮所有型号或所有素材的画质验收证明。

## 失败记录

Claude 曾遇到旧测试仍限定 RTX-only／旧档位默认值，修正断言并重跑通过；导出冻结夹具首次误用 720p 而要求 1080p，换正确输入后通过。启动器原先存在控制字符和 cmd 引号／工作目录问题，现已修正并实际启动验证。

Codex 首次运行设置测试提前创建 scratch 目录，测试按合同退出 2；改传不存在的目录后 274/0，通过前的调用日志保留。这些属于验收调用／断言问题，不作为产品修复宣传。

## 交付位置与复测

- 人工入口：`E:/项目/Veyra/tests/claude-volume-dual-sr-20261108/启动音量与双重超分测试.cmd`。
- 程序：同目录 `app/veyra_qml_ui.exe`；配置：`manual-profile`；日志：`E:/项目/Veyra/logs/claude-volume-dual-sr-20261108/manual/engine.log`。
- 构建：`E:/项目/Veyra/build/claude-volume-dual-sr-20261108`。
- 报告与命令：`E:/项目/Veyra/reports/claude-volume-dual-sr-20261108`，含 `FINAL.json`、接手快照、Claude 输出摘录、Codex 回归和启动器结果。
- 原 UI 及私有配置试验：报告目录 `run.py`；`stage` 会重建 stage，不应对正在使用的目录执行。`codex_verify.py` 使用全新私有目录，复用时应更换任务测试后缀，不能删除真实配置来凑通过。

测试 stage 从正式包提取运行组件，再覆盖此分支主程序／QML／shader；不新增运行组件、不修改运行库或驱动。保留一份当前可用 stage、构建和必要证据，没有另外压缩成整套重复测试包。
