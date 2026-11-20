#include "../../src/ui/LiveTranslation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

// On-screen text (capture card / PS5 subtitles): the bridge hands over the engine's
// settled line. Needs no Live Captions and no OCR language: the line is supplied here.
void screenChecks(const QString& root) {
    LiveTranslation translation(root);
    translation.setCaptionSource(LiveTranslation::ScreenText);
    check(translation.captionSource() == LiveTranslation::ScreenText && translation.sourceSupported(),
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
    translation.acceptScreenText(QString(), true, QString());
    check(translation.caption().isEmpty() && translation.translated().isEmpty(), "line gone from the picture clears it");
    translation.acceptScreenText(QStringLiteral("The bridge is closed."), true, QString());
    check(QTest::qWaitFor([&] { return !translation.translated().isEmpty(); }, 3000) && requests == 1,
          "a repeated line is shown from the recent translations without a new request");
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

// The reported case: a line leaves while its translation is still on the way (a slow
// CPU model), then the next line comes. The next one must be translated, the first
// request must not be aborted (the model would go on with it and hold up the next),
// and its late reply is kept for when the line comes back.
void secondLineChecks(const QString& root) {
    LiveTranslation translation(root);
    translation.setCaptionSource(LiveTranslation::ScreenText);
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
            const bool first = data.mid(end + 4, n).contains("Line one");
            order << (first ? QStringLiteral("one") : QStringLiteral("two"));
            const QByteArray json = first ? QByteArray(R"({"choices":[{"message":{"content":"السطر الأول"}}]})")
                                          : QByteArray(R"({"choices":[{"message":{"content":"السطر الثاني"}}]})");
            QTimer::singleShot(first ? 1500 : 100, socket, [socket, json] {
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
    check(QTest::qWaitFor([&] { return translation.translated() == QString::fromUtf8("السطر الثاني"); }, 8000),
          "a second line after the first one left is translated");
    check(order == QStringList({QStringLiteral("one"), QStringLiteral("two")}),
          "the first request was not aborted and the second waited for it");
    translation.acceptScreenText(QString(), true, QString());
    translation.acceptScreenText(QStringLiteral("Line one is on screen."), true, QString());
    check(QTest::qWaitFor([&] { return translation.translated() == QString::fromUtf8("السطر الأول"); }, 3000)
          && order.size() == 2, "the first line's late reply was kept and is shown when it returns");
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
    const auto root = args.at(1);
    screenChecks(QDir(root).filePath(QStringLiteral("screen")));
    secondLineChecks(QDir(root).filePath(QStringLiteral("second-line")));
    if (!LiveTranslation::supported()) {
        std::puts("SKIP Live Captions unavailable on this Windows build");
        return failures ? 1 : 77;
    }
    apiChecks(QDir(root).filePath(QStringLiteral("api")));
    QFile settings(QDir(root).filePath(QStringLiteral("api/live-translation.ini")));
    check(settings.open(QIODevice::ReadOnly) && !settings.readAll().contains("fixture-secret"),
          "API key absent from saved preferences");
    if (args.size() == 3) localChecks(QDir(root).filePath(QStringLiteral("local")), args.at(2) == QStringLiteral("--download"));
    return failures ? 1 : 0;
}
