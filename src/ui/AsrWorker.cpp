// SPDX-License-Identifier: GPL-3.0-only
#include "AsrWorker.h"
#include "AsrModels.h"
#include "AsrProtocol.h"
#include "../../third_party/sherpa-onnx/c-api.h"
#include <objbase.h>
#include "veyra/sink/RecordingAudioTap.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QThread>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace veyra::ui::asr {
namespace {
constexpr int window = 512;
constexpr int previewIntervalMs = 900;
constexpr int previewBudgetMs = previewIntervalMs * 3 / 10;
constexpr int previewPadding = 10 * window;
constexpr int cutLookbackWindows = sampleRate / window;
constexpr int cutOverlapWindows = 6; // 192 ms on each side of the cut.
constexpr int noiseTrackingWindows = 96;
constexpr size_t maxPendingSegments = 4;
constexpr int languageSilenceUs = 10000000;

qint64 processCpuMs() {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    const auto ticks = [](FILETIME time) {
        return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return qint64((ticks(kernel) + ticks(user)) / 10000);
}

#define VEYRA_ASR_SYMBOLS(F) \
    F(SherpaOnnxGetVersionStr) \
    F(SherpaOnnxCreateOfflineRecognizer) \
    F(SherpaOnnxDestroyOfflineRecognizer) \
    F(SherpaOnnxCreateOfflineStream) \
    F(SherpaOnnxOfflineStreamSetOption) \
    F(SherpaOnnxDestroyOfflineStream) \
    F(SherpaOnnxAcceptWaveformOffline) \
    F(SherpaOnnxDecodeOfflineStream) \
    F(SherpaOnnxGetOfflineStreamResult) \
    F(SherpaOnnxDestroyOfflineRecognizerResult) \
    F(SherpaOnnxCreateVoiceActivityDetector) \
    F(SherpaOnnxDestroyVoiceActivityDetector) \
    F(SherpaOnnxVoiceActivityDetectorAcceptWaveform) \
    F(SherpaOnnxVoiceActivityDetectorDetected) \
    F(SherpaOnnxVoiceActivityDetectorEmpty) \
    F(SherpaOnnxVoiceActivityDetectorFront) \
    F(SherpaOnnxVoiceActivityDetectorPop) \
    F(SherpaOnnxVoiceActivityDetectorReset) \
    F(SherpaOnnxVoiceActivityDetectorFlush) \
    F(SherpaOnnxDestroySpeechSegment) \
    F(SherpaOnnxReadWaveFromBinaryData) \
    F(SherpaOnnxFreeWave)

struct Runtime {
    HMODULE module = nullptr;
#define DECLARE(name) decltype(&::name) name = nullptr;
    VEYRA_ASR_SYMBOLS(DECLARE)
#undef DECLARE
    ~Runtime() { if (module) FreeLibrary(module); }

    bool load(const QString& path) {
        module = LoadLibraryExW(QDir::toNativeSeparators(path).toStdWString().c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return false;
#define BIND(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, #name)); if (!name) return false;
        VEYRA_ASR_SYMBOLS(BIND)
#undef BIND
        return QByteArrayView(SherpaOnnxGetVersionStr()) == runtimeVersion;
    }
};
#undef VEYRA_ASR_SYMBOLS

bool readBytes(HANDLE input, void* data, DWORD size) {
    auto* next = static_cast<char*>(data);
    while (size > 0) {
        DWORD read = 0;
        if (!ReadFile(input, next, size, &read, nullptr) || !read) return false;
        next += read;
        size -= read;
    }
    return true;
}

bool resultLine(HANDLE output, const QJsonObject& message) {
    auto data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    data += '\n';
    DWORD written = 0;
    return WriteFile(output, data.constData(), DWORD(data.size()), &written, nullptr)
        && written == DWORD(data.size());
}

bool hasSpeechText(const QString& text) {
    if (std::none_of(text.begin(), text.end(), [](QChar ch) { return ch.isLetterOrNumber(); })) return false;
    // Repeated identical phrases are a common Whisper failure during long
    // music beds. Ordinary short dialogue such as "thank you" is retained.
    for (qsizetype length = 2; length * 3 <= text.size() && length <= 40; ++length) {
        if (text.size() % length == 0 && text.first(length).repeated(text.size() / length) == text)
            return false;
    }
    return true;
}

class Recognizer {
public:
    explicit Recognizer(HANDLE output) : output_(output) {}
    ~Recognizer() {
        if (decoder_.joinable()) {
            decoder_.request_stop();
            decoder_.join();
        }
        if (vad_) runtime_.SherpaOnnxDestroyVoiceActivityDetector(vad_);
        if (recognizer_) runtime_.SherpaOnnxDestroyOfflineRecognizer(recognizer_);
    }

    bool initialize(const QString& folder, int model, const QString& language, int threads) {
        QElapsedTimer start;
        start.start();
        if (!QDir::isAbsolutePath(folder) || !verify(folder, model, true))
            return fail("asset-verification");
        const auto bundle = assets(model);
        const QString dll = QDir(ModelAssets::path(folder, bundle[0])).filePath(QStringLiteral("sherpa-onnx-c-api.dll"));
        if (!runtime_.load(dll)) return fail("runtime-load", GetLastError());

        threads = std::clamp(threads, 1, 8);
        model_ = model;
        language_ = model == Parakeet ? QStringLiteral("en") : whisperLanguage(language);
        const bool englishWhisper = model == Whisper && language_ == QLatin1String("en");
        const int maxUtteranceMs = model == Parakeet ? 4200 : englishWhisper ? 2800 : 3800;
        const int minUtteranceMs = model == Parakeet ? 2000 : englishWhisper ? 800 : 1150;
        maxSpeechWindows_ = maxUtteranceMs * sampleRate / (1000 * window);
        minSpeechWindows_ = minUtteranceMs * sampleRate / (1000 * window);
        quietPauseWindows_ = model == Parakeet ? 7 : englishWhisper ? 5 : 6;
        previewSamples_.reserve(size_t(maxSpeechWindows_ * window + previewPadding));
        const QDir dir(ModelAssets::path(folder, bundle[2]));
        const auto& files = bundle[2].members;
        const auto encoder = dir.filePath(QString::fromLatin1(files[0].name)).toUtf8();
        const auto decoder = dir.filePath(QString::fromLatin1(files[1].name)).toUtf8();
        const auto joiner = model == Parakeet ? dir.filePath(QString::fromLatin1(files[2].name)).toUtf8() : QByteArray{};
        const auto tokens = dir.filePath(QString::fromLatin1(files.back().name)).toUtf8();
        const auto languageId = language_.toUtf8();
        SherpaOnnxOfflineRecognizerConfig config{};
        config.feat_config.sample_rate = sampleRate;
        config.feat_config.feature_dim = 80;
        config.model_config.tokens = tokens.constData();
        config.model_config.num_threads = threads;
        config.model_config.provider = "cpu";
        config.decoding_method = "greedy_search";
        if (model_ == Parakeet) {
            config.model_config.transducer = {encoder.constData(), decoder.constData(), joiner.constData()};
            config.model_config.model_type = "nemo_transducer";
        } else {
            config.model_config.whisper.encoder = encoder.constData();
            config.model_config.whisper.decoder = decoder.constData();
            config.model_config.whisper.language = languageId.constData();
            config.model_config.whisper.task = "transcribe";
            // The pinned dynamic encoder otherwise processes ten seconds of
            // padding per utterance. Retain an end-of-text margin.
            config.model_config.whisper.tail_paddings = 300;
            config.model_config.whisper.enable_segment_timestamps = 1;
            config.model_config.model_type = "whisper";
        }
        recognizer_ = runtime_.SherpaOnnxCreateOfflineRecognizer(&config);
        if (!recognizer_) return fail("recognizer");

        const auto vadPath = ModelAssets::path(folder, bundle[1]).toUtf8();
        SherpaOnnxVadModelConfig vadConfig{};
        // English can use a shorter endpoint. Preserve the acoustic context
        // for other and unknown Whisper languages.
        const float silenceDuration = model == Parakeet ? 0.25f : englishWhisper ? 0.16f : 0.20f;
        const float minimum = englishWhisper ? 0.16f : 0.20f;
        const float maximum = model == Parakeet ? 5.0f : englishWhisper ? 3.6f : 4.5f;
        vadConfig.silero_vad = {vadPath.constData(), 0.5f, silenceDuration, minimum, window, maximum};
        vadConfig.sample_rate = sampleRate;
        vadConfig.num_threads = 1;
        vadConfig.provider = "cpu";
        vad_ = runtime_.SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 16.0f);
        if (!vad_) return fail("vad");

        // Exercise both graphs before we accept real audio. Silence is never
        // returned to the parent as a recognized sentence.
        const std::array<float, sampleRate / 2> silence{};
        runtime_.SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, silence.data(), int(silence.size()));
        QElapsedTimer warmup;
        warmup.start();
        if (!decode(silence).valid) return fail("self-test");
        lastDecodeMs_ = warmup.elapsed();
        reset(0);
        return writeEvent({{"type", "ready"}, {"model", model}, {"threads", threads},
            {"version", QString::fromLatin1(runtimeVersion)}, {"loadMs", start.elapsed()}, {"cpuMs", processCpuMs()}});
    }

    void startRealtime(unsigned testDecodeDelayMs) {
        decoder_ = std::jthread([this, testDecodeDelayMs](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::unique_lock lock(pendingLock_);
                if (!pendingReady_.wait(lock, stop, [this] { return !pending_.empty() || preview_; }) ||
                    stop.stop_requested()) return;
                Segment segment;
                if (pending_.empty()) {
                    segment = std::move(*preview_);
                    preview_.reset();
                } else {
                    segment = std::move(pending_.front());
                    pending_.pop_front();
                }
                decoderBusy_ = true;
                lock.unlock();
                bool ok = true;
                if (sink::recordingClockUs() - segment.endUs <= maxPendingSegmentAgeUs) {
                    ok = decodeSegment(segment, testDecodeDelayMs);
                } else if (!segment.partial) {
                    ++skippedSegments_;
                }
                lock.lock();
                decoderBusy_ = false;
                if (!ok) return;
            }
        });
    }

