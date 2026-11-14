# GPU与VFG默认值调整（2026-11-13）

用户明确要求：默认GPU优先级改为“高”，VFG自动调控默认关闭。直接在已整合main的工作树 `E:/项目/Veyra/worktrees/main-claude-anr-20261113` 修改，起点325f300、检查点checkpoint/pre-gpu-vfg-defaults-20261113。

- GPU状态初值、未设置偏好时的实际请求、设置页选项与提示统一为High。显式保存的Normal/High/Realtime仍按原值读取；两个“无上限”的cap保持Realtime，未把手动Realtime强制钳制到High。只调Veyra自己的GPU进程优先级，不改系统/驱动的全局设置。
- EnhancementSettings和EnhanceGraphDesc的vfgAutoRegulate均改为false。已有明确保存的全局VFG开关仍读取，不在启动时覆盖为false。列表/节点绑定沿用同一开关。
- 提示与三语翻译同步。没有更改补帧算法、调度逻辑或运行组件。

复用独立主线build，4目标235.0秒构建通过：

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-gpu-vfg-defaults-20261113 --root E:/项目/Veyra/worktrees/main-claude-anr-20261113 --build-dir E:/项目/Veyra/build/codex-main-claude-anr-20261113 --reconfigure --label 2.1.5-main-integration-test --jobs 2 veyra_qml_ui veyra_vfg_settings_tests veyra_repair_contract_tests veyra_preset_library_tests
```

VFG设置683、合同308、预设629通过；i18n2797条、三语缺失0/占位错误0。真实应用私有配置测试三组通过：

| 情况 | 设置页与实际GPU请求/读回 | VFG自动调控 |
| --- | --- | --- |
| 全新配置，VFG2X实播 | high，requested=4 actual=4 state=Applied | false |
| 手动保存普通+开，退出重启实播 | normal，2/2/Applied | true保留 |
| 已保存实时+开，实播 | realtime，5/5/Applied | true保留 |

首轮测试从设置页直接打开文件，日志出现“video host has no valid client rectangle”，虽然后续播放与默认值检查成功，但严格错误门槛未通过。保留UI-first-from-settings.json和原日志；调整测试流程先切专业页等布局，再打开文件，三组无ERROR。不忽略错误或把exit0当通过；本轮没有顺手修改窗口几何逻辑。

新EXE已更新当前main测试入口 `E:/项目/Veyra/tests/codex-main-claude-anr-20261113/启动main整合测试.cmd`，原c343e428主线EXE独立留在archives/codex-gpu-vfg-defaults-20261113用于追溯，不复制整运行包。实际用户配置和旧Claude/ANR工作树不改，QML探针已恢复。没有重做高档VFG的窗口上屏性能对照，不能据此宣称该问题已根治。未push/Release/关机。

新程序身份及当前main提交见 `E:/项目/Veyra/reports/codex-gpu-vfg-defaults-20261113/FINAL.json`。标签仍为2.1.5-main-integration-test，不是新公开发行版。
