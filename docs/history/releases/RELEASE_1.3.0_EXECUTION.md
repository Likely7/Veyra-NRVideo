# 1.3.0 发布施工记录

用户授权整合1.2.0发布后已完成修复、更新说明并发布1.3.0至Likely7/Veyra-NRVideo。未请求关机。

分支审计：远端main是当前修复历史祖先。main从89751cd快进到14b6ee0，再建立codex/release-1.3.0制作发布。全部已完成修复分支已经包含；仅旧源码归档codex/github-source-archive和已废弃的Smooth Motion强制互斥实验不合入，保留历史。

内容见RELEASE_NOTES_1.3.0.md。完整Dolby Vision、Atmos和XeSS内部精确GPU耗时未完成；未将14项硬件问题一概宣称全部根治。最近实卡3X连续120秒有效生成14050/提交14050/过期0，普通软件门禁通过，详见FG_OUTPUT_RATE_AUDIT。

版本化文件：CMakeLists、双语README、Release Notes、Runtime Components和RemotePlay Build。增强运行文件沿用8个批准身份；本次增加已完成AV1接入的dav1d 1.5.4及对应FFmpeg构建。打包脚本检查实际二进制哈希、补齐dav1d许可证/端口来源与独立源码，不将SDK/运行库/本机媒体入Git。

发布顺序：版本构建与delivery → 包装便携和两份依赖源码 → manifest及ZIP扫描 → 解压隔离PATH启动测试 → 提交/tag/main推送 → GitHub上传资产 → 比对远端每个资产字节数与SHA256 → 发布记录。

## 本地验证与资产

- 构建：`cmd.exe /c out\\build\\veyra-build-x64-release.cmd` exit0，版本 `1.3.0`。
- 软件门禁：`logs/delivery/cbbd1e0dc55e4ccea6656fb97e4f5a15/result.json`，software short gate pass；实际NR/NVOF、原生4K、GUI播放/暂停seek、图片、H.264/HEVC导出与取消通过，capture仍标记awaiting user capture test。
- 便携包隔离测试：`logs/release-1.3.0-final-portable-smoke/result.json` pass；移除manifest、隔离PATH后空载/基础/增强组合与首次默认关闭测试通过。
- AV1从便携包直接运行：`logs/release-1.3.0-final-av1.stdout.log` exit0，实际打开 `codec=av1`，软件解码 `libdav1d`，60帧、`failed=false`。
- 三个ZIP与manifest审计：`logs/release-1.3.0-final-archive-audit.json` pass。资产如下：

| 资产 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `Veyra-1.3.0-win64-portable.zip` | 422573820 | `C5182D3F53440F7DA58983A67C8CCB2DCBCF80AAC8A3AB07276E48EE875EFCB9` |
| `Veyra-1.3.0-RemotePlay-source.zip` | 143747463 | `7FC86D207A90355B92C16343D18577A9C4A5388F249ABDFB80566D8CA5E4499F` |
| `Veyra-1.3.0-FFmpeg-source.zip` | 25277828 | `5621EB2238459FA8DACB5E0285541E3D5A0010765BAB353F9DCB41D983A1DF17` |

便携包包含八个已批准增强运行时、patched FFmpeg、dav1d许可证和manifest；源码仓库不包含这些运行库/SDK。完整DV RPU/增强层、Atmos对象、XeSS内部精确GPU耗时及所有硬件实卡验证仍未完成，更新说明已明确写出。

## 发布状态

远端发布已完成，且没有移动已发布的 `v1.3.0` tag：

- GitHub Release：[`Likely7/Veyra-NRVideo v1.3.0`](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.3.0)
- Release ID：`RE_kwDOUTcMa84XMU4n`
- 状态：`draft=false`、`prerelease=false`、`publishedAt=2026-09-15T12:03:08Z`
- Git：`main` 与 `v1.3.0` 已推送；远端 tag 仍指向发布提交 `05dea56`。
- 远端资产核对：三个ZIP及其三个`.sha256`均为`state=uploaded`；远端字节数和SHA-256与上表逐项一致。核对对象为便携包 `422573820` 字节、RemotePlay源码 `143747463` 字节、FFmpeg源码 `25277828` 字节。

这次发布没有执行关机，也没有把完整 Dolby Vision RPU/增强层、Atmos 对象音频、XeSS 内部精确 GPU 时间或未经实机验收的硬件兼容性写成已完成能力。
