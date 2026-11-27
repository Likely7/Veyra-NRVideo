// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Keyboard and mouse as the pad of a PS5 Remote Play session. It fills the same
// ControllerState a real pad does, so nothing downstream changes and no virtual
// pad driver is involved. Pure state, no Win32 and no Qt: the window's input
// capture feeds it and the pad timer samples it.
// Keys are PC scan codes (set 1), i.e. physical positions: WASD is ZQSD on AZERTY.
// No modifier key is mapped, so the release chord (Ctrl+Alt+Shift+Z) presses
// nothing on the console.
#include "Types.h"
#include <algorithm>
#include <cmath>
namespace veyra::remoteplay {
class KeyboardMousePad {
    // Above the 16 pad buttons: what the left stick and the triggers are held by.
    enum : std::uint32_t {StickUp=1u<<16,StickDown=1u<<17,StickLeft=1u<<18,StickRight=1u<<19,TriggerL=1u<<20,TriggerR=1u<<21};
    struct Key{std::uint32_t code,bit;}; // code: scan code, +0x100 for an extended key
    // One key per button: a release always means the button is up.
    static constexpr Key kKeys[]={
        {0x11,StickUp},{0x1f,StickDown},{0x1e,StickLeft},{0x20,StickRight},     // W S A D
        {0x39,ControllerState::Cross},{0x2e,ControllerState::Circle},           // Space C
        {0x13,ControllerState::Square},{0x21,ControllerState::Triangle},        // R F
        {0x10,ControllerState::L1},{0x12,ControllerState::R1},                  // Q E
        {0x2d,ControllerState::L3},                                             // X (R3: middle mouse button)
        {0x148,ControllerState::Up},{0x150,ControllerState::Down},              // arrows
        {0x14b,ControllerState::Left},{0x14d,ControllerState::Right},
        {0x1c,ControllerState::Options},{0x0e,ControllerState::Share},          // Enter Backspace
        {0x0f,ControllerState::Touchpad},{0x147,ControllerState::PS}};          // Tab Home
public:
    // Mouse speed, in counts per millisecond, that pushes the right stick fully over at sensitivity 1.
    static constexpr double kFullSpeed=8.0;
    // The stick starts here as soon as the mouse moves, to get past the game's own deadzone.
    // shortcut: one anti-deadzone and a linear curve for every game; make them per-game
    // settings when a title needs it (a larger game deadzone swallows slow movements).
    static constexpr double kDeadzone=0.15;

    void setSensitivity(double value){sensitivity_=std::clamp(value,0.1,10.0);}
    void key(std::uint32_t scanCode,bool extended,bool down){
        const std::uint32_t code=(scanCode&0xffu)|(extended?0x100u:0u);
        for(const auto& k:kKeys)if(k.code==code){set(k.bit,down);return;}
    }
    void mouseButton(int button,bool down){ // 1 left, 2 middle, 3 right
        if(button==1)set(TriggerR,down);else if(button==3)set(TriggerL,down);else if(button==2)set(ControllerState::R3,down);
    }
    void mouseMove(int dx,int dy){dx_+=dx;dy_+=dy;}
    // Nothing held, mouse at rest. The sensitivity is a setting and stays.
    void reset(){held_=0;dx_=dy_=0;vx_=vy_=0;idle_=0;last_=0;}

    // Adds the keyboard and mouse to what the pad reports (there may be no pad at all).
    // Called once per pad sample; the mouse movement since the previous call becomes
    // the right stick: direction of the movement, deflection from its speed.
    void apply(ControllerState& s,HostTime now){
        s.inputActive=true;
        s.buttons|=held_&0xffffu;
        if(held_&TriggerL)s.l2=255;
        if(held_&TriggerR)s.r2=255;
        const int x=((held_&StickRight)?1:0)-((held_&StickLeft)?1:0),y=((held_&StickDown)?1:0)-((held_&StickUp)?1:0);
        if(x||y){const double full=32767.0/std::hypot(x,y);s.leftX=std::int16_t(x*full);s.leftY=std::int16_t(y*full);} // a diagonal is not faster

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
    void set(std::uint32_t bit,bool down){if(down)held_|=bit;else held_&=~bit;}
    std::uint32_t held_=0;
    long long dx_=0,dy_=0;   // mouse counts since the last sample
    double vx_=0,vy_=0;      // smoothed speed, counts per millisecond
    int idle_=0;             // samples in a row without mouse movement
    HostTime last_=0;
    double sensitivity_=1.0;
};
} // namespace veyra::remoteplay
