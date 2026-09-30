# Moonlight（PC 串流）接入执行记录

方案：`docs/STREAMING_PLAN_MOONLIGHT_XBOX_2026-10-01.md`。分支 `codex/moonlight-20261001`（从 `main` `09392c4` 开出）。范围与授权见 `AGENTS.md` 2026-10-01 条。

**边界（用户 2026-10-01 明确）**：接入的只是串流核心（协议、解码、音频、输入、主机与游戏选择界面）。NR / SR / 补帧 / 调色沿用现有同一条处理链，开关与默认值（补帧本来默认关）不为串流另设。Moonlight 协议本身支持 4K60，实际上限看主机编码器、网络与本机解码。

**验证边界**：我不安装、不连接 Sunshine 或任何主机。下面的"通过"只指离线测试（模拟主机、固件、已知答案测试）；与真实 Sunshine 的互通、4K60、延迟、运动画面、手柄键鼠都要由有双机的测试者验证，未验前不得称通过。

## 阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| S0 | 依赖固定（`scripts/moonlight/dependency-lock.json` + 校验脚本）、CMake 开关、许可记录 | 完成 |
| S1 | 协议层（Qt 无关）：XML、加密、HTTP(S)、主机请求、配对、身份存储 | 完成，离线测试 69 项通过 |
| S2 | 会话来源：`LiStartConnection`、视频解码、音频、遥测、引擎接入 | 代码完成（S2a），只做了编译与离线检查，未连过任何主机 |
| S3 | 输入：手柄、键鼠捕获、保留键 | 代码完成，转换逻辑有离线测试（130 项），未连过主机；接入界面在 S4 |
| S4 | 前端：首页卡片、PC 串流对话框、配对、设置、串流中统计 | 完成（对话框形式），用假主机端到端验证过，未连过真主机 |
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

## S2a 记录（会话来源与引擎接入）

- `MoonlightSessionSource`（`IFrameSource`）：`connect()` 先 launch/resume，再 `LiStartConnection`，失败由库自行清理；拉取式渲染（`LiWaitForNextVideoFrame` / `LiCompleteVideoFrame`）由一个解码线程处理，复用采集卡的 `CaptureCompressedDecoder`（D3D12VA 硬解，失败回落软解 NV12）；丢帧放在解码之后，被跳过的帧记到下一帧的 Drop 标志，与 PS5 来源同一约定。
- Opus 多声道经 `CaptureAudioSession`，增益与音画同步沿用采集卡的设置。
- 引擎：`openMoonlight()`、`snapshot()` 带 `moonlightActive` 与 `MoonlightStats`；`isStream`（采集卡、PS5、Moonlight 共用的"实时来源"判断）替换了原来散落的 `isCapture || isRemote` 分支，PS5 与采集路径行为不变。
- 码率与编码选择（`StreamConfig.h`，纯函数，有测试）：默认码率表取自 moonlight-qt，1080p60=20 Mbps、4K60=80 Mbps；HDR 只提供 10 位格式；AV1 只在已知有硬解时自动提供。
- 构建陷阱（已处理）：① Moonlight 目标必须放在 `CMAKE_MSVC_RUNTIME_LIBRARY`（静态 CRT）设置之后，否则与 Qt 目标链接时 `msvcprt`/`libcpmt` 重复定义；② 带 Moonlight 的 `veyra_qml_ui` 链接行超过响应文件阈值，`link.exe` 按 ANSI 读响应文件，`E:\项目` 下的 Qt 库路径打不开。本机构建用 `C:eyra-deps\qt-veyra`（指向 Qt 的目录联接）作前缀；这是本机环境，不进仓库。
- 验证：`veyra_moonlight_protocol_tests` 87 项通过；整个 `veyra_qml_ui` 链接成功；`veyra_qml_data_tests`、`veyra_qml_easing_tests`、`veyra_ui_contract_tests`、`veyra_effect_chain_tests`、`veyra_repair_contract_tests`、`veyra_preset_library_tests` 通过。
- **未验证**：任何真实串流（连接、解码、音频、HDR、丢包恢复）；解码流水线还没有离线单测；`connect()` 会阻塞引擎线程直到启动完成，首帧最多等 20 秒；软解回落只出 8 位，HDR 需要硬解；手柄震动与输入是空实现（S3）；断线重连未做（S5）。

## S3 记录（输入）

