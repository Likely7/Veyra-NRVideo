// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/StartupMusic.h"
#include "veyra/Log.h"
#include <QFileInfo>
#include <QMetaObject>
#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <windows.h>
#include <audiosessiontypes.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wrl/client.h>

namespace veyra::ui {
namespace {
using Microsoft::WRL::ComPtr;

// The engine calls this on its own threads; events go to the owner's thread. detach()
// before the owner goes away: a queued call to a destroyed owner is then never made.
class EngineNotify final : public IMFMediaEngineNotify {
public:
    explicit EngineNotify(StartupMusic* owner) : owner_(owner) {}
    void detach() { std::lock_guard lock(mutex_); owner_ = nullptr; }
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *out = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG left = --refs_;
        if (!left) delete this;
        return left;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override {
        std::lock_guard lock(mutex_);
        if (StartupMusic* owner = owner_)
            QMetaObject::invokeMethod(owner, [owner, event, param1, param2] { owner->engineEvent(event, param1, param2); }, Qt::QueuedConnection);
        return S_OK;
    }
private:
    std::mutex mutex_;
    StartupMusic* owner_;
    std::atomic<ULONG> refs_{1};
};
}

struct StartupMusic::Impl {
    QString path;
    bool exists = false;
    bool mfStarted = false;
    ComPtr<IMFMediaEngine> engine;
    EngineNotify* notify = nullptr;
    QString state = QStringLiteral("idle");
    bool muted = false;
    bool playWanted = false;
};

StartupMusic::StartupMusic(QString path, QObject* parent) : QObject(parent), p_(std::make_unique<Impl>()) {
    p_->path = std::move(path);
    p_->exists = QFileInfo(p_->path).isFile();
}

StartupMusic::~StartupMusic() { release(); }

bool StartupMusic::available() const { return p_->exists; }
QString StartupMusic::state() const { return p_->state; }
bool StartupMusic::muted() const { return p_->muted; }

void StartupMusic::setMuted(bool muted) {
    if (p_->muted == muted) return;
    p_->muted = muted;
    if (p_->engine) p_->engine->SetMuted(muted ? TRUE : FALSE);
    log::info("startup-music", muted ? "muted" : "unmuted");
    emit mutedChanged();
}

void StartupMusic::setState(const QString& state) {
    if (p_->state == state) return;
    p_->state = state;
    emit stateChanged();
}

void StartupMusic::prepare(double volume) {
    if (!p_->exists || p_->engine || p_->state != QLatin1String("idle")) return;
    const auto fail = [this](const char* step, HRESULT hr) {
        log::error("startup-music", std::format("{} failed hr=0x{:08X}", step, static_cast<unsigned>(hr)));
        release();
        setState(QStringLiteral("error"));
    };
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return fail("MFStartup", hr);
    p_->mfStarted = true;
    ComPtr<IMFMediaEngineClassFactory> factory;
    hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return fail("media engine factory", hr);
    ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(&attributes, 2);
    if (FAILED(hr)) return fail("attributes", hr);
    p_->notify = new EngineNotify(this);
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, p_->notify);
    if (FAILED(hr)) return fail("callback", hr);
    hr = attributes->SetUINT32(MF_MEDIA_ENGINE_AUDIO_CATEGORY, AudioCategory_Media);
    if (FAILED(hr)) return fail("audio category", hr);
    hr = factory->CreateInstance(MF_MEDIA_ENGINE_AUDIOONLY, attributes.Get(), &p_->engine);
    if (FAILED(hr)) return fail("media engine", hr);
    hr = p_->engine->SetAutoPlay(FALSE);
    if (FAILED(hr)) return fail("autoplay off", hr);
    hr = p_->engine->SetLoop(FALSE);                     // once, as the user asked
    if (FAILED(hr)) return fail("loop off", hr);
    hr = p_->engine->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC);
    if (FAILED(hr)) return fail("preload", hr);
    hr = p_->engine->SetVolume(std::clamp(volume, 0.0, 1.0));
    if (FAILED(hr)) return fail("volume", hr);
    hr = p_->engine->SetMuted(p_->muted ? TRUE : FALSE);
    if (FAILED(hr)) return fail("mute", hr);
    BSTR url = SysAllocString(reinterpret_cast<const wchar_t*>(p_->path.utf16()));
    if (!url) return fail("source allocation", E_OUTOFMEMORY);
    hr = p_->engine->SetSource(url);
    SysFreeString(url);
    if (FAILED(hr)) return fail("source", hr);
    log::info("startup-music", std::format("loading volume={:.2f} muted={}", volume, p_->muted));
    setState(QStringLiteral("loading"));
}

void StartupMusic::play() {
    if (!p_->engine || (p_->state != QLatin1String("loading") && p_->state != QLatin1String("playing"))) return;
    p_->playWanted = true;
    const HRESULT hr = p_->engine->Play();
    if (FAILED(hr)) log::error("startup-music", std::format("play failed hr=0x{:08X}", static_cast<unsigned>(hr)));
}

void StartupMusic::pause() {
    // Consumed even before prepare(): a command-line/auto-resume source can open first.
    if (p_->state == QLatin1String("paused") || p_->state == QLatin1String("ended")) return;
    p_->playWanted = false;
    const double at = p_->engine ? p_->engine->GetCurrentTime() : 0.0;
    if (p_->engine) {
        const HRESULT hr = p_->engine->Pause();
        if (FAILED(hr)) log::warn("startup-music", std::format("pause hr=0x{:08X}", static_cast<unsigned>(hr)));
    }
    log::info("startup-music", std::format("stopped permanently at {:.2f} s", at));
    release();                                      // it does not resume: let the audio device go
    setState(QStringLiteral("paused"));
}

double StartupMusic::position() const {
    if (!p_->engine || p_->state != QLatin1String("playing")) return -1;
    return p_->engine->GetCurrentTime();
}

void StartupMusic::engineEvent(unsigned long event, unsigned long long param1, unsigned long param2) {
    if (!p_->engine) return; // Ignore callbacks queued before stop/shutdown.
    switch (event) {
    case MF_MEDIA_ENGINE_EVENT_PLAYING:
        if (p_->playWanted) {
            log::info("startup-music", std::format("playing from {:.3f} s", p_->engine ? p_->engine->GetCurrentTime() : 0.0));
            setState(QStringLiteral("playing"));
        }
        break;
    case MF_MEDIA_ENGINE_EVENT_ENDED:
        log::info("startup-music", "ended");
        release();
        setState(QStringLiteral("ended"));
        break;
    case MF_MEDIA_ENGINE_EVENT_ERROR:
        log::error("startup-music", std::format("engine error {} hr=0x{:08X}", param1, static_cast<unsigned>(param2)));
        release();
        setState(QStringLiteral("error"));
        break;
    default:
        break;
    }
}

// Lets the audio device and Media Foundation go: after the end, on an error and on exit.
void StartupMusic::release() {
    if (p_->notify) p_->notify->detach();
    if (p_->engine) {
        p_->engine->Shutdown();
        p_->engine.Reset();
    }
    if (p_->notify) {
        p_->notify->Release();
        p_->notify = nullptr;
    }
    if (p_->mfStarted) {
        MFShutdown();
        p_->mfStarted = false;
    }
}
}
