# 2.0.4 分支整合、用户测试与正式发布准备

用户要求：检查其他分支和 Claude 新修复，与当前修复合并 main，打包 2.0.4 给用户测试，准备正式发布。此次执行到本地 main 与完整测试包；公开上传等用户实际测试结论。

## 起点、隔离与保存

- main 起点 `de18fc4f71843049dc7fd691898598c17af308f0`，已包含性能优化 `2dbf26c` 与 Claude 现场修复 `6e9d827`。
- 当前 Xbox/采集修复 tip `81b755bf012fe7e0af55fb42e7ea1c3e8ed31ed3`，包含 NR 强度/画面调控 `9eb3b1c`、三风格续修 `33d6685`。新分支 `codex/release-2.0.4-20261005` 从该 tip 创建。
- 工作树 `E:/项目/Veyra/worktrees/release-2.0.4-20261005`；所有产物按用途在 E:/项目/Veyra 的同名 build/tests/logs/tmp/test-packages/verify/archives 目录。
- 全引用 `source-before.bundle` 已 verify，SHA256 `41787718148cee716ab790a4d12386a61409547eb260b60dc0d893f33491bf85`；不可变 start SHA256 `d7e95a9341a90b1550f5764246fe18376d01785727b383bc53a5038135f007f8`，在 archives 同任务目录。
- 存档 20 个既有 worktree 的 HEAD、status、index/working binary patch、全部未提交/未跟踪文件原字节及哈希。main 之外的 19 个 checkout 保持原样；新 guard 每阶段复核。

## 其他分支和 Claude 改动

已读取最新 Claude 会话 `d79d2036-17a9-402a-8925-f9b9e0335fb2` 的真实用户指令与完成记录，逐项审查 `claude/ui-fixes-20261005` 的 18 份未提交文件，复制存档原字节到本分支，不替其工作树提交或覆盖。保留用户指定的默认 GPU 实时档及系统拒绝时高档回退；旧用户已保存的档位不改变。

包括可拖拽列表面板/视频区域跟随、卡片间距和导出滚动条、调色开关窄列布局、滑条数字输入、胶囊切模式点击穿透防护、补帧页完整预设。旧 OBS 未提交改动已在 main 同等或更新实现，不重复移植。其余尚未并入的历史诊断/撤回实验不是本次产品修复；分支清单与判断记录在 BRANCH_MAP_2026-10-05.md。HDR/Dolby PR13/14继续暂缓。

## 验证与交付顺序

1. 最小整合、版本资源/CMake 2.0.4、双语说明/Release 草稿，构建生产目标与涉及的 CPU/GPU/Qt 测试。
2. 新数字输入通过真实 Qt 输入验证范围、百分比/小数、Enter/Esc/失焦；实际桥接测试补帧预设保存/应用/删除/跨进程恢复，列表宽度与播放切页；同时回归 NR 原版强度5风格1/2、计时热切换、Xbox 错误恢复/真实软硬解、特效字幕、视频导出。
3. NVIDIA/AMD 使用同一生产 EXE，继承已经核验的 streamfix1 两包组件原字节，只更新 EXE、QML、生成 shader 与文档；不复制原包后来产生的配置/日志。执行 PE 直接/delay imports、逐文件 manifest、厂商边界审计。
4. 本地 main --no-ff 合并完整整合分支，最终 source tree 与实跑构建身份一致；源码 ZIP 独立，依赖对应源码和 libass 许可证保持。无新的 SDK/DLL/模型入源码。
5. 最终包 ZIP CRC/全 payload SHA 与干净解压启动验证；保留准确大小/SHA/源码 commit、main 合并与 bundle 回执。用户先测试现场，再正式发布。

每测试进程≤300秒，构建≤900秒，GPU工作串行且无压力/竞争负载。真实 RX9070XT+Xbox、RX9000 NR、20/30/40系列、用户采集卡/系统、物理显示节奏和主观画质未新增实机验证，不用本机5070结果替代。
