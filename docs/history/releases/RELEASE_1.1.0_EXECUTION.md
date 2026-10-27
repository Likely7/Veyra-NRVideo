# 1.1.0 发布记录

用户明确授权：发布1.1.0、README标注Smooth Motion支持与教程，发布核实完成后关机。基线54fe32a；施工分支codex/release-1.1.0，开工存档checkpoint/pre-release-1.1.0-20260914。

## 范围

包含普通版Smooth Motion展开教程及采集卡格式扩展。普通版没有强制FG互斥，不合入27c17eb受限实验构建；驱动设置由用户在NVIDIA App管理。用户已报告本机实际有效、稳定，不声称算法内置或叠加效果经过验收。更新双语README、版本号、Release Notes、组件和对应源码说明。

## 实际验证

- `cmd.exe /c out\release-1.1.0-build.cmd`：完整构建通过，EXE ProductVersion=1.1.0，SHA256 `F4106617DD743E2913729D3BDF9DF8A8DE911E1E28EAC482F3204ECD4967AE39`。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair`：23/23通过，46.8241855秒。证据 `logs/delivery/f3bba8f704634831ab09ab62e7c233b7/result.json`；实际RTX5070执行NR Create=0x1/SEH0，原生4K Evaluate=12，播放、图片、H.264/HEVC导出与取消通过。
- `veyra_capture_color_tests.exe`：127项通过；`veyra_hdr_color_tests.exe`：30组采集颜色/精度及8组HDR检查通过。反馈者采集设备尚未实卡验收。
- 便携包独立解压到 `out/releases/1.1.0-verify/`，运行 `scripts/acceptance/portable-smoke.ps1 -PackageDirectory out/releases/1.1.0-verify/Veyra-1.1.0-win64-portable -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-1.1.0/portable-smoke -CaseSeconds 15`：5/5通过，每项约15秒。空载、基线820帧、社区707处理/664生成、原版712/669、RTX Video SR712/669；记录实际模块位于包内，manifest暂移除仍可启动，生成截图成功。
- `out/audit-release-1.1.0.py`：ZIP路径、逐文件大小/哈希、源码二进制排除、七运行文件、FFmpeg身份检查通过。无SDK、个人配置、凭据或测试媒体进入便携包；源码包只含对应开源依赖。`git diff --check`通过。

本次所有单项测试均低于300秒。没有重新做PS5长时测试、Smooth Motion屏幕帧率测量或与内部FG叠加实测；驱动效果依据用户本机反馈。

## 打包与身份

实际使用 `scripts/package-portable.ps1 -Version 1.1.0 -Root . -OutputDirectory out/releases/1.1.0 -BuildDirectory out/remoteplay/product-repair`，并使用 `package-remoteplay-source.py --version 1.1.0`、`package-ffmpeg-source.py --version 1.1.0 --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/ffmpeg-ps5-slices-source` 生成对应源码包。

| 资产 | 字节 | SHA256 |
| --- | ---: | --- |
| Veyra-1.1.0-win64-portable.zip | 303921572 | FC94D54A24F90F104B613D85D3AD260CD1D7EE70144143017DAA56E76BE18E2E |
| Veyra-1.1.0-RemotePlay-source.zip | 143744092 | F1702457EEB4DBB5924F1CB8F2544A4292F6B5AF840E824A944C0531983C93EB |
| Veyra-1.1.0-FFmpeg-source.zip | 23253198 | E091411D5FE3AB29F6453B60E8AB5CC091BC76267B4525B5EC815B16BD92021E |

各附.sha256，共六资产。便携99文件、RemotePlay源码17714文件、FFmpeg源码10451文件。七增强运行文件原样沿用：六文件签名Valid，指定社区NR仍HashMismatch，来源/版本/完整SHA256见RUNTIME_COMPONENTS_1.1.0及package-audit.json，适用许可证随包保留；没有增加Smooth Motion驱动文件。

patched avcodec SHA256仍为0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F，对应源码、补丁和构建记录已附带。源码Git不包含运行时、SDK或构建产物。

## 发布核实

上传前检查与命令结果在 `logs/release-1.1.0/`。先创建草稿上传，核对服务端六资产的字节数和digest后再正式公开为latest；完成结果追加WORKLOG，不移动版本标签或改动已验证资产。

### 发布完成

2026-09-13T18:50:38Z，GitHub Release 387998437已正式公开为latest，tag v1.1.0指向1523ddb，非草稿、非预发布。main/tag推送成功，远端README与标签blob匹配。六资产全部uploaded，服务端size/digest逐项匹配本地，证据logs/release-1.1.0/remote-assets-verified.json与published-release.json。地址：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.1.0 。
发布后不移动tag，不修改ZIP。包内文档与源码文字一致；首次按工作树原始字节对比README因Git切换分支的CRLF/LF差异报不一致，归一化换行后全部一致，包自身逐字节manifest校验仍通过。按用户授权，在保存本记录后执行关机。
