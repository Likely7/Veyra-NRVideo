# 2.0.9 本地测试构建

本轮在 codex/nr-sf-310916-20261017 整合 NR2060、粉丝字幕/鼠标穿透和 issues41–45 已完成修复，基线 main b571a44；main 未推进，未发布 GitHub。源码与 SDK/运行库/模型/测试媒体分离。

Windows x64、MSVC 14.44、CMake/Ninja、Qt 6.8.3；沿用现有 DLSS/Optical Flow/FidelityFX/串流 SDK。设置 `VEYRA_BUILD_QML_UI=ON`、`VEYRA_ENABLE_REMOTEPLAY=ON`、`VEYRA_ENABLE_MOONLIGHT=ON`、`VEYRA_ENABLE_XBOX=ON`、`VEYRA_ENABLE_DEINTERLACE=ON` 和 `VEYRA_DISPLAY_VERSION=2.0.9-test`。完整实用配置/命令及输入SHA记录在本轮 build 回执，不能将工具缓存的旧路径直接当作新机器路径。

FFmpeg 仍须包含 PS5 slices256、dav1d、zlib 字幕补丁。去隔行使用独立 LGPL avfilter-12 模块，不替换原 FFmpeg 五 DLL/dav1d；构建方式及固定来源见 BUILD_DEINTERLACE_2026-10-16.md、scripts/ffmpeg/build-deinterlace-module.py。新模块必须附 FFmpeg-9.0.1-deinterlace-module-source.zip，与既有 FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip 和 Veyra-2.0.6-dependency-source.zip 一起提供。

NVIDIA 包：SF 槽 `runtime/experimental/nr-ampere/nvngx_dlssnr.dll` 使用用户提供的 310.9.16.0、276162560 bytes、NotSigned、SHA256 CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED；保留 `nr-original` 中原 NVIDIA 310.8.0.0 原件。新包不含旧 Lecram 或 SF-v2。AMD 专有运行库/权重/内核沿用 2.0.8 字节。不要从游戏或驱动缓存提取组件、修改 DLL 或将其放入源码 Git。

界面默认 SF、性能优化开；关闭时明确选择同 DLL 的旧模型 A。原版置灰、AMD不显示这一开关。预设库13、会话8、旧设置32新增模型保存，旧文件默认为开启；模型选择跨导出进程的共享协议升级至9。新预设/会话保存必要的 .nr-model 旁档，保留旧构建文件。便携运行需完整解压，不仅替换 EXE。

本机测试脚本/产物统一在 E:/项目/Veyra/{tmp,build,tests,logs,reports,test-packages,verify}/nr-sf-310916-20261017。最终实测和限制见 NR_SF_310916_REPORT_2026-10-17.md；各包 package-manifest.json、release-runtime-manifest.json 和同级 DELIVERY.json 绑定对应源码、EXE及ZIP。运行时清单供审计，不锁定用户自行替换的 DLL。
