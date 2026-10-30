# 英伟达低延迟：执行与验收

用户授权：完善已有Reflex接入，名称改为“英伟达低延迟”，三档“关 / 开 / 开＋增强”。本地分支`codex/nvidia-low-latency-20261028`，main基线4351bbd，存档`checkpoint/pre-nvidia-low-latency-20261028`。不合并main、不push/Release；私有配置和本地文件测试，不改驱动/SDK/运行库或真实配置。

实施合同：保留通用低排队能力；NVIDIA控件三档分别不调用、原生低延迟、原生低延迟+Boost。配置保留显式三档，旧lowQueue=true迁移为开，旧关闭维持关闭。不能用实际回退状态覆盖用户保存的选择。非NVIDIA及提供方独占呈现不伪装为Reflex已启用。

源帧Sleep在选择输入前，每个实际处理源帧最多一次；等待/过滤输入复用准备ID。模拟/提交标记围绕输入和增强提交，真实Present正常标记；生成帧使用out-of-band标记，不重复Sleep/模拟标记。直接DXGI下验证DLSS/VFG与增强档；XeSS保留自己的低延迟调度，不套第二个节拍器。返回值失败时停用原生调用并显示回退，关闭、图重建、源切换、退出清除低延迟/Boost。

直接使用NVAPI，不引入Streamline。ABI沿用NVIDIA/nvapi固定87dca625e83fd89a983e19b904e5f3a580da90d2的MIT公共接口；参考NVIDIA公开Streamline D3D12 `setLatencyMarker` 的IUnknown队列目标用法，仅作为契约参考，不接入其运行库。[Reflex入口](https://developer.nvidia.com/performance-rendering-tools/reflex)、[NVAPI](https://github.com/NVIDIA/nvapi/tree/87dca625e83fd89a983e19b904e5f3a580da90d2)、[D3D12参考](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/platforms/sl.chi/d3d12.cpp)。

验证：产品/相关目标构建；三档状态/持久化/迁移/无效值/关闭与失败回退；真实GPU文件在无补帧、DLSS及VFG下开关/增强、暂停恢复/seek/切源；原生API与生成帧标记成功、正常速度与完整退出。以软件排队/驱动接口证据报告，不宣称已测量点击到光子或采集卡固有延迟。每测试≤300秒、构建并发2/≤900秒，产物统一E盘对应nvidia-low-latency-20261028任务目录，复用既有构建与一份运行目录。

## 实际实现

- PresentationSettings新增独立三档字段，沿用通用低队列布尔字段。NVIDIA选择开/增强时启用低队列，关时两者停用；其他显卡保留旧通用低队列控件。已有lowQueue=true仅在NVIDIA上迁移为开，不自动开启Boost；非法保存值回退关。
- 全局JSON presentation.nvidiaLowLatency持久化，不混入画质预设。列表/节点共用NvidiaLowLatencyEditor。状态取实际有效设置；驱动失败及XeSS呈现所有权有明确说明，用户选择仍保存。
- NvAPI_D3D_SetSleepMode设置low=1、boost=0/1、minimumIntervalUs=0，不叠加原生限帧。System32 nvapi64.dll保持进程生命周期固定，设备关闭前撤销模式，保留旧驱动回调安全规则。
- 输入前Sleep并分配单调源ID，等待/丢弃输入复用准备ID；暂停丢弃未消费准备ID，避免把暂停时间算入下一帧准备。模拟结束/渲染开始在增强提交前，正常渲染结束与真实Present分别标记。
- 独立呈现队列NotifyOutOfBandCommandQueue=RENDER_PRESENT；复制工作用带外9/10标记，生成Present用11/12，真实Present仍4/5。使用公开SetLatencyMarker的队列IUnknown目标，每个源帧只Sleep一次，生成帧无额外Sleep/模拟编号。支持直接DXGI的DLSS、VFG和FSR；XeSS保留提供方所有权与XeLL，不强叠第二个调度器。
- 驱动调用失败立即停止原生路径，保留低队列并更新实际状态；撤销失败不能伪装已关闭。失效的旧模式/图的在途ID不再向新模式报告。

## 实测与失败记录

本轮跨至洛杉矶2026-10-29凌晨。真实GPU为本机RTX5070；输入为私有1080p30文件。没有占用采集卡，也没有进行RTX30/40持卡、真实VRR/光学延迟或长时稳定性认证。

- 产品及相关目标构建退出0，主要批次73.8/72.1秒、增量9.1/14.8/15.7秒和测试目标4.1秒，MSVC/Ninja并发2；命令为现有tmp/claude-sr-stage-20261028/build/build.py指定目标。准确命令与日志路径见WORKLOG和任务回执。
- RepairContract 293项0失败，含关时零调用、真实三档参数、6X只Sleep一次、带外标记、无效值、错误回退、撤销失败重试与旧ID隔离。故障路径为注入API单元验证，不冒充真实驱动故障实测。
- GPU：关、开、开＋增强的无补帧路径；开+DLSS2X；增强+DLSS6X/VFG2X/FSR2X；增强+NR/SR/DLSS2X；XeSS提供方回退；暂停保留Boost/关闭/seek/恢复/三档切换/resize/源替换/退出。最终结果按FINAL.json列出的实际回执判定。
- 一个DLSS2X回执：162次成功Sleep、160张真实Present、159张生成Present、956个带外标记，撤销状态0。说明Sleep跟随源帧而非所有显示提交，不代表屏幕实际显示了319张不同图像或实测了点击到光子。
- 三档实际GUI、旧true迁移、列表/节点共享、关闭保留VFG、Boost重启恢复通过；无QML错误，原配置SHA未改。截图tests/nvidia-low-latency-20261028/controls.png为真实QML控件；原生视频子窗口不在该QML抓图内。
- QtQuick117通过/0失败/1既有跳过；i18n2704条、缺失0、占位符0。状态变化额外进入UI快照比较，不改既有刷新频率。

必须保留的失败与处理：

1. 首版使用NvAPI_D3D12_SetAsyncFrameMarker，队列通知及模式启用成功，但第一次带外标记返回-1；清零内部Present字段后仍失败。两轮日志在native-dlss-1-2、native-dlss-zero-1-2和对应gpu控制台；产品当场回退，没有假报成功。最终移除该调用与多余ABI，改为NVIDIA公开D3D12实现使用的SetLatencyMarker(queue)；同样的9–12标记实际返回0，之后矩阵通过。没有忽略失败码或改SDK/驱动。
2. 首次GUI脚本向addEffect传入无效短名fg，列表三档与VFG已通过，节点夹具失败。改成现有正式键vfg-fg，用新的profile-ui-retry复验通过，原失败JSON/日志保留。
3. NR+SR+DLSS冷启动的首帧等待超过夹具原15秒界限，之后组合以正常速度真实出帧、原生模式/NR/SR/FG均通过，整体仍按失败保存。仅此新增组合的测试等待改为30秒，不改产品超时或旧guard，另目录复验；该时间包含模型初始化，不能冒充Reflex提高/降低延迟的证据。

## 交付与保护

分支本地提交、main保持4351bbd，不push/Release。复用tests/claude-sr-stage-20261028/app，以新文件替换EXE/QML，不写穿供体硬链接；97个运行组件与正式供体及其1730文件manifest复核。独立交互入口tests/nvidia-low-latency-20261028/启动英伟达低延迟测试.cmd，不使用原用户配置。

reports/logs/tests/tmp对应任务目录保留必要回执、原失败证据和脚本；收尾仅清本轮唯一数字TEMP目录中的重建缓存，实际逻辑删除量记录FINAL，不将逻辑字节冒充物理净回收。原8K NR/VFG整机卡死组合未复测，Lecram继续暂停。
