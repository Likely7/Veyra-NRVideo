# 内容节奏与显示限速状态修正（2026-10-11）

用户要求：自动识别内容节奏去重、垂直同步及其它正常限速不再误报输出未达标；状态条上方显示当前开启的选项和原因，正常按设置运行显示正常。

基线是刚验收的 DLSS45 接入分支 e8300d9，单独 codex/cadence-status-20261011，不丢掉用户正在试的新超分，不合并 main。开工 start.json / 增量 source-before.bundle 位于 E:/项目/Veyra/archives/cadence-status-20261011，保护58原工作树/504脏文件及原测试程序，不修改旧保护记录。

1. 只改显示状态判定、bridge/QML/翻译及必要定向测试。共享状态评估计算内容去重、60→30、播放速度、有效输出上限、屏幕同步后的目标；拒绝把已设置但未生效的选项当限速。实际去重使用已有 captureRateSkipped 计数按有界时间窗估算，不改采集、去重、补帧、NR/SR、呈现算法或用户配置。
2. 对比模式按原帧节奏判断。输入不足/时间线异常、真正低于有效目标、没有正常限速理由的补帧降档继续告警；不通过将目标等于实测输出来制造正常。静止画面正常去重也应考虑主动跳过的帧。
3. 列表状态条上方与节点统计区域显示相同选项说明及有效目标。文案支持换行/完整提示和多语言，不能溢出或增加持续绘制。保持33ms统计与250ms性能发布节奏；不开新Timer，不改GPU优先级。
4. 验证状态策略矩阵及真实GUI的限帧/播放速度/对比和模式切换。每测试≤300秒；沿用当前已验构建对象、DeckLink和patched FFmpeg，构建≤900秒。失败原证据保留，实卡去重/其它屏幕/provider未实际验证如实说明。
5. 全产物 E:/项目/Veyra/{build,tests,logs,tmp,archives}/cadence-status-20261011；私有运行目录原子替换、独立data-dir与缓存。交付前核对原包与58旧树，记录WORKLOG与报告。无main合并、push、打包、Release或关机授权。

实测追加：GUI 同一回调关闭 DLSS 4X 并设 0.5X 会触发 seek，重建后的缓存帧在 discardBefore 检查被跳过，局部 transaction 标志丢失；实际输出15fps而 applied 仍为4X，applying永久为true。仅补 EngineController 的 pendingSettingsAck，跨循环保留确认，到新设置首帧成功 process 后发布真实 applied；不提前确认，不改重建/seek/颜色/补帧/呈现算法，不增加逐帧锁或等待。用同一实际GUI序列回归，另跑重建和回滚检查。
