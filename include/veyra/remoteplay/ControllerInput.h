#pragma once
#include "Types.h"
#include "TriggerFilter.h"
#include <deque>
#include <string>
#include <vector>
struct SDL_Gamepad;
struct SDL_AudioStream;
namespace veyra::remoteplay {
// SDL calls stay on the Win32 UI owner, independently of GPU processing.
class ControllerInput {
public:
    ~ControllerInput(){stop();}
    bool start(uint32_t preferredDevice=0);
    void stop(); // also turns follow mode off
    // 谁按键就用谁 (PS5 手柄控制): every pad is opened and the one whose button went
    // down last is the one read, so a streaming tool's virtual pad that appears
    // after a local pad still works. Off (default): the first pad found.
    void setFollowLastPressed(bool on);
    ControllerState poll(bool focused);
    void feedback(ControllerFeedback,bool focused);
    void releaseEffects();
    bool calibrate();
    bool connected()const{return gamepad_!=nullptr;}
    struct Capabilities {bool connected=false,gyro=false,accel=false,touch=false,triggers=false,haptics=false,calibrating=false;std::string name;};
    Capabilities capabilities()const;
private:
    void activate(SDL_Gamepad*);
    void deactivate();
    void applyLights();
    bool opened(uint32_t id)const;
    SDL_Gamepad* gamepad_=nullptr;
    std::vector<SDL_Gamepad*> others_; // follow mode: open, not read
    bool follow_=false;
    bool initialized_=false;
    HostTime enumerateAt_=0;uint32_t preferredDevice_=0;
    std::array<int8_t,2> touchIds_{-1,-1};uint8_t nextTouchId_=0;
    bool gyro_=false,accel_=false,effectsActive_=false,audioInitialized_=false,hapticAttempted_=false;
    SDL_AudioStream* hapticStream_=nullptr;
    // Console-set light bar and player indicator: kept while unfocused, applied to the read pad.
    std::array<uint8_t,3> ledColor_{};uint8_t player_=0;bool haveLed_=false,havePlayer_=false,lightsDirty_=false;
    std::array<ControllerState::Touch,2> touchState_{};
    std::deque<std::array<ControllerState::Touch,2>> touchQueue_;
    std::array<float,3> gyroBias_{},biasSum_{},sensorGyro_{},sensorAccel_{0,1,0};
    unsigned calibrationSamples_=0;bool calibrating_=false,haveGyro_=false,haveAccel_=false;
    HostTime calibrationStart_=0;uint64_t sensorStamp_=0;
    TriggerFilter triggerFilter_;


};
}
