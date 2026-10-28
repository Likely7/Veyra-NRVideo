# 当前工作日志

完整旧日志：[截至本轮整理前的记录](history/worklog/WORKLOG_before_maintenance_2026-10-27.md)。当前状态见[CURRENT_STATUS](CURRENT_STATUS.md)，历史授权见[历史索引](history/INDEX.md)。

## 2026-10-27 屏幕捕获帧率、采集对白与后续方向（Claude）

用户要求：屏幕捕获60帧只有55（日志67）、采集对白很轻（日志68）、后续方向只调研。分支claude/capture-fps-audio-20261027从main 7b0b64b；用户Veyra运行中，未停止、未占采集卡；不push/Release。报告[CAPTURE_FPS_AUDIO_REPORT_2026-10-27](CAPTURE_FPS_AUDIO_REPORT_2026-10-27.md)，证据reports/claude-capture-fps-audio-20261027。

- 屏幕捕获6376564：WGC默认MinUpdateInterval 16ms使165Hz下60帧只收55（日志67六个目标均止于55.0，间隔皆18.18ms倍数）。改为上限间隔一半减1ms；限速容差0.5ms→四分之一间隔；每秒HDR检查改缓存factory（原中位0.43ms/最长21.8ms）。探针60.00fps源：默认52.5–55.1，修复60.2；真实程序2.1.1为53.55/53.72，修复后60.0/59.99（60/120上限）。screen_capture_tests与main同一通过集（6项HDR桌面颜色检查原已失败）。
- 采集音频75444b3：G50仅双声道USB音频，PS5多声道LPCM时中置（对白）在卡内丢失，OBS同样；强制PCM不改PS5输出。通用16-bit立体声DirectShow引脚加PcmCarrierRouter，复用AverMediaAudioCarrier解码载波内AC3/E-AC-3/DTS（PS5位流），真PCM原路径不变。单测真实AC3 5.1中置有信号、无载波字节作PCM播放；carrier 39363、vendor 65+7、passthrough 26+8+8、IEC、采集音频（静音）通过。未上实卡/PS5。
- 调研：建议去色带、独立RCAS、对白增强优先，CRT光束模拟再评估；NR沿用10-26结论；DLSS RR（310.9.1 Preset F）需游戏G-buffer，不适用于视频/采集。

构建build-recording-handoff --task claude-capture-fps-audio-20261027（首轮--reconfigure 79.4秒，其后增量）。未验证：用户165Hz实机与游戏模式、G50位流透传、无该属性的旧Windows。

## 2026-10-27 项目整理1–4

用户授权执行全部四项：清退休产物、收拢分支/工作树、文档归档、按职责整理桥接与QML并验证。分支codex/project-maintenance-20261027，main基线7bf02d4，当前正式版仍2.1.1；不push/Release，Lecram暂停。

- 保全：全部609refs、101工作树状态；6脏树516状态原字节ZIP逐SHA核验（70,537,460字节），完整Git bundle 77,589,296字节通过verify且覆盖原引用。
- 产物：43退休目录/54,827文件，保留12,160份脚本/配置/日志/源码及应用EXE；按SHA对象去重的证据ZIP341,177,909字节，复核通过。先记录并剪枝旧runtime junction，再只unlink其入口，不递归目标。
- 执行：cleanup_native.ps1核对绝对路径/父链/进程、所有文件ID/mtime/size和链接目标；删除43旧目录。E卷空闲增加24,123,240,448字节（22.47GiB），与本轮归档成本分开报告。
- Git：retire_native.ps1重新核对HEAD/状态/ignored及bundle恢复tip，安全移除89干净工作树，122分支引用归入已验证bundle。6脏树/暂停线保留；远端refs、发行标签和共同Git库不动。
- 文档：旧日历式AGENTS/CURRENT_STATUS/WORKLOG移入history，历史计划/报告/发行过程分类并重写Markdown相对链接，旧路径映射可查；历史guard/start不改。恢复用户2026-10-20的NV/AMD纠偏，不拿旧状态当待办。

本轮全部原始证据：`E:/项目/Veyra/reports/project-maintenance-execution-20261027`；恢复归档：`E:/项目/Veyra/archives/project-maintenance-execution-20261027`。

代码整理提交`d4b1034`：桥接主文件9233→640行，14个领域CPP与私有状态头；对话框2277→134行；专业页3213→2144行，独立音频/显示/性能组件。公开头和引擎/管线/采集/音频/着色器未改；681个方法体、QML文案/objectName、2681条翻译内容一致。

构建52.7秒、后续10目标23.2秒通过；16组检查通过，QtQuick117/0/1跳过。四次真实软件/GPU/兼容启动、两种渲染器下十对话框/五页签/37%预设往返通过；软件/GPU文件播放各290实际呈现帧。View首次共享GDI初始化声明缺口已修为唯一实例；自动迁移误改三条对象名被结构检查拦下并恢复，最终复验通过；Playlist初次夹具参数错误exit2保留JSON，按合同改用新目录后通过。

611历史文档/规则归档并修正相对链接；当前和历史Markdown链接目标检查无缺失。新开发入口docs/README.md、DEVELOPMENT.md、ARCHITECTURE.md；历史guard/start保持，旧运行产物链接标注退休路径。

收尾保护检查：516条脏状态对应文件原始字节全部一致，保留树HEAD/status一致，2.1.1正式两包/源码SHA一致，两个正式运行供体全文件一致，测试运行组件179文件身份保持；461发行/检查点标签和远端refs未改。E卷空闲相对开工前净增约28.14GiB，最终值以FINAL.json为准。未重复真实采集或全硬件推理/性能验收。

最后本地整合main并关闭施工分支；不push、不Release，不修改真实配置、驱动、SDK/模型原件或暂停的Lecram。

## 2026-10-27 GitHub交流群二维码更新

用户明确授权更新GitHub群码。仅发布新群7截图、README.md/README_CN.md及RELEASE_SUPPORT中的图片地址与有效期，2.1.1 Release正文同步替换为2026-11-03前有效；赞助图、两图width220、其他发布说明与正式资产保持。README_EN.md为指向英文首页的入口，无需改动。

远端main原9520946，二维码提交a983c18；连接器上传后逐SHA/PNG解码发现图片损坏，改用原文件字节通过Git修复提交26480b2，最终远端原图77616字节、SHA256 AEDA3DF086F9C0983055A4F73EB2BD9F422C03E5C1910AF603A6ED0E79F2BDF9，与用户截图完全一致。两次远端提交均基于远端main，只含二维码范围，未推送本地项目整理提交。

GitHub连接器用于文件树/提交/引用更新，缺少Release编辑接口时用gh release edit --notes-file；随后合并远端二维码改动回本地main。verify.py核对远端三文档、原图字节与PNG解码、赞助图访问、两图宽度、Release正文仅群码/日期变化，全部通过；不涉及产品代码，无需重建。证据、原正文与最终回执：E:/项目/Veyra/reports/github-group-qr-20261027/。真实配置、依赖和正式包未改。
