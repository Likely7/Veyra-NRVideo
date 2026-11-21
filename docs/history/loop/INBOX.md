# Veyra Human Inbox

## RESOLVED · 2026-09-08 用户明确确认两处 hash 同步

preflight exit1，唯一失败 README hash。当前 README 匹配用户授权的存档提交 f79ef95；其他控制与二进制身份通过。拟仅将 CONTROL_HASHES.json 的 README 项由 781FAFD857A82A19FCC09FDEDB2E00EE2465099E32B5A0857127D648316336E0 同步到 F226D0A74E56E79D16F4FBE34130B0B27DAFACE0631388D0BE09C8ED46753A33，并将 scripts/loop-gate.ps1 固定 manifest hash 由 26559334D3A520682A42B20FD8127F3F297E70686CF139DC9325087047FDC8FC 同步到 4D5A30F73E06B59B3D6AD5D4A0AE106A0C3AD808D758418B21CC339D2E28779B。

不改 README 内容、其他控制 hash、逻辑、阈值或 runtime。用户回复“确认，给你全部权限”后，上述两处同步已执行；preflight 71/71 通过。此前 proposal 与失败日志保留在 logs/optimization-goal-20260908/，当前无需再次授权。

## 当前交付后唯一用户验收项（2026-09-07）

本机软件已交付，独立复核PASS见docs/REVIEW_F6.md。请接入采集卡，按docs/USER_GUIDE.md测试视频设备/格式/HDMI音频、NR/FG、停止重开、断连和体感延迟。未做实卡测试，不声明硬件通过。NVOF/NVENC本机依赖已能构建运行，不再因为历史SDK阻塞停工。公开分发依然未授权，不能上传/打包运行时。以下旧阻塞为历史，不应自动重启长测或改驱动。

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

这里仅记录 Agent 无权自行完成的许可、凭据、外部硬件或发布决定。普通 bug 不放这里。缺失项不阻止当前 Phase 中与它独立的工作，但相关 gate 不得伪造通过。

## RESOLVED · NVIDIA Optical Flow SDK 5.0.7 已存在

- 实际目录：`third_party_local/nvidia/Optical_Flow_SDK_5.0.7/`，含 `NvOFInterface`、`Common/NvOFBase`、D3D12 sample、EULA 与 release notes。
- 当前代码已经从该包构建，并在系统 `C:\Windows\System32\nvofapi64.dll` 上取得真实 NVOF capability/init/register/execute/cost 和合成位移证据。
- 该目录仍必须 gitignore，不得提交或打包。新 Agent 不得继续把 NVOF SDK 写成缺失，也不得改回旧的 `Optical_Flow_SDK_5.0/` 路径。

## ACTIVE · 真实采集卡与测试信号

- 需要原因：Phase 7 的“采集卡模式”不能由窗口捕获、虚拟摄像头或 mock 冒充。
- 硬件条件：Windows 能通过 DirectShow/UVC 枚举、并真实提供 3840×2160 60 fps SDR 的采集设备；另一台主机或 HDMI 测试源能稳定输出 4K60 SDR 与 HDMI 音频；最好带 HDMI passthrough，方便分别测试玩家直通与 Veyra 缓冲预览。
- 用户动作：在 P7.2 前连接并保持设备可用。厂商只提供私有 SDK、未暴露 DirectShow 的卡不属于首发兼容范围。

## ACTIVE · NVIDIA Video Codec SDK 13.1 package missing（用户已同意许可方向）

- 需要原因：首发 4K H.264/HEVC 导出必须使用 D3D12 NVENC，不能继续用 raw-frame readback/pipe。
- 当前事实：系统驱动通常提供 `nvEncodeAPI64.dll`，但项目尚无可合法编译的 Video Codec SDK 13.1 header/sample。
- 用户决定（2026-09-03）：用户明确表示接受 NVIDIA EULA，并授权 Veyra 在本机研发中使用该官方 SDK。不要再次询问用户是否愿意接受。
- 尚未完成：仍需用户本人在 NVIDIA Developer Portal 完成接受/下载，或把已经从官方取得的压缩包解压到下列目录；目录出现并经核验前，P7.5 不能标完成。
- 用户动作：从 NVIDIA 官方 Video Codec SDK 页面接受条款并解压到：

  `third_party_local/nvidia/Video_Codec_SDK_13.1.0/`

- Agent 必须核对 `Interface/nvEncodeAPI.h`、D3D12 sample、版本、hash 和 EULA；不得从竞品仓库复制 header。

## PENDING · 深度模型与 Microsoft ML 包许可记录

- Agent 可从官方来源获取固定版本到 gitignored `third_party_local`，但必须先保存 SHA256 与许可证。
- 只允许商业条件清晰的 Depth Anything V2 Small / Video Depth Anything Small；不得为了更高画质混入 CC-BY-NC 的 Base/Large 权重。
- 如果官方权重条款或来源无法确认，Depth UI 保持 unavailable，P5 quality gate 仍要求明确 Auto fallback；不得下载来路不明的模型。

## PENDING · 公开分发决定

- 当前只授权本机研发与本地 checkpoint。
- 制作安装包、上传 release、分发 `nvngx_dlssnr.dll`、`nvngx_dlssg.dll`、NVOF SDK、模型或 FFmpeg binary 前，必须单独完成许可证审查并得到用户确认。
- 2026-09-06 用户决定继续用当前实验 Feature 18 施工；这不是分发授权。即使 NVIDIA 已正式发布 DLSS 5，只要 Veyra 未获得公开 SDK/runtime 分发许可，功能完成状态仍必须是 `distribution_blocked`。
- 如未来采用“用户自行提供 runtime”，必须让应用按精确 hash/签名检查并明确显示实验性质；这种设计只避免随安装包携带文件，不自动解决使用授权或平台风险。

## RESOLVED HISTORY

- 2026-09-06 NVOF SDK：实际已存在的目录是 `third_party_local/nvidia/Optical_Flow_SDK_5.0.7/`；旧 ACTIVE 缺失记录作废。
- 2026-09-03 DLSS SR：`SuperSampling.Available=0` 的根因是 NGX DLL 搜索路径；改为规范绝对路径后 `Available=1`，1080p→4K Create + 30 Evaluate + resize 通过。不是靠更新驱动解决。
- 2026-09-02 D3D12 debug layer：用户已安装 Graphics Tools。
