// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// WebView2Host: an invisible WebView2 running the forked XStreaming player
// (xstreaming-player, MIT) for the Xbox v2 transport. Chromium terminates the
// WebRTC session; RTCRtpScriptTransform exports the encoded H.264 access units
// (complete Annex-B, in-band SPS/PPS/IDR on keyframes) and Opus packets before
// decoding, and this host relays them to the caller as raw bytes. The media
// never renders inside the WebView.
//
// Threading: WebView2 requires an STA thread with a message pump. The host
// owns one (created on start(), independent of the caller's apartment) and
// marshals every command onto it; all callbacks fire on that thread and must
// not block. The player bundle ships as a Qt resource
// (:/xbox-player/veyra-xbox-player.js) and is loaded via NavigateToString.
//
// Wire protocol (JSON, shared with xstreaming-player/src/VeyraBridge.ts):
//   JS -> C++: ready | offer{sdp} | ice{candidates:[{candidate,sdpMid,sdpMLineIndex}]}
//              videoFrame{data:base64, rtpTimestamp:u32@90kHz, timestampUs, keyframe}
//              audioFrame{data:base64, rtpTimestamp:u32@48kHz, timestampUs}
//              state{state:new/connecting/connected/failed/closed}
//              rumble{low,high,leftTrigger,rightTrigger:0..1, durationMs}
//              log{text}
//   C++ -> JS: start{config:{turnServer?,codec?,codecProfiles?,videoBitrate?,audioBitrate?,
//              pollingRate?,vibration?,deadzone?}} | answer{sdp} | remoteIce{candidates}
//              gamepad{state:InputFrame keys} | stop
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "veyra/xbox/StreamApi.h"   // IceCandidate

struct HWND__;
typedef HWND__* HWND;

namespace veyra::xbox {

// One controller rumble report as the player relays it (magnitudes 0..1).
struct WebRumble {
    double low = 0, high = 0;                 // weak (right) / strong (left) motor
    double leftTrigger = 0, rightTrigger = 0;
    int durationMs = 0;
};

struct WebView2HostCallbacks {
    // Encoded media. data is a complete Annex-B H.264 AU (video) or one Opus
    // packet (audio); rtp is the RTP timestamp (90 kHz / 48 kHz); timestampUs
    // is the player's performance.now()-based receive time (diagnostics only,
    // NOT a wall clock); arrival100ns is this host's steady clock at receipt.
    std::function<void(std::vector<uint8_t>&& data, uint32_t rtp, int64_t timestampUs,
                       bool keyframe, int64_t arrival100ns)> videoFrame;
    std::function<void(std::vector<uint8_t>&& data, uint32_t rtp, int64_t timestampUs,
                       int64_t arrival100ns)> audioFrame;
    // Signalling pump (the caller owns the REST exchange, blocking is fine).
    std::function<void(std::string sdp)> offer;
    std::function<void(std::vector<IceCandidate> candidates)> ice;
    // RTCPeerConnection state and page readiness/failure.
    std::function<void(std::string state)> state;
    std::function<void(WebRumble)> rumble;
    std::function<void()> ready;                       // bundle executed
    std::function<void(std::string reason)> failed;    // environment/page/process failure
};

class WebView2Host final {
public:
    WebView2Host();
    ~WebView2Host();
    WebView2Host(const WebView2Host&) = delete;
    WebView2Host& operator=(const WebView2Host&) = delete;

    // Spawns the STA thread, creates the environment (user data under
    // %LOCALAPPDATA%\Veyra\xbox-webview2), the invisible controller and loads
    // the embedded player page. Blocks until the page reports 'ready' or the
    // timeout expires; false means the runtime/page is unusable (see log).
    bool start(HWND parentWindow, WebView2HostCallbacks callbacks,
               std::chrono::milliseconds timeout = std::chrono::seconds(20));

    // Commands; thread-safe, marshalled to the STA thread. No-ops when the
    // host is not running. startSession posts {type:'start',config}.
    void startSession(const std::string& configJson);
    void sendAnswer(const std::string& sdp);
    void sendRemoteIce(const std::vector<IceCandidate>& candidates);
    void sendGamepad(const std::string& stateJson);
    void sendStop();
    // Explicit keyframe request (loss/decode recovery). The player's Control
    // channel also pulses every 5 s on its own; this maps to {type:'keyframe'}.
    void sendKeyframeRequest();

    void stop();          // posts 'stop', closes the controller, joins the STA thread
    bool running() const { return running_.load(); }
    // Why the last start() failed (empty on timeout without a specific error).
    // Valid after start() returns false; survives stop().
    std::string startFailureReason() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    std::atomic<bool> running_{false};
    std::thread staThread_;
    mutable std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false, failed_ = false;
    std::string failReason_;            // guarded by readyMutex_

    void staMain(HWND parentWindow);
};

} // namespace veyra::xbox