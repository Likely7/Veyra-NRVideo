# NVIDIA 显存现场诊断包交付

用户要求给真实显存增长用户使用的诊断包。本轮已完成工具和本机验证，**没有定位或修复现场泄漏根因**。不合并 main、不 push、不发布 Release、不代发文件、不关机，不修改运行库/模型/驱动或原配置。

工作树 E:/项目/Veyra/worktrees/nv-vram-field-20261008，分支 codex/nv-vram-field-20261008，起点 dbfceabee804cd81bac4a4781f0b7b4cf272902e；原产品缓存 71723e0。reports/nv-vram-field-20261008/pre-document-state.json 独立保护 36 原工作树/495 脏文件，audit-state.py check 通过。没有修改旧 guard/start。产品 src/include/QML/shader/CMake 文件无差异，诊断只在构建生成的 3 个覆盖对象加入观察。

## 交付文件

`E:/项目/Veyra/test-packages/nv-vram-field-20261008/Veyra-2.0.6-NVIDIA-VRAM-diagnostic.zip`

78,606,236 bytes；SHA256 `92a2a807202489a023a884c9fab998bd1f6d177e056e3b0ae7c04c06d832775d`。17 个条目，ZIP CRC 与逐文件 SHA 全通过。诊断 EXE SHA256 `7519d7e864cfcf7cff9ef27ecff2a584d2a685175bc3cc5c6a18ea68e0c9f04b`。包含 6 个运行文件（EXE/启动两件/摘要/恢复打包两件）、使用说明、manifest、许可证/notice、原产品对应源码 ZIP 与诊断补充源码。不含 DLL、模型、SDK、PDB、测试媒体、配置或日志，使用用户现有解压后的 2.0.6 NVIDIA 运行目录。原对应源码 ZIP SHA256 `44d51be86fa6b7ffd2083663c23a77bbe74fbc9d048fe7932600425e23db23fa`，内层 CRC 与禁止二进制后缀扫描通过。

启动脚本创建独立配置副本及日志目录，只复制普通设置白名单，不复制凭据。默认持续同 PID 采集直到用户正常退出；不会每 280 秒重启、不会自动开关效果/清缓存/加压。Windows PowerShell 5.1 可用，不需要管理员。退出后只封装明确命名的 14 类诊断文件；private-profile/tmp/视频/截图/dmp 不递归加入 ZIP。日志仍可能含路径/设备标识，不能声称完全匿名。另提供重新打包已有记录入口，原记录不覆盖。

## 观察范围

- 每 2 秒在播放器进程内部查询 node0 LOCAL/NON_LOCAL，原字节/预算/预约量/HRESULT、LUID、context 代次、完成 fence。context 关闭前清除 sampler 的 fence 裸指针，采样锁与注销互斥；不持有设备/资源引用、不等待 GPU，只保留 DXGI adapter 引用以继续停止后的进程统计。多个 context 的 DXGI 结果仍是该适配器上进程统计，不能相加当成独立设备私有资源。
- 只将 EnhanceGraph.cpp 的 makeTexture/makeUploadBuffer 调用和 NrInstance.cpp 的 makeTexture 调用替换为原 helper 加观察 token。原分配/状态/返回结果不变；SetPrivateDataInterface 下的 IUnknown token 在资源最终 COM Release 时退出计数。观察器不 AddRef 纹理/设备，不替换 SDK/vtable，不做逐帧像素回读。借用和重复资源不重复计数；分配大小是 GetResourceAllocationInfo 估计，未知/附加失败有计数，不能冒充总物理驻留。
- 创建、最终销毁、图/NR close 入口、存活资源 ID/年龄记录；释放事件队列最多4096项，溢出计数进入存活快照。窗口几何、DPI、显示器像素/刷新率只读记录，不读窗口标题。coversMonitor 不证明独立翻转/MPO。
- 每 5 秒外部 WDDM GPUProcessMemory 保留每实例 Dedicated/Shared/Committed、进程 Private/工作集/句柄、全部当前模块；首次出现记录路径/版本/大小/磁盘 SHA，最后复核文件是否改变。模块加载不等于启用，磁盘 SHA 不等于内存映像校验。按 PID/EXE/启动时间验证身份，失败/缺项不伪填0。
- 原引擎日志提供实际选项/图重建/NR/NVOF/Core 生命周期/错误。已有显存看门狗保持；若触发自动恢复必须分段，不能掩去。摘要保留样本数/跨度/峰值/净增及连续标记段；首尾包含转换，不能报稳态泄漏率。

