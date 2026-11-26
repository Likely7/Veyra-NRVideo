#pragma once

#include <QJsonObject>
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
//
// Capture control for local tools (a Stream Deck plugin, a script), served only
// after setControlHandlers():
//
//   POST /control/screenshot     -> {"ok":true}   same as the screenshot shortcut
//   POST /control/replay/save    -> {"ok":true}   same as the instant replay shortcut
//   POST /control/record/start   -> {"ok":true}
//   POST /control/record/stop    -> {"ok":true}
//   POST /control/record/toggle  -> {"ok":true}   same as the record shortcut
//   GET  /control/state          -> {"ok":true,...}  whatever the state handler returns
//
// A POST body is empty or a JSON object. {"ok":true} means the request was
// accepted and queued; the outcome is read from /control/state. These routes
// are for native clients: any request carrying an Origin header is refused, the
// extension's included, and no CORS headers are sent.
class PlaybackServer : public QObject {
    Q_OBJECT
public:
    using PlayHandler=std::function<void(const QString&)>;
    enum class ControlAction{Screenshot,ReplaySave,RecordStart,RecordStop,RecordToggle};
    using ControlHandler=std::function<void(ControlAction)>;
    using StateHandler=std::function<QJsonObject()>;
    explicit PlaybackServer(PlayHandler handler, QObject* parent = nullptr);
    ~PlaybackServer() override;

    // The control handler is queued like the play handler; the state handler is
    // called on the server's thread while the request is answered.
    void setControlHandlers(ControlHandler control, StateHandler state);

    bool listen(quint16 port = 0);
    quint16 serverPort() const { return server_.serverPort(); }

private:
    void onNewConnection();
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void handleControl(QTcpSocket* socket, const QByteArray& method, const QByteArray& path,
                       const QByteArray& origin, const QByteArray& body);
    static void respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& origin={});

    PlayHandler handler_;
    ControlHandler control_;
    StateHandler state_;
    QTcpServer server_;
    unsigned activeConnections_=0;
};

} // namespace veyra::ui
