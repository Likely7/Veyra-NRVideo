# 2026-11-18 main整合与项目整理结果

用户授权整理Claude/Codex各分支修复、功能并合main。原main `54aa357`，虚拟摄像机工作树基线`bf596e1`，完整原样产品快照`740534e`（72文件，含Codex接入及Claude节拍修复），已快进main，无代码冲突；3条已有README/发行说明文档提交一并保留。本轮单Agent自审，没有独立Reviewer验收，不push/新Release/关机。

## 本轮包含什么

- 处理后完整尺寸虚拟摄像机、录制旁开关、四语设置、SDR兼容输出、独立32/64位设备与安全系统组件安装器。
- Claude协议2六帧有序共享环、消费者自动帧率、GPU完成事件唤醒、按录制顺序发布、稳定自适应延迟与OBS节拍对齐，更新组件及3X→60节奏提示。
- 之前精修光流、回放/F9/麦克风、采集监听/录音解耦、浏览器全屏XeSS修复、双GPU、AMF FRC、FSR4回退、全效果剔除编辑、日落/音乐修复等均已在main，未重复倒合旧文件。核对了所有本地分支和工作树，也检查了旧脏树的产品文件历史blob；旧UI文件已有后续整合版本，原树保留。
- Lecram两线继续暂停；AverMedia未完成诊断与原生音频驱动原型仍隔离，不作为已完成产品功能合入。

## 本次实际验证

从唯一main源码`E:/项目/Veyra/worktrees/main-claude-anr-20261113`全新配置，构建任务`codex-main-integration-20261118`；10目标641步，325.3秒，通过。脚本为`build-recording-handoff.py --task codex-main-integration-20261118 --root <main源码> --label 2.1.9-main-integration-test --jobs 2`，包含产品、QtQuick/UI合同、摄像机GPU/传输/探针/节奏、回放/时序/修复合同目标。附加Win32传输/探针构建通过；保留patched FFmpeg依赖。

| 验证 | 本轮结果 |
|---|---|
| QtQuick | 209通过，0失败/跳过 |
| 摄像机GPU | 523项，D3D12调试错误0；颜色/HDR代理/奇数尺寸/生命周期/动态节奏 |
| 传输 | x64/x86各73项通过 |
| 回放缓冲 | 37通过/0失败 |
| 修复合同 | 328项/0失败；时序与UI合同退出0 |
| 翻译 | 2939词条，繁中/英/日缺失0，占位符问题0；4条既有废弃启动音乐键只报告、不修改 |
| 产品4X→60 | 420稳态样本，59.99 FPS，0重复/倒退；均匀平移步长4px |
| 产品4X→120 | 839稳态样本，119.99 FPS，0重复/倒退；均匀平移步长2px |
| 摄像机+录制+回放 | 同时开启成功，录制267帧/回放275帧、1080p，两份文件独立完整解码无错误；素材30FPS、静音，不冒充60张源图或采集音频验收 |
| 着色器 | 本次89个逐SHA同步，无不一致；测试QML探针已恢复 |

实际程序动态帧测试采用本次新EXE/DLL与30FPS纹理平移片、DirectShow直接消费者，未在本轮重跑OBS录像；Claude前轮的OBS逐帧结果在[VIRTUAL_CAMERA_CADENCE](VIRTUAL_CAMERA_CADENCE_2026-11-17.md)，两类证据明确区分。本轮EXE SHA256 `6c1a1fc16bb534d2b11fca3843df96726e28a9c9a5de3b0e24f70ca3c795a8c1`。本机RTX5070短测不扩展到AMD/Intel/用户5090、实卡、其他直播软件、长期直播或物理屏幕延迟。管理员OBS加载新系统组件尚未在本轮验证；系统旧模块保留，需本人在设置点“更新组件”并确认Windows管理员界面。

## 项目整理与保全

45个已合分支引用归档，其中23个干净E盘工作树退出；移除前核对分支tip、状态、未跟踪/忽略本地文件、reparse和在用进程。恢复包`archives/codex-main-integration-20261118/completed-branches.bundle`已verify并核对45个ref，94032300字节；SHA256 `eef0c536c80c1fd4ead3ee5a9bfa89cd43b7f68820a12b8f700bac7b871a960a`。9个脏工作树保持，4个隔离分支保留，摄像机原树暂留源码对照。源路径字节减少2100030553；这是路径统计，不是实际物理磁盘回收数，也未抵消本轮构建新增。

AGENTS、CURRENT_STATUS、DEVELOPMENT收拢为单一当前入口，旧全文移入docs/history；E盘根AGENTS已加当前入口并保全旧文。补齐OBS/libdshowcapture七个修改文件与提取HRESULT帮助函数的来源说明，原始固定提交/hash/许可证保留，未新增运行库/SDK/模型到Git。

恢复旧2.1.9 NVIDIA发行目录被测试硬链接误写的package-manifest.json：从原ZIP取原字节后原子替换，恢复SHA `fb19ef3c6a8945a94910b37430a9c01040fb98406a38b39f34b48069ebb092ef`，旧误写内容留报告；ZIP SHA `4225ecb11a8e96c2826b4b0b798feb811d7ae237f3edf0b627f3b6895c79f362`与既有SHA256SUMS一致。qa.py的JSON写入改用原子替换，防止重现。恢复脚本首次编码名utf8-sig拼写错误已修、保留失败；收尾注册核对脚本曾使用错误测试GUID/命令输出编码，后改正确GUID与Unicode winreg并实际移除本任务x64/x86临时注册。系统HKLM和原用户配置未改。

## 当前入口

- main源码：`E:/项目/Veyra/worktrees/main-claude-anr-20261113`
- 构建：`E:/项目/Veyra/build/codex-main-integration-20261118`
- 本地测试启动：`E:/项目/Veyra/tests/codex-main-integration-20261118/启动main整合测试.cmd`，私有manual-profile从已确认HDMI音频测试配置复制，不改原配置、不自动连接源。
- 验收/归档/恢复：`E:/项目/Veyra/reports/codex-main-integration-20261118/FINAL.json`、同任务REGRESS/NATIVE-REGRESS/APP-CADENCE/PARALLEL-CAPTURE/MANIFEST-REPAIR/ARCHIVE-RESULT；归档恢复见[BRANCH_ARCHIVE](BRANCH_ARCHIVE.md)。

应用仍为2.1.9-main-integration-test；原正式ZIP/虚拟摄像机附加ZIP不重打，不把当前QA运行目录当新发行包。后续任务从本main与本轮构建继续。
