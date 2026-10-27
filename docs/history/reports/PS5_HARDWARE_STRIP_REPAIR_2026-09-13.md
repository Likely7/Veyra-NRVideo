# PS5 硬解细条画面修复记录

## 问题与定位

用户报告 PS5 H.264 SDR 硬解只有顶部细条，切换软件解码恢复。基线 83f90ba，施工分支 `codex/ps5-decoded-frame-strip`。本次不发布。

1. 原始日志帧尺寸/图尺寸/显示窗口均完整，不能据“解码成功、60 fps”判断画面正确。
2. 普通 1080p 文件软硬解全图逐像素比较误差 0，实际窗口显示完整；不能代表 PS5 多切片码流通过。
3. 实际已配对 PS5 诊断：硬解资源 1920×1080、NV12、单层、无裁剪、flags=0。硬解帧经 FFmpeg 回读再转色仍为细条；GPU 导入与该回读误差 0。该检查只排除导入改变画面，**不能证明解码正确**。
4. 将同一个真实 PS5 压缩 AU 分别交给产品的软/硬解：软件完整，硬解细条，全图平均通道误差 167.459/255。日志逐条报 32..68 个切片超出 `MAX_SLICES=32`。FFmpeg n9.0.1 的 `d3d12va_h264.c` 直接使用 `h264dec.h` 的 32 长度数组，超限返回 ERANGE；最终收到 AVFrame 仍不能证明所有 slice 被解码。
5. 独立发现硬解 SRV 槽位被下一帧覆盖。两张黑/白图在 GPU gate 后连续排队，旧实现第一张全图 6144 个 RGB 通道错误，单层/三层纹理均复现；按现有两槽 fence 生命周期轮转描述符后错误归零。该修复不是 PS5 多切片问题的替代。

## 修改

- `EnhanceGraph.cpp`：保留软件上传 SRV 槽 0/1；硬解使用 3/4、5/6 两组，与已有 `uploadFences_`/`hardwareInputFrames_` 同生命周期；通过既有 CPU descriptor staging 写入，不新增逐帧等待或 GPU→CPU 回读。
- `RemotePlaySource.cpp`：首张真实硬解输出记录可见尺寸、资源尺寸/格式、层数、slice、crop 和 fence，不记录凭据。
- `scripts/ffmpeg/ps5-h264-slices.patch`：H.264 `MAX_SLICES` 从 32 扩为 256（保持内部按位环索引所需的二次幂）。仅修改外置开源 FFmpeg 源码并重编译，不修改 NVIDIA 二进制。新构建必须沿用原 DLL 编译配置。
- 新诊断 `HardwareImportImageTests` / `HardwareImportFixtures` 检查全图及两帧并发 SRV；`Ps5HardwareImageTests --last-paired-ps5 <输出前缀>` 显式使用最后一个本机 DPAPI 配对，只做短时连接和同码流软硬解图像比较。不会改配对或让主机休眠。诊断图和日志只在忽略目录。

## 已执行证据

- `cmd /c out\remoteplay\build-hw-import.cmd`，普通软/硬解四帧全图误差 0。初次测试程序缺 AVFrame include 编译失败，补齐后成功。
- `veyra_hw_import_image_tests.exe logs/ps5-p1-1080p30-long.mp4 logs/hw-import-after`：4 帧误差 0，单层/三层两帧排队均错误通道 0，退出 0。旧代码的失败保留于 `logs/hw-import-fixture-before.log`。
- `cmd /c out\remoteplay\build-ps5-image.cmd`：初次缺完整设备/命令环类型 include 失败，补齐成功。
- 实际 PS5：`logs/ps5-live-import.log` 为 GPU 导入/回读一致但画面错误；`logs/ps5-live-decode.log` 为同码流软件正确/硬解错误，诊断退出 12。**这两项不记为 PS5 修复通过。**
- FFmpeg 独立源码 `C:\veyra-deps\ffmpeg-ps5-slices-source`，构建 `C:\veyra-deps\ffmpeg-ps5-slices-build`，安装目标 `C:\veyra-deps\ffmpeg-ps5-slices-installed`。原始 vcpkg 源码/构建不覆盖。原 DLL 配置见 `logs/ps5-ffmpeg-original-configuration.txt`。
- FFmpeg 首次 configure 因 MSYS link.exe 抢先于 MSVC link.exe 失败（`logs/ps5-ffmpeg-build.log`）。修正 PATH 后重试，结果待追加。

