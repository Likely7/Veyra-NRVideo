# RTX 30 实验 NR 与首次启动全关

用户于 2026-09-14 授权实施。分支 `codex/rtx30-nr-safe-defaults`，开工前审计存档 `11977ab`。本轮仅本地实现与验证，不发布新版本。此前调研见 [运行组件审计](RTX30_NEURALSCREEN_AUDIT_2026-09-14.md)。

## 实现与边界

- NR 运行版本新增「RTX 30 兼容 · 实验」。原版和 RTX 40/50 社区版保留，设置枚举追加为 2，不改变已有预设含义。
- 用户提供的 `neuralscreen-v1.8.2-full/native/nvngx_dlssnr.dll` 原样复制至忽略的 `runtime_local/nvidia/nr-ampere/`。SHA256 `DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F`，165840496 bytes，310.8.0.0，签名 `HashMismatch`。未修改、重签或提交 DLL，未扩大 Release 默认运行文件白名单。
- 兼容适配独立实现，只更换所选 NR 模块自身的 GetProcAddress 导入指针，使其架构查询进入专用包装。仅当查询成功、物理 GPU 属于实际 D3D12 adapter LUID、且为 Ampere 架构时返回兼容架构值。未知句柄、其他查询、其他 GPU 均保留原结果；系统 NVAPI 函数入口不修改。RTX20 不在支持尝试范围。
- 与上游行为参考的区别：不使用全进程 NVAPI 函数指令补丁，不把未知句柄默认为显卡索引 0；使用原有图重建与 GPU drain 顺序，卸载时恢复导入指针。未复制上游 worker 实现。
- 开启该选项不会解锁 DLSS FG。NR 是否成功、性能和画质需 RTX30 用户实际测试；本机只有 RTX5070。
- 首次启动默认 NR=false、SR=false、内部 FG 倍率=1，总增强=false；底层 PlayerOptions 同步关闭。光流等参数保留默认选择，但全关状态不因这些选择自动执行增强。
- 已保存的预设、上次确认设置继续恢复，不清除个人配置。效果全关指干净首次安装；已有配置若开启 NR，升级后不会自动替用户关闭。软件也不会修改 NVIDIA App 中的 Smooth Motion。
- 便携包扫描新增禁止个人设置文件检查。后续发布仍需沿用脚本白名单、重新制作干净包；GitHub 现有 1.1.0 资产没有被替换。

## 操作方法

启动本地 `out/remoteplay/product-repair/veyra.exe`（桌面「Veyra PS5 测试版」快捷方式也指向它）。专业模式选择 NR 运行版本「RTX 30 兼容 · 实验」，再开启 NR。只选择运行版本不会自动开启效果。首次在 RTX30 上先保留实时档、关闭 SR/FG，确认 NR 单项有效后再逐项开启。

本机组件已经放好。单独拷贝 EXE 到其他电脑不会附带运行组件；未来发布需独立处理新组件的来源、manifest 与分发范围。缺少该组件时应报告加载失败并恢复原管线，不能偷偷改用其他版本。

## 验证记录

证据目录：`logs/rtx30-defaults/`，不提交本地日志或测试图。

1. `out/release-1.1.0-build.cmd`：完整增量编译通过；之后新增同进程切换测试重新构建通过。日志 `build.log`、`build-final.log`、`build-switch.log`。
2. `veyra_repair_preset_tests.exe out/rtx30-preset-test.v1`：预设与 42 组旧后端迁移通过；新增 Ampere 预设往返、48 组架构/选中句柄/查询结果策略检查通过。
3. `veyra_ui_contract_tests.exe out/rtx30-ui-clean-test`：通过，含首次全关、已保存参数恢复和损坏配置保护、四种 DPI 的布局测试。该测试参数应是独立新目录。
4. `veyra_nr_ampere_tests.exe runtime_local/nvidia/nr-ampere`：真实 NVAPI 路由/选中 GPU 查询/未修改系统入口、三次安装与卸载通过。RTX5070 架构 0x1B0，因此 rewrite=false；这不是 Ampere 真机通过证据。
5. `veyra.exe <用户4K视频> --smoke-seconds 12 --nr --nr-ampere --realtime`：本机 RTX5070 创建 NR Feature 18 成功（0x1、SEH=0），输出 162 帧，failed=false。`ampere-play.log` 包含历史追加日志，只以 2026-09-14T06:48:23Z 开始的当前会话为此次证据。最初 `--smoke-save` 未与对应截图测试开关联用，因此该次未生成截图；下项补充实际输出验证。
6. `veyra_nr_runtime_switch_tests.exe <用户4K视频> logs/rtx30-defaults/switch`：同进程先全关播放，再 Ampere → Original → Community → Ampere → 全关，16 项断言通过。四次真实 Feature 18 Create 均 0x1/SEH0，各次确认当前 revision 已生效且有实际 NR Evaluate；保存四张 3840×2160 PNG（文件有效、RGB 标准差非零）。该检查证明输出和切换，不是画质优劣评判。两次 Ampere 卸载均 restore=true，系统架构未伪装。

7. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <项目根> -BuildDirectory <项目根>/out/remoteplay/product-repair`：23/23 通过，47.26 秒。证据 `logs/delivery/e4d638da6ada4df58e208c1545a35171/result.json`。覆盖真实 NR/NVOF、原生4K、播放/暂停seek、JPEG、H.264/HEVC NVENC带音轨导出与取消。统一门禁显式增加 `--nr`，防止首次默认全关后把原 NR 回归变成旁路测试。

已构建本地 EXE SHA256：`7E3A86371544D27173195E9D6E9071B467CB6261F2E1370D2FF3AC1E69F785B5`。原有桌面 PS5 测试快捷方式直接指向此文件，不需要再次替换。旧 `Veyra.cmd` 引用的 `out/build/x64-release` 已不存在，本轮不将旧启动脚本冒充有效入口。源码扫描没有受版本控制的 DLL/LIB/PDB/EXE/ONNX；复制前后新 NR 文件哈希一致，仍为 HashMismatch。

## 待验证

唯一下一步是 RTX30 实卡验收：确认 NR Create/Evaluate、输入输出画面、耗时与显存、持续播放以及切回其他运行版本。未执行 RTX30/RTX40 真机、新版本 PS5/采集实卡、长时间压力或驱动补帧叠加测试。不宣称 30 系全型号可用、性能充足或画质与官方 DLSS 等价。
