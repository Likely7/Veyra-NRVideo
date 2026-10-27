# 0.0.4 发布执行记录

状态：已发布。用户 2026-09-11 明确要求当前版本更新到 GitHub，版本号 0.0.4。发布至 `Likely7/Veyra-NRVideo`，旧版标签和资产未改。

源码提交 `cecf34e1f88ea3538f650ef38e46e26c56ef469d` 与注解标签 `v0.0.4` 已原子推送。Release ID `386843250`，公开时间 `2026-09-11T07:09:56Z`，`draft=false`、`prerelease=false`，latest 接口确认 `v0.0.4`：[发布页](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.4)。四个附件均 uploaded，远端 digest/大小与本机相同，Release 正文与版本说明一致；证据 `logs/release-0.0.4/github-release-published.json`。

发布命令：`git push --atomic nrvideo HEAD:main refs/tags/v0.0.4`；`gh release create v0.0.4 --repo Likely7/Veyra-NRVideo --verify-tag --draft --title "Veyra 0.0.4" --notes-file docs/RELEASE_NOTES_0.0.4.md`；`gh release upload` 上传两个 ZIP 及各自 SHA 文件；核验后 `gh release edit v0.0.4 --repo Likely7/Veyra-NRVideo --draft=false --latest`。未向旧 origin 仓库发布。

源码暂存检查24个文件，无二进制/SDK/模型/运行目录新增；已跟踪文件和历史名称检查未发现运行 DLL 或模型对象，历史 `third_party_local/depth/manifest.json` 是2118字节的旧文本清单，不是模型。推送后先核对两份 README 的 Git blob 与发布提交一致。上传期间用户在 GitHub 新增 README 提交 `51eb18d`，已快进同步并保留；不覆盖用户改动、不重打包、不移动版本标签。以下发布后记录仅改文档。

## 范围

收录文件与采集的软件处理延迟同步修复，以及 NR 波动、XeSS 帧覆盖引起的音频停顿修复。具体实现、原先失败和针对性回归见 [音画同步修复记录](../reports/SOFTWARE_AV_SYNC_REPAIR_2026-09-11.md)。不修改导出完整性检查，不把持续 GPU 过载或采集卡共同输入延迟当成本轮已经消除的问题。

同步 CMake/EXE 版本、中英文 README、更新说明、组件清单及构建文档；保留 README 开头自动播放的 GIF。FFmpeg 源码打包脚本改为显式要求版本参数，避免沿用旧版本号。便携测试显式加载执行宿主的 PowerShell Utility 模块，修复跨 PowerShell 版本调用时报告写入失败。

## 打包与身份

全新 `out/build/release-0.0.4` 构建 174 个目标，exit 0。EXE 产品版本 `0.0.4`，SHA256 `455B17D533D837A88B1A9D8BC27F452677A7D1010033E91EB9B37BF6353DFD9E`。未覆盖用户原开发目录中的进程。

沿用 0.0.3 七个运行文件，发布者检查 SHA256、签名、版本和指定文件大小；无新增运行组件。六个原件签名 Valid，指定社区 NR 为 HashMismatch，分放在 `runtime/experimental/nr-community/`。来源类别、完整哈希、版本、许可证位置及移除方法见 [组件清单](../reports/RUNTIME_COMPONENTS_0.0.4.md) 和包内 manifest；清单不作为程序加载锁。NVIDIA/Intel DLL、SDK、模型不进入源码 Git。

项目外新目录 `%TEMP%/Veyra portable 004 verified/` 解压后，51 个 manifest 条目的大小与 SHA256 全匹配，加 manifest 自身恰为 52 个文件。打包 `forbiddenFiles=0`；不包含 SDK 头文件、库、PDB、日志、模型或测试媒体。证据 `logs/release-0.0.4/extracted-audit.json`、`out/releases/0.0.4/package-audit.json`。

| 资产 | 字节数 | SHA256 |
| --- | --- | --- |
| Veyra-0.0.4-win64-portable.zip | 299001834 | F788E8EFDE257953166C871267C63597CF13325D0433FD620551C7F4E655E04F |
| Veyra-0.0.4-FFmpeg-source.zip | 23248643 | B08C43EB78AF709034835D4CF255D67DAB368B25D47967FD2F3B99D529CC3D38 |

