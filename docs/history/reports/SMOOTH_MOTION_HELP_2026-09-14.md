# Smooth Motion：普通版说明入口

## 最新决定

2026-09-18：用户再次明确 Smooth Motion 已确定可用，本项收尾，不再保留为待验证开发任务。旧强制互斥实验以 `checkpoint/smooth-motion-closed-20260918` 存档；现行普通版说明与允许自主叠加的策略保持。用户已验收的使用方式不扩大为所有显卡或叠加组合保证。

用户在本机实际测试后报告 Smooth Motion 有效、稳定、效果良好。这是用户对本机的验收反馈，不是所有显卡/驱动的验证，也不证明与内部 FG 叠加更好。

用户明确要求简化操作：只用 Smooth Motion 时将软件补帧倍率设为“关闭补帧”；只用 DLSS/XeSS 时去 NVIDIA App 关闭 AI 插帧；允许用户同时开启，不检测、不拦截，不由软件管理驱动配置。原强制互斥方案不再适用。

## 实现

- 从普通构建基线 `6d0ec99` 建立 `codex/smooth-motion-help`，不把受限实验构建合进普通版。受限实验版保存在 `codex/smooth-motion-experiment` 的 `27c17eb`，回退点仍可用。
- 专业模式 → 补帧页 → 补帧倍率下方新增 **Smooth Motion · 开启方法**。点击展开/收起，使用既有毛玻璃控件和滚动区域，不弹原生 MessageBox。
- 说明文字按当前宽度/DPI计算高度，展开后下移内容节奏等设置，收起后恢复。不会覆盖或隐藏原参数。
- 开启步骤明确指定当前使用的 `Veyra.exe`，而非旧实验程序。NVIDIA App 中原实验 EXE 的程序配置需要为普通 EXE 另行设置。
- 提醒软件开关不控制驱动；允许叠加但效果未验证；统计不含驱动生成部分；额外音画延迟、直播捕获需实测；截图和导出不能取得驱动生成的中间帧。
- 不改媒体链路、FG参数、音频、呈现交换链、驱动设置或运行文件。既有 DLSS/XeSS 选择与倍率保持原样。
- 更新双语 README 和 AGENTS 最新决定；原执行方案标为历史，不让后续 Agent 恢复强制互斥。

## 本地验证

实际命令（VS x64 环境，仓库根目录）：

```text
cmake --build out/remoteplay/product-repair --target veyra veyra_ui_contract_tests veyra_repair_preset_tests --parallel 4
out/remoteplay/product-repair/veyra_ui_contract_tests.exe
out/remoteplay/product-repair/veyra_repair_preset_tests.exe <out目录下独立临时预设文件>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File out/smooth-motion-help-ui.ps1
git diff --check
```

构建通过。既有 UI 合同检查通过，包含384组四种DPI布局；预设检查通过，包含42组旧/当前后端迁移。新增说明交互采用本机临时 UI 检查脚本，只操作自己启动的隐藏空载测试进程：展开前隐藏、展开后不覆盖后续内容、收起恢复位置；两个内部FG控件保持启用，XeSS显示2种倍率选项、DLSS显示4种，全部通过。程序空载运行12秒，退出0，没有打开用户媒体、采集卡或PS5。单次测试均小于30秒。

第一次预设测试未传必需的临时文件参数，退出2；补齐独立临时路径后通过，原始日志保留。这是测试调用错误，不是预设产品故障。

证据：`logs/smooth-motion-help/build.log`、`veyra_ui_contract_tests.stdout.log`、`presets-final.stdout.log`、`ui-check.txt`、`ui-interaction.log`。patched FFmpeg `avcodec-63.dll` 仍为 `0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`。

当前普通程序已本地更新到 `out/remoteplay/product-repair/veyra.exe`，版本资源沿用1.0.2。未推送GitHub、未发布新包；SDK、DLL、日志和测试脚本产物不进入源码提交。NVIDIA NR/FG Create/Evaluate、Smooth Motion 与 DLSS/XeSS 叠加均未在本轮执行；本轮只验证说明与原设置行为。

下一步：使用普通版，在 NVIDIA App 为其 EXE 开启 AI 插帧；用户可以自行对照单独/叠加效果。
