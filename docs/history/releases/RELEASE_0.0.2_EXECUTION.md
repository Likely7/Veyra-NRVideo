# 0.0.2 发布执行记录

2026-09-11。用户授权把当前源码、0.0.2免安装包和中英文文档发布到 `Likely7/Veyra-NRVideo`，随后明确要求去掉软件运行时校验、允许替换DLL。旧Loop不参与本次流程。

发布完成：源码提交`a69a9df9fb9002fbd462e63c4d3d668828bbfd79`及`v0.0.2`标签已推送。GitHub于`2026-09-11T03:22:48Z`公开发布，非草稿，四项Release资产均为uploaded；每项远端digest与大小均和本机匹配。链接：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.2 。证据`logs/release-002-github-published.json`。此后仅补充发布记录，未替换EXE/资产或移动版本标签。

## 交付范围

- 包含此前未推送的采集调度、音频恢复、状态统计、XeSS/AMD光流、UI和FRUC移除提交，以及本次实卡无FG按GPU-ready呈现修复。
- EXE资源版本0.0.2，新增中英文README、使用教程、构建说明、组件说明和Release Notes。
- NVIDIA运行目录支持 `runtime/experimental`；XeSS使用 `runtime_local/intel/experimental`。取消XeSS固定哈希/签名门锁，不添加manifest启动检查；NGX诊断不再把替换DLL标成原始固定哈希。
- XeSS受限加载路径加入EXE目录，以找到随包Microsoft CRT，保留绝对路径、导出函数和初始化结果检查。
- 软件允许用户替换DLL；发布者仍检查默认发行文件。NVIDIA NGX/驱动内部的加载策略不是Veyra可取消的校验。
- FFmpeg改为在共享增强库使用前解析，修复全新构建目录缺少头文件路径；build脚本支持独立目录并复制FFmpeg依赖。
- 打包使用显式文件列表，包含6个增强运行DLL、5个FFmpeg DLL、3个Microsoft CRT DLL、自有shader、许可证和说明。FRUC、AMD NR、深度模型未打包。

## 最终资产

| 文件 | 大小 / SHA256 |
| --- | --- |
| Veyra-0.0.2-win64-portable.zip | 185295449 bytes；D1A6D37D61D1E61F8D700EC534DFA718F1909C6781F1AF47F1CFD4955AE4E2C3 |
| Veyra.exe | A07B73C2CD946AD50FD8516A51DB3B0CD6759945BB85E82D65F0D8CEB18B6170 |
| Veyra-0.0.2-FFmpeg-source.zip | 23248643 bytes；04842030E474C2F9FBA8326D61DD74427A2F0FFABC3F5D6EFD25C141AFF31F6C |

本机资产目录 `out/releases/0.0.2-final`，各ZIP附独立`.sha256`。包内50个文件，`package-audit.json`在ZIP外。6个默认增强DLL的来源、SHA256、版本和有效签名见 `RUNTIME_COMPONENTS_0.0.2.md` 及包内manifest；manifest不参与用户运行时拒绝判断。

FFmpeg资料包含本机n9.0.1补丁后源码、与所发SPDX匹配的vcpkg port/patch、许可证、逐文件hash，以及直接调用所发DLL查询的编译配置与LGPL版本。没有使用另一个clip-tool构建缓存冒充所发库配置，没有附带NVIDIA SDK。

## 实际验证

