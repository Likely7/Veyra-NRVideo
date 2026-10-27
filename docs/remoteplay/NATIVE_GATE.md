# Windows 原生 Chiaki 构建门槛

> 当前状态已由 [修复执行记录](../history/reports/REMOTEPLAY_REPAIR_EXECUTION_2026-09-11.md) 更新：metadata patch 已恢复、共享生产 CMake 和 UI 已接入，旧主体保留供追溯，不作为当前失败/成功结论。

**当前状态：Windows x64/MSVC 原生门槛已通过。** 固定 chiaki-ng 提交 `0e16950165f06e5c3291537c2eeba6e852be7120` 在本机完成 248/248 个构建步骤；真实 probe 退出码为 0，并输出 `REAL_CHIAKI_CORE_INITIALIZED upstream_video_callback=1`。这只证明真实 `chiaki-lib` 能构建、链接和初始化，PS5 连接、真码流解码、音频、手柄、Windows 播放器和增强均未执行。当前状态和后续任务以 [`../REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md`](../history/reports/REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md) 为准。

本次通过使用的目录：

```text
Clean checkout: C:\veyra-deps\chiaki-source
Patched stage:  C:\Users\123\Desktop\Veyra DLSS Video Player\out\remoteplay\chiaki-msvc-stage
Dependencies:   C:\veyra-deps\remoteplay-installed\x64-windows-static
Build:          out\remoteplay\native-fresh5
```

原生 probe 的完整边界输出：

```text
REAL_CHIAKI_CORE_INITIALIZED upstream_video_callback=1
PS5_CONNECTION_NOT_TESTED VIDEO_DECODE_NOT_TESTED WINDOWS_PLAYER_NOT_TESTED
```

**二次审计补充：** `upstream_video_callback=1` 是 probe 的固定文字，不是实际回调次数，probe 没有启动会话或执行视频回调。主程序适配层已有真实离线失败，详见交接第18节。此前构建248步的记录仍有效，但旧命令漏了工具参数，不能凭成功 cache 认为新目录可直接复现。

## 1. 输入条件

在与 Veyra 一致的 Windows x64 MSVC 环境准备 CMake 3.24+、Ninja、Python 和真实开发依赖。协议库使用 C，Veyra 新基础库保持 C++20；静态 MSVC runtime 与原应用统一。不要把 MinGW `.a` 当成 MSVC `.lib` 混用。

准备：Opus、OpenSSL、json-c、libevent、miniupnpc 的匹配工具链版本及 pkg-config／CMake 包配置；nanopb 和 protobuf 生成工具；固定上游的 Jerasure、gf-complete、curl 递归子模块。关闭 RUDP 不代表这些 CMake 依赖会自动消失。

本门槛不引入 Qt、libplacebo、另一套 FFmpeg 或任何 NVIDIA SDK。不要因为配置失败创建空 imported target、删除 FEC 源码或忽略链接错误。

## 2. 固定上游源代码

以下目录为例子，不能覆盖已有工作区；这些命令需要联网：

```powershell
git clone --no-checkout https://github.com/streetpea/chiaki-ng.git C:/veyra-deps/chiaki-source
git -C C:/veyra-deps/chiaki-source checkout --detach 0e16950165f06e5c3291537c2eeba6e852be7120
git -C C:/veyra-deps/chiaki-source submodule update --init --recursive
git -C C:/veyra-deps/chiaki-source status --short
```

当前使用 `scripts/remoteplay/build-native.ps1` 创建独立 git worktree，并应用 `patches/0001-chiaki-msvc-vla-compat.patch`。原用户包的 `prepare_chiaki.py` 与 metadata JSON 没有导入；当前 backend 使用未扩展的上游 callback，缺少真实帧号/实际 profile 元数据，这已列为必修项。不要把原包的两文件 metadata patch 误当成当前本机五文件 MSVC patch。

当前 stage 差异全文经本次审计与 MSVC patch 比较一致；但脚本只凭文件名和 reverse-apply 检查，尚不足以拒绝同文件内的额外修改。下一位必须完善全量 stage、index、commit 和递归子模块验证。恢复元数据扩展时，新 header 与库必须共同重建，不得混用旧二进制。

## 3. 运行原生构建

在 Veyra 根目录、已配置 MSVC 的 x64 Developer PowerShell 中：

