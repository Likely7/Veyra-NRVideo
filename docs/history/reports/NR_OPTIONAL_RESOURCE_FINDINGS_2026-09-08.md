# Local optional NR resource findings — 2026-09-08

Status: experimental contract mismatch, not enabled in any product entry point. The existing post-NR protection remains the production path. No SDK/runtime replacement, export integrity change, or normal-path pixel readback.

## What was tested

Fixed signed 310.8.0.0 Feature18, RTX5070. Shared EnhanceGraph, 256x256 RGB synthetic input, real parity encode/NR/parity decode. An unset-by-default diagnostic parameter callback lets this test bind external masks and retain raw proxy/neural texture pointers. The test allocates resources before graph views, uploads once, and explicitly transitions internal SRVs to COMMON for diagnostic readback and back afterwards. All optional-resource cases enable D3D12 debug layer and fail on error messages.

Primary behavioral references (no third-party code copied): [author findings](https://github.com/kibblerz/DLSS5-Reshade-AIO/blob/5112b4f5ed5ed0865cb56709ece751ce53c039f1/lab/PRIVATE-CONTRACT-FINDINGS.md), [author parameter types](https://github.com/kibblerz/DLSS5-Reshade-AIO/blob/5112b4f5ed5ed0865cb56709ece751ce53c039f1/lab/nr-lab.cpp). Local results take precedence over extrapolating that report.

Cases: 0 no optional resource; 1 ControlMask zero; 2 ControlMask one; 3 ControlMask center rectangle; 4 UIAlpha one + matching proxy Backbuffer, UICorrection0; 5 UIAlpha zero + Backbuffer, correction1; 6 UIAlpha one + Backbuffer, correction1; 7 UIAlpha rectangle + Backbuffer, correction1. R8 and RGBA8 masks use zero/255 (all channels for RGBA8). Optional subrects use signed I32 after checking the author's probe; U32 was also tested and did not explain the mismatch.

## Measured results, not a blanket PASS

R8: logs/optimization-goal-20260908/image-423214a367444f56b4d525981d273373,12.736s,exit1. RGBA8: image-7868ab72c2f84ec292b0d44cd26216bd,12.912s,exit1. Each mode created/evaluated successfully, one NR evaluation, debug errors0. NR baseline modifies191323source RGB channels. Formats produced the same reported differences.

| Case | Raw NR result | Final result |
|---|---|---|
| ControlMask zero | Exactly equals input proxy | Up to8/255 from original after parity reconstruction |
| ControlMask one | Up to57/255 from no-mask NR; only7/255 max from proxy | Up to57/255 from ordinary NR baseline |
| ControlMask rectangle | Zero-region exactly proxy; active region differs from baseline up to53/255 | Not a drop-in ordinary NR mask |
| UI correction disabled | Exactly no-resource NR baseline | Exactly baseline |
| UIAlpha zero, correction enabled | Up to1/255 from no-resource NR | Up to2/255 from baseline |
| UIAlpha one, correction enabled | Exactly proxy | Up to8/255 from original after parity reconstruction |
| UIAlpha rectangle, correction enabled | Protected region exactly proxy; outside up to1/255 from baseline | Protected region up to8/255 from original; outside up to2/255 from baseline |

The final source differences cannot be called failed raw masking: the raw neural pixels equal proxy in bypassed regions. Conversely raw masking does not establish exact preservation after parity. The rectangle and gating responses prove spatial resource effectiveness in these static cases, not that the model excludes HUD from all internal computation/history. The ControlMask normal-result assumption remains contradicted locally; its root cause is not established.

The optional probe is a separate explicit command and retains exit1 whenever the expected full contract fails. It is not folded into a falsely green product test or silently enabled in playback. Ordinary image/temporal protection tests are independent of this experimental claim.

## Failure trail and next decision

Initial image-8d0df46b3e5d4104be1a1db70c6408b1,28.075s: overly strong final-source identity assertion failed8; replaced with raw-domain measurement while retaining final error report. image-0a67c23f95cf40bc880ae45875973898,26.825s: diagnostic readback had two invalid COMMON-before barriers; corrected with explicit transitions, no product readback added. image-3e9167257aea4605af46e1cc5b4b12d4,29.701s: debug0, ControlMask-one mismatch57. Signed-I32 test image-16a430e9d2024238a8cbc0c1b0c77ccd,29.947s unchanged. Full R8 and RGBA8 matrices above retain failures. Stop repeating this hypothesis without new evidence.

Keep exact post-NR manual protection; do not expose these private resources as a quality improvement. The next quality work is Q6 motion confidence/out-of-frame/reprojection validation and FG reference policy. Natural footage, moving private masks, SR/FG HUD protection and all candidate algorithm ROI remain unproved. This is evidence for an integration decision, not full Goal completion.

Reproduce: build via scripts/build.ps1 -Root "$PWD" -Preset x64-release. Set FFmpeg DLL PATH as existing runner. Run veyra_image_dimension_tests.exe --nr-optional <unique-output-directory> or --nr-optional-rgba <unique-output-directory>, each with275secondwatchdog. No flag runs normal image plus temporal-protection regression tests.

## Cycles67–68 independent review and checkpoint

review_nr_temporal_optional final scoped PASS; no introduced P0/P1/P2. Preflight71, phase7 logs/delivery/2b2498a58135471e981779c989390d31/result.json41.739s/75 checks; normal image6508c7ccffcc45c6a46ca158e14fe0d7 exit0/26.855s. Optional R8 image-d5f8d7387035495f95d22bc04d705857 exit1/12.753s andRGBA image-d997436007bd4a3c9214adbd550f0dd8 exit1/12.526s independently reproduce expectation mismatch with debug0. Optional failure is retained; no private resource integration approved. Reviewer actual frame4 protected PNG viewed; callback product-default-disabled and resource lifetime/state transitions verified. Diff fingerprint9bdf08fe0712eb335c4e1f027b450b2a3de0cfd8 unchanged. AppB48EB58A70FCB1B25014AD27688B7A3DA90058DD2BD8A0ED4AC383E57E2D0F1C, imageEXEF574508156F15E51B1E9FB932BBA3F96589E7B8A4B3DCA85DE71D046A3B6F6B0.

Existing P2 diagnostic gap found: tests/integration/RepairShaderTests.cpp still uses8 residual constants, actual shader requires24. Current graph image matrix uses24 correctly, but that old standalone harness cannot prove the current contract. Next atomic task fixes and validates this harness, then Q6 adds bounds/photometric motion checks and measuresFG; Q7 and natural comparisons remain open. FullGoal/Phase7in_progress; no practical capture/long-term/distribution acceptance claimed.
