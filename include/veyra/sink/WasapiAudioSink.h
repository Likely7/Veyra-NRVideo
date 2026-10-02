#pragma once

// AudioPipeline: FFmpeg audio demux/decode + watermarked bounded ring
// (low 250ms / prefill 500ms / high 1000ms, 2s hard bound).
// AudioRenderer: event-driven WASAPI shared-mode endpoint whose device clock
// is mapped onto media PTS through an explicit anchor set at Start and after
// every seek restart.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <format>
#include <mutex>
#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "veyra/Log.h"
#include "veyra/sink/AudioPcmSource.h"
#include "veyra/sink/AudioFrameTimeline.h"
#include "veyra/sink/CaptureAudioDsp.h"
#include "veyra/media/AudioTrack.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct SwrContext;
typedef struct AVFrame AVFrame;
namespace soundtouch { class SoundTouch; }

namespace veyra::sink {

constexpr double kAudioLowWatermarkMs = 250.0;
constexpr double kAudioPrefillMs = 500.0;
constexpr double kAudioHighWatermarkMs = 1000.0;
constexpr uint32_t kAudioRate = 48000;

// --- output endpoint choice (P4-d) -------------------------------------------
// One process-wide choice shared by every renderer (file playback and capture
// monitoring). Empty id = follow the Windows default device, which is also what
// happens when the chosen device is missing; it is switched back to as soon as
// it reappears. Changing the choice rebuilds live endpoints through the existing
// recovery path (the same one a default-device change uses): the audio clock and
// PCM handling are untouched.
struct RenderEndpoint { std::wstring id, name; bool isDefault = false; };
std::vector<RenderEndpoint> enumerateRenderEndpoints();
void setPreferredRenderEndpoint(std::wstring id);
std::wstring preferredRenderEndpoint();
// Mix everything down to stereo before the endpoint (headphones on a 5.1/7.1
// device), instead of mapping channels to the device layout.
void setForceStereoDownmix(bool enabled);
bool forceStereoDownmix();
// What the most recently opened renderer actually uses.
struct ActiveRenderEndpoint { std::wstring id, name; bool fallback = false; unsigned channels = 0; bool downmix = false; bool valid = false; };
ActiveRenderEndpoint activeRenderEndpoint();
uint64_t renderEndpointGeneration();

class AudioRenderer; // forward: pipeline thread needs the renderer
enum class AudioFadeResult { NotPlaying, Drained, Cancelled, TimedOut, Failed };

class AudioPipeline : public AudioPcmSource {
public:
    AudioPipeline();
    ~AudioPipeline();

    bool open(const std::wstring& path);
    const std::vector<media::AudioTrack>& tracks() const { return tracks_; }
    int selectedTrack() const { return streamIndex_; }
    // Owner thread only, after stopThread(). Failure preserves the old decoder.
    bool selectTrack(int streamIndex);
    AudioFormat pcmFormat()const override{return pcmFormat_;}

    double bufferedMs() const;
    // PTS (ms) of the first unconsumed buffered sample; < 0 when empty.
    double headPtsMs() const;
    // Decoded-ahead PTS (ms) of the last buffered sample end; -1 if empty.
    double tailPtsMs() const;

    uint64_t underruns() const;
    uint64_t overruns() const; // must stay 0
    uint64_t seekCount() const;
    double lastPrefillMs() const;
    double firstPtsAfterLastSeek() const;
    bool decodingComplete() const { return decodedEof_.load(); }

    // Pull up to maxFrames source-layout frames; sets the PTS of the first pulled
    // frame. Returns frames pulled. A file source pads an empty endpoint with
    // explicit silence; a live source may return 0 so the owner can preserve
    // its media timeline and apply its bounded recovery policy.
    size_t pull(float* dst, size_t maxFrames, double* firstPtsMs) override;
    std::optional<double> lastPullEndPtsMs()const override;
    // Applied by the audio owner on initial open or the next atomic seek.
    bool setPlaybackRate(double rate);

    void stopThread();
    void setPaused(bool value) { paused_.store(value); }

