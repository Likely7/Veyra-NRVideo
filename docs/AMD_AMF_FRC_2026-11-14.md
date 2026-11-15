# AMD AMF FRC 补帧接入（2026-11-14）

用户先确认要找的“A卡视频补帧”就是 AMD AMF FRC，随后说“开始接入”。

- **分支**：`claude/amf-frc-20261114`，叠在 `claude/fsr4-ml-rx9000-20261114`（`f1cd59c`）之上，两者都还没合入 main。
- **目录**：工作树、build、tests、logs、reports、tmp 都用同名任务目录。
- **范围**：只在本机，没有合 main、没有 push、没有发布。

## AMF FRC 是什么

AMF FRC 是 AMD 驱动自带 AMF 运行库里的视频帧率转换组件：

- **文件**：调用入口是 `System32\amfrt64.dll`，实现在驱动里。本机核显驱动 32.0.21042.62 带的运行时版本是 1.4.37。
- **组件**：`AMFFRC`，头文件是 `components/FRC.h`。
- **SDK**：AMF SDK v1.5.3，提交 `57ee0cfb`，MIT 许可。
- **硬件要求**：官方文档没写支持哪些显卡，需要实测。

本次只下载了 SDK 头文件：

- **来源**：GitHub `GPUOpen-LibrariesAndSDKs/AMF` 的 `amf/public/include` 目录。
- **大小**：57 个文件，约 0.6 MB。
- **存放**：`deps/amf-sdk-1.5.3-20261114`，里面附许可证和逐文件 SHA256（`SOURCE.json`）。
- **随包**：运行库不随 Veyra 打包，用的是用户自己装的 AMD 驱动。

## 可行性测试（本机 AMD 核显：Ryzen 7000 集成的 Radeon，RDNA2，2 CU，无显示输出）

测试程序是 `reports/claude-amf-frc-20261114/amf_frc_probe.cpp`。做法是自建 D3D12 设备，按 AMF 的 D3D12 栅栏约定，给 AMF FRC 送一块每帧右移的白方块。

- **初始化**：AMF 运行时 1.4.37（头文件 1.5.3），按运行时版本初始化成功，FRC 组件 `Init` 约 0.27–0.32 s。
- **基本行为**（1080p RGBA8，不参考下一帧）：每送一帧，立刻取回一张输出。从第 3 帧起，输出里方块的位置正好是前后两帧的中点。例如第 3 帧方块中心在 157、第 2 帧在 127，输出是 142。
  - 第 0 帧的输出是原图（没有上一帧）。
  - 第一次运行时第 1 帧的输出重复了第 0 帧，属于预热。
- **CPU 耗时**：`SubmitInput` 约 0.01 ms；`QueryOutput` 约 0.5–2.6 ms，不等 GPU 做完。只有第一次运行的前 3 帧阻塞了 65–233 ms（预热），之后的运行首帧约 9–11 ms。
- **GPU 耗时**：从送入到我们复制回来约 16 ms（原分辨率运动搜索）。改用性能模式约 11 ms，720p 约 8.5 ms。这是 2 CU 核显的数字，不代表 AMD 独显。
- **格式**：RGBA16F（HDR scRGB）和 RGB10A2 都能跑，中点同样正确。
- **参考下一帧**：打开后输出晚一对（第 4 帧的输出是第 2、3 帧的中点），和文档说的“多一帧延迟”一致，所以不采用。
- **栅栏约定**：AMD 示例里的约定能用。每张贴图挂一个栅栏，栅栏上记最后要等的值，谁用完谁加一并发信号。实测输出贴图的栅栏值每帧增长 3。

## 接入设计

### 后端

`gfx::AmfFrcBackend` 每个真实帧分四步：

1. 在真实帧的命令列表里，把增强后的画面复制进 AMF 专用的 4 张输入贴图之一。
2. 这张列表提交后，按 AMF 约定把这张贴图的栅栏值加一并发信号，再 `SubmitInput`，并 `QueryOutput` 取回中间帧。
3. 在新的列表前让我们的队列等 AMF 输出贴图的栅栏，然后把中间帧复制到图的补帧缓冲。
4. 提交后把输出贴图的栅栏值加一并发信号，再把这张帧还给 AMF。

整个过程没有 CPU 等 GPU，只有队列等待。下一次用同一张输入贴图前，队列会等 AMF 读完。

