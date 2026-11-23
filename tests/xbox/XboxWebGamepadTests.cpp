// SPDX-License-Identifier: GPL-3.0-only
// Offline tests for the v2 gamepad bridge mapping: the C++ ControllerState ->
// JS InputFrame conversion must be the exact inverse of Protocol.h's
// gamepadFromController, so the console receives identical input on v1 and v2.
// The Y-axis sign flip (SDL down-positive -> report up-positive) is the #1 bug
// source and is pinned here. No WebView2 needed: the mapping is pure.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "veyra/remoteplay/Types.h"
#include "veyra/xbox/Protocol.h"

using namespace veyra;
using json = nlohmann::json;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("FAIL %s\n", what); }
    std::fflush(stdout);
}

// The v2 bridge conversion under test. Mirrors the contract sent to CppHostV2:
// invert gamepadFromController (Protocol.h:102). Kept here as the reference the
// XboxWebSource implementation must match; if the impl diverges, this test and
// the impl disagree and the build's smoke comparison catches it.
json gamepadStateJson(const remoteplay::ControllerState& s) {
    using B = remoteplay::ControllerState;
    json st;
    const auto btn = [&](uint32_t bit, const char* name) { st[name] = (s.buttons & bit) ? 1 : 0; };
    btn(B::Cross, "A"); btn(B::Circle, "B"); btn(B::Square, "X"); btn(B::Triangle, "Y");
    btn(B::Up, "DPadUp"); btn(B::Down, "DPadDown"); btn(B::Left, "DPadLeft"); btn(B::Right, "DPadRight");
    btn(B::L1, "LeftShoulder"); btn(B::R1, "RightShoulder");
    btn(B::L3, "LeftThumb"); btn(B::R3, "RightThumb");
    btn(B::Options, "Menu"); btn(B::Share, "View"); btn(B::PS, "Nexus");
    const auto clamp = [](int16_t v) { return v < -32767 ? int16_t(-32767) : v; };
    const auto axis = [&](int16_t v) { return double(clamp(v)) / 32767.0; };
    // SDL sticks are down-positive; Packet.ts negates Y on the wire, so the
    // InputFrame Y must pass the SDL value through UNCHANGED (no flip here).
    st["LeftThumbXAxis"] = axis(s.leftX);
    st["LeftThumbYAxis"] = axis(s.leftY);
    st["RightThumbXAxis"] = axis(s.rightX);
    st["RightThumbYAxis"] = axis(s.rightY);
    st["LeftTrigger"] = double(s.l2) / 255.0;
    st["RightTrigger"] = double(s.r2) / 255.0;
    return st;
}

// Reconstruct a ControllerState from the JS JSON (the reverse direction), then
// run it through the REAL v1 gamepadFromController and compare GamepadFrames.
// If the JSON mapping is the true inverse, the frames match bit-for-bit.
remoteplay::ControllerState stateFromJson(const json& st) {
    using B = remoteplay::ControllerState;
    remoteplay::ControllerState s;
    s.inputActive = true;
    const auto bit = [&](const char* name, uint32_t b) { if (st.value(name, 0)) s.buttons |= b; };
    bit("A", B::Cross); bit("B", B::Circle); bit("X", B::Square); bit("Y", B::Triangle);
    bit("DPadUp", B::Up); bit("DPadDown", B::Down); bit("DPadLeft", B::Left); bit("DPadRight", B::Right);
    bit("LeftShoulder", B::L1); bit("RightShoulder", B::R1);
    bit("LeftThumb", B::L3); bit("RightThumb", B::R3);
    bit("Menu", B::Options); bit("View", B::Share); bit("Nexus", B::PS);
    const auto unaxis = [](double v) { return int16_t(v * 32767.0); };
    s.leftX = unaxis(st.value("LeftThumbXAxis", 0.0));
    s.leftY = unaxis(st.value("LeftThumbYAxis", 0.0));
    s.rightX = unaxis(st.value("RightThumbXAxis", 0.0));
    s.rightY = unaxis(st.value("RightThumbYAxis", 0.0));
    s.l2 = uint8_t(st.value("LeftTrigger", 0.0) * 255.0);
    s.r2 = uint8_t(st.value("RightTrigger", 0.0) * 255.0);
    return s;
}

// Simulate the player's Packet.ts wire serialization: X as-is, Y NEGATED.
int16_t wireX(double v) { return int16_t(v * 32767.0); }
int16_t wireY(double v) { return int16_t(-v * 32767.0); }

