# 2.0.4 统一预设纠偏验收

用户要求移除独立补帧预设，改为让光流·运动估算整个区块随既有列表预设保存。范围见 LIST_PRESET_FLOW_PLAN_2026-10-06.md。原2.0.4封存包与其它工作区保持。

实现：同一PresetLibrary增加可选Flow内容位、v10共享参数快照，携带光流后端/质量/AMD半分辨率/内容节奏。新库另存.flow，不覆盖v1–9文件；旧条目仍按原ChainGlobalSettings行为应用。未勾选Flow的新条目会保留当前共享值，即使同时应用效果链。默认启动不会让旧contentRate偏好覆盖所选预设；节点与导出继续复用相同存储/应用路径。独立补帧UI/API移除，旧偏好里的fgPresets原数据保留、停止消费。

生产构建build1 496步/0；build2增量3步/0；build3/build4分别补充纯预设夹具，两步/0，生产EXE未变化。EXE SHA256 d01329aa6d9fbd6ac2d1eee294782f4cb9d233490595ed2e3e1a9ea12c16b2d6，PE File/Product 2.0.4。

已执行（以下命令统一在本次worktree，均为 py -3.11 -B）：

| 命令 / 日志收据（logs/list-preset-flow-20261006） | 实际结果 |
| --- | --- |
| scripts/acceptance/list-preset-flow-build.py build1（七生产/回归目标）及build2–4 | 全部exit 0；旧运行组件不替换 |
| scripts/acceptance/list-preset-flow-tests.py preset3 preset | 482个PASS/0 FAIL；全部32内容掩码的列表/节点范围、54后端/质量/节奏组合、非法值/截断拒绝、旧v1及实际v6/v7 sidecar不覆盖、默认/导入导出 |
| 同脚本 chain1 chain / repair1 repair / legacy1 legacy | 236 PASS / 246 PASS / 旧单用途预设测试exit0 |
| 同脚本 i18n1 i18n；scripts/i18n/extract.py --check | 全部exit0，2002词条、三种译文缺失0、占位符问题0 |
| 同脚本 qml1 qml | Qt 50 passed/0 failed/0 skipped；保留既有mock teardown警告，不冒充生产QML错误 |
| 同脚本 ui3 ui / uirestore1 ui-restore + NVIDIA候选路径 | 真实保存弹窗开关与完整摘要、mask16、列表/节点恢复、不勾选保留、生产API导出导入、播放继续、重启默认压过旧prefs；旧fgPresets原字节值保留且UI/API已移除 |
| 同脚本 hot1 hot + NVIDIA候选路径 | 真实FSR3→XeSS→DLSS→FSR3→关闭；原版NR强度5的风格1自动/风格2手动两层继续播放 |
| 同脚本 nrexport2 nr-export / nrrestore1 nr-restore + NVIDIA候选路径 | 4张实际PNG、单/双层与列表/节点参数、九控制保留；选mask17的列表预设而实时参数故意改掉，worker export-frozen仍flow=0/content=3；D3D12 NVENC HEVC 3840×2160完整60帧；跨进程会话恢复 |
| 同脚本 smokeAMD1/smokeNVIDIA1 smoke + 对应候选；smokeNative1 smoke-native | 两包单独profile启动/ASS五事件及字体附件通过，默认D3D12 UI/视频启动通过 |

所有GPU检查串行，单测试≤300秒、构建≤900秒；没有压力/竞争负载、主机账号连接、全局驱动或用户配置改动。验收截图 tests/list-preset-flow-20261006/ui3/screenshots/unified-flow-save.png 中实际开关与摘要可见。

失败没有抹掉：preset1新增节点夹具未移除旧保护节点而64项失败，修正夹具后preset2/preset3通过，旧存储语义的回归仍使用legacy mask15；ui1找不到已脱离父节点的隐藏弹窗，改为由生产DialogHost先打开后ui2通过，ui3实际点击开关并滚动复核；nrexport1脚本重用了original变量，在应用启动前AttributeError退出，修正为save_call后nrexport2通过。日志/旧临时夹具保留，失败不算产品通过。

本地main合并、源码ZIP、最终ZIP全CRC/载荷SHA和独立解压启动为下一阶段；结果以同任务archives/main-merge.json、logs/tested-inputs.json、test-packages/DELIVERY.json、logs/cold-verify-results.json为准，不用计划当完成证据。

新产物统一在 E:/项目/Veyra/{build,tests,logs,tmp,test-packages,verify,archives}/list-preset-flow-20261006；所有子进程临时目录只覆盖本进程及子进程。使用原Qt、patched FFmpeg、libass与SDK；不引入新组件或驱动。每阶段list-preset-flow-control.py核查20个其它工作区及旧包不变。

不扩大硬件通过范围：真实Xbox/RX9000 NR/RTX20–40/受影响采集卡及物理显示延迟仍按旧2.0.4验收的未测项保留。本轮只重新打包本地测试版，不公开push/Release。
