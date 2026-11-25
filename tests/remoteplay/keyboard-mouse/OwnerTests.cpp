#include "TestConsole.h"
#include "veyra/Log.h"
#include "veyra/remoteplay/KeyboardMouse.h"
#include "veyra/source/RemotePlaySessionSource.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using veyra::remoteplay::test::console;
using namespace veyra::remoteplay;
using veyra::source::RemotePlaySessionSource;

namespace {


void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void connect(RemotePlaySessionSource& source, bool captureCard, bool viewOnly = false) {
    require(console.connect(source, captureCard, viewOnly), "failed to connect requested video route");
}

void testFeedbackConnection(bool captureCard) {
    RemotePlaySessionSource source;
    connect(source, captureCard);
    require(console.feedback && console.feedback == source.keyboardMouse(),
            "Chiaki did not receive the input owner's feedback connection");
}

void testViewOnly(bool captureCard) {
    RemotePlaySessionSource source;
    connect(source, captureCard, true);
    require(!source.keyboardMouse() && !console.feedback, "view-only exposed a capture session");
    ControllerState pad;
    pad.inputActive = true;
    pad.buttons = ControllerState::PS;
    source.controller(pad);
    std::this_thread::sleep_for(40ms);
    source.close();
    require(console.sent.empty(), "view-only submitted controller state during polling or close");
}

auto walking = [](const ControllerState& state){return state.leftY < 0 && !state.r2;};
auto driving = [](const ControllerState& state){return !state.leftY && state.r2 == 255;};
auto neutral = [](const ControllerState& state){return state == ControllerState{};};

void testHandshake(bool captureCard, const std::string& profile) {
    RemotePlaySessionSource source;
    require(console.connect(source, captureCard, false, true), "could not start handshake");
    auto events = *console.events;
    auto pending = console.feedback;
    require(source.sessionSnapshot().state == SessionState::Connecting && !source.keyboardMouse(),
            "capture was available during handshake");
    require(!pending->start(profile, 1600, "on_foot").empty(), "pending mapper accepted input");
    events.loginPinRequired();
    source.loginPin("0000");
    require(source.sessionSnapshot().state == SessionState::LoginPinRequired && !source.keyboardMouse(),
            "login PIN exposed capture before console acceptance");
    ControllerState pad;
    pad.inputActive = true;
    pad.buttons = ControllerState::PS;
    source.controller(pad);
    require(console.mark() == 0, "sender ran before the console connected");
    console.connected = true;
    events.connected();
    require(source.keyboardMouse() == pending, "connected event did not publish the input handle");

    // Keep the owner inside submit so the disconnect check cannot rely on its next poll.
    console.stallSender();
    console.connected = false;
    events.failed(2004);
    require(!source.keyboardMouse() && !pending->start(profile, 1600, "on_foot").empty(),
            "disconnected input handle still accepted capture");
    console.resumeSender();
    source.close();

    require(console.connect(source, captureCard, false, true), "could not start a replacement connection");
    events.connected();
    require(!source.keyboardMouse(), "late connected event published a replacement mapper");
    console.connected = true;
    console.events->connected();
    require(source.keyboardMouse() && source.keyboardMouse() != pending &&
            !pending->start(profile, 1600, "on_foot").empty(),
            "replacement connection revived a stale input handle");
}

void testPausedReplacement(bool captureCard) {
    RemotePlaySessionSource source;
    PhysicalControllerGate gate;
    ControllerState pad;
    pad.inputActive = true;
    pad.buttons = ControllerState::Cross;
    const auto cross = [&](const auto& state) { return state == pad; };
    connect(source, captureCard);
    size_t cursor = console.mark();
    gate.submit(source.keyboardMouse(), pad);
    console.expect(cursor, cross, "initial physical input did not reach console");
    gate.setPaused(true, source.keyboardMouse());
    console.expect(cursor, neutral, "editor did not release the physical controller");
    source.close();

    require(console.connect(source, !captureCard, false, true), "could not replace paused connection");
    gate.submit(source.keyboardMouse(), pad);
    console.connected = true;
    console.events->connected();
    console.stallSender();
    cursor = console.mark();
    gate.submit(source.keyboardMouse(), pad);
    console.resumeSender();
    require(neutral(console.expect(cursor, [](const auto&) { return true; }, "replacement sender was idle")),
            "physical input reached the replacement connection during editing");
    gate.setPaused(false, source.keyboardMouse());
    gate.submit(source.keyboardMouse(), pad);
    console.expect(cursor, cross, "leaving the editor did not restore physical input");
}

void testAutomaticRetry(const std::string& profile) {
    RemotePlaySessionSource source;
    require(console.connect(source, true, false, true), "could not start automatic retry fixture");
    auto firstEvents = *console.events;
    console.connected = true;
    firstEvents.connected();
    auto firstInput = source.keyboardMouse();
    require(bool(firstInput), "first control connection was unavailable");
    PhysicalControllerGate gate;
    gate.setPaused(true, firstInput);
    console.videoCallbacks = 1; // A keepalive enables recovery on this control-only route.
    size_t cursor = console.mark();
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!source.rates().received && std::chrono::steady_clock::now() < deadline)
        console.expect(cursor, neutral, "owner stopped during the keepalive");
    require(source.rates().received == 1, "owner missed keepalive before outage");
    console.connected = false;
    firstEvents.failed(2004);
    {
        std::unique_lock lock(console.mutex);
        require(console.changed.wait_for(lock, 4s, [&] { return console.feedback != firstInput; }),
                "owner did not reconnect after the outage");
    }
    firstEvents.connected();
    require(!source.keyboardMouse() && !firstInput->start(profile, 1600, "on_foot").empty(),
            "old attempt re-enabled capture during retry handshake");
    console.events->loginPinRequired();
    require(!source.keyboardMouse(), "retry PIN exposed capture");
    console.connected = true;
    console.events->connected();
    console.stallSender();
    auto replacement = source.keyboardMouse();
    require(replacement && replacement != firstInput, "connected retry did not publish its own mapper");
    ControllerState pad;
    pad.inputActive = true;
    pad.buttons = ControllerState::PS;
    gate.submit(replacement, pad);
    cursor = console.mark();
    console.resumeSender();
    require(neutral(console.expect(cursor, [](const auto&) { return true; }, "retry owner stopped")),
            "physical input escaped during automatic retry");
    gate.setPaused(false, replacement);
    gate.submit(replacement, pad);
    console.expect(cursor, [&](const auto& state) { return state == pad; }, "physical pad failed after retry");
}

