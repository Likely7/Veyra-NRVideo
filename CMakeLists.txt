cmake_minimum_required(VERSION 3.24)

project(veyra VERSION 1.4.3 LANGUAGES CXX RC)
# Shown in the EXE's version resource and in the package name. CMake versions
# must be numeric, so the beta marker lives here instead.
set(VEYRA_DISPLAY_VERSION "1.4.3" CACHE STRING "Human-readable version string")
# The cache keeps the first value it ever saw, so a release that bumps
# project(VERSION) used to ship an EXE still stamped with the previous display
# string. Re-align it whenever the cached label does not start with the project
# version; an explicit matching label (e.g. "1.4.0beta") is still honoured.
if(NOT VEYRA_DISPLAY_VERSION MATCHES "^${PROJECT_VERSION}")
  message(STATUS "VEYRA_DISPLAY_VERSION ${VEYRA_DISPLAY_VERSION} does not match project ${PROJECT_VERSION}; using ${PROJECT_VERSION}")
  set(VEYRA_DISPLAY_VERSION "${PROJECT_VERSION}" CACHE STRING "Human-readable version string" FORCE)
endif()

# Match the compiler's actual bytes, including on localized Windows installs.
# CMake's cached, decoded prefix can leave Ninja with zero header dependencies.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
  set(VEYRA_INCLUDE_PROBE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/veyra-include-probe")
  file(WRITE "${VEYRA_INCLUDE_PROBE}.h" "// Dependency parser probe\n")
  file(WRITE "${VEYRA_INCLUDE_PROBE}.cpp" "#include \"veyra-include-probe.h\"\n")
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /showIncludes /c /TP "${VEYRA_INCLUDE_PROBE}.cpp" "/Fo${VEYRA_INCLUDE_PROBE}.obj"
    OUTPUT_VARIABLE VEYRA_INCLUDE_OUTPUT ERROR_VARIABLE VEYRA_INCLUDE_OUTPUT
    RESULT_VARIABLE VEYRA_INCLUDE_RESULT ENCODING NONE)
  if(NOT VEYRA_INCLUDE_RESULT EQUAL 0 OR NOT VEYRA_INCLUDE_OUTPUT MATCHES "(^|[\r\n])([^\r\n]*: )[ ]*[A-Za-z]:[^\r\n]*veyra-include-probe.h")
    message(FATAL_ERROR "Unable to verify MSVC/Ninja header dependency prefix")
  endif()
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
endif()

