#include "veyra/engine/ReplayBuffer.h"

#include <algorithm>
#include <climits>

namespace veyra::engine {
namespace {
constexpr int64_t kAudioRate=48000;
constexpr int64_t kAudioFrame=1024;
// Audio a clip takes before its first picture: decoder priming, cut by the
// MP4 edit list.
constexpr int64_t kAudioLead=2*kAudioFrame;
int64_t samplesOf(int64_t us){return us>=0?us*kAudioRate/1000000:-((-us)*kAudioRate/1000000);}
}

ReplayBuffer::ReplayBuffer(int64_t keepUs,size_t capacityBytes):keep_(std::max<int64_t>(keepUs,1000000)),capacity_(capacityBytes){}

void ReplayBuffer::setFormat(VideoFormat video,std::vector<AudioFormat> audio){
    std::lock_guard lock(m_);
    video_=std::move(video);
    frameUs_=1000000/std::max<int64_t>(1,video_.fps);
    audio_=std::move(audio);
    audioPackets_.assign(audio_.size(),{});
    audioEnd_.assign(audio_.size(),INT64_MIN);
}

void ReplayBuffer::setOrigin(int64_t originUs){std::lock_guard lock(m_);origin_=originUs;}

void ReplayBuffer::pushVideo(Packet packet){
    std::lock_guard lock(m_);
    if(end_>=0||!packet.data)return;
    // Until the first keyframe there is nothing a clip could start from.
    if(videoPackets_.empty()&&!packet.key)return;
    if(videoPackets_.empty())frontSequence_=nextSequence_;
    if(packet.key)keys_.push_back(nextSequence_);
    ++nextSequence_;
    bytes_+=packet.data->size();
    videoPackets_.push_back(std::move(packet));
    trim();
}

void ReplayBuffer::pushAudio(size_t track,Packet packet){
    std::lock_guard lock(m_);
    if(end_>=0||!packet.data||track>=audioPackets_.size()||videoPackets_.empty())return;
    audioEnd_[track]=std::max(audioEnd_[track],packet.pts+packet.duration);
    bytes_+=packet.data->size();
    audioPackets_[track].push_back(std::move(packet));
    // Audio can keep arriving while pictures are paused. Enforce the same
    // capacity on both arrival paths, including a single oversized GOP.
    trim();
}

void ReplayBuffer::finish(int64_t endUs){
    std::lock_guard lock(m_);
    if(end_<0)end_=std::max<int64_t>(endUs,videoPackets_.empty()?0:videoPackets_.back().pts+1);
}

bool ReplayBuffer::finished() const {std::lock_guard lock(m_);return end_>=0;}

void ReplayBuffer::trim(){
    // Drop the oldest GOP while the next one still starts early enough for a
    // full-length clip, or while the buffer is over its memory cap.
    const int64_t newest=videoPackets_.back().pts;
    while(keys_.size()>=2){
        const size_t second=keys_[1]-frontSequence_;
        const bool old=videoPackets_[second].pts<=newest-keep_;
        const bool over=bytes_>capacity_;
        if(!old&&!over)break;
        limited_=!old;
        for(size_t i=0;i<second;++i){bytes_-=videoPackets_.front().data->size();videoPackets_.pop_front();}
        frontSequence_+=second;
        keys_.pop_front();
    }
    const int64_t first=samplesOf(videoPackets_.front().pts)-kAudioLead;
    for(auto& queue:audioPackets_)
        while(!queue.empty()&&queue.front().pts+queue.front().duration<first){bytes_-=queue.front().data->size();queue.pop_front();}
    if(bytes_>capacity_){
        // A partial GOP is not independently decodable. If even the last GOP
        // cannot fit, discard it whole and wait for the next keyframe.
        videoPackets_.clear();keys_.clear();
        for(auto& queue:audioPackets_)queue.clear();
        std::fill(audioEnd_.begin(),audioEnd_.end(),INT64_MIN);
        frontSequence_=nextSequence_;
        bytes_=0;limited_=true;
    }
}

int64_t ReplayBuffer::videoEnd() const {
    if(videoPackets_.empty())return 0;
    return end_>=0?end_:videoPackets_.back().pts+frameUs_;
}

size_t ReplayBuffer::clipStart(int64_t endUs) const {
    size_t start=0;
    for(const size_t sequence:keys_){
        const size_t index=sequence-frontSequence_;
        if(videoPackets_[index].pts<=endUs-keep_)start=index;
        else break;
    }
    return start;
}

bool ReplayBuffer::covers(int64_t clockUs) const {
    std::lock_guard lock(m_);
    if(end_>=0)return true;
    if(videoPackets_.empty())return false;
    const int64_t at=clockUs-origin_;
    if(videoPackets_.back().pts+frameUs_/2<at)return false;
    for(const int64_t end:audioEnd_)if(end<samplesOf(at)-kAudioFrame)return false;
    return true;
}

ReplayBuffer::Clip ReplayBuffer::clip(int64_t clockUs) const {
    std::lock_guard lock(m_);
    Clip out;
    out.video=video_;out.audio=audio_;out.originUs=origin_;
    out.audioPackets.resize(audioPackets_.size());
    if(videoPackets_.empty())return out;
    const int64_t end=std::min(clockUs-origin_,videoEnd());
    if(end<=videoPackets_.front().pts)return out;
    const size_t start=clipStart(end);
    out.startUs=videoPackets_[start].pts;
    for(size_t i=start;i<videoPackets_.size()&&videoPackets_[i].pts<end;++i){
        out.videoPackets.push_back(videoPackets_[i]);out.bytes+=videoPackets_[i].data->size();
    }
    if(out.videoPackets.empty()){out.videoPackets.push_back(videoPackets_[start]);out.bytes+=videoPackets_[start].data->size();}
    // At least one frame long, as a recording's last frame is.
    out.endUs=std::max(end,out.videoPackets.back().pts+frameUs_);
    const int64_t first=samplesOf(out.startUs)-kAudioLead,last=samplesOf(out.endUs);
    for(size_t t=0;t<audioPackets_.size();++t)
        for(const auto& packet:audioPackets_[t])
            if(packet.pts+packet.duration>first&&packet.pts<last){out.audioPackets[t].push_back(packet);out.bytes+=packet.data->size();}
    return out;
}

ReplayBuffer::Coverage ReplayBuffer::coverage() const {
    std::lock_guard lock(m_);
    Coverage out;
    out.bytes=bytes_;out.limited=limited_;out.picture=!videoPackets_.empty();
    if(videoPackets_.empty())return out;
    const int64_t end=videoEnd();
    out.seconds=double(std::min<int64_t>(end-videoPackets_[clipStart(end)].pts,keep_))/1e6;
    return out;
}

} // namespace veyra::engine