void testOwnership(bool captureCard, const std::string& profile) {
    RemotePlaySessionSource source;
    connect(source, captureCard);
    auto input = source.keyboardMouse();
    ControllerState pad;
    pad.inputActive = true;
    pad.buttons = ControllerState::PS;

    // Stop the loop at its actual submit call to exercise queued physical input.
    console.stallSender();
    auto cursor = console.mark();
    source.controller(pad);
    const auto error = input->start(profile,1600,"on_foot");
    input->key(0x11, true);
    console.resumeSender();
    require(error.empty(), "could not enable calibrated mapper");
    const size_t acquired = cursor;
    console.expect(cursor, walking, "W did not walk");
    {
        std::lock_guard lock(console.mutex);
        require(console.sent[acquired] == ControllerState{}, "queued pad state escaped capture ownership");
        for (size_t i = acquired; i < cursor; ++i)
            require(!(console.sent[i].buttons & ControllerState::PS), "pad took control during capture");
    }
    input->key(0x11, false);
    input->key(0x21, true);
    input->key(0x21, false);
    console.expect(cursor, [](auto& s){return s.buttons & ControllerState::Triangle;}, "missing tap down");
    console.expect(cursor, [](auto& s){return !(s.buttons & ControllerState::Triangle);}, "missing tap up");
    console.stallSender();
    input->motion(120,80);
    std::this_thread::sleep_for(40ms);
    console.resumeSender();
    console.expect(cursor, [](auto& s){return s.rightX > 0 && s.rightY > 0;}, "mouse motion lost an axis");
    input->button(1,true);
    console.expect(cursor, [](auto& s){return s.l2 == 255;}, "aim did not hold L2");
    source.controller(pad);
    const auto quiet = cursor;
    std::this_thread::sleep_for(150ms);
    {
        std::lock_guard lock(console.mutex);
        require(console.sent.size() > quiet, "sender stopped on quiet input");
        for (size_t i = quiet; i < console.sent.size(); ++i)
            require(console.sent[i].l2 == 255 && !(console.sent[i].buttons & ControllerState::PS),
                    "held aim lost ownership on quiet ticks");
    }

    // Focus loss releases capture, even with a background SDL pad still polling.
    console.stallSender();
    cursor = console.mark();
    input->stop();
    source.controller(pad);
    // A busy decoder can miss the initial neutral for longer than the normal
    // 100 ms stale-state timeout while the background controller stays fresh.
    std::this_thread::sleep_for(130ms);
    source.controller(pad);
    console.resumeSender();
    const auto handoff = console.expect(cursor, [](auto&){return true;}, "owner did not resume");
    require(neutral(handoff), "focus loss did not release controls before physical input");
    console.expect(cursor, [&](auto& s){return s == pad;}, "physical handoff failed");
    input->key(0x11, true);
    require(!input->active(), "foreground change restarted capture");
    PhysicalControllerGate physicalGate;
    physicalGate.setPaused(true, input);
    cursor = console.mark();
    physicalGate.submit(input, pad);
    console.expect(cursor, neutral, "settings did not pause physical input");
    std::this_thread::sleep_for(30ms);
    {
        std::lock_guard lock(console.mutex);
        for (size_t i = cursor; i < console.sent.size(); ++i)
            require(neutral(console.sent[i]), "editor submitted gameplay");
    }
    physicalGate.setPaused(false, input);
    require(input->start(profile,1600,"driving").empty(), "explicit resume failed");
    input->key(0x11,true);
    console.expect(cursor, driving, "resumed W did not operate throttle");
    source.close();
    require(neutral(console.sent.back()) && !input->active(), "session shutdown did not release throttle");
    require(!input->start(profile,1600,"on_foot").empty(), "retired session accepted capture");
}

