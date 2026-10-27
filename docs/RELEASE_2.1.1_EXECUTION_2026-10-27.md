# 2.1.1最终整合、重建与发布记录

用户要求合并Claude软件绘制分支，将默认超分模型改为RTX视频超分，重新构建2.1.1并发布GitHub。主Release保留两完整运行包，应用及依赖对应源码另页；群公告只拟稿。准确资产URL/状态/服务端摘要见reports/codex-release-2.1.1-final-20261027/FINAL.json，发布前此记录不推断发布完成。

## 来源与实现

- eae5fd9正常合并claude/software-ui-default-20261026，原历史保留；仅规则和WORKLOG追加冲突，保留两边。已有菜单首次布局、AMD HDR代理/还原、显示事件查询等未回退。
- 6055656统一新默认SR为RTX Video SR高质量4，列表/节点一致；首次SR/NR关闭。已保存0/4/5等明确模型选择保留，AMD/Intel硬件初始化沿用FSR。
- 09c7fa1将同尺寸DLSS专属测试显式选择DLSS，避免依赖旧默认值；RTX Video SR保留原有同尺寸视频恢复。

## 实际构建与测试

scripts/acceptance/build-recording-handoff.py --task codex-release-2.1.1-final-20261027 --root 本E树 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.1 --jobs 2；主轮--reconfigure90.3秒，定向6.2秒、GPU相关12.2秒通过，均使用同一当前EXE。回执在logs同任务/build-*。

最终13组合同全部通过，QML117/0失败/1跳过、翻译2681/缺译占位0；NR模型夹具补齐输出目录参数后通过，旧失败回执保留。ui-renderer-default.py --task 同任务 --package-dir 本次NVIDIA stage完成四次真实启动：默认software/模型4，选择GPU后d3d12/旧模型0，兼容重启software/0，再次software/旧FSR5，均保留SR关闭。设置页截图已经查看。

menu-amd-hdr-fix.py gpu --task 同任务：AMD测试替身provider+实际D3D12图128组合（108 HDR）/debug0；ABI及HDR颜色通过。实际NVIDIA原版NR+PQ HDR20帧通过。首轮相对out目录缺失引起JXR写入0x80070003，补建本任务输出目录后13秒通过，未改产品。该AMD测试替身结果不作为AMD HIP硬件推理结果。

package-2.1.1.py stage/seal/cold --task 同任务：运行组件沿用2.1.0受控供体，NVIDIA50/AMD654行身份相同；PE2.1.1.0/依赖闭包/模型边界通过。新stage只更新本版EXE/QML/翻译/着色器/说明，SDK/配置/日志/测试媒体未入用户ZIP；应用及未变依赖源码按固定哈希提供。ZIP逐文件SHA/CRC通过，冷启动及远端发布结果另见最终索引。

## 目录与发布

工作树E:/项目/Veyra/worktrees/screen-recording-fix-20261022，源码施工分支codex/release-2.1.1-20261027。新包test-packages/codex-release-2.1.1-final-20261027，正式归档releases/release-2.1.1-final-20261027；之前重建前的2.1.1 ZIP不作为本次资产。未执行旧清理拦截操作，不删除其它任务/分支/脏文件，Lecram暂停。