两个 ZIP 各附 `.sha256`。FFmpeg 对应源码 10442 条，vcpkg port/patch 与已安装 SPDX 匹配，许可证与构建参数取自实际分发 DLL（LGPL 2.1 or later），未混入 NVIDIA SDK。

## 实际命令

从项目根目录执行，PowerShell 进程测试均用 `scripts/run-short-test.ps1` 包装，单次外部超时不超过 300 秒。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/build/release-0.0.4
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 0.0.4 -BuildDirectory out/build/release-0.0.4 -OutputDirectory out/releases/0.0.4
python scripts/package-ffmpeg-source.py --version 0.0.4 --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/vcpkg/buildtrees/ffmpeg/src/n9.0.1-1250e74153.clean --output out/releases/0.0.4/Veyra-0.0.4-FFmpeg-source.zip
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/portable-smoke.ps1 -PackageDirectory "$env:TEMP/Veyra portable 004 verified/Veyra-0.0.4-win64-portable" -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-0.0.4/portable-final
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/release-0.0.4 -PlayerExe "$env:TEMP/Veyra portable 004 verified/Veyra-0.0.4-win64-portable/Veyra.exe" -PortablePlayer
out/build/release-0.0.4/veyra_audio_timeline_tests.exe logs/release-0.0.4/audio-fixtures
out/build/release-0.0.4/veyra_live_presentation_tests.exe "$PWD/logs/audio-jitter-20260911/4k30fps.mp4" "$PWD/logs/release-0.0.4/xess-native4k" --file-continuity
out/build/release-0.0.4/veyra_capture_audio_tests.exe --jitter
```

日志目录 `logs/release-0.0.4/`，包括 `build.log`、`package.log`、`ffmpeg-source.log`、`portable-final-run.*.log` 和 `delivery.*.log`。

## 验证与失败记录

- 便携验证清除开发 PATH、临时移走 manifest、从包外工作目录启动：空窗口、基础播放、社区 SR+NR+FG、原版 SR+NR+FG、Video SR+NR+FG 五组通过，`portable-final/result.json`。三组增强分别产生 224/222/227 个生成帧，NR 原版/社区的实际模块路径与选择匹配。
- Feature18 Create `0x1`、SEH 0；VSR Create `0x1`、SEH 0，有实际 GPU 执行计时和输出图片。VSR 本机可由驱动 NGX 提供，不将成功扩称为包内 VSR DLL 一定被映射。
- 解压后的正式 EXE 参与 delivery，23 项 PASS、44.899 秒，`logs/delivery/f0ab3a10a1ea44e99f8b20e619fbdbdc/result.json`，EXE SHA 与发布包相同。覆盖实际 NR/NVOF、原生 4K 正确性、播放/暂停/seek/图像、NVENC H264/HEVC 音轨/帧数/时间戳/取消。
- 独立 Release 构建的音频完整回归 68 PASS，`audio-full.stdout.log`；原生 4K30 NR＋XeSS 连续性 6 PASS，100 张真实帧前进 3.33333 秒/耗时 3.33291 秒，额外音频暂停 0，末次软件偏差 1.5ms，真实 XeSS PresentStatus `0`。采集音频 `--jitter` 30/35ms 波动回归 exit 0（合成输入、真实 WASAPI），`capture-jitter.stdout.log`。
- 第一轮上述五组软件检查通过，但测试最后写报告时 `Get-FileHash` 无法解析，包装 exit 1，保留 `portable-run.*.log` 与 `portable/`。补显式导入 Utility 后完整重跑通过，不改 EXE、不降低断言。
- 复核先前 XeSS 两份测试的 stdout，各为 6 个 PASS，修正文档原误记的 7 个；原始结果与通过/失败状态未改动。

本版未新增 RTX40 实机、物理采集卡端到端音画、屏幕扫描/扬声器声学测量或长期直播测试。短测不保证持续过载下仍保持一倍速且绝对同步。