    bool consumed(uint32_t bytes, bool stalePcm) {
        return writeEvent({{"type", "consumed"}, {"bytes", qint64(bytes)},
            {"stalePcm", stalePcm}, {"skippedSegments", qint64(skippedSegments_.exchange(0))}});
    }

    void reset(uint64_t epoch) {
        epoch_ = epoch;
        {
            std::lock_guard lock(pendingLock_);
            ++streamGeneration_;
            pending_.clear();
            preview_.reset();
        }
        resetVad();
    }

    bool gap() {
        // A short PCM gap is still the same source. Finish the available
        // utterance and keep completed speech waiting for the decoder.
        if (!flushInput()) return false;
        {
            std::lock_guard lock(pendingLock_);
            preview_.reset();
        }
        resetVad();
        return true;
    }

    void resetVad() {
        runtime_.SherpaOnnxVoiceActivityDetectorReset(vad_);
        vadInputSamples_ = 0;
        vadOriginUs_ = 0;
        haveOrigin_ = false;
        fill_ = 0;
        resetSegment();
        noisePowers_.fill(0.0f);
        noiseWindows_ = 0;
        noiseFloor_ = voicePower_ = 0.0f;
    }

    bool accept(std::span<const float> samples, int64_t playoutUs) {
        if (!haveOrigin_) {
            haveOrigin_ = true;
            vadOriginUs_ = playoutUs;
        }
        for (float pcm : samples) {
            buffer_[fill_++] = std::isfinite(pcm) ? std::clamp(pcm, -1.0f, 1.0f) : 0.0f;
            if (fill_ == buffer_.size()) {
                fill_ = 0;
                if (!acceptWindow()) return false;
            }
        }
        return true;
    }

