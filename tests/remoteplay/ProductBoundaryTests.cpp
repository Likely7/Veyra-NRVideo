#include "veyra/source/RemotePlaySessionSource.h"
#include "veyra/engine/DeadlineWait.h"
#include "veyra/remoteplay/ControllerInput.h"
#include "veyra/remoteplay/Discovery.h"
#include "veyra/remoteplay/KeyboardMousePad.h"
#include <array>
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>
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
static bool keyboardMouseMapping() {
    using Pad = remoteplay::KeyboardMousePad;
    using State = remoteplay::ControllerState;
    Pad pad;
    if (!pad.digital('W', true) || pad.state().leftY != -32767) return false;
    if (pad.digital('W', true) || !pad.digital('D', true)) return false;
    if (pad.state().leftX != 23170 || pad.state().leftY != -23170) return false;
    pad.digital('S', true);
    if (pad.state().leftY != 0) return false;
    pad.reset();
    pad.setBinding(Pad::Cross, Pad::encode('J', Pad::Ctrl));
    pad.setBinding(Pad::Circle, 'J');
    pad.digital(0xA2, true);
    pad.digital('J', true);
    if (pad.state().buttons != State::Cross) return false;
    pad.digital(0xA2, false);
    if (pad.state().buttons || pad.digital('J', true)) return false;
    pad.digital('J', false);
    pad.digital('J', true);
    if (pad.state().buttons != State::Circle) return false;
    pad.setBinding(Pad::Square, 'J');
    if (pad.bindings()[Pad::Circle] || pad.setBinding(Pad::PS, 0x77))
        return false;
    pad.digital('J', true);
    if (pad.state().buttons != State::Square) return false;
    pad.reset();
    bool useMouse = false;
    pad.state(&useMouse);
    if (!useMouse) return false;
    auto tuning = pad.tuning();
    tuning.mouseLook = false;
    pad.setTuning(tuning);
    pad.state(&useMouse);
    if (useMouse) return false;
    pad.setTuning(Pad::defaultTuning());
    pad.setBinding(Pad::RightLeft, 'H');
    pad.digital('H', true);
    if (pad.state(&useMouse).rightX != -32767 || useMouse) return false;
    pad.setBinding(Pad::RightRight, 'K');
    pad.digital('K', true);
    pad.digital('H', true);
    if (pad.state(&useMouse).rightX != 0 || useMouse) return false;
    pad.resetBindings();
    if (pad.bindings() != Pad::Defaults || pad.state().rightX != 0) return false;
    // Use the production PS5 queue with a complete keyboard tap inside one poll.
    RemotePlaySessionSource source;
    source.started_ = true;
    pad.digital(0x20, true);
    source.controller(pad.state());
    pad.digital(0x20, false);
    source.controller(pad.state());
    const auto now = remoteplay::monotonic100ns();
    if (source.takeControllerLocked(now).buttons != State::Cross) return false;
    if (source.takeControllerLocked(now).buttons != 0) return false;
    pad.digital('V', true);
    source.controller(pad.state());
    pad.digital('V', false);
    source.controller(pad.state());
    const auto comboNow = remoteplay::monotonic100ns();
    if (source.takeControllerLocked(comboNow).buttons != (State::L3 | State::R3)) return false;
    if (source.takeControllerLocked(comboNow).buttons != 0) return false;
    source.controller({});
    return source.takeControllerLocked(comboNow) == State{};
}
static bool mouseStickPacing() {
    using Stick = remoteplay::MouseStick;
    using State = remoteplay::ControllerState;
    RemotePlaySessionSource source;
    source.started_ = true;
    State mapped;
    mapped.inputActive = true;
    auto lowPoll = std::make_shared<Stick>(Stick::Tuning{});
    Stick highPoll(Stick::Tuning{});
    const auto base = remoteplay::monotonic100ns() / 10;
    int16_t lowMin = 32767, lowMax = 0;
    unsigned samples = 0, mismatches = 0;
    // Real queue and sampler with a 4 ms consumer. UI heartbeats are uneven
    // and miss 40 ms, while relative input keeps a constant 1,000 counts/s.
    for (int ms = 0; ms <= 1000; ++ms) {
        const auto now = base + ms * 1000;
        highPoll.move(1, 0, now);
        if (ms % 8 == 1) lowPoll->move(8, 0, now);
        if ((ms % 18 == 0 || ms % 18 == 7) && (ms < 370 || ms > 410)) {
            source.controller(mapped, lowPoll);
            source.controllerStamp_ = now * 10;
            source.pendingControllers_.back().stamp = now * 10;
        }
        if (ms >= 32 && ms % 4 == 0) {
            const auto pad = source.takeControllerLocked(now * 10);
            const auto [x, y] = highPoll.sample(now);
            ++samples;
            if (!pad.inputActive || !x || pad.rightX != x || pad.rightY != y) ++mismatches;
            lowMin = std::min(lowMin, pad.rightX);
            lowMax = std::max(lowMax, pad.rightX);
        }
    }
    std::cout << "PS5_MOUSE_PACING samples=" << samples << " mismatches=" << mismatches
              << " stable_axis_min=" << lowMin << " stable_axis_max=" << lowMax << "\n";
    if (mismatches || lowMin != lowMax || lowMax >= 32767) return false;

    // Low-poll motion is held between reports, tapers to neutral after a
    // stop, and can restart immediately without reusing stale counts.
    const auto last = base + 993000;
    const auto held = source.takeControllerLocked((last + 9000) * 10).rightX;
    const auto fading = source.takeControllerLocked((last + 13000) * 10).rightX;
    const auto stopped = source.takeControllerLocked((last + 24000) * 10).rightX;
    const auto resumed = last + 33000;
    lowPoll->move(8, 0, resumed);
    const auto restart = source.takeControllerLocked(resumed * 10).rightX;
    if (held != lowMax || fading <= 0 || fading >= held || stopped || restart != held) return false;

    const auto send = [&](const State& state, std::weak_ptr<Stick> mouse, int64_t nowUs) {
        source.controller(state, std::move(mouse));
        source.controllerStamp_ = nowUs * 10;
        source.pendingControllers_.back().stamp = nowUs * 10;
        return source.takeControllerLocked(nowUs * 10);
    };
    auto keys = mapped;
    keys.rightX = -32767;
    if (send(keys, {}, resumed + 1000) != keys) return false;
    if (send(mapped, lowPoll, resumed + 2000).rightX != held) return false;
    if (send({}, lowPoll, resumed + 3000) != State{}) return false;
    if (send(mapped, lowPoll, resumed + 4000).rightX != held) return false;
    lowPoll.reset();
    if (source.takeControllerLocked((resumed + 5000) * 10) != mapped) return false;
    if (source.takeControllerLocked((resumed + 106000) * 10) != State{}) return false;

    Stick batched(Stick::Tuning{}), unbatched(Stick::Tuning{});
    batched.move(4, 2, base);
    batched.move(4, 2, base);
    batched.move(500, 500, base - 1000); // Out-of-order input must not be replayed.
    unbatched.move(8, 4, base);
    if (batched.sample(base) != unbatched.sample(base)) return false;
    Stick::Tuning tuning;
    tuning.invertY = true;
    tuning.horizontal = 2.0;
    tuning.smoothingMs = 16;
    batched.setTuning(tuning);
    if (batched.sample(base) != std::pair<int16_t, int16_t>{}) return false;
    batched.move(12, 12, base);
    const auto first = batched.sample(base);
    batched.move(12, 12, base + 8000);
    const auto second = batched.sample(base + 8000);
    if (first.first <= 0 || first.second >= 0 || second.first <= first.first ||
        second.second >= first.second || std::abs(second.first + 2 * second.second) > 1) return false;
    if (batched.sample(base + 32000) != std::pair<int16_t, int16_t>{}) return false;
    return true;
}
static bool calibratedMouse(){
    using Pad=remoteplay::KeyboardMousePad;using Stick=remoteplay::MouseStick;
    Stick::Tuning t;t.deadzone=0;t.turnSpeed=4000;Stick high(t);const int64_t origin=1000000;
    for(int i=0;i<=8;++i){
        high.move(1,0,origin+i*1000);
        if((i==4||i==8)&&std::abs(high.sample(origin+i*1000).first/32767.-.25)>.0375)return false;
    }
    if(high.sample(origin+16000).first!=0)return false;
    Stick slow(t);
    for(int i=0;i<6;++i){slow.move(1,0,origin+i*28571);if(i&&slow.sample(origin+i*28571).first>330)return false;}
    const auto flick=[&](int period){
        Stick stick(t);double sum=0;
        for(int i=0;i<16;++i){if(i%period==0)stick.move(period,0,origin+i*1000);
            sum+=stick.sample(origin+i*1000).first;}
        return sum;
    };
    if(std::abs(flick(8)/flick(1)-1)>.2)return false;
    auto t2=t;t2.dpi=1600;Stick low(t),highDpi(t2);
    for(int i=0;i<10;++i){low.move(1,0,origin+i*1000);highDpi.move(2,0,origin+i*1000);}
    if(low.sample(origin+9000)!=highDpi.sample(origin+9000))return false;
    t.deadzone=.18;t.deadzoneShape=1;t.adsScale=.5;Stick shape(t);
    shape.move(10,1,origin);auto diagonal=shape.sample(origin);
    if(diagonal.second<.18*32767)return false;
    remoteplay::ControllerState hip,ads;ads.l2=255;shape.apply(hip,origin);shape.apply(ads,origin);
    if(ads.rightX>=hip.rightX)return false;
    Pad pad;pad.setBinding(Pad::Circle,Pad::encode('J',Pad::Ctrl));
    pad.digital(0xa0,true);pad.digital(0xa2,true);pad.digital('J',true);
    if(!(pad.state().buttons&remoteplay::ControllerState::Circle))return false;
    pad.setBinding(Pad::LeftSlow,'B');pad.digital('W',true);pad.digital('B',true);
    if(std::abs(pad.state().leftY+32767*.45)>1)return false;
    // ADS delay is measured by the send clock while a held button stays active.
    t={};t.deadzone=0;t.turnSpeed=4000;t.adsScale=.5;t.adsDelayMs=8;
    Stick delayed(t);
    remoteplay::ControllerState scoped;scoped.l2=255;
    for(int i=0;i<12;++i){
        delayed.move(1,0,origin+i*1000);
        delayed.apply(scoped,origin+i*1000);
        if(i==4 && std::abs(scoped.rightX/32767.-.25)>.001)return false;
        if(i==10 && std::abs(scoped.rightX/32767.-.125)>.001)return false;
    }
    t.smoothingMs=12;
    Stick reverse(t);
    for(int i=0;i<92;++i){
        reverse.move(i<60?2:-2,0,origin+i*1000);
        const auto x=reverse.sample(origin+i*1000).first;
        if((i==59&&x<=0)||(i==84&&x>=0))return false;
    }
    Pad gestures;
    gestures.setToggleMask(uint64_t{1}<<Pad::L2);
    gestures.digital(Pad::MouseRight,true);gestures.digital(Pad::MouseRight,false);
    if(!gestures.state().l2)return false;
    gestures.digital(Pad::MouseRight,true);gestures.digital(Pad::MouseRight,false);
    if(gestures.state().l2)return false;
    for(int dir=Pad::SwipeUp;dir<=Pad::SwipeRight;++dir){
        gestures.setBinding(dir,'G');gestures.digital('G',true);
        const auto first=gestures.state().touches[0];
        if(first.id<0 || (dir==Pad::SwipeUp && first.y<=464) || (dir==Pad::SwipeDown && first.y>=464)
            || (dir==Pad::SwipeLeft && first.x<=960) || (dir==Pad::SwipeRight && first.x>=960))return false;
        gestures.digital('G',false);
    }
    if(!Pad::validBinding(27,true)||Pad::validBinding(27))return false;
    t={};t.gyro=2;Stick gyro(t);remoteplay::ControllerState mousePad;
    gyro.move(5,0,origin);gyro.apply(mousePad,origin);
    if(mousePad.motionValid||mousePad.rightX<=0)return false;
    mousePad={};mousePad.l2=255;gyro.apply(mousePad,origin+1000);
    if(!mousePad.motionValid||mousePad.gyro[1]>=0||mousePad.rightX)return false;
    mousePad={};gyro.controlContext(true,false);gyro.apply(mousePad,origin+2000);
    if(mousePad.motionValid||mousePad.leftX<=0||mousePad.rightX)return false;
    t={};t.deadzone=0;t.carry=true;Stick carry(t);
    for(int i=0;i<70;++i){carry.move(10,0,origin+i*1000);carry.sample(origin+i*1000);}
    for(int i=70;i<145;++i){
        const auto x=carry.sample(origin+i*1000).first;
        if((i==77&&x==0)||(i==144&&x!=0))return false;
    }
    Pad driving;
    constexpr auto toggle = 0x75;
    driving.setToggleMask(uint64_t{1}<<Pad::L2);
    driving.digital('W', true);
    if (driving.state().leftY != -32767 || !driving.digital(toggle, true)) return false;
    if (!driving.driving() || driving.state().leftY || driving.state().r2) return false;
    if (driving.digital('W', true)) return false;
    driving.digital('W', false); driving.digital('W', true); driving.digital('S', true);
    if (!driving.state().r2 || !driving.state().l2 || (driving.state().buttons & remoteplay::ControllerState::R1)) return false;
    driving.digital('S', false);
    if (driving.state().l2) return false;
    driving.digital('S', true);
    driving.digital('D', true);
    driving.state(nullptr, origin);
    const auto steering = driving.state(nullptr, origin + 40000).leftX;
    if (steering < 15000 || steering > 18000) return false;
    driving.digital(toggle, false); driving.digital(toggle, true);
    if (driving.driving() || driving.state().r2 || driving.state().leftX || driving.state().buttons) return false;
    if (driving.setDriveKey('A') || driving.setDriveKey('V') || !driving.setDriveKey('K')) return false;
    if (!driving.setBinding(Pad::R1, 'N', true) || driving.bindings()[Pad::R1] != 'Q') return false;

    t={};
    Stick fine(t); int16_t minAxis=32767,maxAxis=0;
    for(int i=0;i<160;++i){
        if(i%10==0)fine.move(1,0,origin+i*1000);
        const auto axis=fine.sample(origin+i*1000).first;
        if(i>=30){minAxis=std::min(minAxis,axis);maxAxis=std::max(maxAxis,axis);}
    }
    if(minAxis<=0 || maxAxis-minAxis>2 || fine.sample(origin+190000).first) return false;
    Stick lowPollFine(t);
    const int pulseTimes[] = {8,16,24,40,48,56,64,80,88,96,104,120,128,136,144,160};
    for(int i=0,pulse=0;i<=160;++i){
        if(i==pulseTimes[pulse]){ lowPollFine.move(1,0,origin+i*1000);++pulse; }
        if(i>40 && !lowPollFine.sample(origin+i*1000).first)return false;
    }
    t.deadzone=0;Stick dpi800(t);t.dpi=1600;Stick dpi1600(t);
    for(int i=0;i<10;++i){dpi800.move(1,0,origin+i*1000);dpi1600.move(1,0,origin+i*1000);}
    const auto a800=dpi800.sample(origin+9000).first,a1600=dpi1600.sample(origin+9000).first;
    if(a1600<=0||std::abs(a800-2*a1600)>2)return false;
    using State = remoteplay::ControllerState;
    Pad defaults;
    if (defaults.bindings()[Pad::Cross] != 32 || defaults.bindings()[Pad::Circle] != 0xA2 ||
        defaults.bindings()[Pad::Square] != 'R' || defaults.bindings()[Pad::Touchpad] != 'M' ||
        defaults.bindings()[Pad::Options] != 27 || defaults.tuning().unlock != 3 ||
        defaults.tuning().dpi != 2449) return false;

    constexpr auto stickClicks = State::L3 | State::R3;
    for (const bool vehicle : {false, true}) {
        defaults.selectDriving(vehicle);
        if (defaults.bindings(vehicle)[Pad::StickClickBoth] != 'V' ||
            !defaults.setBinding(Pad::StickClickBoth, Pad::encode('J', Pad::Alt), vehicle, 1)) return false;
        defaults.digital('V', true);
        defaults.digital(0xA4, true);
        defaults.digital('J', true);
        if (defaults.state().buttons != stickClicks) return false;
        defaults.digital('V', false);
        if (defaults.state().buttons != stickClicks) return false;
        defaults.digital(0xA4, false);
        if (defaults.state().buttons != 0) return false;
        defaults.digital('J', false);
        if (!defaults.setBinding(Pad::StickClickBoth, 0, vehicle, 1) ||
            defaults.bindings(vehicle)[Pad::StickClickBoth] != 'V') return false;
    }
    defaults.digital(0xA0, true); // Independent L3 holds during the combo.
    defaults.digital('V', true);
    if (defaults.state().buttons != stickClicks) return false;
    defaults.digital('V', false);
    if (defaults.state().buttons != State::L3) return false;
    defaults.selectDriving(false);
    if (!defaults.setBinding(Pad::Circle, 'J', false, 1) || defaults.setDriveKey('J')) return false;
    defaults.setBinding(Pad::Cross, 'J');
    if (defaults.bindings(false, 1)[Pad::Circle] || defaults.bindings()[Pad::Cross] != 'J') return false;
    defaults.setBinding(Pad::Circle, 'J', false, 1);
    if (defaults.bindings()[Pad::Cross] || defaults.bindings(false, 1)[Pad::Circle] != 'J') return false;

    defaults.setToggleMask(uint64_t{1} << Pad::Circle);
    defaults.digital(0xA2, true);
    defaults.digital('J', true);
    if (defaults.state().buttons != State::Circle) return false;
    defaults.digital(0xA2, false);
    defaults.digital('J', false);
    defaults.digital('J', true);
    if (defaults.state().buttons != 0) return false;
    std::cout<<"PS5_DEFAULT_KEYS L3+R3, two slots, chords, conflicts and held aliases pass\n";
    std::cout<<"PS5_DRIVING_LAYOUT independent, W/R2 S/L2, steering ramp, held keys neutral, 100 counts/s min/max="
             <<minAxis<<'/'<<maxAxis<<" DPI raw equal axes="<<a800<<'/'<<a1600<<"\n";
    std::cout<<"PS5_GESTURES ADS delay, smoothed reversal, toggle, four swipe origins, Escape, gyro, menu and bounded carry pass\n";
    std::cout<<"PS5_CALIBRATION start4/8ms, stop8ms, 35counts/s, flick125/1000, DPI, square, ADS, modifiers and walk pass\n";
    return true;
}
static bool mouseTurnCurve() {
    using Stick = remoteplay::MouseStick;
    const auto axis = [](Stick::Tuning tuning, int countsPerReport) {
        Stick stick(tuning);
        for (int tick = 1; tick <= 12; ++tick) stick.move(countsPerReport, 0, tick * 8000);
        return stick.sample(96000).first / 32767.0;
    };
    Stick::Tuning defaults;
    const double aim = axis(defaults, 1);
    const double turn = axis(defaults, 8);
    if (std::abs(aim-(.08+.92*125/2400.))>.001 || std::abs(turn-(.08+.92*1000/2400.))>.001 || axis(defaults,20)!=1)return false;

    auto field = defaults;
    field.horizontal = field.vertical = 6;
    field.curve = 2.5;
    field.deadzone = 0;
    if (axis(field, 8) != 1 || std::abs(axis(field,1)-.45*std::pow((125.*6/2400.)/.45,2.5))>.001) return false;
    std::cout << "PS5_MOUSE_TURN aim_125=" << aim << " turn_1000=" << turn
              << " turn_1500=" << axis(defaults, 12) << " saved_profile_turn_1000=" << axis(field, 8) << "\n";

    auto adjusted = defaults;
    adjusted.turnBoost = 3.6;
    if (axis(adjusted, 1) != aim || axis(adjusted, 8) <= turn) return false;
    adjusted.inputDeadzone = 0.1;
    if (axis(adjusted, 1) != 0 || axis(adjusted, 2) <= 0) return false;

    // Curve transitions must never turn slower when the mouse moves faster.
    for (const auto gain : {0.1, 1.0, 6.0}) {
        for (const auto curve : {0.5, 1.0, 2.5}) {
            for (const auto boost : {1.0, 3.6, 6.0}) {
                auto tuning = defaults;
                tuning.horizontal = gain;
                tuning.curve = curve;
                tuning.turnBoost = boost;
                double previous = 0;
                for (int count = 1; count <= 64; ++count) {
                    const double value = axis(tuning, count);
                    if (value < previous || value > 1) return false;
                    previous = value;
                }
            }
        }
    }
    Stick diagonal(defaults);
    diagonal.move(24, -24, 8000);
    const auto [x, y] = diagonal.sample(8000);
    return x > 0 && x == -y && std::hypot(x, y) <= 32768;
}
static bool slowConsumer(){
    RemotePlaySessionSource s;s.started_=true;
    const auto base=remoteplay::monotonic100ns();
    unsigned neutral=0,lagged=0;size_t highWater=0;
    // UI at 125 Hz; decoder/controller owner at 62.5 Hz, matching the field
    // backlog. No real device, console, sleeping or altered expiry threshold.
    for(int tick=0;tick<625;++tick){
        const auto now=base+tick*80000LL;
        remoteplay::ControllerState held;held.inputActive=true;held.r2=255;held.rightX=int16_t(tick%100);
        s.controller(held);s.controllerStamp_=now;s.pendingControllers_.back().stamp=now;
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
    const auto sensor=s.pendingControllers_.back().state;
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
    RemotePlaySessionSource s(true);s.started_=true;
    const auto event=s.frameEvent();
    if(!event||WaitForSingleObject(event,0)!=WAIT_TIMEOUT)throw std::runtime_error("empty mailbox event");
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
    if(WaitForSingleObject(event,0)!=WAIT_OBJECT_0||WaitForSingleObject(event,0)!=WAIT_OBJECT_0)
        throw std::runtime_error("mailbox signal cleared while a frame is pending");
    const AVFrame* frame=nullptr;pipeline::FramePacket out;
    if(s.read(out,&frame)!=SourceReadStatus::Frame||!frame||out.sequence!=2||s.skipped()!=1||!pipeline::breaksHistory(out.flags))throw std::runtime_error("latest decoded mailbox");
    if(!pipeline::hasFrameFlag(out.flags,pipeline::FrameFlagBits::Drop)||pipeline::hasFrameFlag(out.flags,pipeline::FrameFlagBits::Discontinuity))throw std::runtime_error("decoded overwrite must be a soft Drop, not a clock discontinuity");
    if(s.read(out,&frame)!=SourceReadStatus::Waiting)throw std::runtime_error("mailbox replay");
    if(WaitForSingleObject(event,0)!=WAIT_TIMEOUT)throw std::runtime_error("consumed mailbox event");
    raw=av_frame_alloc();raw->width=16;raw->height=16;raw->format=AV_PIX_FMT_YUV420P;
    if(av_frame_get_buffer(raw,32)<0)throw std::bad_alloc();
    packet.sequence=1;packet.sourceEpoch=uint64_t(1)<<32;
    packet.flags=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
    bool woken=false;
    std::jthread reader([&]{
        engine::DeadlineWait waiter;
        const auto limit=engine::DeadlineWait::now100ns()+10000000;
        while(!woken&&engine::DeadlineWait::now100ns()<limit)woken=waiter.waitForIdleSource(event);
    });
    s.publishDecoded(raw,packet,info);packet.sequence=2;packet.flags=0;s.publishDecoded(raw,packet,info);
    reader.join();
    if(!woken)throw std::runtime_error("real mailbox waiter did not wake on republish");
    if(s.read(out,&frame)!=SourceReadStatus::Frame||out.sequence!=4||out.sourceEpoch!=(uint64_t(1)<<32)||!pipeline::hasFrameFlag(out.flags,pipeline::FrameFlagBits::Discontinuity))throw std::runtime_error("reconnect keeps application sequence and pending clock reset across mailbox overwrite");
    s.publishDecoded(raw,packet,info);av_frame_free(&raw);
    if(WaitForSingleObject(event,0)!=WAIT_OBJECT_0)throw std::runtime_error("pending mailbox event before close");
    s.close();
    if(WaitForSingleObject(event,0)!=WAIT_TIMEOUT)throw std::runtime_error("closed mailbox event");
}

static bool compareFrameWake() {
    using Wait = engine::DeadlineWait;
    constexpr uint64_t frames = 180;
    const auto measure = [&](bool useEvent) {
        RemotePlaySessionSource source(useEvent);
        source.started_ = true;
        if (useEvent && !source.frameEvent()) throw std::runtime_error("mailbox event unavailable");
        const auto freeFrame = [](AVFrame* frame) { av_frame_free(&frame); };
        std::unique_ptr<AVFrame, decltype(freeFrame)> raw(av_frame_alloc(), freeFrame);
        if (!raw) throw std::bad_alloc();
        raw->width = 16;
        raw->height = 16;
        raw->format = AV_PIX_FMT_YUV420P;
        if (av_frame_get_buffer(raw.get(), 32) < 0) throw std::bad_alloc();

        Wait waiter;
        std::unique_ptr<void, decltype(&CloseHandle)> producerTimer(
            CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS), &CloseHandle);
        if (!producerTimer) throw std::runtime_error("high resolution source pacing timer unavailable");
        const auto start = Wait::now100ns();
        ULONG64 cyclesBefore = 0, cyclesAfter = 0;
        if (!QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore)) throw std::runtime_error("cannot sample consumer CPU cycles");
        std::jthread producer([&] {
            constexpr std::array cadence{16667, 13334, 19999, 16667};
            SourceInfo info;
            pipeline::FramePacket packet;
            auto next=Wait::now100ns();
            for (uint64_t index = 0; index < frames; ++index) {
                next+=int64_t(cadence[index % cadence.size()])*10;
                LARGE_INTEGER due;
                due.QuadPart=-std::max<LONGLONG>(1,next-Wait::now100ns());
                if(!SetWaitableTimer(producerTimer.get(),&due,0,nullptr,nullptr,FALSE)||
                   WaitForSingleObject(producerTimer.get(),250)!=WAIT_OBJECT_0)return;
                packet.decodedHost100ns = Wait::now100ns();
                source.publishDecoded(raw.get(), packet, info);
            }
        });
        pipeline::FramePacket packet;
        std::vector<int64_t> latencies;
        latencies.reserve(frames);
        uint64_t polls = 0;
        // Compare the original 1 ms timer with the real PS5 idle-frame wait.
        while (packet.sequence < frames && Wait::now100ns() - start < 90000000) {
            if (source.read(packet, nullptr) == SourceReadStatus::Frame) {
                latencies.push_back(Wait::now100ns() - packet.decodedHost100ns);
                continue;
            }
            ++polls;
            if(useEvent)waiter.waitForIdleSource(source.frameEvent());
            else waiter.slice(1.0);
        }
        const double elapsedMs = double(Wait::now100ns() - start) / 10000.0;
        if (!QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter)) throw std::runtime_error("cannot sample consumer CPU cycles");
        producer.join();
        if (latencies.empty()) return false;
        std::ranges::sort(latencies);
        const auto percentileUs = [&](size_t percent) {
            return double(latencies[(latencies.size() * percent - 1) / 100]) / 10.0;
        };
        const uint64_t overwritten = source.skipped();
        std::cout << "FRAME_WAKE mode=" << (useEvent ? "event" : "poll") << " frames=" << latencies.size()
                  << " overwritten=" << overwritten << " elapsedMs=" << elapsedMs << " timerWaits=" << polls
                  << " cycles=" << cyclesAfter - cyclesBefore << " wakeP50Us=" << percentileUs(50)
                  << " wakeP95Us=" << percentileUs(95) << '\n';
        return packet.sequence == frames && latencies.size() + overwritten == frames;
    };
    bool ok = true;
    for (const bool event : {false, true, true, false}) ok &= measure(event);
    RemotePlaySessionSource idle(true);
    if(!idle.frameEvent())return false;
    Wait waiter;
    double largestIdleWaitMs=0;
    for(unsigned attempt=0;attempt<16;++attempt){
        // A stop without another frame is noticed on this bounded timer.
        const auto before=Wait::now100ns();
        ok&=!waiter.waitForIdleSource(idle.frameEvent());
        largestIdleWaitMs=std::max(largestIdleWaitMs,double(Wait::now100ns()-before)/10000.0);
    }
    std::cout<<"FRAME_WAKE_IDLE_NO_FRAME waits=16 maxMs="<<largestIdleWaitMs<<'\n';
    ok&=largestIdleWaitMs<30.0;
    return ok;
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
    if(s.frameEvent())throw std::runtime_error("controller-only session has no frame event");
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
    if(argc==2&&std::string_view(argv[1])=="--frame-wake")return veyra::source::RemotePlaySessionSourceTestAccess::compareFrameWake()?0:54;
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
    if(!veyra::source::RemotePlaySessionSourceTestAccess::keyboardMouseMapping()) {
        std::cerr << "PS5 keyboard/mouse mapping regression\n";
        return 45;
    }
    if(!veyra::source::RemotePlaySessionSourceTestAccess::mouseStickPacing()) {
        std::cerr << "PS5 mouse pacing regression\n";
        return 46;
    }
    if(!veyra::source::RemotePlaySessionSourceTestAccess::calibratedMouse())return 79;
    if(!veyra::source::RemotePlaySessionSourceTestAccess::mouseTurnCurve()) {
        std::cerr << "PS5 mouse turn response regression\n";
        return 47;
    }
    std::cout << "PS5_KEYBOARD_MAPPING chord order payload mouse-rate and release passed\n";
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
