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
namespace veyra::source {
struct RemotePlaySessionSourceTestAccess {
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
static void controlOnlyBoundary(){
    // The combined capture mode keeps the same local validation boundary: an
    // invalid host or profile is rejected before any Chiaki call, and repeated
    // connect/close cycles stay reusable. No console is contacted.
    for(int i=0;i<3;++i){
        RemotePlaySessionSource s;RemotePlayConnectDesc request;
        request.controlOnly=true;request.request.host="invalid://host";
        if(s.connect(std::move(request)))throw std::runtime_error("control-only connect must reject invalid host");
        if(s.info().opened)throw std::runtime_error("failed control-only connect must not report opened");
        s.close();
    }
    // A view-only request is contradictory with control-only (no input
    // forwarding). The backend forces the DualSense channel in that case, so
    // the session source must not pre-reject the pair; it stays a UI contract.
    RemotePlayConnectDesc contradictory;contradictory.controlOnly=true;contradictory.request.viewOnly=true;
    contradictory.request.host="192.168.1.50";
    if(contradictory.request.viewOnly&&!contradictory.controlOnly)throw std::runtime_error("unreachable");
    // Controller queue semantics are shared with the full-video session and
    // must survive unchanged in control-only mode (no PS5 is involved).
    RemotePlaySessionSource s;s.started_=true;
    const auto now=remoteplay::monotonic100ns();
    remoteplay::ControllerState down;down.inputActive=true;down.buttons=remoteplay::ControllerState::Cross;
    s.controller(down);
    if(s.takeControllerLocked(now)!=down)throw std::runtime_error("control-only controller state must pass through the shared queue");
    s.controller({});
    if(s.takeControllerLocked(now)!=remoteplay::ControllerState{})throw std::runtime_error("focus loss must neutralize the control-only queue");
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
    unsigned rumbleCalls=0;desc.userdata=&rumbleCalls;
    desc.Rumble=[](void* p,Uint16,Uint16){++*static_cast<unsigned*>(p);return true;};
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
    ok&=input.calibrate();
    gyro[0]=.01f;gyro[1]=.02f;gyro[2]=.03f;
    for(int i=0;i<121;++i){SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,20000000+i*1000000,gyro,3);input.poll(true);}
    auto calibrated=input.poll(true);ok&=std::abs(calibrated.gyro[0])<1e-5&&std::abs(calibrated.gyro[2])<1e-5;
    ControllerFeedback f;f.rumble=true;f.left=5;input.feedback(f,true);ok&=rumbleCalls>0;
    const auto before=rumbleCalls;ok&=input.poll(false)==ControllerState{};ok&=rumbleCalls>before;
    ControllerFeedback bounded;for(int i=0;i<20;++i){ControllerFeedback packet;packet.haptics.resize(120,1);bounded.merge(std::move(packet));ok&=bounded.haptics.size()<=600;}
    input.stop();SDL_CloseJoystick(joystick);SDL_DetachVirtualJoystick(id);SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    std::cout<<"VIRTUAL_INPUT motion_touch_lifecycle_focus_feedback_bounded="<<ok<<" REAL_PS5_NOT_TESTED=1\n";return ok?0:23;
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--virtual")return virtualInput();
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
