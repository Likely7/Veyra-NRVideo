# Veyra 2.2.1 构建与对应源码

Windows x64 使用 MSVC 2022、CMake/Ninja Release、Qt 6.8.3 和 Inno Setup 7.1.1。AMD 与 NVIDIA 的安装器、便携包来自同一个产品构建。各包的 `package-manifest.json` 记录产品源码、逐文件身份和对应源码资产。包中的 Windows 虚拟摄像机组件同时提供 x86、x64。安装器的 Authenticode 与在线更新清单的 Ed25519 分别验证。

- `project(veyra VERSION 2.2.1)`、`VEYRA_DISPLAY_VERSION=2.2.1`。发行配置保留 2.2.0 的 Ed25519 公钥，`VEYRA_UPDATE_TEST_PORT` 留空。自行构建可选择自己的信任根；无公钥时软件检查版本并打开发布页。
- 2.2.1 FFmpeg 源码使用 n9.0.1、dav1d、zlib、256 slice 及 D3D12VA 参考帧池补丁。对应源码包 `FFmpeg-9.0.1-PS5-reference-pool-source.zip` 随 2.2.1 源码资产准备。
- Chiaki 固定在 `0e16950165f06e5c3291537c2eeba6e852be7120`，应用 [锁定的 0001–0005 补丁](../scripts/remoteplay/dependency-lock.json)。libdovi 3.4.0 固定在 `d1abe0e27ff2c7ab3339614d06db9f8a058af6b2`。对应 `Veyra-2.2.1-remoteplay-dovi-source.zip`。
- AMD RX 7000 使用已验证来源的网络源码重新编译 Veyra worker。网络源码和许可证在 `Veyra-2.2.1-rdna3-network-source.zip` 中。HIP 由 AMD 驱动提供。
- 运行版沿用已审核的 2.2.0 厂商组件。实验 Runtime Pack 身份在 `release-runtime-manifest.json`。ANR 下载官方运行组件，实时翻译模型通过软件内安装。RIFE 通过[独立组件](RIFE_DIRECTML.md)启用。启动音乐只在用户运行包内。视频降噪随构建生成 `VideoDenoise.dxil`，算法来源和许可证见 [第三方许可](../THIRD_PARTY_NOTICES.md)。

未变更的基础依赖、去交错模块和 lmxxf 对应源码见 [2.2.0 构建记录](BUILD_2.2.0.md)。产品依赖安装、调试构建和私有测试见 [开发入口](DEVELOPMENT.md)。
