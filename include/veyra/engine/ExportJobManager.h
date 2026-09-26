#pragma once
#include "veyra/engine/EngineController.h"
#include <memory>
namespace veyra::engine {
enum class ExportState { Idle, Preparing, Running, Paused, Finishing, Succeeded, Failed, Cancelled };
struct ExportJobSnapshot {
    ExportState state=ExportState::Idle;
    double progress=0;
    uint64_t jobId=0,frozenRevision=0,sourceFrames=0,generated=0,holds=0,encoded=0;
    EnhancementSettings frozen;
    std::wstring message,output,workerLog;
    uint32_t workerPid=0;
    bool active()const{return state>=ExportState::Preparing&&state<=ExportState::Finishing;}
};
// One isolated NGX context in a child process. Anonymous inherited mapping is
// the entire IPC capability; no discoverable pipe, shell or config pathname.
class ExportJobManager {
public:
    ExportJobManager();~ExportJobManager();
    bool start(const std::wstring&,const std::wstring&,EnhancementSettings,bool hevc,unsigned maxFrames=0,int audioStreamIndex=-1,double trimStartSeconds=0.0,double trimEndSeconds=0.0);
    void cancel();void pause(bool);void watching(bool);
    ExportJobSnapshot poll();
private:struct Impl;std::unique_ptr<Impl> p_;
};
int runExportWorker(HANDLE mapping);
}
