# 补帧选项文案与颜色（2026-11-13）

用户明确要求VFG名称后加👑、金色和“顶级补帧质量”，ANR红色并标“极差质量”。这两条是用户指定的界面评价文案，本轮没有新增画质对比结论。

## 实现

- `FrameGeneration.cpp`同一选项源同时供列表与节点：`NVIDIA VFG 👑（顶级补帧质量） · RTX 30（实验）/ 40 / 50`和`ANR FG（极差质量） · 本地实验`，保留硬件与实验说明、原后端ID、能力限制和选择顺序。
- VFG选项增加gold样式标志，ANR复用danger。VMenu与VSelect使用现有Theme.warn金色#F5C84B、Theme.err红色#FF5D5D；菜单与收起后的选中项一致。其余选项保持原色，长文案沿用菜单换行及选中项省略/提示。
- 同步繁中、英文、日文译文；没有修改处理图、补帧算法、运行库或保存格式。

## 本次验证

源码仍为 `E:/项目/Veyra/worktrees/main-claude-anr-20261113` 的本地main，起点ccbf9d5。复用build/codex-main-claude-anr-20261113。

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-fg-quality-labels-20261113 --root E:/项目/Veyra/worktrees/main-claude-anr-20261113 --build-dir E:/项目/Veyra/build/codex-main-claude-anr-20261113 --label 2.1.5-main-integration-test --jobs 2 veyra_qml_ui veyra_ui_contract_tests
py -3.11 -X utf8 -B -I scripts/i18n/extract.py --check
py -3.11 -X utf8 -B -I E:/项目/Veyra/reports/codex-fg-quality-labels-20261113/stage.py
py -3.11 -X utf8 -B -I E:/项目/Veyra/reports/codex-fg-quality-labels-20261113/verify.py
```

两个目标构建12.2秒通过；UI合同通过，含384种布局/四DPI原有检查。QtQuick198通过、0失败、1跳过：旧spring raster测试要求显式输出目录，本次未设；本轮真实产品截图另存并已目视检查。翻译2797条、三语0缺失/占位问题0。真实软件渲染界面：列表VFG金色菜单/选中项、列表ANR红色、节点ANR红色菜单/选中项、节点VFG金色全部通过；6个后端完整，RX9000限制保留，菜单文字无裁切，👑正常显示。截图reports同任务/list-menu.png、node-menu.png，产品无ERROR；测试探针已移除，104个QML模块文件（含86个.qml）与源码一致。

## 本地程序与入口

用户的旧main测试实例PID12588在运行，本轮没有停止或覆盖它。新程序单独放在 `E:/项目/Veyra/tests/codex-fg-quality-labels-20261113/app`；EXE SHA256 `3b5d20a35f28e73a0fcbc2a82720590e3995f25f0da07e6c4e74c0c6fb5dad0d`。397个不变资源/运行组件用硬链接，QML/i18n/可变小文件私有复制；不复制整工程，不改原运行组件字节。

原 `tests/codex-main-claude-anr-20261113/启动main整合测试.cmd` 已改为启动新app，原manual-profile及环境隔离路径保持，未写入其设置；原启动器字节备存在reports同任务/main-launcher-before.cmd。另提供 `tests/codex-fg-quality-labels-20261113/启动补帧质量标注测试.cmd` 使用全新私有配置，已实启并按2500ms退出，无ERROR。当前已运行窗口仍是旧版，重新从入口打开后显示新文案。

所有新日志/报告/临时文件/配置均位于E:/项目/Veyra的codex-fg-quality-labels-20261113任务目录；最终结果FINAL.json。交付stage私有文件合计43,495,978字节，未重复复制大型运行组件；当前构建、交付及必要证据保留。不重测算法性能或实卡延迟，不把本轮UI检查当作质量评级证据；没有push、Release、外发消息或关机。
