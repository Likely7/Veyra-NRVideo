// SPDX-License-Identifier: GPL-3.0-only
// Shared types of the Xbox transport (Chromium/WebView2). Kept separate from
// the source class so the IXboxSource interface can include them without a
// circular dependency.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "veyra/xbox/StreamProfile.h"

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::xbox { class Account; }

namespace veyra::source {

struct XboxConnectDesc {
    std::shared_ptr<xbox::Account> account;
    std::string serverId;            // the console
    std::string consoleName;         // for messages
    bool gamepad = true;             // forward the PC's controller
    bool softwareDecode = false;    // opt-in workaround for silent hardware decode corruption
    std::string locale = "zh-CN";
    xbox::StreamProfile profile;   // quality tier / bitrate / ICE policy; default = historical behaviour
    int transportEngine = 2;         // 1 = V1 native libdatachannel, 2 = V2 enhanced Chromium/WebView2
    std::shared_ptr<ID3D12Device> decodeDevice;
    std::shared_ptr<ID3D12CommandQueue> decodeQueue;
    const std::atomic<bool>* stopRequested = nullptr; // engine owner outlives the source
};

struct XboxStats {
    enum class State : uint8_t { Idle, Starting, Connecting, Streaming, Ended, Failed };
    State state = State::Idle;
    std::wstring message;           // progress while starting; the reason when Ended or Failed
    std::string disconnectReason;
    bool reconnectable = false;
    uint32_t width = 0, height = 0;
    bool hardwareDecode = false;
    bool softwareDecodeActive = false; // the compatibility switch or the recovery fallback is in effect
    int qualityTier = 1;               // 0=720p 1=1080p 2=1080p HQ; what this session actually requested
    uint64_t units = 0, decoded = 0, dropped = 0, decodeErrors = 0, keyframeRequests = 0;
    uint64_t nacksSent = 0, packetsRecovered = 0, framesDroppedIncomplete = 0;
    double receivedFps = 0, decodedFps = 0, videoMbps = 0;
    double decodeMs = 0, rttMs = 0;
};

} // namespace veyra::source