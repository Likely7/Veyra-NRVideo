
## 2026-09-12 拖动进度条回弹及输出槽占用错误

用户日志03:09:30.998 frame-pool slot=1 still leased; refusing overwrite batch=1276。此前两次seek约344.93/643.709秒已完成，再播放时发生。原始日志保留logs/seek-user-original.log。证据证明资源仍被占用；不能仅凭这条日志确定唯一引用持有者。

Engine背压从只检查两个batch容量改为同时检查下一奇偶输出槽所有real/generated弱引用是否释放；推进呈现/完成观测后再取下一帧，不覆盖活跃纹理。跳转请求在背压等待中到达时立即返回外层处理seek，避免旧时间线继续取帧；无作业却长期占槽才超时报错，不把正常低帧率deadline等待当错误。未关闭原frame-pool保护，未增加每帧GPU fence阻塞。

进度条松手后原来立即用旧snapshot.position刷新，导致回弹。现在seek请求和呈现确认有序号，发出后保持最新目标，只有该请求对应的新帧实际Present后才恢复跟随；TB_ENDTRACK不重复发请求，时间文字显示目标及跳转中。暂停连续请求以最后一次为准。顺带修复有效生成帧从Pending到Valid时队列计数增加可能触发unsigned减法的问题。

验证：最终构建logs/seek-ui-final-build.log；repair_contract 88checks0failures。新增LivePresentationTests --seek-stress，实际用户4K长视频、原生NR+3X，六次前后seek（含原日志两位置）、每次后续45原帧、暂停连续32/44/61秒seek、恢复和关闭，16项通过exit0，logs/seek-stress-final.stdout.log与logs/seek-stress-final/engine.log，180秒上限内结束。此前首轮测试分支插入遗漏误入旧测试导致FAIL，logs/seek-stress.stdout.log保留；修正后seek-stress2及最终两轮均通过。UI既有切换脚本logs/seek-ui.log通过；未通过自动鼠标视频测试独立逐帧验证拖动视觉，需用户实测手感。未做新的PS5/采集卡测试、未发布或push。
