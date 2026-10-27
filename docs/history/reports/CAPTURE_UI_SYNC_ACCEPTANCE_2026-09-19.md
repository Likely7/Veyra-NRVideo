# 采集、截图、字幕与显示同步修复记录

日期：2026-09-19。分支：`codex/capture-ui-sync-20260919`；前置存档 `7761f34`。

## 修复及证据

- 音频：PCM 与压缩音频回调原来因 GetTime 失败而拒绝有效数据。现在缺失时间戳时使用样本数连续计时，断点重新锚定，有真实时间戳时优先使用。六项时钟合同检查通过，WASAPI 抖动输入测试通过。
- 音频：确认 Dolby/DTS 的解码和直通入口误用只接收 PCM 的 NativeSink。新增独立压缩音频 sink，保持 codec/媒体类型变化必须重连，压缩包不套 PCM 对齐限制。回归包含无 WAVEFORMAT、无时间戳、奇数字节包、空包拒绝、codec 变化拒绝、未知类型拒绝及原 PCM/视频合同，退出 0。
- 音频：发现 pin 和选择格式时补读 IAMStreamConfig::GetFormat，兼容枚举失败但当前格式可用的驱动。没有按品牌随意绑定麦克风，没有给 GC551 套 GC553 私有开关。此驱动兼容分支通过编译与代码检查，尚无对应实卡证据。
- 采集：callbackFps 改为近两秒窗口，停止到帧一秒后归零；不再用启动以来的累计平均代表当前输入。记录回调到锁获取、到复制完成的耗时与字节数，保留设备协商及 mailbox 丢弃诊断。没有软件重复帧凑 60，也没有扩大队列。**尚不能认定 GC573 的 57fps 是读数问题或已修复真实掉帧。**
- HDR 截图：增加源格式、尺寸与各阶段 HRESULT；继续 FP16 scRGB JPEG XR 无损编码、解码完整性检查和原子落盘，不覆盖已有文件。生产路径撤掉逐位相同比较门禁，独立测试保留像素比对。真实视频 NR+HDR 直接处理图截图通过；真实引擎四组合各播放/暂停两次，共八张截图保存和重开通过。本机 SDR 显示器上的引擎组合测试走 SDR 输出，不能冒充 HDR 显示器验收。用户原始错误本机未复现，不能据此断言已经定位其根因。
- 字幕：轨道仍使用选择菜单；样式/延时使用常驻窗口，字号、底部距离、延时、目标行数、字体、描边、背景可连续调节。默认目标两行，0 为自动，1–8 自定义；完整测量文字后缩放布局，不通过丢弃第三行实现两行。偏好格式 v7，v6 迁移默认两行。连续 20 次编辑、无效输入、失焦恢复、延时、复开、关闭、宿主销毁测试通过。补充跨 DPI 调整和初始工作区定位；复杂 ASS/双语/PGS/多 DPI 视觉矩阵尚未全部验收。
- 音量：上下键每次 5%，支持重复；普通模式视频区域及音量条滚轮调整音量，高精度滚轮累计；调节解除静音。文本框、列表、其他滑杆、弹窗不抢键。专业模式视频滚轮继续缩放。音量条跟随引擎值刷新。未添加原方案的独立 OSD 或滚轮用途设置，本轮以用户要求的快捷调节为范围。
- 显示同步：区分帧节奏和显示同步。XeSS 保留 SDK 调度，只将所选 VSync 传给代理 swapchain，不叠加外部等待；完全关闭时 VSync 和应用等待均关闭。DLSS/普通 DXGI 的 VSync 不再依赖最大排队 API 成功。记录实际 Present interval/flags/HRESULT，计时不包括新增日志开销。自动模式明确 VRR 未知；FSR 仍明确显示同步未支持。

## 实际运行

构建入口：`scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/slider-reset-20260919 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/capture-ui-sync-20260919 -DisplayVersion 1.4.2 -Targets ...`。

测试统一使用 `scripts/run-short-test.ps1`，每次不超过 240 秒，GPU 用例顺序执行。日志目录 `E:/项目/Veyra/logs/capture-ui-sync-20260919/`，证据目录 `E:/项目/Veyra/tests/capture-ui-sync-20260919/`。

| 用例 | 实际结果/日志前缀 |
| --- | --- |
| PCM/压缩音频/视频 sink 合同 | 退出 0，capture-color-final |
| 音频抖动输入 | 退出 0，audio-jitter |
| UI 偏好迁移与时钟合同 | 退出 0，ui-contract-final |
| 字幕面板连续调节 | 退出 0，subtitle-panel-final |
| NR+HDR 真实视频图直接截图 | 退出 0，hdr-real |
| 引擎截图四组合×播放/暂停 | 退出 0，screenshots-final |
| HDR 高光、宽色域独立像素比对 | 退出 0，hdr-integrity |
| XeSS 4X+VSync，6秒 | 退出 0，xess4-vsync；Present sync=1 flags=0 |
| XeSS 2X+完全关闭，6秒 | 退出 0，xess2-off |
| DLSS 4X+VSync及生命周期 | 退出 0，dlss4-vsync-lifecycle |

测试素材 `E:/项目/Veyra/tests/1.4.2beta/visible-scene.mp4`。Pacing 参数依次为素材、证据目录、模式、显示策略、倍率、秒数、场景；上述三个用例分别为 `0 1 4 6 xess`、`-1 1 2 6 xess`、`0 1 4 6 lifecycle`。

过程失败如实保留：首次 UI 编译出现 std::max 类型不一致，修正后通过；一次构建指定不存在的 veyra_capture_source_tests，改用实际目标；截图矩阵首次错误地假定 SDR 显示器也生成 JXR，修正测试预期为实际输出合同后八项通过。这不是原用户 HDR 错误复现。

## 上游核对与限制

核对 OBS `obsproject/libdshowcapture` 固定提交 `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`，本地只读参考 `E:/项目/Veyra/deps/libdshowcapture-audit-20260919`。重点是 `source/device.cpp` 的 GetTime/SetupAudioCapture/GetFormat。没有复制其实现代码，也没有引入运行库。

本机未枚举到 GC551/GC573/VC-007PRO。不能宣称两张圆刚卡无声和掉帧已实测解决；后续实卡需比较请求/连接帧率、实际回调、复制耗时、处理 FPS、mailbox 丢弃及音频协商 HRESULT。不同显卡和驱动、HDR 显示器、VRR、物理屏幕撕裂与到眼延迟仍需独立验收。普通软件截图不能证明无撕裂。

本轮没有打包、推送或发布；构建程序位于 `E:/项目/Veyra/build/slider-reset-20260919/veyra.exe`。没有新增 SDK、模型或运行库到源码 Git。
