#include "veyra/source/RemotePlaySessionSource.h"
#include "veyra/remoteplay/ControllerInput.h"
#include "veyra/remoteplay/Discovery.h"
#include <string_view>
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#include <stdexcept>
#include <algorithm>
namespace veyra::source {
struct RemotePlaySessionSourceTestAccess {
static bool slowConsumer(){
    RemotePlaySessionSource s;s.started_=true;
    const auto base=remoteplay::monotonic100ns();
    unsigned neutral=0,lagged=0;size_t highWater=0;
    // UI at 125 Hz; decoder/controller owner at 62.5 Hz, matching the field
    // backlog. No real device, console, sleeping or altered expiry threshold.
    for(int tick=0;tick<625;++tick){
        const auto now=base+tick*80000LL;
        remoteplay::ControllerState held;held.inputActive=true;held.r2=255;held.rightX=int16_t(tick%100);
        s.controller(held);s.controllerStamp_=now;s.pendingControllers_.back().first=now;
        highWater=std::max(highWater,s.pendingControllers_.size());
        if(tick%2==0){const auto out=s.takeControllerLocked(now);if(!out.inputActive||out.r2!=255)++neutral;if(out.rightX!=held.rightX)++lagged;}
    }
    std::cout<<"HELD_RT_SLOW_CONSUMER neutral="<<neutral<<" lagged="<<lagged<<" highWater="<<highWater<<"\n";
    // A stale edge before a fresh held state must not synthesize a release.
    s.pendingControllers_.clear();
    remoteplay::ControllerState old;old.inputActive=true;old.buttons=remoteplay::ControllerState::Cross;
    auto current=old;current.buttons=0;current.r2=255;
    auto pressed=current;
    pressed.buttons=remoteplay::ControllerState::Circle;
    pressed.touches[0].id=2;
    s.pendingControllers_.push_back({base,old});
    s.pendingControllers_.push_back({base+1100000,pressed});
    s.pendingControllers_.push_back({base+1200000,current});
    s.controllerStamp_=base+1200000;
    const bool fresh=s.takeControllerLocked(base+1200000)==pressed
                     &&s.takeControllerLocked(base+1200000)==current;
    const bool stale=s.takeControllerLocked(base+2300001)==remoteplay::ControllerState{};
    s.controller(current);s.controller({});
    const bool focus=s.takeControllerLocked(remoteplay::monotonic100ns())==remoteplay::ControllerState{};
    // Coalescing preserves every sensor sample up to the existing bound.
    for(unsigned i=1;i<=20;++i){
        auto motion=current;motion.motionValid=true;motion.motionSampleCount=1;
        motion.motionTimestampUs=i;motion.motionSamples[0].timestampUs=i;
        s.controller(motion);
    }
    const auto sensor=s.pendingControllers_.back().second;
    const bool ordered=sensor.motionSampleCount==16&&sensor.motionSamples[0].timestampUs==5&&sensor.motionSamples[15].timestampUs==20;
    s.pendingControllers_.clear();auto released=current;released.r2=0;
    s.controller(released);s.controller(current);s.controller(released);
    const auto tapTime=remoteplay::monotonic100ns();
    const auto beforeTap=s.takeControllerLocked(tapTime),duringTap=s.takeControllerLocked(tapTime),afterTap=s.takeControllerLocked(tapTime);
    const bool triggerEdges=beforeTap.r2==0&&duringTap.r2==255&&afterTap.r2==0;
    s.pendingControllers_.clear();
    for(int i=0;i<40;++i){auto edge=current;edge.buttons=(i%2)?remoteplay::ControllerState::Cross:0;s.controller(edge);}
    const bool bounded=s.pendingControllers_.size()<=16;
    std::cout<<"INPUT_SAFETY fresh_after_stale="<<fresh<<" stale_producer_release="<<stale<<" focus_release="<<focus<<" bounded_ordered_motion="<<ordered<<" trigger_edges="<<triggerEdges<<" bounded_edges="<<bounded<<"\n";
    return neutral==0&&lagged==0&&fresh&&stale&&focus&&ordered&&triggerEdges&&bounded;
}
static void mailbox(){
    RemotePlaySessionSource s;s.started_=true;
    const auto now=remoteplay::monotonic100ns();
    auto inbox=std::make_shared<remoteplay::SessionInbox>();auto token=inbox->begin(now-20000000);token.connected();
    inbox->decodeStarted(now-15000000,now-15050000);inbox->decodeFinished(now-14900000);inbox->frameDecoded(now-14900000);
    s.telemetryInbox_=inbox;
    auto progress=s.sessionSnapshot();
    if(progress.decodedFrames!=1||progress.decodedFps!=0||progress.decodeMeanMs||!progress.ratesReady)throw std::runtime_error("stale decode telemetry");
    inbox->decodeStarted(now-100000,now-150000);inbox->decodeFinished(now);inbox->frameDecoded(now);
    progress=s.sessionSnapshot();
    if(progress.decodedFrames!=2||progress.decodedFps!=1||progress.decodeMeanMs!=10.0||progress.ingressWaitMeanMs!=5.0||progress.decodeStarted100ns)throw std::runtime_error("independent decode telemetry");
    remoteplay::ControllerState down;down.inputActive=true;down.touches[0].id=1;
    s.controller(down);s.controller({});
    if(s.takeControllerLocked(now)!=remoteplay::ControllerState{})throw std::runtime_error("focus loss must cancel pending actions");
    s.controller(down);auto up=down;up.touches[0].id=-1;s.controller(up);
    if(s.takeControllerLocked(now).touches[0].id!=1||s.takeControllerLocked(now).touches[0].id!=-1)throw std::runtime_error("input edges overwritten by decoder owner");
    for(int i=0;i<40;++i)s.controller(down);
    if(s.pendingControllers_.size()>16||s.takeControllerLocked(now+2000000)!=remoteplay::ControllerState{})throw std::runtime_error("input queue bound or stale release");
    AVFrame* raw=av_frame_alloc();raw->width=16;raw->height=16;raw->format=AV_PIX_FMT_YUV420P;
    if(av_frame_get_buffer(raw,32)<0)throw std::bad_alloc();
    pipeline::FramePacket packet;SourceInfo info;info.width=16;info.height=16;
    packet.sequence=1;packet.flags=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Open);s.publishDecoded(raw,packet,info);
    packet.sequence=2;packet.flags=0;s.publishDecoded(raw,packet,info);av_frame_free(&raw);
    const AVFrame* frame=nullptr;pipeline::FramePacket out;
    if(s.read(out,&frame)!=SourceReadStatus::Frame||!frame||out.sequence!=2||s.skipped()!=1||!pipeline::breaksHistory(out.flags))throw std::runtime_error("latest decoded mailbox");
    if(!pipeline::hasFrameFlag(out.flags,pipeline::FrameFlagBits::Drop)||pipeline::hasFrameFlag(out.flags,pipeline::FrameFlagBits::Discontinuity))throw std::runtime_error("decoded overwrite must be a soft Drop, not a clock discontinuity");
    if(s.read(out,&frame)!=SourceReadStatus::Waiting)throw std::runtime_error("mailbox replay");
    raw=av_frame_alloc();raw->width=16;raw->height=16;raw->format=AV_PIX_FMT_YUV420P;
    if(av_frame_get_buffer(raw,32)<0)throw std::bad_alloc();
    packet.sequence=1;packet.sourceEpoch=uint64_t(1)<<32;
    packet.flags=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
    s.publishDecoded(raw,packet,info);packet.sequence=2;packet.flags=0;s.publishDecoded(raw,packet,info);av_frame_free(&raw);
    if(s.read(out,&frame)!=SourceReadStatus::Frame||out.sequence!=4||out.sourceEpoch!=(uint64_t(1)<<32)||!pipeline::hasFrameFlag(out.flags,pipeline::FrameFlagBits::Discontinuity))throw std::runtime_error("reconnect keeps application sequence and pending clock reset across mailbox overwrite");
}
// PS5 手柄控制 (from PR #7): the same local validation boundary as a stream, an
// owner loop that is over once the connect failed, and the shared pad queue.
// No console is contacted.
static void controlOnlyBoundary(){
    for(int i=0;i<3;++i){
        RemotePlaySessionSource s;RemotePlayConnectDesc request;
        request.controlOnly=true;request.request.host="invalid://host";
        if(s.connect(std::move(request)))throw std::runtime_error("controller-only connect must reject an invalid host");
        if(s.info().opened)throw std::runtime_error("failed controller-only connect must not report opened");
        if(!s.failed())throw std::runtime_error("failed controller-only connect must report failed");
        s.close();
        if(s.alive())throw std::runtime_error("closed controller-only session must not be alive");
    }
    RemotePlaySessionSource s;s.started_=true;
    const auto now=remoteplay::monotonic100ns();
    remoteplay::ControllerState down;down.inputActive=true;down.buttons=remoteplay::ControllerState::Cross;
    s.controller(down);
    if(s.takeControllerLocked(now)!=down)throw std::runtime_error("controller-only pad state must pass through the shared queue");
    s.controller({});
    if(s.takeControllerLocked(now)!=remoteplay::ControllerState{})throw std::runtime_error("focus loss must neutralize the controller-only queue");
}
};
}

