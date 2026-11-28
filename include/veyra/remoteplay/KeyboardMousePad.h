// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Keyboard and mouse as the pad of a PS5 Remote Play session (a PS5 stream, or the pad-only
// session of a capture card). It fills the same ControllerState a real pad does, so nothing
// downstream changes and no virtual pad driver is involved. Pure state, no Win32 and no Qt:
// the window's input capture feeds it and the pad timer samples it.
//
// Inputs are codes: a PC scan code (set 1) for a key, +0x100 for an extended key, i.e.
// physical positions (WASD is ZQSD on AZERTY); kMouse+1..5 for the mouse buttons and
// kWheelUp / kWheelDown for the wheel. Each input is bound to one action of kActions; the
// bindings are a saved setting and the defaults follow the GTA VI default control scheme.
#include "Types.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace veyra::remoteplay {
class KeyboardMousePad {
public:
    // Outputs above the 16 pad buttons: the left stick, the triggers and the vehicle layer.
    enum : std::uint32_t {StickUp=1u<<16,StickDown=1u<<17,StickLeft=1u<<18,StickRight=1u<<19,
                          TriggerL=1u<<20,TriggerR=1u<<21,VehicleToggle=1u<<22};
    static constexpr std::uint32_t kMouse=0x200, kWheelUp=0x206, kWheelDown=0x207, kCodes=0x208;

    struct Action{std::string_view id;std::uint32_t outputs;};
    // The order is the order of the key settings menu; the ids are what the setting stores.
    static constexpr Action kActions[]={
        {"stickUp",StickUp},{"stickDown",StickDown},{"stickLeft",StickLeft},{"stickRight",StickRight},
        {"cross",ControllerState::Cross},{"circle",ControllerState::Circle},
        {"square",ControllerState::Square},{"triangle",ControllerState::Triangle},
        {"l1",ControllerState::L1},{"r1",ControllerState::R1},{"l2",TriggerL},{"r2",TriggerR},
        {"l3",ControllerState::L3},{"r3",ControllerState::R3},
        {"focus",ControllerState::L3|ControllerState::R3},   // GTA VI: L3+R3 together
        {"up",ControllerState::Up},{"down",ControllerState::Down},{"left",ControllerState::Left},{"right",ControllerState::Right},
        {"options",ControllerState::Options},{"share",ControllerState::Share},
        {"touchpad",ControllerState::Touchpad},{"ps",ControllerState::PS},
        {"vehicle",VehicleToggle}};
    struct Binding{std::uint32_t code;std::string_view action;};
    // GTA VI default scheme, with the keys a GTA V PC player expects where the console allows.
    static constexpr Binding kDefaults[]={
        {0x11,"stickUp"},{0x1f,"stickDown"},{0x1e,"stickLeft"},{0x20,"stickRight"},   // W S A D
        {kMouse+1,"r2"},{kMouse+3,"l2"},{kMouse+2,"r3"},                            // shoot, aim, zoom
        {kMouse+4,"l1"},{kMouse+5,"r1"},                                            // side buttons
        {0x39,"cross"},                    // Space: jump, climb, handbrake
        {0x2a,"l3"},                       // Left Shift: run, horn and nitrous
        {0x1d,"circle"},                   // Left Ctrl: crouch, duck in a car
        {0x13,"square"},                   // R: melee, shove
        {0x12,"triangle"},{0x21,"triangle"},// E and F: interact, enter and leave a vehicle
        {0x10,"r1"},                       // Q: cover, quick throw, dive, shoot from a vehicle
        {0x0f,"l1"},                       // Tab: holster (tap), loadout (hold)
        {0x2e,"r3"},                       // C: look behind
        {0x30,"focus"},                    // B: Focus
        {0x148,"up"},{0x150,"down"},{0x14b,"left"},{0x14d,"right"},                  // arrows: phone, HUD, radio, partner
        {0x01,"options"},{0x32,"touchpad"},{0x0e,"share"},{0x147,"ps"},              // Esc, M (map), Backspace, Home
        {0x2d,"vehicle"}};                 // X: on foot / in a vehicle

    // Mouse speed, in counts per millisecond, that pushes the right stick fully over at sensitivity 1.
    static constexpr double kFullSpeed=8.0;
    // The stick starts here as soon as the mouse moves, to get past the game's own deadzone.
    // shortcut: one anti-deadzone and a linear curve for every game; make them per-game
    // settings when a title needs it (a larger game deadzone swallows slow movements).
    static constexpr double kDeadzone=0.15;

    KeyboardMousePad(){resetBindings();}

    static const Action* action(std::string_view id){
        for(const auto& a:kActions)if(a.id==id)return &a;
        return nullptr;
    }
    static bool validCode(std::uint32_t code){return code>0&&code<kCodes&&code!=kMouse;}
    void resetBindings(){bindings_.clear();for(const auto& b:kDefaults)bindings_.push_back({b.code,b.action});}
    const std::vector<Binding>& bindings()const{return bindings_;}
    // An input does one action: binding it again moves it. An empty action unbinds it.
    void bind(std::uint32_t code,std::string_view id){
        std::erase_if(bindings_,[&](const Binding& b){return b.code==code;});
        if(const auto* a=action(id);a&&validCode(code))bindings_.push_back({code,a->id});
    }
    // "code:action,code:action" with hexadecimal codes, e.g. "11:stickUp,201:r2".
    std::string serialize()const{
        std::string out;char hex[8];
        for(const auto& b:bindings_){
            const auto r=std::to_chars(hex,hex+sizeof hex,b.code,16);
            if(!out.empty())out+=',';
            out.append(hex,r.ptr);out+=':';out+=b.action;
        }
        return out;
    }
    // Unknown actions and bad codes are skipped; an empty or unreadable setting is the defaults.
    void parse(std::string_view text){
        std::vector<Binding> next;
        while(!text.empty()){
            const auto comma=text.find(',');
            const auto item=text.substr(0,comma);
            text=comma==text.npos?std::string_view{}:text.substr(comma+1);
            const auto colon=item.find(':');
            if(colon==item.npos)continue;
            std::uint32_t code=0;
            if(std::from_chars(item.data(),item.data()+colon,code,16).ec!=std::errc{})continue;
            const auto* a=action(item.substr(colon+1));
            if(!a||!validCode(code))continue;
            std::erase_if(next,[&](const Binding& b){return b.code==code;});
            next.push_back({code,a->id});
        }
        if(next.empty())resetBindings();else bindings_=std::move(next);
    }

