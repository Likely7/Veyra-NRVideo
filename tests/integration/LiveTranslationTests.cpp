#include <winsock2.h>
#include "../../src/ui/LiveTranslation.h"
#include "../../src/ui/AsrProtocol.h"
#include "../../src/ui/AsrWorker.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <cstdio>

using veyra::ui::LiveTranslation;

namespace {
int failures = 0;
void check(bool passed, const char* name) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
}

// LiveTranslation launches its own executable for the OS reader. In this test
// only, that entry point supplies a reproducible caption over the same pipe.
int fixtureReader() {
    const auto mode = qgetenv("VEYRA_TEST_CAPTION_SCENARIO");
    for (int i = 0; i < 44; ++i) {
        if (mode == "zh") {
            std::puts(R"({"ready":true,"text":"請選擇簡體中文。"})");
        } else if (mode == "burst" && i >= 7) {
            std::puts(R"({"ready":true,"text":"The bridge is closed. Take the next exit. Drop this queued request. Use the middle road. This is the latest complete sentence. Please wait a little longer."})");
        } else {
            std::puts(R"({"ready":true,"text":"The bridge is closed. Take the next exit."})");
        }
        std::fflush(stdout);
        QThread::msleep(250);
    }
    return 0;
}


void sentenceChecks() {
    using veyra::ui::LiveSentences;
    LiveSentences sentences;
    auto output = sentences.appendFinal(QStringLiteral("Dr. Miller measured 3.14 meters. Move now!"), QStringLiteral("en"), 2000000, 100);
    check(output.size() == 2 && output[0].text == QStringLiteral("Dr. Miller measured 3.14 meters.")
          && output[1].text == QStringLiteral("Move now!"), "sentence boundaries preserve abbreviations and decimals");
    output = sentences.appendFinal(QStringLiteral("So am I. The office is in the U.S. We work with the U.S. Army."), "en", 2100000, 110);
    check(output.size() == 3 && output[0].text == QStringLiteral("So am I.") &&
          output[1].text == QStringLiteral("The office is in the U.S.") &&
          output[2].text == QStringLiteral("We work with the U.S. Army."),
          "terminal pronoun and acronym split without breaking proper names");
    output = sentences.appendFinal(QStringLiteral("Ask Dr. I. Brown to wait."), "en", 2200000, 120);
    check(output.size() == 1 && output[0].text == QStringLiteral("Ask Dr. I. Brown to wait."),
          "a personal initial stays with the name");
    const auto unpunctuated = sentences.appendFinal(QStringLiteral("Get down"), QStringLiteral("en"), 3000000, 200);
    const auto nextSpoken = sentences.appendFinal(QStringLiteral("Cover me"), QStringLiteral("en"), 3480000, 680);
    check(unpunctuated.size() == 1 && unpunctuated.front().text == QStringLiteral("Get down") &&
          nextSpoken.size() == 1 && nextSpoken.front().text == QStringLiteral("Cover me") && sentences.flush(1280).empty(),
          "rapid unpunctuated acoustic finals are submitted immediately and independently");
    sentences.reset();
    const QString first = QStringLiteral("A very long sentence in the live subtitle window. ");
    const QString prefix = first.repeated(8);
    output = sentences.updateSnapshot(prefix + QStringLiteral("The road is clover"), QStringLiteral("en"), 4000000, 1000);
    check(output.size() == 8, "Windows snapshot captures stable sentences beyond 300 characters");
    output = sentences.updateSnapshot(prefix + QStringLiteral("The road is closed."), QStringLiteral("en"), 4250000, 1250);
    check(output.empty() && sentences.flush(1849).empty(), "Windows leaves the revised tail pending");
    output = sentences.flush(1850);
    check(output.size() == 1 && output[0].text == QStringLiteral("The road is closed."), "revised tail finalizes once");
    output = sentences.updateSnapshot(first.repeated(4) + QStringLiteral("The road is closed. Use the exit."),
        QStringLiteral("en"), 5000000, 1900);
    const bool noRepeat = output.empty();
    output = sentences.flush(2500);
    check(noRepeat && output.size() == 1 && output[0].text == QStringLiteral("Use the exit."),
          "rolling window drop keeps committed sentence offsets");
    sentences.reset(QStringLiteral("Old sentence."));
    output = sentences.updateSnapshot(QStringLiteral("Old sentence. New sentence."), "en", 6000000, 2700);
    check(output.empty() && sentences.flush(3300).size() == 1,
          "seek baseline skips text retained by Windows");

    const auto packet = veyra::ui::asr::frame(veyra::ui::asr::FrameType::Audio, 41, 1234000, QByteArrayView("xyz", 3));
    veyra::ui::asr::FrameHeader header;
    std::memcpy(&header, packet.constData(), sizeof(header));
    check(header.magic == veyra::ui::asr::frameMagic && header.bytes == 3 && header.epoch == 41 &&
          header.playoutUs == 1234000 && packet.size() == 35, "worker audio protocol metadata round trips");
}


