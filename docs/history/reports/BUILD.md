# Build / 构建

普通用户下载Release免安装包即可。以下用于开发者构建，不需要把SDK提交到Git。

## Current workspace policy / 当前本机目录

2026-09-18 起，本机所有新构建、日志、临时文件和测试包放到 `E:/项目/Veyra/`。当前隔离源码为 `E:/项目/Veyra/worktrees/video-hdr-20260918`，构建为 `E:/项目/Veyra/build/video-hdr-20260918`，进程级 `TEMP`/`TMP` 为 `E:/项目/Veyra/tmp/video-hdr-20260918`。SDK、运行库和既有 patched FFmpeg 继续引用原绝对路径，不进入源码 Git。

下文 `out/`、`logs/` 和 1.3.0 命令是历史参考，不能原样作为本机新任务默认命令。执行脚本须显式传 `-BuildDirectory`、`-LogDirectory`/`-LogPrefix`；脚本仍硬编码产物位置时先修正再运行。隔离 worktree 不含忽略的 SDK，不能依赖 `build.ps1` 自动探测便宣称构建了完整播放器。需显式配置 NGX、FSR、XeSS、NVENC 和 Remote Play 的实际依赖路径，并保留 FFmpeg slice 补丁。

本轮可复现命令与结果随施工记录在 [Video HDR 计划](../plans/VIDEO_HDR_FSR41_EXECUTION_PLAN_2026-09-18.md) 和 [WORKLOG](../../WORKLOG.md)；尚未运行的命令不作为构建成功证据。

## Current release / 当前版本（2026-09-18）

Version 1.4.1 merges the post-1.4.0 player, scheduling, RTX30/40 FG and export repairs plus the AverMedia initialization repair. The release build uses `out/build/scheduling-audit-20260918`, RemotePlay ON and `C:/veyra-deps/ffmpeg-ps5-dav1d-installed`. See [release execution](../releases/RELEASE_1.4.1_EXECUTION.md), [corresponding source instructions](../releases/REMOTEPLAY_BUILD_1.4.1.md) and [runtime identities](RUNTIME_COMPONENTS_1.4.1.md).

NVENC requires **nv-codec-headers tag n13.0.19.0**, staged at `third_party_local/nvidia/nv-codec-headers-13.0`, or an equivalent path supplied as `VEYRA_NVENC_HEADERS_ROOT`. The API structures must match this ABI; changing only a runtime version number is not an ABI fallback. Do not stage a newer header revision under this name.

## Previous 1.3.0 build reference

