# Issue #88：RX 9060 XT 开 AMD NR 第一帧崩溃——HIP 子进程自检（2026-11-06，Claude）

- **分支**：`claude/issue88-hip-preflight-20261106`，工作树 `E:/项目/Veyra/worktrees/claude-issue88-20261106`。
- **基线**：main `edd901e`。与 2.1.3 产品 `7a2d15d` 相比，代码没有差异，只多了文档。
- **状态**：未提交，未合并，未 push/Release，也没有在 issue 上回复。
- **用户要求**：“看一下 issues88 给他修复”，并附上了报告人的 `veyra-nr-crash-evidence.zip`。

## 现场证据

**报告人环境**：

- RX 9060 XT（gfx1200，Navi 44，8 GB，1002:7590）；
- 驱动 32.0.32015.12012（Adrenalin 26.10.2），System32 中的 `amdhip64_7.dll` 为 10.0.3683.0，`amd_comgr_3.dll` 为 3.0.0.0（两者时间戳相差约 6 分钟，同一个驱动包）；
- Windows 11 26200。

**崩溃情况**：

- 2.1.2 和 2.1.3 共崩溃 14 次，全部是 `amdhip64_7.dll+0x4A40A0` 的访问违例（读地址 0x390）。
- Small 和 Original 两个模型都会崩，每次都在第一帧 NR 约 200 ms 后。

**minidump 解析**（用自写的 minidump 读取脚本，见 `E:/项目/Veyra/reports/claude-issue88-20261106/`）：

- **崩溃线程**是一个 HIP 内部线程：栈上只有 `amdhip64_7.dll`，没有 Veyra 或 lmxxf 的帧。
  - 崩溃指令 `mov rcx,[rcx+390h]` 是一个函数的第一条指令，当时 `rcx=0`，即传入了空对象。
- **同一时刻的播放线程**正在 lmxxf 里调用 HIP 并等待：
  - 用 call-return 核对后，最上层的 lmxxf 帧是 `LmxxfNrRuntime.dll+0xA4A2E`；
  - 该处是 `call [rsi+1C8h]`，前后引用的操作名依次是 `"hipInit"`、`"device"`、`"stream"`，后面紧接加载 `c32_prefix_reference.hsaco`；
  - 也就是说，它正卡在网络初始化的 `hipStreamCreate(&stream)` 里，还没有加载任何 gfx1200 内核。
- **模块来源**：HIP 和 COMGR 都来自驱动（System32），不是 Veyra 打包进去的副本。

## 对照

- **上游 lmxxf issue 18**：另一台 RX 9060 XT（gfx1200），驱动 26.10.1，在 Magpie 里运行 0.45 的同一套内核，能正确出画面，只是慢。所以 gfx1200 并不是根本不能创建 HIP 流。
- **Veyra 进程的特殊之处**：默认把整个进程的 GPU 调度等级设为 Realtime（日志 `gpu-priority requested=5 actual=5`，报告人每一次都是这样）。RX 9000 上的 lmxxf 在主进程里运行 HIP；RX 7000 走的是独立 worker 进程。
- **本机探针**（`E:/项目/Veyra/tmp/claude-issue88-20261106/probe`）：用 AMD 核显和 HIP 6.4，分别在普通、先建 D3D12 设备、再设为 Realtime、设为 High 四种状态下创建流，全部正常。本机没有 RDNA4 和 HIP 7，这个结果不能代表报告人的环境。
- **结论**：根因在 AMD HIP 运行库内部，本机无法复现。“Realtime 调度等级触发”是最可疑的 Veyra 侧差异，但没有证实。

## 改动

**`include/veyra/pipeline/AmdHipPreflight.h` 和 `src/pipeline/AmdHipPreflight.cpp`（新增）**：

- **子进程模式**：`<exe> --amd-hip-preflight <HIP DLL> <适配器 LUID> <调度等级>`。依次执行以下步骤，每步之前先把步骤名写到管道：
  1. 用 LUID 在同一块卡上创建 D3D12 设备；
  2. 设为父进程当前的调度等级；
  3. 从 System32 加载 `amdhip64_7.dll`（与 lmxxf 相同）；
  4. `hipInit`；
  5. 用 `hipGetDevicePropertiesR0600` 按 LUID 找设备（与 lmxxf 一样，只有一个且没有 LUID 时用它）；
  6. `hipSetDevice`；
  7. `hipStreamCreate`；
  8. 分配 1 MB 显存，在流上清零、同步后释放；
  9. 销毁流。
- **崩溃记录**：未处理异常过滤器记录异常码、`模块+偏移` 和当时所在的步骤。
- **退出方式**：成功后直接结束子进程，避免 HIP 卸载时出错把“通过”误报成崩溃。
- **父进程的降级顺序**：先用当前等级。不通过时，Realtime 依次试 High、Normal；High 试 Normal。
  - 加载、建设备这类失败与调度等级无关，不再往下试。
  - 每块卡在每个进程里只测一次，结果缓存。
  - 每次尝试写一行日志 `[amd-hip-preflight]`。

**`LmxxfNrBackend::open`**：在模块、ABI、资源和环境检查都通过之后、`Create` 之前执行自检，且只在宿主登记了自检程序时执行。

- **当前等级通过**：行为与原来相同。
- **当前等级失败、较低等级通过**：用 `setGpuPriorityComputeCap` 把本进程的 GPU 调度等级限制到通过的那一档，并记录警告。
  - 默认值不变，只有自检证明当前等级下 HIP 起不来时才降。
