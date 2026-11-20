#include "../../src/ui/LiveTranslation.h"

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

void apiChecks(const QString& root) {
    LiveTranslation translation(root);
    check(!translation.enabled() && !translation.downloading(), "default startup has no live reader or download");
    translation.setProvider(LiveTranslation::OpenAi);
    translation.setApiBase(QStringLiteral("http://example.com/v1"));
    translation.setApiModel(QStringLiteral("fixture-model"));
    translation.setEnabled(true);
    check(!translation.enabled(), "unencrypted remote API rejected");

    QTcpServer service;
    if (!service.listen(QHostAddress::LocalHost)) { check(false, "loopback service listens"); return; }
    bool requestCorrect = false;
    const QByteArray json = R"({"choices":[{"message":{"content":"桥已关闭。请走下一个出口。"}}]})";
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
            requestCorrect = data.startsWith("POST /v1/chat/completions HTTP/1.1")
                && data.toLower().contains("authorization: bearer fixture-secret")
                && msg.value(QStringLiteral("model")).toString() == QStringLiteral("fixture-model")
                && data.contains("The bridge is closed.");
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
    check(QTest::qWaitFor([&] { return !translation.translated().isEmpty(); }, 9000)
          && requestCorrect, "real Qt request sends caption and returns a translated line");
    check(translation.translated() == QStringLiteral("桥已关闭。请走下一个出口。"), "Unicode reply decoded");
    translation.setPlayback(45, true, true);
    check(translation.caption().isEmpty() && translation.translated().isEmpty(), "source/seek reset clears live captions");
    translation.setPlayback(45, false, false);
    QTest::qWait(350);
    check(translation.caption().isEmpty(), "paused playback keeps live caption hidden");
    translation.setEnabled(false);
}

void localChecks(const QString& root, bool download) {
    LiveTranslation translation(root);
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
    if (args.size() < 2 || !QDir().mkpath(args.at(1))) return 2;
    if (!LiveTranslation::supported()) {
        std::puts("SKIP Live Captions unavailable on this Windows build");
        return 77;
    }
    const auto root = args.at(1);
    apiChecks(QDir(root).filePath(QStringLiteral("api")));
    QFile settings(QDir(root).filePath(QStringLiteral("api/live-translation.ini")));
    check(settings.open(QIODevice::ReadOnly) && !settings.readAll().contains("fixture-secret"),
          "API key absent from saved preferences");
    if (args.size() == 3) localChecks(QDir(root).filePath(QStringLiteral("local")), args.at(2) == QStringLiteral("--download"));
    return failures ? 1 : 0;
}
