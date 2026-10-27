# 1.1.1 发布记录

用户于2026-09-14授权发布到Likely7/Veyra-NRVideo。基线831432f，发布分支codex/release-1.1.1，存档checkpoint/pre-release-1.1.1-20260914。本次没有关机请求。

范围：RTX30独立实验NR选项；首次NR/SR/内部FG全关；保留用户已保存设置。更新双语README、Release Notes、组件与源码构建说明。沿用原七运行文件及patched FFmpeg，新增用户指定NeuralScreen1.8.2组件DCC0DC24…，与RTX40社区版均为HashMismatch。未修改DLL、驱动入口或默认驱动设置。

## 构建与测试

- `out/release-1.1.0-build.cmd`（沿用现有构建入口）通过；资源与ProductVersion=1.1.1，EXE SHA256 `4C7144A6F3160AF9A0B6446F40A7CC7A7E36AB65B76F9E9ADA9613FEBBF7FF99`。
- 产品代码基线831432f已通过本轮真实RTX5070 NR162次Evaluate、同进程三运行版本切换与四张4K输出、默认全关、预设/架构/UI合同及delivery23/23（47.26秒）。发布仅修改版本资源、说明、包清单和便携验收脚本，详见[实施记录](../reports/RTX30_NR_AND_SAFE_DEFAULTS_2026-09-14.md)。
- `out/audit-release-1.1.1.py`：三ZIP路径、完整清单、逐文件大小/SHA256、源码排除、八运行文件/两HashMismatch及patched avcodec身份全部通过。证据logs/release-1.1.1/archive-audit.json。无SDK、凭据、日志、个人配置、测试媒体进入便携包；源码依赖不含专有二进制。
- 独立解压后运行 `scripts/acceptance/portable-smoke.ps1 -PackageDirectory out/releases/1.1.1-verify/Veyra-1.1.1-win64-portable -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-1.1.1/portable-smoke -CaseSeconds 15`。结果追加如下。

RTX30实卡、PS5/采集实卡新回归、长时压力及Smooth Motion叠加未执行。不能把5070兼容适配成功写成30系全型号成功。

独立便携验收7/7通过，各约15秒：empty、baseline821帧、RTX40社区548处理/546生成、原版721/697、Video SR710/686、fresh-defaults824帧且NR/NVOF/FG均0、新Ampere725处理帧且真实NR执行。临时移走manifest、隔离PATH后仍通过；FFmpeg及三NR模块路径确认在包内。结果logs/release-1.1.1/portable-smoke/result.json。包内双语README/Release/组件/构建说明与当前源码逐项一致，git diff --check通过。

## 资产

实际执行 scripts/package-portable.ps1 -Version 1.1.1 -Root . -OutputDirectory out/releases/1.1.1 -BuildDirectory out/remoteplay/product-repair；package-remoteplay-source.py --version 1.1.1；package-ffmpeg-source.py --version 1.1.1 --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/ffmpeg-ps5-slices-source。各源码脚本使用out/releases/1.1.1中对应文件为--output。

| 资产 | 字节 | SHA256 |
| --- | ---: | --- |
| Veyra-1.1.1-win64-portable.zip | 421715775 | 17D1F9C6A56043014E62F598AB1C6DA492DF5BC040168B42AA9D836594951D6A |
| Veyra-1.1.1-RemotePlay-source.zip | 143745956 | 663D4030A457882CF50D1454780CD0F8EFE5422583A6038801CAEE70AFF3E0B6 |
| Veyra-1.1.1-FFmpeg-source.zip | 23253198 | 72945C1D42829C86ACAF886C731E5E0F210CFA2B56A18C1C16D138D22A69A0A9 |

各附.sha256，共六资产；ZIP分别100、17714、10451文件。FFmpeg avcodec保持0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。Runtime完整身份与来源见RUNTIME_COMPONENTS_1.1.1及包内manifest；许可证随包保留，MIT应用许可证不冒充社区NR再分发授权。

## 发布步骤

验收完成后快进合入main，提交并推送main/v1.1.1；先创建草稿上传六资产，逐项核对服务端size与digest，再公开为latest。实际发布结果另记WORKLOG，不改动已经核实的压缩包。

完成：main与v1.1.1指向发布提交146f035；2026-09-14T07:10:02Z正式公开latest，Release ID 388201201，draft=false、prerelease=false。六资产均uploaded，发布前已逐项匹配服务端size/SHA256；发布后latest接口再次确认完整资产。地址https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.1.1 。证据logs/release-1.1.1/remote-assets-verified.json和published-release.json。草稿按tag读取返回404，改按列表获得的release ID查询成功；未重复创建或更换资产。
