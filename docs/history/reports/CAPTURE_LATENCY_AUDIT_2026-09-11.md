# 采集预览延迟排查与修复

日期：2026-09-11。源码基线 `4e2bd73`。本轮由用户反馈 Veyra 采集预览慢于 OBS/PotPlayer 触发。确定缺陷已修复，构建与相关软件回归通过；未测三款软件的同源物理显示延迟。

## 已确认的事实

1. 本机仅发现 OBS 进程，没有正在运行的 Veyra。`logs/veyra-app.log` 已被上一轮软件验收覆盖，不能当作反馈者本次采集的日志。没有抢占 OBS 的采集设备。
2. 本机 OBS 32.1.2 日志 `C:/Users/123/AppData/Roaming/obs-studio/logs/2026-09-10 21-56-37.txt` 的设备段记录 USB3 Video、1920x1080、60fps、YUY2、`buffering: disabled`、`hardware decode: disabled`。这是本机上次配置，不证明反馈者使用相同配置。YUY2 不需要压缩视频解码，不能由这个 hardware decode 字段推断 CPU 正在解码 H.264。
3. 核对 OBS 32.1.2 对应提交 `fb4d98bf88fae5fc85cb11fc57f7c5e309282194`：`plugins/win-dshow/win-dshow.cpp` 的 `SetupBuffering` 调用 `obs_source_set_async_unbuffered`；`libobs/obs-source.c` 的非缓冲取帧分支丢弃旧帧、取最新帧直接返回。这里只研究行为，没有复制源码。
4. Veyra 当前 YUY2/NV12/RGB32 通过 `ConnectDirect` 接自有接收器，回调复制至容量1的 mailbox；接收器没有按参考时钟等待显示。三个 allocator buffer 是内存池请求，不等于固定积压三帧。不能把这项直接说成50ms延迟。
5. Git 标签 `v0.0.1` 指向 `181a047`，采集仍强制 `RGB32` 并用 DirectShow `RenderStream` 自动接转换器；当前源码的 YUY2 原格式 GPU 转换不在该标签中。发布用户包与本地开发版必须分别判断，不能宣称本地修复已经送达下载用户。实际下载包身份还需反馈者确认。

## 确定缺陷：关闭 FG 仍按源时钟等帧

`EngineController.cpp` 在同一 epoch 只锚定一次 `PresentationScheduler`：

```text
deadline = 首次回调主机时间 + 当前PTS - 首次PTS + FG主动延迟
```

FG关闭只把最后一项设为0，仍会按源PTS等待。首帧回调偶发变晚，或设备PTS与主机时钟存在漂移时，后续已经GPU-ready的真实画面会被继续扣留。无补帧的交互预览不需要这种平滑播放时钟。

确定性复现输入：首帧比稳定到达关系晚12ms；第二帧恢复正常，处理3ms。旧策略在第二帧处理完成后仍等待9ms。这个9ms是构造条件下的调度算术，不是实卡测量或对OBS的实际差距。

本次修复：物理采集且FG关闭时按GPU-ready呈现，不再由源PTS增加等待。NR/SR开关不影响此策略。保留源PTS供历史、计数和音频同步；文件播放、无设备节拍的测试文件回放以及FG的连续时间线保留。日志新增 `pacing=capture-ready/source-pts`。没有移除资源fence或改变图处理顺序。

旧测试覆盖缺口：`LivePresentationTimingTests` 中“no-FG never waits”的断言测试的是 `livePairHoldMs`，该函数已不被产品源码调用。它通过不代表当前 `PresentationScheduler` 没有等待。本次新增回归直接覆盖产品使用的调度器，包含旧行为复现、无缓冲、时钟漂移和切回FG。

## 仍需实证的部分

| 位置 | 当前行为及风险 | 下一步 |
| --- | --- | --- |
| 显示端 | 3-buffer flip-discard，vsync=false，支持时允许tearing；没有显式使用frame-latency waitable object。Present返回不等于实际扫描，也不能由3-buffer反推固定3帧延迟 | 用PresentMon/ETW区分实际display mode、Present阻塞和提交到扫描；再判断是否接入最大排队1和显示槽位反压，不能只改buffer数 |
| MJPEG及其他压缩模式 | 仍经DirectShow兼容链转RGB32，自动选择的解码/转换filter可能有额外成本；当前回调计时起点在它们之后 | 下一轮记录实际filter/allocator及帧时间，固定相同分辨率/帧率/格式比较；不得以本机YUY2结果代表所有采集卡 |
| 普通预览转换 | YUY2先复制mailbox、再复制upload，GPU转linear，再转RGBA8，最后blit；无增强时不运行NR/SR/flow，但仍有共享图转换成本 | 测量各pass与CPU复制，必要时在共享图增加无增强融合输出pass；保留颜色正确性与对比，不复制新播放器 |
| GPU调度 | 两批in-flight，有界但不等于零等待；输出等fence完成后由CPU推进；约0.2至1ms轮询片段可能增加完成观察延迟；upload复用有fence保护，已完成时直接返回 | 先看readyWait、slotWait、Present及过期帧的同帧轨迹，不能把所有fence调用都说成每帧阻塞 |
| FG | DLSS使用一源周期的主动时间线偏移，并等待后帧计算插帧；XeSS仍经过这层时间线，SDK内部另有调度，存在重复等待的待测风险 | 单独核对DLSS/XeSS的提交/扫描时序；不在本轮无FG修复中随意取消插帧间距 |
| 音频 | 采集视频呈现后仅更新共享PTS/host；音频线程跟随视频调节PCM，不主动要求视频等音频 | 修复后由用户检查音画同步；不能把500ms音频容量当成视频固定延迟 |

