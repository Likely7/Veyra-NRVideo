#include "TestConsole.h"
using namespace std::chrono_literals;

namespace veyra::remoteplay::test {
TestConsole console;

bool TestConsole::connect(source::RemotePlaySessionSource& source, bool captureCard, bool viewOnly, bool wait) {
    veyra::source::RemotePlayConnectDesc request;
    request.request.host = "offline.invalid";
    request.controlOnly = captureCard;
    request.request.viewOnly = viewOnly;
    source.close();
    sent.clear();
    deferConnected = wait;
    return source.connect(std::move(request)) && discardMedia == captureCard;
}
}

// Only the network and audio endpoints are replaced. Both production sender loops,
// the source adapter, its queue, and the mapper worker run in this executable.
namespace veyra::remoteplay {
using test::console;
struct ChiakiBackend::Impl {};
ChiakiBackend::ChiakiBackend() = default;
ChiakiBackend::~ChiakiBackend() = default;
PairingCredentials::PairingCredentials(PairingCredentials&&) noexcept = default;
PairingCredentials& PairingCredentials::operator=(PairingCredentials&&) noexcept = default;
PairingCredentials::~PairingCredentials() = default;
BackendResult ChiakiBackend::start(const NativeConnectRequest& request, SessionInbox::Token token, bool discardMedia) {
    std::lock_guard lock(console.mutex);
    console.feedback = request.keyboardMouse;
    console.owner = std::this_thread::get_id();
    console.discardMedia = discardMedia;
    p_ = std::make_unique<Impl>();
    console.events = token;
    console.videoCallbacks = 0;
    console.connected = !console.deferConnected;
    if (console.connected) token.connected();
    console.changed.notify_all();
    return {true};
}
BackendResult ChiakiBackend::submitController(const ControllerState& state) {
    if (console.owner != std::this_thread::get_id())
        throw std::runtime_error("controller used outside connection owner");
    std::unique_lock lock(console.mutex);
    console.sent.push_back(state);
    console.stalled = console.stall;
    console.changed.notify_all();
    console.changed.wait_for(lock, 2s, []{return !console.stall;});
    console.stalled = false;
    return {true};
}
BackendResult ChiakiBackend::submitLoginPin(std::string_view) {return {true};}
BackendResult ChiakiBackend::requestIdr() {return {true};}
ControllerFeedback ChiakiBackend::takeFeedback() {return {};}
NativeSnapshot ChiakiBackend::snapshot() const {
    NativeSnapshot snapshot{bool(p_),p_ && console.connected.load()};
    snapshot.videoCallbacks = console.videoCallbacks;
    return snapshot;
}
BackendResult ChiakiBackend::stop() {p_.reset(); return {true};}
}

namespace veyra::sink {
struct CaptureAudioSession::Impl {};
CaptureAudioSession::CaptureAudioSession() = default;
CaptureAudioSession::~CaptureAudioSession() = default;
bool CaptureAudioSession::configure(const WAVEFORMATEX&, size_t) {return false;}
bool CaptureAudioSession::start(std::wstring) {return false;}
bool CaptureAudioSession::push(const void*, size_t, double, bool) {return false;}
void CaptureAudioSession::stop() {}
}
