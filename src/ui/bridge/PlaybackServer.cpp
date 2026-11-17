#include "veyra/ui/PlaybackServer.h"

#include "veyra/Log.h"
#include "veyra/ui/QmlPlayerBridge.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTcpSocket>

#include <format>

namespace veyra::ui {
namespace {

// The extension is a fetch() caller, so replies need permissive CORS headers.
// The socket itself only listens on 127.0.0.1, so this cannot be reached from
// another machine.
constexpr const char* kCors = "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type\r\n";

} // namespace

PlaybackServer::PlaybackServer(QmlPlayerBridge* bridge, QObject* parent)
    : QObject(parent), bridge_(bridge) {
    connect(&server_, &QTcpServer::newConnection, this, &PlaybackServer::onNewConnection);
}

PlaybackServer::~PlaybackServer() { server_.close(); }

bool PlaybackServer::listen(quint16 port) {
    if (!server_.listen(QHostAddress::LocalHost, port)) {
        log::warn("playback-server", std::format("listen 127.0.0.1:{} failed: {}", port,
                                                 server_.errorString().toStdString()));
        return false;
    }
    log::info("playback-server", std::format("listening on 127.0.0.1:{}", server_.serverPort()));
    return true;
}

void PlaybackServer::respond(QTcpSocket* socket, int status, const QByteArray& body) {
    const char* reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : "Not Found";
    QByteArray head = std::format("HTTP/1.1 {} {}\r\n", status, reason).c_str();
    head += "Content-Type: application/json; charset=utf-8\r\n";
    head += std::format("Content-Length: {}\r\n", body.size()).c_str();
    head += "Connection: close\r\n";
    head += kCors;
    head += "\r\n";
    socket->write(head);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
}

void PlaybackServer::handleRequest(QTcpSocket* socket, const QByteArray& request) {
    const int headEnd = request.indexOf("\r\n\r\n");
    const QByteArray head = headEnd >= 0 ? request.left(headEnd) : request;
    const QByteArray body = headEnd >= 0 ? request.mid(headEnd + 4) : QByteArray();

    const QList<QByteArray> firstLine = head.split('\n').value(0).trimmed().split(' ');
    const QByteArray method = firstLine.value(0);
    const QByteArray path = firstLine.value(1);

    if (method == "OPTIONS") {
        respond(socket, 200, "{}");
        return;
    }
    if (method == "GET" && path == "/ping") {
        respond(socket, 200, R"({"ok":true,"app":"veyra"})");
        return;
    }
    if (method == "POST" && path == "/play") {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QString url = doc.object().value(QStringLiteral("url")).toString().trimmed();
        // Optional Bilibili quality (qn) from the extension; 0 keeps the default
        // 「best available」 request.
        const int quality = doc.object().value(QStringLiteral("quality")).toInt(0);
        // Browser-side session: without it Veyra would only see the renditions its
        // own stored cookie allows.
        const QString cookie = doc.object().value(QStringLiteral("cookie")).toString();
        if (url.isEmpty()) {
            respond(socket, 400, R"({"ok":false,"error":"missing url"})");
            return;
        }
        // Same path the 「播放地址」 box uses, so Bilibili page URLs, direct media
        // URLs and local files all behave identically.
        QMetaObject::invokeMethod(
            bridge_, [bridge = bridge_, url, quality, cookie] {
                bridge->bringToFront();
                bridge->openNetworkAddress(url, quality, cookie);
            },
            Qt::QueuedConnection);
        log::info("playback-server", std::format("play requested: quality={} url={}", quality, url.toStdString()));
        respond(socket, 200, R"({"ok":true})");
        return;
    }
    respond(socket, 404, R"({"ok":false,"error":"not found"})");
}

void PlaybackServer::onNewConnection() {
    while (auto* socket = server_.nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const int headEnd = buffer->indexOf("\r\n\r\n");
            if (headEnd < 0) return;
            int length = 0;
            for (const QByteArray& line : buffer->left(headEnd).split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (trimmed.startsWith("Content-Length:"))
                    length = trimmed.mid(15).trimmed().toInt();
            }
            if (buffer->size() < headEnd + 4 + length) return; // body still arriving
            const QByteArray request = buffer->left(headEnd + 4 + length);
            handleRequest(socket, request);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

} // namespace veyra::ui
