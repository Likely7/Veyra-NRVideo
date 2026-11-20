#include <winsock2.h>
#include "../../src/ui/LiveTranslation.h"
#include "../../src/ui/AsrProtocol.h"
#include "../../src/ui/AsrWorker.h"
#include <cmath>
#include <cstring>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>
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
    for (int i = 0; i < 44; ++i) {
        std::puts(R"({"ready":true,"text":"The bridge is closed. Take the next exit."})");
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
    check(sentences.appendFinal(QStringLiteral("A short fragment"), QStringLiteral("en"), 3000000, 200).empty()
          && sentences.flush(799).empty() && sentences.flush(800).size() == 1,
          "unpunctuated audio is finalized after 600 ms");
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

void recognitionDownloadChecks(const QString& root, bool whisper) {
    LiveTranslation asr(root);
    check(asr.recognitionSource() == LiveTranslation::VeyraAudio && !asr.enabled(), "Veyra audio is the default source and remains opt-in");
    if (whisper) asr.setRecognitionModel(1);
    if (asr.recognitionInstalled()) { check(true, "pinned recognition model already installed"); return; }
    asr.downloadRecognitionModel();
    check(asr.recognitionDownloading(), "on-demand recognition download starts");
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

void apiChecks(const QString& root) {
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
    if (args.size() == 2 && args.at(1) == QStringLiteral("--asr-worker")) return veyra::ui::asr::runWorker();
    if (args.size() < 2 || !QDir().mkpath(args.at(1))) return 2;
    sentenceChecks();
    audioTapChecks();
    if (args.contains(QStringLiteral("--asr-download"))) {
        recognitionDownloadChecks(args.at(1), args.contains(QStringLiteral("--whisper")));
        return failures ? 1 : 0;
    }
    if (!LiveTranslation::windowsCaptionsSupported()) {
        std::puts("SKIP Live Captions unavailable on this Windows build");
        return failures ? 1 : 0;
    }
    const auto root = args.at(1);
    apiChecks(QDir(root).filePath(QStringLiteral("api")));
    QFile settings(QDir(root).filePath(QStringLiteral("api/live-translation.ini")));
    check(settings.open(QIODevice::ReadOnly) && !settings.readAll().contains("fixture-secret"),
          "API key absent from saved preferences");
    if (args.size() == 3) localChecks(QDir(root).filePath(QStringLiteral("local")), args.at(2) == QStringLiteral("--download"));
    return failures ? 1 : 0;
}
