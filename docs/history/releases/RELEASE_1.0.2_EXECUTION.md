# 1.0.2 重连修复与发布记录

用户授权修复并发布1.0.2；基线aad8ce4，开工标签checkpoint/ps5-retry-budget-20260913，施工codex/release-1.0.2。包含此前未发布的PS5占用重试、30秒首帧等待、具体错误提示及日志跨重启保留。

## 现场与设计

现场副本logs/ps5-network-20260913/incident.log和analysis.json仅保留本地。23:16:42前约60fps/47–51Mbps，之后完整输入与解码停在8116，缺包/传输/组帧错误增加；decodeBusy=-1、队列0，未见增强超时。用户确认PS5使用Wi-Fi，电脑走1Gbps以太网；事后12次ping均2–4ms无丢包，不能排除瞬时无线故障，也不能由上游统计直接认定路由器丢包。23:16:48软件因之前已累计重试3次而放弃，再次证明“一整个用户连接只许3次”不适用于持续游玩。

改为最多3次/连续故障：连续解码30秒、相邻帧间隔均小于1秒后恢复额度；断档、倒退时间、新会话重新计连续时间。保留累计重连次数用于sourceEpoch与历史隔离；UI/退避使用本轮次数，避免额度归零导致epoch回退或移位异常。1/2/4秒退避、用户停止优先、明确拒绝不重试仍保留。仅测试显式环境变量可在额度恢复后再次停止测试自身传输，以验证第二次断流；正常GUI不设置该变量。

## 验证与发布

构建、离线回归、重复断流实机、包内启动和上传校验结果见下文。七个增强运行文件与patched FFmpeg不变，源码/运行包分离，发布完整便携ZIP、RemotePlay/FFmpeg对应源码ZIP及各自SHA256文件。所有测试单次不超过300秒。不宣称Wi-Fi断流根因已修复。

### 实际构建与恢复回归

- `cmd.exe /c out\ps5-reconnect-build.cmd`通过相关产品/核心测试构建；`cmd.exe /c out\gpu-dis-build-all.cmd`完整21步增量构建通过。EXE ProductVersion=1.0.2，SHA256 B693547F722C01E583F1B549507E355540618B628740147154A212F61D3BFFE2；桌面快捷方式指向同一out/remoteplay/product-repair/veyra.exe。
- `scripts/run-short-test.ps1 -Exe out/remoteplay/core-windows/veyra_remoteplay_core_tests.exe -TimeoutSeconds 30 -LogPrefix logs/release-1.0.2/core`：77/77。新增三次耗尽后连续30秒恢复、累计编号保持3→4、断续帧不恢复额度检查。此前aad8ce4的107项产品合同/GUI日志保留/手动实机记录另见PS5_RECONNECT_REPAIR文档，本轮不冒充重跑。
- `scripts/run-short-test.ps1 -Exe out/remoteplay/product-repair/veyra_live_presentation_tests.exe -Arguments @('--last-paired-ps5','logs/release-1.0.2/repeat-outage','--repeat-outage') -TimeoutSeconds 150 -LogPrefix logs/release-1.0.2/repeat-outage`：115秒观察PASS，episodes=2、resumedWithFgAndAudio=1、postHealthySamples=2319、idle=1。第一次15:23:51.837Z恢复；15:24:21.847Z额度续期，随即测试第二次停止自身传输；15:24:31.655Z再次出帧；15:25:01.683Z再次续期。是可控故障，不是复现Wi-Fi自然断流。
- 实机NR Feature18 Create=0x1/非空handle/SEH=0，DLSSG 3840x2160 Create=0x1、Release=0x1、evaluates=4851。第二次恢复后呈现约100–118fps，音频输出推进；没有宣称固定120fps或主观手柄/声音验收。

### 便携包与源码

`scripts/package-portable.ps1 -Root . -Version 1.0.2 -OutputDirectory out/releases/1.0.2 -BuildDirectory out/remoteplay/product-repair`成功。`package-remoteplay-source.py --version 1.0.2 --output out/releases/1.0.2/Veyra-1.0.2-RemotePlay-source.zip`与`package-ffmpeg-source.py --version 1.0.2 --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/ffmpeg-ps5-slices-source --output out/releases/1.0.2/Veyra-1.0.2-FFmpeg-source.zip`成功。Python使用Codex本地runtime。

| 资产 | 字节 | SHA256 |
| --- | ---: | --- |
| Veyra-1.0.2-win64-portable.zip | 303913945 | 12D8B1F3CA891EF14B76117E43093A814CC8C2DC76D874B17B62FFD2C10D152D |
| Veyra-1.0.2-RemotePlay-source.zip | 143744092 | 9DA3A6B6CD51BB137249311765457B6C4B708F5159AEA679E9DC69969D3C152E |
| Veyra-1.0.2-FFmpeg-source.zip | 23253198 | 3E885B2A9FE33867B9F2C9DF0C098A32CEF480083C2391A40E9E2691C15AD747 |

各附.sha256，共六个发布资产。便携99文件，远程源码17714文件，FFmpeg源码10451文件；路径/逐文件大小与哈希/源码禁用二进制检查通过，FFmpeg上游.mp4后缀文本参考按内容核实保留。README相对链接通过。七运行文件身份与1.0.1一致，原版签名Valid、社区NR仍HashMismatch，许可证见RUNTIME_COMPONENTS_1.0.2；FFmpeg avcodec保持0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。

独立便携smoke初次7秒检查在原版DLSS组合失败：初始化和自动seek后只有1个处理帧、0生成帧，NGX没有失败，但测试结束前只走完预热。保留logs/release-1.0.2/portable-smoke，不计作通过。脚本新增可选CaseSeconds（默认仍7，范围7–20），本轮使用15；保持真实生成数、模块路径、截图、manifest可移除等原断言及35秒单项硬超时，没有放宽产品FG准入。

最终命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/portable-smoke.ps1 -PackageDirectory out/releases/1.0.2-verify/Veyra-1.0.2-win64-portable -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-1.0.2/portable-smoke-15s -CaseSeconds 15`。5/5 PASS，每项15.54–15.85秒；基线817帧，社区528处理/526生成，原版562/560，Video SR707/665。精简PATH、独立解压目录、manifest临时禁用仍加载包内模块成功；结果和失败日志均保留。未改ZIP中的产品文件，源码包只需随标签提供新增测试脚本，无依赖源码变化。

源码改变：StreamRecovery.h、RemotePlaySessionSource.cpp、CoreTests.cpp、LivePresentationTests.cpp、portable-smoke.ps1、版本/README/构建组件说明与本记录。上一修复aad8ce4同时随本次标记发布。git diff --check及变更路径检查通过，不含SDK、运行二进制、日志或凭据。

### GitHub 发布完成

main及附注标签v1.0.2已推送到nrvideo（Likely7/Veyra-NRVideo），标签指向09c054c。六个资产均为uploaded，逐项名称、字节数、服务端SHA256与本地文件匹配（包括三个.sha256文件自身的摘要）。核验记录为logs/release-1.0.2/github-draft.json。

2026-09-13T15:32:29Z已公开为latest，非草稿、非预发布，Release ID 387942841；再次查询releases/latest确认v1.0.2及六资产，记录logs/release-1.0.2/github-published.json。发布地址：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.2 。未修改旧版本资产。发布后只追加文档记录，不重打包、不移动标签。下一步由用户在日常Wi-Fi环境下长时游玩验收，网络瞬时断流仍不属于已消除的问题。
