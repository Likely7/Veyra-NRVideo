# RTSS 未启动误提示与兼容重启循环修复

## 现场证据与开工存档

用户在已发布 NVIDIA 2.0.3 本地包反馈：没有开启小飞机，弹出要求兼容重启的对话框；重启后仍反复弹出。三份用户日志均显示启动时 `running=false injected=false compat=auto`，约两秒后 RTSSHooks64.dll 注入并建议重启。用户最后手动把 overlayCompat 改为 off，不能修改或重置这份配置来制造通过。

本机发现 `E:/项目/Veyra/deps/rtss-overlay-20261002/RTSSHooksLoader64.exe /i`，PID 13316、父 PID 11020、启动时间 2026-10-04 11:31:40 +08:00。父 PID 与上一轮 `logs/release-2.0.3-20261004/rtss-test-pid.txt` 完全一致，主 RTSS 已退出，子加载器仍在注入。这是 Agent 上轮测试收尾遗漏，不是用户开了小飞机。

独立 profile 运行已发布原 EXE 的 `baseline-orphan` 实际复现同样日志，exit 0。开工主线/源码基线 354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff，分支 `codex/rtss-restart-loop-20261004`，标签 `checkpoint/pre-rtss-restart-loop-20261004`。全引用 bundle 已 verify，SHA256 `08679242174753dbf6f367f3dfe2cd85b0a8981180d6597767e0b2ea9a491dd2`。原现场日志和状态在 `E:/项目/Veyra/archives/rtss-restart-loop-20261004/`。

## 最小修复范围

1. 记录并停止本轮查明的 Agent 自有残留加载器，只按已核验 PID/绝对路径/父 PID/创建时间处理，不停用户的监控程序。验证没有 RTSS 后原发布 EXE 正常运行且不再触发此提示。
2. 区分 RTSS 活跃服务器和仅加载钩子。启动与晚到检测统一以活跃服务器为自动兼容/弹窗依据；只有模块存在不推断“小飞机已启动”。保留实际模块路径和状态日志，GPU UI 保持已有的进程内 OSD 关闭策略。
   - 原用户安装测试 installed-matrix-v1 暴露 force terminate 后共享内存仍为有效 signature、但无 RTSS/加载器进程的真实反例。仅共享内存签名不足以证明运行；补充 RTSS.exe 存活检查，并在该残留 mapping 下回测。原配置已在失败后逐字节恢复。
3. 兼容重启携带一次性、专用途径的 software UI 参数，避免服务器检测时序或启动退出造成反复重启。普通设置重启不强制 RTSS 模式，off/显式 RHI/OBS 的用户选择仍适用，不永久改用户设置。
4. 修正提示措辞，不断言小飞机主程序刚刚启动；独立 profile 验证未启动、孤立钩子、真实 RTSS 启动/晚到、off、设置重启、接受兼容重启及第二进程渲染方式。真实视频/软件背景做针对性回归。测试清理包括 owned RTSS 的子加载器，验证无遗留。

必要改动限 `apps/veyra-qml/main.cpp`、RTSS 检测/重启桥接与 QML 对话框、翻译和定向验收脚本/文档。增强/音频/串流/颜色/运行库均保持。最新现场问题授权覆盖对应历史 UI 冻结，未授权新 merge/push/Release/关机；HDR PR 仍暂缓。桌面旧工作树、用户配置、已发布压缩包及其 manifest 不改。

所有新构建/测试/日志/临时/本地修复交付产物在 `E:/项目/Veyra/{build,tests,logs,tmp,test-packages,archives}/rtss-restart-loop-20261004`，单测试进程最多 300 秒、构建最多 900 秒，TEMP/TMP 仅子进程。仅提供本地可验证修复，远端发布另按当前用户指令。

## 验收要求

- 原发布包现场复现留证；停自有残留后原包不再误提示；孤立钩子在修复版中不要求重启。
- 真实 RTSS 自动兼容仍生效、透明背景不回归；晚到情况下仅一次建议，接受后实际第二进程为软件 UI 且不再弹窗。
- 无 RTSS / off / 显式渲染器 / OBS / 普通重启保持各自行为；导出期间不能重启。
- 构建成功；记录实际命令、退出码、原生日志、用户目录/源码/Runtime 隔离与本轮自启进程零遗留。
- 测试过程不篡改 proprietary DLL 或永久关闭系统 RTSS、不把“未弹窗”冒充全部监控软件兼容。

## 用户追加真实安装测试

