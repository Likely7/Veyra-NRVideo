# 社区 PR 整合与 B 站功能移除

用户从 20 个已审查 PR 中选出 9 个进入实施。隔离分支 `codex/community-prs-20261119` 从远端 main `0dac67a0` 创建。未选中的 11 个 PR 已关闭。本轮更新源码主线，版本号沿用 2.1.9。

## 处理结果

| PR | 进入主线的改动 |
| --- | --- |
| [#168](https://github.com/Likely7/Veyra-NRVideo/pull/168) | 修复 PS5 刚入队的完整帧被错误判定为过期。 |
| [#156](https://github.com/Likely7/Veyra-NRVideo/pull/156) | 60→30 在低于 30 FPS 的输入中跳过能确认的重复画面，保留源时间戳、长时间静止和无画面 ID 的回退。 |
| [#59](https://github.com/Likely7/Veyra-NRVideo/pull/59) | 通过 DisplayStateService 传递峰值可信标记。可信报告参与曲线缩放，手填峰值优先，10000 nit/近零黑位报告回到参考曲线。 |
| [#166](https://github.com/Likely7/Veyra-NRVideo/pull/166) | Dolby Vision P8.4 的错误 PQ 标记按 HLG 基础层解析，确认条件包括 HEVC、配置记录、基础层、Profile 8 和兼容 ID 4。AV1 硬解使用 FFmpeg 原生解码器，保留 libdav1d 软解。8/10-bit 硬解表面按实际 NV12/P010 辨认。 |
| [#167](https://github.com/Likely7/Veyra-NRVideo/pull/167) | 用当前控制台编码解码 MSVC 探测输出，再写入 Ninja 的头文件依赖前缀。保留旧 GUI 目标的存在性保护。 |
| [#161](https://github.com/Likely7/Veyra-NRVideo/pull/161) | 合并同一按键状态下的连续模拟量，过期时逐项移除，只消费第一项仍有效的按键或触摸状态。 |

[#158](https://github.com/Likely7/Veyra-NRVideo/pull/158) 的 80 项离线测试通过。队列对照证明提案在暂停 249 ms 后仍允许取出旧 AU。产品保持 4 帧/100 ms，在现有周期日志加上恢复次数、峰值深度、丢帧和等待关键帧状态。

[#165](https://github.com/Likely7/Veyra-NRVideo/pull/165) 的 85 项离线测试通过。关闭路径仍会在界面线程等待可能阻塞的 HID 写入，本机没有 DualSense 蓝牙设备验证异常写入和拔插，未采用。

[#62](https://github.com/Likely7/Veyra-NRVideo/pull/62) 的删除方案会移除已有 HDR10 设置。本轮恢复当前呈现器的发送和清除路径，重建窗口时重新应用。只有输出 HDR10 时才发送，DXGI 接受提示与物理屏幕实际使用属于不同结果。

已关闭的其他候选为 #7、#8、#11、#13、#15、#31、#55、#56、#58、#60、#71。

## B 站与通用播放

移除站点浏览、登录、收藏、播放解析、站方字幕和弹幕、专属赞助入口、站点扩展脚本和账号凭据转交。回环接口仍接收直链，接受旧扩展的不带凭据请求，拒绝非空 Cookie。扩展可发送 HTML5 视频的直接 HTTP(S) 地址或直接媒体标签页。播放器内的 HTTP、HTTPS、RTSP、普通字幕、Dolby Vision 和文档教程外链继续适用。旧配置文件可读取，原有 B 站字段无运行用途。

## 验证记录

- Windows 本机完整应用与 12 个相关测试目标构建成功。首次链接用非 ASCII Qt 实体路径时，MSVC 读取响应文件报 LNK1181，使用指向同一 Qt 实体且库哈希一致的既有 ASCII 入口后通过。
- MSVC 中文环境最小项目分别验证 CP936 和 UTF-8，均生成 rules.ninja，Ninja 记录头文件且修改头文件后重编。实际仓库关闭 Qt 和 FFmpeg 后也在 CP936 下成功配置、编译并记录头文件。原始 report-only PR 样本头文件依赖数为 0。
- 60→30 与调度 346 项通过。24/26/29 FPS 的 20 秒模拟样本分别消除 120/80/40 张重复画面。MCS 4K-T911 空闲时实测 1080p60 NV12，重连前后分别收到 60.164 和 60.100 FPS，均读到 362 帧且时间戳严格递增。动态游戏接入 60→30 后的呈现节奏在夹具中验证。
- PS5 队列回归 79/79。实际构建的会话边界、虚拟手柄、手柄切换及 200 次控制会话退出验证通过。慢消费样本中持续按键误释放 0、输入落后 0，队列峰值 1。测试没有发起 PS5 主机连接。
- HDR 参数与旧预设 217 项通过。GPU/DXGI 45 项通过，HDR10 的设置、窗口缩放后重设和清除均返回 HRESULT 0。Dolby 基础层色彩回归与 libdovi 256 项测试通过。
- 8-bit 与 10-bit AV1 均用 D3D12VA、libdav1d 和无硬解设备的自动回退各解码 60 帧，对照 PTS 一致。
- 通用播放桥 22 项通过，扩展通过 Node 测试。原生 QML 主程序读取含旧 B 站字段的私有偏好，通过 HTTP 播放本机夹具并正常退出。普通 SRT、ASS、VTT、两种内嵌字幕各保留 6 个预期字幕。五语言及占位符检查通过。
- Qt Quick 最终 220 通过、0 失败、0 跳过。按移除后的赞助入口更新资源计数，并在既有 NR 控件测试里等待布局稳定后再点击。

构建用 `scripts/acceptance/build-recording-handoff.py`，任务名 `codex-community-prs-20261119`，并发 2。日志在 `E:/项目/Veyra/logs/codex-community-prs-20261119`，私有样本在 `E:/项目/Veyra/tests/codex-community-prs-20261119`。现有 FFmpeg 和 libdovi 依赖按本机已审核前缀构建。
