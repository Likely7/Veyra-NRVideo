// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/VideoIngress.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace veyra::remoteplay {
void QueueLimits::validate() const {
    if(maxFrames<1||maxFrames>64||maxBytes<1024||maxBytes>64u*1024u*1024u ||
       maxConfigBytes<1||maxConfigBytes>maxBytes||maxConfigBytes>1024u*1024u ||
       maxAge100ns<10000||maxAge100ns>5000000)
        throw std::invalid_argument("Unsafe remote-play queue limits");
}
VideoIngress::VideoIngress(QueueLimits limits):limits_(limits){limits_.validate();}
void VideoIngress::begin(Generation g) {
    if(!g)throw std::invalid_argument("Generation zero is invalid");
    std::lock_guard lock(mutex_);queue_.clear();config_.reset();generation_=g;payloadBytes_=0;
    closed_=false;waiting_=true;pendingIdr_=false;lastIdr_=-1;sequence_.reset();stats_={};cv_.notify_all();
}
void VideoIngress::recoverLocked() {
    stats_.dropped+=queue_.size();queue_.clear();payloadBytes_=0;
    // Only one epoch increment per recovery episode; repeated P frames do not reset forever.
    if(!waiting_){++stats_.epoch;++stats_.recoveries;}
    waiting_=true;pendingIdr_=true;updateStatsLocked();
}
void VideoIngress::updateStatsLocked() {
    stats_.depth=queue_.size();stats_.bytes=payloadBytes_+(config_?config_->payload.size():0);
    stats_.waitingForIdr=waiting_;
    stats_.highWaterDepth=std::max(stats_.highWaterDepth,stats_.depth);
    stats_.highWaterBytes=std::max(stats_.highWaterBytes,stats_.bytes);
}
bool VideoIngress::expiredLocked(HostTime now) const {
    // `now` is the caller's clock sample from before it took the lock. A frame that was
    // stamped and queued in between carries a later stamp: it is new, not expired.
    return !queue_.empty() && now>queue_.front().sample.arrival100ns &&
        now-queue_.front().sample.arrival100ns>limits_.maxAge100ns;
}
PushStatus VideoIngress::push(VideoSample sample,HostTime now) {
    std::lock_guard lock(mutex_);
    if(closed_)return PushStatus::Closed;
    if(sample.generation!=generation_)return PushStatus::WrongGeneration;
    if(sample.payload.size()==0 || sample.arrival100ns<0 || now<sample.arrival100ns ||
       !sample.payload.paddingIsZero() || sample.framesLost<0 ||
       !((sample.width==1280&&sample.height==720)||(sample.width==1920&&sample.height==1080))){
        ++stats_.rejected;recoverLocked();return PushStatus::Malformed;
    }
    const auto nal=inspectAnnexB(sample.payload.bytes(),sample.codec);
    if(!nal.valid){++stats_.rejected;recoverLocked();return PushStatus::Malformed;}
    if(sample.kind==SampleKind::CodecConfig){
        if(!nal.hasConfig||nal.hasPicture||sample.wireFrameIndex||sample.payload.size()>limits_.maxConfigBytes){
            ++stats_.rejected;recoverLocked();return PushStatus::Malformed;
        }
        ++stats_.configMessages;
        const bool same=config_&&config_->codec==sample.codec&&config_->width==sample.width&&config_->height==sample.height&&
            std::ranges::equal(config_->payload.bytes(),sample.payload.bytes());
        if(!same){recoverLocked();config_=std::make_shared<const VideoSample>(std::move(sample));}
        updateStatsLocked();return PushStatus::ConfigStored;
    }
    if(sample.kind!=SampleKind::AccessUnit || !nal.hasPicture){++stats_.rejected;recoverLocked();return PushStatus::Malformed;}
    ++stats_.accessUnits;
    if(sample.wireFrameIndex){
        const auto seq=sequence_.observe(*sample.wireFrameIndex);
        if(!seq.accepted){++stats_.rejected;return PushStatus::Malformed;}
        if(seq.gap)recoverLocked();
    }
    if(sample.framesLost>0 || sample.referenceRecovered)recoverLocked();
    if(now-sample.arrival100ns>limits_.maxAge100ns){++stats_.dropped;recoverLocked();return PushStatus::WaitingForIdr;}
    if(!config_ || sample.codec!=config_->codec || sample.width!=config_->width || sample.height!=config_->height){
        ++stats_.dropped;recoverLocked();return PushStatus::WaitingForIdr;
    }
    const auto headerBytes=config_->payload.size();
    if(sample.payload.size()>limits_.maxBytes-headerBytes){++stats_.dropped;recoverLocked();return PushStatus::WaitingForIdr;}
    if(expiredLocked(now)||queue_.size()>=limits_.maxFrames||payloadBytes_>limits_.maxBytes-headerBytes-sample.payload.size())recoverLocked();
    if(waiting_&&!nal.idr){++stats_.dropped;pendingIdr_=true;return PushStatus::WaitingForIdr;}
    const bool reset=waiting_;
    queue_.push_back(QueuedVideo{std::move(sample),reset?config_:nullptr,stats_.epoch,reset});
    payloadBytes_+=queue_.back().sample.payload.size();waiting_=false;
    if(reset)pendingIdr_=false;
    ++stats_.accepted;updateStatsLocked();cv_.notify_one();return PushStatus::Accepted;
}
std::optional<QueuedVideo> VideoIngress::tryPop(HostTime now) {
    std::lock_guard lock(mutex_);
    if(closed_)return {};
    if(expiredLocked(now)){recoverLocked();return {};}
    if(queue_.empty())return {};
    auto out=std::move(queue_.front());queue_.pop_front();payloadBytes_-=out.sample.payload.size();
    ++stats_.consumed;updateStatsLocked();return out;
}
bool VideoIngress::waitForData(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);cv_.wait_for(lock,timeout,[&]{return closed_||!queue_.empty();});return !closed_&&!queue_.empty();
}
bool VideoIngress::takeIdrRequest(HostTime now) {
    std::lock_guard lock(mutex_);
    if(closed_||!pendingIdr_||now<0||(lastIdr_>=0&&(now<lastIdr_||now-lastIdr_<5000000)))return false;
    lastIdr_=now;pendingIdr_=false;++stats_.idrRequests;return true;
}
void VideoIngress::requireKeyframe(){std::lock_guard lock(mutex_);if(!closed_)recoverLocked();}
void VideoIngress::close(){std::lock_guard lock(mutex_);closed_=true;queue_.clear();config_.reset();payloadBytes_=0;pendingIdr_=false;updateStatsLocked();cv_.notify_all();}
VideoQueueStats VideoIngress::stats()const{std::lock_guard lock(mutex_);return stats_;}

