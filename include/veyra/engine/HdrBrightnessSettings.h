#pragma once
// Custom addition: per-scene HDR brightness management.
//
// A single static exposure cannot serve a film whose scenes are mastered at very
// different levels: lifting a dark scene blows out a bright one. This block
// adapts to the scene the frame belongs to, using the per-frame luma histogram
// and the scene-change signal the graph already computes for its cadence and
// anti-flicker work, so no new GPU pass and no readback are needed.
//
// It runs inside the input conversion, where the frame still exists as absolute
// nits, and only on the HDR-output path. Defaults keep the previous behaviour:
// `enabled=false` means the tone map is not applied at all.
namespace veyra::engine {
struct HdrBrightnessSettings {
    bool enabled=false;
    // 0..100: how much of the measured correction is applied (0 = measure only).
    unsigned strength=60;
    // The display's peak. The scene's highlights are compressed to it; when the
    // scene is darker than this nothing is compressed.
    unsigned targetPeakNits=1000;
    // 0..100: 0 updates the mapping only on a scene change (no breathing at all),
    // higher values follow the measurement faster within a scene.
    unsigned response=0;
    // 0..2000 ms: how long a scene change takes to reach the new mapping. The
    // ramp is time based, so it looks the same at 24 and 60 fps.
    unsigned transitionMs=1000;
    bool operator==(const HdrBrightnessSettings&) const = default;
    bool valid() const {
        return strength<=100 && targetPeakNits>=400 && targetPeakNits<=4000 && response<=100 && transitionMs<=2000;
    }
};
}
