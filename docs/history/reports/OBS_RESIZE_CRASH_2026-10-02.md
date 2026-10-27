# OBS 游戏采集 + 调整窗口大小崩溃（2026-10-02）

状态：发布阻断，未修复、未做修复后验收。用户要求先不构建，后续构建和打包暂停；图标版先前编译已完成，尚未交付。

## 本机事实

- 用户在现有 2.0.0 本地便携包使用 OBS 游戏采集，调整窗口大小后崩溃。OBS 32.1.2，RTX 5070，驱动 32.0.16.1656。
- 17:38:02 PID 36744：访问违例，nvwgf2umx.dll+0x769587。
- 17:40:38 PID 11364：访问违例，d3d11.dll+0x72888，读取空地址；故障线程 1728 是应用日志中的视频呈现线程。该线程此前刚完成 present-sink resize。
- 同时 UI 线程 18324 持续调整窗口；OBS 日志频繁出现 d3d12 capture freed、d3d12_init_11on12、shared texture capture successful，尺寸对应 QML 主窗口。
- 17:40:38 的异常上下文原始栈地址包含 graphics-hook64.dll+0x2d26/+0x2f8b/+0x5d85/+0x5fa2；这是地址扫描，不是完成符号解析和展开的调用栈。最初旧工具扫描的是转储生成线程上下文，后续已改读 ExceptionStream 中的异常上下文，保留两次输出供追溯。
- 进程包含 OBS graphics-hook64.dll、NVIDIA nvspcap64.dll；不能排除多钩子共存因素。无证据把故障归因于新图标：用户运行的仍是旧图标原包。

## 源码对照及假设

对照 OBS 32.1.2 原版 dxgi-capture.cpp：static dxgi_swap_data 为进程共享状态；hook_resize_buffers 无条件清空捕获状态并调用 data.free，未按当前捕获的 swap 做过滤；hook_present 也访问该状态。d3d12-capture.cpp 的 static d3d12_data 持有 D3D11On12 设备/上下文，free 释放后归零。Veyra 的 QML UI 与视频在不同线程使用独立交换链。

因此首要假设是 UI resize 与视频 Present 同时经过 OBS 共享采集状态，引起生命周期冲突。与现场日志及异常上下文一致，但尚未通过带符号栈/隔离复现确认，不宣称 OBS 是唯一责任方。

参考：https://github.com/obsproject/obs-studio/blob/32.1.2/plugins/win-capture/graphics-hook/dxgi-capture.cpp
参考：https://github.com/obsproject/obs-studio/blob/32.1.2/plugins/win-capture/graphics-hook/d3d12-capture.cpp

## 后续验证要求

- 保留用户 OBS 场景和设置，使用独立 Veyra 配置；对比无捕获、OBS 游戏采集、OBS Windows 图形捕获，覆盖首页/播放、连续拖动大小、最大化/还原和关闭。
- 区分 UI 与视频 Present/Resize 所在线程，补符号化异常调用栈；在用户同意相关设置变化后隔离 NVIDIA 叠加共存因素。
- 不通过吞掉访问违例、每帧等待 GPU、禁用用户拖动、盲切 D3D11 或恢复取消的 RTSS 分支冒充修复。
- 如采用串行协调，必须覆盖 Qt UI 与视频的 Present/Resize/销毁，以及补帧内部 Present；仅给 Veyra resize 加锁不足以保护第三方钩子。此方案尚未实现。
- 本轮只诊断/存证，没有改动呈现链路或运行用户应用复现；用户要求先不构建。

证据目录：E:/项目/Veyra/tests/obs-resize-20261002。原始 Windows WER 全转储留在系统原位置，未移动/删除。包中日志和小转储已复制存证。
