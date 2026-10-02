#pragma once
#include "veyra/engine/EngineController.h"
#include <functional>
#include <utility>
#include <vector>
namespace veyra::engine {
struct ExportCounts {uint64_t source=0,generated=0,holds=0,encoded=0;};
// Windows reset the GPU in the middle of a job (a driver hang, TDR). NGX cannot
// start again in the same process after that, so the job does not retry here:
// everything the encoder had returned is closed into a valid file, and the job
// reports where a fresh process can carry on (ExportJobManager does that and the
// last process joins the parts). Field report 2026-10-02: 8K NR exports on RTX 40
// cards lost the whole file to one hang.
struct ExportResume {
    bool resumable=false;
    double resumeAtSeconds=0;  // source time the next part starts at (first frame at/after it)
    double originSeconds=0;    // source time of this part's first frame (its time zero)
};
// Parts already written by earlier processes of the same job, in order: the file
// and the source time of its first frame. The finishing process joins them with
// its own part into the output, without re-encoding.
using ExportSegments=std::vector<std::pair<std::wstring,double>>;
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions,bool hevc,
                 std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,
                 unsigned maxFrames=0,const std::function<bool()>& frameBoundary={},const std::function<void(const ExportCounts&)>& counts={},
                 ExportResume* resume=nullptr,const ExportSegments& priorSegments={},unsigned resumeAttempt=0);
// Joins export parts into `output` by copying packets: part i's timestamps move by
// (its origin - the first part's origin). Every part must carry the same streams
// and the same video parameter sets, otherwise nothing is written.
bool joinExportSegments(const ExportSegments& parts,const std::wstring& output,bool matroska,std::wstring& error);
}
