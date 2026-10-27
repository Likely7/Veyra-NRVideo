# Veyra 2.0.8 发布输入与验收

用户已经实机试用两个2.0.8测试包并明确允许发布，只改外部文件名。产品/运行组件/包内文档没有变化，本轮只整理发布文档、检查原包和远端发布，不重新构建或制造新的性能对照。

## 已绑定的输入

- 基线main3107e00；产品构建99435ae，679输入/35EXE；原包源归档80a3d7c。
- NVIDIA718608815字节 / SHA256 `e89972d234fc79167c08b2183b42347c9ace606663b9ac363e2d09a547afe0ba`，1654payload/49运行组件。
- AMD355816850字节 / SHA256 `1740d7dbc6c8307d0dd8deefb9aaafac4398ed0e40022b1e40e66a86092ecda9`，2093payload/534运行组件。
- 两包相同EXE `a4676e9a5bf08738dcc85c6c6909045f0593ebadcf857e6108904e90d4df2432`，PE2.0.8.0，内部显示2.0.8-test；原测试清单状态不改写。
- 应用源码2011文件，SHA256 `4ca39a70797c6080ef02646e1d2aec0a392346ea3594206c8809f188a64d3d99`；无新增专有SDK/runtime/模型进Git。

## 原包验证

35目标可构建。4场定向播放/回滚/半帧/33ms遥测通过；另40场中38场零失败，VFG合成首次22项异常，独立2425项复测通过，另人工延迟输入189项通过。原HDR桌面严格RGB5项阈值失败与原2.0.7基线相同。实际DLSS性能模式/VFGLow4X播放无GPU/QML错误，生成帧呈现提交1233/4768。

最终ZIP独立解压，Windows-only PATH、私有配置，GTA4K原片、NVIDIA/AMD×D3D12/software四场通过；realPresented397/403/401/404。软件界面实际标识确认，sentinel设置保留。AMD包在NV机器上运行不作为AMD增强实卡验收。

原始回执：E:/项目/Veyra/test-packages/claude-release-2.0.8-20261015/{DELIVERY.json,cold-start.json}；reports/test-2.0.8-handoff-20261015/final.json；[原测试报告](../reports/TEST_2.0.8_REPORT_2026-10-15.md)。完整失败/复测保留。

## 发布说明数据的含义

延迟部分取自Claude实际T911和屏幕捕获验证（logs/claude-latency-min-20261013/r3-xess2x-nr-102052、对应screen-xess对照）；T9112K60NV12+NR+XeSS2X回调→Present返回10→3ms主要说明提交提前，GPU约8ms仍约束显示，不能宣传总延迟降低70%。无NR屏幕30fpsXeSS2X3.8→3.0ms和周期错位922/960→0/960为轻负载对照。没有采用理论PBP延迟表或旧时间归属问题中的数据作为屏幕收益。

DLSS性能模式取自logs/claude-fg-gpu-home-20261014/lite-{off,on}-6x-*及fg-ota-4x-*，RTX5070/T9112K60NV12+NR，稳定窗口统计的P95中位数：6XGPU8.1→3.3ms、过期7.7→1.0/s；4X5.6→2.8ms。可选模式可能改变画质，不外推RTX30/40。

GUI六效果器脚本原65–80ms→当前节点4.95/专业5.05ms；移动节点仍61–64ms。窗口捕获连续缩放只在约300ms稳定后重建一次，最终初始化仍约0.5s。33ms/250ms生产刷新节奏没有增加。

PS5手柄控制由用户反馈实机正常；本机此前只有控制失败/虚拟手柄/状态流程检查。真实VRR、GC550/BMD、AMD/Intel、真实直播伴侣并行负载和跨设备远程游戏仍需对应设备验证；已完成项、模拟与待验证不混同。

## 本轮发布核对

按发布计划核对原ZIP与正式命名ZIP相同SHA、CRC、所有payload/运行组件/EXE和源码；只上传两个用户包，源归档另列。77旧树/509脏文件、五原归档、既有分支和旧Release保护。远端实际发布结果另写RELEASE_2.0.8_REPORT_2026-10-15.md与E盘reports/release-2.0.8-20261015；不能从此检查清单推断已发布。
