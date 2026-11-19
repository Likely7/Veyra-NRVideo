#include "veyra/remoteplay/ControllerInput.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_audio.h>
#include <cstring>
#include "veyra/Log.h"
#include "veyra/remoteplay/DualSenseHapticsReport.h"
#include "veyra/remoteplay/HapticsBlockPacer.h"
#include <SDL3/SDL_hidapi.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace veyra::remoteplay {
// ---------------------------------------------------------------------------------------
// DualSense haptics over Bluetooth.
//
// Over USB the pad exposes a four-channel audio endpoint and the haptic audio is played into
// channels 3/4 (feedback() below). Over Bluetooth there is no audio endpoint; the same audio
// is sent as HID output report 0x32 (DualSenseHapticsReport.h).
//
// SDL keeps the pad open through its own HIDAPI driver; Windows lets a second handle be
// opened on the same device, and that handle is only written to (SDL never sends report
// 0x32). The writes are paced by a dedicated thread with a high-resolution timer: Windows
// takes a report long before the pad plays it (in about 0.1 ms in the measurement of AnyPS5
// issue 2056), so the write call gives no back-pressure to pace by, and the UI timer that
// delivers the audio is far too uneven for a 10.667 ms cadence (HapticsBlockPacer.h keeps
// the reserve between the two).
//
// SDL's hid_write waits up to a second for a write and leaves it in flight when that time
// runs out. After a failed write the handle is therefore not written to again: the route
// closes and is opened anew when the pad is activated again. Closing joins the sender, so a
// write that is stalled at that moment holds the caller for up to that second.
// ---------------------------------------------------------------------------------------
class DualSenseBtHaptics {
public:
    // waits: reports that had to wait for audio, counted once per wait.
    struct Totals {uint64_t reports=0,writeErrors=0,waits=0,framesDropped=0;};
    // nullptr unless `pad` is a DualSense connected over Bluetooth; `why` says what happened.
    static DualSenseBtHaptics* open(SDL_Gamepad* pad,std::string& why){
        if(GetEnvironmentVariableW(L"VEYRA_DISABLE_BT_HAPTICS",nullptr,0)>0){why="switched off by VEYRA_DISABLE_BT_HAPTICS";return nullptr;}
        const char* path=SDL_GetGamepadPath(pad);
        if(!path){why="the pad has no device path";return nullptr;}
        // SDL's HID calls do not set an error text on every failure: start from none.
        SDL_ClearError();
        const auto failure=[](const char* what){const char* text=SDL_GetError();return text&&*text?std::string(what)+": "+text:std::string(what);};
        if(SDL_hid_init()!=0){why=failure("SDL_hid_init failed");return nullptr;}
        auto* device=SDL_hid_open_path(path);
        if(!device){why=failure("a second HID handle could not be opened");SDL_hid_exit();return nullptr;}
        const auto* info=SDL_hid_get_device_info(device);
        // 054C:0CE6 only: the report has not been tried on the DualSense Edge (0DF2).
        const bool dualSense=info&&info->vendor_id==0x054c&&info->product_id==0x0ce6;
        if(!dualSense||info->bus_type!=SDL_HID_API_BUS_BLUETOOTH){
            why=!info?"no device info":!dualSense?"not a DualSense (054C:0CE6)":"not connected over Bluetooth";
            SDL_hid_close(device);SDL_hid_exit();return nullptr;
        }
        return new DualSenseBtHaptics(device);
    }
    ~DualSenseBtHaptics(){
        // The sender ends before the handle is closed. Windows cancels the I/O a thread has
        // started when that thread exits, so no write is in flight at the close.
        thread_.request_stop();if(thread_.joinable())thread_.join();
        SDL_hid_close(device_);SDL_hid_exit();
    }
    // Interleaved L,R at 3 kHz. Any thread.
    void push(const int16_t* stereo,size_t frames){
        if(!frames||failed_)return;
        // The console may send haptic audio that is pure silence. While nothing is playing,
        // silence is not queued: the reports start with the first audible block and stop
        // 250 ms after the last one, so an idle pad gets no report traffic at all.
        bool loud=false;
        for(size_t i=0;i<frames*2&&!loud;++i)loud=dualSenseAudible(stereo[i]);
        std::lock_guard lock(mutex_);
        if(!loud&&!streaming_&&fifo_.empty())return;
        lastPush_=Clock::now();
        // Audible audio that is queued keeps the stream alive like audio that was sent: the
        // start of a new effect waits in the queue for its reserve and must not be cleared
        // when the 250 ms after the previous effect run out.
        if(loud)lastLoudPush_=lastPush_;
        if(fifo_.empty()){fifoSince_=lastPush_;wake_.notify_one();}
        fifo_.insert(fifo_.end(),stereo,stereo+frames*2);
        // More than 128 ms queued means the sender fell far behind: keep the newest part.
        if(fifo_.size()>kMaxFrames*2){const auto drop=fifo_.size()-kMaxFrames*2;fifo_.erase(fifo_.begin(),fifo_.begin()+ptrdiff_t(drop));framesDropped_+=drop/2;}
    }
    void clear(){std::lock_guard lock(mutex_);fifo_.clear();}
    bool failed()const{return failed_;}
    Totals totals()const{return {reports_,writeErrors_,waits_,framesDropped_};}
private:
    using Clock=std::chrono::steady_clock;
    static constexpr size_t kBlock=kDualSenseHapticsFrames,kMaxFrames=384;
    explicit DualSenseBtHaptics(SDL_hid_device* device):device_(device){
        thread_=std::jthread([this](std::stop_token stop){run(stop);});
    }
    // Returns false when the write failed.
    bool send(const int16_t* frames,bool& loud){
        std::array<uint8_t,kDualSenseHapticsReportSize> out;
        loud=buildDualSenseHapticsReport(out,frames,sequence_++,counter_++);
        ++reports_;
        if(SDL_hid_write(device_,out.data(),out.size())>=0)return true;
        ++writeErrors_;return false;
    }
    void run(std::stop_token stop){
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL); // an even cadence matters more than throughput here
        const HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        const auto waitUntil=[&](Clock::time_point until){
            const auto now=Clock::now();if(until<=now)return;
            LARGE_INTEGER due;due.QuadPart=-std::chrono::duration_cast<std::chrono::nanoseconds>(until-now).count()/100;
            if(timer&&due.QuadPart<0&&SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)){WaitForSingleObject(timer,100);return;}
            std::this_thread::sleep_for(until-now);
        };
        constexpr auto period=std::chrono::nanoseconds(10'666'667); // 32 frames at 3 kHz
        // When a stream starts and how many source frames go into each block: see
        // HapticsBlockPacer.h (it keeps a reserve of queued audio and rebuilds it).
        HapticsBlockPacer pacer;
        bool streaming=false,wasStarved=false;auto next=Clock::now();Clock::time_point lastLoud{};
        const auto stopStream=[&]{streaming=false;pacer.reset();std::lock_guard lock(mutex_);streaming_=false;fifo_.clear();};
        while(!stop.stop_requested()){
            std::array<int16_t,(kBlock+1)*2> source{};std::array<int16_t,kBlock*2> frames{};
            HapticsBlockPacer::Step step;auto now=Clock::now();
            {std::lock_guard lock(mutex_);
                const auto queued=unsigned(fifo_.size()/2);
                const auto since=[&](Clock::time_point from){return unsigned(std::clamp<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now-from).count(),0,100000));};
                step=pacer.next(queued,since(lastPush_),since(fifoSince_));
                step.take=std::min({step.take,queued,unsigned(kBlock+1)}); // `source` holds 33 frames
                if(step.send){
                    std::copy_n(fifo_.begin(),size_t(step.take)*2,source.begin());
                    fifo_.erase(fifo_.begin(),fifo_.begin()+ptrdiff_t(step.take)*2);
                    streaming_=true;
                }
                if(lastLoudPush_>lastLoud)lastLoud=lastLoudPush_;
            }
            if(!streaming){
                if(!step.send){
                    // Nothing queued: sleep until audio arrives. Something queued but its
                    // reserve is not there yet: look again shortly.
                    std::unique_lock lock(mutex_);
                    if(fifo_.empty())wake_.wait_for(lock,stop,std::chrono::milliseconds(250),[&]{return !fifo_.empty();});
                    else{lock.unlock();waitUntil(now+std::chrono::milliseconds(4));}
                    continue;
                }
                streaming=true;wasStarved=false;next=now;if(lastLoud<now)lastLoud=now;counter_=0;
            }
            if(step.starved){
                // The block is not full although audio still arrives: the report waits.
                // `next` is left alone, so the following reports go out early until the
                // schedule is caught up; the pacer then rebuilds the reserve.
                if(!wasStarved){wasStarved=true;++waits_;} // counted once per wait
                if(now-next>std::chrono::milliseconds(60)){next=now;wasStarved=false;} // far beyond the pad's buffer: start over
                waitUntil(now+std::chrono::milliseconds(2));continue;
            }
            wasStarved=false;
            // No audio for this report (between effects, or a new effect still gathering its
            // reserve): the pad gets silence, so the cadence of the stream is kept.
            if(step.send)hapticsFitBlock(source.data(),step.take,frames.data());
            bool loud=false;
            // A failed write ends the route (see above); feedback() closes it.
            if(!send(frames.data(),loud)){failed_=true;stopStream();continue;}
            if(loud)lastLoud=now;
            // The pad keeps getting silence for 250 ms after the last audible audio, then
            // the link rests (whatever is still queued by then is silence).
            if(now-lastLoud>std::chrono::milliseconds(250)){
                std::unique_lock lock(mutex_);
                // Audible audio that arrived while this report was written keeps the stream.
                if(lastLoudPush_>lastLoud)lastLoud=lastLoudPush_;
                else{streaming=false;pacer.reset();streaming_=false;fifo_.clear();continue;}
            }
            next+=period;now=Clock::now();
            if(now-next>period*3)next=now; // stalled: resynchronise rather than burst
            waitUntil(next);
        }
        if(timer)CloseHandle(timer);
    }
    SDL_hid_device* device_;
    mutable std::mutex mutex_;std::condition_variable_any wake_;std::vector<int16_t> fifo_;Clock::time_point fifoSince_{},lastPush_{},lastLoudPush_{};
    std::atomic<bool> failed_{false};
    bool streaming_=false; // under mutex_: the sender thread is sending reports
    std::atomic<uint64_t> reports_{0},writeErrors_{0},waits_{0},framesDropped_{0};
    uint8_t sequence_=0,counter_=0;
    std::jthread thread_;
};
bool ControllerInput::start(uint32_t preferredDevice){
    preferredDevice_=preferredDevice;
    if(initialized_)return true;
    // No SDL window is created. Our explicit foreground gate is authoritative.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    initialized_=SDL_InitSubSystem(SDL_INIT_GAMEPAD);return initialized_;
}
void ControllerInput::stop(){
    for(auto* pad:others_)SDL_CloseGamepad(pad);
    others_.clear();follow_=false;
    releaseEffects();closeHaptics();
    if(audioInitialized_){SDL_QuitSubSystem(SDL_INIT_AUDIO);audioInitialized_=false;}
    touchQueue_.clear();touchState_={};haveGyro_=haveAccel_=false;calibrating_=false;gyroBias_={};enumerateAt_=0;
    if(gamepad_){SDL_CloseGamepad(gamepad_);gamepad_=nullptr;}
    if(initialized_){SDL_QuitSubSystem(SDL_INIT_GAMEPAD);initialized_=false;}
}
void ControllerInput::setFollowLastPressed(bool on){
    if(!on){for(auto* pad:others_)SDL_CloseGamepad(pad);others_.clear();}
    if(on!=follow_)enumerateAt_=0;
    follow_=on;
}
bool ControllerInput::opened(uint32_t id)const{
    if(gamepad_&&SDL_GetGamepadID(gamepad_)==id)return true;
    return std::any_of(others_.begin(),others_.end(),[id](SDL_Gamepad* pad){return SDL_GetGamepadID(pad)==id;});
}
void ControllerInput::activate(SDL_Gamepad* pad){
    gamepad_=pad;
    touchQueue_.clear();touchState_={};sensorStamp_=0;haveGyro_=haveAccel_=false;gyroBias_={};calibrating_=false;touchIds_={-1,-1};gyro_=SDL_GamepadHasSensor(gamepad_,SDL_SENSOR_GYRO)&&SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_GYRO,true);
    accel_=SDL_GamepadHasSensor(gamepad_,SDL_SENSOR_ACCEL)&&SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_ACCEL,true);
    std::string line=std::string("device opened gyro=")+(gyro_?"1":"0")+" accel="+(accel_?"1":"0")+" touchpads="+std::to_string(SDL_GetNumGamepadTouchpads(gamepad_));
    if(follow_){const char* name=SDL_GetGamepadName(gamepad_);line+=std::string(" active (follow last pressed, ")+std::to_string(others_.size()+1)+" open) name="+(name?name:"?");}
    veyra::log::info("remoteplay-input",line);
}
// The pad stops being read: its rumble/trigger effects and haptic stream end.
void ControllerInput::deactivate(){
    releaseEffects();closeHaptics();
    if(gamepad_&&SDL_GamepadConnected(gamepad_)){
        if(gyro_)SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_GYRO,false);
        if(accel_)SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_ACCEL,false);
    }
}
ControllerState ControllerInput::poll(bool focused){
    ControllerState state;if(!initialized_)return state;
    SDL_PumpEvents();SDL_UpdateGamepads();
    // Only this module initializes SDL; drain its input events rather than
    // accumulating an unconsumed second application event queue.

    if(gamepad_&&!SDL_GamepadConnected(gamepad_)){deactivate();SDL_CloseGamepad(gamepad_);gamepad_=nullptr;enumerateAt_=0;}
    if(follow_){
        std::erase_if(others_,[](SDL_Gamepad* pad){if(SDL_GamepadConnected(pad))return false;SDL_CloseGamepad(pad);return true;});
        if(!gamepad_&&!others_.empty()){auto* next=others_.front();others_.erase(others_.begin());activate(next);}
    }
    const auto now=monotonic100ns();
    // Follow mode keeps enumerating while a pad is open: a virtual pad created
    // by a streaming tool joins after the local one.
    if((!gamepad_||follow_)&&now>=enumerateAt_){
        int count=0;auto* ids=SDL_GetGamepads(&count);
        for(int i=0;i<count&&(follow_||!gamepad_);++i){
            if((preferredDevice_&&ids[i]!=preferredDevice_)||opened(ids[i]))continue;
            auto* pad=SDL_OpenGamepad(ids[i]);if(!pad)continue;
            if(!gamepad_)activate(pad);else others_.push_back(pad);
        }
        SDL_free(ids);enumerateAt_=now+10000000;
    }
    if(calibrating_&&focused&&gamepad_){
        if(!calibrationStart_)calibrationStart_=now;
        else if(now-calibrationStart_>100000000){calibrating_=false;veyra::log::warn("remoteplay-input","Calibration timed out; prior bias retained");}
    }
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(follow_&&focused&&gamepad_&&event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&event.gbutton.which!=SDL_GetGamepadID(gamepad_)){
            const auto it=std::find_if(others_.begin(),others_.end(),[&](SDL_Gamepad* pad){return SDL_GetGamepadID(pad)==event.gbutton.which;});
            if(it!=others_.end()){auto* next=*it;others_.erase(it);deactivate();others_.push_back(gamepad_);activate(next);}
        }
        if(!focused||!gamepad_)continue;
        if((event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN||event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_UP||event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION)&&event.gtouchpad.which==SDL_GetGamepadID(gamepad_)){
            const auto& touch=event.gtouchpad;
            if(touch.touchpad!=0||touch.finger<0||touch.finger>=2||!std::isfinite(touch.x)||!std::isfinite(touch.y))continue;
            auto& value=touchState_[touch.finger];
            if(event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_UP)value={};
            else{if(value.id<0)value.id=int8_t(nextTouchId_++&0x7f);value.x=uint16_t(std::clamp(touch.x,0.f,1.f)*1919);value.y=uint16_t(std::clamp(touch.y,0.f,1.f)*1079);}
            if(!touchQueue_.empty()&&touchQueue_.back()[0].id==touchState_[0].id&&touchQueue_.back()[1].id==touchState_[1].id)touchQueue_.back()=touchState_;
            else if(touchQueue_.size()<16)touchQueue_.push_back(touchState_);
            else{touchQueue_.clear();touchState_={};touchQueue_.push_back(touchState_);veyra::log::warn("remoteplay-input","Touch edge queue overflow; releasing touches");}
        }
        if(event.type==SDL_EVENT_GAMEPAD_SENSOR_UPDATE&&event.gsensor.which==SDL_GetGamepadID(gamepad_)){
            const auto& sample=event.gsensor;if(!std::all_of(std::begin(sample.data),std::end(sample.data),[](float v){return std::isfinite(v);}))continue;
            if(sample.sensor==SDL_SENSOR_ACCEL){for(int i=0;i<3;++i)sensorAccel_[i]=sample.data[i]/SDL_STANDARD_GRAVITY;haveAccel_=true;}
            else if(sample.sensor==SDL_SENSOR_GYRO){std::copy_n(sample.data,3,sensorGyro_.begin());haveGyro_=true;}
            else continue;
            sensorStamp_=sample.timestamp/1000;
            if(haveAccel_&&haveGyro_){
                if(calibrating_&&sample.sensor==SDL_SENSOR_GYRO){
                    float g2=0,a2=0;for(int i=0;i<3;++i){g2+=sensorGyro_[i]*sensorGyro_[i];a2+=sensorAccel_[i]*sensorAccel_[i];}
                    if(g2<.01f&&std::abs(a2-1)<.1f){for(int i=0;i<3;++i)biasSum_[i]+=sensorGyro_[i];++calibrationSamples_;}
                    else{calibrationSamples_=0;biasSum_={};}
                    if(calibrationSamples_>=120){for(int i=0;i<3;++i)gyroBias_[i]=biasSum_[i]/float(calibrationSamples_);calibrating_=false;veyra::log::info("remoteplay-input","Stationary gyro calibration completed");}
                }
                ControllerState::MotionSample reading;reading.accel=sensorAccel_;reading.timestampUs=sensorStamp_;
                for(int i=0;i<3;++i)reading.gyro[i]=sensorGyro_[i]-gyroBias_[i];
                if(state.motionSampleCount<state.motionSamples.size())state.motionSamples[state.motionSampleCount++]=reading;
                else{std::move(state.motionSamples.begin()+1,state.motionSamples.end(),state.motionSamples.begin());state.motionSamples.back()=reading;}
            }
        }
    }
    if(!focused||!gamepad_){touchQueue_.clear();touchState_={};touchIds_={-1,-1};haveGyro_=haveAccel_=false;releaseEffects();return state;}
    state.inputActive=true;
    struct Button{SDL_GamepadButton input;ControllerState::Button output;};
    constexpr Button buttons[]={
        {SDL_GAMEPAD_BUTTON_SOUTH,ControllerState::Cross},{SDL_GAMEPAD_BUTTON_EAST,ControllerState::Circle},
        {SDL_GAMEPAD_BUTTON_WEST,ControllerState::Square},{SDL_GAMEPAD_BUTTON_NORTH,ControllerState::Triangle},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,ControllerState::Left},{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,ControllerState::Right},
        {SDL_GAMEPAD_BUTTON_DPAD_UP,ControllerState::Up},{SDL_GAMEPAD_BUTTON_DPAD_DOWN,ControllerState::Down},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,ControllerState::L1},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,ControllerState::R1},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,ControllerState::L3},{SDL_GAMEPAD_BUTTON_RIGHT_STICK,ControllerState::R3},
        {SDL_GAMEPAD_BUTTON_START,ControllerState::Options},{SDL_GAMEPAD_BUTTON_BACK,ControllerState::Share},
        {SDL_GAMEPAD_BUTTON_GUIDE,ControllerState::PS},{SDL_GAMEPAD_BUTTON_TOUCHPAD,ControllerState::Touchpad}};
    for(auto b:buttons)if(SDL_GetGamepadButton(gamepad_,b.input))state.buttons|=b.output;
    state.leftX=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFTX);state.leftY=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFTY);
    state.rightX=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHTX);state.rightY=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHTY);
    state.l2=uint8_t(std::max(0,int(SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)))*255/32767);
    state.r2=uint8_t(std::max(0,int(SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)))*255/32767);
    if(!touchQueue_.empty()){state.touches=touchQueue_.front();touchQueue_.pop_front();}
    else{
        for(int finger=0;finger<2;++finger){bool down=false;float x=0,y=0,pressure=0;
            if(SDL_GetNumGamepadTouchpads(gamepad_)>0&&SDL_GetGamepadTouchpadFinger(gamepad_,0,finger,&down,&x,&y,&pressure)&&down&&std::isfinite(x)&&std::isfinite(y)){
                auto& touch=touchState_[finger];if(touch.id<0)touch.id=int8_t(nextTouchId_++&0x7f);touch.x=uint16_t(std::clamp(x,0.f,1.f)*1919);touch.y=uint16_t(std::clamp(y,0.f,1.f)*1079);
            }else touchState_[finger]={};
        }
        state.touches=touchState_;
    }
    if(gyro_&&accel_&&haveGyro_&&haveAccel_){state.motionValid=true;state.motionTimestampUs=sensorStamp_;state.accel=sensorAccel_;for(int i=0;i<3;++i)state.gyro[i]=sensorGyro_[i]-gyroBias_[i];}
    return state;
}
void ControllerInput::releaseEffects(){
    if(!effectsActive_)return;
    if(gamepad_){
        if(!SDL_RumbleGamepad(gamepad_,0,0,0))veyra::log::warn("remoteplay-feedback",SDL_GetError());
        if(SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5){
            std::array<uint8_t,47> effect{};effect[0]=0x0c;effect[10]=5;effect[21]=5;
            if(!SDL_SendGamepadEffect(gamepad_,effect.data(),int(effect.size())))veyra::log::warn("remoteplay-feedback",SDL_GetError());
        }
    }
    if(hapticStream_)SDL_ClearAudioStream(hapticStream_);
    if(bt_)bt_->clear();
    effectsActive_=false;
}
// The haptic route (audio endpoint or Bluetooth reports) is chosen again for the next pad.
void ControllerInput::closeHaptics(){
    if(hapticStream_){SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;}
    if(bt_){
        const auto totals=bt_->totals();delete bt_;bt_=nullptr;
        veyra::log::info("remoteplay-haptics",std::format("Bluetooth haptics closed: reports={} writeErrors={} waits={} droppedFrames={}",totals.reports,totals.writeErrors,totals.waits,totals.framesDropped));
    }
    hapticAttempted_=false;
}
void ControllerInput::feedback(ControllerFeedback f,bool focused){
    if(!focused||!gamepad_){releaseEffects();return;}
    if(f.rumble){effectsActive_=true;if(!SDL_RumbleGamepad(gamepad_,uint16_t(f.left)*257,uint16_t(f.right)*257,5000))veyra::log::warn("remoteplay-feedback",SDL_GetError());}
    if(f.triggers&&SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5){
        // SDL DualSense effects report payload (47 bytes): right at10, left at21.
        std::array<uint8_t,47> effect{};effect[0]=0x0c;
        std::copy(f.rightTrigger.begin(),f.rightTrigger.end(),effect.begin()+10);
        std::copy(f.leftTrigger.begin(),f.leftTrigger.end(),effect.begin()+21);
        effectsActive_=true;
        if(!SDL_SendGamepadEffect(gamepad_,effect.data(),int(effect.size())))veyra::log::warn("remoteplay-feedback",SDL_GetError());
    }
    if(f.haptics.empty()||SDL_GetGamepadType(gamepad_)!=SDL_GAMEPAD_TYPE_PS5)return;
    if(!hapticAttempted_&&SDL_GetGamepadConnectionState(gamepad_)!=SDL_JOYSTICK_CONNECTION_WIRED){
        // A pad on Bluetooth has no audio endpoint of its own. Its haptic audio goes out as
        // HID reports through a second handle on the pad itself, so neither SDL audio nor
        // the other connected pads and endpoints matter for it. open() looks at the bus of
        // the HID device; a pad that SDL reports as wired is not asked.
        std::string why;
        bt_=DualSenseBtHaptics::open(gamepad_,why);
        if(bt_){
            hapticAttempted_=true;
            // The pad keeps what the previous host set. Make sure nothing mutes or weakens
            // the actuators: mute control section all clear (haptics neither muted nor in
            // power save), no motor power reduction, haptic low-pass filter on. Offsets as in
            // SDL's DS5EffectsState_t: [1] valid flags, [9] mute control, [36] motor power,
            // [39] filter. The same flags as in AnyPS5 issue 2056, where a pad that did not
            // get this report played at 12.5% of the level sent.
            std::array<uint8_t,47> setup{};setup[1]=0x02|0x20|0x40;setup[39]=0x01;
            if(!SDL_SendGamepadEffect(gamepad_,setup.data(),int(setup.size())))veyra::log::warn("remoteplay-haptics",SDL_GetError());
            veyra::log::info("remoteplay-haptics","DualSense over Bluetooth: haptic audio is sent as HID report 0x32, one every 10.7 ms");
        }else veyra::log::warn("remoteplay-haptics","Bluetooth haptics route not opened ("+why+"); trying the audio endpoint");
    }
    if(!hapticAttempted_){
        hapticAttempted_=true;
        if(!audioInitialized_)audioInitialized_=SDL_InitSubSystem(SDL_INIT_AUDIO);
        if(!audioInitialized_){veyra::log::error("remoteplay-haptics",SDL_GetError());return;}
        // Never use the default speakers or a stereo endpoint. Ambiguity fails closed.
        int count=0;auto* devices=SDL_GetAudioPlaybackDevices(&count);SDL_AudioDeviceID selected=0;int matches=0;
        for(int i=0;i<count;++i){const auto* name=SDL_GetAudioDeviceName(devices[i]);SDL_AudioSpec spec{};
            if(name&&(std::strstr(name,"Wireless Controller")||std::strstr(name,"DualSense"))&&SDL_GetAudioDeviceFormat(devices[i],&spec,nullptr)&&spec.channels==4){selected=devices[i];++matches;}
        }
        SDL_free(devices);
        int gamepadCount=0;auto* gamepads=SDL_GetGamepads(&gamepadCount);SDL_free(gamepads);
        if(matches!=1||gamepadCount!=1){veyra::log::warn("remoteplay-haptics","No unique four-channel DualSense endpoint/controller pair; haptics not routed");return;}
        SDL_AudioSpec input{SDL_AUDIO_S16,4,3000};
        hapticStream_=SDL_OpenAudioDeviceStream(selected,&input,nullptr,nullptr);
        if(!hapticStream_||!SDL_ResumeAudioStreamDevice(hapticStream_)){veyra::log::error("remoteplay-haptics",SDL_GetError());if(hapticStream_)SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;return;}
        veyra::log::info("remoteplay-haptics","Dedicated four-channel endpoint opened; speaker channels silent");
    }
    if(bt_){
        if(bt_->failed()){
            veyra::log::warn("remoteplay-haptics","A Bluetooth haptics report could not be written; the route stays closed until the pad is activated again");
            closeHaptics();hapticAttempted_=true;return;
        }
        effectsActive_=true;bt_->push(f.haptics.data(),f.haptics.size()/2);
        return;
    }
    if(!hapticStream_)return;
    std::vector<int16_t> quad;quad.reserve(f.haptics.size()*2);
    for(size_t i=0;i+1<f.haptics.size();i+=2){quad.push_back(0);quad.push_back(0);quad.push_back(f.haptics[i]);quad.push_back(f.haptics[i+1]);}
    if(SDL_GetAudioStreamQueued(hapticStream_)>2400)SDL_ClearAudioStream(hapticStream_);
    effectsActive_=true;
    if(!SDL_PutAudioStreamData(hapticStream_,quad.data(),int(quad.size()*sizeof(int16_t))))veyra::log::warn("remoteplay-haptics",SDL_GetError());
}

bool ControllerInput::calibrate(){
    if(!gamepad_||!gyro_||!accel_)return false;
    biasSum_={};calibrationSamples_=0;calibrationStart_=0;calibrating_=true;return true;
}
ControllerInput::Capabilities ControllerInput::capabilities()const{
    if(!gamepad_)return {};
    const char* name=SDL_GetGamepadName(gamepad_);
    return {true,gyro_,accel_,SDL_GetNumGamepadTouchpads(gamepad_)>0,
        SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5,hapticStream_!=nullptr||(bt_&&!bt_->failed()),calibrating_,name?name:""};
}

}
