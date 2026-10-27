# NR 双运行时切换

用户提供 `E:/Ai/mg/DLSSNR-DLL-Options-310.8.0.0/Community-RTX40-RTX50/nvngx_dlssnr.dll`，要求在软件中选择RTX50原版或RTX40/50社区兼容版。原版保留默认，专业模式提供手动选择；不根据尚未实测的硬件声明自动切换或扩大兼容性承诺。

## 输入与边界

- 原版：310.8.0.0，165840496字节，SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，NVIDIA签名有效。
- 用户指定社区版：同版本与大小，SHA256 `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`，Authenticode `HashMismatch`。这不是有效签名的NVIDIA原件；用户明确选用该文件进行本机接入，不修改或重签名它。
- 本机RTX5070只验证两版加载、真实NR输出、切换和回退。RTX40实际效果/稳定性未由本机验证。暂不修改已发布0.0.2资产，不把社区DLL加入默认发布白名单或Git。

## 实施

1. 设置新增Original/Community运行时枚举，旧预设默认Original，新预设保存选择。三入口共用Graph，图片分块和导出同样携带该设置。
2. 原版目录保持不变，社区版放在其`nr-community/`子目录，均保留文件名`nvngx_dlssnr.dll`。仅NR适配器切换绝对路径，SR/FG/VSR和NGX配置不迁移。
3. 专业模式加入“NR运行版本”选择，沿用现有玻璃选择器和滚轮只滚页规则。运行中先排空GPU/呈现、释放旧Feature/DLL再重建；缺文件、缺API或初始化失败通过设置事务回退。
4. 定向测试覆盖预设迁移、两版实际Create/Evaluate与非黑输出、往返切换、缺文件回退，以及社区版视频导出。单次测试上限300秒。执行必要delivery并更新WORKLOG。

## 交付状态

本机接入和针对性验证完成。专业模式的增强页新增“NR 运行版本”：默认“NVIDIA 原版 · RTX 50”，可选择“社区兼容 · RTX 40/50 实验”。实时状态显示实际生效的版本，关闭NR时显示未运行。选择保存到v9预设，旧v1-v8预设默认原版。切换需要重建管线，存在短暂停顿；初始化失败会回退，不能称为无停顿热切换。

初次交付只更新了 `out/build/release-0.0.2-final/veyra.exe`，遗漏常用入口，用户截图确认打开的仍是旧程序。后续已在旧进程退出后清理标准构建目标并完整重建 `out/build/x64-release/veyra.exe`；现在根目录 `Veyra.cmd` 可直接打开含双运行时选择器的新版。本轮没有更新GitHub或0.0.2发布资产。

入口补交付：`cmake --build out/build/x64-release --target clean`，随后 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release` 完整174目标exit0，日志 `logs/nr-runtime-default-entry-build.log`。标准入口EXE SHA256 `4AB989BC9223F8EE821CF449CE7A0F2DE14C49F7C3C08E1ECC4299DC411E88D4`。用该EXE再次运行 `python scripts/acceptance/ui-nr-runtime.py out/build/x64-release/veyra.exe runtime_local/nvidia/nr-community/nvngx_dlssnr.dll`，原版/社区往返、缺文件回退和两种窗口布局均PASS，结果 `logs/nr-runtime-switch/1789100030646506400/result.json`。下面隔离构建的完整GPU/导出证据保留，不将不同路径EXE哈希混为一谈。

### 修改范围

- `EnhancementSettings.h`、`EnhanceGraph.h/.cpp`、`DlssNrRuntimeAdapter.cpp`：枚举、独立目录、NR加载和设置合同。
- `EngineController.cpp`、`EngineControllerImage.cpp`、`VideoExportJob.cpp`：播放/采集共享设置事务、分块图片、视频导出的运行版本传递与回退。
- `PresetStore.cpp`、`RepairPresetTests.cpp`：v9持久化、旧版迁移、无效枚举拒绝。
- `SettingsWindow.cpp`、`LiveStatusPanel.h`、`AppShell.cpp`：玻璃选择器、实际状态和 `--nr-community` / `--nr-original` 参数。
- `CMakeLists.txt`：修复本机MSVC/Ninja头文件依赖识别；`scripts/acceptance/ui-nr-runtime.py`：实际控件切换与回退检查。
- `AGENTS.md`、本方案和 `WORKLOG.md`：用户指定文件授权、身份与交付证据。

### 构建与验证

最终EXE SHA256：`6C3723B1E0F3E10BA456905E39F94824A7E650C5E10E28545201DFCE73A75976`。以下均在本机RTX5070完成，单次短测少于300秒。命令从项目根目录运行；`$exe` 表示上方新版EXE绝对路径，`$clip` 表示 `loop/local/fixed_clips/test_av_1080p.mp4` 的绝对路径，旧素材目录名不代表恢复Loop流程。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/build/release-0.0.2-final
out/build/release-0.0.2-final/veyra_repair_preset_tests.exe logs/nr-runtime-preset-test.v1
out/build/release-0.0.2-final/veyra_repair_contract_tests.exe
python scripts/acceptance/ui-nr-runtime.py out/build/release-0.0.2-final/veyra.exe runtime_local/nvidia/nr-community/nvngx_dlssnr.dll
& $exe $clip --nr-community --nr --sr --fg --export-out logs/nr-community-export.mp4 --max-frames 12
& $exe $clip --nr-community --nr --no-sr --no-fg --smoke-dual --smoke-job --smoke-dual-export logs/nr-community-worker-final.mp4 --smoke-seconds 22
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/release-0.0.2-final
```

