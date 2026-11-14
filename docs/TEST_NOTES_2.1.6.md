# Veyra 2.1.6 本地完整测试包 / Local build

本包未发布到GitHub。解压至新目录，运行veyra_qml_ui.exe；不要直接覆盖正在运行的旧目录。NVIDIA与AMD包按显卡选择，配置不随包附带；应用默认效果关闭，GPU优先级默认高，VFG自动调控默认关闭，已保存的设置保留。

界面补充：专业页录制按钮旁可直接开启/关闭即时回放，原保存按钮/F9继续可用；设置中的HDR亮度曲线、预设和元数据默认折叠，展开后完整调节，折叠不改变HDR参数。The instant replay switch is now beside Record; HDR curve/preset/metadata settings start collapsed without changing saved values.

- 即时回放：设置→播放→即时回放，选择15秒至10分钟；默认关闭。开启后持续多占用一路硬件编码，在内存中保留最近的已处理画面与声音；F9或专业页保存按钮写入录制目录。与F8普通录制可同时使用，快捷键可修改，游戏模式也支持。保存从关键帧开始，可能比所选时长多一个GOP；内存不足时实际长度缩短，界面显示状态。切源/尺寸/HDR变化分段，保存当前可用片段，不跨不同尺寸拼接。
- 麦克风：设置→播放→录制中选设备；默认关闭。可混入游戏声音或保存独立AAC音轨。只开启麦克风也可录，麦克风失效会提示并留静音，不中断画面和游戏声音。独立音轨需播放器或剪辑软件选择，普通播放器可能默认只播放第一轨。
- 精修光流：图内DLSS、FSR、ANR可使用现有边缘神盾。1080p短测增加约1毫秒GPU光流开销，ANR/FSR边缘误差改善；不保证静止UI模糊改善。VFG使用其内部光流。
- 已包含之前的自动去重/60→30、送帧、全效果剔除、GPU/VFG默认值与选项样式修复。本次额外修复了回放音频突破内存上限、保存截止时间越界，并限制切源时编码线程积压。

ANR仍为本地实验，上游核心再分发许可未解决；不能把这个包当成已获许可的公开发行包。原增强运行库保持原件与独立清单。AMD/Intel显卡和其他麦克风未在本机实测，窗口模式VFG高档仍有已记录的DWM上屏限制；不承诺所有环境无卡顿。

Instant replay keeps the last 15 seconds to 10 minutes in memory (off by default). F9 saves it; F8 regular recording can run at the same time. A keyframe-aligned clip may include up to one extra GOP, or be shorter when memory-limited. Source/size/HDR changes start a new segment. Optional microphone capture supports mixing or a separate AAC track; media editors can select the second track. Replay consumes an extra hardware encoding session. This local package includes unmodified ANR experimental files with unresolved core redistribution licensing and is not a public Release. Validation was performed on this machine's RTX5070; AMD/Intel hardware and other microphones remain untested.