using Time = KeyboardMouse::Clock::time_point;
using Trigger = std::array<uint8_t,11>;
constexpr Trigger idleTrigger{0x05};
constexpr Trigger bikeTrigger{0x21,0xff,0x03};

void tap(KeyboardMouse& input, uint16_t key, Time at) {
    input.key(key,true,at);
    input.key(key,false,at+60ms);
}

void testRecordedVehicle(bool captureCard, const std::string& profile) {
    RemotePlaySessionSource source;
    connect(source,captureCard);
    auto input = source.keyboardMouse();
    require(input->start(profile,1600,"on_foot").empty(), "cannot start vehicle replay");
    auto cursor = console.mark();
    // Original timestamps from Stickmouse Mapper's recorded_vehicles.rs. These
    // are relative to 05:04:00 UTC in the recorded bicycle/failed-entry run.
    const auto origin = KeyboardMouse::Clock::now()+40ms-5585ms;
    auto& feedback = *console.feedback;
    feedback.triggerPair(idleTrigger,idleTrigger,origin+5585ms);
    tap(*input,0x21,origin+10563ms);
    feedback.triggerPair(idleTrigger,bikeTrigger,origin+13028ms);
    tap(*input,0x11,origin+14500ms);
    tap(*input,0x21,origin+15301ms);
    feedback.triggerPair(idleTrigger,idleTrigger,origin+15495ms);
    tap(*input,0x11,origin+16751ms);  // Verify W again after the recorded exit.
    tap(*input,0x21,origin+19744ms);
    tap(*input,0x11,origin+29800ms);
    console.expect(cursor, driving, "recorded entry did not put W on the throttle", 12s);
    require(input->situation()==2,"recorded entry did not select Driving");
    console.expect(cursor, walking, "recorded exit did not restore walking", 5s);
    require(input->situation()==0,"recorded exit did not select On foot");
    console.expect(cursor, [](auto& s){return s.buttons & ControllerState::Triangle;},
                   "failed-entry tap was not sent", 5s);
    console.expect(cursor, walking, "failed entry selected a vehicle binding", 12s);
    require(input->situation()==0,"failed entry changed the active situation");
}

