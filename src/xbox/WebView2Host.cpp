// SPDX-License-Identifier: GPL-3.0-only
// See WebView2Host.h. WebView2 is COM/STA: everything below the public API
// lives on the dedicated UI thread; commands cross over through PostThreadMessage.
#include "veyra/xbox/WebView2Host.h"

#include <windows.h>
#include <objbase.h>
#include <wrl.h>

#include <WebView2.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <utility>

#include "veyra/Log.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using json = nlohmann::json;

namespace veyra::xbox {
namespace {

constexpr UINT kCmdMessage = WM_APP + 0x561;   // 'V''X' — host command wakeup
constexpr UINT kQuitMessage = WM_APP + 0x562;

int64_t host100ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 100;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}

std::filesystem::path userDataDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::filesystem::path(buffer) / L"Veyra" / L"xbox-webview2";
    return std::filesystem::temp_directory_path() / L"Veyra" / L"xbox-webview2";
}

// Strict standard base64 (the player's btoa output). Returns false on any
// invalid character or truncated group; '=' only pads the final group.
bool base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    static constexpr auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (text.size() % 4 != 0) return false;
    out.clear();
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        const int a = value(text[i]), b = value(text[i + 1]);
        if (a < 0 || b < 0) return false;
        const bool padC = text[i + 2] == '=', padD = text[i + 3] == '=';
        if (padC && !padD) return false;
        const int c = padC ? 0 : value(text[i + 2]);
        const int d = padD ? 0 : value(text[i + 3]);
        if (c < 0 || d < 0) return false;
        const uint32_t group = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6) | uint32_t(d);
        out.push_back(uint8_t(group >> 16));
        if (!padC) out.push_back(uint8_t(group >> 8));
        if (!padD) out.push_back(uint8_t(group));
    }
    return true;
}

// The player bundle, embedded by VeyraXboxWeb.cmake (VeyraXboxWebEmbed.cmake
// turns resources/xbox-player/veyra-xbox-player.js into a plain byte array).
// The host reads it directly; no Qt resource machinery is involved, so any
// consumer links without Qt.
extern "C" const unsigned char veyra_xbox_player_js[];
extern "C" const unsigned long long veyra_xbox_player_js_size;

const std::string* findPlayerBundle() {
    static std::string bundle;
    static bool searched = false;
    if (searched) return bundle.empty() ? nullptr : &bundle;
    searched = true;
    if (veyra_xbox_player_js_size == 0) return nullptr;
    bundle.assign(reinterpret_cast<const char*>(veyra_xbox_player_js), size_t(veyra_xbox_player_js_size));
    return &bundle;
}

std::wstring playerPageHtml() {
    // Dev override: load the bundle straight from disk (the resources/ copy).
    std::string js;
    wchar_t override[MAX_PATH * 2] = {};
    const DWORD n = GetEnvironmentVariableW(L"VEYRA_XBOX_PLAYER_JS", override, DWORD(std::size(override)));
    if (n > 0 && n < std::size(override)) {
        HANDLE file = CreateFileW(override, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024) {
                js.resize(size_t(size.QuadPart));
                DWORD read = 0;
                if (!ReadFile(file, js.data(), DWORD(js.size()), &read, nullptr) || read != js.size()) js.clear();
            }
            CloseHandle(file);
        }
    }
    if (js.empty()) {
        if (const std::string* embedded = findPlayerBundle()) js = *embedded;
    }
    if (js.empty()) return {};
    // A literal "</script" inside the bundle would end the inline script tag.
    for (size_t at = js.find("</script"); at != std::string::npos; at = js.find("</script", at + 8))
        js.replace(at, 8, "<\\/script");
    return widen("<!doctype html><html><head><meta charset=\"utf-8\"></head>"
                 "<body><div id=\"veyraVideoHolder\"></div><script>" + js + "</script></body></html>");
}

std::string candidateToJson(const std::vector<IceCandidate>& candidates) {
    json array = json::array();
    for (const auto& c : candidates)
        array.push_back({{"candidate", c.candidate}, {"sdpMid", c.sdpMid}, {"sdpMLineIndex", c.sdpMLineIndex}});
    return array.dump();
}