    bool acceptFile(const QString& path) {
        QFile wav(path);
        if (!wav.open(QIODevice::ReadOnly) || wav.size() > 64 * 1024 * 1024) return fail("wav-file");
        auto bytes = wav.readAll();
        std::unique_ptr<const SherpaOnnxWave, decltype(runtime_.SherpaOnnxFreeWave)> wave(
            runtime_.SherpaOnnxReadWaveFromBinaryData(bytes.constData(), int32_t(bytes.size())),
            runtime_.SherpaOnnxFreeWave);
        if (!wave || wave->sample_rate != sampleRate || wave->num_samples <= 0) return fail("wav-format");
        return accept(std::span(wave->samples, size_t(wave->num_samples)), 0) && flushInput();
    }

    bool flushInput() {
        if (!haveOrigin_) return true;
        if (fill_) {
            std::fill(buffer_.begin() + fill_, buffer_.end(), 0.0f);
            fill_ = 0;
            if (!acceptWindow()) return false;
        }
        runtime_.SherpaOnnxVoiceActivityDetectorFlush(vad_);
        return drain();
    }

    bool fail(const char* reason, DWORD error = 0) const {
        writeEvent({{"type", "error"}, {"reason", QString::fromLatin1(reason)}, {"win32", qint64(error)}});
        return false;
    }

private:
    struct Segment {
        std::vector<float> samples;
        int64_t beginUs;
        int64_t endUs;
        uint64_t epoch;
        uint64_t generation;
        uint64_t id;
        bool partial = false;
        bool forced = false;
    };