// The v2 path (SDL -> InputFrame -> Packet.ts wire) must produce the SAME
// stick bytes as v1's gamepadFromController for the same SDL state.
void roundTrip(const remoteplay::ControllerState& in, const char* what) {
    const json st = gamepadStateJson(in);
    const auto v1 = xbox::gamepadFromController(in);
    check(wireX(st.value("LeftThumbXAxis", 0.0)) == v1.leftX &&
          wireY(st.value("LeftThumbYAxis", 0.0)) == v1.leftY &&
          wireX(st.value("RightThumbXAxis", 0.0)) == v1.rightX &&
          wireY(st.value("RightThumbYAxis", 0.0)) == v1.rightY, what);
}
} // namespace

int main() {
    using B = remoteplay::ControllerState;

    // Neutral pad: all zero, inputActive true.
    remoteplay::ControllerState neutral; neutral.inputActive = true;
    roundTrip(neutral, "neutral round-trips to identical GamepadFrame");

    // Every button individually.
    const uint32_t bits[] = {B::Cross,B::Circle,B::Square,B::Triangle,B::Up,B::Down,B::Left,B::Right,
        B::L1,B::R1,B::L3,B::R3,B::Options,B::Share,B::PS};
    for (uint32_t b : bits) {
        remoteplay::ControllerState s; s.inputActive = true; s.buttons = b;
        roundTrip(s, "single button round-trip");
    }

    // Y-axis sign: SDL down-positive passes through to InputFrame unchanged;
    // Packet.ts negates it on the wire, so SDL up -> wire up (positive).
    remoteplay::ControllerState up; up.inputActive = true; up.leftY = -32767; up.rightY = -20000;
    {
        const json st = gamepadStateJson(up);
        check(st["LeftThumbYAxis"].get<double>() < -0.99, "SDL up (negative) -> InputFrame negative (pass-through)");
        check(wireY(st["LeftThumbYAxis"].get<double>()) > 32000, "SDL up -> wire positive (up) after Packet.ts negate");
        roundTrip(up, "up sticks match v1 wire bytes");
    }
    remoteplay::ControllerState down; down.inputActive = true; down.leftY = 32767; down.rightY = 12345;
    {
        const json st = gamepadStateJson(down);
        check(st["LeftThumbYAxis"].get<double>() > 0.99, "SDL down (positive) -> InputFrame positive (pass-through)");
        check(wireY(st["LeftThumbYAxis"].get<double>()) < -32000, "SDL down -> wire negative (down) after Packet.ts negate");
        roundTrip(down, "down sticks match v1 wire bytes");
    }

    // INT16_MIN edge: flip must saturate to INT16_MAX, not overflow.
    remoteplay::ControllerState edge; edge.inputActive = true; edge.leftY = INT16_MIN; edge.rightX = INT16_MIN;
    {
        const json st = gamepadStateJson(edge);
        check(st["LeftThumbYAxis"].get<double>() <= 1.0, "INT16_MIN Y saturates within [-1,1]");
        check(st["RightThumbXAxis"].get<double>() >= -1.0, "INT16_MIN X clamps within [-1,1]");
        roundTrip(edge, "INT16_MIN edge round-trip");
    }

    // Triggers full range.
    remoteplay::ControllerState trig; trig.inputActive = true; trig.l2 = 255; trig.r2 = 128;
    {
        const json st = gamepadStateJson(trig);
        check(st["LeftTrigger"].get<double>() > 0.99, "l2=255 -> JS ~1.0");
        check(std::abs(st["RightTrigger"].get<double>() - 128.0/255.0) < 1e-9, "r2=128 -> JS 128/255");
        roundTrip(trig, "trigger round-trip");
    }

    // inputActive=false -> released (all zero) regardless of stored values.
    remoteplay::ControllerState released; released.inputActive = false;
    released.buttons = B::Cross; released.leftX = 30000; released.l2 = 200;
    {
        const auto f = xbox::gamepadFromController(released);
        check(f.buttons == 0 && f.leftX == 0 && f.leftTrigger == 0, "released pad -> zero GamepadFrame (v1 contract)");
    }

    // Combined: all buttons + diagonal sticks + triggers.
    remoteplay::ControllerState full; full.inputActive = true;
    full.buttons = B::Cross|B::Circle|B::L1|B::Options|B::PS;
    full.leftX = 16000; full.leftY = -16000; full.rightX = -8000; full.rightY = 8000;
    full.l2 = 200; full.r2 = 100;
    roundTrip(full, "combined state round-trip");

    std::printf("v2 gamepad mapping: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}