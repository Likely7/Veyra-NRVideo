# 即时回放开关与HDR高级区收纳

用户要求把即时回放/F9开关放到录制旁，将截图中的HDR区域折叠。基线main2dcff36，codex/replay-controls-hdr-fold-20261113，源码仍worktrees/integrate-2.1.6-20261113。只改ProPage、SettingsPage及“展开”四语译文；打包脚本仅追加本次说明文件。默认回放仍关，回放时长、录制/保存/F9和HDR参数调用保持原接口。

专业页常显“即时回放”开关，直接写现有replayEnabled，与设置页双向同步。开启后原回放时长/保存按钮显示，F9仍按现有快捷键工作。设置中HDR输出格式仍外露，截图中的曲线预设、所有曲线参数和元数据放进默认关闭的高级区；可展开/收起，不切断参数绑定、不更改已有值。

构建沿用build/codex-build-2.1.6-20261113，日志/测试/报告/临时/完整包在E:/项目/Veyra对应codex-replay-hdr-ui-20261113目录。命令scripts/acceptance/build-recording-handoff.py --task codex-replay-hdr-ui-20261113 --build-dir <原build> --root <本树> --label 2.1.6 --jobs 2 veyra_qml_ui veyra_qml_quick_tests veyra_ui_contract_tests；增量7.7秒。无引擎/着色器/运行库改动。

UI合同及Quick199/0/0、i18n2845条三语0缺失/占位错误；实际程序开关双向同步、当前回放保存、收纳/展开参数完整、HDR参数折叠前后不变、保存重启值保留通过（中灰250nit、MaxCLL1200nit作为私有夹具）。简/繁/英/日×960/1280八组header布局通过，按钮不溢出，保存按钮宽103–118px；真实截图保存在reports同任务。

夹具首轮缺qml-tests目录使Quick退出2；补齐源码tests/qml/quick后完成。另首轮在切页同一tick查控件，以及从视觉树查已分离的隐藏设置页，得到null；改为等页面切换并从明确pageItems检查，保留原engine日志，后续LIVE_PASS/RESTART_PASS。不是将空控制台或退出0当作测试通过；Quick完整results.txt和标记均独立核对。

新完整2.1.6两GPU包用固定2.1.5ZIP供体、19个原ANR文件及全部88 shader递归审核，应用/网络源码分离，ZIP逐文件和最小PATH冷启动按package-2.1.6.py --task codex-replay-hdr-ui-20261113执行；最终结果/源码提交/包SHA见reports同任务FINAL.json和test-packages同任务DELIVERY.json。旧2.1.6与用户配置保持，原启动器指向新app时保留原data-dir。不push/公开发布，不关机；不把UI改动当新增GPU性能/其他显卡或物理延迟验收。


收尾完成：产品/包源码f46df7f，EXEbf27ebed5d81a79814441671e933c7a8ef6be584c784f75b8ec80493642c10f3，PE2.1.6.0。NVIDIA 728848705字节/SHA3c11ec9ac6629ede4e62d4c5e585b3d90dee7158fb9056169796bef38734dd38；AMD 719843259字节/SHAbeef54c411b880ba1452820193b468e5a028a28e2a9af38ff71d512129da3a37。两包1671/2295文件及全部88着色器审核，ZIP逐SHA与重新解压最小PATH冷启动213/222真实帧、0错误通过（仅本机RTX5070基础播放）。旧/新启动器均指向新app且保留旧manual-NVIDIA/AMD data-dir，配置JSON逐SHA未变；仅本轮QA/解压副本按路径/reparse/进程检查后退役。完整包/源码/摘要/当前build与必要证据保留。最终main后续仅补文档，不改变交付产品；完整事实见reports/codex-replay-hdr-ui-20261113/FINAL.json，交付见test-packages同任务。未push/Release/关机。
