# Elgato HDR 红色溢出与 RGB 黑屏：对齐 Nitlink / OBS（2026-10-11）

## 授权与范围

- 用户：1.x 时为 Elgato HDR 红色溢出接入过 Nitlink（当时修好了），现在又出现同样问题，怀疑 2.0 把它弄没了；另外 Elgato 采 RGB 格式黑屏；这些问题在 Nitlink 和 OBS 里都不存在。
- 沿用规则：先存档、开分支，负优化退回；不 merge/push/Release。只改 Elgato 厂商控制、P010 色度解码开关与对应测试；其它采集卡、默认设置、补帧/NR、运行库、驱动、用户配置不变。

## 存档与分支

- 与同日 logs16 任务共用存档：`checkpoint/pre-claude-logs16-20261011`（main 5ba186a）、`archives/claude-logs16-20261011/`。
- 分支 `claude/elgato-hdr-20261011`，工作树 `worktrees/elgato-hdr-20261011`；构建 `py -3.11 -I tmp/claude-logs16-20261011/build.py elgato`。

## 核查结论：2.0 没有删掉 1.x 的接入，但它只覆盖了一种情况

- 1.x 的提交 be3e2e6（`ElgatoHdrControl`）完整保留在 2.0 / 当前 main 里，全仓库也只有这一个 Elgato 提交。
- 它只在“设备名精确等于 4K60 Pro MK.2”且“格式是 P010”时生效：读取 HDR InfoFrame，按需关闭/打开卡内色调映射（厂商属性 722）。
- 与参考实现对比（本地固定源码：Nitlink 4f8a723、OBS libdshowcapture c13d4b7、Elgato 官方 capture-device-support fe96309）：
  1. **其它型号**：Nitlink 对所有 Elgato 卡都尝试关闭卡内色调映射；OBS 每次连接都对任何支持该属性的设备设置。Veyra 只管 MK.2。
  2. **8 位格式（NV12/YUY2/RGB）**：OBS 在非 P010 格式时**打开**卡内色调映射，让 HDR 信号由卡转成正常 SDR。Veyra 从不处理 8 位格式，卡上的状态取决于上一个软件留下的设置（例如 Nitlink 启动时会把它关掉且不恢复）。
  3. **色度范围**：Nitlink 实测（4K Pro 对照电视上的 PS5 原生画面）：MK.2 以外的 Elgato 卡输出的 HDR P010 是“亮度有限范围、色度全范围”，按有限范围解色度会过饱和（橙色皮肤）。MK.2 例外，是标准有限范围色度。Veyra 一直按标准有限范围解所有卡。

## 修改

- `isElgatoDevice()`：名字含 “Elgato” 或属于 “Game Capture …” 系列（Nitlink 的规则加厂商自己的滤镜名）；通用名字不进入厂商路径。
- 卡内色调映射（属性 722），在 Run 之前、每次原生格式连接时执行：
  - P010：保持原来的 HDR 配对逻辑；仅 MK.2 失败时拒绝连接（原合同），其它型号不支持该属性（如 4K X/4K S 返回 E_PROP_SET_UNSUPPORTED）时保持卡的状态继续连接。
  - 8 位格式：请求打开卡内色调映射（OBS 行为）；已是打开则不写；关闭会话时恢复可读的原值。
- 色度：新增 `ColorDescription::fullRangeChroma`。MK.2 以外的 Elgato 卡在 P010 且范围为“自动”时置位；`YuvToLinearRgb.hlsl` 的范围参数 2 = 亮度有限、色度全范围。用户手动选的范围优先。

## 测试

- `veyra_capture_color_tests`：270/270 通过，其中 Elgato 合同 13 项新增/更新（身份识别；8 位打开/恢复/已打开不写；无厂商属性的卡仍能连接；非 MK.2 的 P010 色度；手动有限范围；驱动声明全范围；其它设备零厂商调用）。原有 MK.2 合同（InfoFrame 校验、严格失败、回读矛盾拒绝、手动 Rec.709、只写属性）全部保留通过。
- `veyra_hdr_color_tests` 新增 GPU 等价测试：同一 P010 像素（约 800 cd/m² 的红），色度全范围解码 = 把码值按 896/1023 换算后的标准解码（误差 1.7%），而旧的有限范围解码红色高 27%（scRGB R 13.13 对 10.34）。全部通过。
- 回归：hdr、capture-color、chain、contracts、timing、lifecycle、video 通过（`logs/claude-logs16-20261011/elgato/`）。`veyra_capture_tests` 需要实卡参数，未运行。

## 未验证与需要的信息

- 本机没有 Elgato 卡，以上全是软件/模拟/GPU 计算验证，真实颜色和 RGB 黑屏都需要反馈者确认。
- RGB 黑屏的原因目前只是推断：最可能是 HDR 信号 + 卡内色调映射被关掉（例如 Nitlink 关掉后没恢复）时，8 位/RGB 格式拿不到正常画面；本次按 OBS 行为在 8 位格式时打开它。如果不是这个原因，需要反馈者的日志（`capture-elgato`、`capture-color`、`capture-callback`、`capture-source-pixels` 行）。
- 需要反馈者提供：卡的具体型号，复现时的 `veyra-qml.log`，所选格式（P010 还是 RGB/YUY2/NV12），信号源是否 HDR。
- USB 款（HD60 X / HD60 S+）在 OBS 里走 Elgato 官方 HID 协议控制色调映射，本次未移植；4K X / 4K S 在 Nitlink 里也没有色调映射写入，只做来源识别。
