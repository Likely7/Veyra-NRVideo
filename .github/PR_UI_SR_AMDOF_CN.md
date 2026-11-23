# PR 2: ui — 不可用的已存超分迁移到 FSR；为补帧启用 AMD 光流

## 为什么

两个跨切面缺陷，与 Xbox 传输无关：

1. **非 NVIDIA 显卡超分灰锁。** 超分卡片可用性解析到*已保存*的算法。在 N 卡存的
   配置（DLSS SR / RTX Video SR）到 A/I 卡上会让整张超分卡变灰并禁用算法下拉，
   用户永远选不到本卡*支持*的 FSR 超分。死锁。
2. **补帧选光流会自我关闭。** `VEYRA_HAS_AMD_OF` 未定义（缺 FidelityFX SDK 1.1.4
   光流库），`AmdOpticalFlow::initialize` 返回 false，整个增强图初始化失败，
   后端恢复把补帧关掉。零运动正常只是因为它不创建光流会话。FSR 与 XeSS 补帧同受影响。

## 改了什么

- `src/ui/bridge/Hardware.cpp`：加载时若已存超分算法在本卡不可用、且跨厂商 FSR
  超分运行库在位，把 `videoSrQuality` 迁移到 FSR（镜像现有补帧后端迁移）。
- `qml/Veyra/EffectSupport.js`：`status(api,"sr")` 显式读取 `api.videoSrQuality`，
  使选 FSR 后即使可用性调用是任意 invokable 也能重算卡片开关。
- 构建：用 `/MT`（匹配 Veyra 静态 CRT）从 FidelityFX SDK 1.1.4 编译
  `ffx_opticalflow_x64`/`ffx_backend_dx12_x64` 到现有 `VEYRA_FIDELITYFX_ROOT`
  布局；`VEYRA_HAS_AMD_OF` 现已定义。无 CMake 变量/manifest 改动。
- ui-check 片段：`sr-ungray`、`sr-non-nvidia`、`fg-optical-flow`、`fg-amd-flow`。

## 范围 / 依赖

- 不改 `vcpkg.json`/`.gitignore`/`VEYRA_*_ROOT`。光流库由上游已引用的 AMD MIT SDK
  编译（`third_party_local/amd/FidelityFX-SDK`，gitignore，不提交）。
- 超分行为变化作用于所有源（文件/采集/串流），故与 Xbox 传输分开成独立 PR。

## 验证

- `sr-ungray.qml`：非 N 卡强制已存 DLSS(quality 0)，再选 FSR → `switchAvailable`
  由灰变 true，不重启；超分可开启。
- `fg-amd-flow.qml`：强制 `setOpticalFlowChoice(1)`（I 卡路径）+ FSR 补帧 + 光流
  → 真机串流 `amd-of Create result=0`、`Dispatch result=0`、`fg=1 motion=flow` 保持。
- `fg-optical-flow.qml`：N 卡 NVOF 路径同样保持开启。

## 未覆盖

- 不改超分/补帧画质预设或导出；Arc 上光流画质仍需逐机 A/B。