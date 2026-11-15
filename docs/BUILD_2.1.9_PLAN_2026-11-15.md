# 2.1.9 本地整合与验收

用户当前明确授权：全效果剔除显示红框、选中拖移；整理Claude与Codex改动合入本地main；启动音乐独立于动画播放一次，发起媒体打开或手动关闭立即停止；BGM只进用户运行包，不进Git或源码包；跳到2.1.9并测试。没有本轮push/Release/关机授权。

开工前各工作树HEAD、改动路径/SHA、脏文件补丁与未跟踪源码备份见E:/项目/Veyra/reports/codex-build-2.1.9-20261115/BEFORE.json及archives同任务；原桌面脏树不改。main原2f0e499，checkpoint/pre-integrate-2.1.9-20261115；隔离codex/integrate-2.1.9-20261115施工。

整合：Claude日落界面/启动动画原样快照81498ed；AMF/FSR4 b31d536（含f1cd59c）；Codex录音10e502e；浏览器捕获挂起61c9f9c；远端README/教程c327d45。WORKLOG保留双方，Preferences同时保留监听与音乐开关。暂停Lecram/未完成驱动诊断不恢复，旧已合分支不重复导入脏快照。

区域编辑显示层独立于交换链，不进入录制/导出。按同一PreviewView坐标变换处理旋转/缩放/黑边，拖动只预览，释放提交一次，源切换/节点删除取消。默认隐藏，显示开关和效果启用互相独立。

BGM原件1847011字节，SHA256 3005aa601f06a97d3968fd9fa171e870fceb4ec47586f1e950c9ed3c7875fb6a，用户授权仅运行包；受控副本deps/startup-music-20261115/startup-music.mp3。软件包media路径，独立清单说明用户提供并确认分发；不冒称第三方许可证审计。

构建≤2并发/每次≤900秒；测试每次≤300秒。目标：CPU合同/坐标/计数、QtQuick、实际启动音乐完整一次/取消/禁用/缺文件、区域选择移动及导出无红框、相关GPU回归、两包完整文件SHA/ZIP/最小PATH冷启动。其他显卡实卡和屏幕端到端延迟不以本机结果替代。全部产物E:/项目/Veyra下同任务目录。最终以实际回执、WORKLOG和交付记录为准。
