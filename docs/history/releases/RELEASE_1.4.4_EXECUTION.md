# 1.4.4 正式发布执行记录

2026-09-22 用户确认本地测试包可用，要求"发布 1.4.4 到 GitHub、写好更新报告、README 的更新日志只保留
最新的、老版本部分删掉、细节看 Release"，并授权推送与发布。发布后用户指出首次改写把 README 的介绍、
功能表和使用教程一并删掉了（本意只是更新日志只留最新），已在 `b5821cc` 从 `d93864f` 找回全部正文，
只保留 1.4.4 更新日志。

## 源码与分支审计

- 公开基线 `v1.4.3` = `05998da`（`checkpoint/pre-144-merge-20260922`）。
- 本次发布源码提交 = `6851c27`，发布范围 `05998da..6851c27` 共 65 个提交、150 个文件
  （+10977 / −720），新增 blob 最大 0.74 MB（WORKLOG），**无 SDK、无运行库、无凭据、无测试媒体**。
- 集成点 `6c72c35`（merge 1.4.4 repairs: capture, frame generation, presentation and UI）。
  以下修复分支全部已并入 main，逐个用 `git merge-base --is-ancestor` 核对，不按分支名推断：

| 分支 | 内容 | 已并入 |
| --- | --- | --- |
| codex/playback-nr-20260920 | 播放/NR 回归（帧同步前置、字幕、设置） | True |
| codex/capture-color-144beta-20260920 | 采集颜色/范围与 Elgato MK.2 HDR | True |
| codex/5090-capture-fg-20260921 | 采集侧补帧延迟与呈现 | True |
| codex/fg-stability-20260921 | 补帧历史/闪烁修复 | True |
| codex/fg-utilization-20260921 | 输出上限接入生成量 | True |
| codex/bounded-full-chain-20260922 | 有界全链路（HDR 查询、采集格式身份） | True |
| codex/fg-independent-repair-20260922 | 统一修复第 1–10 批 + 帧同步重做 | True |

- 本轮新增两个发布前提交：`d93864f`（严格补帧节奏行重排、滚动提交重绘 + 新回归测试）、
  `6851c27`（1.4.4 更新报告、交流群二维码、README 更新日志换成 1.4.4）。发布后追加 `b5821cc`：
  找回被误删的 README 介绍/功能表/使用教程，更新日志仍只保留 1.4.4。
- 仓库内 `%SystemDrive%/`（一个空目录树，非本版产物）移出到
  `E:/项目/Veyra/tmp/junk-systemdrive-20260922`，使源码打包要求的 clean worktree 成立；
  未删除任何文件。

## 构建、包与来源

