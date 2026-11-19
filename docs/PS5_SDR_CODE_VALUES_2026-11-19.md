# PS5 SDR 暗部压暗：原因、修复与验证

代码基线 main `33dfdf71`；其后远端仅 README 下载量更新（至 `f9e954ab`），本地整合保留这些更新；用户提供 2.1.9/current main 的 PS5 Remote Play SDR 偏暗反馈。截图中的建议经源码和本轮 GPU 测试核对后采用，未直接套用其全局 shader 替换实验。

## 原因与修改

旧 PS5 SDR 输入设置 `displayReferred709=true`，但没有 `preserveSdrCodeValues`。因此共享图用 `pow(rgb,2.4)` 解码，SDR 输出又用 sRGB 编码，两条曲线不互逆。暗部和中间调在进入增强效果前已经变暗，关闭 RTX Video HDR 不会消除这个源端转换。

修复集中在 `RemotePlaySource`：初始颜色合同提取为连接和原生测试共用的方法；H264/H265 SDR 保留 BT709 源元数据，设置可逆 sRGB 工作表示。每帧按实际解析的 transfer 决定 SDR 码值策略；HDR 请求的 8 位 SDR 回退开启该策略，10 位 PQ/HDR 和 HDR decoder reset 明确关闭。范围、矩阵、色域、主机请求、增强算法及全局 BT.1886 shader 分支均保持。旧 `--remote-yuv` 测试的错误 BT.1886→sRGB 期望改为独立的后矩阵 RGB 码值参考。

这里的“保留码值”发生在 YUV range/matrix 转换之后，不跳过 limited/full 展开，不把 BT709 元数据伪写成 sRGB，也不修改显示器 gamma 或用户亮度设置。

## 本轮实测

任务目录 `E:/项目/Veyra/{build,tests,logs,reports,tmp}/codex-ps5-sdr-code-values-20261119`。构建目标：`veyra_qml_ui`、`veyra_remoteplay_source_tests`、`veyra_yuy2_color_tests`、`veyra_hdr_color_tests`、`veyra_capture_color_tests`、`veyra_source_fidelity_tests`。真实 patched FFmpeg、已核验 Chiaki stage、Qt、libdovi 与现有运行组件沿用。

| 检查 | 结果 |
|---|---|
| 完整 Windows 产品与六目标构建 | 成功。初次测试参考代码修改遗漏花括号导致编译失败，修复后 build-20261119-025700 退出0；原失败日志保留。 |
| 实际 RemotePlaySource + 软件/真实 D3D12VA 硬解 | H264、H265 SDR、HDR请求的8位SDR回退、10位PQ均通过；源 metadata 的 SDR preserve=1/workingTransfer=1，PQ preserve=0。含初始合同、重排PTS、软硬解像素比较和故障回退。 |
| GPU旧策略负对照 | 同一个 shader/图，仅恢复旧输入策略，601/709 × limited/full 四组均超出正确参考，最大误差13/255，测试按预期退出1。没有把这个退出码算成产品失败或忽略。 |
| GPU修复后的PS5 SDR合同 | 同样四组最大误差1/255，图到实际呈现回读误差0；退出0。 |
| 文件/采样 | NV12与planar的1080p灰阶/单像素、不同呈现路径与缩放回归通过；入口最大误差0.575342/255，单像素错误0、平面结果一致。 |
| 采集/其他HDR | YUY2 GPU颜色、采集元数据、PQ/HLG/HDR矩阵/色调映射/原生往返等既有 HDR 回归均通过。 |
| 运行stage | 冷启动/退出成功，89个着色器同步后独立核验无不匹配。 |

`TESTS.json` 记录8项正向回归和1项旧策略负对照，全部符合预期。数学复算记录在 `TRANSFER-MATH.json`，例如 RGB 26→旧13/新26、51→旧40/新51；这些是转换计算和合成GPU参考，不是实拍PS5光学测量。

测试码流由 `make-source-fixtures.py` 本地生成：H264、8位HEVC和10位PQ；没有连接/抢占真实PS5，没有读取主机凭据或改真实配置。运行stage从前序私有stage做独立复制，**没有硬链接**；源码/SDK/二进制与官方发行ZIP隔离。实际程序在 `tests/.../app/veyra_qml_ui.exe`，是本地2.1.9-ps5-sdr-test，未发布新包。

## 边界与交付

HDR检查覆盖内建PQ/HLG/颜色与往返测试，未执行依赖外部片库的HDR NR/SR/FG样本。

本轮证明源端颜色合同、真实软硬解和共享GPU输出纠正了该转换误差；尚未进行用户同场景的真实PS5/Chiaki/显示器对照，不宣称全部主机、驱动或显示器画质验收。没有更改全局 shader transfer-3 行为、其他来源的默认合同或 HDR 效果设置。

修复本地提交并整合本地main，远端推送/Release不包含在本次截图请求中。源分支与构建源工作树保留，完整命令/回执与SHA在本轮报告目录 FINAL.json。
