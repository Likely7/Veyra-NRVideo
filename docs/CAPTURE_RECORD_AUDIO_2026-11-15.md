# 采集卡录制与本机监听解耦（2026-11-15）

## 原因与修复

截图反馈2.1.6关闭音频监听后录不到采集卡声音。对照v2.1.5与v2.1.6，WasapiAudioSink的录制取声位置和CaptureAudioSession没有变化，均在音量/静音处理前分发PCM。采集面板的“不监听音频”对应的却是kCaptureAudioDisabled：没有建立音频输入，录制没有可取的PCM。不能仅凭反馈把原因定为麦克风接入引起的版本回归；本次修复的是输入与监听混在一个选择里的实际限制。

现在先明确选择“音频输入”，再独立开关“本机音频监听”。关闭监听只把采集源的输出增益置零，音频输入、时间轴、录制及即时回放继续运行；播放器音量和静音独立保留，不影响文件、PS5/PC/Xbox串流的输出控制。采集对话框、专业音频页和影院音频对话框共用captureAudioMonitor偏好，默认true保留旧行为；首次启动、实时操作、音频恢复和采集重连均应用该值，首次PCM输出前已设置增益。

原先的“不监听音频”准确改名“不采集音频”：选择它仍不打开任何音频设备。想录采集卡声音必须先选择采集卡音频输入，再关本机监听；没有音频输入时无法凭空生成游戏声音。不会自动打开系统默认麦克风或猜测其他设备。录制仍复用原有PCM tap、NVENC/MF与麦克风混音/双轨链路。

## 本次验证

- 构建：`py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-capture-record-audio-20261115 --root E:/项目/Veyra/worktrees/capture-record-audio-20261115 --label 2.1.6-audio-monitor-fix-test --jobs 2 veyra_qml_ui veyra_capture_audio_tests veyra_replay_buffer_tests veyra_qml_quick_tests veyra_ui_contract_tests`，585项、305.9秒通过。
- 新增生产链路集成测试`veyra_capture_audio_tests --record-monitor-off`：真实CaptureAudioSession转换/AudioRenderer渲染，注入220个10ms立体声PCM块，增益0。录制消费者收到52800帧、回放消费者105600帧，峰值均0.25、时间戳单调；移除一个消费者不停止另一个。诊断抓取的210240个渲染样本峰值严格为0，210240个录制前样本峰值0.25。没有打开物理采集卡或麦克风。
- 回放缓冲37/0，UI合同通过，QtQuick进程退出0；翻译2876项，三语缺失/占位错误0。QtQuick标准输出为空，不杜撰测试项数。首轮私有stage遗漏Qt6Test/Qt6QuickTest，次轮遗漏qml-tests目录，均在测试启动前失败；补全测试环境后运行47.17秒、退出0，失败回执保留。这些测试组件不进入交付包。
- 真实程序：中英日繁四语的输入/监听控件、开关off/on/off、录音选项不变、绑定更新及保存通过；重启读取false并核对引擎应用日志通过。英语和中文截图已检查。
- 实际NVENC录制与即时回放：sound-only、mixed、dual三组，共6个MP4，独立FFmpeg严格解码通过。1000Hz游戏音与合成2000Hz麦克风频谱验证：单独游戏音一轨、混音一轨两种声音、双轨各自对应声源；所有音轨均有非零声音。合成麦克风不冒充真实麦克风或Elgato验收。

精确脚本、构建回执、PCM、逐文件解码、截图和失败记录在`E:/项目/Veyra/{reports,logs,tests,tmp}/codex-capture-record-audio-20261115`，主回执`reports/codex-capture-record-audio-20261115/FINAL.json`。调用入口为该reports目录的qa.py（stage/native/quick/ui/restart/sound-only/mixed/dual）与matrix.py。

## 边界与交付

本机RTX5070；Elgato实卡、其他GPU与无可用音频输出端点的环境未验证。本实现仍使用既有WASAPI输出端点，以零增益关闭本机监听，不宣称新增无输出设备录音能力。独立分支codex/capture-record-audio-20261115，基线本地main 2f0e499（含此前已合入的PR104/108）；其他Claude工作树、用户配置、驱动与正式包不改。

本地完整NVIDIA测试包、精确应用源码及摘要放`E:/项目/Veyra/test-packages/codex-capture-record-audio-20261115`，构建保留`build/codex-capture-record-audio-20261115`。运行库沿用公开2.1.6原件，不带ANR或测试音频/个人数据；最终文件名、SHA与打包验证以FINAL回执为准。未合main、未push、未发布GitHub Release、未关机。

封包已完成：产品源码dfa2328853b2019b7002702d01d66d833702923b，EXE be83186dff151fc2437800337b01e506681aec8f0f06dc4935c34e06020f8d44。完整NVIDIA ZIP725809972字节、SHA69977c0eba15e62520d5503851873c7aa553eba592e6841761d7d8443a106693；对应源码SHAa97e6d48f18495ec55fbbc78508ddb00b101b55c97af5f92a08d0cb44d2db73e。1664个包内文件逐SHA/ZIP CRC校验，97个DLL均与正式供体相同；公开包内无ANR、测试DLL/媒体/配置。首次封包的六文件保护规则拦截本地main自带PR104/108的其他QML变动，未生成可交付ZIP；改为按精确Git差异审核资源，补含新增NvidiaVrrProfileRow/qmldir。此为封包清单修正，未再改产品代码。失败记录package-attempt1.json与package.py命令保留。

package.py完成源ZIP、完整包及清单后，cold.py在该完整包目录、最小PATH、全新私有配置下启动：退出0、176真实提交帧、0错误，监听false在引擎启动时确认。手动入口“启动采集录音修复测试.cmd”使用独立manual-profile，默认监听false/录制声音true，不自动连接任何设备。完整包/app/build/六输出/必要证据保留，未复制用户数据。最后仅追加本段交付记录，不改变产品源码或已校验包字节。
