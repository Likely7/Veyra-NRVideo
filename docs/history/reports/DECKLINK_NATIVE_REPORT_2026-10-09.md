# Blackmagic 原生采集开关与本机验证

用户要求修复持续黑屏，并追加“做成和美乐威一样的开关”。已在 `codex/decklink-native-20261009` 实现并构建；原生接口、GPU 解包、界面及普通 USB 回归通过。**本机没有 BMD 卡，尚不能宣称反馈者的黑屏已恢复或实测延迟降低。** 未打测试包、合并 main、推送或发布。

## 使用行为

- 采集设置中，“Blackmagic 原生采集”位于“美乐威低延迟模式”旁，默认关闭，保存选择。选中 BMD 时可启用；普通 USB 卡不会被当成 BMD。
- 关闭使用原来的 DirectShow；开启后重新选择原生设备、格式并连接。设置不会立即中断正在播放的来源。两种接口分别记住明确使用过的设备身份，多张原生卡不猜映射。
- 支持的卡可选自动检测；不支持自动的卡保留手动制式。HDMI/SDI 沿用 Desktop Video Setup，本功能不改驱动永久配置、端口或 EDID。
- 原生路径先确认真实有效帧。无输入标志、模式/颜色变化和驱动错误有明确日志；有效黑画面仍正常交付。输入改变时沿已有恢复/处理图重建流程重置历史。

## 对原故障的判断

上一轮核对 `veyra-qml(45).log`：10-09 的 9 次配置全部启用了前次兼容接收器，已记录的 41 次原始 CPU 样本抽样均为黑场值，位置在上传/GPU 处理前。这个证据排除了“只是 NR 或后续渲染把正常输入变黑”的解释，也说明前次 allocator 修改在现场未解决问题。

配置成功和持续回调不能证明 HDMI/SDI 信号有效。新增原生路径补上此前缺少的实际模式、RGB/YUV/位深回调及 no-input/lock 诊断，避免继续只凭枚举模式猜输入。本例具体是输入口、制式/颜色错配还是 DirectShow 驱动兼容，仍需实卡日志确认；用户反馈 OBS 通用来源也可出画，不能据此宣称 DirectShow 天然不可用。研究证据详见 `BLACKMAGIC_SIGNAL_RESEARCH_REPORT_2026-10-09.md`。

## 实现与延迟边界

使用已安装 Desktop Video 的 COM 服务；稳定 persistent/topological ID 包含子设备身份，重复身份拒绝使用。配置/重配在所有者线程，回调只提交状态或交付样本；停止先分离并等回调退出，再释放采集/音频存储。

自动检测沿 OBS 的 Pause → Enable → Flush → Start 顺序重配；有效帧等待默认 2.5 秒，上限 5 秒，属于连接阶段等待，不是逐帧缓存，也不包含驱动自身 COM 调用耗时。回归另覆盖驱动在 EnableVideoInput 中就报告新制式的情况。

复用现有三个帧存储和容量 1 mailbox；原生回调一次完整 CPU ingest copy，可直接写入现有 D3D12 upload buffer，未新增常驻 FIFO、逐帧 CPU fence wait 或 GPU 像素回读。10 位 v210/RGBXLE 走共享 GPU 解包，正确处理行距、范围、PQ/HLG 和 BT.2020；旧 8 位 packed HDR 防错保持。仅 SDR 可回退 8 位，不支持的 HDR 元数据/EOTF 明确失败。GPU→CPU 读回仅用于颜色测试。

原生内置音频当前协商 48kHz/16-bit/stereo，复用 CaptureAudioSession；用户指定的独立 WASAPI/DirectShow 音频沿原路径。不宣称原生多声道、专门去隔行、BMD 全型号或真实输入到屏幕延迟已验收。

## 构建与验证证据

工作树 `E:/项目/Veyra/worktrees/decklink-native-20261009`，基线 `1d7cd55`。全部新产物在 `E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/decklink-native-20261009`；SDK 位于 `E:/项目/Veyra/deps/decklink-sdk-obs31-20261009`。

