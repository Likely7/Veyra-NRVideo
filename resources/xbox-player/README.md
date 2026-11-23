# Xbox V2 player bundle (vendored build artifact)

`veyra-xbox-player.js` is the **built** IIFE bundle of the V2 Xbox transport
player. It is consumed by `cmake/VeyraXboxWebEmbed.cmake`, which embeds it as a
plain byte array into `veyra_xbox_web` at build time (no Node/Qt needed to build
the C++ side). `WebView2Host` loads it into an offscreen WebView2.

## Provenance

- Upstream: https://github.com/Geocld/XStreaming-player (MIT, declared in
  `package.json`; the upstream repo ships no LICENSE file — see
  `../../licenses/xbox/xstreaming-player-MIT.txt`).
- Base commit: `8e178aef067ec31625311865e5ee51a6924823e2` ("Add trigger swap options").
- Local patch (one commit): `e4c12bd27f73e99b08b7f54eeccd2c27c330bff9`
  "xbox v2: attach encoded transform before negotiation (Chromium
  receiver-transform bug)".

## What the patch changes (and why)

1. **Attach `RTCRtpScriptTransform` to the transceiver receivers BEFORE
   `createOffer`/`setLocalDescription`.** A receiver transform attached at
   `ontrack` (after negotiation) is silently bypassed on real-console SDP:
   frames go straight to the decoder and `reader.read()` never resolves
   (Chromium bug, w3c/webrtc-encoded-transform#314 / chromium 467247265).
   Attaching up front wires the transformer callback before the stream is
   activated.
2. **Export via `worker.onmessage` (`self.postMessage`)**, not a transferred
   `MessagePort` in the transform options — the port variant stalled the
   encoded readable on the real console stream; `self.postMessage` is the
   mechanism verified by the offline probes in `../../tools/wv2-probe/`.
3. **Wait for ICE gathering to complete** before the xHome ICE exchange, and
   apply buffered remote candidates only after `setRemoteDescription` settles.
4. Adds the Veyra host bridge (`VeyraBridge`), host-driven gamepad
   (`HostGamepadDriver`), encoded-export worker, and the `veyra-main` entry.

## Rebuild

```bash
git clone https://github.com/Geocld/XStreaming-player
cd XStreaming-player
git checkout 8e178aef067ec31625311865e5ee51a6924823e2
# apply the Veyra patch (published fork / PR body), then:
npm ci
npm run build
# stage the artifact:
cp dist/assets/veyra-xbox-player.js <veyra>/resources/xbox-player/
```

Dev override: set `VEYRA_XBOX_PLAYER_JS` to a bundle path to load it from disk
instead of the embedded array.

## License

MIT (upstream `package.json`). Attribution text:
`../../licenses/xbox/xstreaming-player-MIT.txt`.