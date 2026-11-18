# 2026-11-18 主线整合与项目整理

用户明确授权：把Claude最新虚拟摄像机修复与各分支修复/功能合并main，整理项目。单Agent；仅本地整合，不推送、不新建/替换Release，不关机。

1. 盘点全部本地分支/工作树/未提交文件；保留脏树原样，区分已纳入main的历史快照与新工作。原main 54aa357、远端跟踪bf596e1。4条明确暂停/未完成研究诊断线保持隔离，不恢复功能。
2. 将虚拟摄像机同树Codex/Claude72文件工作原样提交740534e，检查无运行库/SDK/模型混入，连同3条已存在文档提交快进main。开工标签checkpoint/pre-main-integration-20261118，原始工作快照checkpoint/virtual-camera-claude-snapshot-20261118。
3. 在唯一main源码树E:/项目/Veyra/worktrees/main-claude-anr-20261113重新配置构建，产物任务codex-main-integration-20261118。当前产品/两架构摄像机/组件安装器与相关CPU/GPU/QtQuick验证；实际4X动态送帧、冷热启动/并行录制回放按本次相关风险补测。每测试≤300秒，编译并发2/≤900秒，私有配置。
4. 整理当前入口、历史状态和来源修改清单；旧CURRENT_STATUS归档保留，避免继续堆多份相互矛盾“最新”。完成分支先做tip映射和验证过的bundle，再仅移除已合入、干净且无本地专用文件/连接/在用进程的工作树和分支；脏树/暂停线/共享Git与依赖保护。
5. 修复已确认硬链接误写的旧发行package-manifest，从原发行ZIP取回原字节、原子替换、保留误写版本及SHA；修正原测试脚本原地写入方法。原发行ZIP、运行组件与用户配置保持。
6. WORKLOG和MAIN_INTEGRATION_2026-11-18.md记录本次实际结果，外部reports同任务FINAL.json记录提交/构建/验证/归档/未验范围。

产物统一E:/项目/Veyra/{build,tests,logs,reports,tmp,archives}/codex-main-integration-20261118。当前观察到系统安装仍为旧协议摄像机，管理员OBS需用户自行更新组件；本轮不自动处理Windows管理员授权，不以普通权限测试替代管理员验收。
