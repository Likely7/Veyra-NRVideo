# PR44 HDR显示调节整合计划

用户要求研究并合并Likely7/Veyra-NRVideo#44，固定head3ee17b62f00154fd3efef2d91f6053ce12470f06，base0cfa8a8；本地main8d8eb09。隔离分支codex/pr44-hdr-display-20261016，保留原贡献提交与作者。78旧树/509脏文件快照archives/pr44-hdr-display-20261016/start.json、checkpoint/pre-pr44-hdr-20261016。上一轮issues41–45分支ab0a266不混入本次PR合并。

本次授权必要HDR显示/UI/参数保存/元数据/低频统计和对应shader/测试/构建接点；旧UI-only冻结不阻断用户明确要求。用户已授权PR合并，检查通过后普通合入main并同步GitHubPR；不发Release/打用户包/代发评论/关机，不派Agent，不改SDK/runtime/模型/真实配置。所有产物E:/项目/Veyra/{用途}/pr44-hdr-display-20261016，测试≤300秒、构建≤900秒。

研究重点与必须适配：

1. 保留五张折叠卡片、两轴/旧档切换、精确/简化调节、推荐与显示器/影片记忆；补齐返回失败、保存优先级、开播/重开/退出、关闭记忆语义和外语布局。不能在33ms轮询路径每次枚举显示设备或写盘，不改现有33ms/250ms节奏。
2. 曲线仍作用于HDR输出，包含RTX Video HDR转换，避免仅HDR源gate使现有SDR转HDR失效。图示/提示/元数据使用和着色器一致的已解析强度/显示峰值曲线，显示器不合理报告作为可核对提示，不能凭高峰值/低黑位就断言真实屏不支持。
3. 原PR64x36像素回读违反正常播放契约。改为GPU抽样归约，只异步读少量统计整数，显示页/读数卡可见且HDR曲线生效时才采样，最多4次/秒、不额外submit/CPU fence wait；黑帧是有效零统计。scRGB/HDR10正确单位，切曲线/源epoch/关闭/seek清空过期值。读数明确抽样估计，硬裁剪按实际曲线阶段计算。
4. 用现有CPU HDR单测与真正GPU呈现夹具验证：曲线/metadata/graph/dials、强度0/关闭/SDR/RTXHDR/scRGB/HDR10、异步统计/黑帧/重置/默认无探针/窗口缩放；预设双向读写与记忆重开；英文/繁中/日文默认窄面板，旧UI/遥测回归。记录上游宣称与本机证据区别，未测实体投影仪不冒充验收。

最终报告PR44_HDR_DISPLAY_REPORT_2026-10-16.md、WORKLOG，构建输入SHA/原树保护/来源许可证与发布状态回执。不将本轮后端适配冒充无代价或普遍性能收益。