## 最终验证与交付

- 外置 FFmpeg `configure + make -j8 + make install` 成功，`logs/ps5-ffmpeg-build-retry.log`。功能选项取自原 DLL（LGPL、无额外编解码库），仅安装/归档工具路径改变。没有覆盖原 vcpkg 构建树。
- 同码流实机复测：`veyra_ps5_hw_image_tests.exe --last-paired-ps5 logs/ps5-live-patched`，`logs/ps5-live-patched.log`：D3D12VA、1920×1080、fallback=false；软件/硬件全图误差 **0**，退出 0。已查看硬解输出 PNG：完整《羊蹄山之魂》装备页，细条消失；无旧的超切片警告。
- `cmd /c out\remoteplay\build-extra-delay.cmd` 产品构建成功，`logs/ps5-strip-product-build.log`。新 5 个 FFmpeg DLL 已放入 `out/remoteplay/product-repair` 及默认开发依赖前缀 `C:\veyra-deps\installed\x64-windows\bin`，避免下次构建复制回旧版。原 DLL 保留在 `out/remoteplay/ffmpeg-baseline-backup`；原始 `.lib`/公开头文件 ABI 不变。
- 新 avcodec-63.dll SHA256：`0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`。完整 5 文件身份/源码头文件/补丁哈希与原配置在 `logs/ps5-ffmpeg-local-build.json`、依赖前缀 `share/ffmpeg/veyra-local-build.json`。NVIDIA DLL 未变。
- 最终普通文件全图/并发纹理回归：`logs/hw-import-final.log`，4 帧误差 0，4 个并发黑白图错误通道 0。
- `veyra_hdr_color_tests.exe`：8 组 P010/planar、limited/full、HDR 原生/转SDR GPU色值及呈现全部通过（`logs/ps5-strip-hdr.log`）。
- `veyra_hdr_color_tests.exe logs/ps5-main10-fixture.mp4`：Main10 软/硬解 × NR单独/标准 SR→NR→FG/低延迟 NR→SR→FG，共6组，每组12帧真实NR，组合模式含12次SR及11张生成帧；退出0，`logs/ps5-strip-main10-combo.log`。
- 更新后主程序硬解文件 NR 连续40秒：`VEYRA_TEST_FILE_HW_DECODE=1`, `veyra.exe --smoke-seconds 40 --smoke-view professional logs/ps5-p1-1080p30-long.mp4`；`logs/ps5-strip-final-nr-fg.log`：1135 源帧、1135次NR、1134次NVOF、failed=false。该次FG未开启（文件名不代表测试功能）；NR CreateFeature18=0x1、SEH=0。未在当前修复后主UI做持续PS5手柄/音频验收，实机证据为共享产品Source/Graph的同AU全图比较。
- `veyra_ui_contract_tests.exe` 通过（模式/旁路、384 DPI布局组合、PCM音量及设置存储），`logs/ps5-strip-contract.log`。
- `git apply --reverse --check <ps5-h264-slices.patch>` 对外置已补丁源码通过。最初零上下文补丁的普通 apply 检查失败，已补上下文；最终补丁源码内容不变。原本的中间源码ZIP不作为最终交付。
- 对应源码打包验证成功：`scripts/package-ffmpeg-source.py --prefix C:/veyra-deps/installed/x64-windows --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/ffmpeg-ps5-slices-source --output out/remoteplay/ffmpeg-slices-corresponding-source-final.zip --version 0.0.5`。10442源码记录、LGPL，SHA256 `694E19F42CBC7C210619532B3E101E184522E7CC084491C615918C6DA205B339`；仅本地验证，未上传或替换已发布资产。

结论：本次 H.264 硬解细条故障已通过真实PS5同码流对照修复。用户可重开桌面“Veyra PS5 测试版”，选“自动·优先硬解”或“D3D12VA 硬件解码”重新连接；保留软件解码选项。未发布；实际 PS5 HDR 主机输出、长时串流/手柄/音频仍由用户继续验收，不以文件Main10回归冒充实机HDR。
