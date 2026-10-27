# 2d：空闲预热实测与保留

前存档 `034c2d8` / `checkpoint/perf-nr-2d-before-20261004`。保留待最终R0；不将首次打开收益当成稳态NR提速。设备、command ring、Core、完整Graph由一个PreviewGpuSession持有，所有创建、接管与释放在原Engine dispatcher串行执行。首页空闲2秒后预建一次，用户期间打开视频进入原任务队列等待，不创建第二个设备/snippet/IAT owner。预热只创建Feature/纹理，Evaluate计数必须为0；接管后首帧完整reset。

范围：NVIDIA、普通视频文件、单NR和/或现有DLSS/RTX Video SR、无FG/TrueHDR/颜色额外节点/自定义节点顺序；AMD、多NR、图片、采集和串流不复用。严格key包含完整设置、实际源/内部/输出尺寸及颜色格式、runtime路径/DLL身份元数据；revision归零允许来源打开产生新revision。配置不匹配释放预建Feature，复用已健康Core后正常创建。采用已保存的普通视频源尺寸，无记录使用1920×1080；尺寸上下限64、3840×2160。WDDM测得占用且预算满足才保留，设备失效/设置关闭/取消/退出均释放。预热关闭时不创建增强设备或Feature；Qt界面自身GPU占用不在这一结论中。

## 实际界面A/B/B-off

RTX5070/616.56，同M1、Lecram F95运行库、1280×800、静音、无其它测试GPU负载。全新进程各等8秒，公开open请求的墙钟时间到第一笔成功`[submit]`的软件Present日志；这是成功调用而不是屏幕扫描/端到端显示。每组3次，表中中位数（范围），ms：

| 设置 | 原封存A | B预热 | 同EXE B-off | 相对B-off |
|---|---:|---:|---:|---:|
| 单NR | 1963（1918–1990） | 100（98–110） | 1942（1926–2085） | -94.85% |
| NR+SR到4K | 2000（1998–2029） | 107（104–107） | 2053（2049–2066） | -94.79% |
| SR到4K | 1698（1696–1698） | 92（88–92） | 1733（1727–1740） | -94.69% |

27/27通过，各预热组ready/adopt/CoreInit均1；A/off均无预热、正常CoreInit1。预建工作依然需约2–3秒，只移到首页空闲时，不声称消灭了模型初始化成本。

## 完整像素与生命周期

原生使用实际产品PreviewGpuSession、M1前三个自然帧、每组3次off/on，54张完整RGBA8输出；fresh A-A噪声0，预热差异0，单NR与NR+SR第三帧均匹配此前封存B2a-v2的对应三个完整输出。预建NR/SR Evaluate计数0，播放各3次应有Evaluate，无D3D12 debug错误/设备移除，Core正常关闭。仅图接管CPU时间三轮中位NR0.0596ms、NR+SR0.0988ms、SR0.0652ms；首帧GPU完成18.7769/26.4205/19.8392ms，不含Present，不能与上表混用。

5个独立真实Qt生命周期检查通过：预热进行中打开（1162ms、单Core）、进行中退出（join后释放）、进行中关闭开关（discard后正常冷建、1647ms）、改变NR720内部尺寸（key miss、同Core重新创建、608ms）、全部效果关闭（ready/adopt/CoreInit0）。首次进行中打开不保证100ms；配置不匹配和显存不足回退正常路径。

build-prewarm-v1 475步失败于误把activeRuntime的EffectChain当成含chain包装，修正实际类型；v2 13步exit0。build-prewarm-native-v1 2步exit0。UI v1用baseline属性撞Qt Item FINAL anchor，未打开来源，保留失败样本而不计入；改referenceBuild后v2有效。新增性能设置/能力灰态/偏好持久化，四语言1947项extract检查无缺失/占位问题。导出、全产品R0仍待，其他硬件不推断通过。

原始证据统一在`E:/项目/Veyra/logs/perf-nr-20261004/`：`A2d-ui-v1-*`、`B2d-ui-v2-*`、`B2d-ui-off-v1-*`、`B2d-ui-lifecycle-v1-*`及各summary；`B2d-native-v1-*`/summary含完整原始像素、SHA、CSV、命令、EXE/runtime/source身份和GPU环境；失败构建/loader日志原样保留。所有进程有上限，未操作用户桌面、驱动或原配置。
