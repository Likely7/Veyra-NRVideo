# Capture persistent discontinuity repair

User log: `C:/Users/123/Desktop/veyra-app.log`, 2026-09-20.
RTX 5070 Ti Laptop, native YUY2 1920x1080 at 60.0002 fps.
Audio endpoint is Hagibis; the log does not establish the video card model.

All 92 rate-limited capture-discontinuity records report driverFlag=true,
clockBreak=false. The last counter is source=10560,count=10560. There are
10559 frame-flow records with resetReason=10. With FG enabled the graph
continually reports fgWarmup=1, fgReadyValid=0 and generatedPresented=0.
This establishes repeated input-history resets, not insufficient GPU compute.
The native sink does not manufacture the sample discontinuity flag.

Repair: filter a persistent driver flag only for native, independently
decodable frames. Require three consecutive flagged timestamp pairs, complete
positive sample times, PTS delta 0.5--1.5 nominal intervals and positive arrival
delta no larger than 2.5 intervals. Preserve the first/isolated flag. Restart
evidence gathering after any unflagged sample, invalid timing or cadence break.
Close clears the detector. Compressed inputs retain all driver flags.
Existing clock, mailbox, drop and graph reset policies remain in force.
Rate-limited diagnostics report suppressed flags. No extra buffering or waits.

Limit: a genuine discontinuity that has perfectly continuous timestamps and
arrival cadence cannot be distinguished from this already-stuck flag alone.
The filter is limited to raw frames to avoid hiding compressed reference loss.
This is not a guarantee that every capture device's timing metadata is correct.

Checkpoint: `checkpoint/pre-capture-sticky-discontinuity-20260920`.
Build and regression evidence: `E:/项目/Veyra/logs/capture-timeline-20260920/`.
LivePresentationTimingTests cover sustained flags (10000 samples), isolated
flags, backwards PTS, missing frames, arrival stalls, missing timestamps,
compressed input, reconnect and preservation of an unconsumed mailbox reset.
Initial callback deltas from the user log are included as regression data.
Application build and timing regressions pass; affected-device FG recovery
still needs user verification. No new release or portable package produced.