AudioIngress::AudioIngress(std::uint32_t maxMs):maxMs_(maxMs){if(maxMs<10||maxMs>200)throw std::invalid_argument("Audio buffer must be 10..200 ms");}
void AudioIngress::begin(Generation g){
    if(!g)throw std::invalid_argument("Generation zero is invalid");
    std::lock_guard lock(mutex_);generation_=g;rate_=channels_=0;frames_=0;nextSample_=0;closed_=false;gap_=true;queue_.clear();stats_={};
}
bool AudioIngress::push(PcmBlock block){
    std::lock_guard lock(mutex_);
    if(closed_||block.generation!=generation_)return false;
    if(!block.valid()||block.arrival100ns<0||block.firstSample>std::numeric_limits<std::uint64_t>::max()-block.frames()){
        ++stats_.rejected;return false;
    }
    const auto budget=static_cast<std::size_t>(block.rate)*maxMs_/1000u;
    if(block.frames()>budget){++stats_.rejected;gap_=true;return false;}
    if(rate_!=block.rate||channels_!=block.channels){stats_.dropped+=queue_.size();queue_.clear();frames_=0;rate_=block.rate;channels_=block.channels;nextSample_=block.firstSample;gap_=true;}
    if(block.firstSample<nextSample_){++stats_.rejected;return false;}
    if(block.firstSample!=nextSample_)gap_=true;
    while(!queue_.empty()&&frames_+block.frames()>budget){frames_-=queue_.front().frames();queue_.pop_front();++stats_.dropped;gap_=true;}
    if(gap_&&!queue_.empty())queue_.front().discontinuity=true;
    block.discontinuity|=gap_;gap_=false;nextSample_=block.firstSample+block.frames();frames_+=block.frames();
    queue_.push_back(std::move(block));++stats_.accepted;stats_.bufferedMs=1000.0*static_cast<double>(frames_)/rate_;return true;
}
std::optional<PcmBlock> AudioIngress::tryPop(){
    std::lock_guard lock(mutex_);if(closed_||queue_.empty())return {};
    auto out=std::move(queue_.front());queue_.pop_front();frames_-=out.frames();++stats_.consumed;
    stats_.bufferedMs=rate_?1000.0*static_cast<double>(frames_)/rate_:0;return out;
}
void AudioIngress::close(){std::lock_guard lock(mutex_);closed_=true;queue_.clear();frames_=0;stats_.bufferedMs=0;}
AudioQueueStats AudioIngress::stats()const{std::lock_guard lock(mutex_);return stats_;}
} // namespace veyra::remoteplay
