# 0.0.5 发布执行记录

状态：已合并主线、推送并正式发布。用户 2026-09-12 明确授权主线合并、GitHub 发布、README 与完整便携包，目标 Likely7/Veyra-NRVideo。

开工点 826bbff，标签 checkpoint/pre-release-0.0.5-2026-09-12。先将本地 main 快进到远端 ff4122b，再合并 codex/ps5-scheduler-telemetry-decode，合并提交 5bb5c41。保留用户远端 README 的 GIF 说明删除与赞助图片尺寸调整；无 force push，无修改旧版本。

## 内容与边界

首次公开 PS5 局域网串流和完整 PC 手柄链路，收录已完成的主机持久化、解码选择、颜色、音频/调度、seek、面板和悬停修复。PSN 网页授权、HDR 保留实验标记；免码首次注册、外网串流、原生 HDR NR 未实现，不宣称全部硬件验收。用户此前实际连接和 USB 手柄通过，本轮没有替用户重新登录 Sony 或连接 PS5。

EXE 版本0.0.5，RemotePlay=ON。SHA256：
998A1724C3CFAF8AE9971305FB1D778AAF01334ABDCCA8359A03FA1CF77B8210

## 构建与测试

独立构建436步成功，无占用用户测试版：
pwsh -NoProfile -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/build/release-0.0.5 -RemotePlay -ChiakiCheckout C:/veyra-deps/chiaki-source -ChiakiStage out/remoteplay/chiaki-msvc-stage -RemotePlayPrefixPath C:/veyra-deps/remoteplay-installed/x64-windows-static -ProtocPath C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe -PkgConfigPath C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe

实际命令的 Root/ChiakiStage 使用绝对路径。日志 logs/release-0.0.5/build.log。

- scripts/acceptance/portable-smoke.ps1：项目外 TEMP 解压，清除开发 PATH，临时移开 runtime manifest，空窗口、基础播放、社区 SR+NR+FG、原版 SR+NR+FG、Video SR+NR+FG 五组退出0；后三组生成帧229/233/238。logs/release-0.0.5/portable/result.json。
- scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/release-0.0.5 -PlayerExe <外部解压路径>/Veyra.exe -PortablePlayer：47.046秒通过。logs/delivery/398b73ff8f9149ed86c49d33dc0536f6/result.json。实际 NR/NVOF/原生4K、播放、图片、NVENC H264/HEVC音轨导出，EXE哈希与发布相同。
- veyra_remoteplay_profile_tests.exe：DPAPI/原子替换/损坏拒绝及迁移、OAuth回调解析。profile.log。
- veyra_remoteplay_boundary_tests.exe：invalid-connect reopen、latest mailbox、SDL初始化与失焦中立。boundary.log 中 invalid host 是故意拒绝的测试，不是实际主机失败。
- veyra_repair_contract_tests.exe：90 checks，0 failures。contracts.log。
- 清除 PATH 后运行 scripts/remoteplay/test-ui.ps1，直接针对外部解压的 EXE：PS5入口、两次开关面板、无效配对拒绝、解码/HDR选项及模式/全屏循环通过。ps5-ui.log。不测试真实Sony登录或PS5网络。

本轮没有新物理端到端延迟/长期直播/HDR显示器测量。所有单次软件测试低于300秒，不使用旧Loop门禁。

## 包与源码

七个增强 DLL 与0.0.4身份一致；六个有效签名原件，社区NR HashMismatch。原文件不修改，不重签，不建立运行时哈希锁。详细来源/哈希/签名/许可证/移除路径见 RUNTIME_COMPONENTS_0.0.5.md 和包内 manifest。

新增 Chiaki/SDL/Opus/OpenSSL/json-c/libevent/miniupnpc/curl/nanopb/Jerasure/gf-complete 静态依赖。许可证及installed SPDX纳入包，对应依赖源码单独作为 RemotePlay-source ZIP（包括已有静态FidelityFX开源源码）。应用源码由v0.0.5标签提供，FFmpeg对应源码单独ZIP。原Veyra GPLv3与串流组合AGPLv3/OpenSSL例外分别说明。

scripts/package-portable.ps1 对0.0.5强制检查 RemotePlay=ON，明确文件白名单。71文件，forbiddenFiles=0。没有SDK、PDB、LIB、模型、日志、媒体、PSN令牌或主机档案。最终包与先前实际执行的软件包只有 README_EN.md 的旧音频文案修正，全部运行文件哈希相同；final-audit.log验证每个最终ZIP条目的大小与SHA256。

| 资产 | 字节数 | SHA256 |
| --- | --- | --- |
| Veyra-0.0.5-win64-portable.zip | 298828802 | 8A86A5B5BA81D1DD8A9D513C1E9100618D2E836CAA2D9FBE14C76566C788DCA1 |
| Veyra-0.0.5-RemotePlay-source.zip | 143743123 | C207D30B8CD1B0445033BEDF4EB2508D325446C65206BD81AC56F9112F63BF20 |
| Veyra-0.0.5-FFmpeg-source.zip | 23248643 | 50E1C3BB4A6DC0F342991B07ADA0DEA37D0477FE0A89347B5554418F9894B882 |

每个ZIP附SHA256文件。最终便携ZIP在out/releases/0.0.5/final，两个源码ZIP在out/releases/0.0.5。源码包17710个manifest条目；按已安装SPDX验证六个vcpkg port，保留来源内公开测试证书（不是用户密钥），排除35个开发二进制/测试压缩文件，排除名单写入源码包。不包含NVIDIA/Intel SDK；source Git 跟踪文件及待推送历史扩展名检查未发现DLL/EXE/LIB/PDB/模型/压缩包。

构建和测试日志统一在 logs/release-0.0.5。初次最终ZIP文件计数检查把目录也计作文件而误报，修正检查为非目录条目后逐项哈希通过，未改变包或放宽内容白名单。


## GitHub 发布完成

发布提交 7d8e24c，注解标签 v0.0.5 与 main 通过 git push --atomic nrvideo main refs/tags/v0.0.5 推送。两份 README 的远端 Git blob 与发布提交一致。main 已跟踪 nrvideo/main，未推送旧 origin。

gh release create --verify-tag --draft --notes-file 创建草稿；gh release upload 上传上述三个 ZIP 与各自 SHA 文件。GitHub API 返回六项 uploaded，其 size/digest 均与本机一致，正文与 RELEASE_NOTES_0.0.5.md 一致。检查通过后 gh release edit --draft=false --latest。

Release ID 387470534，2026-09-12T05:54:45Z 公开，draft=false、prerelease=false，latest API 返回 v0.0.5。
发布页：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.5
证据：logs/release-0.0.5/github-published.json、github-verify.log、readme-verify.log。
上传尚未完成时 draft 的 tags API 返回过404，改用已创建草稿的数字ID核验；没有重复建Release或覆盖旧版本。最终tag API正常返回公开记录。

此发布后记录仅更新文档，不移动标签、不重新打包。下一步为用户下载0.0.5进行实际PS5与新增实验功能验收。
