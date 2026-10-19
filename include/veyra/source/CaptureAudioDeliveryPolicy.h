#pragma once

namespace veyra::source {
// One malformed/rejected delivery must not tear down a healthy source. A
// persistent format/PTS error must not be hidden forever either. This state
// belongs to the serialized vendor callback, never to a UI/render timer.
class CaptureAudioDeliveryPolicy {
public:
    static constexpr unsigned kRecoveryThreshold=30;
    bool discontinuity(bool source)const{return source||gap_;}
    bool delivered(bool accepted){
        if(accepted){rejected_=0;gap_=false;return true;}
        gap_=true;return ++rejected_<kRecoveryThreshold;
    }
    bool logRejection()const{return rejected_==1||rejected_%kRecoveryThreshold==0;}
    unsigned rejected()const{return rejected_;}
private:
    unsigned rejected_=0;
    bool gap_=false;
};
}
