# 杜比视界 L1 动态元数据：交接文件

2026-11-11。写于动手之前，供恢复或换会话后继续。

## 项目状态

| 项 | 值 |
|---|---|
| 分支 | `test/merge-215`（基于 2.1.5） |
| HEAD | `d31ce5f3`（增强层开关提示） |
| 恢复点 | tag **`pre-dovi-l1-20261111`**（在 `d31ce5f3`） |
| 部署目录 | `D:\Program\Veyra-2.1.5-NVIDIA-win64-portable`（exe `471893b3…`） |
| 构建 | `cmd /c D:\DSH\.dsh-tmp\build214.cmd`（Ninja，增量约 2 秒） |
| 验证 | `powershell -ExecutionPolicy Bypass -File D:\Program\Veyra\src\scripts\verify-build.ps1 [-SkipBuild]` |

## 任务

把杜比视界的 **L1 动态元数据**（每帧 min/avg/max PQ）用于 HDR 输出的色调映射。

## 重要：L1 已经实现了一半，动手前先读

| 环节 | 状态 | 位置 |
|---|---|---|
| RPU 的 L1 解析 | ✅ 已做 | `include/veyra/source/DolbyVisionRpu.h` `extractDoviFrameInfo()` → `pipeline::DoviFrameInfo{minPq,avgPq,maxPq}` |
| 传到渲染层 | ✅ 已做 | `MediaFileSource` → `FramePacket::dovi` → `VideoPresenter::setFrameDovi()`（`latestDovi_`） |
| L1 应用到曲线 | ✅ 已做，**但只调了一个字段** | `include/veyra/engine/DoviCurveAdapter.h` `doviCurveFromLevel1()`，被 `VideoPresenter.cpp:281` 调用 |
| 开关 | ✅ 已做，**默认关** | `EnhancementSettings::doviFollow=false`；UI 在 ProPage「杜比视界跟随」 |
| 平滑 | ✅ 已做 | `VideoPresenter.cpp` 的 `avgEma_`/`peakEma_`（0.5 s 窗、场景切换重置）+ `smoothedStartNits_` |

**现在 `doviCurveFromLevel1` 只改 `highlightStartNits`（knee = peak*0.85，可选 avg 参与）**，其余字段保持用户静态值：
`peakCapNits`、`strengthPercent`、`midGrayNits`、`rollOffPercent`、`shadowLiftEv100`。

所以“做 L1”的准确含义是**补全**，不是从零实现：

1. `peakCapNits` 跟随 L1 的 `maxPq`（现在永远是用户静态值，场景峰值变化不反映）；
2. `strengthPercent`（压缩强度）按场景动态范围调整（高动态场景更强、低动态更弱）；
3. 可选：`midGrayNits` 参考 `avgPq`（libplacebo 的 `st2094_pick_knee` 思路是 knee 跟 avg，中灰不一定要动）；
4. 考虑把 `doviFollow` 的默认值改成开（需要用户确认，影响所有 HDR 片源的观感）；
5. 日志 `[source-dovi] decoded RPU observed; compatibility playback does not apply dynamic metadata` 有误导性（开关打开时是应用的），应改成说明“需开启杜比视界跟随”。

## 参考实现

- **quietvoid 的 libplacebo 补丁**（`utils/libav: derive scene brightness info from DOVI RPU`，gist `a3ef15d8556c3f346aca5c4e304f0b04`）：
  从 `AV_FRAME_DATA_DOVI_RPU_BUFFER` 用 libdovi 解析，把 L1 的 min/avg/max PQ 写进 `pl_hdr_metadata.max_pq_y` / `avg_pq_y`，供 tone mapping 使用。
  **Veyra 不需要 libdovi**：L1 已经从 `AV_FRAME_DATA_DOVI_METADATA` 解析进 `DoviFrameInfo`，缺的只是“喂给更多曲线字段”。
