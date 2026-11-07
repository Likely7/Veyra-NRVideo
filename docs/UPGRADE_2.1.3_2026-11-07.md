# 升级到 2.1.3（定制版 + 作者版双切换）

2026-11-07。用户说作者发布了 2.1.3，包在 `D:\下载\DLSS5\Veyra-2.1.3-NVIDIA-win64-portable`，要求更新成 2.1.3 并保留个人定制版 / 作者完整版双切换。

## 结果

- 定制源码分支：`test/merge-213`（`ae1c95b1` Merge v2.1.3 into the local custom line），基于 `test/merge-212` 的 18 个定制提交合并上游 `v2.1.3`（`0a09d376`）。
- 部署目录：`D:\Program\Veyra-2.1.3-NVIDIA-win64-portable`，含两个启动脚本与 `.orig` / `.custom` 两套备份。
- 定制 exe：`c1e130ec09c2ea7ec28fdd106133a21f…`（Ninja 构建，`build/`），PE 2.1.3。曾先用 VS 生成器构建 `f1a30ca3…` 应急，Ninja 修复后已整体换成 Ninja 产物，VS 构建目录 `build-vs213` 已删除。
- 作者 exe：`76c4a44ee582dcd4150152c4e32e5e45f61fee1a35a285818dc7ee1464940f7a`（与作者包一致）。
- 两个启动脚本实测互切并各自启动成功（见下）。

## 上游 2.1.3 更新（2.1.2..v2.1.3，35 个提交）

- 三类 RTX 视频超分（标准 / 高码率 / 流媒体），档位改为低 / 中 / 高 / 超高，默认中档；双重超分与超采样可分别选版本和档位。
- NR 参数组内「网状 / 列表」切换，九项手动调控共用同一组参数。
- RTX 30 系 VFG 实验兼容路径；RX 7000 核显 + 独显环境的 NVAPI LUID / HIP 选卡修复。
- 播放与导出：**全屏进度条拖动不再被取消**、NVENC 周期 IDR 与 MP4 跳转索引、菜单文字弹簧回跳修正、低延迟高负载警告只在「开＋增强」显示。
- 验证：QtQuick 173 通过 / 0 失败 / 0 跳过（作者口径）。

## 合并与冲突

`git merge v2.1.3` 自动合并了绝大多数文件，冲突 4 个：

| 文件 | 处理 |
|---|---|
| `i18n/catalog.json` | 用脚本按 `(zh, ctx)` 合并两份条目（v2.1.3 在前、HEAD 独有追加），再跑 `scripts/i18n/extract.py` 按当前源码重建：2953 条，占位符问题 0，缺译 189（其中 171 条是 B 站模块本来就未翻译的界面文字，18 条为新增文案，均回退显示中文原文）。 |
| `qml/Veyra/CineBar.qml` | 保留本仓库「拖动停稳再 seek」的节流（250 ms + `lastSeekTarget` 去重），同时接收上游的 `interacting` 含 `scrubbing`、`DragHandler` 用 `!bar.interacting`、`onEnabledChanged` 清理——这正是全屏拖动被取消的修复。 |
| `src/engine/PresetLibrary.cpp` | 预设库 / 会话版本号两边都占了 18 / 13，重新分配见下。 |
| `src/ui/bridge/Preferences.cpp` | 白名单两边并列：B 站（biliQuality / biliCodec / biliAudio / danmaku*）与上游 nrResidualView / nrCorrectionView 都保留。 |

### 预设库与会话版本号（关键）

上游 v2.1.3 把库 v18、会话 v13 给了「RTX Video SR 高码率 / 流媒体」（`vfxVideoSr` 值 16..23），本仓库原来用 18 / 13 表示「自动补帧倍数」。两边同时占用会写读错位（2.0.4 时踩过一次），因此：

- 库：`autoFg ? 19 : videoAi ? 18 : dual ? 17 : srStage ? 16 : …`，autoFg 字段仍从 v16 起写入（历史存档兼容）。
- 会话：`autoFg ? 14 : videoAi ? 13 : dual ? 12 : srStage ? 11 : …`，解码上限 13 → 14。
- 内嵌库版本映射：`version>=14?19 : version>=13?18 : version>=12?17 : version>=10?16 : version>=9?14 : version>=8?13 : 11`。
- 扩展名：库 v19 写 `.auto-fg`、会话 v14 写 `.auto-fg`；load 在 `.video-ai` 之后检测 `.auto-fg`，优先级最高。

影响：用户 2.1.2 的 v18 存档读入时按 18 解析（含 autoFg 字段，因为 v16+ 都写），保存时升到 v19 / `.auto-fg`，旧文件不覆盖。作者版 2.1.3 写的 v18 存档不含 autoFg 字段，只在两个版本数据目录混用时才会错位——两个 `.cmd` 各带 `--data-dir`，文档已写明不要混。

## 构建

`build-vs213`，Visual Studio 17 2022 生成器 + 8 路并行（`UseMultiToolTask`），Release x64，参数与 2.1.2 一致：QML UI、DLSSNR、DLSS SR/FG、NVOF、CUDA driver 头、FFmpeg、Qt 6.8.3、D3D12 调试层。