    // File playback: audio is the master clock. Prefill while explicitly held
    // (open/seek/settings rebuild/pause); once released the device clock runs
    // at the requested tempo regardless of video lag — slow enhancement drops
    // preview frames instead of pausing sound. Coverage is published for
    // diagnostics only.
    void holdForVideo();
    void videoReady(double ptsMs);
    void videoPresented(double nextPtsMs);
    bool waitingForVideo() const { return videoWaiting_.load(); }
    uint64_t videoWaitCount() const { return videoWaitCount_.load(); }

    // Seek protocol (engine thread calls; audio thread executes). Blocks
    // until the audio thread finished the atomic re-sequence and prefilled.
    // Returns the PTS of the first buffered sample after seek.
    double requestSeek(double targetMs);

    void runOnAudioThread(AudioRenderer* renderer, bool ownEndpoint = false, double initialMs = -1);
    void startThread(AudioRenderer* renderer, bool ownEndpoint = false, double initialMs = -1);
    bool endpointRecovering()const{return endpointRecovering_.load();}
    HRESULT endpointError()const{return endpointError_.load();}
    uint64_t endpointRecoveries()const{return endpointRecoveries_.load();}
    bool clockExhausted()const{return clockExhausted_.load();}
    // Fault injection is consumed by the audio owner; never release COM on a caller thread.
    void requestEndpointLossForTest(){endpointLossForTest_=true;}

private:
    struct Segment {
        double startPtsMs;
        size_t frames;
        double stepMs=1000.0/kAudioRate;
    };

    void pushDecoded(const AVFrame* frame);
    void pushConverted(int frames);
    void pushPcm(const float*,size_t,double,double);
    void drainTempo(bool finish=false);
    void resetTempo();
    void decodeBlock();
    void closeAll();

    friend class AudioThread;

    AudioFormat pcmFormat_;
    std::vector<media::AudioTrack> tracks_;
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = nullptr;
    int swrInputRate_=0,swrInputFormat_=-1;
    SwrContext* swr_ = nullptr;
    int streamIndex_ = -1;
    bool havePacket_ = false;
    bool demuxEof_ = false;
    bool drainSent_ = false;
    std::atomic<bool> decodedEof_{false};
    double nextPtsMs_ = 0.0;           // PTS of the NEXT decoded sample
    double discardUntilPtsMs_ = -1.0;  // seek pruning
    std::unique_ptr<soundtouch::SoundTouch> tempo_;
    std::atomic<double> requestedRate_{1},activeRate_{1},lastPullEnd_{-1};
    double tempoPtsMs_=0,tempoInputEndMs_=0;
    bool tempoAnchored_=false,tempoFlushed_=false;
    std::vector<float> tempoBuffer_;
    static constexpr size_t kMaxRingFrames = kAudioRate * 2; // 2s hard bound

    mutable std::mutex mutex_;
    std::deque<float> ring_{};         // source-layout interleaved
    size_t ringFrames_ = 0;
    std::deque<Segment> segments_{};   // PTS bookkeeping of ring contents

    std::condition_variable wake_;
    std::condition_variable seekDoneCv_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> seekRequested_{false};
    bool seekDone_ = true;
    double seekTargetMs_ = 0.0;

    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<uint64_t> seekCount_{0};
    std::atomic<double> lastPrefillMs_{0.0};
    std::atomic<double> firstPtsAfterSeek_{-1.0};
    std::vector<float> converted_{std::vector<float>(kAudioRate)};

    std::thread thread_;
    std::atomic<bool> endpointRecovering_{false},clockExhausted_{false},endpointLossForTest_{false};
    std::atomic<HRESULT> endpointError_{S_OK};
    std::atomic<uint64_t> endpointRecoveries_{0};
    std::atomic<bool> videoSync_{false},videoHold_{false},videoWaiting_{false};
    std::atomic<double> videoLimitMs_{std::numeric_limits<double>::infinity()};
    std::atomic<uint64_t> videoWaitCount_{0};
};

class AudioRenderer {
public:
    ~AudioRenderer(){shutdown();}
    bool start(AudioFormat input = {},double requestedBufferMs=10.0);
    AudioFormat outputFormat()const{std::lock_guard lock(endpointMutex_);return outputFormat_;}
    void setGain(float value){gain_.store(value);}

    // Write actual PCM before starting the endpoint and its media clock.
    bool startAnchored(AudioPcmSource& pipeline, bool paused = false);
    void setPaused(bool value);

