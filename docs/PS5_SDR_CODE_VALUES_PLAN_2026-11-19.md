# PS5 SDR 暗部压暗修复

用户提供 PS5 Remote Play SDR 在 2.1.9/main 中偏暗的反馈；截图中的原因和修改建议是待核对的报告，不直接当作指令执行。已核对当前 main 33dfdf71：PS5 设置 displayReferred709，工作解码走 pow(2.4)，SDR sink 却重新编码 sRGB。其他桌面 SDR 来源使用 preserveSdrCodeValues。

隔离分支 codex/ps5-sdr-code-values-20261119；只修 PS5 输入颜色契约及对应测试，不修改全局 shader 的 BT.1886 分支、范围/矩阵、增强算法或主机协议。SDR 保留源 BT709 元数据，通过可逆 sRGB 工作表示保持后矩阵 RGB 码值；HDR PQ/BT2020 不启用 SDR 码值策略，8 位 SDR 回退恢复该策略。

执行：集中实际初始化颜色合同供连接与原生测试使用；真实 H264/H265 软件/硬件解码检查帧级合同、HDR请求的8位回退与10位PQ；GPU gray/colour ramp 经共享图及呈现端回读检查恢复前后；HDR、文件、采集颜色针对性回归；完整产品构建。无真实 PS5 连接，不修改用户配置或已发布文件，不把 GPU 参考通过宣称为主机端画质验收。

产物统一 E:/项目/Veyra/{build,tests,logs,reports,tmp}/codex-ps5-sdr-code-values-20261119；既有受控 Qt/Chiaki/patched FFmpeg/libdovi 只读复用。编译并发2，单次不超过900秒、每测试不超过300秒。完成后记录实际命令、失败与修复、SHA和未验范围，提交本地修复；本次截图请求不新增 GitHub push/Release 授权。
