# 便携 ZIP 在线更新

2026-11-19。用户确认继续使用 GitHub 的 NVIDIA 和 AMD 便携 ZIP。Windows 公开发布者签名另行接入。测试及发版验签使用独立的 Ed25519 密钥。

## 用户操作

2.1.9 用户退出旧版，解压首个支持更新的 ZIP，启动根目录的 `Veyra.exe`。需要原设置时，可从旧目录复制 `runtime_local/user-data-2.0.0` 和 `runtime_local/luts` 到新版根目录的 `runtime_local`。同一个 Windows 用户的串流账号数据仍由原来的用户目录和保护机制管理。浏览器扩展的协议注册可指向便携目录的 `Veyra.exe`，后续更新使用同一个入口。

打开「设置 → 关于 → 检查更新」。有可验签的新包时，点击「立即更新」可在同一窗口查看下载百分比及安装步骤。安装完成后，点击「立即重启」即可切换并打开新版。选择「稍后」或正常关闭会保留待重启的新版。下载可取消，导出完成后可重启。虚拟摄像机的系统注册使用其独立安装器。

同一页的「下载线路」可选 GitHub 直连、GH-Proxy 或 GHFast。默认直连，软件会记住选择。GH-Proxy 转发版本检查和下载，GHFast 转发清单及下载，版本检查仍走 GitHub API。选中的镜像或其响应校验失败时，同一请求回到 GitHub 直连重试一次。用户取消下载后停止这次传输。两个镜像仅转发公开发布文件，安装仍逐次核对版本、包型、Ed25519 签名、大小及 SHA-256。[GH-Proxy 服务文档](https://gh-proxy.com/docs/github-accelerator) · [GHFast](https://ghfast.top/)

## 产品布局和保护

- 根目录的启动器负责进程互斥和选择活动版本。每个版本的引擎、Qt、shader 和 GPU 运行库独立。设置、LUT 与日志位于版本目录之外。更新后可保留用户自定义文件和之前的完整版本。请在软件退出时清理不再使用的旧版本文件夹，活动版和回退版由 `current-version.txt` 的两行记录指定。
- 保留原有的 GitHub 最新正式版检查。从对应 Release 读取签名清单，固定仓库、平台及包型，限制下载大小和可用重定向域名。客户端核验 Ed25519 签名和 ZIP 的大小及 SHA-256。旧版、错误包型、元数据篡改及损坏下载不能触发安装。
- 更新进程在主程序运行时复验清单和 ZIP，通过 Windows 附件服务及系统 `tar.exe` 解包，再以独立配置验证新版启动。通过后将版本目录移到位，并记录已验证的版本与 ZIP 摘要。启动器收到重启请求后原子更新活动版本。重启进程等待安装锁释放后，从原启动器打开新版，兼容现有根启动器协议。安装失败继续使用旧版。新版启动异常时也可通过 `Veyra.exe --rollback` 回退。
- 内测构建通过 CMake 配置独立测试公钥和回环地址。正式构建的仓库和线路固定在程序中。没配置公钥时提供版本检查和发布页入口。第一版需要 Windows 自带的 `tar.exe`。安装器和更新器以当前用户权限运行。

## 发行流程

1. 为发行单独创建并保护 Ed25519 私钥，使用 OpenSSL 导出原始 32 字节公钥。公钥以 64 个小写十六进制字符传给 CMake `-DVEYRA_UPDATE_PUBLIC_KEY=...`。OpenSSL 输出的 Ed25519 SubjectPublicKeyInfo DER 为 44 字节，末尾 32 字节是公钥。私钥以加密 PEM 保存在受控的发行环境。
2. 按现有流程审计 NVIDIA、AMD 的版本 stage，签名服务就绪后先完成自有 EXE/DLL 的 Authenticode 签名，再生成 stage 的文件哈希。用同一构建的 `veyra_qml_ui.exe`、`veyra_portable_launcher.exe` 和 `veyra_update_worker.exe` 打包。
3. 两个 stage 分别执行 `scripts/package-portable-update.py wrap`。最终文件名为 `Veyra-<版本>-NVIDIA-win64-portable.zip` 和 `Veyra-<版本>-AMD-win64-portable.zip`。
4. 执行 `scripts/package-portable-update.py sign`，输入两个最终 ZIP、版本、可选发版摘要和发行私钥。生成 `Veyra-portable-update-v1.json`，签名覆盖确切的清单字节。将两份 ZIP 和清单作为同一个正式 Release 的资产，再确认对外能下载完整文件。发布后的 ZIP 内容需要变动时使用新的版本号。

```powershell
py -3.11 -X utf8 -B -I scripts/package-portable-update.py wrap --version <version> --stage <vendor-stage> --build <build> --output <artifacts>
py -3.11 -X utf8 -B -I scripts/package-portable-update.py sign --version <version> --nvidia <nvidia-zip> --amd <amd-zip> --openssl <openssl-exe> --key <offline-ed25519-key> --notes <summary> --output <artifacts>
```

Windows 信任使用 Authenticode。Ed25519 验证的是 Veyra 发布的更新清单和下载完整性。Defender 检测仍由 Windows 处理。
