# RTX Video / FRUC trial — 2026-09-09

## Scope
No product code or defaults changed. Existing Phase 7 timing failure remains. RTX Video SDK has not been installed or executed. FRUC was actually executed on RTX 5070 as a standalone diagnostic; this is not capture integration or performance acceptance.

## Official interfaces
- RTX Video SDK 1.1 offers video super resolution, artifact reduction and SDR-to-HDR, supports RTX 50 and D3D12; it is not frame interpolation: https://developer.nvidia.com/rtx-video-sdk/getting-started
- FRUC is part of Optical Flow SDK. Local 5.0.7 exposes D3D11/CUDA resources, not direct D3D12: https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html
- Official RTX Video download redirects to NVIDIA login, verified in browser and web tool. User was asked to download it to Downloads: https://developer.nvidia.com/downloads/rtx/sdk/rtx_video_sdk_v1.1.0.zip

## Actual evidence
Local NvOFFRUC.dll has Valid NVIDIA signature and SHA256 5A0B6701D30709E25E7E5B92CA46B18AAB1459160CECD4F629872369D85C8B0A.

Own diagnostic source and logs: `logs/video-sdk-trial-20260909/`. Build: `cmd.exe /c logs\video-sdk-trial-20260909\build.cmd`. Each fruc-create.exe invocation used an external 25-second watchdog; all completed in approximately one second. D3D11 NV12 1920x1080, two input textures, one output, shared fence. CPU upload/readback is diagnostic only, prohibited in the production capture implementation.

- `create.stdout.log`: Create=0 and Destroy=0 on RTX 5070.
- `process-ms.stdout.log`: synthetic checker/moving rectangle returned Process=0 but repeated previous frames; diagnostic correctly failed with process exit 6.
- `process-normalized.stdout.log`: changing timestamps to sample-style unit intervals still repeated frames; no claim of an internal cause.
- `process-natural-final.stdout.log`: natural image translated 8 pixels per source frame; four source frames produced three distinct non-repeated intermediates. Create/Register/Process/Unregister/Destroy all returned 0, process exit 0.
- `natural-metrics.json`: first intermediate vs known 4-pixel midpoint has 8-bit luma MAE 0.000005787. Repeated previous frame MAE 1.51549; simple blend MAE 1.16068. Output-to-blend MAE 1.16068, demonstrating this output was not a simple blend. This is one controlled grayscale translation, not evidence for complex game motion, HUD, occlusion or particles.

User player PID 20272 remained running. No capture device was opened by the probe. Concurrent GPU load precludes a performance comparison. No 4K FRUC throughput or capture latency claim.

## Integration proposal
1. RTX Video SR and DLSS SR should be mutually exclusive options in the shared graph. Obtain official SDK and test D3D12 locally before integration. Do not stack both upscalers.
2. FRUC and DLSSG should be mutually exclusive options. First validate D3D12-to-D3D11/CUDA shared GPU resources, fence ownership and color handling. No production pixel readback.
3. Interpolation must wait for real B after A: source intervals are 55.6ms at 18fps and 16.7ms at 60fps. Separate lookahead from computation. 18-to-36fps is not 60fps; arbitrary-rate scheduling needs separate validation.
4. Preserve shared reset semantics and composite player UI/subtitles after interpolation. A capture source's baked-in game HUD cannot automatically be excluded this way.
5. Compare equal input, output and NR settings; measure SR/NR/flow/FG/Present separately, first with NR disabled then enabled. Screenshot GPU99% does not isolate SR cost. A different upscaler does not remove NR cost.
6. Keep export integrity checks and current defaults unchanged until comparisons pass.

Next: obtain official RTX Video SDK for D3D12 SR trial; measure FRUC 1080p/4K and complex motion with user player stopped, then decide integration. No phase advancement, distribution claim or completed-delivery claim.


## Cycle82 — user supplied RTX Video SDK; actual D3D12 trial

The earlier SDK-download blocker is resolved. User supplied `C:/Users/123/Downloads/RTX_Video_SDK_v1.1.0.zip`, 63,493,768 bytes, SHA256 ABF4F34E2B5A618E355B0D5A0365D8ECC3DB4396E756E4C850A867E1AE2ED69E. Extracted with path containment validation into ignored `third_party_local/nvidia/RTX_Video_SDK_1.1.0`. Packaged x64 release nvngx_vsr.dll has Valid signature and SHA256 C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED. This identifies the supplied file, not proof that NGX selected that exact binary instead of its driver-managed implementation; module snapshot did not identify a VSR DLL. SDK license and programming guide read locally. No distribution or product-default change.

Own independent diagnostic `logs/video-sdk-trial-20260909/vsr-trial.cpp` uses SDK headers/library and absolute feature search directory. Build command `cmd.exe /c logs\video-sdk-trial-20260909\build-vsr.cmd`; first link failed missing advapi32/user32, adding those dependencies fixed it. No SDK source copied into tracked product files. Preflight passed 71 checks (`preflight-cycle82.log`).

On RTX5070: Init/Capabilities/AvailableGet/Create/Evaluate/Release/DestroyParams/Shutdown all returned NGX success 0x1; Available=1. Native D3D12 RGBA8 input 1920x1080, output 3840x2160. First functional process exit0, then timestamp/debug process exit0. Every process externally bounded to25s, actual under7s. Each quality tested12 repeated evaluations of one static image; discard first4, summarize remaining8 GPU timestamps around Evaluate. Readback/upload excluded from GPU interval. This serialized diagnostic is not a production slot scheduler or end-to-end latency measurement.

| Quality | Median GPU ms | Observed min–max ms |
|---|---:|---:|
| 0, bicubic | 0.566 | 0.565–0.697 |
| 1, low | 1.374 | 1.373–1.542 |
| 2, medium | 1.774 | 1.771–1.776 |
| 3, high | 4.366 | 4.208–4.555 |
| 4, ultra | 5.857 | 5.769–6.167 |

Evidence: `vsr-timed.stdout.log`, `vsr-timing.json`, `vsr-output-metrics-final.json`. User player not running at checks before these trials; other GPU applications were not exhaustively audited. Do not compare against earlier concurrent or different-content DLSS timings. Eight static warmed samples do not prove sustained moving-video throughput.

Real output readbacks are nonblack. Quality1 differs from bicubic in13,656,811 RGB channels, MAE0.873805/255; quality4 differs in15,110,529 channels, MAE1.094184/255. Quality1 PNG visually inspected: recognizable reference image, no obvious black/garbled output. Difference is not proof of superior reconstruction: source was a resized user reference image, not native4K ground truth. No game/capture quality acceptance.

D3D12 debug error count0. Warnings were retained (buffer initial COPY_DEST state ignored/effectively COMMON); this is not a zero-message or GPU-based-validation claim. First cold host-completion figures46.3/5.4/60.2ms are not warmed GPU timing. Diagnostic has CPU upload/readback and waits only for measurement; never transplant those into capture.

Current assessment: video SR low/medium are viable candidates for integration, and official interface avoids caller-provided game motion/depth. No unconditional speed/quality promise; NR/FG cost remains. Next implement/review an optional shared-graph backend with proper encoded RGB boundary and bounded GPU scheduling, then equal-source capture A/B. FRUC GPU interop and complex-motion tests remain separate. Existing Phase7 timing failure remains; no gate advancement, no product build/change or delivery completion in this cycle.
