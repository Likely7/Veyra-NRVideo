# VFG 专项优化（2026-10-07）

用户明确“尝试优化VFG，其他的补帧暂时不动”。从已封存排查 aec800e 创建 codex/vfg-optimization-20261007，工作树 E:/项目/Veyra/worktrees/vfg-optimization-20261007。上一轮25组原始日志、正式2.0.5包、main、桌面与其他工作树保持。仅VFG所需实现及其定向测试/诊断/构建/候选交付获本轮授权；不改DLSS/XeSS/FSR实现或它们的默认行为，不改NR算法、显示/驱动/用户配置，无新merge/push/Release/关机、子Agent或压力/竞争负载。

1. P0：独立不可变start基线、源码checkpoint，保护其它工作树/运行库及非VFG后端；先做可关闭的VFG各API CPU计时，用同一原GTA6片段复核阻塞调用。
2. P1：对照SDK固定样例，尝试仅必要的VFG参数/输入绑定；必须比较真实FPS、GPU成本、已就绪帧过期与像素结果，不将猜测当收益。
3. P2：如果SDK调用仍阻塞送显，VFG专用有界异步提交或安全的分段推进；保持已存在的独立呈现GPU队列、批次租约/PTS/源帧完整、producer/consumer fence、失败signal、seek/reset/退出顺序。不能增加无界队列、放宽过期阈值或放慢时钟来伪造倍率。
4. P3：需要时以VFG专用策略修正完整成本过期后的重试尖峰与输出达成状态；共享逻辑中的改动必须显式只作用VFG。其他后端的原策略/默认/文件逐字节保留。
5. P4：匹配原片4K30/原版单层1080NR/强度1/零运动/调控关/GPU普通/Auto同步/限帧关，先测Medium3/4与High2，再按效果扩大档位。GUI正常负载串行，每进程≤300秒、构建≤900秒；验证8/10-bit所有质量/倍率、切镜/reset、关闭/seek/倍率与后端切换、完整导出计数与异常回退。只承诺本机实测，不预先承诺高档8X实时。

产物统一E:/项目/Veyra/{archives,build,logs,tests,tmp,test-packages,verify}/vfg-optimization-20261007。先执行scripts/acceptance/vfg-opt-control.py，旧guard/start基线不改。各节点commit/checkpoint，负优化回退并留证据；最终提供有实测收益的本地候选与报告。源代码/运行组件严格分开，DLL/模型保持原字节。

实际收尾：P0/P1/P2/P4完成，核心ddf71b1、最终candidate-build-v3。P1仅减少重复绑定，83fps未证明收益；P2才消除了送显线程阻塞。相邻Medium4原80→120fps（+50%），同EXE同步控制76.5fps；High2原35.5→60fps（+69.0%）。21档正常播放矩阵、913项GPU合同/168张同步异步相同像素、331项设置、GUI生命周期和24导出用例通过，完整表与限制见VFG_OPTIMIZATION_REPORT_2026-10-07.md。

P3未实施：中档6X以上/高档3X以上仍未全部达标，但本轮主因已修正；保留共享准入/恢复策略，不能为追求数字改其它补帧。当前仅本地独立NVIDIA测试候选，无merge/push/Release；全片长稳、其它硬件及NV显存问题不由短测视为通过。包内冷启及最终保护检查以verify/vfg-optimization-20261007/candidate-cold.json、final-check.json为实际回执。