没有取得PotPlayer内部实现，也未进行三款软件的外部延迟A/B，因此不宣称已经证明PotPlayer最快的内部原因。显卡占用率高低同样不能直接换算延迟。

## 验证与交接

完成门槛：调度缺陷可复现、定向修复、完整构建与相关软件回归、记录物理验收缺口。本轮不更新Release、不push，不重新引入FRUC或AMD NR。

实际执行命令（项目根目录）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root 'C:\Users\123\Desktop\Veyra DLSS Video Player' -Preset x64-release
./scripts/acceptance/scheduler-short-test.ps1 -Name capture-latency-contract -Exe out/build/x64-release/veyra_repair_contract_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name capture-latency-timing -Exe out/build/x64-release/veyra_live_timing_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name capture-latency-live -Exe out/build/x64-release/veyra_live_presentation_tests.exe -TestArgs @('loop/local/fixed_clips/test_av_1080p.mp4','logs/capture-latency-live-20260911')
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root 'C:\Users\123\Desktop\Veyra DLSS Video Player'
```

| 检查 | 结果与日志 |
| --- | --- |
| Release完整构建 | exit0，`logs/capture-latency-build-20260911.log` |
| 合同与新增缺陷复现 | 81项PASS，0.063秒；`logs/scheduler-repair-20260910/capture-latency-contract.result.json`及同名stdout |
| 采集时序旧合同 | 41项PASS，0.152秒；同目录`capture-latency-timing.result.json`，不把未调用helper的旧断言当产品验证 |
| 实际RTX引擎回放 | 28项PASS，4.802秒；同目录`capture-latency-live.result.json`；包含NR、DLSS2X/4X、设置、暂停恢复和队列边界 |
| delivery | 23项PASS，42.907秒；`logs/delivery/1be4f528d272465585c96ada5b9b88e3/result.json` |
| Git/身份 | `git diff --check`通过；NR DLL指定SHA匹配、Authenticode Valid；没有SDK/runtime进入Git |

RTX真实执行日志 `logs/capture-latency-live-20260911/engine.log`：Feature18 Create `0x1`、非空handle、SEH0；DLSSG Create/Evaluate `0x1`、SEH0。delivery覆盖NR/NVOF、native4K正确性和NVENC，不代表实卡性能。无测试失败；探索过程中几次猜测文件路径不存在，已通过`rg --files`定位实际文件，未作为测试结果。

新应用 `out/build/x64-release/veyra.exe` SHA256：`CDCB2303402438FC208E00B6F9F88708B16249FEB32410FE6E5D484FBBB15747`。修改文件：`PresentationScheduler.h`、`EngineController.cpp`、`RepairContractTests.cpp`、本报告、`WORKLOG.md`、README当前进度入口。没有制作或更新发布包。

验证边界：新增即时策略通过调度器确定性回归；实际引擎测试使用有节拍控制的文件回放，保留其源PTS调度，因此不冒充物理采集即时分支已实卡通过。实体采集、OBS/PotPlayer同源A/B、实际扫描和音画听觉验收均未执行。测试素材位于历史目录不等于运行旧Loop。

下一条实际验收：确认反馈者版本，NR/SR/FG全部关闭、同一采集格式，以屏幕计时器/高速录像比较Veyra、OBS非缓冲、PotPlayer。分别记录软件回调到Present返回与外部可见延迟，不相互替代。之后才用相同输入逐项打开NR/SR/FG。

参考源码：

- https://github.com/obsproject/obs-studio/blob/fb4d98bf88fae5fc85cb11fc57f7c5e309282194/plugins/win-dshow/win-dshow.cpp
- https://github.com/obsproject/obs-studio/blob/fb4d98bf88fae5fc85cb11fc57f7c5e309282194/libobs/obs-source.c
