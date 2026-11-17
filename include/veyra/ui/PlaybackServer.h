#pragma once

#include <QObject>
#include <QTcpServer>

#include <memory>

namespace veyra::ui {

class QmlPlayerBridge;

// Local HTTP endpoint the browser extension talks to.
//
//   GET  /ping          -> {"ok":true,"app":"veyra"}   (extension uses it to detect Veyra)
//   POST /play          -> {"ok":true}                  body: {"url":"https://..."}
//
// Bound to 127.0.0.1 only, so nothing off this machine can reach it. The request
// body is forwarded to the bridge's openNetworkAddress() on the GUI thread - the
// socket handlers themselves run on the GUI thread too, but the call is still
// queued so a slow open never blocks the accept loop.
class PlaybackServer : public QObject {
    Q_OBJECT
public:
    explicit PlaybackServer(QmlPlayerBridge* bridge, QObject* parent = nullptr);
    ~PlaybackServer() override;

    bool listen(quint16 port = 0);

private:
    void onNewConnection();
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    static void respond(QTcpSocket* socket, int status, const QByteArray& body);

    QmlPlayerBridge* bridge_ = nullptr;
    QTcpServer server_;
};

} // namespace veyra::ui