std::vector<IceCandidate> candidatesFromJson(const json& array) {
    std::vector<IceCandidate> out;
    if (!array.is_array()) return out;
    for (const auto& item : array) {
        if (!item.is_object()) continue;
        IceCandidate c;
        if (const auto it = item.find("candidate"); it != item.end() && it->is_string()) c.candidate = it->get<std::string>();
        if (const auto it = item.find("sdpMid"); it != item.end() && it->is_string()) c.sdpMid = it->get<std::string>();
        if (const auto it = item.find("sdpMLineIndex"); it != item.end() && it->is_number_integer())
            c.sdpMLineIndex = it->get<int>();
        if (!c.candidate.empty()) out.push_back(std::move(c));
    }
    return out;
}

double numberOr(const json& object, const char* key, double fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : fallback;
}

} // namespace

struct WebView2Host::Impl {
    WebView2Host* owner = nullptr;
    WebView2HostCallbacks callbacks;
    DWORD threadId = 0;
    HWND parent = nullptr;
    std::wstring userDataDir;

    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    EventRegistrationToken messageToken{}, processFailedToken{};
    bool closing = false;

    // Command queue: producers push under mutex, the STA thread drains.
    std::mutex cmdMutex;
    std::deque<std::string> commands;

    // One reusable scratch buffer for base64 decoding (STA thread only).
    std::vector<uint8_t> decodeScratch;

    void post(std::string message) {
        if (!threadId) return;
        {
            std::lock_guard lock(cmdMutex);
            if (closing) return;
            commands.push_back(std::move(message));
        }
        PostThreadMessageW(threadId, kCmdMessage, 0, 0);
    }

    void postJson(const json& message) { post(message.dump()); }

    void pumpCommands() {
        std::deque<std::string> batch;
        {
            std::lock_guard lock(cmdMutex);
            batch.swap(commands);
        }
        for (const auto& message : batch) {
            if (webview) webview->PostWebMessageAsJson(widen(message).c_str());
        }
    }

    void fail(const std::string& reason) {
        log::error("xbox-web", reason);
        {
            std::lock_guard lock(owner->readyMutex_);
            owner->failed_ = true;
        }
        owner->readyCv_.notify_all();
        if (callbacks.failed) callbacks.failed(reason);
    }

    void markReady() {
        {
            std::lock_guard lock(owner->readyMutex_);
            owner->ready_ = true;
        }
        owner->readyCv_.notify_all();
        if (callbacks.ready) callbacks.ready();
    }

    void onWebMessage(const std::string& text) {
        json message = json::parse(text, nullptr, false);
        if (message.is_discarded() || !message.is_object()) return;
        const auto typeIt = message.find("type");
        if (typeIt == message.end() || !typeIt->is_string()) return;
        const std::string type = typeIt->get<std::string>();

        if (type == "ready") { markReady(); return; }
        if (type == "log") {
            const auto it = message.find("text");
            if (it != message.end() && it->is_string()) log::info("xbox-web-js", it->get<std::string>());
            return;
        }
        if (type == "state") {
            const auto it = message.find("state");
            if (it != message.end() && it->is_string() && callbacks.state) callbacks.state(it->get<std::string>());
            return;
        }
        if (type == "offer") {
            const auto it = message.find("sdp");
            if (it != message.end() && it->is_string() && callbacks.offer) callbacks.offer(it->get<std::string>());
            return;
        }
        if (type == "ice") {
            const auto it = message.find("candidates");
            if (it != message.end() && callbacks.ice) callbacks.ice(candidatesFromJson(*it));
            return;
        }
        if (type == "rumble") {
            if (!callbacks.rumble) return;
            WebRumble r;
            r.low = numberOr(message, "low", 0);
            r.high = numberOr(message, "high", 0);
            r.leftTrigger = numberOr(message, "leftTrigger", 0);
            r.rightTrigger = numberOr(message, "rightTrigger", 0);
            r.durationMs = int(numberOr(message, "durationMs", 0));
            callbacks.rumble(r);
            return;
        }
        if (type == "videoFrame" || type == "audioFrame") {
            const auto dataIt = message.find("data");
            if (dataIt == message.end() || !dataIt->is_string()) return;
            if (!base64Decode(dataIt->get_ref<const std::string&>(), decodeScratch)) {
                log::warn("xbox-web", "invalid base64 in " + type);
                return;
            }
            // Hand the bytes over and keep the capacity for the next frame.
            std::vector<uint8_t> unit;
            unit.swap(decodeScratch);
            const uint32_t rtp = uint32_t(numberOr(message, "rtpTimestamp", 0));
            const int64_t timestampUs = int64_t(numberOr(message, "timestampUs", 0));
            const int64_t arrival = host100ns();
            if (type == "videoFrame") {
                const auto keyIt = message.find("keyframe");
                const bool keyframe = keyIt != message.end() && keyIt->is_boolean() && keyIt->get<bool>();
                if (callbacks.videoFrame) callbacks.videoFrame(std::move(unit), rtp, timestampUs, keyframe, arrival);
            } else if (callbacks.audioFrame) {
                callbacks.audioFrame(std::move(unit), rtp, timestampUs, arrival);
            }
            return;
        }
    }