void testRemapAndManualCorrection(bool captureCard, const std::string& profile) {
    auto json = QJsonDocument::fromJson(QByteArray::fromStdString(profile)).object();
    auto bindings = json["bindings"].toObject();
    bindings.insert("KeyH",bindings.take("KeyF"));
    json["bindings"] = bindings;
    RemotePlaySessionSource source;
    connect(source,captureCard);
    auto input = source.keyboardMouse();
    require(input->start(QJsonDocument(json).toJson().toStdString(),1600,"on_foot").empty(),
            "remapped profile failed to load");
    auto cursor = console.mark();
    const auto t = KeyboardMouse::Clock::now()+40ms;
    auto& feedback = *console.feedback;
    feedback.triggerPair(idleTrigger,idleTrigger,t);
    // A short entry-only signature cannot select Driving through the passive rule.
    tap(*input,0x21,t+20ms); // F no longer owns Enter vehicle.
    feedback.triggerPair(bikeTrigger,bikeTrigger,t+140ms);
    feedback.triggerPair(idleTrigger,idleTrigger,t+220ms);
    tap(*input,0x11,t+300ms);
    console.expect(cursor, walking, "former Enter vehicle binding still armed recognition");
    tap(*input,0x23,t+400ms); // H now owns the same action.
    feedback.triggerPair(bikeTrigger,bikeTrigger,t+540ms);
    tap(*input,0x11,t+650ms);
    console.expect(cursor, driving, "remapped action did not arm vehicle recognition");
    require(input->situation()==2, "remapped entry did not select Driving");

    cursor = console.mark();
    const auto pending = KeyboardMouse::Clock::now()+50ms;
    tap(*input,0x21,pending); // The Driving override still binds F to Exit vehicle.
    console.expect(cursor, [](auto& s){return s.buttons & ControllerState::Triangle;}, "exit action was not armed");
    require(input->select("driving"), "manual correction was rejected");
    feedback.triggerPair(idleTrigger,idleTrigger,pending+120ms);
    std::this_thread::sleep_until(pending+1200ms);
    cursor = console.mark();
    tap(*input,0x11,KeyboardMouse::Clock::now());
    console.expect(cursor, driving, "stale evidence reversed the manual correction");
    require(input->situation()==2, "manual correction lost ownership");
    // A new exit action and fresh neutral feedback may still select On foot.
    tap(*input,0x21,pending+1450ms);
    feedback.triggerPair(idleTrigger,idleTrigger,pending+1540ms);
    tap(*input,0x11,pending+2800ms);
    console.expect(cursor, walking, "fresh exit evidence did not restore automatic selection",3s);
    require(input->situation()==0, "fresh exit did not select On foot");
}

void testRecordedHaptics(const QJsonObject& recording, bool captureCard, bool losePacket,
                         const std::string& profile) {
    RemotePlaySessionSource source;
    connect(source,captureCard);
    auto input = source.keyboardMouse();
    require(input->start(profile,1600,"on_foot").empty(), "cannot start haptic replay");
    auto cursor = console.mark();
    auto& feedback = *console.feedback;
    // The pinned plane excerpt starts 3000 ms before Enter vehicle. Pass the
    // original trigger pair and every recorded PCM peak on the same session clock.
    const auto t = KeyboardMouse::Clock::now()+3050ms;
    for (const auto& trigger : recording["triggers"].toArray()) {
        auto row = trigger.toArray();
        Trigger left{},right{};
        for (int i=0;i<11;++i) {
            left[i] = uint8_t(row[1].toArray()[i].toInt());
            right[i] = uint8_t(row[2].toArray()[i].toInt());
        }
        feedback.triggerPair(left,right,t+std::chrono::milliseconds(row[0].toInt()));
    }
    for (const auto& time : recording["taps"].toArray())
        tap(*input,0x21,t+std::chrono::milliseconds(time.toInt()));
    for (const auto& peak : recording["peaks"].toArray()) {
        auto row = peak.toArray();
        const std::array<int16_t,2> pcm{0, int16_t(-row[1].toInt())};
        feedback.haptics(reinterpret_cast<const uint8_t*>(pcm.data()),sizeof(pcm),
                         t+std::chrono::milliseconds(row[0].toInt()));
    }
    if (losePacket) {
        const uint8_t nextPcm[4]{};
        feedback.haptics(nextPcm,sizeof(nextPcm),t+2000ms,true);
    }
    std::this_thread::sleep_until(t+5000ms);
    cursor = console.mark();
    tap(*input,0x12,KeyboardMouse::Clock::now()); // E is D-pad Left in flight, Triangle on foot.
    if (losePacket) {
        console.expect(cursor, [](auto& s){return s.buttons & ControllerState::Triangle;},
                       "E did not retain the On foot binding after feedback loss");
        require(input->situation()==0,"partial haptic evidence selected Flying");
    } else {
        console.expect(cursor, [](auto& s){return s.buttons & ControllerState::Left;},
                       "recorded PCM peaks did not select the flight control");
        require(input->situation()==3,"recorded plane entry did not select Flying");
    }
}