    bool writeEvent(const QJsonObject& event) const {
        std::lock_guard lock(outputLock_);
        return resultLine(output_, event);
    }

    bool decodeSegment(const Segment& segment, unsigned testDecodeDelayMs) {
        QElapsedTimer time;
        time.start();
        if (testDecodeDelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(testDecodeDelayMs));
        if (languageGeneration_ != segment.generation ||
            (lastSpeechEndUs_ && segment.beginUs - lastSpeechEndUs_ > languageSilenceUs)) {
            languageGeneration_ = segment.generation;
            languageVotes_.clear();
            detectedLanguage_.clear();
        }
        const auto text = decode(segment.samples, detectedLanguage_);
        const auto decodeMs = time.elapsed();
        lastDecodeMs_ = decodeMs;
        if (segment.generation != streamGeneration_.load()) return true;
        const bool keep = text.valid && hasSpeechText(text.value);
        if (keep && !segment.partial) {
            lastSpeechEndUs_ = segment.endUs;
            if (!voteLanguage(text.language, segment.epoch)) return false;
        }
        QJsonObject event{{"type", segment.partial ? "partial" : "final"}, {"epoch", qint64(segment.epoch)},
            {"segment", qint64(segment.id)}, {"t0", segment.beginUs}, {"t1", segment.endUs},
            {"audioMs", qint64(segment.samples.size()) * 1000 / sampleRate},
            {"decodeMs", decodeMs}, {"cpuMs", processCpuMs()}, {"forced", segment.forced},
            {"text", keep ? text.value : QString{}}, {"lang", text.language}};
        if (keep && !text.tokenEnds.isEmpty()) event.insert("tokenEnds", text.tokenEnds);
        return writeEvent(event);
    }

    bool voteLanguage(const QString& language, uint64_t epoch) {
        if (model_ != Whisper || !language_.isEmpty() || !detectedLanguage_.isEmpty() || language.isEmpty())
            return true;
        languageVotes_ << language;
        if (languageVotes_.size() == 3) {
            for (const auto& vote : languageVotes_) {
                if (languageVotes_.count(vote) < 2) continue;
                detectedLanguage_ = vote;
                break;
            }
            languageVotes_.removeFirst();
        }
        return writeEvent({{"type", "language"}, {"epoch", qint64(epoch)}, {"candidate", language},
            {"locked", detectedLanguage_}});
    }

