# Veyra Xbox Enhanced (fork workspace)

English | [中文](#中文)

This workspace is a fork of [Likely7/Veyra-NRVideo](https://github.com/Likely7/Veyra-NRVideo)
with an enhanced, selectable **Xbox home-streaming transport** and related fixes.
It is prepared as two upstream pull requests; no proprietary binaries are added
to source control.

## What this fork adds

- **Xbox V2 transport (default)** — the reference-client receive stack: a vendored
  MIT build of [XStreaming-player](https://github.com/Geocld/XStreaming-player)
  runs in an offscreen WebView2; Chromium libwebrtc handles NACK/jitter/reorder and
  `RTCRtpScriptTransform` exports encoded H.264/Opus before decode into the existing
  hardware decoder + enhancement chain (super resolution / frame generation /
  rendering unchanged).
- **Xbox V1 (selectable)** — the original native libdatachannel session, retained.
- HQ profile (1080p HQ, public STUN, full Teredo), bitrate cap, D3D11VA decode option
  for Intel iGPUs, stream HUD (`Ctrl+Alt+Shift+S`), corrected gamepad Y/rumble mapping.
- **UI fixes** — unavailable saved SR migrates to FSR on non-NVIDIA GPUs (no gray
  dead-lock); AMD optical flow enabled so FSR/XeSS frame generation with optical
  flow no longer disables itself.

## Pull requests

| PR | Body (EN / 中文) | Content |
|----|------------------|---------|
| 1 | [.github/PR_XBOX_V2_EN.md](.github/PR_XBOX_V2_EN.md) / [CN](.github/PR_XBOX_V2_CN.md) | Xbox V2 transport + V1 selectable + HQ/D3D11VA/HUD/gamepad |
| 2 | [.github/PR_UI_SR_AMDOF_EN.md](.github/PR_UI_SR_AMDOF_EN.md) / [CN](.github/PR_UI_SR_AMDOF_CN.md) | SR availability migration + AMD optical flow for FG |

Design note: [`docs/XBOX_V2_TRANSPORT.md`](docs/XBOX_V2_TRANSPORT.md).
Vendored player provenance: [`resources/xbox-player/README.md`](resources/xbox-player/README.md).

## Build (dependencies identical to upstream)

Follow upstream `docs/BUILD_2.2.0.md` / `docs/DEVELOPMENT.md`. The only added
package is the vcpkg WebView2 port, obtained like libdatachannel/opus:

```bat
vcpkg install webview2:x64-windows-static --x-install-root=<deps\xbox-installed> --classic
cmake -S . -B out/build/xbox -G Ninja -DVEYRA_ENABLE_XBOX=ON ^
  -DCMAKE_PREFIX_PATH="<deps\xbox-installed\x64-windows-static;...>"
ninja -C out/build/xbox veyra_qml_ui veyra_xbox_tests veyra_xbox_web_gamepad_tests
```

`-DVEYRA_ENABLE_XBOX=OFF` (default) needs no new SDK. Optional AMD optical flow /
FSR / XeSS use the upstream `third_party_local` layout (gitignored, not committed).

## Tests

- `veyra_xbox_tests` — 119 checks (upstream V1 loopback + HQ/resilience cases).
- `veyra_xbox_web_gamepad_tests` — 30 checks (gamepad wire contract).
- UI smoke: `scripts/ui-check/xbox-*.qml`, `sr-ungray.qml`, `fg-*-flow.qml`.

## Portable packages (release artifacts, not in git)

Full AMD / NVIDIA portable ZIPs are assembled by combining a local build with the
official release runtime components; they are distributed as Release attachments,
never committed. See the PR bodies for the dependency posture.

## License

GPL-3.0 (upstream). Vendored player MIT; third-party notices in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

---

# 中文

本工作区是 [Likely7/Veyra-NRVideo](https://github.com/Likely7/Veyra-NRVideo) 的 fork，
增强并可选 **Xbox 家庭串流传输**及相关修复，准备为两个上游 PR；不向源码库添加专有二进制。

## 本 fork 增加

- **Xbox V2 传输（默认）**：参考客户端收流栈——vendored MIT 的
  [XStreaming-player](https://github.com/Geocld/XStreaming-player) 在离屏 WebView2 运行；
  Chromium libwebrtc 负责 NACK/抖动/重排，`RTCRtpScriptTransform` 解码前导出编码
  H.264/Opus 进现有硬解+增强链（超分/补帧/渲染不变）。
- **Xbox V1（可选）**：保留原生 libdatachannel 会话。
- HQ 档位（1080p HQ、公网 STUN、完整 Teredo）、码率上限、Intel 核显 D3D11VA 选项、
  串流 HUD（`Ctrl+Alt+Shift+S`）、修正手柄 Y/震动映射。
- **UI 修复**：非 N 卡不可用的已存超分迁移 FSR（无灰锁）；启用 AMD 光流，FSR/XeSS
  补帧选光流不再自我关闭。

## PR / 构建 / 测试 / 便携包 / 许可

见上表与英文部分；构建依赖与上游一致，仅新增 vcpkg `webview2`。便携包为 Release
附件，不入库。GPL-3.0，vendored 播放器 MIT。