void testOverflow(bool captureCard, const std::string& profile) {
    RemotePlaySessionSource source;
    connect(source,captureCard);
    console.stallSender();
    auto cursor = console.mark();
    ControllerState pad;
    pad.inputActive = true;
    for (int i=0;i<17;++i) {
        pad.buttons = i%2 ? ControllerState::Cross : ControllerState::Circle;
        source.controller(pad);
    }
    console.resumeSender();
    console.expect(cursor, neutral, "owner queue overflow omitted the neutral barrier");
    console.expect(cursor, [&](auto& s){return s == pad;}, "owner did not recover the latest physical state");
    auto input = source.keyboardMouse();
    require(input->start(profile,1600,"on_foot").empty(), "cannot run translator overflow check");
    input->button(1,true);
    console.expect(cursor, [](auto& s){return s.l2==255;}, "overflow fixture did not hold L2");
    // Queue more transitions than a worker tick can contain, all at one timestamp.
    const auto t = KeyboardMouse::Clock::now()+50ms;
    for (int i=0;i<100;++i) {
        input->key(0x21,true,t);
        input->key(0x21,false,t);
    }
    console.expect(cursor, neutral, "mapper overflow did not release the held trigger");
    input->key(0x11,true);
    console.expect(cursor, walking, "mapper did not recover after overflow");
}
}

int main(int argc, char** argv) try {
    const auto path = std::filesystem::absolute(argv[0]).parent_path() / "profiles/gta-vi-ps5-0.6.87-calibrated.v2.json";
    std::ifstream file(path);
    const std::string profile{std::istreambuf_iterator<char>(file),{}};
    require(!profile.empty(), "calibrated control profile is missing");
    const std::string test = argc > 1 ? argv[1] : "all";
    require(test=="all" || test=="ownership" || test=="vehicle" || test=="manual" || test=="haptics",
            "unknown owner test");
    for (bool captureCard : {false, true}) {
        if (test == "all" || test == "ownership") {
            testFeedbackConnection(captureCard);
            testHandshake(captureCard,profile);
            testPausedReplacement(captureCard);
            if (captureCard) testAutomaticRetry(profile);
            testViewOnly(captureCard);
            testOwnership(captureCard,profile);
            testOverflow(captureCard,profile);
        }
        if (test == "all" || test == "vehicle")
            testRecordedVehicle(captureCard,profile);
        if (test == "all" || test == "manual")
            testRemapAndManualCorrection(captureCard,profile);
        if (test == "all" || test == "haptics") {
            std::ifstream rounds(path.parent_path().parent_path()/"rounds.json");
            const std::string bytes{std::istreambuf_iterator<char>(rounds),{}};
            const auto plane = QJsonDocument::fromJson(QByteArray::fromStdString(bytes)).object()["plane"].toObject();
            require(!plane.isEmpty(), "missing pinned plane recording");
            for (bool losePacket : {false,true})
                testRecordedHaptics(plane,captureCard,losePacket,profile);
        }
        std::cout << (captureCard ? "Capture-card video" : "Remote Play video") << " passed offline " << test << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