    struct Text {
        bool valid = false;
        QString value;
        QString language;
        QJsonArray tokenEnds;
    };

    Text decode(std::span<const float> samples, const QString& detected = {}) const {
        std::unique_ptr<const SherpaOnnxOfflineStream, decltype(runtime_.SherpaOnnxDestroyOfflineStream)> stream(
            runtime_.SherpaOnnxCreateOfflineStream(recognizer_), runtime_.SherpaOnnxDestroyOfflineStream);
        if (!stream) return {};
        if (!detected.isEmpty()) runtime_.SherpaOnnxOfflineStreamSetOption(stream.get(), "language", detected.toUtf8().constData());
        runtime_.SherpaOnnxAcceptWaveformOffline(stream.get(), sampleRate, samples.data(), int32_t(samples.size()));
        runtime_.SherpaOnnxDecodeOfflineStream(recognizer_, stream.get());
        std::unique_ptr<const SherpaOnnxOfflineRecognizerResult, decltype(runtime_.SherpaOnnxDestroyOfflineRecognizerResult)> text(
            runtime_.SherpaOnnxGetOfflineStreamResult(stream.get()), runtime_.SherpaOnnxDestroyOfflineRecognizerResult);
        if (!text) return {};
        QString language = language_.isEmpty() ? detected : language_;
        if (language.isEmpty() && text->lang) {
            language = QString::fromUtf8(text->lang).remove(QStringLiteral("<|")).remove(QStringLiteral("|>"));
        }
        auto value = QString::fromUtf8(text->text).simplified().left(2048);
        const auto seconds = float(samples.size()) / sampleRate;
        auto ends = model_ == Parakeet ? tokenEnds(*text, value, seconds) : segmentEnds(*text, value, seconds);
        return {true, value, language.left(15), std::move(ends)};
    }

    static QJsonArray tokenEnds(const SherpaOnnxOfflineRecognizerResult& text, const QString& value, float seconds) {
        if (!text.timestamps || !text.durations || !text.tokens_arr || text.count <= 0 || text.count > 256)
            return {};
        QJsonArray ends;
        QString formed;
        for (int i = 0; i < text.count; ++i) {
            auto token = QString::fromUtf8(text.tokens_arr[i]).replace(u'▁', u' ');
            formed += token;
            if (std::none_of(token.begin(), token.end(), [](QChar ch) { return ch.isLetterOrNumber(); })) continue;
            const auto end = text.timestamps[i] + text.durations[i];
            if (std::isfinite(end) && end > 0 && end < seconds + 0.1f)
                ends.append(QJsonArray{int(formed.simplified().size()), int(std::min(end, seconds) * 1000)});
        }
        return formed.simplified() == value ? ends : QJsonArray{};
    }

    static QJsonArray segmentEnds(const SherpaOnnxOfflineRecognizerResult& text, const QString& value, float seconds) {
        if (!text.segment_timestamps || !text.segment_durations || !text.segment_texts_arr ||
            text.segment_count <= 0 || text.segment_count > 32) return {};
        qsizetype from = 0;
        QJsonArray ends;
        for (int i = 0; i < text.segment_count; ++i) {
            const auto line = QString::fromUtf8(text.segment_texts_arr[i]).simplified();
            if (line.isEmpty()) continue;
            const auto at = value.indexOf(line, from);
            if (at < 0) return {};
            from = at + line.size();
            qsizetype wordEnd = line.size();
            while (wordEnd > 0 && !line[wordEnd - 1].isLetterOrNumber()) --wordEnd;
            const auto time = text.segment_timestamps[i] + text.segment_durations[i];
            if (wordEnd && std::isfinite(time) && time > 0 && time <= seconds + 0.10f)
                ends.append(QJsonArray{int(at + wordEnd), int(std::min(seconds, time) * 1000)});
        }
        return ends;
    }

