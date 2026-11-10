# 升级到 2.1.4（定制版 + 作者版双切换）

2026-11-10。作者发布 2.1.4，包在 `D:\下载\DLSS5\Veyra-2.1.4-NVIDIA-win64-portable`。

## 结果

- 定制分支：`test/merge-214`（`4bc50a2d`），在 `test/merge-213` 基础上 merge `v2.1.4`（`560cf8ea`）。
- 部署目录：`D:\Program\Veyra-2.1.4-NVIDIA-win64-portable`。
- 定制 exe：`700fc56004d356cebc01ff616bf45bcf66a03adde5936c3252699487c74b4726`（Ninja 构建）。
- 作者 exe：`f3201556663aca24880e172d0155207e951f4a212f59c449c495a84a8a9a750f`（与作者包一致）。
- 双切换实测：作者版 ↔ 定制版均启动成功，当前停在定制版。
- 启动验证：退出码 0，日志无 QML 错误，PE 2.1.4。

## 上游 2.1.4 更新（2.1.3..v2.1.4，28 个提交、107 文件）

采集卡音频转接（可选 VB-CABLE 虚拟扬声器）、双重超分混搭（DLSS SR / FSR 3.1 / RTX 视频超分，两次独立上下文）、NVIDIA DLAA、HDR 广色域恢复（DLAA/DLSS SR/FSR 共用）、NR 推理复用（自动 / 1/2 / 1/3 / 1/4，光流对齐）、帧率卡提交 FPS 与 1% low、色温冷端平滑、音量记忆、录制快捷键过滤长按、RTX 视频超分选择按 QML 内容隔离缓存、AMD lmxxf NR 子进程 HIP 预检。

## 合并与冲突

6 个冲突文件：

| 文件 | 处理 |
|---|---|
| `i18n/catalog.json` | 脚本按 `(zh, ctx)` 合并（v2.1.4 优先、本地补充），extract.py 重建：3025 条，占位符问题 0，缺译 190（B 站文案与新增项，回退显示原文） |
| `include/veyra/ui/QmlPlayerBridge.h` | 上游新注释 + 本地 5 个 Q_PROPERTY（自动补帧倍数、DV 跟随/P7 合成/平均） |
| `src/engine/PresetLibrary.cpp` | 版本号重新分配，见下 |
| `tests/unit/PresetLibraryTests.cpp` | 取上游的 `ScratchDirectory`（每次运行独立临时目录） |
| `AGENTS.md` / `docs/CURRENT_STATUS.md` | 保留两边（上游 2.1.4 段在前、本地记录在后） |

### 预设库与会话版本号（再次重排）

上游 2.1.4 又把 **库 v19 / v20**（DLSS/FSR 第二次超分、DLAA）和 **会话 v14 / v15** 占了，本地「自动补帧倍数」从 19/14 再让位：

- 库：`autoFg ? 21 : dlaa ? 20 : anySr ? 19 : videoAi ? 18 : …`
- 会话：`autoFg ? 16 : dlaa ? 15 : anySr ? 14 : videoAi ? 13 : …`
- 解码上限：`version > 16`
- 内嵌映射：`version>=16?21 : >=15?20 : >=14?19 : >=13?18 : >=12?17 : >=10?16 : >=9?14 : >=8?13 : 11`
- 扩展名：库 v21 与会话 v16 写 `.auto-fg`；load 在 `.dlaa`、`.dual-sr-any` 之后检测 `.auto-fg`（优先级最高）。

顺带修正上游映射的一处缺口：上游 `version>=15?20:version>=13?18:…` 没有「会话 14 → 库 19」，这里补上。

影响：2.1.3 的本地 v19 存档按 19 解析（autoFg 字段从 v16 起就写），保存时升到 21 / `.auto-fg`；旧文件不覆盖。

## 构建

`D:\Program\Veyra\build`（Ninja，增量），`cmake --build ... --target veyra_qml_ui -j 8`，27 个目标，无警告（`/W4 /WX`）。

合并后曾出现两处大括号不匹配（`PresetLibrary.cpp` 的库/会话 save 收尾、`QmlPlayerBridge.h` 残留冲突标记），已修正并编译通过——记录在此，供下次合并参考：冲突块替换要连 `<<<<<<<` 标记一起替换。

## 部署与验证

- 部署结构同 2.1.3：`.orig`（作者）/ `.custom`（定制）、`qml.orig` / `qml.custom`、两个 `.cmd`、`版本切换说明.txt`。
- 着色器差异 2 个：`PresentBlit_ps.dxil`、`YuvToLinearRgb.dxil`（各有 `.orig` / `.custom`）。
- 双切换：作者版 `f3201556…`、定制版 `700fc560…`，脚本均 rc=0 且启动成功。
- 数据目录：`user-data-2.0.0-p5` / `user-data-2.0.0-author`，从 2.1.3 迁移只需复制旧目录。

## 未验证

- 上游 2.1.4 新功能（VB-CABLE 采集音频、DLAA、NR 推理复用、混合双重超分、1% low）未实测；
- 真实采集卡、AMD 实卡未验；
- 本地定制功能（B 站、DV P5/P7、字幕、缓冲门控、自动补帧倍数）只做了启动检查，未逐项回归。

## 其它

- PR #80 未随本次升级更新（用户要求 PR 只放代码；如需刷新，从 `test/merge-214` 剔除文档后推送）。
- 旧 2.1.3 部署目录保留。
