# Veyra 1.0.2

## 中文

下载 **Veyra-1.0.2-win64-portable.zip**，完整解压后运行 **Veyra.exe**。其余源码包供开发者使用。

- 修复 PS5 恢复播放后仍永久耗尽重连次数的问题。连续正常解码30秒后，下一次断流重新获得最多3次重试；短暂恢复一两帧不会刷新次数。
- 手动连接遇到主机暂时被旧串流会话占用时，会有限等待重试，不再立即放弃。不强制抢占其他用户的会话。
- 每次重新建立会话都给予30秒等待首帧，避免沿用旧会话的6秒断流计时；认证拒绝等明确终止仍停止重试。
- 连接失败显示具体原因和错误码，不再一律要求重新配对。增加会话清理日志；重启保留之前的日志，多实例分别记录。
- 保留1.0.1的采集音频缓冲修复，以及现有PS5、增强、补帧、截图和导出功能。

这是恢复逻辑修复，不保证消除Wi-Fi丢包或主机断流。电脑和PS5优先使用有线连接。

升级：退出旧版，解压到新目录运行。已保存的PS5配对保留在当前Windows用户目录，无需重新输入配对码。需要保留增强设置时迁移 `runtime_local/*.v1` 与 `veyra.ini`。

七个增强运行文件与实验功能边界不变，社区NR仍标注 `HashMismatch`；允许用户替换DLL。SDK/运行时不进入源码Git，附FFmpeg和串流依赖对应源码、补丁及许可证。PS5 H.264硬解切片补丁继续保留。

## English

Download **Veyra-1.0.2-win64-portable.zip**, extract it completely and run **Veyra.exe**. The source archives are for developers.

- Fixed PS5 retry allowance remaining exhausted after playback recovered. Thirty seconds of continuous decoded progress renews up to three retries for the next outage. Brief recovery does not renew the allowance.
- Manual connections now retry temporary console session occupancy within a bounded allowance, without forcibly taking over another session.
- Every new session receives a 30-second first-frame window instead of inheriting the previous session's six-second stall deadline. Explicit terminal failures still stop retries.
- Connection failures retain their actual reason and codes instead of always suggesting pairing again. Added teardown diagnostics; GUI logs survive restarts and concurrent instances use separate files.
- Retains the capture-audio buffering repair from 1.0.1 and existing streaming, enhancement, frame generation, screenshot and export features.

These changes improve recovery; they do not eliminate Wi-Fi packet loss or console outages. Wired networking is recommended for the PC and PS5.

Close the old version and extract to a new directory. Saved pairing remains in the Windows user profile. Presets can be migrated using `runtime_local/*.v1` and `veyra.ini`.

The same seven enhancement runtimes and experimental boundaries remain. Community NR retains `HashMismatch`; user DLL replacement is allowed. Proprietary SDKs/runtimes stay out of source Git. Matching streaming and patched FFmpeg source, patches and licenses are supplied separately, including the PS5 H.264 hardware-decoding slice fix.