    bool quietPause() {
        const auto power = windowPower(buffer_);
        noisePowers_[noiseWindows_ % noiseTrackingWindows] = power;
        ++noiseWindows_;
        if (noiseWindows_ >= 24 && noiseWindows_ % 4 == 0) {
            auto sorted = noisePowers_;
            const auto count = std::min<size_t>(noiseWindows_, sorted.size());
            std::sort(sorted.begin(), sorted.begin() + count);
            noiseFloor_ = sorted[count / 8];
            voicePower_ = sorted[count * 9 / 10];
        }
        // Only cut on an energy valley when the recent signal has distinct
        // foreground peaks over the learned bed. Keep low-contrast speech
        // and transient effects on the Silero path.
        const bool quiet = noiseFloor_ > 0.0f && voicePower_ > noiseFloor_ * 12.0f &&
            power <= noiseFloor_ * 3.0f;
        quietWindows_ = quiet ? quietWindows_ + 1 : 0;
        return speechWindows_ >= minSpeechWindows_ && quietWindows_ >= quietPauseWindows_;
    }

    static float windowPower(std::span<const float> samples) {
        float power = 0.0f;
        for (const auto pcm : samples) power += pcm * pcm;
        return power;
    }

    void resetSegment() {
        speechWindows_ = 0;
        quietWindows_ = 0;
        nextPreviewAt_ = 0;
        previewSamples_.clear();
        ++segmentId_;
    }

    static int64_t sampleUs(int64_t samples) { return samples * 1000000 / sampleRate; }

    void trackPreview(std::span<const float> samples, bool speech) {
        if (model_ != Parakeet) return;
        previewSamples_.insert(previewSamples_.end(), samples.begin(), samples.end());
        if (!speech && previewSamples_.size() > previewPadding)
            previewSamples_.erase(previewSamples_.begin(), previewSamples_.end() - previewPadding);
    }

    void enqueuePreview() {
        if (model_ != Parakeet || !decoder_.joinable() || lastDecodeMs_.load() > previewBudgetMs) return;
        const auto end = vadOriginUs_ + sampleUs(vadInputSamples_);
        if (!nextPreviewAt_) nextPreviewAt_ = end + previewIntervalMs * 1000;
        if (end < nextPreviewAt_) return;
        nextPreviewAt_ = end + previewIntervalMs * 1000;
        {
            std::lock_guard lock(pendingLock_);
            if (decoderBusy_ || !pending_.empty() || preview_) return;
            preview_ = Segment{previewSamples_, end - sampleUs(previewSamples_.size()), end,
                epoch_, streamGeneration_.load(), segmentId_, true};
        }
        pendingReady_.notify_one();
    }

