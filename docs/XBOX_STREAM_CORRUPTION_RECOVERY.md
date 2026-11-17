# Xbox H.264 corruption recovery

Related report: [#138](https://github.com/Likely7/Veyra-NRVideo/issues/138).

## Failure and recovery

An affected Xbox Series S / Windows / RTX 5070 setup showed intermittent
smearing and displaced blocks, including with every enhancement disabled.
PC and console were both connected by Ethernet. The reporter found Microsoft's
client clean on the same setup. This comparison does not identify where the
damaged input originates.

A local copy of the compressed data delivered to the decoder reproduced the
smearing when decoded independently. In the bundled FFmpeg software decoder,
the initiating frame returned successfully with `decode_error_flags=12`
(concealment active and damaged slices), rather than a fatal decoder error.
The following dependent frames could therefore retain damaged references.

The shared compressed decoder now rejects flagged damaged frames in stream
mode. It returns a non-waiting failure before conversion/publication, so the
existing Xbox recovery path clears the reference history and requests an IDR.
Capture-card behavior is unchanged. Missing damage flags cannot be inferred
from the absence of a fatal error.

On the affected hardware path the natural incidents were not reliably flagged.
The Xbox dialog therefore adds an **opt-in Software video decoding** switch,
persisted for the next connection. The default remains hardware decoding.
Super resolution and frame generation remain GPU operations. Software decoding
can increase CPU usage and latency; it is a compatibility workaround.

The software path also needs the swscale result compared with the destination
height. The original check rejected valid conversions when the stream's SPS
dimensions differed from the initially requested output dimensions.

## Evidence and validation boundaries

- A controlled flagged-frame test recovered on the real Xbox session in 183 ms.
- With software decoding and damaged-frame rejection, four natural recovery
  events completed in 211, 309, 800 and 755 ms. The reporter subsequently
  completed a match without visible artifacts with enhancements off and with
  super resolution on. This is one setup and a limited observation period.
- The captured affected data was replayed against the bundled codec. The
  damaged frame was identified and rejected; decoding resumed at the next IDR.
- The swscale destination-row contract was checked using the bundled library.
- A native regression test was added for software decoding into a smaller
  output target. It would reject valid frames with the previous comparison.
- Translation validation reports zero missing translations and zero placeholder
  mismatches; `git diff --check` passes.

The field experiment used an instrumented/patched 2.1.9 executable. Its binary
checks are not a build of this source patch. In particular, the new dialog,
setting persistence and native C++ regression test still need a complete
Windows build and validation. No executable, captured gameplay, raw session
logs, account data, SDK or runtime DLL is included in this contribution.

This mitigates propagation/recovery of detected corruption. It does not prove
the cause of the initial damaged access unit or promise artifact-free streams
on every device.