    void onControllerCreated(HRESULT status, ICoreWebView2Controller* created) {
        if (FAILED(status) || !created) {
            fail(std::format("WebView2 controller creation failed (0x{:08X})", uint32_t(status)));
            return;
        }
        controller = created;
        controller->put_IsVisible(FALSE);
        controller->put_Bounds(RECT{0, 0, 1, 1});
        if (FAILED(controller->get_CoreWebView2(&webview)) || !webview) {
            fail("WebView2 core unavailable");
            return;
        }
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsWebMessageEnabled(TRUE);
        }
        webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR raw = nullptr;
                    // The bridge posts JSON strings; accept objects too.
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                        const std::string text = narrow(raw);
                        CoTaskMemFree(raw);
                        onWebMessage(text);
                    } else if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
                        const std::string text = narrow(raw);
                        CoTaskMemFree(raw);
                        onWebMessage(text);
                    }
                    return S_OK;
                }).Get(), &messageToken);
        webview->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                    COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED;
                    args->get_ProcessFailedKind(&kind);
                    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE ||
                        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED) {
                        fail(std::format("WebView2 process failed (kind={})", int(kind)));
                    }
                    return S_OK;
                }).Get(), &processFailedToken);

        const std::wstring html = playerPageHtml();
        if (html.empty()) {
            fail("player bundle missing: link the veyra_xbox_player Qt resource or set VEYRA_XBOX_PLAYER_JS");
            return;
        }
        if (FAILED(webview->NavigateToString(html.c_str()))) fail("NavigateToString failed");
    }

    void onEnvironmentCreated(HRESULT status, ICoreWebView2Environment* created) {
        if (FAILED(status) || !created) {
            fail(std::format("WebView2 environment creation failed (0x{:08X}); is the Evergreen runtime installed?",
                             uint32_t(status)));
            return;
        }
        environment = created;
        if (FAILED(environment->CreateCoreWebView2Controller(parent,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        onControllerCreated(result, controller);
                        return S_OK;
                    }).Get()))) {
            fail("CreateCoreWebView2Controller failed");
        }
    }
};

WebView2Host::WebView2Host() : p_(std::make_unique<Impl>()) { p_->owner = this; }

WebView2Host::~WebView2Host() { stop(); }

bool WebView2Host::start(HWND parentWindow, WebView2HostCallbacks callbacks, std::chrono::milliseconds timeout) {
    stop();
    p_ = std::make_unique<Impl>();
    p_->owner = this;
    p_->callbacks = std::move(callbacks);
    p_->parent = parentWindow;
    {
        std::lock_guard lock(readyMutex_);
        ready_ = failed_ = false;
    }
    staThread_ = std::thread([this, parentWindow] { staMain(parentWindow); });
    std::unique_lock lock(readyMutex_);
    readyCv_.wait_for(lock, timeout, [this] { return ready_ || failed_; });
    const bool ok = ready_ && !failed_;
    lock.unlock();
    if (!ok) {
        log::error("xbox-web", failed_ ? "WebView2 host failed to start" : "WebView2 host start timed out");
        stop();
    }
    return ok;
}

