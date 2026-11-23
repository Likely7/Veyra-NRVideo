# Xbox transport: V1 native and V2 enhanced (Chromium/WebView2)

Xbox home streaming offers two selectable transports (传输引擎 in the Xbox
dialog, persisted; default **V2 增强版**):

- **V1 原版** — the native libdatachannel session (`XboxSessionSource`):
  in-process WebRTC, Veyra's own NACK/reordering/keyframe recovery. Slightly
  lower latency on a clean network.
- **V2 增强版** — the same receive stack as the reference clients
  (XStreaming / Greenlight): the forked `xstreaming-player` (MIT) runs inside
  an offscreen WebView2. Chromium's libwebrtc owns NACK, the jitter buffer and
  reordering; an `RTCRtpScriptTransform` exports the reassembled encoded frames
  before decoding. Stronger loss/jitter handling on real home networks.

Both engines share the decoder/audio/enhancement chain and the same fixed
streaming profile (1080p HQ tier, public STUN, full Teredo expansion). The
dialog keeps: account, console list, gamepad forwarding, transport engine,
bitrate cap (0 = console decides), the software-decode compatibility switch,
the hardware decode API choice (D3D11VA for Intel iGPUs that corrupt D3D12VA
output), and an optional custom TURN relay.

## V2 critical implementation note

The encoded transform **must be attached to the transceiver receiver before
`createOffer`/`setLocalDescription`** (see `index.ts` `bind`). A transform set
at `ontrack` (after negotiation) is silently bypassed on real-console SDP —
frames go straight to the decoder and `reader.read()` never resolves
(Chromium bug w3c/webrtc-encoded-transform#314 / chromium 467247265). The
export worker uses `self.postMessage` (no transferred MessagePort), matching
the probe-verified mechanism in `tools/wv2-probe/`.

## What the transport does — and what it does not touch

- Video: the exported H.264 access units are complete Annex-B (4-byte start
  codes; keyframes carry in-band SPS/PPS/IDR — verified on WebView2 142 with
  the probes in `tools/wv2-probe/`). They feed the shared `CaptureCompressedDecoder`
  (D3D12VA, optional D3D11VA preference, software fallback) with no
  conversion. Super resolution, frame generation and rendering are untouched.
- Audio: complete Opus packets → `opus_decode_float` → `CaptureAudioSession`.
- Signalling: `Account`/`StreamApi` xHome provisioning, SDP/ICE exchange
  (including full Teredo expansion), keepalive and reconnect budget. The
  player does no signalling; C++ relays offer/answer/ICE over WebView2
  WebMessages on a dedicated worker (the STA callbacks never block).
- Controller: `ControllerState` → the player's InputFrame convention (the
  exact inverse of `gamepadFromController`, Y axis flipped; pinned by
  `veyra_xbox_web_gamepad_tests`, 29 checks). Rumble returns through the same
  `ControllerFeedback` slot.
- Keyframes: the player's Control channel pulses `videoKeyframeRequested`
  every 5 s as the reference client does; decode recovery additionally sends
  an explicit `{type:'keyframe'}` request through the host.

## Architecture

```
Xbox console ⇄ WebRTC ⇄ Chromium (WebView2, invisible)
                          │ RTCRtpScriptTransform (worker, inline blob)
                          │ complete Annex-B AU / Opus, base64 JSON WebMessage
                          ▼
                     WebView2Host (own STA thread + message pump)
                          │ callbacks (non-blocking; signalling queued to a worker)
                          ▼
                     XboxWebSource : IXboxSource
                          │ shared frame queue / decoder / audio / feedback
                          ▼
                     EngineController → EnhanceGraph (SR/FG) → presenter
```

- `veyra_xbox_web` (cmake/VeyraXboxWeb.cmake): `WebView2Host.cpp` + the player
  bundle embedded as a plain byte array at build time
  (`VeyraXboxWebEmbed.cmake`; no Qt runtime dependency). The WebView2 SDK is
  vcpkg `unofficial-webview2` (static loader — no DLL ships; the Evergreen
  runtime must be installed, standard on Windows 11).
- `XboxWebSource.cpp` lives in `veyra_sources` (it needs the shared decoder).
- `veyra_xbox_protocol` is now services only (WinHTTP + nlohmann-json);
  libdatachannel is no longer a dependency.
- Build switch: `-DVEYRA_ENABLE_XBOX=ON`.
- Player source: `xstreaming-player/` (MIT fork). Build with `npm run build`,
  stage `dist/assets/veyra-xbox-player.js` into
  `veyra-nrvideo/resources/xbox-player/`. Dev override: set
  `VEYRA_XBOX_PLAYER_JS` to a bundle path to load it from disk.
- User data: `%LOCALAPPDATA%/Veyra/xbox-webview2`.

## Verified offline

- Encoded-export probes (`tools/wv2-probe/`): complete Annex-B AUs with in-band
  parameter sets, `rtpTimestamp`/`type` metadata; the base64+JSON channel
  sustains 30 KB frames at 60 fps (1080p ≈ 15 Mbps) with zero loss.
- `veyra_xbox_tests` 92 checks (protocol/API/sign-in) and
  `veyra_xbox_web_gamepad_tests` 29 checks pass.
- Full product builds and links; UI smoke drives the dialog
  (`scripts/ui-check/xbox-v2-only.qml`: no selectors, bitrate/decode settings
  visible and persisted).

## What still needs a real console

Whether this closes the perceived quality gap against XStreaming on a given
setup can only be answered by an A/B on the same console/network: the
transport is now the reference stack, but the console's encoder decision per
client remains a variable. Compare against XStreaming with enhancements off
first (isolates transport), then with the usual enhancement chain.