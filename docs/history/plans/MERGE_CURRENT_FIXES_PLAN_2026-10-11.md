# 当前 Codex / Claude 修复本地整合（2026-10-11）

用户明确授权将当前修复合入 main。本轮只整合与验证，不推送、打包、更新版本、发布或关机。

1. 从 main `049902b` 创建 `codex/merge-current-fixes-20261011`。`archives/merge-current-fixes-20261011/start.json` / `source-before.bundle` 保全59原工作树、504脏文件、来源tip；原树不改，合并后唯main按授权例外。原包和运行库不改，所有新产物在E:/项目/Veyra本任务子目录。
2. 合入 `codex/cadence-status-20261011` `4557ab6`，包含 `e8300d9` 的DLSS SR K/M/L接入/持久化及帧率状态/事务确认修复；再合入 `claude/vfg-mixed-20261011` `1d4c33f` 的三种VFG混合档和GPU镜头切换检测。旧六Claude修复已在main，不重复应用Claude原脏补丁。旧RTSS分支仅剩两份性能研究文档，圆刚分支仅剩已明确结束的未完成诊断工具，本轮不恢复。
3. 对真实冲突逐项适配：VFG混合档负载提示保留实际内容帧率和正常限速判定；翻译按zh键结构合并，所有两边新增词条保留；AGENTS/WORKLOG历史记录保留。默认VFG中档、DLSS K、既有33ms统计/250ms状态节奏保持，负优化不恢复。
4. 基于已验对象增量构建完整头文件依赖闭包、必要测试、MOC/翻译及新VFG shader，保留DeckLink和带zlib/PS5 slices256的FFmpeg。核对当前完整CMake配置，不冒称全量重编。每测试≤300秒，构建≤900秒。
5. 定向回归：效果/参数合同、SR和VFG持久化、两种VFG提交路径、状态策略和真实GUI、设置重建/回滚与关闭项编辑。使用GTA已有媒体，私有运行配置和缓存，不改真实用户配置。不将Claude原性能结果冒充本次实测，不将本机测试扩大到其它硬件。
6. 验证通过后冻结实测tip，再本地main --no-ff合并并核对完整树相同、来源均为祖先、main clean、其它58旧树与504脏文件保持。记录WORKLOG、报告、最终回执和源码bundle；保留来源分支。
