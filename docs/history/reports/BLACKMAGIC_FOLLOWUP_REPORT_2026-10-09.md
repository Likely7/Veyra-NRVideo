# BMD 黑屏跟进：本地候选

用户提供 `veyra-qml(43).log`，确认 OBS 通用视频采集和 Blackmagic 来源都可显示。本轮完成 BMD 原生视频的驱动交帧兼容候选；没有 BMD 实卡，不能宣称现场根因已证实或黑屏已经修好。

## 事实

日志七次连接都持续收帧/送显，增强关闭仍黑；HDYC 已原生直连，说明上次格式修复确实存在，但两入口均 `inputMediums=0 crossbar=false`，crossbar 路径没帮助该卡。44 次 CPU 原始样本统计中 38 次是黑场值、6 次 ARGB 全白，没有证据显示真实内容恢复。GetFormat 是配置读回，不能当信号检测。卡型号、实际信号制式、HDMI/SDI 仍未知。

上轮报告仅验过合成接口、本机 USB 和 GPU 颜色，BMD 真卡始终未通过。黑场内容、驱动占位帧、制式/输入错误、缓冲兼容不能只凭这份日志区分。

## 改动及来源

对照 OBS libdshowcapture `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`（LGPL-2.1-or-later，Copyright (C) 2023 Lain Bailey），复用 CapturePin allocator 方法、terminal renderer 标记和 HDevice::ConnectPins 的输出选择类型逻辑；源码与 THIRD_PARTY_NOTICES 已标注。

- BMD 原生接收器不再主动提供通用 MemoryAllocator、缓冲要求或 N4 建议，让输出 pin 选择实际 allocator；NotifyAllocator 后保留实际对象/只读状态及 alignment/prefix/数量诊断。
- 该兼容接收器暴露 renderer 标记，保留 canonical IUnknown、真实 ReceiveCanBlock=S_OK 与原 Stop/flush/短帧检查。
- BMD 保留驱动 DIB 方向；SetFormat/GetFormat 后由输出 pin 提供 ConnectDirect 类型，严格核对 subtype、尺寸、stride、颜色、帧时长和 VideoInfo2 隔行标志，动态扫描变更要求重开。
- 普通 USB/音频使用原工厂和缓冲策略；v210 仍走原系统 RGB 转换，本轮没有原生10-bit v210。没有黑场触发的换档、重连、丢帧或物理输入路由修改。
- `VEYRA_BMD_DSHOW_LEGACY=1` 供同 EXE 旧协商对照，默认 BMD 走新兼容方式。

提供 allocator 本身是合法 DirectShow 行为。旧版测试失败说明它不满足本轮 OBS 式兼容策略，**不是复现 BMD 黑屏，也不代表九个现场根因**。

## 验证

基线 main `dbfceabee804cd81bac4a4781f0b7b4cf272902e`，隔离分支 `codex/blackmagic-followup-20261009`。缓存产品 `71723e0` 与 main 之间只有已核对的清理/管理文档差异。重新编译两个 capture source 及 fixture，GUI/测试链接相同新产品 objects；缓存输入前后 SHA 不变。最终构建 receipt 由 `E:/项目/Veyra/reports/blackmagic-followup-20261009/build-current.json` 指向，含命令、源码、objects、缓存和 EXE SHA。

| 项目 | 结果 |
|---|---|
| 旧实现 + 新兼容约定 | 9 项预期失败，exit1；其余检查通过 |
| 当前采集/音频/格式/COM 样本 | 261 项通过、0 失败；真实 COM allocator、HDYC 字节交付/只读样本/Stop/flush/短帧/隔行变更/重连 |
| GPU 像素 | 124 项通过，HDYC 两方向逐像素等同 UYVY BT709 |
| KUHAIMI 27P YUY2 1080p60 | 初次 60.101426 FPS、重连 59.950521 FPS，PTS 单调；软件回调率 |
| 实际 GUI | 11.954秒 exit0，连接/关闭增强/截图/断开/属性入口状态通过，Main.qml loader 完整恢复 |
| 随包脚本 | PowerShell 解析通过；15秒私有配置启动/正常退出/生成 ZIP，源配置 SHA 未改 |

最终原日志在 `E:/项目/Veyra/logs/blackmagic-followup-20261009/delivery-tests`、`gui` 和 `launcher-smoke`。GUI 截图已查看，是 USB 卡产生的“无信号”图样：证明非黑像素进入/显示/截图，不证明实际 HDMI 内容或 BMD 通过。未验 BMD 两入口、AMD/BMD 联合使用、长稳或屏幕端到端延迟。无竞争 GPU/子 Agent，单测试均小于300秒。

## 失败记录

1. 构建脚本先因缓存缺采集测试 object 中止，改为从源码编译。随后错误组合 Ninja -t commands 的43条依赖命令，因 command line too long 未执行；改为只选/验证目标 cl.exe 行。最终缓存输入逐项 SHA 未变。
2. GPU 首次缺 EXE 相对路径 shaders 而 exit3；硬链接已核验原 shader 后重跑通过124项。失败保留在 contracts/gpu-pixels.log。
3. USB fixture 的 pass=0 是轮次，被包装脚本误当布尔失败；原产品 exit0/两轮 PASS 保留，修正解析并重新取得两轮证据。
4. 隔行旧版对照曾因回调计数级联导致13项失败；改为各自前后计数，最终9项旧版预期失败、当前261项通过。不把级联断言当独立故障。
5. 首个 overlay 包含 qml 下原 Qt plugin DLL，被后置白名单拒绝。失败 SHA/大小/原因在 package-failed-attempt.json；仅删除本任务该失败 ZIP，限定 qml/Veyra 后重新打包通过。最终不含 DLL/模型/PDB/LIB/SDK。

## 交付

`E:/项目/Veyra/test-packages/blackmagic-followup-20261009/Veyra-2.0.6-BMD-compat-test.zip`：9,069,313字节，SHA256 `5da4a452e2612a81fd5b0d63605809723547d63be631b241415cab7aff470a40`，108项，CRC和每项 payload SHA 通过。EXE SHA256 `f1597d657037fe5174fd8d25dbd72902dc3f5b142b8b2d260121844bf9d462ee`；manifest列明基线、源码 SHA、实际显示构建串2.0.6-vfg-next、BMD未验。对应完整源码 `Veyra-2.0.6-BMD-compat-source.zip` 的最终提交/CRC/SHA 见同任务 closure.json。

保留原包，将补丁覆盖到完整2.0.6便携副本，AMD/NVIDIA沿用各自运行库。先用 OBS 成功时的同设备/同精确制式/同像素格式测试基础画面，再开增强和断开重连。脚本默认新协商，-Variant Legacy 使用同EXE旧协商；3分钟输出 ZIP、私有配置。仍黑时必须补齐 OBS 有效模式/卡/物理口，或下一轮用原生 SDK 查询实际信号，不能继续将收帧成功当画面成功。

1614个 donor 文件前后核验；保留当前构建、一个共享依赖私有app和必要证据。38原工作树、495未提交文件及用户日志按独立 start 保护，旧 guard/start 不改。不改运行库/模型/驱动/用户配置，不合并 main、不 push/正式发布或关机；自查不冒称独立 Reviewer。