    bool acceptWindow() {
        runtime_.SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, buffer_.data(), int(buffer_.size()));
        vadInputSamples_ += buffer_.size();
        const bool speech = runtime_.SherpaOnnxVoiceActivityDetectorDetected(vad_);
        speechWindows_ = speech ? speechWindows_ + 1 : 0;
        const bool pause = quietPause();
        if (pause || speechWindows_ >= maxSpeechWindows_) {
            runtime_.SherpaOnnxVoiceActivityDetectorFlush(vad_);
            return drain(true, pause);
        }
        if (!drain()) return false;
        trackPreview(buffer_, speech);
        if (speech) enqueuePreview();
        return true;
    }

    bool submit(Segment segment) {
        if (!decoder_.joinable()) return decodeSegment(segment, 0);
        // Keep short consecutive lines in order while a previous line is
        // decoding. Discard old work only when the queue reaches its bound.
        {
            std::lock_guard lock(pendingLock_);
            preview_.reset();
            if (pending_.size() >= maxPendingSegments) {
                pending_.pop_front();
                ++skippedSegments_;
            }
            pending_.push_back(std::move(segment));
        }
        pendingReady_.notify_one();
        return true;
    }

    static int forceCut(std::span<const float> samples) {
        int cut = int(samples.size()) - cutOverlapWindows * window;
        float minimum = std::numeric_limits<float>::max();
        // Test full 32-ms windows in the last second, leaving some audio on
        // both sides of the boundary so the next decode can recover a word.
        for (int back = cutOverlapWindows + 2; back <= cutLookbackWindows; ++back) {
            const int offset = int(samples.size()) - back * window;
            if (offset < cutOverlapWindows * window) break;
            const auto power = windowPower(samples.subspan(offset - window, 3 * window));
            if (power < minimum) {
                minimum = power;
                cut = offset;
            }
        }
        return cut;
    }

    void restartAt(int64_t startUs, std::span<const float> replay) {
        runtime_.SherpaOnnxVoiceActivityDetectorReset(vad_);
        vadOriginUs_ = startUs;
        vadInputSamples_ = 0;
        for (size_t i = 0; i < replay.size(); i += window) {
            const auto part = replay.subspan(i, std::min<size_t>(window, replay.size() - i));
            runtime_.SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, part.data(), int(part.size()));
            vadInputSamples_ += part.size();
            const bool speech = runtime_.SherpaOnnxVoiceActivityDetectorDetected(vad_);
            speechWindows_ = speech ? speechWindows_ + 1 : 0;
            trackPreview(part, speech);
        }
    }

    bool drain(bool flush = false, bool pause = false) {
        while (!runtime_.SherpaOnnxVoiceActivityDetectorEmpty(vad_)) {
            std::unique_ptr<const SherpaOnnxSpeechSegment, decltype(runtime_.SherpaOnnxDestroySpeechSegment)> seg(
                runtime_.SherpaOnnxVoiceActivityDetectorFront(vad_), runtime_.SherpaOnnxDestroySpeechSegment);
            runtime_.SherpaOnnxVoiceActivityDetectorPop(vad_);
            if (!seg || seg->start < 0 || seg->n <= 0) continue;
            const auto beginUs = vadOriginUs_ + sampleUs(seg->start);
            const std::span samples(seg->samples, size_t(seg->n));
            const bool split = flush && !pause && seg->n > 2 * cutLookbackWindows * window;
            const int cut = split ? forceCut(samples) : seg->n;
            const int end = split ? cut + cutOverlapWindows * window : seg->n;
            Segment segment{{samples.begin(), samples.begin() + end}, beginUs,
                beginUs + sampleUs(end), epoch_, streamGeneration_.load(), segmentId_, false, split};
            if (!submit(std::move(segment))) return false;
            resetSegment();
            if (split) {
                const int next = cut - cutOverlapWindows * window;
                restartAt(beginUs + sampleUs(next), samples.subspan(next));
            } else if (flush) {
                // Adaptive pauses are inferred from the mixed signal. Replay
                // their quiet tail in case it contains a soft consonant.
                const int next = pause ? std::max(0, seg->n - cutOverlapWindows * window) : seg->n;
                restartAt(beginUs + sampleUs(next), samples.subspan(next));
            }
        }
        return true;
    }

    HANDLE output_;
    Runtime runtime_;
    const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
    const SherpaOnnxVoiceActivityDetector* vad_ = nullptr;
    int model_ = Parakeet;
    QString language_;
    uint64_t epoch_ = 0, segmentId_ = 0;
    std::atomic<uint64_t> streamGeneration_{0};
    std::atomic<unsigned> skippedSegments_{0};
    std::atomic<qint64> lastDecodeMs_{0};
    uint64_t languageGeneration_ = 0;
    qint64 lastSpeechEndUs_ = 0;
    QStringList languageVotes_;
    QString detectedLanguage_;
    mutable std::mutex outputLock_;
    std::mutex pendingLock_;
    std::condition_variable_any pendingReady_;
    std::deque<Segment> pending_;
    std::optional<Segment> preview_;
    bool decoderBusy_ = false;
    std::jthread decoder_;
    std::array<float, window> buffer_{};
    size_t fill_ = 0;
    int speechWindows_ = 0, maxSpeechWindows_ = 0, quietWindows_ = 0;
    int minSpeechWindows_ = 0, quietPauseWindows_ = 0;
    size_t noiseWindows_ = 0;
    std::array<float, noiseTrackingWindows> noisePowers_{};
    float noiseFloor_ = 0.0f, voicePower_ = 0.0f;
    qint64 vadInputSamples_ = 0, vadOriginUs_ = 0, nextPreviewAt_ = 0;
    bool haveOrigin_ = false;
    std::vector<float> previewSamples_;
};

