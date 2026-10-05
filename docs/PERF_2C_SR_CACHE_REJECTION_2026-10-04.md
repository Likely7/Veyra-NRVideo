# 2c补充：SR及NR+SR旧实例复用不保留

前存档034c2d8 / checkpoint/perf-nr-2c-sr-before-20261004。原生诊断强制允许SR/SRNR旧Graph缓存，生产RecentGraphCache eligibility仍仅单NR。与单NR同样50次开关、自然源前三帧/后续不同片段、命中后完整process(reset=true)，各模式3次off/on；每轮保存全部50完整RGBA8 SHA与8张关键原始像素。

RTX5070/616.56，Lecram F95、实际DLSS SR4K。SR与SRNR各300帧，各三次fresh控制输出序列一致，A-A噪声0；每个cache-on组21/50帧不同，三轮完全重复该反例。API、24次命中、debug0/device0/Core释放检查均通过，仍不能代替画面一致性。

SR暖创建off107.8185ms（107.4705–108.0255），on0.1461ms（0.14535–0.15515）；SRNR off408.532ms（407.961–410.7295），on0.1595ms（0.15565–0.1616）。收益大但画面不一致，拒绝扩大生产缓存。检查到SR Evaluate确实传Reset=1、jitter全0、NVOF明确disableTemporalHints；未查明模型内部状态原因，不宣称SDK缺陷已定位。预热从未求值的SR实例是另一条件，已在2d独立检查54帧一致，不能混用两种结论。

命令：nr-feature-cache.py B3a B2c-sr-native-v1 sr / B2c-srnr-native-v1 srnr；build-cache-sr-concurrent-present-v1 exit0。CSV、完整SHA、像素、环境、身份、比较JSON分别E:/项目/Veyra/logs/perf-nr-20261004/B2c-sr-native-v1-*、B2c-srnr-native-v1-*及pixel-comparison.json。诊断保留，无生产SR缓存需要回退；单NR保留范围不变。
