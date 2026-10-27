# 软件 UI 滚动残影、图标裁剪与性能保护

用户要求接手 Claude 刚才的 UI 修复，避免恢复 2.0.4 的高频 UI 绘制回退，随后补充超分图标滚出页面后没有遮挡。只修 OBS / RTSS 软件 UI 的绘制和裁剪；不改视频、NR、补帧、GPU 优先级、其它 Claude 分支或真实配置。本轮没有新打包/合并/推送/发布/关机。

## 接手事实与定位

- Claude 会话 fea88e5a-198c-4f0f-b9db-ac275fa983ae 最后实际定位于 2026-10-11 14:54，14:55 额度耗尽。原工作树只有 main.cpp 未完成编辑：删除无效环境变量、引入两个 Qt 私有头，尚未实现替代修复或构建。
- main 基线 5ba186a；独立分支 codex/ui-smear-handoff-20261011。E:/项目/Veyra/archives/ui-smear-handoff-20261011/start.json 保全 53 个既有工作树、496 份未提交文件、Claude 原始补丁、Git bundle 和原包 EXE。旧基线/guard 不改。
- 当前 Qt 6.8.3 的 Qt6Quick.dll 没有实现 QSG_SOFTWARE_RENDERER_FORCE_PARTIAL_UPDATES，旧日志声称关闭局部更新并不成立。
- 固定 Qt 源码显示软件 Shape RenderNode 在 clip region 为空时跳过设裁剪却继续 drawPath，软件 RenderNode 又不同于普通 Image/Glyph 对空 damage 的处理。实际生产页面已复现残留、完整重绘仍把超分图标画到固定头部，说明只扩大重绘区域不足以修正裁剪。
- 2.0.4 回退来自持续统计动画跟随 320Hz 屏幕绘制。已验 VTelemetryValue.qml 保持 33ms 有界插值；历史 GTA NR 6.964 → 6.559ms 不是本轮收益。

## 最终实施

1. 软件渲染时，VIcon 将相同 Lucide PathSvg 数据、颜色/透明度、填充或描边生成 SVG data URI，按屏幕 DPR 光栅化为缓存 Image。由 Qt 普通 Image 节点遵守父级 clip 和局部 damage；GPU 模式保留原 Shape。
2. 专业页“添加 NR 层”的虚线边框采用同样的软件 Image / GPU Shape 分支。其它页面 Shape 中 VBackdrop 已有软件矩形路径，保持原样。
3. 删除宿主无效环境变量和错误日志，记录真实绘制策略。没有 Qt DLL 修改、新依赖、QuickPrivate、新动画/绘制定时器或 update/requestUpdate。QtSvg 和 qsvg 插件已在原包内。
4. 整窗重绘原型撤回：相同 NR 短测中 CPU 从 0.197661 增到 0.325101 CPU 秒/秒，约 +64.5%，且仍存在裁剪越界。条件重绘及软件 Shape layer 原型也撤回；layer 还出现图标绘制位置异常。原型补丁/日志保留追溯，不计最终通过。

## 验证及失败判定

- 私有测试 EXE 驱动实际 Main/ProPage 的 Flickable；不把测试驱动编进生产 EXE。150% DPI，展开第一 NR 层及三个 NR 层形成真实长列表，80 次 1.25px 滚动；通过 QScreen 读取已呈现窗口像素。QQuickWindow::grabWindow 会主动绘制，不能用来证明没有残影。
- 比较滚动后与 resize 触发的完整重绘参考；另比较滚动前后固定头部，尤其检查完整重绘后超分图标是否越界。要求修复路径相关区域像素差为零、图标 Image.Ready、两条兼容路径正确。GPU 监控读数变化在独立区域记录，不当残影。
- 使用用户 GTA 4K 视频、原版 NR、内部 1080p、普通 GPU 优先级做同配置顺序短测，记录 UI 提交次数、进程 CPU 时间和 NR GPU 时间。实际读取 GPU 调度 class=2；不提高优先级掩盖回退。
- 保持 VTelemetryValue 文件 SHA。若增加持续绘制、出现可重复性能退步或仍有图标越界，则修正/撤回，不通过降低画质换取成功。
- 每测试进程 ≤300 秒、构建 ≤900 秒；产物全在 E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/ui-smear-handoff-20261011。收尾核对既有工作树、原包与运行库。实测结论和用户现场待验分开记录于同轮 REPORT。

## 固定版本依据

- [Qt 6.8.3 软件 Shape 的空裁剪处理](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quickshapes/qquickshapesoftwarerenderer.cpp)
- [Qt 6.8.3 软件 RenderNode 与普通节点的 damage 处理](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/adaptations/software/qsgsoftwarerenderablenode.cpp)
- [Qt 6.8.3 软件渲染损伤区域](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/adaptations/software/qsgabstractsoftwarerenderer.cpp)
- docs/STABILITY_EXPORT_PRIORITY_REPORT_2026-10-06.md