- **全部失败**：AMD NR 打开失败，走现有的 NR 后端失败路径，播放继续。错误信息包含每一档的结果，并提示更新驱动、发送日志。

**`GpuSchedulingPriority`**：新增独立的“计算运行库上限”`setGpuPriorityComputeCap`。它与原来的抖音直播伴侣上限取较低者，不改变原来的上限语义。

**`apps/veyra-qml/main.cpp`**：

- `main()` 最前面处理 `--amd-hip-preflight`，在 Qt、窗口、播放器状态、崩溃转储之前。
- 随后把自身 exe 路径登记为自检程序。
- 不新增可执行文件，发布包清单不需要改。

**`tests/unit/AmdHipPreflightTests.cpp` 和 CMake 目标 `veyra_amd_hip_preflight_tests`**：

- **纯逻辑**：降级顺序、输出解析、汇总文字。
- **真实 GPU 调度等级**：Realtime → 计算上限 High → 与 Normal 上限叠加 → 解除。
- **真实子进程**：需要 AMD 适配器，并设置 `VEYRA_TEST_AMD_HIP_DLL`。
- **仅测试用的钩子**：`VEYRA_TEST_AMD_HIP_PREFLIGHT_CRASH=<步骤>:<等级|all>`，它在另一个线程读地址 0x390，模拟现场的崩溃方式。

## 验证

**构建**：用 `scripts/acceptance/build-recording-handoff.py`，任务名 `claude-issue88-20261106`，标签 `2.1.3-issue88-test`，并发 2。

- 构建目标：`veyra_qml_ui`、`veyra_amd_hip_preflight_tests`、`veyra_lmxxf_nr_tests`、`veyra_amd_nr_graph_tests`。
- 首次新建目录 295.5 秒通过；补充子进程信息行后增量构建 9.6 秒通过。
- 新文件没有编译警告。日志里的 `getenv` C4996 警告来自 `LmxxfNrBackend.cpp` 原有的第 59–63 行。

**`veyra_amd_hip_preflight_tests`**：本机 AMD 核显（gfx1036，HIP 6.4.50101），设置 `VEYRA_TEST_AMD_HIP_DLL=amdhip64_6.dll`，全部通过，用时约 4 秒：

- **纯逻辑**：降级顺序、输出解析（含 info 行）、汇总文字。
- **真实调度等级**：Realtime 为 5 → 加计算上限后为 4 → 再叠加 Normal 上限为 2 → 解除后回到 5。
- **真实子进程，正常 HIP**：在等级 5 下全部步骤走到 release，通过。
- **只在等级 5 的 stream 步骤崩溃**：子进程以 0xC0000005 退出并报告偏移和步骤，父进程不受影响，等级 4 通过并被选用。
- **每一档都崩溃**：试了 3 次，HIP 被拒绝。
- **缓存**：同一块卡在同一进程里只测一次。
- **HIP 库不存在**：在 load 步骤失败（code 126），只试一次。

**原有 AMD NR 回归**（准备方式同 `menu-amd-hdr-fix.py` 的 gpu 模式，使用假的 lmxxf 运行库）：

- 两轮都通过：AMD 合成图 128 个用例 0 失败（HDR 108 项全部通过，D3D12 调试层 0 错误）；lmxxf ABI 66 项 0 失败。
- 这两组测试没有登记自检程序，走的是原来的路径。

**真实 `veyra_qml_ui.exe`**：

- **子进程模式**：放进 2.1.3 文件目录，用临时副本，结束后删除。
  - 不加钩子：在等级 5 下走完全部步骤，输出 `done status=ok`，退出码 0。
  - 加崩溃钩子：退出码为 0xC0000005，输出 `crash ... step=stream`。
  - 没有写日志，也没有写转储。
  - 打进补丁的那一份程序又按同样两种情况复测了一遍，结果相同。
- **普通启动冒烟**：私有配置，Pro 页，8 秒后自动退出。退出码 0，日志里没有 ERROR 或 WARN。

**测试补丁**：`E:/项目/Veyra/test-packages/claude-issue88-20261106/Veyra-2.1.3-issue88-test.zip`（8,650,216 字节，SHA256 `9155e585…`）。

- 内含主程序 `063635483e3e…`、双语 README 和 SHA256SUMS。
- 用法是替换 2.1.3 AMD 包里的 `veyra_qml_ui.exe`；两个显卡包的主程序本来就是同一个文件。

## 没有验证的部分

- **RDNA4 实卡**：本机没有 RDNA4 和 `amdhip64_7.dll`。真实的 gfx1200 和 HIP 7 在 Realtime、High、Normal 下的结果，要等报告人运行测试版后从日志里看。
- **报告人机器上的结果**：“降到 High 后 NR 就能用”只是一种可能。也可能三档都失败；那样 AMD NR 会被关闭但不再崩溃，根因仍要等 AMD 驱动修复，或者再做进一步定位。
- **自检只覆盖到创建流和清零显存**：如果真实运行时之后的步骤（加载内核、导入 D3D12 共享资源）在主进程里崩溃，自检拦不住。
- **本机无法走到 lmxxf 那条路径**：核显名称不含 RX 90，所以真实程序没有完整走过“自检 → 降级 → lmxxf 建网络”；这一段由单元测试和代码审读覆盖。
- **启动耗时**：第一次打开 AMD NR 时，多出子进程初始化 HIP 的时间。本机核显每次约 0.1–0.5 秒，现场没有测。
