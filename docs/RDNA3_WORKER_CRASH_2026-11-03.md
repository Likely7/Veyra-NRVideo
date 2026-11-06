# RX7900 XTX NR：架构通过后的worker退出

用户确认首个NVAPI更新后仍不可用，并提供veyra-qml(4).log与cache(1).zip。继续在codex/rdna3-nvapi-luid-20261103（已提交基线b27c0df）排查，不合main、不push/Release。此次先补足崩溃证据，不能把诊断更新说成第二处崩溃已修复。

## 新证据

- 新主日志UTC12:35:36.490、12:35:51.074、12:36:33.615均为`RDNA3 worker status pipe failed`，不再是之前的CreateFeature明确返回0xBAD00001。管道断开只说明子进程通信中止，不给出异常码或退出点。
- 新NGX日志本地时间20:36:30明确`m_gpuArch=0x1b0`，目标LUID0x100e2；NR进入CreateFeature、列出两种模型，选中Small：`hnet-crazy-cuckoo-small-fp8`，1920×1080。最后记录权重heap请求113907200字节（108.6MiB）。架构识别补丁确实生效。
- 对应worker-14168/13764/13260只记录三行基础信息，最后仍有24351MiB显存空闲。日志没有证明是哪次分配、CUDA调用或翻译失败；不能仅凭最后heap日志认定OOM。gate日志只见原生内核登记，也没有异常码。

当前结论：初始设备身份缺口已解决，新的阻塞发生在Small网络创建阶段、权重heap请求附近。仅凭这两份新日志，尚不能确定具体崩溃指令或原因。

## 有限诊断改动

只更新独立worker：记录初始化阶段及请求模型/设备参数；把CRT和Windows标准错误/输出句柄都重定向到独立stderr日志（原CREATE_NO_WINDOW进程会丢失Rust/CUDA异常输出）；未处理SEH异常记录异常码、地址、访问地址和最后阶段，仍交原Windows异常机制处理，不假装从损坏上下文恢复。某些fail-fast会绕过普通异常过滤器，因此同时提供只读收集Windows Application 1000/1001中匹配worker的事件。

不改变模型选择、CUDA参数、共享纹理/fence、调度、颜色或原失败回退。主EXE不改，保留已验证身份映射DLL；只在worker初始化打印阶段，不加逐帧日志。无自动上传、无注册表/驱动修改，不下载模型，不尝试以切换模型/分辨率掩盖未知崩溃。

新增scripts/diagnostics/collect-rdna3-nr.ps1：只复制app日志/worker日志与组件身份清单，读取近3小时、最多20条匹配worker的崩溃事件；不复制ComputeCache数据库、模型或配置。默认输出软件logs下ZIP。-AppRoot/-OutputRoot允许在E盘夹具中验证，-SkipWindowsEvents用于明确的离线夹具，不把跳过说成真实事件已验。

## 实际验证与本地交付

构建入口build-recording-handoff.py，task codex-rdna3-crash-diagnostics-20261103，既有build-dir，reconfigure、label2.1.2-test、jobs2，目标worker/test-worker/diagnostics-tests/rdna3-contract-tests；23.1秒退出0，回执logs/codex-rdna3-crash-diagnostics-20261103/build-20261103-044618。

CPU诊断夹具14项通过：无控制台子进程正常退出和故意SEH退出的阶段、CRT/原生stderr、异常记录与原退出码保持；不吞掉异常。既有跨进程D3D12合同54项通过，包含颜色、双层并存和死worker自有等待解除；是协议夹具，不是AMD NR推理。回执reports/rdna3-crash-diagnostics-20261103/tests.json。

最终增量构建4.2秒通过（build-20261103-045220），加Rust panic backtrace默认值和更细的共享fence/预热阶段。最终CPU14项与跨进程GPU54项再次通过，见reports/rdna3-crash-diagnostics-20261103/final/tests.json。中间重复跑GPU合同因复用已有证据目录被程序以exit2拒绝，不是产品崩溃；改每轮独立目录后通过，tests-repeat-refused.json保留。

收集脚本在PowerShell5.1私有夹具验证：CRT/原生stderr都进入ZIP，真实新worker哈希与Python独立SHA256一致，包中不含数据库/可执行组件，Windows事件查询实际执行（不是Skip分支）。起初Get-FileHash在子进程脚本环境不可解析，改为.NET流式SHA256以消除该命令依赖后通过；保留失败日志collector-final.log和成功collector-test.json。输出里没有匹配worker事件只代表本机这次未找到该事件，不冒充用户那台机器的崩溃已定位。

最终ZIP、文件身份与源码证据写FINAL.json。本地诊断更新只带新worker、保留的身份修复DLL、收集工具和对应源码/许可，放test-packages/codex-rdna3-crash-diagnostics-20261103；不是NR已跑通的交付。先收集用户刚才三次失败留下的Windows事件即可；证据若仍不足再用新worker开启一次NR，不反复变更设置。原正式包、工作app及运行组件原件不覆盖。
