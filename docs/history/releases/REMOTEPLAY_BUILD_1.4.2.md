# Veyra 1.4.2构建与对应源码

本发布构建启用 Remote Play，应用源码对应 Git tag v1.4.2，整合此前隔离分支的已验证修改。NVIDIA FSR4 实验已回退。

Veyra-1.4.2-source.zip 提供当前应用源码与构建脚本，并附未改变的 1.4.1 FFmpeg 和 RemotePlay 对应源码 ZIP。依赖版本未改变，保留原文件名以准确标注来源。source-manifest.json 记录发布提交、分支、工作树状态和逐文件 SHA256。

Chiaki 固定于 streetpea/chiaki-ng 的 0e16950165f06e5c3291537c2eeba6e852be7120，补丁位于 scripts/remoteplay/patches。组合程序适用 Chiaki 的 AGPL-3.0-only 及 OpenSSL exception，Veyra 自身源码为 GPLv3。各依赖许可证见 licenses/remoteplay。

FFmpeg 使用现有 32→256 slices 的 PS5 H.264 修补版本和 dav1d，便携包 licenses/FFMPEG-VEYRA-BUILD.json 记录二进制身份；依赖源码包保留实际补丁与构建说明。

用 VS2022 x64、CMake/Ninja 和自行准备的授权 SDK 构建；详见 BUILD.md。隔离构建脚本 scripts/build-isolated.ps1 接受 Root、BuildDirectory、DependencyCache、TempDirectory 与 DisplayVersion=1.4.2。运行库、NVIDIA/Intel SDK、凭据和测试媒体不在源码包内。