bool validHeader(const FrameHeader& header) {
    return header.magic == frameMagic && !header.reserved && header.bytes <= maxPayload;
}
}

int runWorker(unsigned testDecodeDelayMs) {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE) return 2;
    FrameHeader header;
    if (!readBytes(input, &header, sizeof(header))) return 0;
    if (!validHeader(header) || header.type != FrameType::Config || !header.bytes) return 2;
    QByteArray config(header.bytes, Qt::Uninitialized);
    if (!readBytes(input, config.data(), header.bytes)) return 0;
    const auto settings = QJsonDocument::fromJson(config).object();
    Recognizer worker(output);
    if (!worker.initialize(settings.value("folder").toString(), settings.value("model").toInt(-1),
                           settings.value("language").toString(), settings.value("threads").toInt(1))) return 3;
    uint64_t epoch = header.epoch;
    worker.reset(epoch);
    worker.startRealtime(testDecodeDelayMs);
    bool waitingForFreshPcm = false;
    std::array<float, maxPayload / sizeof(float)> pcm{};
    while (readBytes(input, &header, sizeof(header))) {
        if (!validHeader(header)) return 2;
        switch (header.type) {
        case FrameType::Quit: return header.bytes ? 2 : 0;
        case FrameType::Reset:
            if (header.bytes || header.epoch <= epoch) return 2;
            epoch = header.epoch;
            worker.reset(epoch);
            waitingForFreshPcm = false;
            break;
        case FrameType::Gap:
            if (header.bytes || header.epoch != epoch) return 2;
            if (!worker.gap()) return 0;
            waitingForFreshPcm = false;
            break;
        case FrameType::Audio: {
            if (!header.bytes || header.bytes % sizeof(float) || header.epoch != epoch) return 2;
            if (!readBytes(input, pcm.data(), header.bytes)) return 0;
            const bool stale = header.playoutUs < sink::recordingClockUs() - maxAudioAgeUs;
            if (stale) {
                if (!waitingForFreshPcm && !worker.gap()) return 0;
                waitingForFreshPcm = true;
            } else {
                waitingForFreshPcm = false;
                if (!worker.accept(std::span(pcm.data(), header.bytes / sizeof(float)), header.playoutUs)) return 0;
            }
            if (!worker.consumed(header.bytes, stale)) return 0;
            break;
        }
        default: return 2;
        }
    }
    return 0;
}

int runFile(int argc, wchar_t** argv) {
    if (argc < 7 || argc % 2 == 0) return 2;
    const QString input = QString::fromWCharArray(argv[2]);
    QString directory, language;
    int model = -1;
    int threads = std::clamp(QThread::idealThreadCount() / 4, 1, 4);
    for (int i = 3; i < argc; i += 2) {
        const QString key = QString::fromWCharArray(argv[i]);
        const QString value = QString::fromWCharArray(argv[i + 1]);
        if (key == QLatin1String("--asr-data-dir")) directory = value;
        else if (key == QLatin1String("--asr-model")) {
            if (value == QLatin1String("parakeet")) model = Parakeet;
            else if (value == QLatin1String("whisper")) model = Whisper;
            else return 2;
        } else if (key == QLatin1String("--asr-lang")) language = value;
        else if (key == QLatin1String("--asr-threads")) threads = value.toInt();
        else return 2;
    }
    if (directory.isEmpty() || model == -1) return 2;
    Recognizer worker(GetStdHandle(STD_OUTPUT_HANDLE));
    if (!worker.initialize(root(directory), model, language, threads)) return 3;
    return worker.acceptFile(input) ? 0 : 4;
}
}