```powershell
powershell.exe -NoProfile -File scripts/remoteplay/build-native.ps1 `
  -Root . `
  -ChiakiCheckout C:/veyra-deps/chiaki-source `
  -StageDirectory "C:/Users/123/Desktop/Veyra DLSS Video Player/out/remoteplay/chiaki-msvc-stage" `
  -BuildDirectory out/remoteplay/native-handoff `
  -PrefixPath C:/veyra-deps/remoteplay-installed/x64-windows-static `
  -ProtocPath C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe `
  -PkgConfigPath C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe
```

以上依赖路径来自本机成功 cache，换机需定位实际文件。旧参数组没有 ProtocPath，在全新目录真实失败 `Could not find protoc`，日志 `logs/remoteplay-audit-20260911/configure-from-handoff.log`；这份补全后的命令尚未重新全量执行。使用 vcpkg 时可通过 `-ToolchainFile` 指定真实文件；库要选择与 `/MT` 一致的 triplet。脚本不自动切换／覆盖 Veyra 原 FFmpeg 包。

若此前已生成本机 staging 且要直接运行 CMake，不删除旧目录；须同时传 clean verify checkout 和工具位置：

```powershell
cmake -S services/remoteplay-probe -B out/remoteplay/native-handoff -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DVEYRA_RP_BUILD_NATIVE=ON `
  "-DVEYRA_RP_CHIAKI_SOURCE_DIR=C:/Users/123/Desktop/Veyra DLSS Video Player/out/remoteplay/chiaki-msvc-stage" `
  -DVEYRA_RP_CHIAKI_VERIFY_DIR=C:/veyra-deps/chiaki-source `
  -DCMAKE_PREFIX_PATH=C:/veyra-deps/remoteplay-installed/x64-windows-static `
  -DPROTOC=C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe `
  -DPKG_CONFIG_EXECUTABLE=C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build out/remoteplay/native-handoff --config Release --target veyra_remoteplay_native_probe
ctest --test-dir out/remoteplay/native-handoff -C Release -L native-core --output-on-failure --timeout 30
```

## 4. 真实通过标准

必须链接实际 `chiaki-lib`，执行 `chiaki_lib_init`、上游 profile 预设和手柄 idle 契约检查。记录 CMakeCache、编译器、架构、依赖版本、完整命令及退出码。

只有真实执行后打印的 `REAL_CHIAKI_CORE_INITIALIZED` 才能作为这个小门槛的结果；字符串出现在源码里不算执行。**即使通过，也未测试 PS5 连接、解码、音频播放、GPU 和增强。**

## 5. 协议桥使用契约

所有 `ChiakiBackend` 操作（包括 snapshot）在同一个会话 owner 线程调用。网络回调仅复制输入、投递到 SessionInbox；不调用增强图、Present、UI、磁盘日志或 join。

每次建立会话：`SessionInbox.begin(commonOrigin)` → 将其 token 传给 `ChiakiBackend.start(request, token)`。请求含明确的 host、720/1080p 30/60 SDR 预设和 8/16/16 字节配对凭据；不把它们塞进路径、环境变量或命令行。

解码消费端 `RemotePlaySource` 已落盘但有未修缺陷。`configBefore` 必须与 AU 形成经真解码验证的输入契约；当前单独送 H.264 配置头已复现失败，不能照旧代码直接接 UI。decoder reset、PTS/尺寸、EAGAIN adapter 与 metadata 的详细动作见主交接。`PacketPump` 的独立测试不能证明实际 source 使用了它。

`takeIdrRequest()` 须由 owner 周期性取走并调用 `backend.requestIdr()`，当前 source 尚未转发。PCM 经 Remote Play 专用音频 owner 适配现有 AudioRenderer/WASAPI，不套用采集卡 DirectShow 的 CaptureAudioSession。样本按每声道计数，固定音频段锚点＋样本偏移，不逐块用arrival重建时钟。

终止必须：inbox.invalidate → backend.stop（包含 stop/join/fini）→ 确认成功清理 → inbox.finishStop。新会话前先真实关闭旧 backend，不能只重置 inbox 来假装线程已经停止。

## 6. 未完成项和策略风险

保守的丢包处理会等待 IDR；H.265 CRA 并不被当作 IDR。需要真机确认编码习惯和恢复行为。`pairLocalPs5` 的超时是请求取消的时点，不是已证实的底层 join 时限；UI 不应同步等待它。

日志仅记录安全计数和操作错误码，未开启原始上游日志／hexdump。后续若引入更详细诊断，需要逐项脱敏，不能直接转发含注册响应的字符串。

本机原生门槛已记录为通过，但不能据此假定其他 Windows 环境或完整产品 CMake 已兼容。下一批工作是修复 `RemotePlaySource` 正确性并建立生产 CMake 的 ON/OFF 两种稳定配置。
