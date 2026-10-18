
## build-author 的构建配置留档（2026-10-04 已删除该目录以节省空间）

若需重建"按作者官方预设"的版本（与 build-nv 功能相同）：

    cmake -S <源码> -B <新目录> -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH="D:/Program Files/Qt/6.8.3/msvc2022_64" `
      -DVEYRA_BUILD_QML_UI=ON -DVEYRA_DISPLAY_VERSION=2.0.3 `
      -DVEYRA_ENABLE_D3D12_DEBUG=OFF `
      -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON `
      -DVEYRA_CUDA_DRIVER_INCLUDE_DIR="<源码>/third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/cudart_header" `
      -DVEYRA_DLSS_SDK_ROOT="<源码>/third_party_local/nvidia/DLSS-SDK-310.7" `
      -DVEYRA_NVENC_HEADERS_ROOT="<源码>/third_party_local/nvidia/nv-codec-headers-13.0" `
      -DVEYRA_FFMPEG_ROOT="<根>/deps/ffmpeg/ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0"

删除时该目录的配置摘要：

CMAKE_BUILD_TYPE:STRING=Release
CMAKE_CXX_COMPILER:FILEPATH=C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe
CMAKE_PREFIX_PATH:UNINITIALIZED=D:/Program Files/Qt/6.8.3/msvc2022_64
VEYRA_BUILD_QML_UI:BOOL=ON
VEYRA_BUILD_QT_PROBE:BOOL=OFF
VEYRA_CLIP_TOOLS_ROOT:PATH=
VEYRA_CUDA_DRIVER_INCLUDE_DIR:PATH=D:/项目/Veyra-NV/Veyra-NRVideo/third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/cudart_header
VEYRA_DISPLAY_VERSION:STRING=2.0.3
VEYRA_DLSS_SDK_ROOT:PATH=D:/项目/Veyra-NV/Veyra-NRVideo/third_party_local/nvidia/DLSS-SDK-310.7
VEYRA_ENABLE_DLSS_FG:BOOL=OFF
VEYRA_ENABLE_DLSS_SR:BOOL=OFF
VEYRA_ENABLE_EXPERIMENTAL_DLSSNR:BOOL=ON
VEYRA_ENABLE_MOONLIGHT:BOOL=OFF
VEYRA_ENABLE_NVOF:BOOL=OFF
VEYRA_ENABLE_REMOTEPLAY:BOOL=OFF
VEYRA_ENABLE_XBOX:BOOL=OFF
VEYRA_FFMPEG_ROOT:PATH=D:/项目/Veyra-NV/deps/ffmpeg/ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0
VEYRA_FIDELITYFX_ROOT:PATH=D:/项目/Veyra-NV/Veyra-NRVideo/third_party_local/amd/FidelityFX-SDK/sdk
VEYRA_MAGEWELL_SDK_DIR:PATH=
VEYRA_NVENC_HEADERS_ROOT:PATH=D:/项目/Veyra-NV/Veyra-NRVideo/third_party_local/nvidia/nv-codec-headers-13.0
VEYRA_NVOF_SDK_ROOT:PATH=
VEYRA_RUNTIME_ROOT:PATH=
VEYRA_XESS_ROOT:PATH=D:/项目/Veyra-NV/Veyra-NRVideo/third_party_local/intel/xess-3.0.2
veyra_BINARY_DIR:STATIC=D:/项目/Veyra-NV/build-author
veyra_IS_TOP_LEVEL:STATIC=ON
veyra_SOURCE_DIR:STATIC=D:/项目/Veyra-NV/Veyra-NRVideo
CMAKE_GENERATOR:INTERNAL=Ninja