- 手柄：沿用 PS5 用的 SDL 手柄读取（`ControllerInput`），`moonlight::mapPad` 转成 GameStream 帧：按位置映射（南键=A），摇杆 Y 取反（SDL 向下为正，主机向上为正；-32768 不溢出），扳机 0–255；状态变化才发送，失焦或设备丢失发全零以释放。只有 1 号手柄；首次发送前发一次到达事件（Xbox 类型，模拟量扳机 + 震动），旧主机回"不支持"也能工作。
- 震动：主机的 rumble 回调存下来，`takeFeedback()` 交给 SDL；因为主机只在变化时通知、SDL 的震动有时长，非零值每 2 秒重发一次。
- 键鼠：`moonlight::InputRouter` 把窗口消息转成主机事件（Win32 虚拟键码加 0x8000；Shift/Ctrl/Alt 按扫描码与扩展位分左右；关闭 Num Lock 的小键盘导航键按小键盘键发送；字符消息与 Alt 菜单被吞掉；长按重复由主机自己处理）。`MoonlightInputCapture`（Qt）用原生事件过滤器在 Qt 之前接管，鼠标用原始相对位移，光标隐藏并限制在窗口内，失焦自动释放并让主机松开所有键、鼠标键与手柄。
- 保留键：Ctrl+Alt+Shift+Z 释放捕获，+Q 结束串流，+S 切换统计；这些键不会发给主机。
- 未捕获：Windows 键、Alt+Tab、Ctrl+Alt+Del 仍归系统（Moonlight 客户端用低级钩子，这里先不用）。
- 不含：陀螺仪与触摸板、电池状态、扳机震动、手柄退出组合键、多手柄。
- 发送都在 `libMutex` 下检查 `streaming`，和 `LiStopConnection` 不会重叠。

## S4 记录（前端）

- 入口：首页改为 5 张来源卡（窄窗口自动换行），新增"PC 串流"卡；专业页/节点页的片源菜单加"PC 串流…"；"继续上次"支持 PC 串流。
- **形式与方案稿不同**：做成和 PS5、采集卡一样的对话框（`DialogHost.qml` 的 `moonlight`，宽 760），而不是新页面；内容一样：主机列表（自动发现 + 手填 IP、状态点、配对、删除）→ PIN 框 → 游戏网格（运行中标记、退出游戏）→ 串流核心设置（分辨率、帧率、码率、编码、HDR、声道、手柄、自动捕获键鼠、让主机切换分辨率、主机同时出声，按主机保存）。没有做游戏封面图（文字卡片）。
- `MoonlightModel`（Qt，独立小库）：全部网络操作在工作线程，界面不阻塞；主机与设置存 `%LOCALAPPDATA%/Veyra/moonlight/hosts.json`（只含公开证书；私钥在 DPAPI 保护的 `client.identity`）。局域网发现用 Windows 自带 DNS-SD（`_nvstream._tcp`）。主机上有别的游戏在跑时不自动结束它，要用户点"退出游戏"。
- 串流中：Ctrl+Alt+Shift+S 显示统计浮层（分辨率/编码/硬解、主机耗时、往返、接收、排队、解码、FEC 与丢帧、捕获状态）；点画面重新捕获键鼠；断开不结束主机上的游戏。
- 验证（离线，假主机）：`veyra_moonlight_model_tests` 42 项（添加主机、配对、游戏列表、设置校验与持久化、另一个游戏在跑时拒绝、HDR 与 H.264 冲突拒绝、恢复运行中的游戏、重启后保留、主机遗忘配对、删除）；测试发现并修掉两个真缺陷（JSON 迭代器用了两个临时对象、启动后读了已被移走的标签）。`scripts/moonlight/ui-demo.py` 用假主机进程驱动真实界面：添加主机、配对（界面显示的 PIN 交给假主机）、游戏列表、改设置、发起串流，最后 RTSP 被拒，界面给出可操作的失败原因；应用自报 `MLTEST_PASS`，逐步截图在 `tests/moonlight-20261001/ui-demo-r5`。
- **未验证**：真实 Sunshine 互通、任何真实画面/音频/HDR/4K60、手柄键鼠实际效果、局域网自动发现（只验证了启动停止不崩溃）、`veyra_qml_quick_tests`（未暂存运行）。

## Beta 包

- 2026-10-01 构建本机测试包 `E:/项目/Veyra/test-packages/2.0.0beta2-moonlight-20261001/`（目录与 zip，约 408 MB；`scripts/moonlight/make-beta.py` 从 2026-09-30 候选的运行库布局加新播放器生成；说明见 `docs/TEST_PACKAGE_2.0.0beta2_moonlight.md`）。包内播放器 SHA256 `03C12063F51505C3AD29F406442B0F0A96FF5A50408D51DC300130D79098AC32`。只做了启动冒烟，未连真实主机；未推送、未打标签、未合并到 main。

## 下一步

S3 输入（手柄、键鼠、保留键）与 S4 前端（桥接接口、主机与应用列表、配对对话框、串流页与设置）。