Version 1.3.0 integrates the repairs through `14b6ee0`. The release build uses `out/build/audio-continuity-repair-20260915`, RemotePlay **ON**, and `C:/veyra-deps/ffmpeg-ps5-dav1d-installed` (PS5 slice patch retained, dav1d enabled). Full machine-specific build arguments are recorded in [audio repair §6](../plans/CAPTURE_AUDIO_WAVEFORM_REPAIR_PLAN_2026-09-15.md#6-用户要求先修已知缺陷后的实施2026-09-15); use equivalent paths on your machine. See [1.3.0 source instructions](../releases/REMOTEPLAY_BUILD_1.3.0.md) and [runtime identities](RUNTIME_COMPONENTS_1.3.0.md).

The reduced default build below is not the full release configuration. Version 1.3.0 supplies corresponding FFmpeg/dav1d and Remote Play source assets separately from the portable ZIP. Historical instructions below remain reference material.

Targeted audio verification from the repository root:

```powershell
& ./out/build/audio-continuity-repair-20260915/veyra_audio_waveform_tests.exe --offline
& ./out/build/audio-continuity-repair-20260915/veyra_wasapi_input_tests.exe --offline
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915 -LogDirectory logs/audio-continuity-local-check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915 -LogDirectory logs/audio-continuity-local-check -DriftOnly
```

The first two commands are offline DSP/timebase checks. The script additionally uses the real Windows audio endpoint with muted synthetic tests; each drift case runs for 120 seconds. Neither proves capture-card listening quality or replaces GPU / export verification. Do not run endpoint suites concurrently.

WASAPI input-specific checks:

```powershell
& ./out/build/audio-continuity-repair-20260915/veyra_wasapi_input_tests.exe --list
& ./out/build/audio-continuity-repair-20260915/veyra_wasapi_input_tests.exe --invalid
& ./scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra_wasapi_input_tests.exe -Arguments @('--endpoint','<explicit endpoint ID>') -TimeoutSeconds 20 -LogPrefix logs/wasapi-input-20260915/endpoint
& ./scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra_capture_tests.exe -Arguments @('--wasapi','<explicit endpoint ID>') -TimeoutSeconds 20 -LogPrefix logs/wasapi-input-20260915/video-wasapi
```

`--endpoint` and `--wasapi` require a user-selected active recording endpoint ID from `--list`; the placeholder must not be replaced with a default device. These tests use muted input/output and do not save PCM. A successful local endpoint test proves enumeration, PCM delivery and lifecycle only; it does not prove the reported hiss is gone.

## Requirements

- Windows 11 x64, Visual Studio 2022 C++ tools, Windows SDK, CMake 3.24+, Ninja.
- Local FFmpeg development libraries matching avcodec63 / avformat63 / avutil61 / swresample7 / swscale10. Release 1.3.0 uses the LGPL vcpkg FFmpeg 9.0.1#1 build with dav1d enabled and the Veyra H.264 slice-capacity patch.
- The PS5 H.264 repair retained in 1.3.0 requires the additional [slice-capacity patch and matching rebuild](../../../scripts/ffmpeg/README.md). Stock 9.0.1 D3D12 H.264 has a 32-slice limit; the tested PS5 stream uses 68. Preserve `veyra-local-build.json` alongside the original vcpkg provenance in corresponding-source packages. Older release assets remain unchanged.
- NVIDIA DLSS SDK 310.7.0, Optical Flow SDK 5.0.7, RTX Video SDK 1.1.0, and nv-codec-headers. Prepare these under their respective licenses in ignored local directories.
- Intel XeSS SDK 3.0.2 for the XeSS presenter; AMD FidelityFX SDK 1.1.4 optical-flow/backend static libraries for the AMD flow option.
- Local NR runtime and NGX project configuration for experimental NR. The source repository intentionally does not contain them.

## Local Build

```powershell
git clone https://github.com/Likely7/Veyra-NRVideo.git
cd Veyra-NRVideo
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
```

`build.ps1` resolves Visual Studio and CMake, stages the five FFmpeg DLLs, and builds `out/build/x64-release/veyra.exe`. Its default FFmpeg location is `C:/veyra-deps/installed/x64-windows`. Use `-FfmpegRoot C:/path/to/prefix` when testing another FFmpeg prefix; the prefix must contain matching `include`, `lib`, `bin`, and `share/ffmpeg` trees. An AV1-enabled local prefix must also provide `bin/dav1d.dll` and its `share/dav1d` notices; the build script stages that DLL app-locally and never searches the system PATH. `-BuildDirectory` selects a separate output directory while an existing build is running.

| CMake variable | Local dependency |
| --- | --- |
| `VEYRA_FFMPEG_ROOT` | Prefix with FFmpeg include/lib/bin/share directories |
| `VEYRA_DLSS_SDK_ROOT` | NVIDIA DLSS SDK, including NGX static shim |
| `VEYRA_NVOF_SDK_ROOT` | Optical Flow SDK |
| `VEYRA_XESS_ROOT` | XeSS SDK `inc` directory parent |
| `VEYRA_FIDELITYFX_ROOT` | FidelityFX SDK `sdk` directory with built static libraries |
| `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR` | Enable the experimental NGX application targets |

The full player needs the NGX/NVOF and FFmpeg targets; a build with those dependencies absent is not the portable application's equivalent. CMake may disable optional XeSS/AMD flow when their local SDKs are missing. Inspect the configuration and test the actual backends before packaging.

The NGX configuration lives in `runtime_local/config/ngx-local.json` for development. A release places NVIDIA DLLs in `runtime/experimental` and the configuration in `runtime/config`. XeSS DLLs use `runtime_local/intel/experimental` in both layouts. Shader binaries live beside the executable in `shaders`.

## Dependency Sources

Release 1.3.0 includes `Veyra-1.3.0-FFmpeg-source.zip` separately: the patched FFmpeg/dav1d source, SPDX-verified vcpkg ports and patches, notices, and configuration queried from the shipped DLLs. It is not needed to run Veyra.

The 1.3.0 AV1 path uses FFmpeg's LGPL build with dav1d 1.5.4; its separate source asset includes the matching patched FFmpeg tree, dav1d source, port recipes, notices and binary configuration. MOV is a container rather than a codec: ProRes/H.264/HEVC MOV playback is tested independently, while AV1 needs a compatible container such as MP4 or WebM. Extensions alone do not guarantee compatibility.

- FFmpeg: https://github.com/FFmpeg/FFmpeg/tree/n9.0.1 ; vcpkg port source recorded in the distributed `licenses/FFMPEG-SPDX.json`.
- vcpkg FFmpeg port: https://github.com/microsoft/vcpkg/tree/55cd8b8a4f19d8e6ba2ad114c8acacc4af5915a0/ports/ffmpeg . Its patches and the LGPL notices are part of the corresponding-source material.
- FidelityFX SDK: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55 ; MIT license.
- XeSS SDK: https://github.com/intel/xess ; Intel Simplified Software License.
- DLSS SDK: https://github.com/NVIDIA/DLSS ; NVIDIA RTX SDK license.
- NVIDIA video SDK documentation: https://developer.nvidia.com/rtx-video-sdk . System NVENC/NVOF driver DLLs are not copied into the package.

Veyra and FidelityFX code are compiled into the executable; FFmpeg is dynamically linked. The executable uses the static MSVC runtime; FFmpeg and XeSS use the shipped redistributables `vcruntime140.dll`, `vcruntime140_1.dll`, and `msvcp140.dll`. UCRT, DirectX, and GPU driver components come from Windows and the installed driver.

## Verification and Packaging

Run targeted tests and `scripts/gates/delivery.ps1`; each individual test must stay below300seconds. Physical capture and screen scanout require separate hardware verification. The legacy Loop gate is not part of the current process.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 1.3.0 -BuildDirectory out/build/audio-continuity-repair-20260915 -OutputDirectory out/releases/1.3.0-final
```

The packager accepts `-BuildDirectory` for an isolated build, checks the publisher's fixed input files, copies an explicit payload, and emits a ZIP, checksums, and component manifests. It refuses to overwrite an existing candidate. This build-time audit does not restrict user DLL replacement. Do not upload SDK headers, samples, libraries, private media, logs, or development archives with the source.

## PS5 in 1.3.0

The release enables Remote Play; the default non-RemotePlay command above is a reduced build. Follow [REMOTEPLAY_BUILD_1.3.0.md](../releases/REMOTEPLAY_BUILD_1.3.0.md) for the full build, dependency source, patches and licensing. The published package also includes the matching FFmpeg/dav1d source asset.

## GPU DIS (1.3.0 experimental option)

The open-source subset in `third_party/gpu-dis` compiles with the existing Windows
SDK DXC; no additional GPU vendor SDK is required for DIS. `cmake/VeyraGpuDis.cmake`
retains the shader variants and compiler flags from `shader-recipes.json`.
Keep `shaders/dis/*.dxil`, `GpuDisLuma.dxil`, and `GpuDisValidate.dxil` when moving
an executable. Runtime requires D3D12 double-precision shader operations.
See `docs/GPU_DIS_INTEGRATION_PLAN_2026-09-13.md` for tests and limitations.
