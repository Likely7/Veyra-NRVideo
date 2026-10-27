# PS5 原始串流画质与码率审计

基线 `5c3fad0`，分支 `codex/ps5-source-quality`。用户选择 100 Mbps，但任务管理器只有约 13–16 Mbps，要求从原始输入解决游玩模糊，不用 SR 掩盖。

## 结论与未完成边界

本机实测没有发现 Veyra 把 100 Mbps 截成 15 Mbps。UI 的 100000 Kbps 原样经过 NativeConnectRequest → ChiakiConnectInfo.video_profile.bitrate → launch_spec.bw_kbps_sent → network.bwKbpsSent。PS5 返回 target_bitrate=97087000，实际视频随场景变化，说明请求不是固定 CBR 流量。15 Mbps 对照返回14563000，证明请求值确实改变主机目标。

**这不证明用户的游玩模糊已经解决。** 用户确认装备菜单中的角色同样模糊，该反馈是有效问题，不要求退出菜单或再证明模糊。后续按用户要求仅通过协议、代码和像素数值检查定位，肉眼验收由用户完成。不能宣称已提高原始编码画质、H.265 必定更清晰、排除全部丢包或强制主机发满 100 Mbps。最新内部/主机边界审计见 [PS5_SOURCE_FIDELITY_AUDIT_2026-09-13.md](PS5_SOURCE_FIDELITY_AUDIT_2026-09-13.md)。

## 实机证据

每轮35秒，RTX 5070 / D3D12VA硬解，1920×1080 / 30 fps。只在测量结束后通过共享颜色路径保存一张1:1 PNG；无SR、NR、FG、NVOF，无重编码、无配对文件修改、无主机输入或电源命令。

| 请求 | PS5目标反馈 | 稳态有效视频均值 | 解码帧 | framesLost / callbackRejected / error |
| --- | --- | --- | --- | --- |
| H.264，100 Mbps | 97.087 Mbps | 10.018 Mbps | 1002 | 0 / 0 / 0 |
| H.265，100 Mbps | 97.087 Mbps | 5.956 Mbps | 992 | 0 / 0 / 0 |
| H.264，15 Mbps | 14.563 Mbps | 5.242 Mbps | 996 | 0 / 0 / 0 |

均值按10秒后有效视频负载字节与单调时钟计算，不含音频、FEC和UDP协议开销，不能与任务管理器进程流量逐点相等。此前查看PNG的内容识别不构成清晰度证明，撤回以“文字和服装细节可辨”支持画质正常的判断。人物与飘落树叶随时间变化，非逐像素同帧，未做PSNR或宣称其中一种胜出。H.265流量更低也不能单凭流量说更糊。range/matrix/transfer均有实际元数据，日志range=1、matrix=2、transfer=2、display709=true，未标assumed。

本地忽略证据：`logs/ps5-quality-h264-30-100/`、`logs/ps5-quality-h265-30-100/`、`logs/ps5-quality-h264-30-15/`，各自含 `source.log` / `source.png`，同级 `*-result.log`。不得把游戏图像和日志提交源码。

## 已修复的显示问题

原面板更改编码、码率、分辨率不会实时改变已连接会话，却只显示新的下拉值，容易把待应用值当作当前值。现在：

- PS5连接状态显示真正的本次请求：分辨率、帧率、codec、请求Mbps，与实际接收视频Mbps分开。
- 活跃连接的按钮显示“应用设置并重连”；格式标签和悬停说明明确重连生效。不会静默中断用户正在玩的串流。
- 重新打开面板时继续显示正在运行会话的状态，不再只能等下一次手动连接。
- 专业诊断和session-start日志补齐本次实际请求，包括HDR codec区别。未更改任何用户已保存的选择或默认画质，也未改像素处理算法。

涉及 `RemotePlayPanel.cpp`、`AppShell.cpp`、`TelemetryWindow.cpp`、`RemotePlaySource.cpp`。

## 诊断实现与性能边界

`ChiakiBackend` 增加可选品质反馈观察，仅 `VEYRA_TEST_PS5_QUALITY_TRACE` 存在时开启上游verbose格式化；只解析固定前缀中的数值目标和计数，不打印原始上游字符串/密钥。正常软件不打开逐包verbose，不添加常驻大量日志。未修改第三方Chiaki源码、协议、拥塞反馈、码率夹限或SDK。

新增 `tests/integration/Ps5SourceQualityTests.cpp`，CMake目标 `veyra_ps5_quality_tests`，组合真实产品Source/颜色图/PNG sink。该工具不实现第二套播放器。

## 构建与检查

- `cmd /c out/remoteplay/build-source-quality.cmd`：构建上述诊断目标成功（脚本为忽略目录本机命令包装）。等价 `cmake --build out/remoteplay/product-repair --target veyra_ps5_quality_tests -j8`，先初始化MSVC环境。
- `out/remoteplay/product-repair/veyra_ps5_quality_tests.exe --last-paired-ps5 logs/ps5-quality-h264-30-100 0 30 100000`，H.265参数为 `1 30 100000`，低码率对照为 `0 30 15000`；三轮exit0、captured=1。所有增强关闭，NGX初始化被disabled-feature配置跳过，本轮没有执行新的NR/FG Create/Evaluate。
- `cmd /c out/remoteplay/build-extra-delay.cmd`：主程序构建成功，`logs/ps5-quality-product-build.log`。保留既存CMake依赖探测警告及FFmpeg头文件C4244警告。
- `veyra_repair_contract_tests.exe`：103 checks / 0 failures，`logs/ps5-quality-contract-result.log`。`git diff --check`通过。UI新文字尚未做真实鼠标/视觉回归。

## 下一步：定位游玩时的质量损失

1. 不再要求用户退出装备菜单。通过同压缩码流软硬解数值对照、已知像素的原尺寸/缩放呈现检查定位；菜单也是有效样本，两个不同时刻的随机截图不能作为逐像素胜负证据。用户负责肉眼清晰度验收。
2. 区分源头已经模糊、局部解码损坏、颜色错误和播放器显示缩放。当前PresentBlit正常稳态是一次双线性contain/zoom，ScaleBlit同尺寸用直接Load，未静态发现持续额外降分辨率；窗口动画期间确有保留buffer的临时DXGI缩放。需要实际失真时对照，不凭静态代码排除运行时错误。
3. 只有原始输入已经变差才进一步研究PS5编码与网络自适应；如果输入清楚而显示糊，修本机呈现。不能为追求100 Mbps数字伪造丢包、关闭拥塞反馈、加入填充流量，或宣称某个未证实协议字段能强制无损/2K/4K。
4. HDR/SDR是独立颜色检查，不能用打开HDR作为“更清晰”的万能修复。保留用户当前选择，以实际输入元数据和已知色值检查，不以AI看图判断是否灰或糊。

## 一手来源

- 本机固定Chiaki commit `0e16950165f06e5c3291537c2eeba6e852be7120`：`lib/src/launchspec.c`、`streamconnection.c`、`congestioncontrol.c`；Veyra目前使用其带既有MSVC/metadata补丁的stage。
- 在线核对：[上游launchspec](https://github.com/streetpea/chiaki-ng/blob/main/lib/src/launchspec.c)、[上游连接与目标反馈](https://github.com/streetpea/chiaki-ng/blob/main/lib/src/streamconnection.c)。在线main可能变化，本机固定版本及真实实测为当前证据，不自动升级依赖。

未推送、未发布、未更换运行DLL。当前交付仅确认请求链路和修正显示；游玩模糊仍待实际场景定位。
