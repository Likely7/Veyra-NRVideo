#pragma once
#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/ExportStreams.h"
#include <future>

namespace veyra::engine {
enum class ExportItemState { Ready, Queued, Running, Done, Failed, Cancelled };
struct ExportQueueItem {
    uint64_t id=0,batch=0,job=0,revision=1,inspected=0;
    std::wstring input,output,note;
    ExportItemState state=ExportItemState::Ready;
    double progress=0,start=0,end=0;
    ExportMediaOptions media;
    ExportMediaInfo info;
    EnhancementSettings frozen;
    bool hevc=false,explicitOutput=false;
    sink::ExportRateControl rate=sink::ExportRateControl::Cq;
};
// Sole order/state store for the QML queue. The existing manager remains the
// single-process executor; its legacy immediate-enqueue API is not used here.
class ExportQueue {
public:
    explicit ExportQueue(ExportJobManager& worker);
    ~ExportQueue();
    uint64_t add(const std::wstring& input,const std::wstring& output={});
    bool remove(uint64_t id);void clearWaiting();bool move(uint64_t id,int destination);
    bool retry(uint64_t id);
    bool setTrim(uint64_t id,double start,double end);
    bool setTracks(uint64_t id,bool audio,ExportTrackSelection selection);
    void setContainer(ExportContainer container);
    bool start(const std::wstring& directory,EnhancementSettings settings,bool hevc,sink::ExportRateControl rate,std::wstring& error);
    void cancel();bool tick();
    bool busy()const{return running_||snapshot_.active();}
    bool needsTick()const;
    size_t readyCount()const;
    const std::vector<ExportQueueItem>& items()const{return items_;}
    const ExportQueueItem* find(uint64_t id)const;
    const ExportJobSnapshot& snapshot()const{return snapshot_;}
    uint64_t successEvent()const{return successEvent_;}
    uint64_t batchId()const{return batch_;}
    unsigned successes()const{return successes_;}unsigned failures()const{return failures_;}
    const std::wstring& status()const{return status_;}
private:
    ExportQueueItem* mutableItem(uint64_t id);
    bool editable(const ExportQueueItem&)const;
    void finishBatch();
    ExportJobManager& worker_;
    std::vector<ExportQueueItem> items_;
    ExportJobSnapshot snapshot_;
    ExportContainer container_=ExportContainer::Mp4;
    uint64_t nextId_=0,batch_=0,active_=0,successEvent_=0;
    unsigned successes_=0,failures_=0;
    bool running_=false,interrupted_=false;
    std::wstring status_=L"添加文件后，点击开始导出",directory_;
    std::future<ExportMediaInfo> probe_;
    std::atomic<bool> stopProbe_{false};
    uint64_t probingId_=0,probingRevision_=0;
};
}