- 清理该隔离构建目录的目标后完整重建174个目标成功，日志 `logs/nr-runtime-clean-build.log`。原用户进程未终止。
- CPU合同81项、预设36组迁移及当前字段往返通过；预设测试使用新临时文件，重跑须换未存在的路径。
- 社区版播放：`logs/nr-community-second.stdout.log`，Init_Ext/CreateFeature18为 `0x1`、SEH0，239次NR、236次NVOF；保存1920x1080非黑JPEG。
- UI：`logs/nr-runtime-switch/1789099278517146300/result.json` 为PASS。实际弹出选择器完成社区→原版→缺社区文件拒绝并恢复原版→社区→原版；源保持打开，各成功切换Create为 `0x1`。1280x900、1040x540均能滚动访问，滚轮不改选项，视频像素非黑。
- 社区SR+NR+FG导出：`logs/nr-community-export.stdout.log` 和MP4。3840x2160、H264+AAC、120fps、24帧；12源帧、11生成帧、1显式CFR补齐帧，不能把补齐帧写成生成帧。
- 独立导出进程：`logs/nr-community-worker-final.stdout.log`、`logs/export-worker-24872.log`。子进程明确加载社区目录，Init/Create `0x1`、SEH0，导出120源帧且前台继续播放，冻结参数不随前台修改改变，exit0。使用ffprobe检查输出视频与音轨。
- 最终delivery23项全部通过，43.129秒；`logs/delivery/e39733bf752b414c9fbc06f4c08ef230/result.json`、`logs/nr-runtime-delivery.log`。此项包含原版基线，不替代社区版专项结果。

### 失败与修正

初次增量构建后的空窗口启动崩溃发生在加载NR之前。调试定位到 `exportJob.poll()` 的wstring析构访问冲突：本机CMake缓存的中文 `/showIncludes` 前缀乱码，Ninja记录头文件依赖为0，设置结构新增字段后 `ExportJobManager.cpp` 没有重编译，造成混合ABI。通过实际 `/c /utf-8 /showIncludes` 编译探针提取原始字节前缀，并完整重建解决；最终该目标记录13个依赖，包含 `EnhancementSettings.h`。调试证据 `logs/nr-ui-exception.log`。强设VSLANG因未安装英文编译资源无效，`/EP`探针和实际编译前缀不同，也未采用；失败日志保留在 `logs/nr-runtime-dependencies-build.log`、`logs/nr-runtime-prefix-build.log`。

UI脚本两次早期失败分别是短窗未滚动到控件、日志缓冲尚未刷新，修正为滚动可达和等待真实Applied记录后通过。首次独立导出测试漏传 `--smoke-dual`，没有触发worker，exit1；补齐参数后上述最终测试通过。没有将这些失败解释为社区DLL不兼容。

### 未验证与下一步

RTX40硬件的Create/Evaluate、效果、性能和稳定性未执行；本次也未新增实体采集卡/长期测试。图片分块设置传递已接入但未做社区版超大图片专项。下一步唯一验收为RTX40用户使用新版并选择社区兼容运行版本进行实测，收集真实日志。本机通过不代表整个40系列兼容，也不代表社区文件具有有效NVIDIA签名。本次DLL仅留在gitignore本地目录，不提交、不自动发布。
