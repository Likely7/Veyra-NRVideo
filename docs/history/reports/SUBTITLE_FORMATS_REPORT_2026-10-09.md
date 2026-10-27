# PGS / SUP 与其它字幕格式修复记录

基线 main `0e7018adab9b211d76dd05da40cfbaeb9d77cf2c`，实施分支 `codex/subtitle-formats-20261009`。用户确认截图中的五条 HDMV_PGS_SUBTITLE 不可用并非仅一位用户反馈，片源在其他用户电脑；本机没有这些实际失败文件或对应播放日志。

## 已确认的问题

原包具备 `pgssub` decoder，不能归因为没有 PGS 解码器。实际 FFmpeg n9.0.1 配置为 `--disable-zlib`，Matroska demuxer 因而拒绝 zlib 压缩的轨道数据。公开样本 [supsample.mkv](https://samples.ffmpeg.org/sub/PGS/supsample.mkv) 在原 DLL 上可以列出 PGS 轨道，但日志为 `Unsupported encoding type`、0 条 cue、usable=false。

固定同一个旧 probe EXE，只替换 FFmpeg DLL 后，同一文件变为 1 条位图 cue，1.001–9.009 秒、usable=true；未压缩的 [另一 PGS 样本](https://samples.ffmpeg.org/sub/PGS/Girl_With_The_Dragon_Tattoo_2%3A23%3A56.mkv) 前后均正常。两份原始输出、EXE / DLL / 样本 SHA 保存在 `E:/项目/Veyra/logs/subtitle-formats-20261009/pgs-ab-20261009-134011/receipt.json`。这证明一个可复现的封装支持缺口，不能据此断定所有反馈者的原因均相同。

另有两个代码缺口：外挂 SUP/PGS 原先只走 UTF 文本读取；PGS 只有 Matroska BlockDuration、没有后继 clear set 时，旧字幕缓存没有按该时长结束。后者根据 [Matroska 字幕契约](https://www.matroska.org/technical/subtitles.html) 修正，并用独立构造、不含 clear set 的文件验证 3/7 秒边界。

## 实施结果

- 在独立目录重建同一份 patched FFmpeg n9.0.1，仅启用 zlib，保留 PS5 H.264 slices256、dav1d 1.5.4 与 LGPL 配置。静态 zlib 1.3.2 使用动态 MSVC CRT `/MD`；没有新增 zlib DLL，没有覆盖旧 prefix 或修改任何专有运行时。
- 内嵌和外挂位图复用同一 FFmpeg 字幕解码路径。支持 SUP/PGS、VobSub IDX+SUB 的多个语言轨道；二进制 `.sub` 重定向至同名 `.idx`，文本 `.sub` 保持按内容解析。保留原始画布、位置、颜色和透明度。
- 接通并实测 MicroDVD、SubViewer/SBV、SAMI、MPL2、Spruce STL。SRT、ASS/SSA、WebVTT 沿用原有解析及 ASS 脚本/样式。选择文件的过滤器与同名外挂自动发现同步更新。
- 外挂读取进入可取消的后台 loader；一次插入 VobSub 多语言，保留副字幕、独立延时和自动对齐索引。坏文件不清空当前字幕，换源取消旧请求。
- 菜单区分正在读取、无字幕数据、解码失败、缺少 decoder 与缓存上限，不再在扫描过程中统一标成不可用。每轨记录 packet/display/error/cue 计数和真实 FFmpeg 返回值。
- 保留 64 MiB 解码字幕缓存、120000 条 cue 与 16 MiB 文本文件限制；VobSub 二进制文件不套用文本限制，其 IDX 索引仍受限制。没有增加 GPU 像素回读或逐帧 fence wait。
- QML 打包接点校验 zlib-enabled FFmpeg 及实际 DLL SHA，拒绝误用旧缓存；提供显式 `--ffmpeg-root`。同步保留 zlib 许可证、SPDX 和对应源码。

## 验证

最终应用 EXE SHA256：`3be70422720f22272ff39eac6b79ddd837892b5923972ea0b4442ebe30d6f99b`。构建使用当前 main 必要覆盖对象，逐源/对象 SHA 核验，保留之前 HDR 开关与 BMD 改动；没有直接拿旧静态库冒充当前 main。

| 验证 | 结果与证据 |
| --- | --- |
| FFmpeg 构建 | 270.48 秒、exit 0；原 patched 源树、输入库和原 DLL SHA 保持。`logs/.../ffmpeg-build-20261009-131500/receipt.json` |
| 最终应用及 5 个测试程序构建 | 全部 exit 0，原输入不变。`logs/.../build-20261009-133206/receipt.json` |
| 原生字幕回归 | 格式 322、实际字幕层像素 28、原文本 5、PGS 21、DVD 16、DVB 21，共 **413 项断言通过**；异步加载另行通过。`logs/.../tests-20261009-133424/receipt.json` |
| 多轨压缩 PGS | 独立构造的 5 条压缩轨与未压缩轨逐像素相等、共 10 条 cue；首条可用及时发布，加载提示正确清除 |
| 位图显示 | 真实解码的 SUP 进入产品 SubtitleOverlay；验证作者坐标/letterbox、半透明与透明间隙、调色板更新、清屏、回退 seek、窗口尺寸变化。不是仅统计 cue 数量 |
| 实际 QML / bridge | 最终 EXE 的 embedded / external / sidecar / english 四组均 exit 0、无错误：`gui-embedded-20261009-133438`、`gui-external-20261009-133515`、`gui-sidecar-20261009-133543`、`gui-english-20261009-133622`。同名前置 SUP、两条 VobSub 语言、主副选择与各自延时、Unicode 路径、坏文件保留、换源取消、菜单可选及英文错误说明通过 |
| 常规视频播放短测 | GTA6 原片 H.264 4K D3D12 硬解提交 350 原帧；HEVC 4K D3D11 硬解 180/180 帧播放至 EOS。两组 exit 0、无 ERROR。不是性能或全片长稳验收 |
| 发布者打包检查 | 原无 zlib prefix 被拒，新 prefix 的 6 DLL 身份通过。`reports/.../package-ffmpeg-audit.json` |

上述 `logs/...` / `reports/...` 均以 `E:/项目/Veyra/` 为根、任务目录为 `subtitle-formats-20261009`。QML 抓图仅检视菜单与界面，不包含原生视频子窗口像素；字幕层像素结论来自原生回归。

### 回归中发现的已有问题

AV1 4K 测试片 `tests/stream-fix-20261001/av1_4k_150m.mkv` 回退 libdav1d 软件解码后，12 秒观察仅提交 4 个原帧。候选 EXE+新 FFmpeg、候选 EXE+旧 FFmpeg、原 EXE+旧 FFmpeg 三组均复现，均无 ERROR，不能将这一项记作通过。日志显示 software fallback 后 `prepareMs` 约 11.9 秒；这不是已确定的根因。三组证据分别为 `video-20261009-133700`、`video-20261009-133833`、`video-20261009-133918`。本轮不修改 AV1 播放/调度链路；它不是本次 FFmpeg 重编译特有的回归。

### 失败和修正

首次 FFmpeg configure 因 MinGW pkg-config 损坏中文路径失败；改用 `E:/veyra-subtitle-20261009` junction（实际仍在 E:/项目/Veyra）后成功。首次格式测试暴露短 MPL2 被误探为 LRC，按明确扩展名选择已有 demuxer 后通过；loading fixture 实为两个内嵌加一个同名外挂，期望值纠正为 3。原失败回执保留，没有覆盖为通过。

## 当前交付与边界

用户在收尾明确要求“**不用打测试包**”。本轮交付代码修复和排查结果，不继续打包、不交付测试包。产品代码已存档于 `a29243c1753ca75a63ad1d9e7051ff35178f9a11`；其后只更新这项交付决定的文档，不改变已验证的代码或 EXE。

收到此指令前，覆盖包已经生成于 `E:/项目/Veyra/test-packages/subtitle-formats-20261009/Veyra-2.0.6-subtitle-fix-overlay.zip`，只保留为内部中间证据，**不是本轮交付物**。对应 Veyra 与 FFmpeg/dav1d/zlib 源码、许可证和逐文件 manifest 已核对。以后整合正式版本时，必须一并采用 zlib-enabled FFmpeg，单换 EXE 不会修复压缩 PGS；当前旧包和用户配置均未覆盖。

仍未验证受影响用户实际原片、整部影片大量 PGS、AMD/Intel 实机与 HDR 字幕显示；本机是 RTX5070。缓存上限未提高，达到上限会明确提示后续内容未加载。TTML decoder 不在当前 FFmpeg 中，不宣称支持；STL 指 Spruce 文本格式，不是 EBU 二进制 STL；TXT 只识别带支持格式时间码的内容，不自动生成字幕。没有更改字幕渲染顺序、NR、补帧或导出实现。

本轮仅本地隔离分支存档，未合并 main、未 push、未创建 Release、未更改正式版本号。开工的 39 既有工作树/495 脏文件与 main 固定快照由 `subtitle-formats-control.py` 逐项核对；最终验收、未交付包的状态和保全以 `reports/subtitle-formats-20261009/final.json` 为准。