**Ninja 规则文件问题（已修复，2026-11-07）**：本机上 CMake 生成的 `build.ninja` 引用 `CXX_COMPILER__*_unscanned_Release`，却不写 `CMakeFiles/rules.ninja`，ninja 报 `unknown build rule`。与中文路径无关（纯 ASCII 全新目录可复现，CMake 3.30.9 / 3.31.6 / 4.4.5 都一样），最小项目正常。

根因在 `CMakeLists.txt` 第 19–30 行的 MSVC include 前缀探测：`cl.exe` 在中文 Windows 上输出 **GBK 字节**的 `注意: 包含文件: `（实测 `d7a2d2e2 3a20 b0fcbaac cec4bcfe 3a20`），这段被 `set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")` 交给 CMake；CMake 写 `rules.ninja` 时按 UTF-8 处理，写不出来就删掉临时文件且不报错，于是只剩一个引用空规则的 `build.ninja`。

修法：探测保留，但只转发**纯 ASCII** 前缀（`if(CMAKE_MATCH_2 MATCHES "^[ -~]*$")`）；中文环境回退到 CMake 默认英文前缀。代价是本地化环境下头文件依赖前缀不匹配（改头文件不触发重编），构建本身正常。修复后 `rules.ninja` 511 KB / 1044 条规则，`veyra_base` 与 `veyra_qml_ui` 均编译通过。提交 `acbc7b84`。

## 部署与验证

```
D:\Program\Veyra-2.1.3-NVIDIA-win64-portable\
  veyra_qml_ui.exe          生效（当前 = 定制版 f1a30ca3…）
  veyra_qml_ui.exe.custom   定制版
  veyra_qml_ui.exe.orig     作者版 76c4a44e…
  qml\Veyra                 生效（= qml.custom，93 文件）
  qml.orig\Veyra            作者版 92 文件
  qml.custom\Veyra          定制版 93 文件
  shaders\                  PresentBlit_ps.dxil、YuvToLinearRgb.dxil 与作者版不同，各有 .orig/.custom；新增 19 个 dis\*.dxil
  启动-个人定制版.cmd / 启动-作者完整版.cmd / 版本切换说明.txt
```

- 定制版启动：`--data-dir runtime_local\user-data-2.0.0-test213 --exit-after 8000`，退出码 0，日志无 `Unexpected token` / `no root object` / `Type ... unavailable` / 绑定错误。
- 双切换实测：`启动-作者完整版.cmd --exit-after 8000` → exe 变 `76c4a44e…` 并启动；`启动-个人定制版.cmd --exit-after 8000` → exe 变 `f1a30ca3…` 并启动；当前停在定制版。
- 作者版启动时 `[update-check] current`，说明更新检查把 2.1.3 认成最新，不会误报有新版本。

## 未验证 / 注意事项

- 没有在真实采集卡 / 真实 60 Hz 屏 / AMD 实卡上跑功能验收；只做了启动与切换检查。
- 2.1.3 上游的 SR 三版本、NR 网状编辑、RTX30 VFG 实验路径都未逐项实测。
- 用户 2.1.2 的数据（`runtime_local\user-data-2.0.0-p5`、`runtime_local\luts`）尚未迁移到 2.1.3 目录，需要用户确认后复制。
- 旧目录 `D:\Program\Veyra-2.1.2-NVIDIA-win64-portable` 保持原样未动，可随时回退。
- 代码只在本地提交，未 push。

## 合并遗漏审计与修复（2026-11-07，用户验收通过）

`814ec867`（把定制从 2.1.1 搬到 2.1.2、同时拆分 `QmlPlayerBridge.cpp` → `src/ui/bridge/*`）丢了 8 段代码。2.1.3 merge 本身没有额外丢失——PR #80 分支（`7b72a241`）同样缺这 8 段，说明丢失早于这次 merge。

| # | 位置 | 后果 | 提交 |
|---|---|---|---|
| 1 | `tickSubtitles()` 位置追踪 | 从不写真实进度，详情页不能续播 | `598b1a54` |
| 2 | `togglePlayPause()` 播完 B 站分支 | 用过期 CDN 地址重连，403 | `2fb53b61` |
| 3 | `openPath()` B 站状态清理 | 本地文件继承上一部音频/Referer；弹幕不清 | `2fb53b61` |
| 4 | `openPath()` options 泄漏 | 同上（本仓库直接写 `impl_->options` 导致） | `37349c5c` |
| 5 | `openSourceUri()` 镜像重试重置 | 采集/串流打开不取消待触发的镜像重试 | `37349c5c` |
| 6 | `hdrSourceInfo()` DV 字段 | HDR 页看不到 profile/RPU/峰值/HDR10+ | `37349c5c` |
| 7 | `store/loadHdrTitleProfile()` | 路径归一化与 doviFollow/doviP7Compose 每片记忆丢失 | `37349c5c` |
| 8 | `openNetworkAddress()` B 站 URL 解析 | 粘贴 B 站链接不播放 | `37349c5c` |

同时清理了 2.1.1 遗留的 `pendingHeaders/pendingUserAgent/pendingAudioPath`（B 站改直写 `impl_->options` 后只剩赋值与 clear，无读取）。编译器 `/W4 /WX`、61 个偏好键白名单、505 个 QML 桥接成员均通过检查。

用户 2026-11-07 验收：8 项（续播、播完重播、本地文件音频、采集/捕获切换、DV 源信息、HDR 每片记忆、B 站链接播放）全部通过。部署 exe `78c0a584…`；未 push。
