# 五项修复本地验收（2026-10-03）

分支 `codex/field-upgrade-20261003`，起点 main `66cd3e5`。先建立
`checkpoint/pre-field-upgrade-20261003`、Git bundle/桌面修改存档及施工方案，
再修改产品。未合并、推送或公开发布。

## 修复与证据

| 项目 | 实现与本机结果 | 尚未验证 |
|---|---|---|
| Xbox 无声音 | configure 48k stereo float 后未启动输出线程；改为 configure + start，记录 Opus 首块与启动结果。真实 WASAPI 链 250 个 20ms float 块通过。 | 测试 gain=0，不证明主机声音已听到；需真实 Xbox 有声验收。 |
| Xbox 断开 | 日志 16 持续约 17 分钟后收到 KickForServerShutdown。保留具体 reason，网络丢失/服务器关闭/过期最多 3 次重连，1/2/3 秒退避，可取消；30 秒连续解码才恢复额度，重连原子 reset 图历史。 | 本地 ICE/DTLS/SCTP/RTP peer 90 checks 通过；无 Xbox 长稳测试，不能阻止服务端关闭或保证 Wi-Fi 永不丢包。 |
| AMD NR | MIT 独立 runtime、共享 NR 图及按 GPU 的 UI 接入；用户 0.39 包校验通过，186 权重/2 映射/62 HIP 核补齐，真实 DLL API 和模块布局验证通过。 | RTX5070 无 RX9000 HIP 推理/画质/性能/长稳实测；仅 1080p 像素预算，HDR/原生 4K AMD NR 导出未支持。 |
| 导出码率 | 有效键入即时提交，草稿不受其他 exportChanged 覆盖，统一 1–300 Mbps，具体 validate 错误和冻结参数入日志。真实 bridge/worker/NVENC 的两种复现均通过。 | 本机驱动 616.56；用户 616.92 未直接测试。 |
| RTSS 透明背景 | 软件绘制用 Rectangle/Image，Window 不透明；真实 RTSS 7.3.7 自动兼容，RTSSHooks64 已注入，截图深色 alpha=255。 | 未枚举全部 RTSS/OBS 版本、注入组合与显示配置。 |
| NVIDIA 插件误判 | 删除 nvppex/NvPresent 风险分类、检测与崩溃归因；旧 device/nvppex marker 不再触发 NVIDIA App 提示，真实 GPU 错误仍记录。 | 不宣称所有第三方插件安全；不再只凭模块存在判断崩溃。 |

## 自动测试与真实导出

- Fresh 工作树编译与最终增量成功：
  `E:/项目/Veyra/logs/field-upgrade-20261003/build-final-contracts.log`。
- `E:/项目/Veyra/tests/field-upgrade-20261003/final/summary.json`：Qt Quick
  39 passed，Xbox loopback 90 checks，WASAPI float output passed，AMD ABI/identity
  GPU-copy 62 checks，全部 0 failure。测试 provider 没有 HIP/NR，不随候选分发。
- 音频：48k float stereo、5 秒 250 个 20ms 块；视频 RTP epoch 与音频从零 PTS
  不同时走现有 video-arrival fallback（35ms），未伪造同一时钟域。
  缓冲计数不是扬声器/屏幕端到端延迟，gain=0 无可听证据。
- `field-upgrade-ui.py rtss --rtss` 在 RTX5070 / 616.56 通过。
  输入 2560×1440、30fps、MPEG-4 Part 2 的 AVI，0.8 秒、24 帧；NR 启用、
  RTX Video SR quality 4、目标 4K HEVC。导出仍依原生 NR 合同执行，
  不将预览的 1080p 内部档冒称导出已降采样。
- 输入 18 Mbps VBR 后改 HEVC/4K/容器再开始，第二份输入 24 Mbps CBR 直接
  开始，均成功输出 24/24 帧、3840×2160 HEVC。worker encoder 收到 18/24，
  启动后 UI 改为 7/8 不改变冻结任务。日志/ffprobe：
  `E:/项目/Veyra/logs/field-upgrade-20261003/production-rtss/`。
- 短片 ffprobe 平均约 20.745/27.311 Mbps。VBR 随复杂度变化，CBR 启动/缓冲
  受短样本影响；本次证明设置到达编码器，不保证实际平均数逐位等于目标。
- RTSS 截图在
  `E:/项目/Veyra/tests/field-upgrade-20261003/production-rtss/software-background.png`，
  背景取样 (37,37,41,255)、(16,16,20,255)、(8,8,10,255)。三档背景像素检查
  也在 Qt Quick 回归中。只关闭本轮启动的 RTSS，未编辑用户 RTSS/OBS 配置。
- Source guard 和 git diff --check 通过。旧 delivery.ps1 明确仅支持 legacy
  Win32，QML 用以上定向生产测试，不把旧 gate 当通过。

## 失败修复与边界

初次 AMD adapter 编译发现 st 作用域，已修复。MSVC 对上游 GNU noinline、
Windows min/max、user32 需显式适配，版本化 recipe 重建成功（lmxxf-recipe.log）。
Qt 测试初次进程路径/Unicode argv0/offscreen plugin 失败，改为绝对 executable
+ 相对 argv0、明确 Qt platform plugin 路径后通过；不把失败计为通过。
最初 0.40 下载尚无链接、0.39 镜像未取得，用户 zip 已补齐该缺口。
RTSS 普通启动需提升，本轮以当前用户 RunAsInvoker 启动测试副本，成功注入，
没有提权或修改系统配置。没有 RX9000 或真实 Xbox 的验收，不冒称全型号稳定。

## 本地候选

最终候选 Veyra-2.0.2-field-20261003-win64-portable.zip，766558374 bytes，
SHA256 `099751e596b4a7de814ddc2af9cdaa527817d1a422861cdcee5a6be77877c679`。
代码存档 `c80662f344032ca3adac42277b1ab6726609cde6`，对应源码 ZIP 单独提供。
2054 个 payload 文件校验通过。干净解压后在 GPU D3D12 与软件模式各启动
home/pro/node/exp/set/min 六页，12 张截图、背景像素与正常退出均通过；
证据 E:/项目/Veyra/logs/field-upgrade-20261003/clean-smoke/summary.json。
最终多语言与 AMD NR preset/list/node session 持久化回归通过，见 ui-final。

输出 `E:/项目/Veyra/test-packages/field-upgrade-20261003/`；软件版本仍为 2.0.2，
候选名称带 field 标识。AMD 放 runtime/amd-nr，独立 manifest/许可证随包；
原 2.0.2 的 43 个增强运行组件沿用原件身份。没有 SDK、测试替身、PDB、测试媒体、
凭据或用户配置。最终包审计/干净启动在 WORKLOG 补记；硬件未验与 AMD 功能
边界随包保留，源码/模型/运行库继续隔离。本轮未授权公开资产上传。
