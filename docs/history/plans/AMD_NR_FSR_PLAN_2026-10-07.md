# AMD NR 结果被 FSR 覆盖：现场排查和修复

用户报告：1080p 文件和 XSX 1080p60 串流开关 AMD NR 均看不到变化，提供 veyra-qml(37).log。本轮只处理共享 NR → FSR 的资源交接及定向回归；不改 NR 模型/运行库、补帧算法、采集、音频或解码器。

## 已核实的现场事实

- RX 9070 XT，文件1920×1080/23.976，XSX1920×1080/60，两者均 NR → residual(source) → FSR SR(3840×2160) → XeSS FG。AMD session/enqueue、modules_ok=62、HIP=1、图重建和设置版本确认均存在。仅凭这些记录不能证明增强像素进入最终输出。
- 开关 NR 已传到 graph；文件 NR GPU 约11.1–11.47ms。FSR evaluate 非 split 顺序取 preSrColorOutput 或原始 srcRgba，没有取 NR residual。这条代码也存在于已发布2.0.5/main af5bfc3，不是本轮VFG或Blackmagic改动引入。
- 首帧/断点没有有效光流时走普通 blit，同样只在 split/独立调色时选择 SR 边界结果，会覆盖普通 NR-first 输出。
- XSX另有6次D3D12VA send_packet -22、硬解重启后回退软解并恢复帧的记录；它不解释正常本地文件同样无NR效果，本轮不凭该日志改解码器。
- 原始日志SHA256 b0e8ce013adf600fd033133e26235fc2cf195612e9f39085ed04bb3f71ac90bf。日志含私人会话数据，只留E盘私有证据，不进入源码或测试包。

## 隔离与最小改动

从已验4294341（保留VFG和Blackmagic候选改动）在E:/项目/Veyra/worktrees/amd-nr-fsr-handoff-20261007 / codex/amd-nr-fsr-handoff-20261007施工。新独立start.json和完整bundle在archives同任务目录；保护32个既有工作树/495个未提交文件和3672项原包/模型/运行库/证据。先运行amd-nr-fsr-control.py，旧guard/baseline保持。所有构建/测试/临时/候选放E盘；不派Agent、不施加竞争负载，不改用户配置/驱动，不合并main、推送或发布。

1. 扩展现有测试专用C ABI provider，加入固定非恒等GPU输出。现有identity模式、ABI失败测试保持；测试provider绝不进入产品包。
2. 修复前使用实际FSR provider和实际共享graph/shader跑像素断言，要求NR-off、SR→NR对照通过，NR→SR出现期望失败；否则不能把静态代码疑点当复现。
3. FSR evaluate统一使用已有srStageInput契约；普通缩放回退选择同一边界描述符，显式设置其读状态。保持NR-off、split节点顺序、调色/保护边界的含义，不新增每帧CPU等待或生产像素回读。
4. 修复后跑非恒等、光流首帧/稳态/断点、单/多层、不同帧率及1080p→4K，保留原identity/4K导出和ABI回归；核对D3D12 debug错误。每测试≤300秒、构建≤900秒。
5. 构建本地AMD测试候选和对应源码，沿用已发布2.0.5 AMD包运行库/模型原字节，准确记录本机RTX测试的是共享交接而非AMD/HIP推理；RX9070/XSX需要用户实机复测。

## 审查重点

不能以NR耗时、Create/Enqueue成功、源码模式匹配或identity provider当作增强效果通过证明。必须让NR输出区别于输入，并断言最终像素保留变化，同时确认NR-off和SR→NR顺序无变化。测试读取像素仅为诊断，正常播放链路不得添加GPU→CPU回读。
