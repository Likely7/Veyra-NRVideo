// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// SDL ControllerState -> the player's InputFrame JSON (V2 gamepad bridge).
// Shared by XboxWebSource and the offline mapping test so the test exercises
// the production mapper, not a copy of it.
//
// Wire contract: the player's Packet.ts serializes X as-is and NEGATES Y
// (-input.LeftThumbYAxis). SDL sticks are down-positive, so the InputFrame
// axes pass the SDL value through UNCHANGED (clamped to [-32767, 32767]);
// negating here as well would invert up/down in game. The resulting wire
// bytes equal v1's gamepadFromController (Protocol.h) for the same state.
#include <cstdint>

#include <nlohmann/json.hpp>

#include "veyra/remoteplay/Types.h"

namespace veyra::xbox {

inline nlohmann::json gamepadStateJson(const remoteplay::ControllerState& s) {
    using B = remoteplay::ControllerState;
    const auto button = [&](uint32_t mask) { return (s.inputActive && (s.buttons & mask)) ? 1 : 0; };
    const auto axis = [](int16_t v) {
        if (v < -32767) v = -32767;   // INT16_MIN saturates to -32767, keeping direction
        return double(v) / 32767.0;
    };
    const bool active = s.inputActive;
    return nlohmann::json{
        {"GamepadIndex", 0},
        {"A", button(B::Cross)}, {"B", button(B::Circle)}, {"X", button(B::Square)}, {"Y", button(B::Triangle)},
        {"DPadUp", button(B::Up)}, {"DPadDown", button(B::Down)},
        {"DPadLeft", button(B::Left)}, {"DPadRight", button(B::Right)},
        {"LeftShoulder", button(B::L1)}, {"RightShoulder", button(B::R1)},
        {"LeftThumb", button(B::L3)}, {"RightThumb", button(B::R3)},
        {"Menu", button(B::Options)}, {"View", button(B::Share)}, {"Nexus", button(B::PS)},
        {"LeftThumbXAxis", active ? axis(s.leftX) : 0.0},
        {"LeftThumbYAxis", active ? axis(s.leftY) : 0.0},
        {"RightThumbXAxis", active ? axis(s.rightX) : 0.0},
        {"RightThumbYAxis", active ? axis(s.rightY) : 0.0},
        {"LeftTrigger", active ? double(s.l2) / 255.0 : 0.0},
        {"RightTrigger", active ? double(s.r2) / 255.0 : 0.0},
    };
}

} // namespace veyra::xbox