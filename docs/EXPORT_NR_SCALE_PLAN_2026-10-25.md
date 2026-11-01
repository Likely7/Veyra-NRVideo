# 2026-10-25 社区NR导出尺寸补丁接入与手动百分比兼容

用户要求研究并接入所附2_EXPORT_NR_PRESET_SIZE补丁，同时兼容刚加入的25%–100%手动滑条。附件是待审查的代码/问题证据，不是额外执行授权。基线038beacd51b1274da8ff76263abc6f724c0a1942，独立codex/export-nr-scale-20261025，存档checkpoint/pre-export-nr-scale-20261025；复用当前工作树与同源构建，不覆盖桌面/其它工作树，无merge/push/Release、子Agent，Lecram暂停。

原附件逐字备份于E:/项目/Veyra/archives/export-nr-scale-20261025：contributor.patch SHA256 E4D92E28A35DFE64AE309D6B238B1061CBCDF85A1D25EA04A797C9F9F2480598，说明SHA256 35FB5FC6F5BA9ECF95CC58D5AEA08A700B9515D04E1A891956E503A8296DD04A。作者身份未提供，不虚构署名。先git apply --check，GraphDescription/EffectChain因已有百分比接点冲突，其它改动可检查接入；不盲目覆盖。记录原贡献和本地适配边界。

- 确认三处导出强制Native真实存在。采纳视频导出按每层所选固定档处理的意图；输出编码尺寸与NR内部尺寸分开，4K选1080p仍输出4K。Auto在视频任务快照固定最高1440p（AMD上限1080p），不运行预览自动池。图片仍完整处理。
- 手动策略125..200编码保持，通过原POD/预设/会话/任务快照/worker传输，不增字段或破坏协议。NVIDIA比例按该层实际输入宽高；AMD比例按既有1080p预算内输入，再乘百分比，不能把手动50%错误重置为1080p。非法策略不在freeze中被归一化成合法值，必须继续拒绝。
- 共享NR导出尺寸计算，图描述与逐层执行一致；保留离线基础/光流/输出语义和必要flowAdapt（AMD现有光流预算不得因本次修复放开）。保留列表/节点参数索引、禁用层、SR顺序与所有NR效果/FG合同，不改调度修复、运行库/模型/驱动或真实用户配置。
- 四语说明包含手动比例适用于视频导出、内部尺寸与最终输出不变、图片例外。日志逐层记录实际NR输入与策略，不能只日志宣称1080p而实际创建Native。
- CPU回归：固定档/原生/Auto/25/37/50/75/100%、8K/超宽/小图/逐层混合/禁用层/SR、AMD预算/无效值/快照与worker序列化。GPU验证要走真实视频导出路径，核对Create/Evaluate内尺寸、输出ffprobe尺寸/帧数/解码非黑；另验图片完整处理、必要分层尺寸与Flow适配。无AMD实卡/真实8K素材或资源不足如实报告，合同测试不冒充硬件通过。

所有产物E:/项目/Veyra/{archives,reports,logs,tmp,tests,test-packages}/export-nr-scale-20261025；复用build/claude-screen-recording-20261022，构建-j2<=900秒、单测<=300秒，GPU测试前查其它Veyra进程，不停止用户进程。复用单份私有app与合成短媒体，无真实配对主机/采集测试；保留必要日志和一个当前本地交付，不复制整工程。实际命令/失败/证据与结果写WORKLOG和本任务报告。
