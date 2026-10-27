# Veyra 2.0.0 Beta Runtime Components

本版的运行组件变化如下，其余沿用 1.4.4 的身份。逐文件来源、哈希、版本、大小与签名状态记录在
`package-manifest.json`，以及 `runtime/experimental`、`runtime_local/intel/experimental`、
`runtime_local/amd/fidelityfx` 下的 `release-runtime-manifest.json`。

## 变化一：补帧运行库升到官方 310.9.1

| 文件 | 身份 |
| --- | --- |
| `runtime/experimental/nvngx_dlssg.dll` | 官方 NVIDIA DLSS SDK **310.9.1** ・ 7,460,976 字节 ・ 310.9.1.0 ・ Authenticode **Valid / NVIDIA** ・ SHA256 `FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82` |

- 超分 `nvngx_dlss.dll` **不变**，仍为官方 310.7（`BE6E434A…B6EE6E`）。
- 310.9.1 的 provider 在 NGX 初始化前由 Veyra 自己在**进程内存**里打补丁（40/30 系补帧解锁）。
  磁盘文件不被修改、不被重签名，释放时逐字节回滚。

## 变化二：新增补帧优化内核（29 个运行时文件）

`runtime/experimental/dlssg-kernels/` 下 29 个 `.ptx`：27 个 DL1/DL2 网络内核与 2 个图像内核。

- 来源：SilyNoMeta/DLSSG-Transfusion 发布包里只读提取（release `v1.4.5.3-rtx20-30-40`，
  SHA256 `3C0621BE577E56D03945ACCA48C2DFC5935A8A5759C432044E4E561B16DBBAD8`）。
- 每个文件在 manifest 中按名称、大小、SHA-256 列出，签名状态 `NotSigned`。
- **两位作者都没有为这批内核授予许可。** 上游只以编译产物形式发布，并明确声明无权授权。
  用户 2026-09-30 在知悉风险后决定随包分发。详见 `THIRD_PARTY_NOTICES.md`。
- Veyra 自己在加载前做 FNV-1a 指纹与形状校验；provider 指纹不匹配时这批内核**根本不会被使用**。
  删除该目录只关闭优化，不影响播放。

## 变化三：NR 默认位置换成社区 Lecram 310.8.3

`runtime/experimental/nvngx_dlssnr.dll`：310.8.3.0 ・ 165,840,496 字节 ・ HashMismatch（NVIDIA 证书、内容已修改）・
SHA256 `F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC`。来源 RankFTW/rhi-repo
`dlssnr-310.8.Lecram`。RTX 5070 同帧 NR 输出与 NVIDIA 310.8.0.0 逐字节相同。

## 变化四：RTX 20–50 兼容位置使用社区 SF-v2 310.8.2

`runtime/experimental/nr-ampere/nvngx_dlssnr.dll`：版本字符串 310.8.SF.0（数字 310.8.2.0）・ 165,830,144 字节 ・
NotSigned ・ SHA256 `6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927`。来源 RankFTW/rhi-repo
`dlssnr-310.8.SF-v2`。取代 1.4.4 的 NeuralScreen 1.8.2 版（DCC0DC24）和旧40版（984BEE0F）。`nr-ampere` 为历史路径名。新 QML 列表/节点提供 Lecram / SF-v2 两个选择，全 NR 链同步；新安装的 20/30/40 默认 SF-v2，50 默认 Lecram，手选及旧配置迁移保留。20/30/40 实卡尚未验证。

## 回退文件

`runtime/experimental/fallback-dlssg-310.7/nvngx_dlssg.dll`：1.4.4 使用的官方 310.7 补帧运行库
（`135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F`，Valid）。不会被自动加载，
仅供 30/40 系测试者手动换回。

## 其余组件（沿用 1.4.4）

旧 RTX40/50 NR 版 `984BEE0F…F81014` 不进入新候选或 2.x 打包清单；原 beta/1.4.4/下载原件保留作为历史回退。原 beta 的内容不随此文档更新。
RTX Video HDR 保留 NVIDIA RTX Video SDK 1.1.0 TrueHDR，SHA256
`9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B`。
许可证在 `licenses/` 下。patched FFmpeg 与 dav1d 不变；`licenses/FFMPEG-VEYRA-BUILD.json`
记录二进制来源。发布者身份审计不限制用户自行替换运行时。
