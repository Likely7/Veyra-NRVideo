# DLSS 性能模式 / 双卡运算 / 全屏 Home 面板 2026-10-14（Claude）

分支 `claude/fg-gpu-home-20261014`，基于 `e550f96`（latency-min 之上）。产物在
`E:/项目/Veyra/{build,tests,logs,tmp}/claude-fg-gpu-home-20261014`。
透传只出方案：[PC_GAME_PASSTHROUGH_PLAN_2026-10-14.md](../plans/PC_GAME_PASSTHROUGH_PLAN_2026-10-14.md)。

## 1. DLSS 帧生成性能模式

- **机制：** NVIDIA App 11.0.9 的“DLSS 帧生成性能模式（测试版）”会通过 OTA 下发 DLSS-G 310.9.16
  （`nvapp_override` 模型）。模式本身由创建时参数 `DLSSG.LiteCandidateBlendingEnabled=1` 打开，
  逐帧设置无效。按字符串检索，只有 310.9.16 的 DLL 里有这个参数名；310.9.1、310.7 和驱动
  自带的版本都没有。
- **运行库：** 用户选择“打包进运行库”。310.9.16 放在 `runtime/experimental/dlssg-lite/nvngx_dlssg.dll`，
  身份：310.9.16.0，7,506,544 bytes，SHA256 `CADEE1AA…1BEA72`，NVIDIA 签名有效。
  开关打开时，NGX 的功能路径先找这个目录，再找 `runtime/experimental`。
  NGX 核心缓存的键加入了该目录，开关切换会重建核心。
- **范围：**
  - 只用于 RTX 50。40/30 的多帧解锁补丁只认 310.9.1，在这两代卡上提示“目前只支持 RTX 50 系列”。
  - 默认关闭；导出仍用标准模式。
  - 软件包没带这个运行库时，开关变灰并说明原因。
- **界面：** 补帧卡片“补帧方式”为 DLSS 时出现“DLSS 性能模式”开关，列表页和节点页都有，
  偏好键 `dlssgPerformance`。
- **实测**（RTX 5070，T911 2K60 NV12 + NR，取稳态中位数）：

  | 倍率 | 补帧 GPU P95 | 过期帧 | 送显→上屏 P95 | 回调→送显 P95 |
  |---|---|---|---|---|
  | 6X 标准 → 性能模式 | 8.1 → 3.3 ms | 7.7 → 1.0 次/秒 | 9.59 → 7.45 ms | 31.5 → 26.8 ms |
  | 4X 标准 → 性能模式 | 5.6 → 2.8 ms | — | 7.01 → 6.36 ms | — |

## 2. 双卡：同时用这块显卡运算

- **设置：** 设置 → 性能，“GPU 占用监控”下面新增“同时用这块显卡运算”（偏好键 `computeOnMonitorGpu`）。
  上面选了一块具体的显卡时，增强、补帧和导出都在这块卡上运行；选“自动”时仍用性能最强的卡。
- **实现：**
  - `gfx::setPreferredAdapterLuid` 写入环境变量 `VEYRA_COMPUTE_ADAPTER_LUID`，导出子进程会继承。
  - `D3D12DeviceContext` 优先用这块卡，找不到时回到默认顺序，并写日志。
  - 预热的设备如果不在这块卡上，就丢弃重建。
  - 界面判断效果是否可用（`effectGpu`、VFG 判定）时，用的是同一块卡。
- **启动顺序修正：** 运算卡在采样能力和恢复上次链路之前确定。之前的顺序下，AMD 运算卡仍显示
  DLSS 补帧和 NVOF 光流。
- **运行中切换：** 按重启时的规则迁移设置：
  - DLSS 改成可用的补帧方式；
  - NVOF 改成 FidelityFX 光流；
  - 新卡跑不了的效果关掉，参数保留；
  - 迁移结果保存。
- **实测：**
  - AMD 核显运算：适配器 `used=yes`，60 次/秒送显，无错误；补帧自动为 FSR 3.1 2X + AMD FidelityFX 光流。
  - 运行中把 DLSS 4X 切到 AMD：当场改为 FSR 2X，120 次/秒送显，无错误。
  - 切回“关闭”：保持 FSR，不自动改回 DLSS。

## 3. 全屏 Home 调整面板

