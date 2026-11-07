# Claude两项修复合并与组合验收（2026-11-06）

用户要求合并Claude最新修复到main。开工main edd901e，存档checkpoint/pre-claude-fixes-merge-20261106。工作树仍为E:/项目/Veyra/worktrees/screen-recording-fix-20261022。

- claude/qmlcache-20261106：将原未提交7文件保存为a66f7e9，merge a625787。QML内容摘要隔离编译缓存；保留外部环境设置，不删除旧缓存。
- claude/issue88-hip-preflight-20261106：原未提交10文件保存为b15152b，merge f3b103d。AMD lmxxf建立前运行独立HIP自检，按通过的等级限制调度，全部失败拒绝开启NR。RX9060XT现场根因仍未证明为Realtime，属于崩溃前置防护，不宣称已在该卡治愈。
- CMake/main.cpp自动合并成功；唯一WORKLOG冲突按原始共同前缀保留两边完整追加内容。原两个工作树干净，无SDK/DLL/模型入Git，不恢复暂停Lecram或撤销预算/RIFE。外部NR优化只是调研，没有产品代码可合。

组合构建f3b103d，8目标86.8秒通过，标签2.1.3-integration-test，复用build/claude-screen-recording-20261022；不是公开正式包。实际命令在logs/codex-claude-merge-20261106/build-20261106-204917/receipt.json，入口build-recording-handoff.py --reconfigure --jobs 2，目标qml_ui/qml_cache_tests/amd_hip_preflight_tests/lmxxf_nr_tests/amd_nr_graph_tests/qml_quick_tests/ui_contract_tests/ui_i18n_tests。

验收：QML缓存单测通过；HIP自检本机核显/HIP6真实子进程及调度5→4→2→5、单档崩溃降级、全部崩溃拒绝、缓存和缺库路径通过；UI合同、i18n/提取通过；完整软件Quick173/0/0。AMD合成图128/0（HDR108、D3D12调试层0错误）、lmxxf ABI66/0，不是RDNA4推理。

合并主程序实际--amd-hip-preflight：正常exit0/done ok，受控stream崩溃exit0xC0000005，未启动Qt界面。实际普通程序使用隔离配置、关闭所有效果且不打开视频，开启QML编译缓存进行真实下拉点击，首次和再次均为RTX标准2/高码率17/流媒体21，DLSS0、FSR5；无engine/QML错误。再次启动的ProPage缓存mtime与SHA完全不变，证实复用。私有Main探针已恢复源码原字节。命令check.py、amd-regression.py gpu --task codex-claude-merge-20261106、ui_runner.py配menu-real.qml；日志和JSON在reports/logs同任务。

本轮首个缓存单测调用漏传必需的新scratch目录，exit2 usage，已补参数单独复测通过，保留CHECKS.json首败与QML-CACHE-CORRECTED.json，不重跑已通过无关检查。未碰用户运行进程/配置，未发布新包、未push；当前GitHub仍2.1.3。现有测试副本保留作本轮可用组合构建，不声称删除或回收。真实RX9060XT/HIP7推理仍需现场验收。

此前Codex私有UI测试设置QML_DISABLE_DISK_CACHE=1，验证了ID映射却漏掉升级缓存条件。Claude在保留缓存的真实跨版路径复现了旧ProPage执行Number(id)并产生0/0/0。今后界面升级验收至少包含开启缓存的首次/再次启动及同路径跨版升级，不以禁用缓存的孤立通过替代。
