# Xbox 串流（非官方）执行记录

分支 `codex/xbox-20261001`（从 `codex/moonlight-20261001` 开出，beta 包同时带 PC 串流和 Xbox）。用户 2026-10-01 决定：用 Greenlight 的方案，用 Xbox/微软账号直接登录，接受账号风险。

**做法**：照 Greenlight（MIT）的协议，原生 C++ 实现，不嵌浏览器。
- 登录：微软设备码（屏幕显示一个码，用户在 microsoft.com/link 用自己的账号登录；软件不碰密码），借用 xbox.com 网页版的客户端身份 `1f907974-…`，与 Greenlight 现行做法相同。之后是 Xbox Live 用户令牌 → XSTS（gssv）→ xhome 串流令牌；只保存刷新令牌，DPAPI 加密于 `%LOCALAPPDATA%/Veyra/xbox`。
- 会话：`/v6/servers/home` 列主机，`/v5/sessions/home/play` 建会话，轮询状态，`ReadyToConnect` 时用 MSAL 转移令牌 `/connect`，再交换 SDP 与 ICE（信令走微软服务器），之后每 30 秒保活。
- 媒体：libdatachannel（MPL-2.0）收 H.264 与 Opus；四条数据通道 chat / control / input / message；握手、授权、手柄二进制包、震动回报、关键帧请求照 Greenlight 的播放器。解码与之后的处理链与 Moonlight、PS5 相同。
- **非官方**：微软可以随时改协议或封掉这个客户端身份。

## 阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| X1 | 依赖（libdatachannel 等经 vcpkg）、登录、会话 API、WebRTC 会话与协议 | 完成，离线 72 项通过 |
| X2 | 引擎来源、解码、音频、手柄与震动 | 未开始 |
| X3 | 前端：首页卡片、Xbox 对话框（登录码、主机列表）、统计 | 未开始 |
| X4 | 用户本人账号与主机实测；beta | 需要用户 |

## X1 记录

- 依赖：复制 `remoteplay-installed` 为 `C:/veyra-deps/xbox-installed`，只往副本里装 libdatachannel[srtp]（同一份 OpenSSL 3.6.4），PS5/Moonlight 用的原树不动。版本与命令见 `scripts/xbox/dependency-lock.json`。
- **发现并修复的环境问题**：本机 IPv6 回环（`::1`）的 UDP 不通（有 198.18.0.1 的 TUN 代理网卡）。libjuice 用 "localhost" 绑定唤醒线程的 socket，解析成 `::1`，唤醒包无声丢失，ICE 检查要等 60 秒超时才开始。用 overlay port 打一行补丁改绑 127.0.0.1（`scripts/xbox/vcpkg-overlay/libjuice/ipv4-interrupt.diff`）。开代理/TUN 的用户机器上同样会有这个问题，所以这是产品修复，不只是测试修复。
- HTTPS 用 Windows 自带的 WinHTTP（系统证书与代理设置），不用 OpenSSL 自管 CA。
- 离线验证（`veyra_xbox_tests`，72 项，连跑 3 次稳定）：
  - 协议：输入报告的二进制布局（含参考客户端把"虚拟物理性"写成大端的怪处）、SDL 手柄到 Xbox 帧的映射、震动与服务器画面尺寸的解析。
  - 会话 API：用脚本化的假服务回复，验证请求路径/头/体与解析（204 重试、错误详情、Teredo 地址还原 IPv4，按 RFC 4380 的例子）。
  - 登录链：设备码、等待/降速、成功保存（文件里没有明文刷新令牌）、重启后保持、串流令牌与默认区域、转移令牌、过期自动刷新并保存轮换后的刷新令牌、刷新被拒则退出登录、XErr 的中文说明。
  - WebRTC：第二个本地 libdatachannel 节点扮演主机，走真实的 ICE/DTLS/SCTP/RTP：四条通道的名字与协议、握手与授权、客户端元数据与手柄报告逐字节、分片的 IDR 访问单元逐字节还原为 Annex-B、Opus 原样、震动与画面尺寸回调、关键帧请求。
- **未验证**：任何与微软服务器或真实 Xbox 的交互。假服务和本地"主机"都是按 Greenlight 的协议理解写的，只证明客户端自洽、本地 WebRTC 链路可用。
- 调试开关：环境变量 `VEYRA_XBOX_RTC_DEBUG=1` 打开 libdatachannel 的详细日志与候选地址日志（现场排查用）。