Qt、decoder、采集源、其它 passes、SDK 内部分配没有完整覆盖。不通过减去已知纹理估计来命名驱动泄漏，也不把这次本机资源销毁当成所有受影响显卡已修好。没有引入 ETW 或提升权限。

## 实际验证

1. `py -3.11 -B scripts/diagnostics/vram-field/build-diagnostic.py`：MSVC 编译 D3D12DeviceContext / EnhanceGraph / NrInstance 三份诊断对象、重链接 GUI，均 exit0。原缓存对象/静态库/EXE 和源产品三文件前后 SHA 不变，完整命令和收据在 logs/nv-vram-field-20261008/build。不是重新构建全部产品。编译没有开启调试层/GBV，也没有修改产品优先级。
2. `py -3.11 -B E:/项目/Veyra/reports/nv-vram-field-20261008/run-field.py final-exe-nr-stop-powershell51`：唯一共享 app 目录使用 1614 文件母包的只读硬链接，独立 profile/log/tmp，不为每轮复制 DLL。RTX5070 / 616.56，GTA 4K 原视频、NVIDIA 原版 NR 内部1080、NVOF、普通优先级。测试脚本在自有 Main.qml 副本定时关闭 FG、启用 NR+flow、全屏、关闭 NR、停止播放，最后恢复 QML；产品母包完整 manifest 前后通过。
3. 最终用例整个 runner 97.875 秒 exit0，玩家定时90秒退出；实际 DXGI 90.1秒/92条（两段各46样本），0 query失败，27有效 WDDM 样本，同 PID7012。停止后 contextActive=0 仍有36条样本（两 segment），不是退出进程后强制释放的假回收。135 创建/135最终销毁，资源ID一一配对，存活0、attachFailures0、unknownSizes0。55窗口记录，34几何全屏记录，DPI96（本机100%）；实际日志有 NR original、1920×1080、NVOF init status0、nr=1 fg=0 nvof=1；无 ERROR/FATAL/QML错误。工具有效，**没有复现持续泄漏，也没有进行长期或4K225%/5060Ti验收**。性能开销尚未做匹配 A/B 量化，不能宣称零开销。
4. 末次 LOCAL87.719MiB、WDDM Dedicated1157.738MiB，两个口径不同且采样时刻不同；不相减判归属。停止后较长平台 LOCAL638.324MiB，关窗退出前又下降，已原样保留；不能把135个已覆盖对象销毁等同全进程显存立即归零。
5. 真实 Windows PowerShell5.1 启动、EXE exit0、moduleFilesChanged空、原配置 SHA 不变、自动证据ZIP14条目CRC通过。专设凭据哨兵文件不被复制或打包；ZIP无路径子目录、媒体/dmp/bin，没有伪“无GPU=0”。
6. `py -3.11 -B .../check-tools.py`：PS5.1四份脚本解析、查询失败留空、GPU不可用样本排除、重复 nr-on 标记段不合并、恢复打包和隐私白名单均通过。无管理员、无压力/竞争负载、所有 GPU 用例串行且每次<300秒。
7. `py -3.11 -B .../package.py`：最终交付ZIP、内层源码ZIP、manifest每文件SHA/禁止运行库扫描通过。`git diff --check` 通过。全产物在 E盘；没有删除或重试以往被拒缓存清理。

失败和纠正：第一次 PS5.1 启动继承PS7的模块搜索路径，Get-FileHash 未找到，尚未启动播放器，保留失败目录；改为显式加载执行宿主自带 Utility/Management/Cim/Archive 模块后两轮正常。初版验证器只匹配 `[nvof]` 而实际为 `[nvof-session]`，造成观察标记 false（用例实际 NVOF 已成功）；最终用例改为检验 nvOFInit status0 与 graph nvof=1。初轮观察头只记销毁总数，后来补最终销毁事件、存活年龄、DPI/窗口记录后重编并重跑最终EXE；初轮结果不冒充最终EXE通过。未隐藏这些尝试。

下一步仅需受影响用户用原复现条件返回 ZIP，按实际 revision/图代次/窗口阶段分析持续增长与停止回收，再决定是否需要更深入分配追踪。不能保证每个现场只靠这个包就能直接锁定所有者。