- **mpv 官方 issue #9761**：明确“mpv does not use any metadata from DV so far”——这块我们其实领先。
- **LibreELEC x86 DV 项目**（CroqueMr/libreelec-x86-DV）的 CB1 引擎：另一个完整参考。

## 关键代码位置

| 文件 | 作用 |
|---|---|
| `include/veyra/source/DolbyVisionRpu.h:223` | `extractDoviFrameInfo()`：从 `AVDOVIMetadata` 取 L1 |
| `include/veyra/pipeline/FramePacket.h:258` | `DoviFrameInfo`（`minPq/maxPq/avgPq`） |
| `src/source/MediaFileSource.cpp:1231` | `out.dovi = doviFrameInfo` |
| `src/engine/VideoPresenter.cpp:245-290` | L1 跟随：EMA 平滑 + `doviCurveFromLevel1` + `smoothedStartNits_` |
| `include/veyra/engine/DoviCurveAdapter.h:31` | **要改的核心**：只调 `highlightStartNits` |
| `include/veyra/engine/EnhancementSettings.h:390` | `doviFollow=false`、`doviFollowAverage=false` |

## 实施步骤（建议）

1. 在 `doviCurveFromLevel1` 里补 `peakCapNits`：
   - `out.peakCapNits = clamp(peakNits, ...)`，与用户值取 min（用户更保守时保留用户的）；
2. 补 `strengthPercent`：
   - 场景动态范围 = peakNits / avgNits；范围大时提高压缩强度，范围内小时回落；
   - 需要与用户值取 min/max，避免“跟随”覆盖用户意图（现有 knee 逻辑就是 `min(user)`）；
3. 每个改动都加 CPU 单测（`RepairContractTests` 已有 `doviCurveFromLevel1` 的用例）；
4. 平滑沿用现有 EMA，不要新开时间窗；
5. 改日志措辞（`does not apply dynamic metadata` → 提示需要开关）；
6. 跑 `verify-build.ps1`（567/462/342/172 + 部署一致 + 运行时 0 错误）；
7. 部署 exe（shader 若未改可不部署）；
8. 用 `FEL RPU_EL测试.mkv`（标准 P7，L1 完整）和 HDR10+ 测试片（`hdr10+test_lake_2021_02_01.mp4`）做实际观感验证。

## 验证与回退

- 验证：`verify-build.ps1` 全过 + 打开「杜比视界跟随」后 HDR 片源观感变化 + 日志无 ERROR。
- 回退：`git checkout pre-dovi-l1-20261111`（或只 revert 本次提交），重跑构建与部署。

## 已知风险

- `doviFollow` 默认关，改默认值会影响所有 HDR 片源的观感，**需要用户确认**；
- `peakCapNits` 跟随峰值可能让很亮的场景压得更狠（用户可能觉得“变暗”），必须保留用户静态值作为上限；
- L1 来自帧侧数据，HDR10+ 片源走同一个 `DoviFrameInfo`（`profile == kHdr10PlusProfile`），改动会同时影响 HDR10+ 的跟随逻辑，注意别把两条路径混在一起。

## 结论（2026-11-11 更新，本任务收尾）

L1 的解析与应用**已经按设计完成**，不需要补实现：

- `doviCurveFromLevel1()` **只跟随 `highlightStartNits`**；
- `peakCapNits` / `midGrayNits` **刻意不跟随**（它们是中灰 gamma 的锚点，跟随后实测某预设 1 nit 处丢 78%，`RepairContractTests` 的
  “leaves the user peak ceiling exactly as set” / “keeps the ceiling and mid-gray of a real preset” 锁住了这个行为）；
- `doviFollow` 默认关，用户在 ProPage 手动开。

本轮只做了用户选定的 **B**：把容易误解的日志改为
`decoded RPU observed; dynamic metadata (L1) drives the HDR curve only when the Dolby Vision follow switch is on (ProPage), otherwise the static curve is used`
（提交 `af1e99d1`，部署 exe `94317343…`）。

**A（默认开启）未做**：会改变所有 HDR 片源的默认观感，需要用户另行决定。
