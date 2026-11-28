# Veyra 2.2.2 构建与对应源码

Windows x64 发行物用 MSVC 2022、Ninja Release、Qt 6.8.3 和 Inno Setup 7.1.1 构建。AMD 和 NVIDIA 安装器及便携包使用同一版核心程序，按实际 GPU 打包各自的运行组件。

- 产品源码提交为 **52481824847b6ad20c9872af1fa3575ff8b74141**。项目版本和显示版本均为 2.2.2，使用正式 Ed25519 更新公钥，测试更新端口留空。包内清单包含源码提交、文件大小和 SHA-256。
- FFmpeg 使用 n9.0.2、256 slices、D3D12VA 引用池和去隔行补丁。对应源码包含实际构建的配置、dav1d 和 zlib 源码及许可证。
- AMD RX 7000 worker 使用 RedDuke 1.4.0 的 MIT 网络层，固定在 3d7eb7cac4a5185455ae029d9a3339369d900f88。FSR 头文件为 2.4.0，发行运行库沿用验证过的 2.3.0。
- Chiaki 和 libdovi 的构建版本沿用 [2.2.1 的对应源码](BUILD_2.2.1.md)。新源码页同时提供共用归档。源码归档不含私有运行库、本地预设、启动音乐和测试素材。
- ANR B20、RIFE 及实时翻译模型走按需安装。启动音乐仅在运行包内提供。Windows 安装器尚无 Authenticode 发布者签名，在线更新索引的签名可独立核验。

[源码资产及校验](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.2.2-sources) · [完整更新与实测数据](RELEASE_NOTES_2.2.2.md) · [第三方许可](../THIRD_PARTY_NOTICES.md)
