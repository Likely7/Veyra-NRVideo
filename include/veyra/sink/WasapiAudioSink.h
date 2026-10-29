#pragma once
#include "veyra/media/DemuxSession.h"
#include "veyra/media/NetworkInput.h"

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
// 音频落后画面多少毫秒就主动跳过去重新对齐。正常播放 lead 只有几十毫秒，超过它
// 一定是真的掉了（读包失败恢复中、端点长时间欠载），不校正就会一直不同步。
// How far the audio clock may fall behind the picture before it is re-synced by
// seeking. Steady playback stays within a few tens of milliseconds, so exceeding
// this means it really fell behind (recovering from a failed read, a long endpoint
// underrun) and would otherwise stay out of sync for the rest of the video.
constexpr double kAudioResyncBehindMs = 300.0;

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

    // `requestHeaders` / `userAgent` are for a DASH audio address on a CDN that
    // checks the Referer (Bilibili): the audio URL is a separate host request and
    // needs the same headers the video one carries.
    bool open(const std::wstring& path,const std::atomic<bool>* cancelled=nullptr,
              const std::wstring& requestHeaders={},const std::wstring& userAgent={});
    // Network sources and files on a server: take audio packets from the shared demux
    // session that also feeds the video (one connection, #48) instead of opening the
    // address a second time.
    bool open(std::shared_ptr<media::DemuxSession> session);
    bool sharedSession() const { return session_ != nullptr; }
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
    // 视频那边的网络缓冲状态。B 站的音轨是单独打开的（session_ 为空），管道自己看不到
    // 视频会话的缓冲阶段，于是 shouldPause() 里的 networkHold() 恒为假 —— 结果就是画面
    // 因缓冲停住、声音却继续播。引擎在缓冲阶段变化时把状态推过来。
    // The video's own buffering state. A Bilibili audio track is opened separately (session_
    // is null), so the pipeline cannot see the video session's phase and networkHold() stays
    // false - the picture stalls for buffering while the sound keeps going. The engine pushes
    // the state here when the phase changes.
    void setNetworkHold(bool hold) { networkHoldExternal_.store(hold); }
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
    // 音频正在等网络重试（读包失败、尚未读到下一段）：不是流结束，只是暂时没数据。
    // 引擎据此冻结媒体时钟，让画面停下来等声音 —— 不这样做，画面会一直往前跑，
    // 声音恢复后就落后几秒，对不上口型。
    // The audio is waiting for a network read to succeed: not end of stream, just no data
    // yet. The engine freezes the media clock on it so the picture waits for the sound;
    // without that the picture runs ahead and the sound comes back seconds behind.
    bool retryingRead() const { return readRetrying_.load(); }
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
    // Packet read-ahead (2026-10-11, logs16): the audio thread pumps a 22 ms
    // WASAPI buffer, so it must never wait on file I/O. During steady playback
    // a reader thread owns av_read_frame and queues this track's packets; open,
    // seek and endpoint recovery pause it and read directly as before.
    // 返回 av_read_frame 的原始结果：>= 0 成功，AVERROR_EOF 才是真正的结束，其它负值
    // 是这一次没读到（网络源可能是瞬时的），调用方不得当成 EOF。
    // Returns av_read_frame's own result: >= 0 success, AVERROR_EOF is a real end, any
    // other negative value means this read returned nothing (transient on a network
    // source) and must not be treated as EOF.
    int readPacket(AVPacket* packet);   // av_read_frame with timing and test stall
    void readerLoop();
    void startReader();
    void pauseReader();                  // returns once the reader is idle; drops queued packets
    void resumeReader();
    void stopReader();
    bool takeQueuedPacket(bool& eof,int waitMs=0);   // moves the head packet into packet_; waits up to waitMs (session only)
    // Shared session: reposition the decoder and PCM ring at `target` without moving
    // the (shared) demuxer. The engine has already repositioned the session for a seek.
    bool rewindSession(double target);
    void noteDamagedAudio(int code);
    void trimRingBefore(double targetMs);   // mutex_ held

    friend class AudioThread;

    AudioFormat pcmFormat_;
    std::vector<media::AudioTrack> tracks_;
    AVFormatContext* fmt_ = nullptr;
    std::shared_ptr<media::DemuxSession> session_;   // shared mode: packets come from here, fmt_ stays null
    // 独立打开模式下这个音频目标是不是网络地址。B 站这类 DASH 源的音轨是**单独一条
    // CDN 请求**（`options.audioPath`），走的是 open(path) 而不是 open(session)，
    // 所以 session_ 为空。容错判断必须同时认这种情况，否则一个坏包就会让声音永久停止。
    // Whether the separately-opened audio target is a network address. A DASH source
    // such as Bilibili carries its audio as its OWN CDN request (options.audioPath) and
    // goes through open(path) rather than open(session), so session_ is null there. The
    // damaged-packet tolerance must cover that case too, or one bad packet silences
    // playback for good.
    bool networkTarget_ = false;
    uint32_t sessionGeneration_ = 0;                 // audio thread: the session reposition this decoder follows
    bool sessionDiscontinuity_ = false;              // next packet starts a new live connection: reset the decoder
    std::chrono::steady_clock::time_point prefillDeadline_{};
    bool prefillWait_ = false;                       // audio thread: wait for session packets (open / seek prefill)
    uint64_t damagedAudioPackets_ = 0;               // network audio packets the decoder rejected and we skipped
    unsigned damagedAudioRuns_ = 0;
    // 预读线程连续读失败的次数；达到阈值就请求跳过损坏区段（见 skipDamagedToMs_）。
    // Consecutive read failures on the read-ahead thread; at the threshold it asks for the
    // damaged stretch to be skipped (see skipDamagedToMs_).
    unsigned readFailureRun_ = 0;
    // 已经跳过损坏段几次。必须封顶：跳到音频末尾之后，seek 会"成功"但一字节都读不到，
    // 于是又累计失败再跳同一个位置 —— 现场 2026-10-25 就是这样无限循环
    // （日志里 58 次 "skipping to 33272 ms"），画面被反复冻住。
    // How many times a damaged stretch has been skipped. This must be capped: once the
    // jump lands past the end of the audio the seek "succeeds" but nothing can be read, so
    // failures accumulate and it jumps to the same place again - the field log of
    // 2026-10-25 shows 58 repeats of "skipping to 33272 ms", freezing the picture each time.
    unsigned damagedSkipCount_ = 0;
    std::chrono::steady_clock::time_point damagedAudioLogAt_{};
    media::InputInterrupt inputInterrupt_;
    AVCodecContext* codecCtx_ = nullptr;
    AVRational streamTimeBase_{1, 1000};
    AVPacket* packet_ = nullptr;
    int swrInputRate_=0,swrInputFormat_=-1;
    SwrContext* swr_ = nullptr;
    int streamIndex_ = -1;
    bool havePacket_ = false;
    bool demuxEof_ = false;
    bool drainSent_ = false;
    std::atomic<bool> decodedEof_{false};
    // 网络源读包失败后置位，读到下一段数据或确认 EOF 时清除。引擎用它冻结时钟。
    // Set when a network read fails, cleared once packets flow again or EOF is confirmed.
    // The engine freezes the media clock while it is set.
    std::atomic<bool> readRetrying_{false};
    // 第一次读失败的时刻。重试超过 10 秒就放弃冻结画面（网络可能真的断了），让视频
    // 继续播、音频在后面重试；恢复后由落后校正追上来。
    // When the current burst of read failures started. Past ten seconds the freeze is
    // released - the link may really be gone - so the video keeps playing while the audio
    // retries; the drift correction catches it up if it recovers.
    std::chrono::steady_clock::time_point readRetryingSince_{};
    // 最后成功读到的包时间戳（毫秒）。读包卡在损坏数据上时，用它算「往前跳多远」。
    // Timestamp of the last packet read successfully, in ms. Used to work out how far to
    // jump when reading is stuck on damaged data.
    std::atomic<double> lastGoodReadMs_{-1.0};
    // 需要跳过损坏区段时由预读线程写入目标位置，解码线程取走后转成一次 seek。
    // 用原子变量而不是 seekTargetMs_：预读线程已持有 readerMutex_，再去拿 mutex_ 会
    // 和解码线程的加锁顺序打结。
    // Written by the read-ahead thread when a damaged stretch has to be skipped, taken by
    // the decode thread and turned into a seek. An atomic on purpose: the read-ahead thread
    // already holds readerMutex_, and taking mutex_ there would invert the lock order
    // against the decode thread.
    std::atomic<double> skipDamagedToMs_{-1.0};
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
    std::atomic<bool> networkHoldExternal_{false};   // 见 setNetworkHold()
    std::atomic<double> videoLimitMs_{std::numeric_limits<double>::infinity()};
    std::atomic<uint64_t> videoWaitCount_{0};

    static constexpr double kReadAheadMs = 2500.0;
    static constexpr size_t kReadAheadPackets = 4096, kReadAheadBytes = size_t(16) << 20;
    std::thread reader_;
    std::mutex readerMutex_;
    std::condition_variable readerCv_;
    std::deque<AVPacket*> queuedPackets_;
    double queuedMs_ = 0;
    size_t queuedBytes_ = 0;
    bool readerStop_ = false, readerPaused_ = false, readerBusy_ = false, readerEof_ = false;
    bool queueMode_ = false;             // audio thread only: packets come from the reader
    bool directReadForTest_ = false;     // VEYRA_TEST_AUDIO_DIRECT_READ=1: pre-2026-10-11 behaviour
    double readStallMs_ = 0, readStallEveryMs_ = 0;  // VEYRA_TEST_AUDIO_READ_STALL_MS / _EVERY_MS
    std::chrono::steady_clock::time_point nextReadStall_{};
    std::atomic<double> maxReadMs_{0};   // slowest av_read_frame since the last summary
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
    int64_t tapLatencyUs_=0;   // live recording: device-clock playout minus the queued-frame estimate
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