| 检查 | 结果与证据（logs/decklink-native-20261009 下） |
|---|---|
| 完整产品与定向测试构建 | `build-20261009-203938/receipt.json`；保留此前全部已验 HDR/字幕/PR31/播放列表对象，按当前头文件依赖闭包重新编译/校验，旧缓存只读 |
| 最终 UI/翻译增量构建 | `build-20261009-205406/receipt.json`；18.953 秒，非 UI 测试 EXE 哈希前后不变 |
| CMake 集成 | `build-20261009-204033/receipt.json`；原生 SDK 与不配 SDK 两种全工程配置成功，分别经 CMake 实际编译适配器对象，SDK 头文件经 MIDL 生成 |
| 原生产品状态机 | `tests-20261009-204033/receipt.json`；SDK/no-SDK 两个变体各 25 项通过，含真实回调线程、早期检测、无信号、有效黑场、精确时间、格式/HDR变化、错误/超时/停止释放及无驱动显式失败 |
| 旧采集颜色/格式 | 同上；261 项颜色合同通过，格式身份/排序测试 exit 0 |
| 实际 GPU 颜色 | 同上；148 条通过记录，包含 24 组新增 10 位测试：v210/RGBXLE × full/limited × SDR/PQ/HLG × CPU/upload 帧，对照独立双精度参考；日志无 pass=0 / D3D12 ERROR / CORRUPTION |
| 普通 USB 源帧率/重连 | `tests-20261009-204508/receipt.json`；KUHAIMI 27P 1080p60，重连前后回调 59.926940 / 59.997117 FPS，361 / 362 次读取，PTS 均递增 |
| 既有音频恢复 | 同上；真实静音 WASAPI A→B→A→默认/重复选择、端点丢失、慢启动、瞬态、Xbox float/RTP 五项通过；Windows 默认端点未改 |
| 实际界面开关 | `gui-switch-20261009-205501/receipt.json`；新配置默认关闭、保存开启后重启恢复、经生产开关关闭、列表过滤、无驱动提示、普通卡隔离和英文三阶段通过，无 QML 错误 |
| 最终 GUI 普通 USB 实采 | `gui-usb-20261009-205516/receipt.json`；11.953 秒，连接/播放/截图/断开通过，回调 59.938308 FPS |
| 翻译 | `reports/decklink-native-20261009/translations.json`；新增 41 键均有繁中/英文/日文，2106 个原条目未变，占位符错误 0；仍有 29 个遗留缺译，不冒称全项目翻译完成 |

命令均在本工作树执行：

```text
python -B scripts/acceptance/decklink-native-build.py
python -B scripts/acceptance/decklink-native-build.py cmake-check
python -B scripts/acceptance/decklink-native-build.py ui-only
python -B scripts/acceptance/decklink-native-test.py native no-sdk legacy-color selection gpu usb-rate endpoint-switch endpoint-loss slow-start transient xbox-float-rtp
python -B scripts/acceptance/decklink-native-test.py usb-rate endpoint-switch endpoint-loss slow-start transient xbox-float-rtp
python -B scripts/acceptance/decklink-native-gui-test.py switch
python -B scripts/acceptance/decklink-native-gui-test.py usb
python -B scripts/i18n/extract.py --check
python -B scripts/acceptance/decklink-native-control.py final
```

最终 GUI SHA256：`30B0898A97E101E7C9E5A46AACCD9BD16E56AE6F539D35B4917E41227D710D7D`。两个最终 GUI 回执使用同一 EXE，并各自核对原便携包 1614 文件哈希不变；全部用户配置在 E 盘私有 profile。截图和证据在对应 logs 目录。

## 构建接入与来源

后续正常构建必须显式配置 `-DVEYRA_DECKLINK_SDK_DIR=<外部 Win IDL 目录>`。未指定则编译不可用的原生 stub，DirectShow 仍可用；指定无效目录则 CMake 明确失败。不能只带 UI 就声称原生后端已随包构建。MIDL 生成头文件只在 binary dir，不进入源码 Git。

生命周期改造来源为 [OBS e7f0b0d 的 DeckLink 插件](https://github.com/obsproject/obs-studio/tree/e7f0b0d43c538f5d108aa4cbea473d57b3012a1a/plugins/decklink)，GPL-2.0-or-later；SDK 12.0 Win IDL 原件来自 [OBS 31.0.3 固定提交 fcd1910b](https://github.com/obsproject/obs-studio/tree/fcd1910bf5116b69404a6ecdda6efedd1d00ebdf/plugins/decklink/win/decklink-sdk)，许可/来源见 THIRD_PARTY_NOTICES。未搬入带非商业限制的 VideoProcessor 代码，无驱动或 SDK 分发、运行库修改。

## 失败记录与收尾

首轮局部构建暴露旧静态库依赖闭包遗漏、SDK 枚举名不符、链接工作目录相对库路径、音频测试误引入无关编码对象，均修正后重新编译/链接，失败 build 日志保留。不将首轮失败当作通过。CMake 检查首次错误指向仅有 DLL/DEF 的 patched FFmpeg runtime prefix，改用已验 MSVC import-library prefix 后两种配置通过，运行仍用已验含 zlib/PS5 slice 补丁的同版本 FFmpeg。

帧率测试首次进程 exit 0、两段 PASS，审计脚本却把 `PASS RATE pass=0` 的阶段编号当 GPU 布尔失败；现仅对 GPU 记录解释 pass=0，原日志保留，真实 USB 重测通过后继续音频回归。首次复用旧 GUI 脚本将单张测试截图写到 Pictures，已按精确路径/哈希移入本轮 E 盘 evidence；之后私有 profile 显式设置 screenshotDir，新截图全在 E 盘，未移动用户文件。

范围守卫保护的 42 个既有工作树、495 个脏文件和 main `0e7018ad` 保持原样。最终回执/源码提交/bundle 信息见 `E:/项目/Veyra/reports/decklink-native-20261009/final.json`。实卡仍需验证实际输入锁定、自动/手动出画、拔插/制式切换和输入到屏幕延迟。
