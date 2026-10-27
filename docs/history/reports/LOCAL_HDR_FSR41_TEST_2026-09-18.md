# RTX Video HDR / NVIDIA FSR4 本地测试

> 已撤回的实验记录：用户于 2026-09-18 要求停止并回退 FSR4。本页的旧测试包、命令与接口通过结果仅供追溯，不再推荐运行。当前源码和回退构建已移除该入口，RTX Video HDR 保留；以 CURRENT_STATUS 和 WORKLOG 为准。

这是基于 1.4.1 的本地实验构建，未发布、未合并 main。

最新结论：用户实测 FSR4 明显变糊、细节丢失，画质验收失败；下文通过仅为当时的接口/稳定性证据。确认固定模型布局与 4K 请求不匹配，完整修复尚未完成。用户已要求停止构建；HDR 状态提示的后续源码修改也未编译。见 [画质修复记录](FSR41_QUALITY_REPAIR_2026-09-18.md)。

## 使用

- 视频 HDR：专业模式 -> 增强页，向下滚动到 RTX Video HDR。默认关闭，支持对比度、饱和度、中间灰、峰值亮度；设置会保存。
- 预览需要 Windows HDR 与 HDR 显示器；当前显示器未启用 HDR 时保留 SDR 预览。离屏 HDR 视频导出不依赖显示器 HDR，使用 HEVC Main10 / BT.2020 / PQ。
- 原生 HDR 输入不再做 SDR 转 HDR。字幕与 OSD 在 FG 之后合成。游戏版 RTX HDR 没有加入。
- 新版 `-hdr-fsr41-ui-test` 包已包含独立 FSR4 provider：专业模式 -> 增强 -> 勾选超分辨率 -> 算法选择「FSR 4.1 AI（实验）」。设置自动保存；选择其他算法或关闭超分即可退出，不需要环境变量。旧 `-video-hdr-test` 包没有此界面入口。
- FSR4 DLL 位于 `runtime_local/amd/fsr41-int8/`，不覆盖官方 FSR。开发者可用 `VEYRA_FSR41_PROVIDER` 覆盖实验 DLL 路径；该变量不再改变普通 FSR 选项。详情见 `scripts/fsr41/README.md`（包内 `docs/FSR41_EXPERIMENT.md`）。
- FSR4 失败会报告并关闭 SR，不以 FSR3 成功冒充 FSR4。原有运行库替换自由不变，身份日志不是加载锁。

## 实测

本机 RTX 5070，驱动 616.56 / 32.0.16.1656。以下是本机证据，不替代 30/40 系实测。

HDR：TrueHDR Create/Evaluate/Release 均为 0x1，SEH=0；独立 HDR、DLSS 2X/4X/6X、NR+DLSS SR+6X、原生 HDR 回归通过。90 帧 HDR 导出经开发期 ffprobe 与解码确认 Main10、10bit、BT.2020/PQ；缺 DLL 时 SDR 导出回退通过。未恢复产品导出结束扫描。预设 roundtrip 与 66 个旧格式迁移通过。参数页在 1280x800 窗口无文字重叠，滑块中灰 44 -> 59 即时更新；当前 SDR 显示不能证明实际屏幕 HDR 效果。

FSR4：CPU smoke 和六槽调用方 ABI 通过。使用上游固定提交的真实 ML passes，包含 R11G11B10 输入/输出适配。实验 DLL 874496 字节，未签名，SHA256 `F6EB66096E80CB9A8C066FA7519CF5C82442D0876C8C7A461F489402A6AD44F2`。

| 测试 | 结果 |
|---|---|
| 720p -> 1080p 持续处理 | 12000 帧，11675 次 SR，53.24 秒；command GPU 平均 1.84 / P95 4.34 ms；显存 345 -> 388 MiB |
| 1080p -> 2160p 持续处理 | 3600 帧，3502 次 SR，23.39 秒；command GPU 平均 1.66 / P95 3.88 ms；显存 1088 -> 1131 MiB |
| 1279x719 -> 1919x1079 | 90 帧 / 87 次 SR，D3D12 debug errors=0 |
| NR + FSR4 + HDR + DLSS 6X | NR 12、SR 10、生成批次 10，像素与参数变化检查通过 |
| 实际影片 720p -> 1080p | 同一 60 帧：普通缩放 SR=0、FSR3/FSR4 各 58 次；输出均非空并保持内容 |

持续处理是两个分别重启的进程，不能称为连续十分钟。GPU 时间为整条 command list，不是完整播放链路或单独 SR 的时间，不等于屏幕 FPS。首帧及每 37 帧 reset 时没有光流则暂不 SR，计数不伪造；pending reset 保留到下一次实际 dispatch。

实际影片同帧 PNG 位于 `E:/项目/Veyra/tests/fsr41-nvidia-20260918/media-{baseline,fsr3,fsr4}/fsr-frame.png`。已目视检查内容和几何，FSR4 与基线差异较小，合成棋盘存在边缘/调性差异；没有足够证据宣称画质提升。该对照不是长片动态拖影/闪烁验收。

## 限制与下一步

FSR4 上游模型的中间工作类固定 960x540，PRE/POST 支持不同几何不等于原生 4K 模型质量。视频没有游戏引擎的 jitter、真实 motion/depth；使用零 jitter、估计光流、常量深度和固定曝光。第一次 Create 实测 23.7 秒，后续进程约 0.5 秒；冷启动仍有明显等待。

未执行：RTX 30/40 新功能矩阵、真实 HDR 显示/跨屏、物理采集/PS5 HDR 转换、所有颜色格式和完整动态画质场景。现有 30/40 DLSS 兼容代码没有在本轮修改，但不以 5070 测试代替回归实卡。

下一步：HDR 显示器和 RTX 30/40 本地测试包验收；FSR4 在形成可靠的动态画质证据前保持实验入口。

## 产物与源码

- HDR 提交 `3477ed2`，分支 `codex/video-hdr-20260918`。
- FSR 分支 `codex/fsr41-nvidia-20260918`，源码在 `E:/项目/Veyra/worktrees/fsr41-nvidia-20260918`。
- 构建、测试、日志、临时文件分别位于 `E:/项目/Veyra/{build,tests,logs,tmp}/{video-hdr-20260918,fsr41-nvidia-20260918}`。
- 本地包目录：`E:/项目/Veyra/test-packages/hdr-fsr41-20260918`。二进制、SDK、影片与生成模型数据均未进入源码 Git。