int virtualInput(){
    using namespace veyra::remoteplay;
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))return 20;
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id=0x054c;desc.product_id=0x0ce6;desc.name="Veyra isolated input fixture";
    desc.naxes=6;desc.nbuttons=21;desc.button_mask=(1u<<21)-1;desc.axis_mask=63;
    SDL_VirtualJoystickTouchpadDesc touch{};touch.nfingers=2;desc.ntouchpads=1;desc.touchpads=&touch;
    SDL_VirtualJoystickSensorDesc sensors[2]={{SDL_SENSOR_GYRO,120},{SDL_SENSOR_ACCEL,120}};desc.nsensors=2;desc.sensors=sensors;
    desc.SetSensorsEnabled=[](void*,bool){return true;};
    struct Fixture {unsigned rumble=0,led=0;Uint8 rgb[3]{};int player=-1;} fixture;desc.userdata=&fixture;
    unsigned& rumbleCalls=fixture.rumble;
    desc.Rumble=[](void* p,Uint16,Uint16){++static_cast<Fixture*>(p)->rumble;return true;};
    desc.SetLED=[](void* p,Uint8 r,Uint8 g,Uint8 b){auto& f=*static_cast<Fixture*>(p);++f.led;f.rgb[0]=r;f.rgb[1]=g;f.rgb[2]=b;return true;};
    desc.SetPlayerIndex=[](void* p,int index){static_cast<Fixture*>(p)->player=index;};
    desc.SendEffect=[](void*,const void*,int){return true;};
    const auto id=SDL_AttachVirtualJoystick(&desc);if(!id)return 21;
    auto* joystick=SDL_OpenJoystick(id);ControllerInput input;
    if(!joystick||!input.start(id))return 22;
    input.poll(true);
    float gyro[3]={.1f,.2f,.3f},accel[3]={0,SDL_STANDARD_GRAVITY,0};
    SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,10000000,gyro,3);
    SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_ACCEL,10000000,accel,3);
    SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,.25f,.75f,1);
    SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,1,0,1);
    auto first=input.poll(true);bool ok=first.motionValid&&std::abs(first.accel[1]-1)<1e-5&&std::abs(first.gyro[2]-.3f)<1e-5;
    ok&=first.motionSampleCount>0;
    first=input.poll(true);
    ok&=first.touches[0].id>=0&&first.touches[1].id>=0&&first.touches[0].id!=first.touches[1].id&&first.touches[0].x==479&&first.touches[0].y==809;
    SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,.5f,.5f,1);auto moved=input.poll(true);ok&=moved.touches[0].id==first.touches[0].id&&moved.touches[0].x==959;
    SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0);auto up=input.poll(true);ok&=up.touches[0].id==-1;
    // Both edges arrive between UI ticks: a tap must still produce down then up.
    SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,.4f,.4f,1);SDL_UpdateGamepads();
    SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,.4f,.4f,0);SDL_UpdateGamepads();
    auto tapDown=input.poll(true),tapUp=input.poll(true);ok&=tapDown.touches[0].id>=0&&tapUp.touches[0].id<0;
    auto click=touchpadPressOnly(tapDown);
    ok&=click.touches[0].id<0&&!(click.buttons&ControllerState::Touchpad);
    tapDown.buttons|=ControllerState::Touchpad;
    click=touchpadPressOnly(tapDown);
    ok&=click.touches[0].id<0&&(click.buttons&ControllerState::Touchpad);
    ok&=input.calibrate();
    gyro[0]=.01f;gyro[1]=.02f;gyro[2]=.03f;
    for(int i=0;i<121;++i){SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,20000000+i*1000000,gyro,3);input.poll(true);}
    auto calibrated=input.poll(true);ok&=std::abs(calibrated.gyro[0])<1e-5&&std::abs(calibrated.gyro[2])<1e-5;
    ControllerFeedback f;f.rumble=true;f.left=5;input.feedback(f,true);ok&=rumbleCalls>0;
    const auto before=rumbleCalls;ok&=input.poll(false)==ControllerState{};ok&=rumbleCalls>before;
    // Console light bar / player indicator: applied when focused; a change while unfocused
    // is kept and applied when focus returns.
    ControllerFeedback lights;lights.led=true;lights.ledColor={255,64,0};lights.playerIndex=true;lights.player=2;
    input.feedback(lights,true);
    ok&=fixture.led>0&&fixture.rgb[0]==255&&fixture.rgb[1]==64&&fixture.rgb[2]==0&&fixture.player==2;
    ControllerFeedback later;later.led=true;later.ledColor={0,0,255};input.feedback(later,false);
    ok&=fixture.rgb[2]==0;
    input.feedback(ControllerFeedback{},true);ok&=fixture.rgb[0]==0&&fixture.rgb[2]==255;
    ControllerFeedback merged;merged.merge(lights);merged.merge(later);ok&=merged.led&&merged.ledColor[2]==255&&merged.playerIndex&&merged.player==2;
    // Held RT must never pulse neutral or become LT; focus loss releases it,
    // and returning focus must resume the held physical state without reconnect.
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);
    for(int n=0;n<250;++n){const auto held=input.poll(true);ok&=held.inputActive&&held.r2==255&&held.l2==0;}
    ok&=input.poll(false)==ControllerState{};
    const auto resumed=input.poll(true);ok&=resumed.inputActive&&resumed.r2==255&&resumed.l2==0;
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    const auto brake=input.poll(true);ok&=brake.l2==255&&brake.r2==0;
    ControllerFeedback bounded;for(int i=0;i<20;++i){ControllerFeedback packet;packet.haptics.resize(120,1);bounded.merge(std::move(packet));ok&=bounded.haptics.size()<=600;}
    input.stop();SDL_CloseJoystick(joystick);SDL_DetachVirtualJoystick(id);SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    std::cout<<"VIRTUAL_INPUT motion_touch_lifecycle_focus_feedback_bounded="<<ok<<" REAL_PS5_NOT_TESTED=1\n";return ok?0:23;
}
// 谁按键就用谁 (PS5 手柄控制): two virtual pads; the one whose button went down last
// is read. Real pads on the test machine may also be open, so only names of the
// virtual pads are asserted.
int followInput(){
    using namespace veyra::remoteplay;
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))return 30;
    // Distinct unknown product ids: pads with the same vendor/product share one
    // SDL mapping and so one reported name.
    auto attach=[](const char* name,Uint16 product){
        SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id=0x1209;desc.product_id=product;desc.name=name;
        desc.naxes=6;desc.nbuttons=15;desc.button_mask=(1u<<15)-1;desc.axis_mask=63;
        return SDL_AttachVirtualJoystick(&desc);
    };
    const auto a=attach("Veyra follow pad A",0x5641),b=attach("Veyra follow pad B",0x5642);if(!a||!b)return 31;
    auto* ja=SDL_OpenJoystick(a);auto* jb=SDL_OpenJoystick(b);if(!ja||!jb)return 32;
    const auto press=[&](SDL_Joystick* j,bool down){SDL_SetJoystickVirtualButton(j,SDL_GAMEPAD_BUTTON_SOUTH,down);};
    ControllerInput input;if(!input.start())return 33;
    input.setFollowLastPressed(true);
    bool ok=true;
    const auto check=[&](bool pass,const char* step){if(!pass)std::cout<<"FOLLOW_STEP_FAILED "<<step<<" active="<<input.capabilities().name<<'\n';ok&=pass;};
    input.poll(true); // opens every pad
    press(jb,true);auto state=input.poll(true);
    check(input.capabilities().name=="Veyra follow pad B"&&(state.buttons&ControllerState::Cross),"press B");
    press(jb,false);input.poll(true);
    press(ja,true);state=input.poll(true);
    check(input.capabilities().name=="Veyra follow pad A"&&(state.buttons&ControllerState::Cross),"press A");
    press(ja,false);input.poll(true);
    // Without focus (and no background permission) a press does not take over.
    press(jb,true);input.poll(false);press(jb,false);input.poll(false);
    check(input.capabilities().name=="Veyra follow pad A","unfocused press B");
    // The read pad leaves: another open pad is read instead.
    SDL_CloseJoystick(ja);SDL_DetachVirtualJoystick(a);input.poll(true);
    check(input.capabilities().connected&&input.capabilities().name!="Veyra follow pad A","detach A");
    // Follow off: a press on another pad no longer switches.
    input.setFollowLastPressed(false);
    const auto before=input.capabilities().name;
    press(jb,true);input.poll(true);press(jb,false);input.poll(true);
    check(input.capabilities().name==before,"follow off");
    input.stop();
    SDL_CloseJoystick(jb);SDL_DetachVirtualJoystick(b);SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    std::cout<<"FOLLOW_INPUT last_pressed_switch_focus_detach_off="<<ok<<" REAL_PS5_NOT_TESTED=1\n";return ok?0:34;
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--virtual")return virtualInput();
    if(argc==2&&std::string_view(argv[1])=="--follow")return followInput();
    if(argc==2&&std::string_view(argv[1])=="--control-stress"){
        for(int i=0;i<200;++i){
            veyra::source::RemotePlaySessionSource s;veyra::source::RemotePlayConnectDesc request;
            request.controlOnly=true;request.request.host="invalid://host";
            if(s.connect(std::move(request)))return 11;
            s.close();if(s.alive())return 12;
        }
        std::cout<<"CONTROL_STRESS_PASS iterations=200 PS5_NOT_TESTED=1\n";
        return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--discover"){
        const auto report=veyra::remoteplay::discoverLocalPs5({});
        for(const auto& line:report.diagnostics)std::cout<<line<<'\n';
        for(const auto& host:report.hosts)std::cout<<"PS5_HOST="<<host.host<<" standby="<<host.standby<<'\n';
        std::cout<<"DISCOVERY_RESULT hosts="<<report.hosts.size()<<" error="<<report.error<<'\n';
        return report.error&&report.hosts.empty()?5:0;
    }
    std::stop_source cancelled;cancelled.request_stop();
    if(!veyra::remoteplay::discoverLocalPs5(cancelled.get_token()).hosts.empty())return 6;
    veyra::source::RemotePlaySessionSourceTestAccess::mailbox();
    veyra::source::RemotePlaySessionSourceTestAccess::controlOnlyBoundary();
    if(!veyra::source::RemotePlaySessionSourceTestAccess::slowConsumer())return 44;
    veyra::source::RemotePlaySessionSource source;
    for(int i=0;i<3;++i){
        veyra::source::RemotePlayConnectDesc request;request.request.host="invalid://host";
        if(source.connect(std::move(request)))return 1;
        source.close();if(source.info().opened)return 2;
    }
    veyra::remoteplay::ControllerInput input;
    if(!input.start()){std::cerr<<"SDL_GAMEPAD_INITIALIZATION_FAILED\n";return 3;}
    for(int i=0;i<3;++i)if(input.poll(false)!=veyra::remoteplay::ControllerState{})return 4;
    input.stop();input.stop();
    std::cout<<"REMOTEPLAY_BOUNDARY_PASS invalid_connect_reopen=3 decoded_latest_mailbox=1 control_only_boundary=1 SDL_initialized=1 unfocused_neutral=1 PS5_NOT_TESTED=1\n";
    return 0;
}
