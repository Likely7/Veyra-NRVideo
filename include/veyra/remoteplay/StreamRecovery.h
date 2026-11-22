#pragma once
#include <cstdint>
namespace veyra::remoteplay {
// Local monotonic milliseconds. Three retries per outage; only sustained
// decoded progress replenishes them. Lifetime attempts remain monotonic so
// source epochs cannot collide when the retry allowance is renewed.
class StreamRecovery {
public:
    enum class Action {Wait,Keyframe,Reconnect,Fail};
    explicit StreamRecovery(int64_t startupTimeoutMs=30000):startupTimeoutMs_(startupTimeoutMs){}
    void beginAttempt(int64_t now){lastProgress_=now;healthySince_=-1;keyframes_=0;currentVideo_=false;}
    bool frame(int64_t now){
        if(!currentVideo_||healthySince_<0||now<lastProgress_||now-lastProgress_>=1000)healthySince_=now;
        lastProgress_=now;keyframes_=0;everVideo_=currentVideo_=true;
        if(episodeRetries_&&now-healthySince_>=30000){episodeRetries_=0;return true;}
        return false;
    }
    Action poll(int64_t now,bool loginPin,bool failed,bool retryAllowed=true,bool startupRetryAllowed=false){
        const auto silent=now-lastProgress_;
        if(loginPin)return silent>=120000?Action::Fail:Action::Wait;
        if(failed&&!retryAllowed)return Action::Fail;
        // Each rebuilt session needs its own handshake/first-frame allowance.
        // A previous session having video must not shorten a new handshake.
        if(failed||silent>=(currentVideo_?6000:startupTimeoutMs_)){
            if((!everVideo_&&!(failed&&startupRetryAllowed))||episodeRetries_>=3)return Action::Fail;
            ++episodeRetries_;++reconnects_;return Action::Reconnect;
        }
        if(currentVideo_&&keyframes_<2&&silent>=1000+int64_t(keyframes_)*2000){++keyframes_;return Action::Keyframe;}
        return Action::Wait;
    }
    unsigned reconnects()const{return reconnects_;}
    unsigned episodeRetries()const{return episodeRetries_;}
private:
    int64_t startupTimeoutMs_=30000;
    int64_t lastProgress_=0,healthySince_=-1;unsigned keyframes_=0,reconnects_=0,episodeRetries_=0;
    bool everVideo_=false,currentVideo_=false;
};
}
