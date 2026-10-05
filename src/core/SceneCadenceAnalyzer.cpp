#include "veyra/core/SceneCadenceAnalyzer.h"

#include <cmath>
#include <algorithm>

namespace veyra::core {

double SceneCadenceAnalyzer::histogramDistance(const std::vector<double>& a,
                                                const std::vector<double>& b) const {
    if (a.size() != b.size() || a.empty()) return 1.0; // max distance
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(a[i] - b[i]);
    }
    return sum * 0.5; // L1 / 2 to normalize to [0,1]
}

SceneAnalysisResult SceneCadenceAnalyzer::analyze(uint64_t frameId,
                                                    const std::vector<double>& histogram,
                                                    double sad,
                                                    uint64_t ptsUs,
                                                    uint64_t sourceStep) {
    SceneAnalysisResult result;
    result.frameId = frameId;
    result.sadScore = sad;

    if (!hasBaseline_) {
        // First frame: establish baseline, no cut possible.
        prevHistogram_ = histogram;
        prevPtsUs_ = ptsUs;
        hasBaseline_ = true;
        lastResult_ = result;
        return result;
    }

    // Compute distances.
    result.histogramDistance = histogramDistance(histogram, prevHistogram_);
    result.ptsDeltaUs = static_cast<double>(ptsUs > prevPtsUs_ ? ptsUs - prevPtsUs_ : 0);

    // Duplicate detection: near-zero SAD + tiny PTS delta.
    if (sad < config_.duplicateSadThreshold &&
        result.ptsDeltaUs < config_.duplicatePtsWindowUs) {
        result.isDuplicate = true;
        ++duplicateCount_;
        // Duplicates don't reset history but shouldn't generate new history either.
    }
    // Flash detection: high SAD but LOW histogram distance (similar content,
    // just brighter). This prevents false cuts from camera flashes.
    else if (sad > config_.flashSadThreshold &&
             result.histogramDistance < config_.flashHistogramThreshold) {
        result.isFlash = true;
        ++flashCount_;
        // Flash is NOT a cut  don't reset.
    }
    // Hard discontinuity: either large per-pixel change, or almost complete
    // histogram replacement with moderate change. The latter catches cuts with
    // similar brightness. A flash can also trigger this conservative reset;
    // without future frames this is not a semantic scene classification.
    else if ((sad > config_.sceneCutSadThreshold &&
              result.histogramDistance > config_.sceneCutHistogramThreshold) ||
             (sad > config_.replacementSadThreshold &&
              result.histogramDistance > config_.replacementHistogramThreshold)) {
        result.isSceneCut = true;
        ++sceneCutCount_;
    }

    // Cadence break: PTS irregularity (beyond expected jitter), measured per
    // source frame so that skipped/dropped frames do not count as a break.
    double cadenceDeltaUs = 0;
    if (result.ptsDeltaUs > 0 && result.ptsDeltaUs < 1e15) {
        cadenceDeltaUs = result.ptsDeltaUs / double(sourceStep ? sourceStep : 1);
        // Check against expected cadence (simplified: if delta is 3x the
        // previous delta, consider it a break)
        if (lastCadenceDeltaUs_ > 0 &&
            cadenceDeltaUs > lastCadenceDeltaUs_ * 3.0 + config_.cadenceBreakPtsJitterUs) {
            result.isCadenceBreak = true;
        }
    }

    // Update baseline (but not on duplicates  they carry no new info).
    if (!result.isDuplicate) {
        prevHistogram_ = histogram;
        prevPtsUs_ = ptsUs;
    }

    if (cadenceDeltaUs > 0 && !result.isDuplicate) lastCadenceDeltaUs_ = cadenceDeltaUs;
    lastResult_ = result;
    return result;
}

} // namespace veyra::core
