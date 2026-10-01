#include "veyra/engine/ExportQueue.h"
#include <algorithm>
#include <filesystem>
#include <format>

namespace veyra::engine {
ExportQueue::ExportQueue(ExportJobManager& worker):worker_(worker){}
ExportQueue::~ExportQueue(){stopProbe_=true;if(probe_.valid())probe_.wait();}
ExportQueueItem* ExportQueue::mutableItem(uint64_t id){for(auto& item:items_)if(item.id==id)return &item;return nullptr;}
const ExportQueueItem* ExportQueue::find(uint64_t id)const{for(const auto& item:items_)if(item.id==id)return &item;return nullptr;}
bool ExportQueue::editable(const ExportQueueItem& item)const{return item.id!=active_&&item.state!=ExportItemState::Queued;}
uint64_t ExportQueue::add(const std::wstring& input,const std::wstring& output){
    ExportQueueItem item;item.id=++nextId_;item.input=input;item.output=output;item.explicitOutput=!output.empty();item.media.container=container_;
    item.media.audio.policy=ExportTrackPolicy::All;item.media.subtitles.policy=ExportTrackPolicy::All;
    items_.push_back(std::move(item));return nextId_;
}
bool ExportQueue::remove(uint64_t id){
    if(id==active_)return false;
    const auto size=items_.size();
    if(const auto* item=find(id);item&&running_&&item->batch==batch_)interrupted_=true;
    std::erase_if(items_,[&](const auto& item){return item.id==id;});return size!=items_.size();
}
void ExportQueue::clearWaiting(){
    if(running_)interrupted_=true;
    std::erase_if(items_,[&](const auto& item){return item.id!=active_&&(!busy()||item.state==ExportItemState::Ready||item.state==ExportItemState::Queued||item.state==ExportItemState::Cancelled);});
}
bool ExportQueue::move(uint64_t id,int destination){
    if(id==active_||destination<0||size_t(destination)>=items_.size())return false;
    const auto it=std::find_if(items_.begin(),items_.end(),[&](const auto& item){return item.id==id;});
    if(it==items_.end())return false;
    auto item=std::move(*it);items_.erase(it);items_.insert(items_.begin()+destination,std::move(item));return true;
}
bool ExportQueue::retry(uint64_t id){
    auto* item=mutableItem(id);if(!item||!editable(*item)||item->state!=ExportItemState::Failed)return false;
    item->state=ExportItemState::Ready;item->note.clear();item->progress=0;++item->revision;return true;
}
bool ExportQueue::setTrim(uint64_t id,double start,double end){
    auto* item=mutableItem(id);if(!item||!editable(*item)||!std::isfinite(start)||!std::isfinite(end)||start<0||end<0||(end>0&&end<=start))return false;
    if(item->start==start&&item->end==end)return true;
    item->start=start;item->end=end;++item->revision;return true;
}
bool ExportQueue::setTracks(uint64_t id,bool audio,ExportTrackSelection selection){
    auto* item=mutableItem(id);if(!item||!editable(*item)||!selection.valid())return false;
    (audio?item->media.audio:item->media.subtitles)=selection;++item->revision;return true;
}
void ExportQueue::setContainer(ExportContainer container){
    container_=container;
    for(auto& item:items_)if(editable(item)&&item.state!=ExportItemState::Done&&item.media.container!=container){item.media.container=container;if(!item.explicitOutput)item.output.clear();++item.revision;}
}
size_t ExportQueue::readyCount()const{
    return std::count_if(items_.begin(),items_.end(),[](const auto& i){return i.state==ExportItemState::Ready||i.state==ExportItemState::Cancelled;});
}
bool ExportQueue::needsTick()const{
    return busy()||probe_.valid()||std::any_of(items_.begin(),items_.end(),[](const auto& i){return i.inspected!=i.revision;});
}
bool ExportQueue::start(const std::wstring& directory,EnhancementSettings settings,bool hevc,sink::ExportRateControl rate,std::wstring& error){
    if(busy()){error=L"上一批任务仍在执行或回收";return false;}
    if(!readyCount()){error=L"没有待导出文件；失败项请先点击重试";return false;}
    std::error_code ec;
    if(!std::filesystem::is_directory(directory,ec)){error=L"请选择有效的输出目录";return false;}
    // Check directory writability before changing queue state.
    const auto check=reserveExportTemporaryFile((std::filesystem::path(directory)/L".veyra-write-check").wstring(),error);
    if(check.empty())return false;
    if(!removeExportTemporaryFile(check,error))return false;
    ++batch_;successes_=failures_=0;interrupted_=false;directory_=directory;
    for(auto& item:items_)if(!item.explicitOutput&&(item.state==ExportItemState::Ready||item.state==ExportItemState::Cancelled))item.output.clear();
    std::vector<std::filesystem::path> reserved;
    for(const auto& item:items_)if(!item.output.empty())reserved.emplace_back(item.output);
    for(auto& item:items_){
        if(item.state!=ExportItemState::Ready&&item.state!=ExportItemState::Cancelled)continue;
        item.batch=batch_;item.frozen=settings;item.hevc=hevc;item.rate=rate;
        item.state=ExportItemState::Queued;item.progress=0;item.note.clear();
        if(item.output.empty()){
            const auto stem=std::filesystem::path(item.input).stem().wstring()+L"_veyra";
            const auto ext=item.media.container==ExportContainer::Matroska?L".mkv":L".mp4";
            for(unsigned suffix=0;;++suffix){
                auto candidate=std::filesystem::path(directory)/(stem+(suffix?std::format(L" ({})",suffix):L"")+ext);
                const auto used=std::any_of(reserved.begin(),reserved.end(),[&](const auto& path){return _wcsicmp(path.c_str(),candidate.c_str())==0;});
                if(!used&&!std::filesystem::exists(candidate,ec)){item.output=candidate.wstring();reserved.push_back(candidate);break;}
            }
        }
        // Re-probe at Start: the source may have changed since it was added.
        ++item.revision;
    }
    running_=true;status_=L"正在预检本批文件和轨道";return true;
}
void ExportQueue::cancel(){
    running_=false;interrupted_=true;worker_.cancel();snapshot_=worker_.poll();
    for(auto& item:items_)if(item.state==ExportItemState::Queued)item.state=ExportItemState::Ready;
    status_=active_?L"正在取消并回收；等待项保留":L"已停止本批导出；等待项保留";
}
void ExportQueue::finishBatch(){
    running_=false;
    status_=std::format(L"本批完成：成功 {}，失败 {}",successes_,failures_);
    if(successes_&&!failures_&&!interrupted_)successEvent_=batch_;
}
bool ExportQueue::tick(){
    bool changed=false;
    if(active_){
        snapshot_=worker_.poll();auto* item=mutableItem(active_);
        if(item&&item->job==snapshot_.jobId){item->progress=snapshot_.progress;item->note=snapshot_.message;changed=true;
            if(!snapshot_.active()){
                if(snapshot_.state==ExportState::Succeeded){item->state=ExportItemState::Done;++successes_;}
                else if(snapshot_.state==ExportState::Cancelled)item->state=ExportItemState::Cancelled;
                else{item->state=ExportItemState::Failed;++failures_;}
                // Batch-wide storage errors stop further launches. The exact
                // reason and owned partial remain on this failed item.
                if(item->note.find(L"（-28）")!=std::wstring::npos||item->note.find(L"错误 112")!=std::wstring::npos){cancel();status_=L"输出磁盘空间不足，已停止本批调度";}
                active_=0;
            }
        }
    }
    if(probe_.valid()&&probe_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        auto result=probe_.get();auto* item=mutableItem(probingId_);
        if(item&&item->revision==probingRevision_){item->info=std::move(result);item->inspected=item->revision;item->note=item->info.error.empty()?item->info.skipped:item->info.error;changed=true;}
        probingId_=0;
    }
    // Prioritize the first waiting member of the frozen batch. UI reordering
    // changes this order directly; there is no second shadow deque.
    ExportQueueItem* next=nullptr;
    if(running_&&!active_){
        for(auto& item:items_)if(item.state==ExportItemState::Queued&&item.batch==batch_){next=&item;break;}
        if(!next){finishBatch();changed=true;}
        else if(next->inspected==next->revision){
            if(!next->info.error.empty()){
                next->state=ExportItemState::Failed;next->note=next->info.error;++failures_;changed=true;
            }else{
                std::error_code ec;const auto free=std::filesystem::space(directory_,ec);
                if(ec||free.available<1024*1024){cancel();status_=L"输出目录不可用或磁盘空间不足，已停止本批调度";return true;}
                const bool started=worker_.start(next->input,next->output,next->frozen,next->hevc,0,-1,next->start,next->end,next->rate,next->media);
                snapshot_=worker_.poll();next->job=snapshot_.jobId;next->note=snapshot_.message;
                if(started){active_=next->id;next->state=ExportItemState::Running;status_=L"正在按队列顺序导出";}
                else{next->state=ExportItemState::Failed;++failures_;}
                changed=true;
            }
        }
    }
    if(!probe_.valid()){
        if(!next||next->inspected==next->revision){next=nullptr;for(auto& item:items_)if(item.inspected!=item.revision){next=&item;break;}}
        if(next&&next->inspected!=next->revision){
            probingId_=next->id;probingRevision_=next->revision;
            const auto input=next->input;const auto options=next->media;const double start=next->start,end=next->end;
            probe_=std::async(std::launch::async,[this,input,options,start,end]{try{return ExportStreams::probe(input,options,start,end,stopProbe_);}catch(...){ExportMediaInfo result;result.error=L"读取媒体信息失败";return result;}});
        }
    }
    return changed;
}
}
