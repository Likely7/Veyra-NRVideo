# 源码依据与许可证记录

> 当前状态已由 [修复执行记录](../history/reports/REMOTEPLAY_REPAIR_EXECUTION_2026-09-11.md) 更新：metadata patch 已恢复、共享生产 CMake 和 UI 已接入，旧主体保留供追溯，不作为当前失败/成功结论。

> 本文原主体为用户 Code 01 包的历史来源记录。2026-09-11 二次审计确认：当前工作树未导入其 `prepare_chiaki.py` / metadata JSON / PatchToolsTests；实际 backend 改用普通视频 callback，metadata 丢失已列为必须修复的问题。当前 native build 使用五文件 MSVC patch，初始化已通过；当前 source adapter 则有真解码/PCM/PTS失败。状态和命令以 [主交接第18节](../history/reports/REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md#18-2026-09-11-二次代码审计用户要求交给其他-agent-修) 为准，不能照下方历史描述寻找不存在的元数据脚本。

## Veyra 审查基线

`https://github.com/Likely7/Veyra-NRVideo/tree/ff4122befc5909682d7a4df29da086d5f5bb619d`

本轮通过 GitHub 连接确认 HEAD，读取 `AGENTS.md`、相关输入／引擎／UI 文件，与前次审查方案对照。本地直接 clone 未成功，因此没有声称完整仓库在本容器构建通过。

## Chiaki 真实 API 来源

固定：`https://github.com/streetpea/chiaki-ng/tree/0e16950165f06e5c3291537c2eeba6e852be7120`

使用与核对的位置：

- `lib/include/chiaki/common.h`：`chiaki_lib_init`、target、codec。
- `lib/include/chiaki/session.h`：连接资料、事件、视频回调、stop/join、PIN、控制和 IDR。
- `lib/src/session.c`：结构归零、连接初始化、profile 与码率单位。
- `lib/include/chiaki/regist.h`、`lib/src/regist.c`：配对资料与 `fini` 内 join。
- `lib/include/chiaki/opusdecoder.h`、`lib/src/opusdecoder.c`：每声道 `samples_count`。
- `lib/include/chiaki/controller.h`：按钮枚举与 idle 初始化。
- `lib/include/chiaki/log.h`：回调签名；避免转发原始密钥日志。
- `lib/src/videoreceiver.c`：配置头及画面样本的不同发出点、frame_index 和 reference-recovered。
- 根及 `lib/CMakeLists.txt`、`third-party/CMakeLists.txt`：实际依赖与目标 `chiaki-lib`。
- `COPYING`、`LICENSES/AGPL-3.0-only-OpenSSL.txt`：上游许可与 OpenSSL 附加许可。

元数据补丁是可审查的改动数据，并非伪造上游 API 已经存在；`prepare_chiaki.py` 明确添加 `CHIAKI_VEYRA_VIDEO_METADATA_API`，adapter 没有它就编译失败。

## FFmpeg 包泵行为参考

官方 send/receive API 说明：
`https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html`

EAGAIN 不消费输入；必须先 receive，再重送原 packet。当前 PacketPump 的控制流已用脚本化 codec 验证；本包未包含 FFmpeg，也没有声称实际解码或硬解通过。

## 本包许可边界

新写源码：GPL-3.0-only。Chiaki 本体及修改后派生文件保留其 AGPL-3.0-only/OpenSSL 许可标识。本包没有专有 SDK、运行时、模型、第三方二进制或真实 PSN 凭据。源码许可记录不替代未来完整程序／运行时组合的分发审查。
