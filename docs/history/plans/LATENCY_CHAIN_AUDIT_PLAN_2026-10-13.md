# 可见延迟链路排查（2026-10-13）

用户要求阅读会话01a13328-397f-7621-920e-303cb1c737b0并排查4K无效果约33ms、NR+补帧约110ms。用户确认组合XeSS2X、1080p NR、60→30。先核对测量/实际日志/源码，不预设66ms一定正确，不把软件计时当光学延迟。

从main7571919开codex/latency-chain-audit-20261013，只做诊断、必要只读工具/既有测试和文档，不修改产品算法、运行库/模型/真实设置，不合并main/push/发布、不派Agent。71旧工作树及状态、main增量源码bundle已封存verify；前一更新检查分支保持独立。新产物均在E:/项目/Veyra/{archives,reports,tests,logs,tmp}/latency-chain-audit-20261013。

已读取三录像分析报告（来自指定会话，不冒称本轮再次解码15638帧）。录像22:14:29/22:16:00/22:17:25；找到cadence测试包同一时段的veyra-qml.log，哈希/筛选证据固定在archives/inputs.json与test-session-extract.log。须核对日志时间、卡/格式、NR尺寸/XeSS/VSync/半帧或内容模式，并区分回调以前、图内执行、GPU队列等待、提供方生成与实际送显。

计划：逐段汇总日志，不把每秒P95的中位数当真实全样本P95；审查XeLL sleep调用位置、源mailbox取帧时点、图/呈现队列及lease/fence、SDK自身lookahead与帧延迟设置；优先复用现有XeSS追踪/harness做正常负载验证，不做压力测试。单测试≤300秒，无播放器占用时才做实际GPU短测。只用primary官方资料核对SDK语义。最终报告明确已证事实、根因候选、待测边界和可行修复点。

排查报告已写入 `docs/LATENCY_CHAIN_AUDIT_REPORT_2026-10-13.md`：自动同步实际 sync=1、XeSS 跳过浅队列控制均已核对，SDK 独立探针初始化/启用 max=3、设1及重启读回1；四组原版实卡正常负载通过。当前320Hz不同于昨晚60Hz，未完成同场光学A/B，逐帧XeSS trace也未成功导出，不宣称修复或全部112ms精确分解。只提交文档及日志汇总脚本。
