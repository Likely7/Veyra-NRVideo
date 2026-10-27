# 0.0.3 发布执行记录

状态：已发布。用户2026-09-11授权发布当前版本到 `Likely7/Veyra-NRVideo`，更新源码、中英文README和完整便携包，版本0.0.3；保持0.0.2资产与标签不变。

源码提交 `83f6353803861307b7003213dc510a837b6f3966` 与注解标签 `v0.0.3` 已原子推送至nrvideo/main。Release ID `386798119`，公开时间 `2026-09-11T05:09:04Z`，`isDraft=false`、`isPrerelease=false`，latest接口返回v0.0.3。链接：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.3 。四个附件state=uploaded，远端digest及字节数全部与本机相符；GitHub README blob与该提交一致。此后仅补发布记录，不改EXE、资产或移动版本标签。

发布命令：`git push --atomic nrvideo HEAD:main refs/tags/v0.0.3`；`gh release create v0.0.3 --repo Likely7/Veyra-NRVideo --verify-tag --draft --title "Veyra 0.0.3" --notes-file docs/RELEASE_NOTES_0.0.3.md`；`gh release upload`上传两ZIP及其.sha256；比对后 `gh release edit v0.0.3 --repo Likely7/Veyra-NRVideo --draft=false --latest`。草稿阶段REST按tag查询返回404，改用`gh release view`取得草稿及资产并完成核验，未重复创建Release。

## 范围

收录当前NR双运行时、直播兼容实验开关、v10预设及MSVC/Ninja依赖修复。专业UI大改仅有方案，不标作已实现。捕获故障已由用户明确选用WGC恢复：本机OBS日志12:48:59自动选BitBlt，12:53:57手选WGC，用户反馈视频出现；据此README给正确捕获步骤，不追加软件提示、不宣称切换交换链解决该故障。

## 二进制与源码隔离

沿用0.0.2的六个增强运行文件、FFmpeg、Microsoft CRT、自有shader与许可证；额外包含本次用户指定并随当前版本发布的社区NR，路径 `runtime/experimental/nr-community/nvngx_dlssnr.dll`。社区文件SHA256 `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`、310.8.0.0、165840496字节、签名 `HashMismatch`；默认仍用签名有效的原版。

打包脚本按文件检查来源hash和签名状态，仅指定社区文件允许指定HashMismatch，不普遍放宽其余文件审计。运行时不恢复身份锁。社区版来源是用户指定的本机文件，未修改或重签名；详细许可证位置见 `RUNTIME_COMPONENTS_0.0.3.md`，不把官方许可证解释为社区文件通用授权。

仅Release包含运行二进制；源码、Git历史和LFS不包含SDK、运行DLL、模型、开发头文件/库或测试媒体。FFmpeg对应源码资料单独作为Release附件，按现有LGPL发布流程。

## 计划与证据

1. 同步版本/README/Release Notes/组件清单，构建独立 `out/build/release-0.0.3`，不覆盖正在运行的用户进程。
2. 显式清单打包，生成SHA256和审计记录，在项目外解压并进行清洁PATH、无manifest启动与真实GPU、NR运行版本、显示开关和导出回归。
3. 审计Git暂存与历史，提交源码，push到nrvideo/main并创建v0.0.3标签；上传草稿Release所有附件并比对digest/大小，再公开发布。

## 最终候选验证

全新目录174目标构建exit0，日志 `logs/release-003-build.log`。最终EXE产品版本0.0.3，SHA256 `D9C7DCCEA7B1538066CC648D7C8E0D5513439A367E0999A70E614556FAB6395C`。CPU合同81项通过、预设42组迁移及v1升级/完整字段往返通过。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/build/release-0.0.3
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 0.0.3 -BuildDirectory out/build/release-0.0.3 -OutputDirectory out/releases/0.0.3
python scripts/package-ffmpeg-source.py --version 0.0.3 --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/vcpkg/buildtrees/ffmpeg/src/n9.0.1-1250e74153.clean --output out/releases/0.0.3/Veyra-0.0.3-FFmpeg-source.zip
out/build/release-0.0.3/veyra_repair_preset_tests.exe logs/release-003-presets.v1
out/build/release-0.0.3/veyra_repair_contract_tests.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/portable-smoke.ps1 -PackageDirectory "$env:TEMP/Veyra portable 003 verified/Veyra-0.0.3-win64-portable" -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-003-portable
python scripts/acceptance/ui-nr-runtime.py "$env:TEMP/Veyra portable 003 verified/Veyra-0.0.3-win64-portable/Veyra.exe" "$env:TEMP/Veyra portable 003 verified/Veyra-0.0.3-win64-portable/runtime/experimental/nr-community/nvngx_dlssnr.dll"
python scripts/acceptance/ui-broadcast-mode.py "$env:TEMP/Veyra portable 003 verified/Veyra-0.0.3-win64-portable/Veyra.exe" --xess
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/release-0.0.3 -PlayerExe "$env:TEMP/Veyra portable 003 verified/Veyra-0.0.3-win64-portable/Veyra.exe" -PortablePlayer
```

- 包外临时目录解压后，清空开发PATH并临时移走manifest，空窗口、基础播放、社区SR+NR+FG、原版SR+NR+FG、VideoSR+NR+FG全部exit0，`logs/release-003-portable/result.json`。确认两种NR实际模块路径分别指向包内原版和社区子目录。NR/DLSSG Create `0x1`、SEH0，有效执行和保存图像非黑；三张增强图均3840x2160，RGB标准差至少75。VSR沿用驱动NGX实现，不声称包内nvngx_vsr.dll在本机已映射。
- 实际UI原版/社区往返及缺文件回退PASS，`logs/nr-runtime-switch/1789102928174822400/result.json`。实际XeSS开关与直播模式往返、暂停、全屏、总增强关闭和两尺寸布局PASS，`logs/broadcast-mode/1789102958375091400/result.json`。
- 最终解压EXE参与delivery，23项PASS，47.474秒，`logs/delivery/cb1f9a5f77524603a6bd29d1d0f630e7/result.json`。覆盖NR/NVOF、原生4K正确性、播放暂停seek、图片保存、H264/HEVC含音轨和帧数/时间线、取消。没有将这些测试当作外部录制成功证据。
- ZIP重开后检查所有51个manifest条目的大小和SHA256，成员集合恰为51个条目加package-manifest，共52文件；无额外文件。打包审计 `out/releases/0.0.3/package-audit.json` forbiddenFiles=0。

| Release资产 | 大小 / SHA256 |
| --- | --- |
| Veyra-0.0.3-win64-portable.zip | 298999496 bytes；`3A37336BF09177A8224AA5F15C2FB9333AF5657F37F3B86411DD4AB0BC9E1D7B` |
| Veyra-0.0.3-FFmpeg-source.zip | 23248643 bytes；`82D055AF7D335269EC87E859BBEC7C0973C65B72E7C7EF23A6F9A69335624D4D` |

两ZIP另附各自.sha256文件。FFmpeg源码资料10442条，已核对vcpkg SPDX与所发DLL的LGPL配置，不含NVIDIA SDK。Git已跟踪文件和历史对象按名称/体积审计未发现运行DLL/SDK/模型/开发ZIP；历史存在已移除的合成测试MP4，不是本次新增发布文件。

单次测试均小于300秒；本轮发布候选未出现构建/回归失败，之前功能开发失败保留在对应方案。未新增RTX40实机、实卡端到端延迟、外部录制节奏或长期压力测试。不会把本机5070通过扩展成全系列验收。
