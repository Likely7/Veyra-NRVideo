#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTcpServer>
#include <functional>

#include <memory>

namespace veyra::ui {

enum class CaptureControlAction { Screenshot, RecordStart, RecordStop, ReplaySave };
enum class CaptureControlResult { Accepted, Unchanged, Busy, Unavailable };

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
    using ControlHandler = std::function<CaptureControlResult(CaptureControlAction)>;
    using StateHandler = std::function<QJsonObject()>;
    explicit PlaybackServer(PlayHandler handler, QObject* parent = nullptr);
    ~PlaybackServer() override;

    bool listen(quint16 port = 0);
    quint16 serverPort() const { return server_.serverPort(); }

    void setControlHandlers(ControlHandler control, StateHandler state);
    bool setControlEnabled(bool enabled);
    bool controlAvailable() const { return server_.isListening() && control_ && state_; }
    bool controlEnabled() const { return !controlToken_.isEmpty(); }
    QByteArray controlToken() const { return controlToken_; }

signals:
    void controlChanged();

private:
    void onNewConnection();
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void handleControl(QTcpSocket* socket, const QByteArray& method, const QByteArray& path,
                       const QByteArray& authorization, const QByteArray& contentType, const QByteArray& body);
    static void respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& origin={});

    PlayHandler handler_;
    QTcpServer server_;
    unsigned activeConnections_=0;
    ControlHandler control_;
    StateHandler state_;
    QByteArray controlToken_;
    quint64 controlGeneration_ = 0;
    bool controlPending_ = false;
    QElapsedTimer lastControlRead_, lastControlWrite_;
};

} // namespace veyra::ui
