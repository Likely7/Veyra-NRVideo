# RX7000 NR：多 GPU 时工作进程只看选中的显卡（2026-11-03，Claude）

用户：“直接修复吧”。排查见 `E:/项目/Veyra/reports/claude-rdna3-nr-field-20261103/FINDINGS.md`。

## 原因

现场机器：RX 7900 XTX（gfx1100），另有 Ryzen 核显（gfx1036）。HIP 的设备顺序是核显 0 号、独显 1 号。

RedDukeDev/ZLUDA `b135e23` 至少有两处默认使用 HIP 0 号设备：
1. **NVAPI 报的 LUID**：报的是 0 号设备的 LUID，NGX 因此拒绝显卡架构，返回 0xBAD00001。Codex 已在 `b27c0df` 修复。
2. **清零用的流建错了设备**：`Context::new` 先建 `alloc_stream`，后执行 `hipSetDevice`，所以这条流建在核显上。每次 `cuMemAlloc` 都通过这条核显流去给独显的显存清零。现场工作进程正是在 NGX 申请 108.6 MB 权重之后退出的。

## 改动

- **`include/veyra/pipeline/Rdna3HipDevices.h`（新）**：
  - 读取 `hipDeviceProp_tR0600`：name 在偏移 0，LUID 在偏移 272，gcnArchName 按前缀查找。
  - 按 LUID 匹配设备，没有或不唯一都视为匹配失败。
  - 定义探测子进程的输出格式和解析方法。
  - 启动探测子进程：只继承它自己的输出管道，超时 15 秒。
  - 用 `SetEnvironmentVariableW` 加 `_wputenv_s` 设置 `HIP_VISIBLE_DEVICES`。
- **`src/pipeline/Rdna3NrWorker.cpp`**：
  - 在加载 `amdhip64_7.dll` 之前调用 `selectHipDevice`。它用同一个 exe 的 `--hip-probe` 模式列出设备。只有 HIP 设备多于一块、且选中适配器的 LUID 唯一匹配时，才把其余设备隐藏。
  - 外面已经设置了 `HIP_VISIBLE_DEVICES`、`CUDA_VISIBLE_DEVICES` 或 `GPU_DEVICE_ORDINAL` 时，保留原设置。
  - 探测失败时所有设备保持可见，和原来行为一致，不会因此报错退出。
  - 日志新增设备列表和选择结果；诊断阶段新增 `hip-device-selection`。
- **测试**：新增 `tests/integration/Rdna3HipDevicesTests.cpp`，并在 `CMakeLists.txt` 中加入目标 `veyra_rdna3_hip_devices_tests`。
- **不变的部分**：主程序、协议、模型、ZLUDA、翻译缓存、驱动和配置都没有改。合同测试用的 worker（`VEYRA_RDNA3_CONTRACT_WORKER`）不包含这些改动。

## 验证（本机 RTX 5070 + AMD 核显 gfx1036；没有 HIP 7，也没有 RX7000）

- **构建**：`build-recording-handoff.py --task claude-rdna3-hip-visible-20261103 … --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=…/prepared`，构建了 4 个目标，用时 43.7 秒。
  - `C4005 WIN32_LEAN_AND_MEAN` 警告原来就有，不是这次引入的。
- **`veyra_rdna3_hip_devices_tests`（单元）**：15 项通过，0 失败。
- **`veyra_rdna3_hip_devices_tests --hip C:\Windows\System32\amdhip64_6.dll`**：19 项通过，0 失败。
  - 真实探测得到 `index=0 luid=0x16583 arch=gfx1036`，这个 LUID 与 DXGI 报告的一致，说明偏移 272 正确。
  - 在进程内、加载 HIP 之前设置序号后，只剩所选设备。
  - 设置一个超出范围的序号后，设备数变为 0。说明 Windows 上的 HIP 确实读取这个变量。
- **`veyra_amd_nr_worker.exe --hip-probe`**：
  - 对 HIP 6：列出 1 块设备，退出码 0。
  - 本机没有 HIP 7：输出 `load-failed-win32-126`，退出码 2。
  - 参数个数不对时仍返回 2。
- **`veyra_rdna3_worker_diagnostics_tests`**：14 项通过，0 失败。
- **未运行**：`veyra_rdna3_nr_contract_tests`。它依赖 `veyra_pipeline`，而这次的改动不进入合同测试 worker。
- **没有在 RX7000 实卡上推理过**，需要现场用户确认。

## 交付

`E:/项目/Veyra/test-packages/claude-rdna3-hip-visible-20261103/Veyra-2.1.2-RX7000-NR-multiGPU-fix2.zip`

- 包内 worker 的 SHA256 是 `eb012a1c…`；NVAPI 仍用 Codex 的 `a7adb043…`。另含崩溃收集脚本、许可证和源码补丁。
- 源码补丁以公开的 v2.1.2 源码 ZIP 为基准，已重放并逐文件比对。
- 回执：`E:/项目/Veyra/reports/claude-rdna3-hip-visible-20261103/FINAL.json`。

分支 `claude/rdna3-hip-visible-20261103` 基于 Codex 的 `77fcd89`，独立工作树为 `worktrees/rdna3-hip-visible-20261103`。改动未提交、未合并、未 push。
