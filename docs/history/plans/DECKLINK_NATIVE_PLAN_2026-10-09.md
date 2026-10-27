# BMD 原生采集修复

用户在本轮信号排查后明确要求“修”。本轮从 `1d7cd55` 在 `codex/decklink-native-20261009` 隔离实现必要 DeckLink 输入、共享采集接点、模式变化恢复、GPU 10位输入解包及 UI 提示；不推进其它补帧/NR算法。当前授权覆盖这些必要 source/engine/pipeline/shader/CMake 接点的历史冻结。

开工保全 `E:/项目/Veyra/archives/decklink-native-20261009/start.json`：42个既有工作树、495脏文件、main `0e7018ad` 不动。增量源码bundle以 `973fc9b` 为前提，对应完整基包仍在上一轮 archives/blackmagic-signal-research-20261009/source-before.bundle，不重复制作整工程副本。

## 实现合同

1. 对照已固定 OBS DeckLink 实现，使用官方稳定12.0 COM ABI（SDK由OBS31.0.3固定提交fcd1910bf5116b69404a6ecdda6efedd1d00ebdf提供原件，E盘生成头文件；SDK/驱动不入Git）。Windows使用已安装Desktop Video COM服务，不捆绑或修改驱动。
2. 用户追加“做成和美乐威一样的开关”：采集设置增加“Blackmagic 原生采集”，默认关闭；开启显示带型号/当前输入口的原生设备，关闭恢复DirectShow入口，切换需重连。保存用户明确选过的两种接口设备身份；多原生设备不猜映射，唯一原生设备可明确选中。原生优先自动检测，老卡保留手动制式。使用persistent/topological ID（含子设备），不以ordinal猜设备；HDMI/SDI跟随Desktop Video Setup，不写驱动配置/EDID。
3. 配置期有界识别真实模式与有效帧，所有HRESULT、实际制式/位深/像素格式/扫描/信号标记进日志。回调不调用停止重配、不持源锁调用驱动；控制在所有者线程执行。有效黑画面正常交付，no-input帧不当作有效画面。模式/颜色合同变化走既有恢复及共享图重建/reset。
4. 复用现有三帧存储、容量1mailbox/上传帧/真实PTS；不增加固定排队、逐帧CPU fence wait或GPU像素回读。内置PCM音频复用CaptureAudioSession；独立WASAPI沿现有路径，端点恢复保留。
5. 对常见v210/10-bit RGBXLE只在明确新格式合同下GPU解包，保留PQ/HLG/色域/峰值元数据；不全局放开旧8位packed HDR限制，不把10/12位输入静默当8位SDR。不能支持的驱动模式明确失败/手动回退。
6. 驱动抽象允许真实产品状态机注入假驱动验证无卡/自动模式/无信号/格式重配/错误/超时/停止/有界资源等；GPU独立验证10位打包、范围、颜色、HDR和旧格式，原普通USB/音频定向回归。不得把这些测试冒充受影响BMD卡验收。

所有新构建/测试/日志/临时/依赖位于 `E:/项目/Veyra/{build,tests,logs,tmp,reports,deps,downloads,archives,worktrees}/decklink-native-20261009` 或已列的 `deps/decklink-sdk-obs31-20261009`。每测试≤300秒、构建≤900秒。不派Agent，不改用户配置/运行库/模型，不打测试包、不merge/push/Release/关机。完成后记录实际命令、失败修复、哈希及实卡限制。
