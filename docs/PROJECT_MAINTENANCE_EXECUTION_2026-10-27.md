# 项目整理执行记录（2026-10-27）
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户明确授权按 PROJECT_MAINTENANCE_PLAN_2026-10-27.md 执行 1–4 全部工作：产物清理、分支/工作树收拢、文档归档、按职责整理桥接与 QML。此授权覆盖这些明确范围内的历史“只做方案/不删除/不改产品结构”限制；Lecram继续暂停，运行组件/模型/驱动与真实配置不改，正式发行资产和对应源码保护。不推送或发布。

施工在既有E盘树，分支 `codex/project-maintenance-20261027`，基线 main `7bf02d4`。通过构建/回归后本地整合回main。单Agent、MSVC构建并发2、每个测试≤300秒；产物统一E盘。

## 续接入口

- 实际操作/恢复清单：`E:/项目/Veyra/reports/project-maintenance-execution-20261027`。
- Git bundle、脏文件原字节、退休产物证据：`E:/项目/Veyra/archives/project-maintenance-execution-20261027`。
- 当前可复用构建：`E:/项目/Veyra/build/claude-screen-recording-20261022`。
- 保留正式版本：`E:/项目/Veyra/releases` 中2.0.7及以后正式资产/源码；当前2.1.1供体先保留。

## 进度

1. 已重新检查当前main基线和进程；未发现运行中的Veyra或构建进程。
2. 已保存全部609引用、101工作树状态；6脏树516条状态原始字节ZIP逐条SHA核验；source-before.bundle已verify并覆盖全部原引用。
3. 退休产物先保全脚本/配置/日志/源码记录及应用EXE。首次准备检查发现六个旧构建内有runtime junction，拒绝按普通目录处理；已改为记录链接目标、剪枝不遍历。执行时只删除已验证的链接入口，绝不递归其目标。
4. 退休产物证据ZIP341,177,909字节，保存12,160份文件并逐SHA验证；cleanup_native.ps1复核54,827文件身份/mtime/size后删除43目录，E卷空闲增加24,123,240,448字节。此为删除阶段读数，整体净变化另计。
5. retire_native.ps1先核对bundle各tip、实际HEAD/工作树状态和ignored清单，移除89个E盘干净工作树，122个旧分支引用退休到bundle。带独有忽略内容的C盘树、6脏树、Lecram两线和Codex托管的脱离分支树保留；发行标签和远端refs不动。初次JSON解析大小写冲突在执行前失败，改为大小写敏感Hashtable后完成；没有绕过安全移除。

## 文档完成

- 611份历史文档/原规则移入history；计划、报告、发行、规则、状态及日志分类，PATH_MAP.json记录旧→新，Markdown相对链接修正。8个此前已失效的旧运行产物链接明确标注为退休路径，不伪造原件；当前及历史链接检查无缺失目标。
- 当前根AGENTS约7KiB，WORKLOG改为短日志，CURRENT_STATUS只保留当前事实；README/DEVELOPMENT/ARCHITECTURE/BRANCH_ARCHIVE提供日常入口。
- 从原文档纠偏分支保留10/20用户确认：NV显存增长已解决、AMD用户实测无问题；不扩张成本轮新增硬件验证。
- 历史guard/start和GPL对应源码/运行组件保留，桌面共用Git库不迁移。

## 代码完成与构建

- QmlPlayerBridge.cpp从9233行拆为约640行的启动/生命周期接线、原私有状态头和14个独立领域编译单元；公开头未改。硬件缓存仍单一定义，GDI+初始化仍单一实例。
- DialogHost从2277行拆为约134行路由、统一DialogLayer和各来源/设置对话框，宿主依赖显式传入；原VTextField兼容入口保留。
- ProPage从3213行降到约2144行，提取显示/音频面板、呈现行、性能球、状态与加层控件，保留原ProPage兼容类型及HDR观测可见性合同。
- 翻译提取器读取桥接子目录和私有头，2681条翻译及三种语言逐项一致；681个桥接方法体逐项SHA相同，QML文案和objectName集合一致。
- 首次编译发现共享ensureGdiplus仍在字幕匿名命名空间，已改成私有声明+唯一外部定义；未复制初始化或修改方法体。修复后产品构建52.7秒通过，随后10目标构建23.2秒通过。日志logs/codex-project-maintenance-20261027/build-*。
- 自动迁移校验发现三条objectName被宽泛host替换误改，已改为只重命名词法标识符、保护字符串和成员字段，恢复全部原名字；最终QML/结构检查重新通过。

## 实际验证

- run_checks.py共16组通过；QtQuick为117通过/0失败/1跳过；UI/翻译/修复合同、效果链、异步显示查询、预设/配置、呈现/预览/几何、采集能力与播放列表检查均通过。Playlist最初夹具漏传新目录exit2，按工具合同补齐后通过，初次JSON保留。
- ui-renderer-default.py用单一任务运行目录和私有配置完成四次真实启动，软件/GPU/兼容重启和已选SR模型保存恢复通过，无QML错误。
- 最终shell_check在软件/GPU各模式打开五页签、十对话框，完成37%NR比例预设保存→改值→恢复并核对效果仍关闭，无创建/绑定错误；截图已查看。整个流程不连接采集卡或真实串流主机。
- 软件/GPU两次私有配置文件播放各实际呈现290帧，退出0、无QML/D3D错误，配置哨兵保留。没有重复实卡采集或NR/GPU全硬件推理/性能测试，相关引擎与运行库未改。
- 当前状态和保护项最终检查、主线整合及最终空间读数见同任务FINAL.json；后续仅收尾文档，不扩大产品范围。
