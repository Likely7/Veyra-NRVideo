# PS5 断流后自动及手动重连修复

基线e3d6c3c（已发布1.0.1后的文档提交），存档checkpoint/ps5-reconnect-20260913，施工codex/ps5-reconnect-repair。用户描述自然停帧后自动重连失败、手动连接报“未返回可解码画面”，重启进程可恢复。用户授权使用已保存的配对进行连接测试，不重新配对、不改主机电源、不发布新版本。

## 事实与范围

- 原现场logs/veyra-app.log已在22:57重新启动后被清空，没有这次自然断流证据。GUI main和AppShell重复openFile(wb)，每次重启覆写；重复打开只能确定丢启动行，不能据此宣称整份空日志或断流根因。此前监测程序先打开默认日志再采用override，亦会触碰默认日志。
- 已有恢复策略把everVideo用于重连首帧截止时间：第一次等30秒，后续会话只等6秒。它可能过早终止握手，不证明本次现场就是这个原因。
- 初次会话一旦RP_IN_USE立即失败；用户手动重连创建新的StreamRecovery，因此即使此前播放过，也不等待PS5释放旧占用。
- 初帧失败由EngineController覆盖成通用“重新配对”提示，丢失owner保存的终止原因。
- 用户停流后运行旧版live presentation测试 --last-paired-ps5 logs/ps5-reconnect-20260913/before --reconnect（75秒外部上限）：50秒观察，PASS。一次自动重连、713次恢复后健康采样、退出idle=1。可控停止传输与自然故障不同，不声称已复现自然卡死。

## 实施

1. 每个新会话均给予30秒握手/首帧时间，已经出帧后仍按1/3秒IDR、6秒重连；最多3次重连和1/2/4秒退避不变。首次只放行明确RP_IN_USE占用错误的有限重试；认证拒绝、主动结束仍立即失败，不强制抢占主机。
2. 初帧失败显示owner原因及固定数字终止码/错误码，不提示无依据重新配对。teardown开始/完成加入日志，辨别join卡住与主机拒绝。
3. GUI仅在main初始化日志，启动时尊重override；追加保留重启前记录，独占写失败时用PID日志分流，启动边界立即flush。其他测试默认截断独立文件不变。日志不包含配对密钥或原始Chiaki报文。
4. 回归覆盖新会话首帧超6秒、首次占用有限重试、拒绝不重试、旧预算不被偶发帧刷新、日志重开保留及竞争写不截断。真实PS5测试增加同一EngineController取消恢复后连续手动重连两轮；仍复用现有产品引擎。

## 验证与交付

日志统一logs/ps5-reconnect-20260913。自然故障根因未确定；禁止把有限恢复策略改善称为长期断流问题全部根除。SDK、运行文件、配对、测试日志不进入Git。

### 最终结果

- `cmd.exe /c out\ps5-reconnect-build.cmd`：产品相关库、live presentation、repair contract、remoteplay boundary及core-windows构建成功。随后正常关闭用户已停止串流的旧GUI（CloseMainWindow，无强杀），`cmd.exe /c out\gpu-dis-build-all.cmd`完整99步构建成功；原有FFmpeg头文件/未使用局部变量警告保留。日志build.log、full-build.log。
- 使用`scripts/run-short-test.ps1 -Exe <目标> -TimeoutSeconds <上限> -LogPrefix logs/ps5-reconnect-20260913/<名称>`：core-windows/veyra_remoteplay_core_tests.exe 75/75，product-repair/veyra_repair_contract_tests.exe 107检查/0失败，veyra_remoteplay_boundary_tests.exe PASS；每项30秒外部上限。新增日志测试确认重开追加保留两段内容、第二写者不能截断活跃日志；独立的无效连接重开3次和邮箱/手柄边界通过，不冒充网络实机。
- `veyra_live_presentation_tests.exe --last-paired-ps5 logs/ps5-reconnect-20260913/manual --manual-reconnect`（210秒上限）：取消可控断流恢复后，同一个EngineController/进程手动重连两轮，每轮至少180实际处理帧、100生成帧及音频running，停止后idle=1；整体PASS，实际约40秒。
- **这次实机确认了占用问题**：第一轮手动重连attempt0收到quitReason=4，attempt1在15:09:56.895Z恢复首帧；第二轮attempt0/1/2均收到quitReason=4，attempt3在15:10:13.654Z恢复首帧。前者旧策略必然立即失败，因为本次新owner还没有任何画面；新版保留同一进程/配对，等待主机释放后恢复。未强制抢占，也未修改PS5设置。
- `veyra_live_presentation_tests.exe --last-paired-ps5 logs/ps5-reconnect-20260913/automatic --reconnect`（100秒上限）：50秒观察PASS，自动恢复attempt2（attempt1收到占用），463次恢复后健康采样，画面/NR/SR/FG/音频重新推进，idle=1。真实NR CreateFeature18 result=0x1、handle非空、seh=0；DLSSG 3840x2160 Create=0x1、Release=0x1、evaluates=907。构建与此轮有重叠，呈现速率45–118不作为稳态性能验收，不声称固定120fps或手柄/声音主观体验。
- 新GUI两次运行`--smoke-empty --no-nr --no-sr --no-fg --smoke-seconds 2`，各20秒上限，指定VEYRA_LOG_FILE=本轮gui-restart.log；两次started/stopped均保留，原默认日志哈希不变。验证了main尊重override、AppShell不再二次截断。GUI不自动连接PS5。
- EXE SHA256：23791CE1A316A4CFDAA9D292502E4620CB9E74BAA07E5310F82D843A53FAE326；FFmpeg avcodec仍0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。桌面“Veyra PS5 测试版”仍指向out/remoteplay/product-repair/veyra.exe，已更新为本地修复版（资源版本暂仍1.0.1），没有改GitHub的1.0.1包。

### 交付边界

此次确认并修复“暂时占用导致新手动连接立即放弃”，并修正新会话首帧等待过短、提示与日志覆盖。没有原自然卡死日志，不能认定网络/解码/驱动哪个最先停住，也不能保证此后不再断流。下一步用户长时游玩；若再停住，新的日志会保留跨重启的记录，检查最后完整视频回调、解码时间、quitReason和teardown完成标记。永久阻塞的驱动/join不能安全强杀线程，仍不detach或释放存活回调资源。

修改文件：apps/veyra/main.cpp、apps/veyra/ui/AppShell.cpp、include/veyra/Log.h、src/base/Log.cpp；StreamRecovery.h、ChiakiBackend.h/.cpp、RemotePlaySessionSource.cpp、EngineController.cpp；CoreTests.cpp、RepairContractTests.cpp、LivePresentationTests.cpp及本记录/WORKLOG。git diff --check通过。初次误用run-short-test.ps1 -Help只产生参数错误，未启动产品；真实测试命令及输出按上述记录。未push、未发布。
