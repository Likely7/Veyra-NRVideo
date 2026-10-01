#pragma once
#include "veyra/engine/EngineController.h"
#include <memory>
#include <vector>
namespace veyra::engine {
enum class ExportState { Idle, Preparing, Running, Paused, Finishing, Cancelling, Succeeded, Failed, Cancelled };
struct ExportJobSnapshot {
    ExportState state=ExportState::Idle;
    double progress=0;
    uint64_t jobId=0,frozenRevision=0,sourceFrames=0,generated=0,holds=0,encoded=0;
    EnhancementSettings frozen;
    std::wstring message,output,workerLog,temporaryPath;
    uint32_t workerPid=0;
    double etaSeconds=0;
    uint64_t queuePosition=0,queued=0;
    // Queue items that could not start (existing output, invalid range...) are
    // counted and the latest reason kept, then the queue moves on; they are
    // never skipped silently. Reset when a fresh export starts from idle.
    uint64_t queueFailures=0;
    std::wstring lastQueueFailure;
    bool active()const{return state>=ExportState::Preparing&&state<=ExportState::Cancelling;}
};
// One isolated NGX context in a child process. Anonymous inherited mapping is
// the entire IPC capability; no discoverable pipe, shell or config pathname.
class ExportJobManager {
public:
    ExportJobManager();~ExportJobManager();
    bool start(const std::wstring&,const std::wstring&,EnhancementSettings,bool hevc,unsigned maxFrames=0,int audioStreamIndex=-1,double trimStartSeconds=0.0,double trimEndSeconds=0.0,sink::ExportRateControl rateControl=sink::ExportRateControl::Cq,ExportMediaOptions media={});
    bool enqueue(const std::wstring&,const std::wstring&,EnhancementSettings,bool hevc,unsigned maxFrames=0,int audioStreamIndex=-1,double trimStartSeconds=0.0,double trimEndSeconds=0.0,sink::ExportRateControl rateControl=sink::ExportRateControl::Cq);
    void clearQueue();
    size_t queuedCount() const;
    void cancel();void pause(bool);void watching(bool);
    ExportJobSnapshot poll();
private:struct Impl;std::unique_ptr<Impl> p_;
};
int runExportWorker(HANDLE mapping);
}