# ---------------------------------------------------------------------------
# Veyra cache variables and feature switches (Playbook section 5.1).
# Machine-specific absolute paths are resolved by scripts/build.ps1 at
# configure time; CMakePresets.json stays free of local paths.
# ---------------------------------------------------------------------------
set(VEYRA_DLSS_SDK_ROOT "" CACHE PATH "NVIDIA DLSS SDK root")
set(VEYRA_NVOF_SDK_ROOT "" CACHE PATH "NVIDIA Optical Flow SDK root")
if(NOT VEYRA_NVOF_SDK_ROOT)
  set(VEYRA_NVOF_CANDIDATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party_local/nvidia/Optical_Flow_SDK_5.0.7")
  if(EXISTS "${VEYRA_NVOF_CANDIDATE}/NvOFInterface/nvOpticalFlowCommon.h")
    set(VEYRA_NVOF_SDK_ROOT "${VEYRA_NVOF_CANDIDATE}")
  endif()
endif()
set(VEYRA_RUNTIME_ROOT "" CACHE PATH "Local NVIDIA runtime root")
set(VEYRA_FFMPEG_ROOT "" CACHE PATH "vcpkg-installed x64-windows root with FFmpeg (player deps)")
set(VEYRA_CLIP_TOOLS_ROOT "" CACHE PATH "vcpkg-installed x64-windows root with openh264-enabled FFmpeg (clip tooling only)")

option(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR
  "Enable local-only experimental Feature 18 adapter" OFF)
option(VEYRA_ENABLE_REMOTEPLAY "Build PS5 Remote Play with pinned Chiaki source" OFF)
option(VEYRA_ENABLE_DLSS_SR "Enable public DLSS SR backend" OFF)
option(VEYRA_ENABLE_DLSS_FG "Enable public DLSSG backend" OFF)
option(VEYRA_ENABLE_NVOF "Enable NVIDIA Optical Flow guidance" OFF)
option(VEYRA_ENABLE_D3D12_DEBUG "Enable D3D12 debug layer" ON)
set(VEYRA_FIDELITYFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party_local/amd/FidelityFX-SDK/sdk" CACHE PATH "Local official FidelityFX SDK 1.1.4")
set(VEYRA_XESS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party_local/intel/xess-3.0.2" CACHE PATH "Local Intel XeSS SDK 3.0.2")
set(VEYRA_FIDELITYFX2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party_local/amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX" CACHE PATH "Local AMD FidelityFX SDK 2.3.0 Kits/FidelityFX tree")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VeyraWarnings)
include(VeyraShaders)

# NVIDIA's static NGX core shim (nvsdk_ngx_s*.lib) is built against the
# static CRT (MT/MTd); match it for the diagnostic tools that link it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Targets grow phase by phase; only what actually compiles is declared.
# ---------------------------------------------------------------------------
add_library(veyra_base STATIC
  src/base/Log.cpp
  src/base/NgxResult.cpp
  src/base/FileIdentity.cpp
  src/base/RuntimePaths.cpp
  src/base/ThunkHook.cpp
  # .cube parsing, the runtime LUT store and named colour looks: low level, no
  # D3D12, shared by the ingest pipeline and the settings/import paths.
  src/engine/ColorLut.cpp
  src/engine/ColorLookStore.cpp
)
veyra_apply_warnings(veyra_base)
target_compile_options(veyra_base PUBLIC /utf-8)
target_compile_features(veyra_base PUBLIC cxx_std_20)
target_compile_definitions(veyra_base PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_base PUBLIC bcrypt wintrust crypt32 version)

add_library(veyra_gfx STATIC
  src/gfx/D3D12DeviceContext.cpp
  src/gfx/CommandSlotRing.cpp
  src/gfx/PresentSink.cpp
  src/gfx/XessPresenter.cpp
  src/gfx/XessMfgUnlock.cpp
  src/gfx/XessPacing.cpp
  src/gfx/FsrFgPresenter.cpp
  src/gfx/FsrSrBackend.cpp
)
veyra_apply_warnings(veyra_gfx)
target_compile_features(veyra_gfx PUBLIC cxx_std_20)
target_link_libraries(veyra_gfx PUBLIC veyra_base d3d12 dxgi dxguid)
if(EXISTS "${VEYRA_XESS_ROOT}/inc/xess_fg/xefg_swapchain_d3d12.h")
  target_include_directories(veyra_gfx PRIVATE "${VEYRA_XESS_ROOT}/inc")
  target_compile_definitions(veyra_gfx PRIVATE VEYRA_HAS_XESS=1)
endif()
if(EXISTS "${VEYRA_FIDELITYFX2_ROOT}/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h")
  target_include_directories(veyra_gfx PRIVATE
    "${VEYRA_FIDELITYFX2_ROOT}/api/include"
    "${VEYRA_FIDELITYFX2_ROOT}/framegeneration/include"
    "${VEYRA_FIDELITYFX2_ROOT}/upscalers/include")
  target_compile_definitions(veyra_gfx PRIVATE VEYRA_HAS_FSR=1)
endif()
if(VEYRA_ENABLE_D3D12_DEBUG)
  target_compile_definitions(veyra_gfx PUBLIC VEYRA_D3D12_DEBUG=1)
endif()

# ---------------------------------------------------------------------------
# veyra_ngx: NGX core host + local experimental DLSSNR adapter.
# Compiled only with the explicit experimental switch; missing SDK paths are
# a configure-time failure, never a runtime surprise (Playbook 5.1/8.1).
# ---------------------------------------------------------------------------
if(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR)
  if(NOT VEYRA_DLSS_SDK_ROOT OR NOT EXISTS "${VEYRA_DLSS_SDK_ROOT}/include/nvsdk_ngx.h")
    message(FATAL_ERROR "VEYRA_ENABLE_EXPERIMENTAL_DLSSNR requires VEYRA_DLSS_SDK_ROOT pointing at NVIDIA DLSS SDK 310.7 (include/nvsdk_ngx.h missing)")
  endif()
add_library(veyra_ngx STATIC
  src/ngx/NgxCoreHost.cpp
  src/ngx/DlssNrRuntimeAdapter.cpp
  src/ngx/DlssSrBackend.cpp
    src/ngx/VideoSrBackend.cpp
    src/ngx/TrueHdrBackend.cpp
  src/ngx/DlssFgBackend.cpp
  src/ngx/FgCompatibilitySession.cpp
  src/ngx/compat/hde/hde64.c
  src/ngx/AdaMfgUnlock.cpp
  src/ngx/AmpereMfgUnlock.cpp
  src/ngx/NvapiArchSpoof.cpp
)
  set_source_files_properties(src/ngx/compat/hde/hde64.c PROPERTIES LANGUAGE CXX)
  veyra_apply_warnings(veyra_ngx)
  target_compile_features(veyra_ngx PUBLIC cxx_std_20)
  target_link_libraries(veyra_ngx PUBLIC veyra_base veyra_gfx d3d11)
  target_include_directories(veyra_ngx PUBLIC
    "${VEYRA_DLSS_SDK_ROOT}/include")
  # NVIDIA ships the static core shim as MT-Release; the _dbg variants match
  # the MDd debug runtime. Pick per configuration to avoid LNK2038.
  target_link_libraries(veyra_ngx PRIVATE
    "$<IF:$<CONFIG:Debug>,${VEYRA_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib,${VEYRA_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib>")
  target_compile_definitions(veyra_ngx PUBLIC
    VEYRA_EXPERIMENTAL_LOCAL_DLSSNR=1)

  # Diagnostic: exercises the Ada multi-frame unlock against the local DLSS-G
  # runtime image without involving NGX (works on any NVIDIA adapter).
  add_executable(veyra_dlssg_unlock_probe tools/dlssg_unlock_probe/main.cpp)
  target_compile_features(veyra_dlssg_unlock_probe PRIVATE cxx_std_20)
  target_compile_options(veyra_dlssg_unlock_probe PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_dlssg_unlock_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_dlssg_unlock_probe PRIVATE veyra_ngx)

  # Diagnostic: exercises the RTX 30 (sm_86) Ampere unlock against the local
  # DLSS-G runtime image without involving NGX (works on any NVIDIA adapter).
  add_executable(veyra_dlssg_ampere_probe tools/dlssg_ampere_probe/main.cpp)
  target_compile_features(veyra_dlssg_ampere_probe PRIVATE cxx_std_20)
  target_compile_options(veyra_dlssg_ampere_probe PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_dlssg_ampere_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_dlssg_ampere_probe PRIVATE veyra_ngx)
endif()

add_executable(veyra_runtime_probe
  tools/runtime_probe/main.cpp
)

# Dolby/DTS endpoint passthrough capability probe (diagnostic, never shipped).
add_executable(veyra_bitstream_probe tools/bitstream_audio_probe/main.cpp)
target_compile_features(veyra_bitstream_probe PRIVATE cxx_std_20)
target_compile_options(veyra_bitstream_probe PRIVATE /utf-8 /WX-)
target_compile_definitions(veyra_bitstream_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(veyra_bitstream_probe PRIVATE ole32)

# Isolated descriptor/Present diagnosis probe (user directive 2026-09-04 s3).
# No NGX/FFmpeg/NVOF/WASAPI. Uses PresentBlit shaders for cases F/G.
add_executable(veyra_descriptor_present_probe
  tools/descriptor_present_probe/main.cpp
)
veyra_apply_warnings(veyra_descriptor_present_probe)
target_compile_features(veyra_descriptor_present_probe PRIVATE cxx_std_20)
target_compile_definitions(veyra_descriptor_present_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
target_link_libraries(veyra_descriptor_present_probe PRIVATE d3d12 dxgi)
add_dependencies(veyra_descriptor_present_probe veyra_shader_present_blit)
veyra_apply_warnings(veyra_runtime_probe)
target_compile_features(veyra_runtime_probe PRIVATE cxx_std_20)
target_compile_definitions(veyra_runtime_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(veyra_runtime_probe PRIVATE veyra_gfx)

veyra_add_compute_shader(veyra_shader_parity_encode ParityEncode)
veyra_add_compute_shader(veyra_shader_parity_decode ParityDecode)
veyra_add_compute_shader(veyra_shader_hdr_video_sr HdrVideoSrRestore)
if(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR)
  veyra_add_compute_shader(veyra_shader_test_pattern GenerateTestPattern)
  add_executable(veyra_nr_ampere_tests tests/integration/NrAmpereTests.cpp)
  target_compile_features(veyra_nr_ampere_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_nr_ampere_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_nr_ampere_tests PRIVATE veyra_gfx veyra_ngx)
  add_executable(veyra_nr_harness
    tools/nr_harness/main.cpp
    tools/nr_harness/frame_loop.cpp
    tools/nr_harness/harness_util.cpp
    tools/nr_harness/parity_compare.cpp
    tools/nr_harness/sr_test.cpp
  )
  veyra_apply_warnings(veyra_nr_harness)
  target_compile_features(veyra_nr_harness PRIVATE cxx_std_20)
  target_compile_definitions(veyra_nr_harness PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
  target_link_libraries(veyra_nr_harness PRIVATE veyra_gfx veyra_ngx veyra_parity)
  add_dependencies(veyra_nr_harness veyra_shader_test_pattern
    veyra_shader_parity_encode veyra_shader_parity_decode)

  # Phase 6: DLSSG truth harness + WASAPI audio probe.
  add_executable(veyra_fg_harness
    tools/fg_harness/main.cpp
    tools/fg_harness/fg_test.cpp
    tools/fg_harness/audio_test.cpp
    tools/nr_harness/harness_util.cpp
  )
  veyra_apply_warnings(veyra_fg_harness)
  target_compile_features(veyra_fg_harness PRIVATE cxx_std_20)
  target_compile_definitions(veyra_fg_harness PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_fg_harness PRIVATE veyra_gfx veyra_ngx ole32 mmdevapi avrt)
endif()

# YUV→RGB shader is shared between the media pipeline and the harness.
veyra_add_compute_shader(veyra_shader_yuv_to_rgb YuvToLinearRgb)
veyra_add_compute_shader(veyra_shader_rgb_to_linear RgbToLinear)
veyra_add_compute_shader(veyra_shader_yuy2_to_linear Yuy2ToLinear)
veyra_add_compute_shader(veyra_shader_packed_capture PackedCaptureToLinear)
add_dependencies(veyra_shader_rgb_to_linear veyra_shader_yuy2_to_linear)
# ScaleBlit: working frame -> swapchain backbuffer (Phase 6 present path).
veyra_add_compute_shader(veyra_shader_scale_blit ScaleBlit)
# Nv12Upload: byte buffer -> luma/chroma planes (copy-free upload path).
veyra_add_compute_shader(veyra_shader_nv12_upload Nv12Upload)
# PresentBlit: fullscreen-triangle pixel blit onto flip-model back buffers.
veyra_add_graphics_shader_pair(veyra_shader_present_blit PresentBlit)
veyra_add_compute_shader(veyra_shader_present_motion PresentMotion)
# NvofDensify: SHORT2 S10.5 raw flow -> workingExtent float2 + confidence.
veyra_add_compute_shader(veyra_shader_nvof_densify NvofDensify)
veyra_add_compute_shader(veyra_shader_nr_downsample NrDownsample)
veyra_add_compute_shader(veyra_shader_nr_residual NrResidualComposite)
veyra_add_compute_shader(veyra_shader_flow_adapt FlowAdapt)
veyra_add_compute_shader(veyra_shader_rgb_nv12 RgbToNv12)

# ---------------------------------------------------------------------------
# veyra_parity: RenoDX-equivalent codec reference (CPU golden + shaders later).
# The CPU reference has no D3D12 dependency, so it builds unconditionally.
# ---------------------------------------------------------------------------
add_library(veyra_parity STATIC
  src/parity/RenoDxParityCodec.cpp
)
veyra_apply_warnings(veyra_parity)
target_compile_features(veyra_parity PUBLIC cxx_std_20)
target_link_libraries(veyra_parity PUBLIC veyra_base)

add_executable(veyra_parity_tests
  tests/unit/ParityCpuReference.cpp
)
veyra_apply_warnings(veyra_parity_tests)
target_compile_features(veyra_parity_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_parity_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(veyra_parity_tests PRIVATE veyra_parity)

add_executable(veyra_unified_tests
  tests/unit/UnifiedContractTests.cpp
  src/core/EnhanceGraph.cpp
)
veyra_apply_warnings(veyra_unified_tests)
target_compile_features(veyra_unified_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_unified_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_unified_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_unified_tests PRIVATE d3d12 veyra_gfx veyra_ngx)

# Guidance product library (Playbook R3/R4): providers produce the unified
# GuidanceFrame. First real member: ZeroGuidanceProvider (honest fallback).
# NvofGuidanceProvider moves in from player_probe during R3.2.
add_library(veyra_guidance STATIC
  src/guidance/ZeroGuidanceProvider.cpp
)
veyra_apply_warnings(veyra_guidance)
target_compile_options(veyra_guidance PRIVATE /WX-)
target_compile_features(veyra_guidance PRIVATE cxx_std_20)
target_compile_definitions(veyra_guidance PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_guidance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_guidance PUBLIC veyra_pipeline veyra_gfx veyra_base d3d12 dxgi dxguid)

add_executable(veyra_guidance_tests
  tests/integration/GuidanceProviderTests.cpp
)
veyra_apply_warnings(veyra_guidance_tests)
target_compile_options(veyra_guidance_tests PRIVATE /WX-)
target_compile_features(veyra_guidance_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_guidance_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_guidance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_guidance_tests PRIVATE veyra_guidance)

# Pipeline product library (Playbook R2/R3): data contracts + reset coordinator.
# Grows in R3 as the real GPU graph migrates out of the probes.
add_library(veyra_pipeline STATIC
  src/pipeline/ResetCoordinator.cpp
  src/pipeline/ColorGradeTables.cpp
)
veyra_apply_warnings(veyra_pipeline)
# FFmpeg/NVOF headers are third-party: /WX stays off for this target.
target_compile_options(veyra_pipeline PRIVATE /WX-)
target_compile_features(veyra_pipeline PRIVATE cxx_std_20)
target_compile_definitions(veyra_pipeline PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
target_include_directories(veyra_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_pipeline PUBLIC veyra_base veyra_gfx d3d12 dxgi dxguid d3dcompiler)

add_executable(veyra_pipeline_tests
  tests/unit/PipelineContractTests.cpp
)
veyra_apply_warnings(veyra_pipeline_tests)
target_compile_features(veyra_pipeline_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_pipeline_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_pipeline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_pipeline_tests PRIVATE veyra_pipeline)

add_executable(veyra_scene_tests
  tests/unit/SceneCadenceTests.cpp
  src/core/SceneCadenceAnalyzer.cpp
)
veyra_apply_warnings(veyra_scene_tests)
target_compile_features(veyra_scene_tests PRIVATE cxx_std_20)
target_compile_definitions(veyra_scene_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_scene_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone scene analyzer tool (phase5 gate expects veyra_scene_analyzer.exe).
if(VEYRA_FFMPEG_ROOT AND EXISTS "${VEYRA_FFMPEG_ROOT}/include/libavformat/avformat.h")
  add_executable(veyra_scene_analyzer
    tools/scene_analyzer/main.cpp
    src/core/SceneCadenceAnalyzer.cpp
  )
  veyra_apply_warnings(veyra_scene_analyzer)
  target_compile_options(veyra_scene_analyzer PRIVATE /WX-)
  target_compile_features(veyra_scene_analyzer PRIVATE cxx_std_20)
  target_compile_definitions(veyra_scene_analyzer PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_scene_analyzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_scene_analyzer PRIVATE veyra_media veyra_base)
endif()

# NVOF probe tool (phase5 gate expects veyra_nvof_probe.exe).
set(VEYRA_NVOF_SDK_ROOT "" CACHE PATH "NVIDIA Optical Flow SDK root")
if(VEYRA_NVOF_SDK_ROOT AND EXISTS "${VEYRA_NVOF_SDK_ROOT}/NvOFInterface/nvOpticalFlowCommon.h")
  # Reusable NVOF D3D12 session (Phase 6 motion guidance for DLSSG).
  add_library(veyra_nvof STATIC src/ngx/NvOfSession.cpp)
  veyra_apply_warnings(veyra_nvof)
  target_compile_features(veyra_nvof PUBLIC cxx_std_20)
  target_include_directories(veyra_nvof PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${VEYRA_NVOF_SDK_ROOT}/NvOFInterface"
    "${VEYRA_NVOF_SDK_ROOT}/Common/NvOFBase")
  target_link_libraries(veyra_nvof PUBLIC veyra_base veyra_gfx)

  add_executable(veyra_nvof_probe tools/nvof_probe/main.cpp)

  # s9-A3: table-driven null-argument fault injection for NvOfSession.
  add_executable(veyra_nvof_fault_inject tools/nvof_fault_inject/main.cpp)
  veyra_apply_warnings(veyra_nvof_fault_inject)
  target_compile_options(veyra_nvof_fault_inject PRIVATE /WX-)
  target_compile_features(veyra_nvof_fault_inject PRIVATE cxx_std_20)
  target_compile_definitions(veyra_nvof_fault_inject PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_nvof_fault_inject PRIVATE veyra_gfx veyra_nvof)
  veyra_apply_warnings(veyra_nvof_probe)
  target_compile_options(veyra_nvof_probe PRIVATE /WX-)
  target_compile_features(veyra_nvof_probe PRIVATE cxx_std_20)
  target_compile_definitions(veyra_nvof_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_nvof_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${VEYRA_NVOF_SDK_ROOT}/NvOFInterface"
    "${VEYRA_NVOF_SDK_ROOT}/Common/NvOFBase")
  target_link_libraries(veyra_nvof_probe PRIVATE veyra_gfx veyra_base)
endif()

# Resolve FFmpeg before any consumer, including the shared enhancement graph.
if(VEYRA_FFMPEG_ROOT AND EXISTS "${VEYRA_FFMPEG_ROOT}/include/libavformat/avformat.h")
  list(APPEND CMAKE_MODULE_PATH "${VEYRA_FFMPEG_ROOT}/share/ffmpeg")
  find_package(FFMPEG REQUIRED)
endif()

# EnhanceGraph (the real GPU chain) needs the NGX + NVOF backends; wire it
# into veyra_pipeline only when both exist so bare contract builds still work.
if(TARGET veyra_ngx AND TARGET veyra_nvof AND VEYRA_FFMPEG_ROOT)
  target_sources(veyra_pipeline PRIVATE src/pipeline/EnhanceGraph.cpp src/core/SceneCadenceAnalyzer.cpp)
  target_include_directories(veyra_pipeline PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(veyra_pipeline PRIVATE veyra_ngx veyra_nvof ${FFMPEG_LIBRARIES})
  add_dependencies(veyra_pipeline
    veyra_shader_yuv_to_rgb veyra_shader_rgb_to_linear veyra_shader_parity_encode
    veyra_shader_parity_decode veyra_shader_scale_blit
    veyra_shader_nv12_upload veyra_shader_nvof_densify veyra_shader_packed_capture)
endif()

# ---------------------------------------------------------------------------
# veyra_media: FFmpeg demux/decode (Phase 3). Links only the Playbook 3.4
# dependency set from the pinned vcpkg install.
# ---------------------------------------------------------------------------
if(VEYRA_FFMPEG_ROOT AND EXISTS "${VEYRA_FFMPEG_ROOT}/include/libavformat/avformat.h")
  add_library(veyra_media STATIC
    src/media/FFmpegDemuxer.cpp
    src/media/FFmpegVideoDecoder.cpp
  )
  veyra_apply_warnings(veyra_media)
  # FFmpeg headers are third-party; /WX stays off for this target
  # (Playbook 5.3: never let third-party headers block the build).
  target_compile_options(veyra_media PRIVATE /WX-)
  target_compile_features(veyra_media PUBLIC cxx_std_20)
  # d3d11: the HEVC hardware path decodes on a private D3D11VA device and shares
  # the surfaces with the D3D12 graph as NT handles.
  target_link_libraries(veyra_media PUBLIC veyra_base veyra_gfx d3d11)
  target_include_directories(veyra_media PUBLIC ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(veyra_media PUBLIC ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(veyra_media PUBLIC ${FFMPEG_LIBRARIES})

  add_executable(veyra_media_probe
    tools/media_probe/main.cpp
    tools/nr_harness/harness_util.cpp
  )
  veyra_apply_warnings(veyra_media_probe)
  target_compile_options(veyra_media_probe PRIVATE /WX-)
  target_compile_features(veyra_media_probe PRIVATE cxx_std_20)
  target_compile_definitions(veyra_media_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
  target_link_libraries(veyra_media_probe PRIVATE veyra_media veyra_gfx veyra_ngx veyra_parity psapi)
  add_dependencies(veyra_media_probe veyra_shader_yuv_to_rgb)

  # veyra_sinks: product output sinks (Playbook R3). First member is the
  # WASAPI audio sink extracted verbatim from player_probe; D3D12PresentSink
  # currently lives in veyra_gfx and moves here when the migration completes.
  add_library(veyra_sinks STATIC
    src/sink/WasapiAudioSink.cpp
    src/sink/CaptureAudioSession.cpp
    src/sink/CaptureAudioDsp.cpp
    src/sink/BitstreamAudio.cpp
    src/sink/BitstreamAudioSink.cpp
    src/sink/ImageExportSink.cpp
    src/sink/NvencD3D12Encoder.cpp
    src/sink/MfVideoEncoder.cpp
    src/sink/VideoEncoderFactory.cpp
  )
  veyra_apply_warnings(veyra_sinks)
  target_compile_options(veyra_sinks PRIVATE /WX-)
  target_compile_features(veyra_sinks PRIVATE cxx_std_20)
  target_compile_definitions(veyra_sinks PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_sinks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_include_directories(veyra_sinks PRIVATE ${FFMPEG_INCLUDE_DIRS})
  set(VEYRA_NVENC_HEADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party_local/nvidia/nv-codec-headers-13.0" CACHE PATH "NVENC 13.0 ABI headers (nv-codec-headers n13.0.19.0)")
  if(NOT EXISTS "${VEYRA_NVENC_HEADERS_ROOT}/include/ffnvcodec/nvEncodeAPI.h")
    message(FATAL_ERROR "Stage nv-codec-headers n13.0.19.0 at VEYRA_NVENC_HEADERS_ROOT; see THIRD_PARTY_NOTICES.md")
  endif()
  target_include_directories(veyra_sinks PRIVATE "${VEYRA_NVENC_HEADERS_ROOT}/include")
  target_compile_definitions(veyra_sinks PRIVATE VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
  add_dependencies(veyra_sinks veyra_shader_rgb_nv12)
  target_link_libraries(veyra_sinks PUBLIC veyra_base veyra_gfx ole32 windowscodecs mmdevapi avrt mfplat mfuuid mf)
  target_link_libraries(veyra_sinks PRIVATE ${FFMPEG_LIBRARIES})
  if(FFMPEG_INCLUDE_DIRS)
    target_include_directories(veyra_sinks PRIVATE ${FFMPEG_INCLUDE_DIRS})
  endif()

  # veyra_sources: product input sources (Playbook R3/R7). MediaFileSource
  # composes veyra_media; no second decode implementation exists.
  add_dependencies(veyra_pipeline veyra_shader_nr_downsample veyra_shader_nr_residual veyra_shader_flow_adapt)
  add_library(veyra_sources STATIC
    src/source/NativeCaptureSink.cpp
    src/source/MediaFileSource.cpp
    src/source/CaptureCardSource.cpp
    src/source/AjaCaptureSource.cpp
    src/source/ScreenCaptureSource.cpp
    src/source/AverMediaAudioSwitch.cpp
    src/source/CaptureCompressedDecoder.cpp
    src/source/WasapiAudioInput.cpp
  )
  veyra_apply_warnings(veyra_sources)
  target_compile_options(veyra_sources PRIVATE /WX-)
  target_compile_features(veyra_sources PRIVATE cxx_std_20)
  target_link_libraries(veyra_sources PUBLIC veyra_sinks)
  target_compile_definitions(veyra_sources PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_sources PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_sources PUBLIC veyra_pipeline veyra_media veyra_base)
  target_link_libraries(veyra_sources PRIVATE ${FFMPEG_LIBRARIES} strmiids ole32 oleaut32 d3d12 dxgi)
  target_link_libraries(veyra_sources PRIVATE d3d11 windowsapp dwmapi)

  include(cmake/VeyraAja.cmake)

  if(VEYRA_ENABLE_REMOTEPLAY)
    set(VEYRA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
    include(cmake/VeyraRemotePlay.cmake)
    target_compile_definitions(veyra_sources PUBLIC VEYRA_ENABLE_REMOTEPLAY=1)
    target_sources(veyra_sources PRIVATE src/source/RemotePlaySource.cpp src/source/RemotePlaySessionSource.cpp)
    target_link_libraries(veyra_sources PUBLIC veyra_remoteplay_chiaki)
  endif()

  add_executable(veyra_source_tests
    tests/integration/MediaFileSourceTests.cpp
  )
  veyra_apply_warnings(veyra_source_tests)
  target_compile_options(veyra_source_tests PRIVATE /WX-)
  target_compile_features(veyra_source_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_source_tests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_source_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_source_tests PRIVATE veyra_sources)

  add_executable(veyra_capture_tests tests/integration/CaptureSourceTests.cpp)
  add_executable(veyra_screen_capture_tests tests/integration/ScreenCaptureTests.cpp)
  target_compile_features(veyra_screen_capture_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_screen_capture_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_screen_capture_tests PRIVATE veyra_engine dwmapi)
  add_executable(veyra_wasapi_input_tests tests/integration/WasapiInputTests.cpp)
  target_compile_features(veyra_wasapi_input_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_wasapi_input_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_wasapi_input_tests PRIVATE veyra_sources)
  target_compile_features(veyra_capture_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_capture_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_capture_tests PRIVATE veyra_sources)

  add_executable(veyra_live_timing_tests tests/unit/LivePresentationTimingTests.cpp)
  add_executable(veyra_control_paint_tests tests/integration/ControlPaintTests.cpp)
  target_compile_features(veyra_control_paint_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_control_paint_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_control_paint_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps/veyra/ui)
  target_link_libraries(veyra_control_paint_tests PRIVATE veyra_base comctl32 gdiplus uxtheme gdi32 user32 ole32)
  target_compile_features(veyra_live_timing_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_live_timing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  add_executable(veyra_repair_contract_tests tests/unit/RepairContractTests.cpp)
  add_executable(veyra_subtitle_loading_tests tests/integration/SubtitleLoadingTests.cpp)
  add_executable(veyra_screenshot_combination_tests tests/integration/ScreenshotCombinationTests.cpp)
  target_link_libraries(veyra_screenshot_combination_tests PRIVATE veyra_engine)
  target_compile_features(veyra_screenshot_combination_tests PRIVATE cxx_std_20)
  add_executable(veyra_bitmap_subtitle_tests tests/integration/BitmapSubtitleTests.cpp)
  target_link_libraries(veyra_bitmap_subtitle_tests PRIVATE veyra_engine)
  target_compile_features(veyra_bitmap_subtitle_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_subtitle_loading_tests PRIVATE veyra_engine)
  target_compile_features(veyra_subtitle_loading_tests PRIVATE cxx_std_20)
  # veyra_engine is linked for the subtitle engine (SRT/ASS/VTT parsing +
  # indexed cue lookup); FFmpeg import libraries come from its public surface.
  target_link_libraries(veyra_repair_contract_tests PRIVATE veyra_base veyra_engine ${FFMPEG_LIBRARIES})
  target_compile_features(veyra_repair_contract_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_repair_contract_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_repair_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  add_executable(veyra_repair_parameter_tests tests/integration/RepairParameterTests.cpp)
  add_executable(veyra_image_dimension_tests tests/integration/ImageDimensionTests.cpp)
  add_executable(veyra_capture_color_probe tools/capture_color_probe/main.cpp)
  add_executable(veyra_capture_copy_bench tools/capture_copy_bench/main.cpp)
  target_compile_features(veyra_capture_copy_bench PRIVATE cxx_std_20)
  target_compile_options(veyra_capture_copy_bench PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_capture_copy_bench PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_capture_copy_bench PRIVATE veyra_engine)
  add_executable(veyra_decode_profile_probe tools/decode_profile_probe/main.cpp)
  target_compile_features(veyra_decode_profile_probe PRIVATE cxx_std_20)
  target_compile_options(veyra_decode_profile_probe PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_decode_profile_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_decode_profile_probe PRIVATE d3d12 dxgi dxguid)
  add_executable(veyra_fsr_probe tools/fsr_probe/main.cpp)
  target_compile_features(veyra_fsr_probe PRIVATE cxx_std_20)
  target_compile_options(veyra_fsr_probe PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_fsr_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_fsr_probe PRIVATE d3d12 dxgi dxguid)
  add_executable(veyra_fsr_upscale_probe tools/fsr_upscale_probe/main.cpp)
  target_compile_features(veyra_fsr_upscale_probe PRIVATE cxx_std_20)
  target_compile_options(veyra_fsr_upscale_probe PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_fsr_upscale_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_fsr_upscale_probe PRIVATE d3d12 dxgi dxguid)
  add_executable(veyra_capture_color_tests tests/unit/CaptureColorContractTests.cpp)
  add_executable(veyra_presentation_worker_tests tests/unit/LiveGpuSchedulerTests.cpp)
  target_compile_features(veyra_presentation_worker_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_presentation_worker_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_presentation_worker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_executable(veyra_realtime_preview_tests tests/unit/RealtimePreviewSchedulingTests.cpp)
  target_compile_features(veyra_realtime_preview_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_realtime_preview_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_realtime_preview_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_executable(veyra_live_presentation_tests tests/integration/LivePresentationTests.cpp)
  add_executable(veyra_nr_runtime_switch_tests tests/integration/NrRuntimeSwitchTests.cpp)
  target_compile_features(veyra_nr_runtime_switch_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_nr_runtime_switch_tests PRIVATE veyra_engine)
  add_executable(veyra_audio_timeline_tests tests/integration/AudioTimelineTests.cpp)
  target_compile_features(veyra_audio_timeline_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_audio_timeline_tests PRIVATE veyra_sinks)
  add_executable(veyra_capture_audio_tests tests/integration/CaptureAudioTests.cpp)
  add_executable(veyra_avermedia_switch_tests tests/unit/AverMediaAudioSwitchTests.cpp)
  target_compile_features(veyra_avermedia_switch_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_avermedia_switch_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_avermedia_switch_tests PRIVATE veyra_sources)
  add_executable(veyra_iec61937_probe_tests tests/unit/Iec61937ProbeTests.cpp)
  target_compile_features(veyra_iec61937_probe_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_iec61937_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_iec61937_probe_tests PRIVATE veyra_sinks)
  if(VEYRA_FFMPEG_ROOT AND EXISTS "${VEYRA_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    add_executable(veyra_bitstream_audio_test tools/bitstream_audio_test/main.cpp)
    target_compile_features(veyra_bitstream_audio_test PRIVATE cxx_std_20)
    target_compile_options(veyra_bitstream_audio_test PRIVATE /utf-8 /WX-)
    target_compile_definitions(veyra_bitstream_audio_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_include_directories(veyra_bitstream_audio_test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(veyra_bitstream_audio_test PRIVATE veyra_sinks veyra_base ${FFMPEG_LIBRARIES})
  endif()
  add_executable(veyra_audio_waveform_tests tests/integration/AudioWaveformTests.cpp)
  target_compile_features(veyra_audio_waveform_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_audio_waveform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_audio_waveform_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(veyra_audio_waveform_tests PRIVATE veyra_sinks ${FFMPEG_LIBRARIES})
  target_compile_features(veyra_capture_audio_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_capture_audio_tests PRIVATE veyra_sinks)
  target_compile_features(veyra_live_presentation_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_live_presentation_tests PRIVATE veyra_engine)
  add_executable(veyra_fg_admission_tests tests/integration/FgAdmissionTests.cpp)
  target_compile_features(veyra_fg_admission_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_fg_admission_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_fg_admission_tests PRIVATE veyra_engine)
  target_compile_features(veyra_capture_color_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_capture_color_tests PRIVATE veyra_sources)
  add_executable(veyra_capture_compressed_tests tests/unit/CaptureCompressedDecoderTests.cpp)
  target_compile_features(veyra_capture_compressed_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_capture_compressed_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_capture_compressed_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(veyra_capture_compressed_tests PRIVATE veyra_sources veyra_gfx ${FFMPEG_LIBRARIES})
  add_executable(veyra_yuy2_color_tests tests/integration/Yuy2ColorTests.cpp)
  target_compile_features(veyra_yuy2_color_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_yuy2_color_tests PRIVATE veyra_engine)
  target_compile_features(veyra_capture_color_probe PRIVATE cxx_std_20)
  target_compile_definitions(veyra_capture_color_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_capture_color_probe PRIVATE veyra_engine)
  target_compile_features(veyra_image_dimension_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_image_dimension_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_image_dimension_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_image_dimension_tests PRIVATE veyra_engine)
  add_executable(veyra_preview_geometry_tests tests/integration/PreviewGeometryTests.cpp)
  add_executable(veyra_experimental_backend_tests tests/integration/ExperimentalBackendTests.cpp)
  target_compile_features(veyra_experimental_backend_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_experimental_backend_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_experimental_backend_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_experimental_backend_tests PRIVATE veyra_engine)
  target_compile_features(veyra_preview_geometry_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_preview_geometry_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_preview_geometry_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_preview_geometry_tests PRIVATE veyra_engine)
  target_compile_features(veyra_repair_parameter_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_repair_parameter_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_repair_parameter_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_repair_parameter_tests PRIVATE veyra_pipeline veyra_sinks veyra_nvof)
  add_executable(veyra_repair_fg_tests tests/integration/RepairFgTests.cpp)
  add_executable(veyra_export_frame_pool_tests tests/integration/ExportFramePoolTests.cpp)
  add_executable(veyra_export_worker_failure_tests tests/integration/ExportWorkerFailureTests.cpp)
  target_compile_features(veyra_export_worker_failure_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_export_worker_failure_tests PRIVATE veyra_engine)
  add_executable(veyra_fg_lifecycle_tests tests/integration/FgLifecycleTests.cpp)
  add_executable(veyra_fg_settings_tests tests/integration/FgSettingsTests.cpp)
  add_executable(veyra_fg_sustained_tests tests/integration/FgSustainedTests.cpp)
  target_compile_features(veyra_fg_sustained_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_fg_sustained_tests PRIVATE veyra_engine)
  add_executable(veyra_fg_presentation_tests tests/integration/FgPresentationTests.cpp)
  target_compile_features(veyra_fg_presentation_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_fg_presentation_tests PRIVATE VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_fg_presentation_tests PRIVATE veyra_engine)
  add_executable(veyra_player_tracks_tests tests/integration/PlayerTracksTests.cpp)
  add_executable(veyra_scheduling_chain_tests tests/integration/SchedulingChainTests.cpp)
  add_executable(veyra_capture_latency_tests tests/integration/CaptureLatencyComparisonTests.cpp)
  target_compile_features(veyra_capture_latency_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_capture_latency_tests PRIVATE /utf-8)
  target_link_libraries(veyra_capture_latency_tests PRIVATE veyra_engine)
  add_dependencies(veyra_capture_latency_tests veyra_shader_present_blit)
  add_executable(veyra_presentation_pacing_tests tests/integration/PresentationPacingTests.cpp)
  target_compile_features(veyra_presentation_pacing_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_presentation_pacing_tests PRIVATE /utf-8)
  target_link_libraries(veyra_presentation_pacing_tests PRIVATE veyra_engine)
  target_compile_features(veyra_scheduling_chain_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_scheduling_chain_tests PRIVATE veyra_engine)
  target_compile_features(veyra_player_tracks_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_player_tracks_tests PRIVATE veyra_engine)
  add_executable(veyra_audio_track_output_tests tests/integration/AudioTrackOutputTests.cpp)
  target_compile_features(veyra_audio_track_output_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_audio_track_output_tests PRIVATE veyra_engine)
  target_compile_features(veyra_fg_settings_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_fg_settings_tests PRIVATE veyra_engine)
  target_compile_features(veyra_fg_lifecycle_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_fg_lifecycle_tests PRIVATE VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_fg_lifecycle_tests PRIVATE veyra_engine)
  target_compile_features(veyra_export_frame_pool_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_export_frame_pool_tests PRIVATE VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_export_frame_pool_tests PRIVATE veyra_engine)
  add_executable(veyra_motion_validation_tests tests/integration/MotionValidationTests.cpp)
  add_executable(veyra_depth_response_tests tests/integration/DepthResponseTests.cpp)
  add_executable(veyra_srfg_depth_tests tests/integration/SrFgDepthResponseTests.cpp)
  target_compile_features(veyra_srfg_depth_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_srfg_depth_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_srfg_depth_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_srfg_depth_tests PRIVATE veyra_engine)
  add_executable(veyra_sr_reconstruction_tests tests/integration/SrReconstructionTests.cpp)
  target_compile_features(veyra_sr_reconstruction_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_sr_reconstruction_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_sr_reconstruction_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_sr_reconstruction_tests PRIVATE veyra_engine)
  target_compile_features(veyra_depth_response_tests PRIVATE cxx_std_20)
  target_include_directories(veyra_depth_response_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_definitions(veyra_depth_response_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_depth_response_tests PRIVATE veyra_engine)
  add_executable(veyra_bidirectional_flow_tests tests/integration/BidirectionalFlowTests.cpp)
  target_compile_features(veyra_bidirectional_flow_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_bidirectional_flow_tests PRIVATE veyra_pipeline veyra_nvof)
  target_compile_features(veyra_motion_validation_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_motion_validation_tests PRIVATE veyra_pipeline)
  add_executable(veyra_repair_shader_tests tests/integration/RepairShaderTests.cpp)
  add_executable(veyra_gpu_timing_tests tests/integration/GpuTimingTests.cpp)
  target_compile_features(veyra_gpu_timing_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_gpu_timing_tests PRIVATE veyra_pipeline)
  target_compile_features(veyra_repair_shader_tests PRIVATE cxx_std_20)
  target_link_libraries(veyra_repair_shader_tests PRIVATE veyra_pipeline)
  target_include_directories(veyra_repair_fg_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_compile_features(veyra_repair_fg_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_repair_fg_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(veyra_repair_fg_tests PRIVATE veyra_pipeline veyra_sinks veyra_nvof)

  # Headless quality-core runner (Playbook R3.3): the phase5 gate's quality
  add_library(veyra_engine STATIC src/engine/EngineController.cpp src/engine/EngineControllerImage.cpp src/engine/TiledImageProcessor.cpp src/engine/PresetStore.cpp src/engine/VideoPresenter.cpp src/engine/VideoExportJob.cpp src/engine/ExportJobManager.cpp src/engine/Subtitles.cpp src/engine/FgCompatibilityProbe.cpp)
  target_sources(veyra_pipeline PRIVATE src/guidance/AmdOpticalFlow.cpp
    src/guidance/GpuDisOpticalFlow.cpp third_party/gpu-dis/src/gpu/native_strict_dis_provider.cpp)
  target_include_directories(veyra_pipeline PRIVATE third_party/gpu-dis/src/gpu)
  include(cmake/VeyraGpuDis.cmake)
  veyra_add_compute_shader(veyra_shader_gpu_dis_luma GpuDisLuma)
  veyra_add_compute_shader(veyra_shader_gpu_dis_validate GpuDisValidate)
  add_dependencies(veyra_pipeline veyra_shader_gpu_dis veyra_shader_gpu_dis_luma veyra_shader_gpu_dis_validate)
  if(EXISTS "${VEYRA_FIDELITYFX_ROOT}/bin/ffx_sdk/ffx_opticalflow_x64.lib")
    target_compile_definitions(veyra_pipeline PRIVATE VEYRA_HAS_AMD_OF=1)
    target_include_directories(veyra_pipeline PRIVATE "${VEYRA_FIDELITYFX_ROOT}/include")
    target_link_libraries(veyra_pipeline PRIVATE
      "${VEYRA_FIDELITYFX_ROOT}/bin/ffx_sdk/ffx_opticalflow_x64.lib"
      "${VEYRA_FIDELITYFX_ROOT}/bin/ffx_sdk/ffx_backend_dx12_x64.lib")
  endif()
  target_compile_features(veyra_engine PUBLIC cxx_std_20)
  target_compile_options(veyra_engine PRIVATE /utf-8)
  target_compile_definitions(veyra_engine PUBLIC VEYRA_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}" VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
  target_link_libraries(veyra_engine PUBLIC veyra_pipeline veyra_sources veyra_sinks veyra_ngx veyra_nvof)
  add_executable(veyra_repair_preset_tests tests/unit/RepairPresetTests.cpp src/engine/PresetStore.cpp)
  # CPU bake of the colour grade: no GPU, no engine link - the tables the ingest
  # shader reads are pure maths and must be verifiable without a device.
  add_executable(veyra_color_grade_tests tests/unit/ColorGradeTableTests.cpp)
  target_compile_features(veyra_color_grade_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_color_grade_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_color_grade_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_color_grade_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_color_grade_tests PRIVATE veyra_pipeline)
  # GPU contract for the v4 colour chain: identity when off/neutral, exposure,
  # curve, saturation and hue, all through a real graph and its ingest pass.
  add_executable(veyra_color_grade_gpu_tests tests/integration/ColorGradeGpuTests.cpp)
  target_compile_features(veyra_color_grade_gpu_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_color_grade_gpu_tests PRIVATE /utf-8 /WX-)
  target_compile_definitions(veyra_color_grade_gpu_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_color_grade_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(veyra_color_grade_gpu_tests PRIVATE veyra_pipeline veyra_sinks)
  # Adobe .cube parsing + the runtime LUT store (CPU only).
  add_executable(veyra_color_lut_tests tests/unit/ColorLutTests.cpp src/engine/ColorLut.cpp)
  target_compile_features(veyra_color_lut_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_color_lut_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_color_lut_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_color_lut_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_color_lut_tests PRIVATE veyra_base bcrypt)
  # Named colour looks + .vpcolor export/import (CPU only).
  add_executable(veyra_color_look_tests tests/unit/ColorLookTests.cpp src/engine/ColorLookStore.cpp)
  target_compile_features(veyra_color_look_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_color_look_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_color_look_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_color_look_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_color_look_tests PRIVATE veyra_base)
  target_compile_features(veyra_repair_preset_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_repair_preset_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_repair_preset_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_repair_preset_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_executable(veyra_ui_contract_tests tests/unit/UiContractTests.cpp src/engine/PresetStore.cpp)
  target_compile_features(veyra_ui_contract_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_ui_contract_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_ui_contract_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(veyra_ui_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_executable(veyra_popup_selector_tests tests/integration/PopupSelectorTests.cpp)
  add_executable(veyra_subtitle_panel_tests tests/integration/SubtitlePanelTests.cpp)
  add_executable(veyra_subtitle_overlay_tests tests/integration/SubtitleOverlayTests.cpp)
  target_compile_features(veyra_subtitle_overlay_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_subtitle_overlay_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_subtitle_overlay_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_subtitle_overlay_tests PRIVATE veyra_engine gdiplus comctl32)
  target_compile_features(veyra_subtitle_panel_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_subtitle_panel_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_subtitle_panel_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_subtitle_panel_tests PRIVATE veyra_base comctl32)
  target_compile_features(veyra_popup_selector_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_popup_selector_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_popup_selector_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(veyra_popup_selector_tests PRIVATE veyra_base comctl32)
  add_dependencies(veyra_engine veyra_shader_hdr_video_sr veyra_shader_present_motion veyra_shader_present_blit)
  add_executable(veyra_hdr_color_tests tests/integration/HdrColorTests.cpp)
  add_executable(veyra_multichannel_tests tests/integration/MultichannelTests.cpp)
  target_compile_features(veyra_multichannel_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_multichannel_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_multichannel_tests PRIVATE veyra_engine)
  add_executable(veyra_hdr_enhancement_tests tests/integration/HdrEnhancementTests.cpp)
  add_executable(veyra_video_hdr_tests tests/integration/VideoHdrTests.cpp)
  target_compile_features(veyra_video_hdr_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_video_hdr_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_video_hdr_tests PRIVATE veyra_engine)
  target_compile_features(veyra_hdr_enhancement_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_hdr_enhancement_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_hdr_enhancement_tests PRIVATE veyra_engine)
  add_executable(veyra_source_fidelity_tests tests/integration/SourceFidelityTests.cpp)
  add_executable(veyra_file_safety_tests tests/integration/FileSafetyTests.cpp)
  target_compile_features(veyra_file_safety_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_file_safety_tests PRIVATE /utf-8)
  target_compile_definitions(veyra_file_safety_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_file_safety_tests PRIVATE veyra_engine)
  target_compile_features(veyra_source_fidelity_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_source_fidelity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_source_fidelity_tests PRIVATE veyra_engine)
  add_executable(veyra_hw_import_image_tests tests/integration/HardwareImportImageTests.cpp)
  target_compile_features(veyra_hw_import_image_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_hw_import_image_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_hw_import_image_tests PRIVATE veyra_engine)
  # Headless export harness: verifies the frame-timestamp policy (tail snap,
  # dropped-sample gap fill, hard refusal) without the GUI.
  add_executable(veyra_export_probe tools/export_probe/main.cpp)
  target_compile_features(veyra_export_probe PRIVATE cxx_std_20)
  target_compile_definitions(veyra_export_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_export_probe PRIVATE veyra_engine)
  target_compile_features(veyra_hdr_color_tests PRIVATE cxx_std_20)
  target_compile_definitions(veyra_hdr_color_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(veyra_hdr_color_tests PRIVATE veyra_engine)
  add_executable(veyra WIN32 apps/veyra/main.cpp apps/veyra/ui/AppShell.cpp apps/veyra/ui/CapturePanel.cpp apps/veyra/ui/SubtitleOverlay.cpp apps/veyra/SettingsWindow.cpp apps/veyra/TelemetryWindow.cpp)
  target_sources(veyra PRIVATE apps/veyra/ui/ScreenCapturePanel.cpp)
  add_executable(veyra_slider_reset_tests tests/integration/SliderResetTests.cpp apps/veyra/SettingsWindow.cpp)
  target_compile_features(veyra_slider_reset_tests PRIVATE cxx_std_20)
  target_compile_options(veyra_slider_reset_tests PRIVATE /utf-8)
  target_link_libraries(veyra_slider_reset_tests PRIVATE veyra_engine comctl32 comdlg32 shell32)
  if(VEYRA_ENABLE_REMOTEPLAY)
    add_executable(veyra_ps5_hw_image_tests tests/integration/Ps5HardwareImageTests.cpp)
    add_executable(veyra_ps5_quality_tests tests/integration/Ps5SourceQualityTests.cpp)
    target_compile_features(veyra_ps5_quality_tests PRIVATE cxx_std_20)
    target_compile_definitions(veyra_ps5_quality_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(veyra_ps5_quality_tests PRIVATE veyra_engine)
    target_compile_features(veyra_ps5_hw_image_tests PRIVATE cxx_std_20)
    target_compile_definitions(veyra_ps5_hw_image_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(veyra_ps5_hw_image_tests PRIVATE veyra_engine)
    find_package(SDL3 3.4.14 EXACT CONFIG REQUIRED)
    target_sources(veyra PRIVATE apps/veyra/ui/RemotePlayPanel.cpp src/remoteplay/ControllerInput.cpp)
    target_link_libraries(veyra PRIVATE SDL3::SDL3)
    add_executable(veyra_remoteplay_boundary_tests tests/remoteplay/ProductBoundaryTests.cpp src/remoteplay/ControllerInput.cpp)
    target_compile_features(veyra_remoteplay_boundary_tests PRIVATE cxx_std_20)
    target_link_libraries(veyra_remoteplay_boundary_tests PRIVATE veyra_sources SDL3::SDL3)
    add_executable(veyra_remoteplay_profile_tests tests/remoteplay/ProfileStoreTests.cpp)
    target_compile_features(veyra_remoteplay_profile_tests PRIVATE cxx_std_20)
    target_compile_definitions(veyra_remoteplay_profile_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(veyra_remoteplay_profile_tests PRIVATE veyra_remoteplay_chiaki)
  endif()
  target_compile_features(veyra PRIVATE cxx_std_20)
  target_compile_options(veyra PRIVATE /utf-8)
  target_link_libraries(veyra PRIVATE veyra_engine comctl32 comdlg32 shell32)
  add_dependencies(veyra veyra_shader_present_blit)

  # matrix executes through the shared product libraries only.
  add_executable(veyra_quality_probe
    tools/quality_probe/main.cpp
    tools/nr_harness/harness_util.cpp
  )
  veyra_apply_warnings(veyra_quality_probe)
  target_compile_options(veyra_quality_probe PRIVATE /WX-)
  target_compile_features(veyra_quality_probe PRIVATE cxx_std_20)
  target_compile_definitions(veyra_quality_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
  target_include_directories(veyra_quality_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(veyra_quality_probe PRIVATE
    veyra_pipeline veyra_sources veyra_media veyra_ngx veyra_nvof veyra_sinks
    ${FFMPEG_LIBRARIES} psapi dxgi)
  add_dependencies(veyra_quality_probe
    veyra_shader_yuv_to_rgb veyra_shader_parity_encode
    veyra_shader_parity_decode veyra_shader_scale_blit
    veyra_shader_nv12_upload veyra_shader_nvof_densify)

  # Headless Remote Play probe: drives the product session stack with a saved
  # profile so codec/bitrate variants can be compared without the GUI.
  if(VEYRA_ENABLE_REMOTEPLAY)
    add_executable(veyra_remoteplay_probe tools/remoteplay_probe/main.cpp)
    target_compile_features(veyra_remoteplay_probe PRIVATE cxx_std_20)
    target_compile_options(veyra_remoteplay_probe PRIVATE /WX-)
    target_compile_definitions(veyra_remoteplay_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(veyra_remoteplay_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(veyra_remoteplay_probe PRIVATE veyra_sources veyra_gfx ${FFMPEG_LIBRARIES})
  endif()

  # Phase 6 realtime engine probe: decode -> SR -> NR -> NVOF -> DLSSG -> present.
  if(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR AND EXISTS "${VEYRA_NVOF_SDK_ROOT}/NvOFInterface/nvOpticalFlowCommon.h")
    add_executable(veyra_player_probe
      tools/player_probe/main.cpp
      tools/nr_harness/harness_util.cpp
    )
    veyra_apply_warnings(veyra_player_probe)
    target_compile_options(veyra_player_probe PRIVATE /WX-)
    target_compile_features(veyra_player_probe PRIVATE cxx_std_20)
    target_compile_definitions(veyra_player_probe PRIVATE
      UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
      VEYRA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
    target_link_libraries(veyra_player_probe PRIVATE
      veyra_media veyra_gfx veyra_ngx veyra_nvof veyra_parity veyra_sinks veyra_pipeline
      ${FFMPEG_LIBRARIES} psapi ole32 mmdevapi avrt)
    add_dependencies(veyra_player_probe
      veyra_shader_yuv_to_rgb veyra_shader_parity_encode
      veyra_shader_parity_decode veyra_shader_scale_blit veyra_shader_nv12_upload
      veyra_shader_present_blit veyra_shader_nvof_densify)
  endif()
endif()

# Clip generator: links the openh264-enabled toolchain FFmpeg ONLY.
if(VEYRA_CLIP_TOOLS_ROOT AND EXISTS "${VEYRA_CLIP_TOOLS_ROOT}/include/libavcodec/avcodec.h")
  add_executable(veyra_clip_gen tools/clip_generator/main.cpp)
  veyra_apply_warnings(veyra_clip_gen)
  target_compile_options(veyra_clip_gen PRIVATE /WX-)
  target_compile_features(veyra_clip_gen PRIVATE cxx_std_20)
  target_compile_definitions(veyra_clip_gen PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(veyra_clip_gen PRIVATE "${VEYRA_CLIP_TOOLS_ROOT}/include")
  target_link_directories(veyra_clip_gen PRIVATE "${VEYRA_CLIP_TOOLS_ROOT}/lib")
  target_link_libraries(veyra_clip_gen PRIVATE
    veyra_base
    "${VEYRA_CLIP_TOOLS_ROOT}/lib/avcodec.lib"
    "${VEYRA_CLIP_TOOLS_ROOT}/lib/avformat.lib"
    "${VEYRA_CLIP_TOOLS_ROOT}/lib/avutil.lib")
endif()

# Layered child subtitle windows require a modern-Windows compatibility manifest.
configure_file(apps/veyra/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/veyra-version.rc @ONLY)
configure_file(apps/veyra/resource.h ${CMAKE_CURRENT_BINARY_DIR}/veyra-resource.h COPYONLY)
configure_file(assets/veyra.ico ${CMAKE_CURRENT_BINARY_DIR}/veyra.ico COPYONLY)
target_sources(veyra PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/veyra-version.rc)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/veyra-version.rc PROPERTIES
  OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/veyra.ico;${CMAKE_CURRENT_SOURCE_DIR}/apps/veyra/resource.h")
target_link_options(veyra PRIVATE "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/apps/veyra/veyra.manifest")
