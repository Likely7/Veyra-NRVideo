# Remote Play 代码交付 01 — 实施记录

> 历史阶段记录：本文保留 Code 01 交付当时的状态，其中“原生编译未执行”和 `BLOCKED_NATIVE_BUILD` 已过期。Windows x64/MSVC 原生 Chiaki probe 后续已完成 248/248 步并真实初始化成功。当前事实、必修缺陷和下一步只以 [`../REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md`](../history/reports/REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md) 为准。

> 二次审计已写入主交接第18节：普通 callback 替换了本包的 metadata 扩展；H.264 首帧、PCM 和 PTS 在实际 source 中失败，67项原包核心测试不覆盖它们。当前不是继续重复 native 初始化，而是先修数据适配层。

2026-09-11，基线：Veyra `ff4122befc5909682d7a4df29da086d5f5bb619d`；Chiaki `0e16950165f06e5c3291537c2eeba6e852be7120`。

## 已实现（代码）

`Types/Validation`、`AnnexB`、`Timeline`、`VideoIngress/AudioIngress`、`LatestMailbox`、`PacketPump`、`InputGate`、`SessionInbox` 已有真实实现和离线测试。

`ChiakiBackend/ChiakiRegistration` 使用已核对的真实 API，元数据扩展保存在可审查 JSON 中；这部分仅完成源码，原生编译和真机调用未执行。

## 对总方案的对应关系

| 总方案职责 | 本包实现位置 | 状态 |
|---|---|---|
| 配置、Account-ID、配对码严格校验 | `Types.h` / `Validation.cpp` | 离线测试通过 |
| 回调元数据扩展 | `chiaki_metadata_patch.json` / `prepare_chiaki.py` | 片段变换测试通过；完整上游应用未在本环境执行 |
| 有界视频输入与恢复 | `VideoIngress` | 离线测试通过 |
| 音频缓冲与每声道采样计数 | `AudioIngress` | 离线测试通过；WASAPI 未接 |
| 时基、帧编号展开 | `Timeline` | 离线测试通过；本机估计不是远端 PTS |
| AVFrame 最新帧邮箱 | `LatestMailbox<T>` | 所有权逻辑测试通过；实际 T 需绑定 AVFrame 引用 |
| EAGAIN 包泵 | `PacketPump<Packet>` | 控制流测试通过；真实 FFmpeg 适配未接 |
| 会话回调代次 | `SessionInbox` | 离线测试通过；不冒充完整会话控制器 |
| Chiaki 核心会话／配对／音频／控制 | `ChiakiBackend.cpp` / `ChiakiRegistration.cpp` | 源码完成，BLOCKED_NATIVE_BUILD |
| 现有 IFrameSource 接入、引擎、界面 | 无生产挂接 | NOT_IMPLEMENTED_IN_CODE01 |
| PS5、GPU、OBS、低延迟 | 无硬件验证 | BLOCKED_HARDWARE |

测试证据随交付包 `TEST_REPORT.md` 与 `evidence/` 提供。没有改变生产主程序、已有采集卡、旧增强后端、发布版本或运行时。

## 下一条唯一任务

Windows 上配置真实依赖、应用固定上游元数据补丁，并构建／运行 native probe。若失败，保留真实错误定位编译依赖，不先写 UI 来掩盖 native 缺口。
