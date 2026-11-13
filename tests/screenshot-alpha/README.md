# SDR screenshot alpha regression

Video/capture screenshots should be opaque even if the processed texture has
non-opaque alpha. The SDR `EngineController` save call selects `ImageAlpha::Opaque`
for video/capture and retains `Preserve` for still images. The opaque PNG writer
uses WIC 24bpp BGR, copying straight RGB bytes without compositing. Other
`saveImage` callers preserve transparency by default. JPEG already uses BGR.
The HDR JXR writer and the processing/presentation/recording paths are unchanged.

## Run without optional player SDKs

Requires Windows x64, MSVC 2022, CMake 3.24+, Windows SDK with DXC and the D3D12
debug layer, and a hardware D3D12 adapter supported by `D3D12DeviceContext`.
The test compiles the actual image sink, device context, command ring and final
`ScaleBlit` shader. No Qt, FFmpeg, NGX, vendor inference runtime or capture card
is required. Missing GPU/debug support is a failure, not a silent pass.

```powershell
cmake -S tests/screenshot-alpha -B <build-dir> -G "Visual Studio 17 2022" -A x64
cmake --build <build-dir> --config Release --parallel 2
ctest --test-dir <build-dir> -C Release -V
```

The root build also exposes `veyra_screenshot_alpha_tests` and CTest
`screenshot_alpha`. The standalone entry avoids the root project's optional SDK
requirements. Each invocation creates a fresh results directory and retains its
files and log. The deterministic fixtures do not use game screenshots.

Coverage includes straight RGBA PNG roundtrips (including RGB under zero alpha),
opaque screenshots, near-black gradients, dark green/red/brown textures with
channels below 32, a materialized WIC 8-bit premultiply/unpremultiply, JPEG alpha
independence, unchanged input buffers, destination collision protection,
multi-band PNG writes, padded GPU readback, the production final output shader,
and native FP16/scRGB and packed PQ HDR JXR save/decode. GPU debug errors fail
the test. JPEG is lossy: its assertion compares the same RGB with different
alpha policies, not JPEG pixels against the original RGB.

## Observed reproduction, 2026-11-13

Base: upstream main `8332c8a8382242096a2699d3ba4dd52ad47bbd74`; the relevant
save path is unchanged from `v2.1.5`. On RX 9070 XT, Windows driver
32.0.32015.7029, SDK 10.0.26100.0, MSVC 19.44:

- Initial unmodified writer: the four opaque-screenshot/consumer checks fail;
  straight PNG, JPEG, GPU readback and both HDR checks pass.
- Fixed writer: 21 checks pass. Native FP16 JXR has zero observed error;
  neutral packed-PQ JXR maximum absolute linear error is 0.000448809.
- A separate run of existing `HdrDisplayStateTests.cpp` and
  `HdrOutputTuningTests.cpp`, with their production dependencies, passes.

The supplied original PNG is 3840x2160, with mean alpha 18.2233, 0.86484% opaque
pixels, and **37** raw RGB-black pixels. Pillow and WIC straight RGBA decode
agree byte for byte. The earlier report of roughly 20.8% black is not a raw PNG
RGB measurement: a materialized WIC consumer conversion produces 1,730,063 black
pixels (20.8582%). The example RGB (33,33,29), A=4 becomes (63,63,0) in this WIC
conversion; a consumer rounding the unpremultiply to nearest can produce 64.
The fixed sink re-exports the supplied RGB without a single changed channel,
with decoded A=255 throughout and no loss in the same consumer roundtrip.

The reference screenshot is 3839x2159 and six seconds later. No resizing was
used for measurements. Offsets within +/-2 native pixels favor (0,0) in the
static left UI; its shared ROI is x=[0,1000), y=[400,1800). Raw RGB MAE there is
0.15676 codes; 99.3863% of channels are within one code. Animated regions cannot
be treated as same-frame ground truth. A round-nearest premultiply followed by
integer-truncated unpremultiply predicts 99.3778% of that ROI's WIC consumer
channels, but only 92.7549% across the shared frame. Neither this approximate
formula nor the earlier 97.99%/88.01% figures establishes exact codec arithmetic.

## Validation limits

The tests demonstrate the export-boundary bug: an opaque video frame's texture
alpha is exported as transparency, allowing a consumer to lose dark RGB detail.
The low-alpha texture, final shader, GPU readback and original PNG writer can
preserve straight RGB. The specific producer of the intermediate alpha and the
consumer used in the original report have not been identified; this change
does not assert a fault in capture, inference or WIC's straight PNG encoder.

A complete local QML player was subsequently built. An ASUS 4KPRO -> RX 9070 XT
capture with FSR 4.1.2 and AMD NR produced an opaque 3840x2160 F7 PNG from
2560x1440 input; the reporter confirmed normal display. Production WIC decode
and the materialized consumer roundtrip preserve all channels of that PNG.
F7 still reads the processed real frame, before presentation/UI composition.

Final review also passed the root-build `screenshot_alpha` CTest target,
eight ScreenshotCombination save/reopen cases, the HdrColor synthetic GPU
subset, and FileSafety graph/normal-EOS/overload-EOS modes. HDR display-state
and tuning unit tests pass with a fresh temporary directory; the existing
tuning persistence test is not repeatable in a reused temporary directory.
ScreenshotCombination requested HDR but the display's output remained SDR.

The local player build disables optional PS5/Moonlight/Xbox streaming and does
not stage libass/XeSS. Actual HDR capture/F7, recording integration and those
optional features were not retested. Their production code is outside this
patch. `ImageDimensionTests --tiled-color` fails all six cases with
`invalid staged SR extent` on both the untouched upstream base and this branch,
before image save; the per-case results match exactly. This pre-existing failure
is disclosed separately and is outside the screenshot fix.

Optional local inspection (creates large RGBA dumps; never uploads input):

```powershell
<test-exe> --inspect <original.png> <new-evidence-directory>
```

This exports straight, premultiplied and consumer RGBA dumps, and an opaque PNG
through the patched production writer. It asserts exact RGB and opacity, then
tests the opaque PNG through the same WIC consumer conversion. Game images and
generated dumps must stay outside source Git.