- **极简全屏：** 按 Home，专业页在全屏画面下面切换为当前页（不播放动画），调整面板显示出来；
  再按 Home 或 Esc 退出全屏时回到极简。
- **节点模式**（`veyra.nodeMode===1`）：呼出节点页下半部分的画布，作为画面底部的面板
  （高度 max(260, 42%)），从原生视频窗口中挖出，右上角提示“Home 关闭”。
- **实测**（PostMessage 发送 Home，PrintWindow 截图）：
  - 极简 → 列表面板 → 回到极简；
  - 节点 → 底部画布 → Esc 退出全屏并回到极简；
  - 页面日志 `leaving=` 为空，说明没有动画闪烁。
  - 面板开着时用户自己换页（比如切换极简/专业），面板就在那一页结束，之后不会跳回原页；下次按 Home 一次就能打开。

## 4. 回归

- repair 259/0；UI contract 通过；realtime preview 0 失败。
- 节奏测试：回放 6X 垂直同步、生命周期、屏幕 XeSS、屏幕 DLSS 4X 全部通过。

## 5. 未做 / 待用户

- RTX 40/30 实卡未测（这两代不启用性能模式）。
- 真实双独显未测（本机只有 RTX 5070 + AMD 核显）。
- 透传按方案等待拍板。


## 6. 第二轮（用户：“在捕获内加一个开关…把你没注意到的内容处理好，顺便看看 VFG”）

- **游戏模式（穿透）**：已实施，见 [PC_GAME_MODE_2026-10-14.md](PC_GAME_MODE_2026-10-14.md)。
  显示器捕获同时修正两处：去掉黄色边框；控制条不再被捕获进画面。
- **双卡切回恢复补帧设置**：在 AMD 上被换成 FSR 2X 的 DLSS 4X，切回后恢复为 DLSS 4X。
  - 只在列表模式记住，偏好键 `computeFgMemory`，用一次即清除。
  - 实测 `switch-back-151042`。
- **双卡代码审查**：
  - VFG 按 D3D12 适配器 LUID 选择 CUDA 设备；NVOF、NGX、NVENC 都用同一个 D3D12 设备。
  - 只有全新配置的默认值（NR 运行库、FSR 超分默认值）仍按性能最强的卡选择，其余都跟随运算卡。
- **310.9.16 发布准备**：核对身份后复制到 `E:/项目/Veyra/releases/runtime-staging/dlssg-lite-310.9.16/`，
  附 `runtime-lock-row.json`。
  - 2.0.x 的打包方式是“版本运行库锁 + 上一版包作运行库来源”，下次打 NVIDIA 包时把这一行加进去即可。
  - 主仓库的 `runtime_local` 没动：其中 `nvidia/nvngx_dlssg.dll` 是 310.7，`package-portable.ps1`
    并不是 2.0.x 的打包路径。
- **VFG**：NVIDIA 没有新版本。
  - PyPI 与 pypi.nvidia.com 上 `nvidia-vfx` 最新为 0.2.0.0（2026-09-30）；VFX SDK Samples 最新 v1.3.0.1，
    与当前集成版本相同。
  - NGX 模型目录里没有 VFG 的 OTA。
  - 官方文档：每次 `NvVFX_Run` 生成一帧，只支持 batch 1；只有 Low / Medium / High 三档，没有性能参数。
  - 实测每张 GPU 耗时（RTX 5070，CUDA 事件）：
    - 4K：Low 1.10 ms、Medium 3.2 ms、High 17.3–17.5 ms；
    - 1080p：Medium 2.9 ms、High 15.1–15.4 ms。
  - High 约为 Medium 的 5 倍，而且几乎不随分辨率变化，开销在模型本身。10-11 的深挖已确认子帧不复用、
    调用方式不浪费、多实例收益很小。
  - 结论：没有可以在 Veyra 侧把 High 本身做快的办法。可选做法：
    - 用已有的“高+中 / 高+低”混合档；
    - 60fps 源用 High 2X 已占满显卡，30fps 源才适合；
    - 等 NVIDIA 新 SDK（近一年约每 2–6 个月一版）。

回归：repair 259/0、UI 合同、realtime preview 0 失败；节奏 4 项通过；`veyra_screen_capture_tests`
5 项 RGB 颜色检查失败，原版代码同样失败（本机 HDR 桌面），见游戏模式记录。
