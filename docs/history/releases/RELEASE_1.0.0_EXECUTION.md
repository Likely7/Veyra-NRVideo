# 1.0.0 发布施工记录

用户于 2026-09-13 授权整合分支、换透明 Logo、发布正式应用 1.0.0，随后要求增加专业顶部截图。一并完成。沿用七个已批准增强 DLL 与 Release-only 规则；无新增 NVIDIA 文件，不将 SDK/运行组件/个人资料新增进 Git。

## 分支核对

- 开工 `2c45419`，工作区干净，tag `checkpoint/pre-release-1.0.0-2026-09-13`。
- `codex/release-p1-repair` 的 `3ae4d5c` 已是当前祖先，含用户另一轮 `out/release-p1-20260913` 对应的三个 P1 修复，未遗漏。
- 其余 PS5/音频/硬解/采样/性能面板分支均已在祖先链内，无需重复 cherry-pick。
- `nrvideo/main` 新增的两次 README 图片更新已合并保留。
- `codex/github-source-archive` 属于旧仓库历史，非新的修复，不把旧代码重新合入。
- 发布分支 `codex/release-1.0.0`。源码目标仓库 `Likely7/Veyra-NRVideo`。

## 本次修改

透明 PNG 原件保存为 `assets/veyra-logo.png`。生成 16/20/24/32/40/48/64/128/256 多尺寸 ICO，透明等比留边，不重新绘制。CMake/RC 版本为 1.0.0。

顶部截图复用现有 EngineController::saveFrame 和最终 videoFrame 读取，单次用户触发的 GPU readback；不加入常规逐帧回读。保存最新处理后的完整真实帧，不包含桌面/UI、原图对比与窗口缩放裁切，不宣称精确截取正在扫描的插值帧。PNG 自动写入系统 Pictures/Veyra Screenshots，唯一文件名、既有不覆盖写入。原生 HDR 路径明确拒绝截图；SDR及HDR映射为SDR可保存。大图沿用全分辨率增强结果保存。

README 中英版、更新说明、运行组件表及对应源码脚本更新到本版。仍保留原实验边界与两项导出限制（不保留内嵌字幕、避免并发相同目标），没有把“正式1.0.0”写成所有场景无缺陷。

## 验证与资产

构建目录 `out/remoteplay/product-repair`，日志 `out/release-1.0.0-*.log`、`logs/release-1.0.0/`。每次测试最多 300 秒。最终验证、包身份、源码扫描和发布结果在完成后追加。


## 最终本地证据

- 全部目标重建成功：`cmd /c out/gpu-dis-build-all.cmd`，日志 `out/release-1.0.0-final-build2.log`。EXE FileVersion/ProductVersion=1.0.0；SHA256 `95CE6F6236DC3A9CF90E68330A1FC999580BC3F66D4658EA6A96D6066EFF9159`。
- 顶部截图处理器测试：`veyra.exe <test_av_1080p.mp4> --nr --video-sr 1 --fg --realtime --smoke-screenshot --smoke-save <processed-screenshot.png> --smoke-seconds 7`，exit=0，PNG 3840×2160，图像解码非空，NR=261、生成=221、failed=false；明确日志 `toolbar handler saved processed PNG`，证据 `logs/release-1.0.0/screenshot-run.log`。测试图片均为本地合成素材，不发布。
- 读取实际 PE 的 GROUP_ICON/ICON 资源：9 个尺寸完整，256 图标 alpha 0..255，证据 `icon-verification.json`。不是只检查磁盘上的 ICO。
- `scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair` 23/23 PASS，44.737 秒；`logs/delivery/92e09ba3556148a9b9275d2ba1ef1bfe/result.json`。包含实际 NVIDIA Create/Evaluate、呈现、导出及图片/seek。新版本截图没有新增常规逐帧回读。
- `scripts/package-portable.ps1 -Root . -Version 1.0.0 -OutputDirectory out/releases/1.0.0-final -BuildDirectory out/remoteplay/product-repair` 通过。首个候选后补齐英文 README/BUILD 版本与统计说明，未覆盖首个候选，在 final 目录重打。
- 最终 ZIP 解压到 `out/releases/1.0.0-verify`；`scripts/acceptance/portable-smoke.ps1` 使用清理后的 PATH、临时禁用 publisher manifest，5/5 PASS。模块来自解压目录，无开发环境运行时依赖；原版/社区 NR 路径正确，生成帧非零。`logs/release-1.0.0/portable-smoke/result.json`。
- 包内逐文件 SHA256 与 manifest 一致，GPU DIS 子目录着色器与 Apache/BSD notices 完整，FFmpeg local-build 记录存在；源码 ZIP 无 DLL/LIB/EXE/OBJ/PDB/模型。证据 `asset-verification.json`。
- 七文件身份、签名、来源及许可证按 `RUNTIME_COMPONENTS_1.0.0.md` 和 `package-audit.json` 核对；社区 NR 保持 HashMismatch，其他六文件 Valid。无 SDK、新 runtime、凭据或个人配置进入本次源码/用户包。

最终资产：

| 文件 | SHA256 |
| --- | --- |
| Veyra-1.0.0-win64-portable.zip（303913391 bytes） | EAAE5B13252EDE1F59DC41E3773C6DAFC960918EFDA914F160D1F3B03A34DBC5 |
| Veyra-1.0.0-RemotePlay-source.zip | 05D06DBF3DD51F794C7E542554F1B73B8C1F25E22856B792AC6B57E628481A8A |
| Veyra-1.0.0-FFmpeg-source.zip | 4A164BB72AC6A70ED70C77571CC7F08B637580F7CFAD4DDF2FC16B06B2B15BA1 |

源码脚本 `package-remoteplay-source.py --version 1.0.0` 与 `package-ffmpeg-source.py --version 1.0.0 --source C:/veyra-deps/ffmpeg-ps5-slices-source` 成功；后者校验真实 patched header、DLL、补丁身份和 LGPL 配置。不用 stock source 冒充修复版对应源码。6 个上传资产为上述 ZIP 与各自 .sha256。

未执行真实 PS5 新版长时/蓝牙/多GPU/HDR屏幕验收；未声称新版无全部bug。导出两个已知P2功能边界已写 Release Notes。正式应用版本不改变各实验组件属性。无独立 Reviewer，仅代码自查与实际自动检查。

## 2026-09-13 1.0.0 公开发布完成

源码整合提交 2939cd4046e24f2b2fc987322f0196e7cc5acd2a 已 fast-forward 至 main，git push nrvideo main v1.0.0 成功。v1.0.0 标注标签固定该提交；本段是发布后的文档记录，不移动标签或替换已验证二进制。

GitHub Release https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.0 于 2026-09-13 06:38:08 UTC 公开。gh release edit v1.0.0 --repo Likely7/Veyra-NRVideo --draft=false --prerelease=false --latest 成功；随后读取 releases/latest，确认 tag=v1.0.0、draft=false、prerelease=false。六个资产均 uploaded，逐项服务端 digest、size 与本地 SHA256、长度一致，包括三个 ZIP 和三个校验文件。核对记录 logs/release-1.0.0/github-assets-verified.json（仅本地）。源码 v0.0.5..v1.0.0 新增/修改文件扫描未发现 DLL/LIB/EXE/ZIP/模型；工作区发布前干净。当前发布与对应源码包均已完成，真实 PS5 长时及其他未测硬件边界仍按上文，不由发布状态推定通过。
