# 1.0.1 发布记录

用户授权将采集音频修复发布为1.0.1。修复提交79734ff、用户日志复核eabf3c7；施工分支codex/release-1.0.1，合并GitHub main的用户README编辑77c2c04，保留其删除，仅补齐剩余details标签配对。

版本资源、双语README、Release Notes、组件/构建说明更新。七个运行组件身份沿用1.0.0、社区NR保留HashMismatch；FFmpeg使用实际slice补丁版，源码资产保留固定Chiaki及全部补丁/许可。未以反馈者旧版日志冒充新版验收。

所有生成物在out/releases/1.0.1及logs/release-1.0.1，不进入Git。

## 构建与验证

- 修复本体79734ff的完整构建100步、采集合同38 PASS、PCM/WASAPI同步16 PASS、抖动和设备恢复回归通过；delivery 23/23，53.123279秒，证据logs/delivery/c7ecf48ee7ae44a99cc3eb6f971d61fc/result.json。上述为版本资源更新前的产品代码测试，本轮未伪称重跑整套delivery。
- 版本更新后运行`cmd.exe /c out\gpu-dis-build-all.cmd`：配置、版本资源编译通过，但用户仍运行当前EXE，链接报LNK1104。未停止用户进程；从Ninja `-t commands veyra.exe`提取实际链接命令，仅改输出EXE/implib/PDB到out/release-1.0.1-bin，使用相同对象、库和新RC资源完成链接（out/release-1.0.1-link.cmd）。FileVersion/ProductVersion均1.0.1，EXE SHA256：073B72E2D6C44045036684B115CEA99F54FCD10F52D4BA4FA79C191C54DA94BE。当前已打开的桌面进程未被替换。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 1.0.1 -OutputDirectory out/releases/1.0.1 -BuildDirectory out/release-1.0.1-bin`通过。七个增强运行文件身份/许可不变，社区NR仍HashMismatch；patched avcodec SHA256为0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。
- 使用`package-remoteplay-source.py --version 1.0.1`及`package-ffmpeg-source.py --version 1.0.1 --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/ffmpeg-ps5-slices-source`制作对应源码。输出路径均为out/releases/1.0.1。FFmpeg使用实际patched tree；Chiaki固定版本与补丁保留。
- 解压ZIP前验证路径、便携清单全文件大小与哈希、无额外文件；源码扫描无SDK运行二进制或用户媒体。首次扩展名扫描命中FFmpeg上游tests/ref/lavf-fate的六个`.mp4`文件，检查字节确认它们是约100字节的ASCII测试哈希/大小参考文本，不是视频，保留上游源码。结果logs/release-1.0.1/asset-verification.json。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/portable-smoke.ps1 -PackageDirectory out/releases/1.0.1-verify/Veyra-1.0.1-win64-portable -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-1.0.1/portable-smoke`：5/5 PASS，每项7.43–7.92秒。独立目录、精简PATH、manifest临时禁用后仍成功；FFmpeg/MSVC/增强模块实际从包内加载。
- 空窗口0帧，基线326帧；社区SR/NR/FG为228处理/205生成，原版SR/NR/FG为235/211，Video SR/NR/FG为229/205。实际NR Create Feature18 result=0x1、handle非空、seh=0；VSR op=0 result=0x1 seh=0，229次NR执行、225次NVOF执行。完整结果logs/release-1.0.1/portable-smoke/result.json。
- 最初验证脚本因上游文本参考文件被后缀扫描拒绝，随后启动检查因未解压而退出；修正检查后才执行上述通过的验证。未把失败当成功。

## 资产

| 文件 | 字节数 | SHA256 |
| --- | ---: | --- |
| Veyra-1.0.1-win64-portable.zip | 303913506 | B86C093D50CA50B4B1CC8D9E845BC1374CB4C199BE09AD5068B3A6D4A7F591B0 |
| Veyra-1.0.1-RemotePlay-source.zip | 143744092 | 0A82E0FAE9299BA2FB1DD0D67284775ADB47D1F2F287B0125C85610CA29C8CFF |
| Veyra-1.0.1-FFmpeg-source.zip | 23253197 | C95CA37A595BF8CD9EA666B21EE33E6F52307D248E91503590D9E2B784D2C933 |

每个ZIP附独立.sha256。包内分别99、17714、10451个文件（包含顶层说明/构建元数据）。发布只上传这六个资产，不上传本地audit、日志或展开目录。发布结果在完成后追加。

## 公开发布结果

main与标记v1.0.1已推送到Likely7/Veyra-NRVideo；标记指向c0cba4b042ad62cc3331d7136c11a4280f566a5d。使用`gh release create v1.0.1 --repo Likely7/Veyra-NRVideo --verify-tag --title "Veyra 1.0.1" --notes-file docs/RELEASE_NOTES_1.0.1.md --draft`上传上述六资产；GitHub API逐项确认uploaded、大小及SHA256全部与本地一致，再使用`gh release edit v1.0.1 --repo Likely7/Veyra-NRVideo --draft=false --prerelease=false --latest`公开。

2026-09-13T14:53:27Z发布，Release ID 387931939，公开latest接口确认v1.0.1、draft=false、prerelease=false、六资产。地址：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.1 。证据logs/release-1.0.1/github-draft.json及github-published.json。本段以发布后文档提交补记，不移动版本标记；1.0.0资产未修改。README本地链接及Git改动文件扫描通过，工作树无SDK或运行二进制新增。

## 边界

未占用反馈者的实体采集卡、未进行声学端到端延迟验收；请求10ms并不保证驱动遵守或全部设备均解决。下一步为反馈者新版测试，结合inputBlockMs、inputIntervalMs、skewMs和各队列确认。如果小块已生效仍持续落后，继续排查设备PTS/同步重锚。
