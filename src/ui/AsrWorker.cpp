// SPDX-License-Identifier: GPL-3.0-only
#include "AsrWorker.h"
#include "AsrModels.h"
#include "AsrProtocol.h"
#include "../../third_party/sherpa-onnx/c-api.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QVector>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>

namespace veyra::ui::asr {
namespace {
constexpr int window = 512;
constexpr int maxSpeechWindows = 7 * sampleRate / window;

#define VEYRA_ASR_SYMBOLS(F) \
    F(SherpaOnnxGetVersionStr) \
    F(SherpaOnnxCreateOfflineRecognizer) \
    F(SherpaOnnxDestroyOfflineRecognizer) \
    F(SherpaOnnxCreateOfflineStream) \
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
            config.model_config.model_type = "whisper";
        }
        recognizer_ = runtime_.SherpaOnnxCreateOfflineRecognizer(&config);
        if (!recognizer_) return fail("recognizer");

        const auto vadPath = ModelAssets::path(folder, bundle[1]).toUtf8();
        SherpaOnnxVadModelConfig vadConfig{};
        vadConfig.silero_vad = {vadPath.constData(), 0.5f, 0.35f, 0.25f, window, 7.0f};
        vadConfig.sample_rate = sampleRate;
        vadConfig.num_threads = 1;
        vadConfig.provider = "cpu";
        vad_ = runtime_.SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 16.0f);
        if (!vad_) return fail("vad");

        // Exercise both graphs before we accept real audio. Silence is never
        // returned to the parent as a recognized sentence.
        const std::array<float, sampleRate / 2> silence{};
        runtime_.SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, silence.data(), int(silence.size()));
        if (!decode(silence).valid) return fail("self-test");
        reset(0);
        return resultLine(output_, {{"type", "ready"}, {"model", model}, {"threads", threads},
            {"version", QString::fromLatin1(runtimeVersion)}, {"loadMs", start.elapsed()}});
    }

    void reset(uint64_t epoch) {
        runtime_.SherpaOnnxVoiceActivityDetectorReset(vad_);
        epoch_ = epoch;
        received_ = 0;
        originUs_ = 0;
        vadOriginUs_ = 0;
        fill_ = 0;
        speechWindows_ = 0;
        speechPower_ = 0.0f;
    }

    bool accept(std::span<const float> samples, int64_t playoutUs) {
        if (!received_) originUs_ = vadOriginUs_ = playoutUs;
        for (float pcm : samples) {
            buffer_[fill_++] = std::isfinite(pcm) ? std::clamp(pcm, -1.0f, 1.0f) : 0.0f;
            ++received_;
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
        if (!accept(std::span(wave->samples, size_t(wave->num_samples)), 0)) return false;
        if (fill_) {
            std::fill(buffer_.begin() + fill_, buffer_.end(), 0.0f);
            if (!acceptWindow()) return false;
        }
        runtime_.SherpaOnnxVoiceActivityDetectorFlush(vad_);
        return drain();
    }

    bool fail(const char* reason, DWORD error = 0) const {
        resultLine(output_, {{"type", "error"}, {"reason", QString::fromLatin1(reason)}, {"win32", qint64(error)}});
        return false;
    }

private:
    struct Text {
        bool valid = false;
        QString value;
        QString language;
    };

    Text decode(std::span<const float> samples) const {
        std::unique_ptr<const SherpaOnnxOfflineStream, decltype(runtime_.SherpaOnnxDestroyOfflineStream)> stream(
            runtime_.SherpaOnnxCreateOfflineStream(recognizer_), runtime_.SherpaOnnxDestroyOfflineStream);
        if (!stream) return {};
        runtime_.SherpaOnnxAcceptWaveformOffline(stream.get(), sampleRate, samples.data(), int32_t(samples.size()));
        runtime_.SherpaOnnxDecodeOfflineStream(recognizer_, stream.get());
        std::unique_ptr<const SherpaOnnxOfflineRecognizerResult, decltype(runtime_.SherpaOnnxDestroyOfflineRecognizerResult)> text(
            runtime_.SherpaOnnxGetOfflineStreamResult(stream.get()), runtime_.SherpaOnnxDestroyOfflineRecognizerResult);
        if (!text) return {};
        QString language = language_;
        if (language.isEmpty() && text->lang) {
            language = QString::fromUtf8(text->lang).remove(QStringLiteral("<|")).remove(QStringLiteral("|>"));
        }
        return {true, QString::fromUtf8(text->text).simplified().left(2048), language.left(15)};
    }

    bool acceptWindow() {
        runtime_.SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, buffer_.data(), int(buffer_.size()));
        if (runtime_.SherpaOnnxVoiceActivityDetectorDetected(vad_)) {
            ++speechWindows_;
            float power = 0.0f;
            for (float pcm : buffer_) power += pcm * pcm;
            const bool quiet = speechWindows_ > (6 * sampleRate / window) && power < speechPower_ * 0.4f;
            speechPower_ = speechWindows_ == 1 ? power : speechPower_ * 0.95f + power * 0.05f;
            if (quiet || speechWindows_ >= maxSpeechWindows) {
                runtime_.SherpaOnnxVoiceActivityDetectorFlush(vad_);
                if (!drain()) return false;
                runtime_.SherpaOnnxVoiceActivityDetectorReset(vad_);
                vadOriginUs_ = originUs_ + received_ * 1000000 / sampleRate;
                speechWindows_ = 0;
                return true;
            }
        } else speechWindows_ = 0;
        return drain();
    }

    bool drain() {
        while (!runtime_.SherpaOnnxVoiceActivityDetectorEmpty(vad_)) {
            std::unique_ptr<const SherpaOnnxSpeechSegment, decltype(runtime_.SherpaOnnxDestroySpeechSegment)> seg(
                runtime_.SherpaOnnxVoiceActivityDetectorFront(vad_), runtime_.SherpaOnnxDestroySpeechSegment);
            runtime_.SherpaOnnxVoiceActivityDetectorPop(vad_);
            if (!seg || seg->start < 0 || seg->n <= 0) continue;
            QElapsedTimer time;
            time.start();
            const auto text = decode(std::span(seg->samples, size_t(seg->n)));
            const auto beginUs = vadOriginUs_ + seg->start * 1000000 / sampleRate;
            const auto endUs = beginUs + int64_t(seg->n) * 1000000 / sampleRate;
            const bool keep = text.valid && hasSpeechText(text.value);
            if (!resultLine(output_, {{"type", "final"}, {"epoch", qint64(epoch_)},
                {"t0", beginUs}, {"t1", endUs}, {"audioMs", int64_t(seg->n) * 1000 / sampleRate},
                {"decodeMs", time.elapsed()}, {"text", keep ? text.value : QString{}}, {"lang", text.language}})) return false;
        }
        return true;
    }

    HANDLE output_;
    Runtime runtime_;
    const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
    const SherpaOnnxVoiceActivityDetector* vad_ = nullptr;
    int model_ = Parakeet;
    QString language_;
    uint64_t epoch_ = 0;
    std::array<float, window> buffer_{};
    size_t fill_ = 0;
    int speechWindows_ = 0;
    float speechPower_ = 0.0f;
    int64_t received_ = 0, originUs_ = 0, vadOriginUs_ = 0;
};

bool validHeader(const FrameHeader& header) {
    return header.magic == frameMagic && !header.reserved && header.bytes <= maxPayload;
}
}

int runWorker() {
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
    std::array<float, maxPayload / sizeof(float)> pcm{};
    while (readBytes(input, &header, sizeof(header))) {
        if (!validHeader(header)) return 2;
        switch (header.type) {
        case FrameType::Quit: return header.bytes ? 2 : 0;
        case FrameType::Reset:
            if (header.bytes || header.epoch <= epoch) return 2;
            epoch = header.epoch;
            worker.reset(epoch);
            break;
        case FrameType::Audio:
            if (!header.bytes || header.bytes % sizeof(float) || header.epoch != epoch) return 2;
            if (!readBytes(input, pcm.data(), header.bytes)) return 0;
            if (!worker.accept(std::span(pcm.data(), header.bytes / sizeof(float)), header.playoutUs)) return 0;
            break;
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
