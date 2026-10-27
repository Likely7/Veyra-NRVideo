
## 2026-09-12 状态面板卡片与曲线

按用户图片将默认实时状态面板改为深色圆角卡片：光流/NR/SR/FG四项最近一秒GPU均值；30秒额外延迟估计历史（250ms采样，缺失断线、不填0）；旁边总估计；底部待输出帧数和状态。详情保留旧阶段诊断。代码 apps/veyra/ui/LiveStatusDashboard.h。

FrameFlowMetrics.pendingOutputFrames 接实际呈现作业中尚未消费的有效帧机会（含待GPU完成、待截止时间的原帧/有效生成帧，不把两个batch冒充两帧），正常呈现、过期、取消均扣除；XeSS SDK内部队列不可观测，卡片星号注明不含内部队列。低于请求目标95%持续8个250ms样本判黄过载；达到阈值持续8样本恢复绿正常；failed红错误；待机/暂停/采样/调整灰。95%容差防止59.94相对60等正常抖动报警；无目标不据此推断性能。软件reported failed以外未知故障不能凭低FPS武断标红。

构建logs/dashboard-final-build.log通过；中途scope guard初始化/文本替换两次编译错误修复，保留dashboard-build.log和dashboard-build2.log。UI脚本logs/dashboard-ui.log通过（模式切换/全屏/详情），overview截图实际查看布局完整。修复待机applying残留显示后最终构建通过。repair_contract 88checks0failures，logs/dashboard-contract.log。

用户4K视频原生NR+2X实际12秒smoke退出0，294原帧、292生成、failed=false。logs/dashboard-4k.log，稳态pendingFrames=1，额外延迟估计约0.4–0.5ms，absLatenessP95=0.81ms。仅本地RTX运行验证，未做PS5/采集卡实测和人为故障红灯注入。没有发布、push或二进制入Git；桌面PS5测试版仍指向已更新EXE。

### 曲线卡片内切换精细面板

用户指定只在曲线卡片区域切换。右上小三角切换精细数据/曲线；顶部四卡、底部队列与状态固定。精细列表按卡片高度裁切完整行，滚轮仅在卡片内容区生效；返回曲线保留历史。旧全局标题点击切换已取消。

构建 logs/dashboard-inset-final-build.log 通过，UI脚本按DPI点击新位置、依次抓取overview/advanced/returned，logs/dashboard-inset-final-ui.log通过；实际查看advanced截图，顶部/底部固定且文字未溢出卡片。首次链接被运行中EXE占用，正常关闭后重建；测试脚本首轮坐标变量遗漏，补齐后重跑，上述最终结果为有效证据。未修改播放链路，不重复GPU性能测试；未发布。