- 构建目录 `E:/项目/Veyra/build/release-144-20260922`：Ninja + MSVC 14.44，Release，
  `VEYRA_DISPLAY_VERSION=1.4.4`、`VEYRA_ENABLE_D3D12_DEBUG=OFF`、`VEYRA_ENABLE_REMOTEPLAY=ON`，
  FFmpeg 根 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed`（含 PS5 H.264 slice 补丁与 dav1d）。
- 发布 EXE `Veyra.exe` SHA256 `92AC81B14AB050895B33FD2A822C179F65C4A42BD89C6C00772B89C5684D7121`
  （1.4.4，13 662 720 字节），与用户已验收的 test6 包内 EXE 完全一致。
- 便携包：`scripts/package-portable.ps1 -Version 1.4.4`，输出
  `E:/项目/Veyra/releases/1.4.4/final/Veyra-1.4.4-win64-portable.zip`，
  472 478 966 字节，SHA256 `BFF5149B55F8665FEBEAECEEE421956DF72370FA56515A013EE6ABBD45B8878B`。
  12 个增强运行文件与 1.4.3 身份相同（逐项哈希/签名核验通过），含逐文件
  `release-runtime-manifest.json` ×3 与 `package-manifest.json`；载荷 129 项。
  说明：build 目录原本缺少 FFmpeg DLL，按 `scripts/build.ps1` 的既有步骤把 6 个 DLL 复制到构建目录后打包。
- 对应源码：`scripts/package-release-source.py`，从 clean HEAD 归档并回读校验 919 个文件，
  合并经 SHA256 核验的 1.4.1 FFmpeg / RemotePlay 对应源码 ZIP（依赖未变，文件名保留原版本）。
  `Veyra-1.4.4-source.zip` 215 437 386 字节，SHA256
  `7d3a8efed76422eab536d714221f579f1478921b927149fbb5adffa8635c53ba`，记录提交 `6851c27`。

## 本地正式包验收

- 便携冒烟 `scripts/acceptance/portable-smoke.ps1 -CaseSeconds 7`，
  输入 `loop/local/fixed_clips/test_av_1080p.mp4`，输出
  `E:/项目/Veyra/tests/release-144-20260922/portable-smoke-final/`：**7/7 PASS**，
  `passed=true`、`exeHash=92AC81B1…`，逐例 exit=0 且截图生成：

| 用例 | 处理帧 | 生成帧 | 秒 |
| --- | --- | --- | --- |
| empty | 0 | 0 | 7.5 |
| baseline | 344 | 0 | 7.7 |
| community-sr-nr-fg | 197 | 4 | 8.0 |
| dlss-sr-nr-fg | 199 | 3 | 8.0 |
| video-sr-nr-fg | 223 | 6 | 8.1 |
| fresh-defaults | 344 | 0 | 7.7 |
| ampere-nr | 227 | 0 | 7.9 |

  脚本在收窄 PATH 并临时移走 manifest 的情况下仍要求所有依赖来自包内目录，验证后 manifest 已还原。
- **一次失败与更正（保留记录）**：首次用 4K 素材 `p001.mp4` 运行，`video-sr-nr-fg` 断言失败
  （`gpuSrP95Ms=0.000`，无 `[video-sr]` 日志）。原因是该用例要求"源分辨率小于 4K 目标"，
  4K 输入时超分被直接旁路（1.4.3 记录里也写明"实际 SR 被原生 4K 旁路"），不是产品回归；
  改用标准 1080p 夹具后 7/7 通过。该次产物移到
  `E:/项目/Veyra/tmp/portable-smoke-4k-input-attempt/`。
- 受影响门槛测试：`veyra_settings_layout_tests`（新增，0 失败）、`veyra_slider_reset_tests`、
  `veyra_control_paint_tests`、`veyra_ui_contract_tests`（384 用例）、`veyra_repair_contract_tests`（205 项）
  全部 exit=0。补帧调度逻辑本轮未改动。

## 正式发布完成

- 推送：`git push --atomic nrvideo main refs/tags/v1.4.4` 成功，只推送 main 与本标签，不推送其他历史分支。
  远端 `main=6851c27`，签注标签 `v1.4.4=9e21d18 → 6851c27`。
- Release：`gh release create v1.4.4 --verify-tag --draft --notes-file docs/RELEASE_NOTES_1.4.4.md`
  上传 4 个资产，核对 body 与本地 notes 完全一致（5726 字符），随后
  `gh release edit v1.4.4 --draft=false --prerelease=false --latest` 发布。
- 发布地址：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.4 ，发布 ID `393860692`，
  发布时间 2026-09-22T15:23:44Z。
- 远端资产（state=uploaded，size/digest 与本地一致）：

| 资产 | 大小 | SHA256 |
| --- | --- | --- |
| Veyra-1.4.4-win64-portable.zip | 472 478 966 | BFF5149B55F8665FEBEAECEEE421956DF72370FA56515A013EE6ABBD45B8878B |
| Veyra-1.4.4-source.zip | 215 437 386 | 7D3A8EFED76422EAB536D714221F579F1478921B927149FBB5ADFFA8635C53BA |
| 两个 .sha256 | 98 / 90 | 随包提供 |

- 发布后核验：`releases/latest` 返回 `v1.4.4`（draft=false、prerelease=false）；
4 个下载地址与两个二维码图片地址全部 HTTP 200；远端 `README.md` / `README_EN.md` 的更新日志只保留 1.4.4
（介绍、功能表与使用教程按用户更正在 `b5821cc` 恢复），
  正文两个固定二维码区块各 `width=220`（赞助码沿用 1.4.0 资产，交流群码为 `docs/images/1.4.4/community-group.png`）。

## 遗留与未验收

- 交流群二维码 7 天内（2026-09-29 前）有效；过期后需要重新进入更新，届时只改图片与引用，不改资产。
- `E:/项目/Veyra/tmp/` 下留有两处中间产物：`release-144-partial-stage/`（首次打包失败时的半成品）与
  `junk-systemdrive-20260922/`。本轮环境对递归删除有自动审批拦截，未强行绕过，改为移动到 tmp，
  待用户清理指令处理。
- 真 1080p→4K 超分＋NR 的 DLSS 4X/6X 长间隔、XeSS 4X 重负载连续性、独立输入降噪、
  XeSS/FSR 提供方输出限帧、杜比视界 P5 仍未解决；实卡、多显示器、HDR 屏观感与
  input-to-photon 延迟仍由用户验收。发布正文已逐条写明，不作为已修复宣称。