void speechHypothesisChecks() {
    using veyra::ui::LiveSpeechText;
    LiveSpeechText speech;
    auto result = speech.update(1, 1000000, 2000000, QStringLiteral("The bridge is closed."), false);
    check(result.committed.isEmpty() && result.draft == QStringLiteral("The bridge is closed."),
          "first interim hypothesis stays on the draft line");
    result = speech.update(1, 1000000, 3000000, QStringLiteral("The bridge is closed. Take the next"), false);
    check(result.committed == QStringLiteral("The bridge is closed.") && result.draft == QStringLiteral("Take the next"),
          "confirmed punctuation submits the first sentence before the final result");
    result = speech.update(1, 1000000, 3750000, QStringLiteral("The bridges are closed. Take the next exit."), true);
    check(result.committed == QStringLiteral("Take the next exit.") && result.draft.isEmpty(),
          "a final revision does not resend the early translation and replaces the draft");
    result = speech.update(2, 3450000, 6000000, QStringLiteral("next exit, then keep left."), true);
    check(result.committed == QStringLiteral("then keep left."), "audio replay at a cut drops matched duplicate words");
    result = speech.update(3, 6100000, 7000000, QStringLiteral("Keep left."), true);
    check(result.committed == QStringLiteral("Keep left."), "a repeat spoken outside the overlap is retained");

    speech.reset();
    speech.update(1, 1000000, 2000000, QStringLiteral("沿着这条路走到出口"), true);
    result = speech.update(2, 1800000, 3000000, QStringLiteral("到出口，然后左转。"), true);
    check(result.committed == QStringLiteral("然后左转。"), "overlap can align text without spaces");

    speech.reset();
    speech.update(1, 1000000, 2000000, QStringLiteral("Hold."), false);
    result = speech.update(1, 1000000, 3000000, QStringLiteral("Hold on. Try that door."), false);
    check(result.committed.isEmpty(), "interim punctuation that changes is not published");
    speech.reset();
    result = speech.update(2, 5000000, 6500000, QStringLiteral("Try that door."), true);
    check(result.committed == QStringLiteral("Try that door."), "stream reset clears interim history");

    veyra::ui::LiveSentences sentences;
    const auto timed = sentences.appendFinal(QStringLiteral("Go now. Wait here."), QStringLiteral("en"), 4500000, 100,
        {{6, 2300000}, {17, 3550000}});
    check(timed.size() == 2 && timed[0].wordEndUs == 2300000 && timed[1].wordEndUs == 3550000,
          "sentence end time follows its own final token within a recognition result");
}