所有单次检查均小于300秒。完整源码全新目录构建成功；最终CRT加载修正后增量构建成功。使用RTX5070，驱动32.0.16.1656。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/build/release-0.0.2-final
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 0.0.2 -BuildDirectory out/build/release-0.0.2-final -OutputDirectory out/releases/0.0.2-final
python scripts/package-ffmpeg-source.py --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/vcpkg/buildtrees/ffmpeg/src/n9.0.1-1250e74153.clean --output out/releases/0.0.2-final/Veyra-0.0.2-FFmpeg-source.zip
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/portable-smoke.ps1 -PackageDirectory "$env:TEMP/Veyra portable 002 final/Veyra-0.0.2-win64-portable" -InputFile loop/local/fixed_clips/test_av_1080p.mp4 -OutputDirectory logs/release-002-portable-verified
python scripts/acceptance/ui-fg-backends.py "$env:TEMP/Veyra portable 002 final/Veyra-0.0.2-win64-portable/Veyra.exe" --portable
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/release-0.0.2-final -PlayerExe "$env:TEMP/Veyra portable 002 final/Veyra-0.0.2-win64-portable/Veyra.exe" -PortablePlayer
```

- CPU契约81项通过：`logs/scheduler-repair-20260910/release002-contract.result.json`。
- 最终delivery23项通过，42.929秒：`logs/delivery/130f307d2be14370876e63b6a9f1c6f9/result.json`。quality probe使用新构建，播放器/导出使用项目目录外解压的最终EXE；包含NR/NVOF、原生4K正确性、播放/暂停/seek、4K图片保存、H264/HEVC音轨与24帧/120fps短导出及取消检查。
- 便携测试清空开发PATH、从临时目录启动、临时移走所有manifest：空启动、基础播放、DLSS SR+NR+FG、Video SR+NR+FG四项通过。证据 `logs/release-002-portable-verified/result.json`。增强两项分别记录228/235个源帧、225/232个生成帧，不能用启动过程的这些累计值冒充稳态FPS。
- NR Feature18、DLSS SR、DLSSG Create返回`0x1`/SEH0；有效执行及GPU计时见对应stdout。保存图像为1080p基线和两张3840x2160增强图，像素标准差均大于74，已排除黑图。
- 最终XeSS→DLSS→关闭的真实专业面板操作通过，三个窗口尺寸无控件重叠；证据 `logs/continuation-repair-20260910/ui-fg-1789096638666386100/result.json`。XeSS Create/Init成功，未宣称所有设备画质验收。
- 模块路径证明FFmpeg、NR、DLSS SR/FG来自解压目录。本机VSR由NGX驱动实现，未观察到包内`nvngx_vsr.dll`进入模块列表；其Create成功、GPU SR计时非零。不声称本机实际使用的是包内VSR版本，也不从驱动提取新二进制。

## 失败与修正

1. 常规输出目录链接失败LNK1104：用户正开着旧EXE，保留该进程并改用独立构建目录。
2. 全新构建FFmpeg头文件缺失：修正CMake依赖解析顺序；最终174目标构建通过。日志 `release-002-build-isolated.log` / `release-002-build-final.log`。
3. 第一候选包缺少XeSS所需msvcp140/vcruntime140_1：补齐并将EXE目录加入受限依赖查找；最终构建 `logs/release-002-build-portable-crt.log`。
4. 修改gate时误把quality probe工作目录也切到bin，导致其相对NGX配置找不到：仅便携播放器改变工作目录，保留原probe约定。失败 `logs/delivery/608b8f70c1b045eabf1ca1605b9b2f85`，后续通过不覆盖失败日志。
5. 便携UI测试环境变量大小写错误已修正。首次VSR模块断言错误要求包内DLL必须映射；核对真实NGX Create/GPU执行及系统模块后，明确允许驱动实现并记录真实来源。失败日志 `logs/release-002-smoke-final.log`，不把错误断言解释成VSR功能失败。

## 源码与验收边界

`git rev-list --objects HEAD`与对象大小/文件名审计未发现SDK、DLL、EXE、LIB、模型或开发压缩包。历史中有已移除的合成测试视频和仅JSON的深度依赖说明，不是SDK或模型。当前源码树无`third_party_local`/`runtime_local`二进制；新增`/runtime/`忽略项。只暂存审核过的源码、脚本与文档；Release资产不进入Git或LFS。

未执行新的实卡同源物理延迟对照、设备拔插/音画验收或长期压力测试；8K、所有显卡/驱动及用户任意替换DLL的兼容性不作通用保证。下一项产品验证由用户进行实卡验收。发布状态以GitHub的v0.0.2标签与Release资产为准，不以本文的本机候选记录冒充上传完成。
