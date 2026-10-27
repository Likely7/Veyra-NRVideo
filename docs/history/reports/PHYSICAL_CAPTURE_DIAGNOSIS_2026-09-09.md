# Physical capture diagnosis — 2026-09-09

## Evidence and scope
Same running user process PID22300, same USB3 Video device0 nativeformat0 YUY2 1920x1080/60, kept open throughout. No product code or binaries changed; no competing capture/GPU process launched. Metadata enumeration confirmed USB3 Video. Existing app journal and same-session temporary settings comparisons, not external HDMI/photon measurement. Every comparison recorded13seconds; original selected quality4/nativeNR/FG2 restored in finally, final Appliedrevision22 confirms restoration. Scripts/logs under logs/video-sdk-trial-20260909.

## Main bottleneck
User snapshot revision3 withoutSR: NR1920x1080 median5.777ms. Revision5 enabled RTX Video SR quality4 and work3840x2160 while NRpolicy was Native: NR ALSO became3840x2160. Revision12 NRmedian22.911ms/P9524.261, SRmedian6.617/P957.114. Inputcallback60.01fps, application processedabout30fps, droppedabout30fps. More than16.7ms/sourceframe at60fps; even NRalone exceeds that. SR->NR->FG order is already correct.

This is a workload/configuration interaction, not evidence that the card cannot produce60fps. Capture latest-frame mailbox overwrites stale frames when processing lags. CaptureCardSource flags Drop; full history resets break valid FG pairs, explaining fewer effective generatedframes and uneven motion. Do not suppress these resets to inflate FG counts.

## Controlled comparisons (same source; FG2 unless stated)
After excluding first40 GPU samples for initialization, actual medians:
| Configuration | SR GPU ms | NR GPU ms | Observed source processing |
|---|---:|---:|---:|
| Original native4KNR + highest videoSR |6.617|22.911|about30fps|
| RealtimeNR + highest videoSR |6.497|6.100|51.16fps|
| RealtimeNR + low videoSR |1.505|6.366|59.51fps|

Stable last~5seconds of each13s comparison: high/realtime input60.04fps, processed51.16fps,45drops; low/realtime input59.90fps, processed59.51fps,2drops; low/realtime/FGoff input59.96fps, processed59.77fps,2drops,0generated. Prior FGon low had59.31generatedfps; submission/processing is not physicalscreen refresh. Highest/realtime has little budget left (flowabout.79ms, FGabout2.58ms plus remaining passes/scheduling), so highest alone remains less stable here.

Logs: capture-user-snapshot.log; physical-realtime-q4.log; physical-realtime-q1.log; physical-realtime-q1-nofg.log; physical-throughput.json; physical-ab-summary.json; physical-current-devices.txt. Originalsettings restoration logged in live app after17:46:46Z. NR remains Native and quality4 after testing, deliberately not silently replacing user's choices.

## Measurement problem
WorkspaceChrome.h explicitly displays GpuStage::Nr, labelled NR single stage. The familiar6/20ms aligns with NR stage; it is not entirepipeline/HDMI-to-display latency. EngineController TimingWindow retains1200 samples and is not cleared on settings transitions. Thus current capture P95 includes older high-cost configuration for roughly20seconds at60fps or40seconds at30fps. The13s FGoff trial's reported rolling63.858ms is contaminated by earlier configuration and cannot measure FGoff delay. Do NOT conclude disablingFG raised latency from that number. Current readAge .58ms also is not finaldisplay latency.

## Actions justified
For1080p60 to4K: start realtimeNR + RTXVideoSR low, then compare medium separately; FGoff for minimum additional waiting, FG2 if smoother motion is preferred. NativeNR remainsoptional and4Kexport unchanged. Avoid auto-switching away from explicitly chosenNative policy without uservisibility. Follow-up software fix should separate/clear timing windows on appliedsettings revision and display totalpipeline/stage costs clearly. This turn only diagnoses; no claimed latency code repair. Existing historical intermittent timing issue, sustained natural quality and true physicaldisplay latency remain unresolved. No currentphase advancement.

Correction: Cycle84 final revision22 multiplier1 means FG was NOT restored to original2X; only NativeNR/quality4 restored. Hiddenlegacycheckbox was stale. See docs/SETTINGS_PAGE_SCOPE_REPAIR_2026-09-09.md.
