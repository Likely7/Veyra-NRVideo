# Veyra 1.4.1

相对公开版本 1.4.0 的修复汇总。发布日期：2026-09-18。

## 重点：RTX 30/40 DLSS 与持续补帧

- 修复 RTX 30/40 无法开启 DLSS 补帧、能力识别不完整及请求高倍率却回落到 2X 的问题，支持最高 6X。兼容初始化覆盖 NGX 初始化、能力查询、Feature 创建与释放，不再只改界面倍率。
- 修复 RTX 3060 开启补帧直接卡死：恢复真实架构参与网络选择，按上游范围重建兼容程序，修正程序长度、压缩段和字体资源处理。预览兼容探测失败后及时结束子进程，避免异常清理继续拖死播放器。
- 修复 DLSS 4X/6X 正常运行一段时间后一直“补帧受限”：修正软重置后的统计饥饿、过期呈现成本、重复计入 GPU 等待和固定退避；负载恢复后可重新生成帧。
- DLSS 增强与呈现使用独立队列和资源交接，实时采集限制在途增强批次，避免旧任务积压、生成帧尚未显示便过期。保持真实时间戳，不以降低播放速度掩盖欠速。
- 修复 6X 批次数组容量、编码描述符容量和第 4/5 张生成帧的 GPU 计时槽覆盖；播放中切换倍率、暂停、拖动与恢复后同步实际能力。
- RTX 50 继续使用原生路径，不安装 RTX 30/40 兼容补丁。磁盘上的 NVIDIA DLL 未修改。

受影响用户已在本轮测试后确认“测试好了都可用”；这是用户实卡反馈，不是开发机对所有 30/40 型号的覆盖。兼容解锁仍为社区实验功能，来源、固定提交及改动见 `THIRD_PARTY_NOTICES.md`。

## 播放器

- MKV 内嵌字幕可以选择，主字幕和副字幕独立设置，选中即启用。多轨字幕改为单次后台扫描并渐进显示，不再为每条字幕重复扫描整部影片。
- 增加内嵌音轨选择，可切换英语、西班牙语等语言，保留播放位置、暂停状态与音量；导出也使用所选音轨。
- 优化大 MKV 打开与拖动：音频跳转使用视频索引定位后裁切，去掉拖动结束的重复 seek。测试文件含 32 条字幕、17 条音轨；本机热缓存首帧约 367ms，数次跳转约 31ms，其他机器和冷缓存可能不同。
- 修复启动增强期间拖动导致音视频互相等待；无声文件欠速时保持媒体时钟推进，按真实时间均匀跳过过期预览机会。
- 修复 NR、SR、NR 运行版本等设置关闭软件后丢失；效果总开关关闭时仍保存已调好的参数。
- 播放时阻止系统自动休眠和屏保，全屏观看无需定时触碰鼠标；暂停、停止和关闭时释放请求。
- 修正先开 NR 再开 XeSS 时代理交换链缓冲索引不一致，资源屏障、绘制和呈现使用同一个缓冲区索引。
- 整合 1.4.0 发布后的色彩分组折叠、预设保存行、底部设置状态和测试启动脚本改进。

## 采集与音频

- 修复音频输出设备短暂故障时，仍在输入的音频未被统计，进而被误判断流并重启整个采集图的问题；恢复输出期间不积压陈旧音频，真正的输入断流仍能恢复。
- 修复音频恢复和启动 seek 的状态切换，补充持续欠速、输出中断和恢复的回归检查。
- 合并 AVerMedia 音频切换修复：使用设备树中的真实名称定位视频功能，按组件要求等待初始化与卸载，在当前连接内尝试另一 USB 功能，并保留厂商组件自身的错误日志。具体圆刚型号的 5.1 直通仍需逐台验收。

## 导出

- 固定匹配的 NVENC 13.0 ABI，修复只回退 apiVersion 却沿用另一套结构定义的问题；能力/版本查询用于诊断，实际调用失败时保留系统编码器回退和具体错误。
- 取消 120 帧资格扫描、严格 CFR 资格门禁、独立 FG 预探测、音轨查询/损坏标记的提前拦截，以及结束后重新打开成片逐帧解码检查。编码、封装、关闭文件完成后直接结束，避免额外等待整片复检。
- 可变帧率按真实源帧时间间隔封装，编码帧号与封装时间戳分开管理；缺失或倒退时间戳自动补齐并记录。源帧全部保留，生成帧按相邻源帧间隔分配。
- 修复 6X 导出资源容量、帧池回调释放顺序、取消后清理和 worker 错误回传；导出不再静默改成 2X 或关闭补帧。
- 导出使用当前请求的效果与所选音轨，不再等待预览先成功出帧；HDR 选择 H.264 时自动改用 HEVC Main10。
- 实际编码、封装、写盘失败仍报告错误，已有文件保护仍保留。重复帧只用于时间槽补齐，不计作 DLSS 生成帧。

## 验证与边界

- 用户确认最新测试包可用；开发机为 RTX 5070 / 616.56。此前本机通过真实 NR/FG、强制 Ampere/Ada 2X 至 6X 生命周期、实际异常探测恢复、字幕/音轨/设置、音频连续性、导出和 USB3 采集故障恢复测试。正式版本构建、交付短测和解压包证据见 [发布记录](RELEASE_1.4.1_EXECUTION.md)。
- 部分合成图案仍存在内容相关的 6X 插值位置偏差。高分辨率 NR+SR+FG 可能超过单帧预算，低总 GPU 占用不代表串行链路一定能达到目标帧率；本次不承诺 4K 全增强 360fps。
- 原 5090 NR→XeSS 故障未在本机复现，已修正确定的缓冲索引问题并验证两种启用顺序；不能据此认定所有类似卡死均已覆盖。
- AMD/Intel 导出、全部采集卡/音频设备和长片整段播放未逐一验收；HDR MaxCLL/MaxFALL 仍沿用来源，未重新计算。
- 运行组件沿用原包，社区 NR 组件保留 `HashMismatch` 标识；不代表 NVIDIA、Intel 或 AMD 认证。附 patched FFmpeg 与 Remote Play 对应源码和第三方许可证。

## English

Veyra 1.4.1 focuses on RTX 30/40 DLSS frame generation up to 6X, RTX 3060 freezes on enabling FG, and sustained playback incorrectly remaining FG-limited. It corrects compatibility initialization and cleanup, 6X batch/timestamp capacity, GPU queue handoffs and overload recovery. RTX 50 retains its native path. Affected users confirmed successful testing; this does not establish coverage of every model and driver.

Other changes include selectable embedded dual subtitles and audio tracks, one background subtitle scan, faster large-MKV opening/seeking, saved NR/SR/runtime settings, playback sleep prevention, consistent NR/XeSS presentation, capture recovery without unnecessary video restarts, and AverMedia device naming/initialization fixes.

Export now uses a consistent NVENC ABI, preserves variable source timing and selected audio, supports recovery and accurate worker errors, and automatically chooses HEVC Main10 for HDR. Qualification gates and the final full-file decode pass are removed; real encoding/muxing/I/O failures still report errors. No source frames are discarded, and timing-fill repeats are not labeled as generated frames.

Community unlocks remain experimental. Content-dependent synthetic interpolation deviations and hardware-dependent performance limits remain documented. See the execution record for final build/package evidence and the runtime component list for unchanged binary identities.