    void setSensitivity(double value){sensitivity_=std::clamp(value,0.1,10.0);}
    void key(std::uint32_t scanCode,bool extended,bool down){input((scanCode&0xffu)|(extended?0x100u:0u),down);}
    void mouseButton(int button,bool down){if(button>=1&&button<=5)input(kMouse+std::uint32_t(button),down);}
    // A wheel notch is a short press: long enough for the 8 ms pad timer to send it twice.
    void wheel(int delta){if(delta)pulse_[delta>0?0:1]=2;}
    void mouseMove(int dx,int dy){dx_+=dx;dy_+=dy;}
    bool vehicle()const{return vehicle_;}
    // Nothing held, mouse at rest, on foot. The bindings and the sensitivity are settings and stay.
    void reset(){pressed_.assign(kCodes,false);pulse_[0]=pulse_[1]=0;dx_=dy_=0;vx_=vy_=0;idle_=0;last_=0;vehicle_=false;toggleHeld_=false;}

    // Adds the keyboard and mouse to what the pad reports (there may be no pad at all).
    // Called once per pad sample; the mouse movement since the previous call becomes
    // the right stick: direction of the movement, deflection from its speed.
    void apply(ControllerState& s,HostTime now){
        std::uint32_t held=outputs();
        const bool toggle=(held&VehicleToggle)!=0;
        if(toggle&&!toggleHeld_)vehicle_=!vehicle_;
        toggleHeld_=toggle;
        if(vehicle_){
            // GTA VI in a vehicle: R2 accelerates, L2 brakes, R1 shoots. W and S drive, the
            // shoot button shoots from the vehicle, A and D still steer.
            const bool shoot=(held&TriggerR)!=0;
            held&=~TriggerR;
            if(shoot)held|=ControllerState::R1;
            if(held&StickUp)held|=TriggerR;
            if(held&StickDown)held|=TriggerL;
            held&=~(StickUp|StickDown);
        }
        s.inputActive=true;
        s.buttons|=held&0xffffu;
        if(held&TriggerL)s.l2=255;
        if(held&TriggerR)s.r2=255;
        const int x=((held&StickRight)?1:0)-((held&StickLeft)?1:0),y=((held&StickDown)?1:0)-((held&StickUp)?1:0);
        if(x||y){const double full=32767.0/std::hypot(x,y);s.leftX=std::int16_t(x*full);s.leftY=std::int16_t(y*full);} // a diagonal is not faster
        for(auto& p:pulse_)if(p)--p;

        const double ms=last_&&now>last_?std::min(double(now-last_)/10000.0,50.0):8.0;last_=now;
        idle_=(dx_||dy_)?0:idle_+1;
        // Mouse reports and pad samples do not line up (a 125 Hz mouse against an 8 ms
        // timer): two samples are averaged so one empty sample is not read as a stop,
        // and two empty samples in a row are the stop.
        if(idle_>=2)vx_=vy_=0;
        else{vx_=(vx_+dx_/ms)/2;vy_=(vy_+dy_/ms)/2;}
        dx_=dy_=0;
        const double speed=std::hypot(vx_,vy_);
        if(speed>0){
            const double level=std::min(1.0,kDeadzone+(1.0-kDeadzone)*speed*sensitivity_/kFullSpeed)*32767.0/speed;
            s.rightX=std::int16_t(vx_*level);s.rightY=std::int16_t(vy_*level);
        }
    }
private:
    static bool modifier(std::uint32_t code){return code==0x1d||code==0x11d||code==0x2a||code==0x36||code==0x38||code==0x138;}
    void input(std::uint32_t code,bool down){if(code<kCodes)pressed_[code]=down;}
    std::uint32_t outputs()const{
        // Ctrl+Alt held is the start of the release chord (Ctrl+Alt+Shift+Z): the modifiers
        // then press nothing, so releasing the capture does not crouch or run in the game.
        const bool chord=(pressed_[0x1d]||pressed_[0x11d])&&(pressed_[0x38]||pressed_[0x138]);
        std::uint32_t held=0;
        for(const auto& b:bindings_){
            const bool on=b.code==kWheelUp?pulse_[0]>0:b.code==kWheelDown?pulse_[1]>0:pressed_[b.code];
            if(on&&!(chord&&modifier(b.code)))held|=action(b.action)->outputs;
        }
        return held;
    }
    std::vector<Binding> bindings_;
    std::vector<bool> pressed_=std::vector<bool>(kCodes,false);
    int pulse_[2]{};
    long long dx_=0,dy_=0;   // mouse counts since the last sample
    double vx_=0,vy_=0;      // smoothed speed, counts per millisecond
    int idle_=0;             // samples in a row without mouse movement
    HostTime last_=0;
    double sensitivity_=1.0;
    bool vehicle_=false,toggleHeld_=false;
};
} // namespace veyra::remoteplay
