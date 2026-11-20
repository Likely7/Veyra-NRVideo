#pragma once

#include <QObject>
#include <QTcpServer>
#include <functional>

#include <memory>

namespace veyra::ui {


// Local HTTP endpoint the browser extension talks to.
//
//   GET  /ping          -> {"ok":true,"app":"veyra"}   (extension uses it to detect Veyra)
//   POST /play          -> {"ok":true}                  body: {"url":"https://..."}
//
// Loopback only, bounded HTTP framing, extension-origin/Host validation; queued
// handler receives only validated HTTP(S) playback requests. No wildcard CORS.
class PlaybackServer : public QObject {
    Q_OBJECT
public:
    using PlayHandler=std::function<void(const QString&)>;
    explicit PlaybackServer(PlayHandler handler, QObject* parent = nullptr);
    ~PlaybackServer() override;

    bool listen(quint16 port = 0);
    quint16 serverPort() const { return server_.serverPort(); }

private:
    void onNewConnection();
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    static void respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& origin={});

    PlayHandler handler_;
    QTcpServer server_;
    unsigned activeConnections_=0;
};

} // namespace veyra::ui
