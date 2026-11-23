# Xbox: add selectable V2 transport (Chromium reference player in WebView2) + HQ profile, D3D11VA option, loss recovery, HUD

## Why

The existing Xbox home-streaming path (V1, native libdatachannel) works, but on
real home networks its loss recovery is weaker than the reference clients
(XStreaming / Greenlight), and some Intel iGPUs silently corrupt D3D12VA H.264
output. Users also had no way to see what the host/network actually deliver
before the enhancement chain. This PR adds a **selectable V2 transport** that
uses the same receive stack as the reference clients, keeps V1 as the default
off-switchable alternative, and surfaces stream statistics — without touching
the super-resolution / frame-generation / rendering chain.

## What changed

- **V2 transport (`XboxWebSource`)**: an offscreen WebView2 runs a vendored,
  MIT-licensed build of `XStreaming-player`. Chromium's libwebrtc owns
  NACK/jitter/reordering; an `RTCRtpScriptTransform` exports complete Annex-B
  H.264 access units and Opus packets **before** decode, fed to the existing
  `CaptureCompressedDecoder` / `CaptureAudioSession` with zero bitstream
  conversion. Super resolution, frame generation and rendering are unchanged.
  - The transform is attached to the transceiver receivers **before**
    `createOffer`/`setLocalDescription`: a receiver transform attached at
    `ontrack` is silently bypassed on real-console SDP (Chromium bug
    w3c/webrtc-encoded-transform#314 / chromium 467247265). Export uses
    `worker.onmessage`; the ICE exchange waits for gathering to complete.
- **V1 retained and selectable**: `IXboxSource` abstracts both engines;
  `XboxConnectDesc.transportEngine` (1=V1, 2=V2, default 2) is persisted and
  exposed as a dialog selector. V1 keeps libdatachannel + its loss recovery.
- **HQ profile**: 1080p HQ tier (tizen browser identity), public STUN, full
  Teredo candidate expansion; bitrate cap; fixed profile shared by both engines.
- **D3D11VA (DXVA) decode option** for Intel iGPUs that corrupt D3D12VA output;
  software-decode compatibility switch retained.
- **Stream HUD** (`Ctrl+Alt+Shift+S`): resolution/codec/decode path, RTT, decode
  ms, bitrate, received/decoded fps, drops, retransmit recovery. The HUD toggle
  is no longer gated behind `VEYRA_ENABLE_MOONLIGHT`.
- **Gamepad**: shared `GamepadBridge.h` mapper (exact inverse of
  `gamepadFromController`, Y pass-through because Packet.ts negates on the wire;
  INT16_MIN clamps to -32767). Rumble returns through `ControllerFeedback`.
- **Build**: `cmake/VeyraXboxWeb.cmake` + `VeyraXboxWebEmbed.cmake` embed the
  player bundle as a byte array (no Node/Qt at C++ build time). New dependency:
  vcpkg `webview2` (static loader, no DLL ships; Evergreen runtime required).

## Scope / dependencies

- `vcpkg.json`, `.gitignore`, `CMakePresets.json` and every existing
  `VEYRA_*_ROOT` default are **byte-identical to main**. libdatachannel / Opus /
  nlohmann-json / FidelityFX optical-flow libs are pre-existing upstream
  requirements with identical CMake variables.
- The only added package is the vcpkg `webview2` port, obtained exactly like
  libdatachannel/opus (out-of-manifest static install on `CMAKE_PREFIX_PATH`):
  `vcpkg install webview2:x64-windows-static`. `-DVEYRA_ENABLE_XBOX=OFF` remains
  the default and needs no new SDK.
- Vendored artifact: `resources/xbox-player/veyra-xbox-player.js` (MIT).
  Provenance + rebuild: `resources/xbox-player/README.md`; license:
  `licenses/xbox/xstreaming-player-MIT.txt`; notices: `THIRD_PARTY_NOTICES.md`.
  Base `8e178aef`, patch `e4c12bd` (fork to be published / described here).

## Tradeoffs / blast radius

- V2 adds a WebView2 process and a base64/WebMessage hop (~1-3 ms/frame) and
  Chromium's adaptive jitter buffer; in exchange it gets reference-grade loss
  recovery. V1 remains for users who prefer the in-process path.
- Generated/decoded frames flow through the same enhancement graph; no change
  to SR/FG/export/virtual-camera behaviour.
- Xbox-only surface: no `remoteplay`, `moonlight`, `sink`, `beauty`, `gfx`
  changes. (A separate follow-up PR handles the cross-cutting SR-availability
  migration + AMD optical-flow enablement.)

## Verification

- `veyra_xbox_tests`: **119 checks, 0 failures** (upstream V1 loopback restored
  + HQ profile / resilience cases added). `veyra_xbox_web_gamepad_tests`:
  **30 checks, 0 failures**.
- Real console (Xbox Series, H.264 D3D11VA): V1 and V2 both reach `streaming`
  with decoded frames + Opus; V2 `amd-of`/NVOF optical-flow FG stays enabled
  (`Create/Dispatch result=0`); engine switch + HUD verified via
  `scripts/ui-check/xbox-*.qml`.
- Offline WebView2 probes in `tools/wv2-probe/` confirm complete Annex-B AUs
  with in-band SPS/PPS/IDR and the base64 channel sustaining 1080p60@15Mbps.

## Not covered

- No real-console A/B latency measurement vs XStreaming; perceived quality on
  a given setup still depends on the host's per-client encoder decision.
- Intel Arc native XeSS MFG (3X/4X without the existing unlock) is intentionally
  out of scope here.