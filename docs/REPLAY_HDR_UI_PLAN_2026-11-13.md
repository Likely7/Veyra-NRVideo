# 回放开关与HDR设置收纳

用户要求：即时回放/F9开关放到录制按钮旁边，设置中截图的HDR亮度曲线/预设/元数据默认折叠。基线main2dcff36，分支codex/replay-controls-hdr-fold-20261113；复用当前干净源码路径和2.1.6构建，原脏树/依赖/用户配置保持。

仅QML与所需译文：专业页常显回放开关写现有replayEnabled，原保存按钮/F9继续生效；设置里回放开关仍可用且相互同步。HDR高级区默认折叠，点击展开/收起，不改变现存HDR参数与输出格式设置。检查窄窗口/长文本、设置同步/重启及HDR值保留；构建主程序、UI合同/Quick及实际程序验证，完整2.1.6两GPU包/源码更新到新任务目录后ZIP与冷启动核验。无push/公开发布/关机。

新产物E:/项目/Veyra/{reports,tests,logs,tmp,archives,test-packages}/codex-replay-hdr-ui-20261113；build沿用build/codex-build-2.1.6-20261113，构建并发2/≤900s，单测试≤300s。实测及最终入口写WORKLOG和本次执行记录。