void fallbackDownloadChecks(const QString& root, bool domestic, int model) {
    using namespace veyra::ui;
    QTcpServer unavailable;
    if (!unavailable.listen(QHostAddress::LocalHost)) { check(false, "test fallback endpoint listens"); return; }
    QObject::connect(&unavailable, &QTcpServer::newConnection, &unavailable, [&] {
        auto* socket = unavailable.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    const QByteArray rejected = "http://127.0.0.1:" + QByteArray::number(unavailable.serverPort()) + "/asset";
    const auto selected = asr::assets(model);
    std::vector<ModelAsset> manifest(selected.begin(), selected.end());
    for (auto& asset : manifest) {
        asset.url = rejected.constData();
        if (domestic) asset.fallbacks = asset.fallbacks.last(1);
    }
    const auto destination = asr::root(root);
    ModelAssets loader(destination, manifest);
    QStringList origins;
    QObject::connect(&loader, &ModelAssets::changed, &loader, [&] {
        if (!loader.downloading()) return;
        const auto status = loader.status();
        if (!origins.contains(status)) origins << status;
    });
    loader.download();
    const bool finished = QTest::qWaitFor([&] { return !loader.downloading(); }, std::chrono::seconds(238));
    check(finished && loader.installed() && asr::verify(destination, model, true),
          "failed primary falls back and installs only SHA-verified pinned assets");
    const auto labels = origins.join(QStringLiteral(" | "));
    check(labels.contains(QStringLiteral("gh-proxy.com")) &&
          labels.contains(domestic ? QStringLiteral("cdn.jsdelivr.net") : QStringLiteral("raw.githubusercontent.com")) &&
          labels.contains(domestic ? QStringLiteral("hf-mirror.com") : QStringLiteral("huggingface.co")),
          "worker exposes active download hosts for runtime, VAD and weights");
    std::printf("model fallback origins %s | status %s\n", qPrintable(labels), qPrintable(loader.status()));
}

void recognitionDownloadChecks(const QString& root, bool whisper) {
    LiveTranslation asr(root);
    check(asr.recognitionSource() == LiveTranslation::VeyraAudio && !asr.enabled(), "Veyra audio is the default source and remains opt-in");
    if (whisper) asr.setRecognitionModel(1);
    if (asr.recognitionInstalled()) { check(true, "pinned recognition model already installed"); return; }
    asr.downloadRecognitionModel();
    check(asr.recognitionDownloading(), "on-demand recognition download starts");
    const int selected = asr.recognitionModel();
    asr.setRecognitionModel(selected ? 0 : 1);
    check(asr.recognitionModel() == selected, "active download keeps the model selection unchanged");
    asr.cancelRecognitionDownload();
    check(!asr.recognitionDownloading() && !asr.recognitionInstalled(), "cancel discards partial recognition download");
    asr.downloadRecognitionModel();
    const bool finished = QTest::qWaitFor([&] { return !asr.recognitionDownloading(); }, std::chrono::seconds(235));
    check(finished && asr.recognitionInstalled(), "download verifies and installs pinned recognition runtime and model");
    std::printf("Download state %s\n", qPrintable(asr.recognitionStatus()));
}

void audioTapChecks() {
    using namespace veyra::ui::asr;
    AudioTap::Block block;
    auto tap = std::make_shared<AudioTap>();
    const int stream = 0;
    const int secondary = 1;
    std::array<float, 480 * 6> input{};
    auto format = veyra::sink::AudioFormat{6, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT};
    for (size_t frame = 0; frame < 480; ++frame) input[frame * 6 + 2] = 1.0f;
    tap->renderedPcm(input.data(), 480, format, 1000000, &stream);
    tap->renderedPcm(input.data(), 480, format, 1000000, &secondary);
    check(tap->pop(block) && block.frames == 480 && block.renderer == &stream
          && block.playoutUs == 1000000, "primary audio is copied with its playout timestamp");
    Resampler resampler;
    std::vector<float> output;
    check(resampler.convert(block, output) && output.size() >= 140 && output.size() <= 160 &&
          std::abs(output.at(output.size()/2) - 0.625f) < 0.01f,
          "48 kHz multichannel PCM resamples to 16 kHz with the centre weighted");
    check(!tap->pop(block), "simultaneous secondary renderer ignored");
    tap->renderedPcm(input.data(), 480, format, 1500000, &secondary);
    check(tap->pop(block) && block.renderer == &secondary, "new renderer recognized after a gap");
    for (int i = 0; i < 140; ++i) tap->renderedPcm(input.data(), 480, format, 2000000 + i * 10000, &secondary);
    check(tap->takeDrops() == 12, "audio tap queue is bounded under backpressure");
}


void stressChecks(const QString& root, const QString& file, int seconds, int extraMs) {
    using namespace veyra;
    QFile pcmFile(file);
    if (!pcmFile.open(QIODevice::ReadOnly)) { check(false, "stress PCM opens"); return; }
    const auto data = pcmFile.readAll();
    constexpr int frames = 480; // 10 ms at the real renderer input rate.
    if (data.isEmpty() || data.size() % (frames * sizeof(float))) {
        check(false, "stress PCM has complete float blocks");
        return;
    }
    std::vector<float> samples(size_t(data.size()) / sizeof(float));
    std::memcpy(samples.data(), data.constData(), size_t(data.size()));
    qputenv("VEYRA_TEST_ASR_DECODE_DELAY_MS", QByteArray::number(extraMs));
    ui::LiveAsr asr(root);
    asr.configure(ui::asr::Whisper, QStringLiteral("en"), 4);
    if (!asr.installed()) { check(false, "Whisper stress model installed"); return; }
    std::vector<qint64> arrivals;
    qint64 highestAge = 0;
    QElapsedTimer realtime;
    QObject::connect(&asr, &ui::LiveAsr::finalText, &asr, [&](const QString&, const QString&, qint64 endUs) {
        const auto age = (sink::recordingClockUs() - endUs) / 1000;
        highestAge = std::max(highestAge, age);
        arrivals.push_back(realtime.elapsed());
        std::printf("ASR_STRESS_RESULT wallMs=%lld ageMs=%lld\n", realtime.elapsed(), age);
        std::fflush(stdout);
    });
    asr.setPlayback(true, true);
    asr.setEnabled(true);
    if (!QTest::qWaitFor([]{ return sink::recordingAudioActive(); }, 18000)) {
        check(false, "Whisper stress worker starts");
        return;
    }
    const auto startUs = sink::recordingClockUs();
    const sink::AudioFormat mono{1, SPEAKER_FRONT_CENTER};
    int64_t blocks = 0;
    realtime.start();
    while (realtime.elapsed() < seconds * 1000) {
        const auto due = realtime.elapsed() / 10;
        auto tap = sink::recordingAudioSink();
        while (tap && blocks <= due) {
            const size_t offset = size_t(blocks) * frames % samples.size();
            tap->renderedPcm(samples.data() + offset, frames, mono, startUs + blocks * 10000, &mono);
            ++blocks;
        }
        QTest::qWait(4);
    }
    asr.setPlayback(false, true);
    asr.setEnabled(false);
    qint64 longestGap = 0, last = 0;
    for (const auto arrival : arrivals) {
        longestGap = std::max(longestGap, arrival - last);
        last = arrival;
    }
    longestGap = std::max(longestGap, qint64(seconds) * 1000 - last);
    std::printf("ASR_STRESS_SUMMARY duration=%d delay=%d results=%zu maxGapMs=%lld maxAgeMs=%lld\n",
        seconds, extraMs, arrivals.size(), longestGap, highestAge);
    check(arrivals.size() >= size_t(seconds / 7) && highestAge <= 4000 && longestGap <= 8200,
          "Whisper keeps producing fresh subtitles through sustained decode lag");
}

void apiChecks(const QString& root, const QByteArray& mode = {}) {
    LiveTranslation translation(root);
    check(!translation.enabled() && !translation.downloading(), "default startup has no live reader or download");
    translation.setRecognitionSource(LiveTranslation::WindowsCaptions);
    translation.setProvider(LiveTranslation::OpenAi);
    translation.setApiBase(QStringLiteral("http://example.com/v1"));
    translation.setApiModel(QStringLiteral("fixture-model"));
    translation.setEnabled(true);
    check(!translation.enabled(), "unencrypted remote API rejected");

    QTcpServer service;
    if (!service.listen(QHostAddress::LocalHost)) { check(false, "loopback service listens"); return; }
    bool requestCorrect = false;
    int requests = 0;
    bool waitingOnSecond = false, secondCancelled = false;
    QStringList received;

    QObject::connect(&service, &QTcpServer::newConnection, &service, [&] {
        auto* socket = service.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, data = QByteArray{}]() mutable {
            data += socket->readAll();
            const auto end = data.indexOf("\r\n\r\n");
            if (end < 0) return;
            const auto lines = data.first(end).split('\n');
            int n = 0;
            for (const auto& line : lines)
                if (line.toLower().startsWith("content-length:")) n = line.mid(15).trimmed().toInt();
            if (data.size() < end + 4 + n) return;
            const auto msg = QJsonDocument::fromJson(data.mid(end + 4, n)).object();
            ++requests;
            const auto text = msg.value(QStringLiteral("messages")).toArray().last().toObject().value(QStringLiteral("content")).toString();
            received << text;
            if (mode == "burst" && text == QLatin1String("Take the next exit.")) {
                waitingOnSecond = true;
                QObject::connect(socket, &QTcpSocket::disconnected, &service, [&]{ secondCancelled = true; });
                return;
            }
            requestCorrect = data.startsWith("POST /v1/chat/completions HTTP/1.1")
                && data.toLower().contains("authorization: bearer fixture-secret")
                && msg.value(QStringLiteral("model")).toString() == QStringLiteral("fixture-model")
                && (data.contains("The bridge is closed.") || data.contains("Take the next exit."));
            const QByteArray json = requests == 1
                ? R"({"choices":[{"message":{"content":"桥已关闭。"}}]})"
                : R"({"choices":[{"message":{"content":"请走下一个出口。"}}]})";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                + QByteArray::number(json.size()) + "\r\n\r\n" + json);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    translation.setApiBase(QStringLiteral("http://localhost:%1/v1").arg(service.serverPort()));
    translation.setApiKey(QStringLiteral("fixture-secret"));
    translation.setTargetLanguage(QStringLiteral("zh-CN"));
    if (!mode.isEmpty()) {
        if (mode == "zh") translation.setSourceLanguage(QStringLiteral("zh"));
        qputenv("VEYRA_TEST_CAPTION_SCENARIO", mode);
        translation.setPlayback(12, true, false);
        translation.setEnabled(true);
        if (mode == "burst") {
            check(QTest::qWaitFor([&]{ return waitingOnSecond; }, 2500), "second sentence is awaiting translation");
            const auto pair = translation.displayCaption();
            check(pair.original == QLatin1String("The bridge is closed.") && pair.translated == QStringLiteral("桥已关闭。"),
                  "pending new speech keeps the displayed translation paired with its original");
            check(QTest::qWaitFor([&]{ return secondCancelled && received.contains(QStringLiteral("This is the latest complete sentence.")); }, 4800),
                  "an evicted in-flight translation is cancelled and visible work advances");
            check(!received.contains(QStringLiteral("Drop this queued request.")),
                  "evicted queued sentences never reach the network");
        } else {
            check(QTest::qWaitFor([&]{ return !translation.translated().isEmpty(); }, 3500)
                  && translation.translated() == QStringLiteral("请选择简体中文。") && requests == 0,
                  "zh recognition maps to Simplified Chinese locally without a translation request");
        }
        translation.setEnabled(false);
        qunsetenv("VEYRA_TEST_CAPTION_SCENARIO");
        return;
    }
    translation.setEnabled(true);
    QTest::qWait(350);
    check(!requestCorrect && translation.translated().isEmpty(), "no text sent without active playback");
    translation.setPlayback(44, true, false);
    // Reopen the reader to simulate the first new sentence after playback starts.
    translation.setEnabled(false);
    translation.setEnabled(true);
    check(QTest::qWaitFor([&] { return translation.translated() == QStringLiteral("请走下一个出口。"); }, 9000)
          && requestCorrect, "real Qt request sends caption and returns a translated line");
    check(requests == 2 && translation.translated() == QStringLiteral("请走下一个出口。"),
          "two sentence IDs receive distinct Unicode replies");
    translation.setPlayback(45, true, true);
    check(translation.caption().isEmpty() && translation.translated().isEmpty(), "source/seek reset clears live captions");
    translation.setPlayback(45, false, false);
    QTest::qWait(350);
    check(translation.caption().isEmpty(), "paused playback keeps live caption hidden");
    translation.setEnabled(false);
    translation.setPlayback(46, true, true);
    translation.setEnabled(true);
    check(QTest::qWaitFor([&] { return translation.translated() == QStringLiteral("请走下一个出口。"); }, 3500)
          && requests == 2, "repeated sentences use the provider-language cache");
    translation.setEnabled(false);
}

// On-screen text (capture card / PS5 subtitles): the bridge hands over the engine's
// settled line. Needs no Live Captions and no OCR language: the line is supplied here.
void screenChecks(const QString& root) {
    LiveTranslation translation(root);
    translation.setRecognitionSource(LiveTranslation::ScreenText);
    check(translation.recognitionSource() == LiveTranslation::ScreenText && translation.sourceSupported(),
          "on-screen text source selectable");
    check(!translation.screenTextWanted(), "picture not read while translation is off");
    QTcpServer service;
    if (!service.listen(QHostAddress::LocalHost)) { check(false, "loopback service listens"); return; }
    int requests = 0;
    bool requestCorrect = false;
    const QByteArray json = R"({"choices":[{"message":{"content":"الجسر مغلق."}}]})";
    QObject::connect(&service, &QTcpServer::newConnection, &service, [&] {
        auto* socket = service.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, data = QByteArray{}]() mutable {
            data += socket->readAll();
            const auto end = data.indexOf("\r\n\r\n");
            if (end < 0) return;
            int n = 0;
            for (const auto& line : data.first(end).split('\n'))
                if (line.toLower().startsWith("content-length:")) n = line.mid(15).trimmed().toInt();
            if (data.size() < end + 4 + n) return;
            ++requests;
            requestCorrect = data.mid(end + 4, n).contains("The bridge is closed.");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                + QByteArray::number(json.size()) + "\r\n\r\n" + json);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    translation.setProvider(LiveTranslation::OpenAi);
    translation.setApiBase(QStringLiteral("http://localhost:%1/v1").arg(service.serverPort()));
    translation.setApiModel(QStringLiteral("fixture-model"));
    translation.setApiKey(QStringLiteral("fixture-secret"));
    translation.setTargetLanguage(QStringLiteral("ar"));
    translation.setEnabled(true);
    check(translation.enabled() && translation.screenTextWanted(), "enabled source asks the engine to read the picture");
    check(translation.screenTextLanguage() == QStringLiteral("en") && translation.screenTextCentered()
          && translation.screenTextRect().bottom() > 0.9, "auto source reads English in the centred bottom band");
    translation.setPlayback(7, true, false);
    translation.acceptScreenText(QString(), true, QString());
    translation.acceptScreenText(QStringLiteral("The bridge is closed."), true, QString());
    check(QTest::qWaitFor([&] { return !translation.translated().isEmpty(); }, 6000) && requests == 1 && requestCorrect,
          "on-screen line translated through the selected service");
    check(translation.translated() == QString::fromUtf8("الجسر مغلق."), "right-to-left reply decoded");
    check(translation.caption() == QStringLiteral("The bridge is closed."), "the line is shown with its translation");
    translation.acceptScreenText(QString(), true, QString());
    check(!translation.translated().isEmpty(), "a line gone from the picture keeps its translation a moment (time to read it)");
    check(QTest::qWaitFor([&] { return translation.caption().isEmpty() && translation.translated().isEmpty(); }, 5000),
          "then its translation is taken down");
    translation.acceptScreenText(QStringLiteral("The bridge is closed."), true, QString());
    check(!translation.translated().isEmpty() && requests == 1,
          "a repeated line is shown at once from the recent translations without a new request");
    translation.acceptScreenText(QString(), false, QStringLiteral("fixture OCR error"));
    check(translation.caption().isEmpty() && translation.status() == QStringLiteral("fixture OCR error"),
          "an OCR error clears the line and shows as the status");
    translation.setScreenRegion(LiveTranslation::CustomRegion);
    translation.setCustomRegion(QRectF(0.9, -1.0, 0.5, 2.0));
    const auto r = translation.screenTextRect();
    check(r.left() >= 0.0 && r.top() >= 0.0 && r.right() <= 1.0001 && r.bottom() <= 1.0001 && r.width() >= 0.05,
          "custom region kept inside the picture");
    translation.setEnabled(false);
    check(!translation.screenTextWanted(), "disabling stops reading the picture");
}

// The reported cases: a translator slower than the dialogue (a CPU model). A line leaves
// while its translation is still on the way and the next line comes. Before, the late
// translation was never shown and lines went untranslated; now each one is shown, for
// about as long as its line was up, and the request on its way is never aborted.
void secondLineChecks(const QString& root) {
    LiveTranslation translation(root);
    translation.setRecognitionSource(LiveTranslation::ScreenText);
    QTcpServer service;
    if (!service.listen(QHostAddress::LocalHost)) { check(false, "loopback service listens"); return; }
    QStringList order;
    QObject::connect(&service, &QTcpServer::newConnection, &service, [&] {
        auto* socket = service.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, data = QByteArray{}]() mutable {
            data += socket->readAll();
            const auto end = data.indexOf("\r\n\r\n");
            if (end < 0) return;
            int n = 0;
            for (const auto& line : data.first(end).split('\n'))
                if (line.toLower().startsWith("content-length:")) n = line.mid(15).trimmed().toInt();
            if (data.size() < end + 4 + n) return;
            const auto body = data.mid(end + 4, n);
            const QString which = body.contains("Line one") ? QStringLiteral("one")
                : body.contains("Line two") ? QStringLiteral("two") : QStringLiteral("three");
            order << which;
            const QByteArray json = which == QStringLiteral("one") ? QByteArray(R"({"choices":[{"message":{"content":"السطر الأول"}}]})")
                : which == QStringLiteral("two") ? QByteArray(R"({"choices":[{"message":{"content":"السطر الثاني"}}]})")
                                                 : QByteArray(R"({"choices":[{"message":{"content":"السطر الثالث"}}]})");
            QTimer::singleShot(which == QStringLiteral("one") ? 1500 : 100, socket, [socket, json] {
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(json.size()) + "\r\n\r\n" + json);
                socket->disconnectFromHost();
            });
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    translation.setProvider(LiveTranslation::OpenAi);
    translation.setApiBase(QStringLiteral("http://localhost:%1/v1").arg(service.serverPort()));
    translation.setApiModel(QStringLiteral("fixture-model"));
    translation.setTargetLanguage(QStringLiteral("ar"));
    translation.setEnabled(true);
    translation.setPlayback(9, true, false);
    translation.acceptScreenText(QString(), true, QString());
    translation.acceptScreenText(QStringLiteral("Line one is on screen."), true, QString(),
                                 QStringLiteral("Line one is on screen."), 5);
    check(translation.screenReading() == QStringLiteral("Line one is on screen.") && translation.screenWords() == 5,
          "the last read is offered as a preview");
    QTest::qWait(600);                                         // its translation is still on the way
    translation.acceptScreenText(QString(), true, QString());  // the line leaves first
    translation.acceptScreenText(QStringLiteral("Line two follows it."), true, QString());
    check(QTest::qWaitFor([&] { return translation.translated() == QString::fromUtf8("السطر الأول"); }, 4000)
          && translation.caption() == QStringLiteral("Line one is on screen."),
          "the first line's translation is shown although it came after the line left");
    check(QTest::qWaitFor([&] { return translation.translated() == QString::fromUtf8("السطر الثاني"); }, 8000),
          "then the second line's translation follows");
    check(order == QStringList({QStringLiteral("one"), QStringLiteral("two")}),
          "the first request was not aborted and the second waited for it");
    translation.acceptScreenText(QString(), true, QString());
    translation.acceptScreenText(QStringLiteral("Line one is on screen."), true, QString());
    check(QTest::qWaitFor([&] { return translation.translated() == QString::fromUtf8("السطر الأول"); }, 5000)
          && order.size() == 2, "a line that comes back is shown from the recent translations");

    // A line replaced while another is being translated is skipped: it has gone, and
    // translating it would only put the translator further behind the dialogue.
    translation.setEnabled(false);
    translation.setEnabled(true);
    translation.setPlayback(10, true, false);
    order.clear();
    translation.acceptScreenText(QString(), true, QString());
    translation.acceptScreenText(QStringLiteral("Line one again, slowly."), true, QString());
    QTest::qWait(300);
    translation.acceptScreenText(QStringLiteral("Line two, briefly."), true, QString());
    QTest::qWait(300);
    translation.acceptScreenText(QStringLiteral("Line three stays."), true, QString());
    check(QTest::qWaitFor([&] { return translation.translated() == QString::fromUtf8("السطر الثالث"); }, 8000)
          && order == QStringList({QStringLiteral("one"), QStringLiteral("three")}),
          "a translator behind the dialogue goes on with the line on screen now");
    translation.setEnabled(false);
}

void localChecks(const QString& root, bool download) {
    LiveTranslation translation(root);
    translation.setRecognitionSource(LiveTranslation::WindowsCaptions);
    translation.setTargetLanguage(QStringLiteral("zh-CN"));
    if (download) {
        translation.downloadModel();
        check(translation.downloading(), "download starts on request");
        translation.cancelModelDownload();
        check(!translation.downloading() && !translation.modelInstalled(), "cancel atomically discards unfinished download");
        const auto begin = std::chrono::steady_clock::now();
        translation.downloadModel();
        check(QTest::qWaitFor([&] { return !translation.downloading(); }, std::chrono::seconds(220))
              && translation.modelInstalled(), "pinned runtime and GGUF downloaded and verified");
        std::printf("Download seconds %.2f (%s)\n",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(),
                    qPrintable(translation.status()));
    }
    if (!translation.modelInstalled()) {
        if (!download) check(false, "local model present in isolated fixture directory");
        return;
    }
    translation.setPlayback(1, true, false);
    const auto begin = std::chrono::steady_clock::now();
    translation.setEnabled(true);
    check(QTest::qWaitFor([&] { return !translation.translated().isEmpty(); }, 10000),
          "verified local server translates through the production request path");
    std::printf("Local caption %.2f s (%s) %s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(),
                qPrintable(translation.status()), qPrintable(translation.translated()));
    translation.setEnabled(false);
    check(translation.caption().isEmpty() && translation.translated().isEmpty(), "disabling clears local captions");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 2 && args.at(1) == QStringLiteral("--read-live-captions")) return fixtureReader();
    if (args.size() == 2 && args.at(1) == QStringLiteral("--asr-worker"))
        return veyra::ui::asr::runWorker(unsigned(std::clamp(qEnvironmentVariableIntValue("VEYRA_TEST_ASR_DECODE_DELAY_MS"), 0, 3000)));
    if (args.size() < 2 || !QDir().mkpath(args.at(1))) return 2;
    if (args.size() == 6 && args[2] == QStringLiteral("--asr-stress")) {
        stressChecks(args[1], args[3], std::clamp(args[4].toInt(), 15, 270), std::clamp(args[5].toInt(), 0, 3000));
        return failures ? 1 : 0;
    }
    sentenceChecks();
    speechHypothesisChecks();
    audioTapChecks();
    if (args.contains(QStringLiteral("--asr-fallback"))) {
        fallbackDownloadChecks(args.at(1), args.contains(QStringLiteral("--domestic")),
            args.contains(QStringLiteral("--whisper")) ? veyra::ui::asr::Whisper : veyra::ui::asr::Parakeet);
        return failures ? 1 : 0;
    }
    if (args.contains(QStringLiteral("--asr-download"))) {
        recognitionDownloadChecks(args.at(1), args.contains(QStringLiteral("--whisper")));
        return failures ? 1 : 0;
    }
    const auto root = args.at(1);
    screenChecks(QDir(root).filePath(QStringLiteral("screen")));
    secondLineChecks(QDir(root).filePath(QStringLiteral("second-line")));
    if (!LiveTranslation::windowsCaptionsSupported()) {
        std::puts("SKIP Live Captions unavailable on this Windows build");
        return failures ? 1 : 0;
    }
    apiChecks(QDir(root).filePath(QStringLiteral("api")));
    apiChecks(QDir(root).filePath(QStringLiteral("api-burst")), "burst");
    apiChecks(QDir(root).filePath(QStringLiteral("api-zh")), "zh");
    QFile settings(QDir(root).filePath(QStringLiteral("api/live-translation.ini")));
    check(settings.open(QIODevice::ReadOnly) && !settings.readAll().contains("fixture-secret"),
          "API key absent from saved preferences");
    if (args.size() == 3) localChecks(QDir(root).filePath(QStringLiteral("local")), args.at(2) == QStringLiteral("--download"));
    return failures ? 1 : 0;
}
