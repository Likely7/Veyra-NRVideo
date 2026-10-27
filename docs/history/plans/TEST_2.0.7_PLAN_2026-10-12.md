# 2.0.7 本地整合测试包

用户授权将 Claude 最新 XeSS 已验修复及各分支已完成修复合入本地 main，构建 2.0.7 测试包。起点 main a167831；独立分支 codex/test-2.0.7-20261012。66 旧工作树、509 脏文件、来源 tip 由 archives/test-2.0.7-20261012/start.json 和已 verify 的增量 bundle 保护，原树不改。

## 合并范围

- Claude e46bbdf：包含 Codex f0fafdf 的 XeSS 光流接线和 FSR-only 光流初始化修复；新增 NVIDIA 前后向一致性、AMD FidelityFX 认领检查及默认开启的边缘神盾开关。Automatic 零运动与 NR/SR guidance 保持原路径。
- Codex d883216：包含 34f479e 的异常短采集时间戳误重置修复、专业模式原生视频与 QML 实际区域统一及测试。
- 已在 main 的 UI、字幕、播放列表、采集保存、VFG 混合档、DLSS 4.5 SR、卡死/叠加兼容与音频修复保留。不重合并旧 RTSS 研究、已结束诊断或被撤回的 SR/性能实验。
- 文档冲突保留双方记录；翻译按 (zh,ctx) 保留 main 全部值，补入 Claude 四条新键。只做有证据的必要整合适配。

## 构建与验收

1. 完整配置并构建当前应用和有关测试目标；版本资源 2.0.7.0、显示 2.0.7-test。依赖沿用已审 SDK，明确 DeckLink，FFmpeg 保留 PS5 slices256 与 zlib 字幕支持；不改运行库/驱动/模型。
2. 定向回归 XeSS motion、FSR-only、采集 cadence、图/设置/预设、呈现生命周期、HDR、字幕及 UI 契约；每测试子进程不超过 300 秒、单次构建不超过 900 秒。
3. GTA VI 原生 4K 输入、内部 1080p NR：XeSS 开关、后端/倍率/运动来源切换、重启保存；专业窗口实际 HWND 与播放条边界回归，GPU/软件 UI。保持 33ms 统计和 250ms 状态发布，不加入生产 Timer 或提高 GPU 优先级。
4. GPU 无竞争时核对必要性能与开关增量；只报告本机 RTX 5070 的软件/GPU 指标，不冒充 AMD 实卡、物理屏幕刷新/端到端延迟或长期显存验收。
5. 封存实测产品源码后本地 main --no-ff 合并，核对完整 Git 树及旧树/脏文件。制作 NVIDIA、AMD 两个完整便携测试包，审计 PE、全部文件、运行组件 manifest、许可证、源码/依赖源码及 ZIP 哈希，并使用最终包私有 data-dir 冷启动。

产物统一 E:/项目/Veyra/{archives,build,tests,logs,tmp,reports,test-packages}/test-2.0.7-20261012。原测试包/正式包不覆盖。当前授权不包含 GitHub push、Release、上传或关机。未验证项和失败证据写入报告及 WORKLOG。

## 构建前追加的采集格式约束

用户要求先选分辨率再选格式，MJPEG 标红并括号“不建议使用”，实际不存在的组合弹错误而不换格式。首次新设备不默认选分辨率；有效历史设置继续恢复。分辨率/帧率改变清空具体格式，列表仅显示设备在所选两轴下提供的完整媒体身份。无效 key、驱动拒绝及连接失败均明确报错，压缩连接/解码失败不再自动转 RGB；显式诊断 RGB 入口保留。草稿两轴按设备写入现有 JSON，capture-preferences.v1 格式不升级。增加严格组合/MJPEG 单测、红色菜单 Qt 测试、新设备/草稿重启/无效选择/全格式可达及原保存/继续/自动恢复真实 GUI 回归。
