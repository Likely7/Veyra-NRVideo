# RTX Video SR optional integration — 2026-09-09

Current Phase7 remains in progress; this is a candidate for local user testing, not full delivery completion.

## Changes
Professional enhancement panel adds a persistent algorithm selector: DLSS SR and RTX Video SR qualities1–4. Default remains DLSS, SR switch remains off unless requested. Select video-low or medium and enable SR; unchanged source/output size bypasses SR. Existing3840x2160 sources therefore bypass upscaling.

Shared EnhanceGraph uses linear source -> encoded RGBA8 -> VSR -> linear work texture -> NR -> output -> existing DLSSG. Normal-path conversion stays on GPU with the existing command ring; no new pixel readback or per-pass CPU fence wait. Separate VSR parameter block shares the existing NGX core. Player, images and video export receive the selected backend; export integrity checks unchanged. Preset schema3 persists the quality and reads versions1/2. Runtime and SDK remain ignored.

Modified production files: include/src ngx VideoSrBackend; EnhanceGraph; EnhancementSettings; EngineController; VideoExportJob; PresetStore; SettingsWindow; AppShell CLI/tooltip; ScaleBlit; CMakeLists; THIRD_PARTY_NOTICES. RepairPresetTests now covers video quality roundtrip and invalidquality5.

## Actual checks
Build scripts/build.ps1 -Root <project> -Preset x64-release passed; final log logs/video-sdk-trial-20260909/build-cycle83-switchfix.log. Local app starts with --video-sr 1 (or2), --nr --no-fg --realtime and --smoke-seconds8; external25second timeout. Synthetic1920x1080/60 glass-video.mp4 is the test source, not natural-game acceptance.

- low+NR:211frames,59.56processedfps,P95lateness1.53ms,failed=false. app-vsr-low.log.
- medium+NR:358frames,59.76fps,P951.56ms,failed=false. app-medium-nr.log.
- low+NR+DLSSG2X:365real/364generated,59.80sourcefps,P951.41ms,failed=false. app-low-nr-fg.log. These are processing/submission figures, not physical scanout or latency measurements.
- Integrated SR GPU medians after sourceframe30 include both color conversions: low1.486ms/181samples; medium1.896ms/328samples; low withFG1.499ms/335samples. NR medians5.797/5.804/5.817ms. FG1 median2.498ms in the FG case. Short synthetic measurements only.
- First capture attempt0:0:0 failed with0frames because current device0 was OBSVirtualCamera. Fresh --list found only OBS, no physicalUSBcard. app-capture-low-nr.log and capture-list.txt. NOT a physical capture pass.
- First selector test correctly FAILED: requested/Applied revisions changed but backend did not. Reviewer identified missing rebuild condition. Fixed by rebuilding for every quality/backend change and rejecting mismatched applySettings; existing rollback retained.
- Retest ui-switch.py: while SR on, quality1->2->DLSS0->1, three Applied revisions plus actual corresponding Create logs; exit0/pass=true,18second smoke. app-switch-beforefix.log retained; app-switch.log final. GUI draft clearing found by reviewer fixed by preserving dirty and cancelling pending slider timer; independent verification pending.
- Preset roundtrip/invalid/legacy1 migration/corrupt preservation test passed. V2 parser remains supported; dedicated new V2 test not yet run.
- --export-out export-vsr-low.mp4 --max-frames12 --video-sr1 --nr --no-fg: exit0, native4K NR, real D3D12NVENC. ffprobe count_frames confirms H2643840x2160/12frames; decoded firstframe visually checked as expected nonblack checker. Input has no audio, so this does not prove audio retention. export-vsr-low.log/png.

All paths above under logs/video-sdk-trial-20260909. Actual VSR Create/Evaluate/Release op results0x1 andSEH0 in application logs. No DLSS/NR binary modified. All new code remains uncommitted candidate pending final review/gate. Existing intermittent Phase7 timing failure not erased by these narrow results.

Next: final independent scoped review then user reconnects physical card and tests video-low/medium with same source/settings. FRUC remains standalone diagnostic, not integrated in this change.

## Cycle83 independent final scoped PASS
Independent review_video_sr: no remaining introduced P0/P1/P2; original backend-switch P1 and draft-loss P2 closed. Independent low->medium->DLSS->low:3 applied transitions, VSR Create/Evaluate/Release3/401/3 all0x1/SEH0; DLSS Create1. Draft0.314159 retained across all3 changes, never secretly applied. Preset v2 protection/feather and defaultquality0 retained; v3quality2 roundtrip and invalid5 rejection pass.
Preflight71PASS; phase7 delivery a76caae2b03b4a96aaed4e4e1ab4eb71 45.295s/75PASS. Initial reviewer environment PSModulePath failure retained; fixed module search and reran. Evidence logs/video-sdk-trial-20260909/review-ui-switch.stdout.log, review-app-switch.log, review-preset.log. Tracked fingerprint98da728f82273048c095e9a12b2fe63c3114def9 unchanged during review; no tracked proprietary assets.
Final exeSHA37574BF6ACCA4E78A26EF10BE0D3AD0CDD90470D8A2353C6D551ACC798006A5B. User can choose Professional/Enhancement/RTX Video SR low or medium, enable SR with realtimeNR; test FGoff then2X. Physicalcard not enumerated this turn, onlyOBS; physical capture/naturalquality/longstability/unresolved historical intermittenttiming remain unaccepted. No overallGoal completion or distribution approval; no new integratedGBV claim.
