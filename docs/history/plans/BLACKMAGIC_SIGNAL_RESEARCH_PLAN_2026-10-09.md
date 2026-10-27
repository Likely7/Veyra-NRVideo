# BMD 黑屏：原始信号与 madVR 配套采集路径核查

用户提供 `veyra-qml(45).log`，要求具体核对 madVR、Blackmagic 官方软件，解释前次兼容修复后仍黑屏的原因。用户暂时不清楚实际可出画的软件是 VideoProcessor＋madVR 还是 Media Express。本轮同时核对两条路径，不将猜测写成现场事实。

## 范围与保全

- 基于已验 PR31/播放列表分支 `973fc9b816602ed6f11a40e88a54fc8913a2a87c`，仅在 `codex/blackmagic-signal-research-20261009` 记录本轮排查。保留已完成但尚未合入 main 的字幕、采集音频和播放列表修改。
- 开工 `E:/项目/Veyra/archives/blackmagic-signal-research-20261009/start.json` 和 `source-before.bundle`，保护 41 个既有工作树、495 个脏文件。main 保持 `0e7018adab9b211d76dd05da40cfbaeb9d77cf2c`。
- 当前任务为原因核查：读取用户日志、现有产品源码、固定提交的上游采集源码、官方文档；本轮不再凭接口差异制作一个未经实卡验证的修复候选。
- 新产物只在 `E:/项目/Veyra/{tmp,reports,downloads,archives,worktrees}/blackmagic-signal-research-20261009`。不制作测试包，不改驱动/用户配置/运行库，不派 Agent，不合并、推送、发布或关机。

## 核查步骤

1. 按应用会话、用户连接请求和实际重新配置分组；核对兼容接收器是否真的启用，避免把追加的旧日志误判为本轮测试。
2. 对照原始像素采样的位置与 HDYC 字节布局；区分回调计数、配置帧率、送显计数和真实有效输入，不拿 FPS 证明出画。
3. 固定 VideoProcessor、OBS DeckLink 插件与 libdshowcapture 的源码身份，核对输入口、自动制式检测、RGB/YUV/位深切换、无信号标记、实际模式状态和生命周期。核对许可证，不直接搬入带附加非商业限制的代码。
4. 查 Blackmagic 官方 Media Express/DeckLink 文档，辨别 madVR 渲染器与实际采集程序。官方闭源程序的具体内部调用不做推定性证明。
5. 反向审查：OBS 通用 DirectShow 来源据用户反馈也能出画，因此“Veyra 没有原生 SDK”是确定的能力缺口，并非足以单独证明本次黑屏根因的证据。
6. 输出已确认/高概率/仍未知的分层结论及能区分原因的后续方案；最终校验原件哈希、Git 范围与全部旧工作树。

## 后续修复的验收合同

后续应补 BMD 原生输入适配与信号状态，而不是继续靠黑场像素猜制式：

- 先通过原生设备稳定身份确认型号、子设备和当前输入口；读取接口成功与否、信号锁定状态、检测制式、扫描方式、RGB/YUV/位深、当前启用模式。不支持的字段明确 unknown。
- 自动模式只在驱动声明支持时启用 `bmdVideoInputEnableFormatDetection`；保留手动模式与 DirectShow 回退。物理输入由用户明确选择或沿用驱动配置，不盲切 HDMI/SDI，不写 EDID 或永久配置。
- 在格式变化回调中处理真实模式与像素类型，沿官方 pause/enable/flush/start 流程并原子重建尺寸/颜色合同、reset 共享增强历史。失败明确上报；正常黑色内容不能被判为无信号。
- 读取 `bmdFrameHasNoInputSource`，与“有效信号恰好是黑画面”分开。所有 HRESULT/标志值进诊断，不把初始化成功当实卡画面恢复。
- 沿用共享 `FrameSource -> EnhanceGraph -> FrameSink` 和容量 1 mailbox；回调只复制到有界自有帧，不建立额外排队、固定 lookahead 或 GPU 像素回读。实际延迟须实卡测量，不能从 allocator 数量推算。
- 采用许可证适配的 OBS DeckLink 路径与官方 SDK 约定，SDK/驱动文件留在 E 盘依赖区，不加入源码 Git。VideoProcessor 仅作对照研究。
- 受影响 BMD 卡用同一输入、互斥关闭其它占用程序，对比已能出画的软件与 Veyra：真实出画、正确制式、信号拔插/恢复、切源、音频、重开及停止；本机普通 USB 回归不能替代该项。

本轮完成情况及事实边界见 `BLACKMAGIC_SIGNAL_RESEARCH_REPORT_2026-10-09.md`。
