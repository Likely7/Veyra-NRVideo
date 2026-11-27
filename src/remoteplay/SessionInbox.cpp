// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/SessionInbox.h"
#include <limits>
#include <algorithm>
#include <atomic>
#include <vector>
namespace veyra::remoteplay {
namespace {
// A reconnect creates a new inbox whose generation can start again at one.
// A UI response must identify the native request across all inboxes.
std::atomic<uint64_t> nextLoginPinRequest=1;
}
struct SessionInbox::Shared {
    explicit Shared(QueueLimits limits):video(limits){}
    mutable std::mutex mutex;Generation generation=0;SessionState state=SessionState::Idle;
    HostTime origin=0;bool accepting=false;std::uint64_t stale=0;int error=0;
    VideoIngress video;AudioIngress audio;
    HostTime lastVideo=0,lastAudio=0,decodeStart=0,lastDecoded=0;
    uint64_t decodedFrames=0,receivedBytes=0,framesLost=0,referenceRecoveryEvents=0;
    bool hardwareDecode=false,decodeFallback=false,decodeConfirmed=false;
    VideoProfile requestedProfile;
    LoginPinPrompt loginPin;
    std::atomic<bool> pinQueued=false;
    std::array<char,4> pin{};
    void clearPin(){
        for(auto& digit:pin)static_cast<volatile char&>(digit)=0;
        pinQueued.store(false,std::memory_order_relaxed);
    }
    void resetLoginPin(){clearPin();loginPin={};}
    struct Sample {HostTime time;double value;};
    std::deque<Sample> received,decoded,decodeTimes,ingressWait;
    static void add(std::deque<Sample>& q,HostTime now,double value){
        while(!q.empty()&&(q.size()>=4096||q.front().time<=now-10000000))q.pop_front();q.push_back({now,value});
    }
    bool current(Generation g){if(!accepting||g!=generation){++stale;return false;}return true;}
};
SessionInbox::SessionInbox(QueueLimits l):shared_(std::make_shared<Shared>(l)){}
SessionInbox::~SessionInbox(){invalidate();}
SessionInbox::Token SessionInbox::begin(HostTime origin){
    if(origin<0)throw std::invalid_argument("Negative local clock origin");
    auto& s=*shared_;std::lock_guard lock(s.mutex);
    if(s.state!=SessionState::Idle)throw std::logic_error("Stop and join previous backend before a new session");
    if(s.generation==std::numeric_limits<Generation>::max())throw std::overflow_error("Session generation exhausted");
    ++s.generation;s.video.begin(s.generation);s.audio.begin(s.generation);s.origin=origin;s.state=SessionState::Connecting;s.accepting=true;s.error=0;
    s.lastVideo=s.lastAudio=s.decodeStart=s.lastDecoded=0;s.decodedFrames=s.receivedBytes=s.framesLost=s.referenceRecoveryEvents=0;
    s.hardwareDecode=s.decodeFallback=s.decodeConfirmed=false;
    s.received.clear();s.decoded.clear();s.decodeTimes.clear();s.ingressWait.clear();
    return Token{shared_,s.generation};
}
void SessionInbox::invalidate(){auto& s=*shared_;std::lock_guard l(s.mutex);s.accepting=false;s.resetLoginPin();if(s.state!=SessionState::Idle)s.state=SessionState::Stopping;s.video.close();s.audio.close();}
void SessionInbox::finishStop(){auto& s=*shared_;std::lock_guard l(s.mutex);if(s.accepting)throw std::logic_error("Invalidate before join/finishStop");s.state=SessionState::Idle;}
bool SessionInbox::isCurrent(Generation g)const{auto& s=*shared_;std::lock_guard l(s.mutex);return s.accepting&&s.generation==g;}
bool SessionInbox::decodedFrameReady(Generation g){
    auto& s=*shared_;std::lock_guard l(s.mutex);
    if(!s.accepting||s.generation!=g||(s.state!=SessionState::WaitingFirstFrame&&s.state!=SessionState::Streaming))return false;
    s.state=SessionState::Streaming;return true;
}
std::optional<QueuedVideo> SessionInbox::tryVideo(HostTime now){auto& s=*shared_;std::lock_guard l(s.mutex);if(!s.accepting)return {};return s.video.tryPop(now);}
std::optional<PcmBlock> SessionInbox::tryAudio(){auto& s=*shared_;std::lock_guard l(s.mutex);if(!s.accepting)return {};return s.audio.tryPop();}
bool SessionInbox::queueLoginPin(uint64_t requestId,std::string_view pin){
    if(!requestId||!validLoginPin(pin))return false;
    auto& s=*shared_;std::lock_guard l(s.mutex);
    if(!s.accepting||s.loginPin.requestId!=requestId||s.loginPin.pending)return false;
    std::copy(pin.begin(),pin.end(),s.pin.begin());
    s.loginPin.pending=true;s.loginPin.submitFailed=false;
    s.pinQueued.store(true,std::memory_order_relaxed);
    return true;
}
uint64_t SessionInbox::takeLoginPin(std::array<char,4>& pin){
    auto& s=*shared_;
    if(!s.pinQueued.load(std::memory_order_relaxed))return 0;
    std::lock_guard l(s.mutex);
    if(!s.pinQueued.load(std::memory_order_relaxed))return 0;
    pin=s.pin;s.clearPin();
    return s.loginPin.requestId;
}
void SessionInbox::loginPinSubmitFailed(uint64_t requestId){
    auto& s=*shared_;std::lock_guard l(s.mutex);
    if(!s.accepting||s.loginPin.requestId!=requestId)return;
    s.loginPin.pending=false;s.loginPin.submitFailed=true;
}
bool SessionInbox::takeIdrRequest(HostTime now){auto& s=*shared_;std::lock_guard l(s.mutex);return s.accepting&&s.video.takeIdrRequest(now);}
void SessionInbox::decodeFailed(Generation g){auto& s=*shared_;std::lock_guard l(s.mutex);if(s.accepting&&s.generation==g)s.video.requireKeyframe();}
SessionInbox::Snapshot SessionInbox::snapshot()const{
    auto& s=*shared_;std::lock_guard l(s.mutex);Snapshot result{s.generation,s.state,s.origin,s.stale,s.error,s.video.stats(),s.audio.stats()};
    const auto now=monotonic100ns();result.lastVideo100ns=s.lastVideo;result.lastAudio100ns=s.lastAudio;
    result.decodeStarted100ns=s.decodeStart;result.lastDecoded100ns=s.lastDecoded;
    result.decodedFrames=s.decodedFrames;result.receivedBytes=s.receivedBytes;result.framesLost=s.framesLost;result.referenceRecoveryEvents=s.referenceRecoveryEvents;
    result.hardwareDecode=s.hardwareDecode;result.decodeFallback=s.decodeFallback;result.decodeConfirmed=s.decodeConfirmed;
    result.requestedProfile=s.requestedProfile;
    result.loginPin=s.loginPin;
    result.ratesReady=now-s.origin>=10000000;
    for(const auto& v:s.received)if(v.time>now-10000000){++result.receivedFps;result.videoMbps+=v.value*8/1000000;}
    for(const auto& v:s.decoded)if(v.time>now-10000000)++result.decodedFps;
    auto aggregate=[&](const auto& q,std::optional<double>& mean,std::optional<double>* p95){
        std::vector<double> values;double sum=0;for(const auto& v:q)if(v.time>now-10000000){values.push_back(v.value);sum+=v.value;}
        if(values.empty())return;mean=sum/values.size();if(p95){std::sort(values.begin(),values.end());*p95=values[(values.size()*95+99)/100-1];}
    };
    aggregate(s.decodeTimes,result.decodeMeanMs,&result.decodeP95Ms);aggregate(s.ingressWait,result.ingressWaitMeanMs,nullptr);return result;
}
void SessionInbox::decodeStarted(HostTime now,HostTime arrival){auto& s=*shared_;std::lock_guard l(s.mutex);s.decodeStart=now;if(arrival>0&&now>=arrival)Shared::add(s.ingressWait,now,double(now-arrival)/10000);}
void SessionInbox::decodeFinished(HostTime now){auto& s=*shared_;std::lock_guard l(s.mutex);if(s.decodeStart>0&&now>=s.decodeStart)Shared::add(s.decodeTimes,now,double(now-s.decodeStart)/10000);s.decodeStart=0;}
void SessionInbox::frameDecoded(HostTime now){auto& s=*shared_;std::lock_guard l(s.mutex);s.lastDecoded=now;++s.decodedFrames;Shared::add(s.decoded,now,1);}
bool SessionInbox::Token::video(VideoSample sample)const noexcept{
    try{auto s=shared_.lock();if(!s)return false;std::lock_guard l(s->mutex);if(!s->current(generation_))return false;
        if(sample.generation!=generation_)return false;
        if(sample.kind==SampleKind::AccessUnit){s->lastVideo=monotonic100ns();s->receivedBytes+=sample.payload.bytes().size();s->framesLost+=std::max(0,sample.framesLost);s->referenceRecoveryEvents+=sample.referenceRecovered?1:0;Shared::add(s->received,s->lastVideo,double(sample.payload.bytes().size()));}
        const auto r=s->video.push(std::move(sample),monotonic100ns());return r==PushStatus::Accepted||r==PushStatus::ConfigStored;
    }catch(...){failed(-1001);return false;}
}
bool SessionInbox::Token::audio(PcmBlock block)const noexcept{
    try{auto s=shared_.lock();if(!s)return false;std::lock_guard l(s->mutex);if(!s->current(generation_)||block.generation!=generation_)return false;s->lastAudio=monotonic100ns();return s->audio.push(std::move(block));}
    catch(...){failed(-1002);return false;}
}
void SessionInbox::Token::connected()const noexcept{
    try{auto s=shared_.lock();if(!s)return;std::lock_guard l(s->mutex);if(!s->current(generation_))return;
        if(s->state==SessionState::Connecting||s->state==SessionState::LoginPinRequired){
            s->resetLoginPin();s->state=SessionState::WaitingFirstFrame;
        }
    }catch(...){}
}
void SessionInbox::Token::loginPinRequired(bool incorrect)const noexcept{
    try{auto s=shared_.lock();if(!s)return;std::lock_guard l(s->mutex);if(!s->current(generation_))return;
        if(s->state!=SessionState::Connecting&&s->state!=SessionState::LoginPinRequired)return;
        s->resetLoginPin();s->state=SessionState::LoginPinRequired;
        s->loginPin={nextLoginPinRequest.fetch_add(1,std::memory_order_relaxed),incorrect};
    }catch(...){}
}
void SessionInbox::Token::failed(int code)const noexcept{
    try{auto s=shared_.lock();if(!s)return;std::lock_guard l(s->mutex);if(!s->current(generation_))return;
        s->error=code;s->state=SessionState::Failed;s->accepting=false;s->resetLoginPin();s->video.close();s->audio.close();
    }catch(...){}
}
void SessionInbox::decoderBackend(bool hardware,bool fallback){
    auto& s=*shared_;std::lock_guard lock(s.mutex);s.hardwareDecode=hardware;s.decodeFallback=fallback;s.decodeConfirmed=true;
}
void SessionInbox::streamProfile(VideoProfile profile){auto& s=*shared_;std::lock_guard lock(s.mutex);s.requestedProfile=profile;}
} // namespace veyra::remoteplay