用户提供 `E:/App/RivaTuner Statistics Server/` 并要求“真实启动，并且多设置几个参数”。直接运行该目录的 RTSS.exe/加载器，按官方随附 SDK 的 Profile API 设置 Veyra 专属测试 profile，包含检测级别、OSD 开关/缩放/位置/颜色/阴影与帧率限制；不改变全局检测和启动设置。写入前存档现有 Profiles 文件，收尾恢复逐字节并清除本轮创建的 Veyra profile，只停止本轮启动的已核验进程及其加载器。

同时在本地修复候选/独立用户目录里实际播放测试视频，切换 NR 参数、RTX 4K 超分、DLSS/XeSS/FSR/VFG 的倍率与质量、暂停/seek、窗口/全屏/页面，使用官方共享内存的独立 OSD 槽投放真实屏显文字，并保留帧进展、实际后端、原生截图和故障日志。每个 GUI 测试 ≤300 秒。

版本记录纠正：本轮读出用户安装和之前测试目录 RTSS.exe 的 FileVersion/ProductVersion 都为 `7.3.5.28314`、SHA256 `84E6E439D313DCEE0BA9549D8248D3923D9DE6805D0380BE5EAB337446856736`。此前文档写 RTSS 7.3.7 没有对应二进制证据，应改为 7.3.5.28314；不能因为目录/下载说明便宣称另一版本通过。RTSSHooks64.dll 为 `68C496DEA7DED5B8766092F0159F4E9BA3947DF0829F594EB6981D2B06352AD5`。

## 最终真实验收（2026-10-04）

最终 build-v3.log 成功，EXE SHA256 d4ab803562641ef16d7b917b4b27db51b7b379ebb7ea975d47d6b472598bf994，17,342,976 bytes。实际用户安装版 7.3.5.28314；installed-matrix-v3 11启动/重启项通过，installed-stress-v4三套RTSS Profile API配置×11效果/操作=33项通过，installed-transport-v1先验证异步seek/全屏恢复。

| RTSS配置 | 实际参数 | 结果 |
|---|---|---|
| low-osd | 检测1，OSD开，2倍，(16,16)，橙，背景开，统计开，不限帧 | 11/11；真实视频截图+RTSS计数/颜色/坐标/缩放一致 |
| high-large-osd | 检测3，OSD开，3倍，(24,24)，绿，背景关，统计开，不限帧 | 11/11；实际大OSD截图与原生统计一致 |
| medium-osd-off-cap60 | 检测2，OSD关，1倍，(48,32)，背景开，统计关，限60 | 11/11；视频截图无测试OSD，限制属性读回60 |

每套含基础、NR四参数、NR+最高RTX4K、NR+RTX+DLSS2、DLSS6、XeSS4、FSR3.1 2、VFG2Low/4Medium/8High、NR resize/暂停/seek2s/全屏/退窗。33视频与33UI截图，软件背景alpha255，进度和实际active后端核验；最终3个GUI约80秒，日志无ERROR/FATAL，无第二次重启提示或设备移除。

失败记录保留：matrix-v1 Win740普通用户启动修正；installed-matrix-v1共享内存残留反例；stress-v1 seek调用名/手算字段偏移、v2 PNG写入读取竞争、v3 NR冷初始化期间固定时间断言，分别修正测试脚本而非放宽实际状态断言。v4使用真实SDK编译offsetof，异步操作等待已呈现2秒目标帧/真正全屏/窗口恢复。不能把失败轮内的QML PASS计作通过。

本机RTX5070/616.56：VFG8High 2K30有实时调度降档，瞬时提交约30–31fps；VFG4在限60时也降档。软件稳定短测不代表稳定240fps、实屏刷新率/延迟、全部驱动或画质。未新增OBS捕获、AMD或Xbox实机测试。

RTSS Config/Global逐字节恢复，SHA256 46e3258195f5784f99f152dfe7e1405f614d7b42f6609d95993cc19396b1ac83 / 1e44c57669402f4c49e519c8322613170b72f3ddd519f8f1a495d9cc828ee7ca，测试profile删除，主RTSS和加载器零遗留。最初现场配置为off，但原包在12:44–12:45又运行两次并切为auto；保留当前值，不能回写最初off。主日志增加4359字节，最初2341字节前缀完整且三份原日志已完整存档，其余两日志byte-identical。最终本地完整NVIDIA包/源码/manifest已由rtss-restart-deliver.py生成并独立审计通过：1537载荷、95PE、48运行组件，产品commit c9d946e；索引在test-packages/task/DELIVERY.json。仅本地，不公开发布。