    // Event-driven pump for ONE event cycle. Writes real data when available;
    // file sources pad with silence, while live sources release zero frames
    // on an isolated gap so synthetic PCM cannot advance their timeline.
    bool waitForEvent();
    bool pumpOnce(AudioPcmSource& pipeline, double* firstWrittenPtsMs, bool wait = true);

    // Master clock, or NaN when unstarted/reset or the device clock failed.
    double mediaTimeMs() const;

    // Atomic seek support: stop + reset the endpoint; the clock is invalid
    // until the next startAnchored.
    void stopAndReset();
    // Audio-owner only; drains a 5ms fade before a controlled reset, bounded to 80ms.
    AudioFadeResult fadeAndReset(AudioPcmSource& source,const std::atomic<bool>& cancel);

    bool started() const;
    uint64_t underruns() const;
    uint64_t underrunFrames() const;
    uint64_t silenceFrames() const;
    uint64_t emptyPulls() const{return emptyPulls_.load();}
    uint64_t recoveryFades() const{return recoveryFades_.load();}
    uint64_t clockStalledGaps() const{return clockStalledGaps_.load();}
    uint64_t framesWritten() const;
    // A changed endpoint choice reads as a lost device, so the owner rebuilds it.
    HRESULT lastError()const{if(running_&&endpointGeneration_.load()!=renderEndpointGeneration())return AUDCLNT_E_DEVICE_INVALIDATED;return lastError_.load();}
    double bufferedMs()const{return bufferedMs_.load();}
    double capacityMs()const{std::lock_guard lock(endpointMutex_);return 1000.0*bufferFrames_/sampleRate_;}

    void shutdown();

private:
    bool checked(HRESULT hr,const char* operation)const{if(SUCCEEDED(hr))return true;if(lastError_.exchange(hr)!=hr)log::error("audio",std::format("{} hr=0x{:08X}",operation,unsigned(hr)));return false;}
    mutable std::atomic<HRESULT> lastError_{S_OK};
    // Protect endpoint lifetime/anchor from readers; pump remains audio-owner only.
    mutable std::mutex endpointMutex_;
    std::atomic<double> bufferedMs_{0};
    mutable std::mutex timelineMutex_;
    AudioFrameTimeline outputTimeline_;
    bool timedPcm_=false;
    std::atomic<uint64_t> timelineWriteFrame_{0};
    std::atomic<float> gain_{1};float smoothedGain_=1,loggedGain_=-1;
    IMMDeviceEnumerator* enum_ = nullptr;
    // Default-endpoint change notification: flags the renderer as invalidated
    // so the existing recovery path rebuilds the endpoint promptly instead of
    // waiting for the next write to fail (sweep 2026-09-22 C6).
    struct EndpointNotifier;
    EndpointNotifier* notifier_ = nullptr;
    void registerEndpointNotification();
    void unregisterEndpointNotification();
    IMMDevice* device_ = nullptr;
    // Set in start() before the notifier is registered, read by its callbacks.
    std::wstring deviceId_;
    std::wstring preferredId_;
    std::atomic<bool> followsDefault_{true};
    std::atomic<bool> onFallback_{false};
    std::atomic<uint64_t> endpointGeneration_{0};
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    IAudioClock* clock_ = nullptr;
    HANDLE event_ = nullptr;
    UINT32 bufferFrames_ = 0;
    uint32_t sampleRate_ = kAudioRate;
    double devicePeriodMs_=10;
    UINT64 clockFrequency_ = 0;
    std::atomic<UINT64> anchorPos_{0};
    std::atomic<double> anchorPtsMs_{0.0};
    std::atomic<bool> started_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> fading_{false};bool pausedEndpoint_=false;
    size_t fadeInRemaining_=0;
    LiveAudioGapTracker liveGap_;
    std::vector<float> lastRaw_=std::vector<float>(2,0);
    AudioFormat inputFormat_,outputFormat_;
    SwrContext* channelMix_=nullptr;
    std::vector<float> mixed_;
    bool copyPcm(BYTE* destination,const float* input,size_t frames);
    bool comInited_ = false;
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> underrunFrames_{0},silenceFrames_{0};
    std::atomic<uint64_t> emptyPulls_{0},recoveryFades_{0};
    std::atomic<uint64_t> clockStalledGaps_{0};
    std::atomic<uint64_t> framesWritten_{0};
    std::vector<float> chunk_;
};

} // namespace veyra::sink