### 模式与参数

- **模式**：`FRC_ONLY_INTERPOLATED`，不参考下一帧。参考下一帧会多一帧延迟，而且输出会晚一对，不符合补帧“前一对的中点”这个约定。
- **其他**：不确定处混合两帧，不显示角标。
- **搜索档位**：1440p 以上用 SUPER，其余用 HIGH。专用显存少于 2 GB 的 APU 用降分辨率运动搜索，这是 AMD 的建议。

### 接进渲染图

接法和 VFG 相同：

- 真实帧列表里做复制；
- GPU 计时从复制开始，到复制回来结束，所以算进了 AMF 自己队列上的时间；
- 历史重置（跳转、开播、跳过的对）先 `Flush` 清掉 AMF 的历史，这一对不显示生成帧；
- 只支持 2X，不需要 Veyra 的光流，所以不开光流会话。

### 设置和界面

- 新增后端 `FrameGenerationBackend::AmdFrc = 6`。
- 菜单名“AMD FRC · 视频补帧 · 2X”。只在 AMD 显卡上、并且 System32 里有 AMF 运行库时可选；其他显卡显示“需要 AMD 显卡（驱动自带 AMF 运行库）”。
- 节点模式里是 `amdfrc-fg`。
- 状态栏显示“AMD FRC”。
- 组件页列出 AMF 运行库的位置。
- 预设和会话文件接受新值。
- 导出可以使用。
- 启用失败时关闭补帧并提示原因；播放中切换失败时恢复上一套参数。
- 不会被自动选为默认，新老配置的默认补帧都不变。

### 词库与构建

- **词库**：新增 5 条，共 2879 条，缺译 0，占位符问题 0。
- **构建**：新增 CMake 变量 `VEYRA_AMF_SDK_ROOT`，找不到头文件时这个后端编译为不可用。

## 实现中遇到的问题

1. **程序目录里的 `D3Dcompiler_47.dll`**
   - **现象**：进了 Veyra 进程后，FRC 的 `Init` 返回 AMF_DIRECTX_FAILED。
   - **原因**：打开 AMF 自己的跟踪日志（环境变量 `VEYRA_AMF_TRACE`）后看到，AMF 用 `d3dcompiler_47.dll` 的 `D3DReflect` 读取自带的 D3D12 着色器，并按名字加载这个文件。Veyra 程序目录里有一份 Qt 自带的 6.3.9600（Windows 8.1 时代），它读不了这些着色器，返回 E_NOINTERFACE。系统目录里那份是 10.0.26100，能读。
   - **修复**：AMF 初始化前，如果进程里还没有加载这个文件，就先按系统目录加载系统那份。若已加载了别处的副本，记录警告。
   - **没动的**：随包的 Qt 文件没改。
2. **输入贴图的状态**：AMF 读完我们的输入贴图后会把它留在“着色器读取”状态，并把这个状态记在贴图上。复制前要按记录的状态转换，复制后再写回新状态，否则 D3D12 调试层报错。修好后调试层 0 错误。
3. **小分辨率下的性能模式**：降分辨率运动搜索在 640×360 时原样给回上一帧，960×540 以上正常。所以只有低功耗显卡并且分辨率不低于 720p 时才用性能模式，其余用原分辨率搜索。
4. **初始化后的第一张**：每次初始化后第一次插帧会重复上一帧（预热）。这一张不显示；跳转后 `Flush` 的第一对是正常的。
5. **AMF FRC 能在 N 卡上跑**：本机装着 AMD 核显驱动，所以有 AMF 运行库。在这种机器上，AMF FRC 在 RTX 5070 上也能运行，图级测试同样 45/45 张落在中点，引擎级测试也正常出帧。
   - 这不是 AMD 官方支持的用法，本版界面仍只对 AMD 显卡开放。
   - 是否对其他显卡开放（作为实验选项）由用户决定。
   - 没有 AMD 驱动的机器没有这个运行库，用不了。

## 验证