void WebView2Host::staMain(HWND parentWindow) {
    p_->threadId = GetCurrentThreadId();
    // Force the thread message queue into existence before anyone can
    // PostThreadMessage to it (otherwise the first post may be dropped).
    MSG peek{};
    PeekMessageW(&peek, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    // WebView2 requires STA. The caller's apartment is irrelevant: this thread
    // initializes its own and pumps until stop().
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) {
        p_->fail(std::format("CoInitializeEx failed (0x{:08X})", uint32_t(co)));
        return;
    }

    HWND parent = parentWindow;
    if (!parent) {
        // A real (never shown) window, not a message-only one: WebView2 hosts
        // its composition visuals under the parent and needs a normal HWND.
        parent = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        if (!parent) {
            p_->fail("could not create the hidden host window");
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }
    }

    p_->userDataDir = userDataDirectory().wstring();
    std::error_code ec;
    std::filesystem::create_directories(p_->userDataDir, ec);
    if (ec) log::warn("xbox-web", "user data directory: " + ec.message());

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, p_->userDataDir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                p_->onEnvironmentCreated(result, environment);
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        p_->fail(std::format("CreateCoreWebView2EnvironmentWithOptions failed (0x{:08X})", uint32_t(hr)));
    } else {
        running_ = true;
        log::info("xbox-web", "WebView2 environment starting");
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && msg.message == kCmdMessage) { p_->pumpCommands(); continue; }
        if (msg.hwnd == nullptr && msg.message == kQuitMessage) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    running_ = false;
    {
        std::lock_guard lock(p_->cmdMutex);
        p_->closing = true;
        p_->commands.clear();
    }
    if (p_->webview) {
        p_->webview->remove_WebMessageReceived(p_->messageToken);
        p_->webview->remove_ProcessFailed(p_->processFailedToken);
    }
    if (p_->controller) p_->controller->Close();
    p_->webview.Reset();
    p_->controller.Reset();
    p_->environment.Reset();
    if (parent != parentWindow) DestroyWindow(parent);
    if (SUCCEEDED(co)) CoUninitialize();
}

void WebView2Host::startSession(const std::string& configJson) {
    json config = json::parse(configJson, nullptr, false);
    if (config.is_discarded() || !config.is_object()) config = json::object();
    p_->postJson({{"type", "start"}, {"config", std::move(config)}});
}

void WebView2Host::sendAnswer(const std::string& sdp) {
    p_->postJson({{"type", "answer"}, {"sdp", sdp}});
}

void WebView2Host::sendRemoteIce(const std::vector<IceCandidate>& candidates) {
    p_->postJson({{"type", "remoteIce"}, {"candidates", json::parse(candidateToJson(candidates))}});
}

void WebView2Host::sendGamepad(const std::string& stateJson) {
    json state = json::parse(stateJson, nullptr, false);
    if (state.is_discarded() || !state.is_object()) return;
    p_->postJson({{"type", "gamepad"}, {"state", std::move(state)}});
}

void WebView2Host::sendStop() { p_->postJson({{"type", "stop"}}); }

void WebView2Host::sendKeyframeRequest() { p_->postJson({{"type", "keyframe"}}); }

void WebView2Host::stop() {
    if (!staThread_.joinable()) {
        running_ = false;
        return;
    }
    // Let the player tear down its RTCPeerConnection cleanly before the pump
    // exits. The quit message is queued behind any pending commands, so the
    // STA thread drains them (including this stop) before it sees kQuitMessage.
    if (running_.load()) sendStop();
    if (p_->threadId) PostThreadMessageW(p_->threadId, kQuitMessage, 0, 0);
    staThread_.join();
    running_ = false;
    p_->callbacks = {};
}

} // namespace veyra::xbox