# 1.4.4 XeSS A/B 测试包

## 目的

本轮生成两个独立的 1.4.4 测试包，用同一段 `p001.mp4` 对比当前 XeSS 链路与
1.4.3 XeSS 链路。两个包都保留 1.4.4 的采集、HDR、字幕、UI、输出限制、NR
时间域和其他播放器修复；只对 XeSS 帧生成实现做对照。

这不是画质验收，也不是屏幕扫描帧率验收。短测只证明程序能启动、进入 XeSS、
完成 4X 提交并正常退出，用户需要在同一台机器、同一显示器和同一设置下进行
肉眼与长时间对比。

## 两个包的差异

### A：当前 XeSS

- 源工作区：`codex/fg-stability-20260921`，提交 `0de0a1d4175ebb392d9f2f425df49f504548b2c8`，按构建时工作区状态编译。
- 使用当前 `XessPacing` / `XessPresenter`，保留现有诊断和资源/呈现时序代码。
- 默认 `frameRenderTime=0`；源 PTS 时间提示仍是显式诊断开关，默认关闭。
- 不使用 1.4.3 的文件播放持续欠速自动暂停 XeSS 门控。

### B：1.4.3 XeSS 对照

- 其余源码基线仍为同一个 `0de0a1d`，只在独立 worktree
  `E:/项目/Veyra/worktrees/xess143-compare-20260921-r1` 中回退 XeSS 部分。
- `XessPacing.h/.cpp`、`XessPresenter.h/.cpp` 恢复到 `v1.4.3`
  源提交 `3b4570e1f3f7301fcce21b829003bb8ef7854e24`。
- `VideoPresenter` 恢复 1.4.3 的 XeSS provider 调用方式：不传源 PTS，
  `frameRenderTime` 固定为 0。
- 恢复 1.4.3 的 `XessGenerationGate`：持续欠速达到门槛时暂停 SDK 生成，
  连续健康帧后恢复。这个门控正是对照变量，不能把 B 的自动暂停误判为“当前
  XeSS 已修好”。

没有回退 DLSS、FSR、采集卡、字幕、HDR 或 UI 代码。

## 构建与包

| 包 | ZIP | 大小 | SHA-256 | EXE SHA-256 |
|---|---|---:|---|---|
| A 当前 XeSS | `Veyra-1.4.4-beta-xess-current-20260921-win64-portable.zip`（历史产物路径：`../../../test-packages/1.4.4-xess-current-20260921-r2/Veyra-1.4.4-beta-xess-current-20260921-win64-portable.zip`，原产物不随当前源码保留） | 472,386,611 | `03567900697B359C8BFC6DC797929F81167D331016F5C2745BB176E31C55FA11` | `931F342A7EBFA6F9D49190C8929B017E5B874B5237444F34E6276CB687B4F8F7` |
| B 1.4.3 XeSS | `Veyra-1.4.4-beta-xess143-20260921-win64-portable.zip`（历史产物路径：`../../../test-packages/1.4.4-xess143-20260921-r1/Veyra-1.4.4-beta-xess143-20260921-win64-portable.zip`，原产物不随当前源码保留） | 472,384,216 | `D6077DCE8A977203AF57E8980B5FDDFBC176340C7F610396FFBB312ACB2E75EF` | `81E3A2A579F3083B78589ABEA4CDC874EFF015ECDB066AA79D6095901CA0C727` |

实际绝对路径：

- `E:/项目/Veyra/test-packages/1.4.4-xess-current-20260921-r2/`
- `E:/项目/Veyra/test-packages/1.4.4-xess143-20260921-r1/`

两个包的 `package-audit.json` 都是 123 个文件、`forbiddenFiles=0`，运行时身份和
许可证核验通过。构建日志分别位于
`E:/项目/Veyra/logs/1.4.4-xess-compare-20260921/`。

## 本机烟测

命令（两包完全相同）：

```text
Veyra.exe "E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4" --fg-xess --fg-multiplier 4 --no-nr --no-sr --smoke-seconds 12
```

- A：exit 0，`failed=false`，639 个源帧，1908 个生成帧，`processedFps=60.00`；
  XeSS 日志显示 `framesPresented=4`，pacing 稳定段约 4.167 ms。
- B：exit 0，`failed=false`，668 个源帧，1995 个生成帧，`processedFps=60.00`；
  XeSS 日志显示 `framesPresented=4`，pacing 稳定段约 4.166 ms。

两次进程运行时间和解码帧数略有差异，不能用这组短测宣称 A 或 B 的画质、功耗
或长时间稳定性更好；它只确认两个包都能实际进入 4X XeSS。完整 stdout/app 日志
在同一 `logs/1.4.4-xess-compare-20260921/` 目录。

本轮没有合并 main、创建正式 tag、推送或发布 GitHub Release。