- **构建**：首次全量 8 个目标 324.6 s 通过，标签 `2.1.6-amf-frc-test`；之后为下面几处修复做了增量构建。最终 EXE `5db1433656c9…`，和测试副本里的私有副本一致。
- **测试副本**：1577 个硬链接，124 个私有副本，88 个着色器一致；修复后已用新编译的 EXE 和测试程序替换副本里的私有文件。
- **修复合同**（含 AMD FRC 新增 12 项）：通过（328 checks 0 failures）
- **ANR 设置**：通过（ANR_SETTINGS checks=47 failures=0）
- **界面合同**：通过
- **QML**：通过（Totals: 208 passed, 0 failed, 0 skipped, 0 blacklisted, 46101ms）
- **图级测试，核显**（`veyra_amf_frc_tests amd`）：通过（AMF_FRC generated frames move the bar (sharp edges), not blend the two frames pass=1；AMF_FRC generated=45 checked=45 midpoint=45 sharp=45 maxSoftPixels=1 maxErrorPx=0.5 debugErrors=0 pass=1）
- **图级测试，N 卡**（`veyra_amf_frc_tests other`；能跑就同样检查，不能跑就必须干净报失败）：通过（AMF_FRC_OTHER runs on this adapter；AMF_FRC generated frames move the bar (sharp edges), not blend the two frames pass=1；AMF_FRC generated=45 checked=45 midpoint=45 sharp=45 maxSoftPixels=1 maxErrorPx=0.5 debugErrors=0 pass=1）
- **FSR / XeSS / DLSS 切换回归**（N 卡）：通过（FSR4_UNSUPPORTED_ROLLBACK pass=1；FSR_SWITCH pass=1 cycles=18 generated=774 debugErrors=0）
- **FSR 4 ML 退回 FSR 3.1 回归**（N 卡）：通过（FSR4_FALLBACK fromWorking=0 provider=3.1.6 warning=52；FSR4_FALLBACK fromWorking=1 provider=3.1.6 warning=52；This test checks settings, output counts and lifecycle; not target-card support or interpolation quality.）
- **引擎级测试，核显**（`fg_settings amdfrc`）：通过（AMD_FRC applied=6 multiplier=2 frames=42 generated=27 warning=0；This test checks settings, output counts and lifecycle; not target-card support or interpolation quality.）
- **引擎级测试，N 卡**（`fg_settings amdfrc-other`）：通过（AMD_FRC applied=6 multiplier=2 frames=42 generated=28 warning=0；This test checks settings, output counts and lifecycle; not target-card support or interpolation quality.）
- **真实程序，核显**：核显上菜单列出并可选“AMD FRC”（FSR 4 ML 因不是 RX 9000 显示不可用）。切到 AMD FRC 2X 后补帧生效，提示文字正确。720p 测试视频共 736 对，632 对显示了中间帧；开头 AMF 预热时补帧准入拒掉 93 对。补帧阶段 GPU P95 约 5.8 ms，提交帧率中位数 49（源 30 帧；核显只有 2 个计算单元，画面还要跨卡送到 N 卡接的显示器）。AMF 进 643 帧、出 643 帧，没有卡死。
- **真实程序，N 卡**：N 卡上菜单里“AMD FRC”显示不可用，选择时提示“需要 AMD 显卡（驱动自带 AMF 运行库）”，补帧方式保持 DLSS。随后开 DLSS 补帧时出现一次 8.2 s 的“画面引擎无响应”，堆栈在 NVIDIA 的 CUDA PTX 即时编译。测试用的是空的私有 CUDA 缓存，属于项目已记录的冷启动编译停顿，与本次改动无关；之后补帧正常。

## 未验证

- **独立显卡**：RX 5000/6000/7000/9000 等 AMD 独显都没有实测，本机只有 2 CU 的核显。哪些 AMD 显卡和驱动支持 FRC，官方没写。
- **画质**：没有在真实视频上和 FSR 3.1、FSR 4 ML 做画质对比。
- **HDR**：RGBA16F 和 RGB10A2 格式只在测试程序里跑通，没有在 HDR 播放中实测。
- **采集与导出**：采集卡实时补帧和导出没有在 AMD 卡上实测。
- **长时间运行**：长时间稳定性未测。

## 产物

- 头文件：`deps/amf-sdk-1.5.3-20261114`
- 测试程序与脚本：`reports/claude-amf-frc-20261114/`（`amf_frc_probe.cpp`、`gui_frc.py`、`run_tests.py`、`stage.py`）
- 测试副本：`tests/claude-amf-frc-20261114/app`
