# Moonlight（PC 串流）接入执行记录

方案：`docs/STREAMING_PLAN_MOONLIGHT_XBOX_2026-10-01.md`。分支 `codex/moonlight-20261001`（从 `main` `09392c4` 开出）。范围与授权见 `AGENTS.md` 2026-10-01 条。

**边界（用户 2026-10-01 明确）**：接入的只是串流核心（协议、解码、音频、输入、主机与游戏选择界面）。NR / SR / 补帧 / 调色沿用现有同一条处理链，开关与默认值（补帧本来默认关）不为串流另设。Moonlight 协议本身支持 4K60，实际上限看主机编码器、网络与本机解码。

**验证边界**：我不安装、不连接 Sunshine 或任何主机。下面的"通过"只指离线测试（模拟主机、固件、已知答案测试）；与真实 Sunshine 的互通、4K60、延迟、运动画面、手柄键鼠都要由有双机的测试者验证，未验前不得称通过。

## 阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| S0 | 依赖固定（`scripts/moonlight/dependency-lock.json` + 校验脚本）、CMake 开关、许可记录 | 完成 |
| S1 | 协议层（Qt 无关）：XML、加密、HTTP(S)、主机请求、配对、身份存储 | 完成，离线测试 69 项通过 |
| S2 | 会话来源：`LiStartConnection`、视频解码、音频、遥测、引擎接入 | 未开始 |
| S3 | 输入：手柄、键鼠捕获、保留键 | 未开始 |
| S4 | 前端：首页卡片、PC 串流页、配对对话框、设置、串流中 UI | 未开始 |
| S5 | 质量：HDR、AV1、丢包与重连、稳定性 | 未开始 |

## S0/S1 记录

**依赖**（均按 GPL-3.0 移植/使用，记录在 `THIRD_PARTY_NOTICES.md`）
- moonlight-common-c `f900dd47…`，含 enet `aca87840…`、nanors `b1e3c22c…`；构建前由 `verify-moonlight-stage.py` 校验，提交号或未记录的改动都会拒绝构建。
- moonlight-qt `8369d1a0…`：仅作移植参考。其自带的 common-c 子模块指针 `478e79b9…` 在上游已取不到（`not our ref`），但 moonlight-qt 代码已使用我们固定版本的新接口（`MODIFIER_EXTENDED`、`LI_CTYPE_STEAM`、`LiSendControllerTouchEvent2`），二者配套。
- 构建依赖来自已有的 vcpkg 静态树：OpenSSL 3.6.4、Opus；没有新增第三方二进制。

**设计决定**
1. 协议层不用 Qt、不用 curl：现有 curl 是 Chiaki 自带的（系统 TLS，不支持客户端证书）。直接用 OpenSSL 写很小的 HTTP(S) 客户端（Winsock 非阻塞 + 截止时间 + 取消标志），可离线单测，引擎库保持 Qt 无关。
2. 证书固定：握手后、发出任何请求前，比较服务器证书与配对时保存的证书，不符即拒绝（`TransportError::Kind::Tls`）；没有固定证书时拒绝发 HTTPS。
3. `serverinfo` 逻辑照搬 moonlight-qt：有固定证书先走 HTTPS，遇 401 或 TLS 失败回落 HTTP；未配对只走 HTTP 并取 HTTPS 端口。
4. 配对五步按 moonlight-qt 的 `NvPairingManager` 移植（盐 + PIN → AES-128 密钥，代数 7 起 SHA-256，之前 SHA-1）；失败路径都会通知主机 `unpair`；取消不会留下半完成的配对。
5. 客户端身份（RSA-2048 自签证书 + 私钥 + 唯一 id）用 DPAPI 加密存于 `%LOCALAPPDATA%\Veyra\moonlight\`，原子写入；私钥不进日志。
6. 设备名用 `Veyra`（moonlight-qt 用历史遗留的 `roth`），这样 Sunshine 的已配对客户端列表里能认出。**这是与上游不同之处，真机互通时要留意**；若有主机拒绝，改回常量 `kDeviceName`。
7. 解析器是只读小型 XML 读取器，限制 1 MB 与 32 层嵌套，只支持五个预定义实体和数字字符引用。

**验证（离线）**
- `veyra_moonlight_protocol_tests`：69 项通过，连续运行 10 次无失败。覆盖：XML 解析与畸形输入、实体与空标题、状态码（含 GFE 特例）；AES-128 已知答案（FIPS-197）、SHA-256/SHA-1 已知答案、签名校验；身份创建与 DPAPI 存取（文件内无明文私钥、损坏文件被拒）；一个模拟主机（实现主机一侧的五步配对、TLS 双向证书、三种 HTTP 分帧）：配对成功、错 PIN、主机忙、等待 PIN 时取消、主机遗忘配对后的回落、服务器证书与固定证书不符被拒、启动请求参数（mode、rikey、rikeyid 的大端 int32、HDR、GFE 的 fps 特例）、盒装图、连接被拒。
- 变异验证：故意改坏配对第 4 步的签名，测试立即报 4 项失败；恢复后全过。
- 构建：`veyra_moonlight_protocol`（`/W4 /WX`）与 `veyra_moonlight_common_c`（含 ENet、nanors）均编译通过。
- **模拟主机与客户端出自同一份协议理解**，所以它证明客户端自洽且对坏数据安全，不证明与真实 Sunshine 互通。

**发现的测试自身问题**：Windows 上回环口被拒绝的连接有时要约 2 秒才报错，可能与 2 秒超时撞车而报成 Timeout；相应测试已同时接受 Connect 与 Timeout。

## 下一步

S2：`MoonlightSessionSource`（拉取模式 `LiWaitForNextVideoFrame`、每帧立即解码、丢帧放在解码后且不标不连续）、Opus 多声道、遥测与延迟分解；先做离线可测的部分（解码单元拼接、HDR 元数据、时间戳换算）。
