# 去隔行模块构建与接入

本轮在独立E盘前缀增加LGPL avfilter，不重编译/覆盖已批准的播放器FFmpeg五DLL、dav1d或专有NR/FG组件。默认 `VEYRA_ENABLE_DEINTERLACE=ON`，配置时要求所选FFmpeg前缀具有libavfilter头文件。显式关闭该编译选项时，界面显示组件不可用，不把不可用开关留作有效设置。

在此源码树执行：

```powershell
python -X utf8 -B -I scripts/ffmpeg/build-deinterlace-module.py --task issues41-45-20261016
```

固定FFmpeg n9.0.1提交 `bf1b838f2ab88b4f8fd83443325c782ea0e0f7fa`，上游ZIP SHA256 `d16837ddbb0963753aa2739971a09b07fe14fe855a6e84f253f851127f99c746`。脚本核对身份并应用Git内patch，只启用bwdif/deinterlace_d3d12；使用本机MSVC14.44及现有MSYS构建工具，不把SDK/头文件/DLL提交Git。重复编译已有配置可加 `--compile-only`。

输出前缀 `E:/项目/Veyra/deps/ffmpeg-filter-issues41-45-20261016`；CMake选择该目录为 `VEYRA_FFMPEG_ROOT`，其余引擎/厂商SDK参数沿用当前已接受配置。模块放在EXE同目录，绝对路径受限加载。旧运行组件从 `deps/ffmpeg-subtitle-formats-20261009/installed` 复用并核验逐文件SHA；没有迁移/覆盖旧目录。

日志/命令/配置：`E:/项目/Veyra/logs/issues41-45-20261016/ffmpeg-filter`。模块元数据 `share/ffmpeg/veyra-filter-build.json` 含DLL/patch/对应源码SHA。完整对应源码 `E:/项目/Veyra/archives/issues41-45-20261016/FFmpeg-9.0.1-deinterlace-module-source.zip`，包含完整上游源码、实际修改、构建脚本/配置；不含编译DLL或微软SDK。软件便携构建脚本已切到该前缀，发布审计会把模块加入文件清单并携带许可证。后续交付必须同时交付此新增依赖源码，不能只链接旧的字幕FFmpeg源码包；本轮没有制作用户测试包或发布。

自动去隔行默认开，只处理解码器标记隔行的文件/网络视频；优先D3D12视频处理器（由驱动决定CUSTOM/BOB能力），软解使用bwdif双场输出。标记逐行的视频直接旁路。设置保存，下一次打开视频生效。不宣称所有驱动具有Adaptive模式；错误标记为逐行的梳齿视频不会被自动识别。采集路径和离线导出保持原行为。

测试入口 `veyra_network_interlace_tests`：软/硬解×TFF/BFF/1080i/非零PTS单帧/逐行/关闭，重复seek与EOF完整帧数/PTS/标志；GPU回读仅存在于测试可执行程序。真实EngineController另外验证普通播放和NR+XeSS组合；网络验证HTTP点播/RTSP双流、取消连接、UI入口/非法协议/隐私和设置保存。详见本轮报告和日志。
