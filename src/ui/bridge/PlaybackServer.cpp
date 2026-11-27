#include "veyra/ui/PlaybackServer.h"
#include "veyra/Log.h"
#include <QHostAddress>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <memory>
#include <utility>

namespace veyra::ui {
namespace {
constexpr qsizetype maxHeaders=8192,maxBody=65536;
bool extensionOrigin(const QByteArray& origin){
    constexpr auto prefix="chrome-extension://";
    if(!origin.startsWith(prefix))return false;
    const auto id=origin.mid(qsizetype(sizeof("chrome-extension://")-1));
    return id.size()==32&&std::all_of(id.begin(),id.end(),[](char c){return c>='a'&&c<='p';});
}
struct Pending{QByteArray bytes;qsizetype expected=-1;bool finished=false;};

bool validBearer(QByteArrayView authorization, QByteArrayView token) {
    constexpr QByteArrayView prefix("Bearer ");
    if (!authorization.startsWith(prefix) || authorization.size() != prefix.size() + token.size()) return false;
    unsigned difference = 0;
    for (qsizetype i = 0; i < token.size(); ++i)
        difference |= static_cast<unsigned char>(authorization[i + prefix.size()] ^ token[i]);
    return difference == 0;
}
}
PlaybackServer::PlaybackServer(PlayHandler handler,QObject* parent)
    :QObject(parent),handler_(std::move(handler)){
    server_.setMaxPendingConnections(32);
    connect(&server_,&QTcpServer::newConnection,this,&PlaybackServer::onNewConnection);
}
PlaybackServer::~PlaybackServer(){server_.close();}

void PlaybackServer::setControlHandlers(ControlHandler control, StateHandler state) {
    setControlEnabled(false);
    control_ = std::move(control);
    state_ = std::move(state);
    emit controlChanged();
}

bool PlaybackServer::setControlEnabled(bool enabled) {
    if (enabled == controlEnabled()) return true;
    if (enabled && !controlAvailable()) return false;
    ++controlGeneration_;
    controlPending_ = false;
    lastControlRead_.invalidate();
    lastControlWrite_.invalidate();
    if (enabled) {
        std::array<quint32, 8> random;
        QRandomGenerator::system()->fillRange(random.data(), random.size());
        controlToken_ = QByteArray(reinterpret_cast<const char*>(random.data()), sizeof(random)).toHex();
    } else {
        controlToken_.clear();
    }
    emit controlChanged();
    return true;
}

bool PlaybackServer::listen(quint16 port){
    if(!server_.listen(QHostAddress::LocalHost,port)){
        log::warn("playback-server",std::format("loopback listen failed: {}",server_.errorString().toStdString()));return false;
    }
    log::info("playback-server",std::format("listening on 127.0.0.1:{}",server_.serverPort()));
    emit controlChanged();
    return true;
}
void PlaybackServer::respond(QTcpSocket* socket,int status,const QByteArray& body,const QByteArray& origin){
    const char* reason=status==200?"OK":status==202?"Accepted":status==400?"Bad Request":status==403?"Forbidden":
        status==404?"Not Found":status==408?"Request Timeout":status==409?"Conflict":status==411?"Length Required":
        status==413?"Content Too Large":status==415?"Unsupported Media Type":status==429?"Too Many Requests":"Service Unavailable";
    QByteArray head=QByteArray::fromStdString(std::format("HTTP/1.1 {} {}\r\n",status,reason));
    head+="Content-Type: application/json; charset=utf-8\r\n";
    head+="Content-Length: "+QByteArray::number(body.size())+"\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if(extensionOrigin(origin)){
        head+="Access-Control-Allow-Origin: "+origin+"\r\nVary: Origin\r\n";
        head+="Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n";
    }
    socket->write(head+"\r\n"+body);socket->disconnectFromHost();
}
void PlaybackServer::handleRequest(QTcpSocket* socket,const QByteArray& request){
    const auto end=request.indexOf("\r\n\r\n");
    if(end<0){respond(socket,400,R"({"ok":false,"error":"invalid headers"})");return;}
    const auto lines=request.left(end).split('\n');
    const auto start=lines.value(0).trimmed().split(' ');
    QHash<QByteArray,QByteArray> headers;
    for(qsizetype i=1;i<lines.size();++i){
        const auto line=lines[i].trimmed();const auto colon=line.indexOf(':');
        if(colon<=0){respond(socket,400,R"({"ok":false,"error":"invalid header"})");return;}
        const auto name=line.left(colon).toLower(),value=line.mid(colon+1).trimmed();
        if(headers.contains(name)){respond(socket,400,R"({"ok":false,"error":"duplicate header"})");return;}
        headers.insert(name,value);
    }
    const auto origin=headers.value("origin"),host=headers.value("host").toLower();
    const auto port=QByteArray::number(server_.serverPort());
    if((!origin.isEmpty()&&!extensionOrigin(origin))||
       (host!="127.0.0.1:"+port&&host!="localhost:"+port)){
        respond(socket,403,R"({"ok":false,"error":"untrusted request origin or host"})");return;
    }
    if(start.size()!=3||(start[2]!="HTTP/1.1"&&start[2]!="HTTP/1.0")||headers.contains("transfer-encoding")){
        respond(socket,400,R"({"ok":false,"error":"invalid request framing"})",origin);return;
    }
    const auto& method=start[0];const auto& path=start[1];
    if (path.startsWith("/control/")) {
        if (headers.contains("origin") || headers.contains("sec-fetch-site") || headers.contains("sec-fetch-mode") ||
            headers.contains("sec-fetch-dest") || headers.contains("sec-fetch-user")) {
            respond(socket, 403, R"({"ok":false,"error":"native clients only"})");
            return;
        }
        handleControl(socket, method, path, headers.value("authorization"), headers.value("content-type"), request.mid(end + 4));
        return;
    }
    if(method=="OPTIONS"&&(path=="/play"||path=="/ping")){respond(socket,200,"{}",origin);return;}
    if(method=="GET"&&path=="/ping"){respond(socket,200,R"({"ok":true,"app":"veyra"})",origin);return;}
    if(method!="POST"||path!="/play"){respond(socket,404,R"({"ok":false,"error":"not found"})",origin);return;}
    if(headers.value("content-type").split(';').first().trimmed().toLower()!="application/json"){
        respond(socket,415,R"({"ok":false,"error":"expected application/json"})",origin);return;
    }
    QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(request.mid(end+4),&error);
    const auto object=doc.object();
    const auto url=object.value("url").toString().trimmed();
    const auto legacyCookie=object.value("cookie");
    const QUrl parsed(url,QUrl::StrictMode);
    if(error.error!=QJsonParseError::NoError||!doc.isObject()||!parsed.isValid()||
       (parsed.scheme()!="http"&&parsed.scheme()!="https")||parsed.host().isEmpty()||
       !parsed.userInfo().isEmpty()||url.size()>8192||
       (!legacyCookie.isUndefined() && legacyCookie != QJsonValue(QString()))){
        respond(socket,400,R"({"ok":false,"error":"invalid playback request"})",origin);return;
    }
    QMetaObject::invokeMethod(this,[handler=handler_,url]{if(handler)handler(url);},Qt::QueuedConnection);
    log::info("playback-server",std::format("play requested: host={}",parsed.host().toStdString()));
    respond(socket,200,R"({"ok":true})",origin);
}

void PlaybackServer::handleControl(QTcpSocket* socket, const QByteArray& method, const QByteArray& path,
                                   const QByteArray& authorization, const QByteArray& contentType,
                                   const QByteArray& body) {
    if (!controlEnabled()) { respond(socket, 404, R"({"ok":false,"error":"not found"})"); return; }
    if (!validBearer(authorization, controlToken_)) {
        respond(socket, 403, R"({"ok":false,"error":"control token required"})");
        return;
    }

    if (method == "GET" && path == "/control/state") {
        if (!body.isEmpty()) { respond(socket, 400, R"({"ok":false,"error":"unexpected body"})"); return; }
        if (lastControlRead_.isValid() && lastControlRead_.elapsed() < 250) {
            respond(socket, 429, R"({"ok":false,"error":"state rate limit"})");
            return;
        }
        lastControlRead_.start();
        auto state = state_();
        state.insert("ok", true);
        respond(socket, 200, QJsonDocument(state).toJson(QJsonDocument::Compact));
        return;
    }

    static constexpr std::pair<const char*, CaptureControlAction> routes[] = {
        {"/control/screenshot", CaptureControlAction::Screenshot},
        {"/control/record/start", CaptureControlAction::RecordStart},
        {"/control/record/stop", CaptureControlAction::RecordStop},
        {"/control/replay/save", CaptureControlAction::ReplaySave}
    };
    const auto route = std::find_if(std::begin(routes), std::end(routes), [&](const auto& entry) { return path == entry.first; });
    if (method != "POST" || route == std::end(routes)) { respond(socket, 404, R"({"ok":false,"error":"not found"})"); return; }
    if (body.size() > 128) { respond(socket, 413, R"({"ok":false,"error":"control body too large"})"); return; }
    if (!body.isEmpty()) {
        if (contentType.split(';').first().trimmed().toLower() != "application/json") {
            respond(socket, 415, R"({"ok":false,"error":"expected application/json"})");
            return;
        }
        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() || !doc.object().isEmpty()) {
            respond(socket, 400, R"({"ok":false,"error":"control does not take parameters"})");
            return;
        }
    }
    if (controlPending_ || (lastControlWrite_.isValid() && lastControlWrite_.elapsed() < 300)) {
        respond(socket, 429, R"({"ok":false,"error":"control rate limit"})");
        return;
    }

    lastControlWrite_.start();
    controlPending_ = true;
    const auto generation = controlGeneration_;
    QMetaObject::invokeMethod(this, [this, reply = QPointer<QTcpSocket>(socket), action = route->second, generation] {
        if (generation != controlGeneration_) {
            if (reply) respond(reply, 403, R"({"ok":false,"error":"control authorization revoked"})");
            return;
        }
        if (!reply || reply->state() != QAbstractSocket::ConnectedState) {
            controlPending_ = false;
            return;
        }
        const auto result = control_(action);
        if (generation == controlGeneration_) controlPending_ = false;
        if (!reply) return;
        switch (result) {
        case CaptureControlResult::Accepted:
            respond(reply, 202, R"({"ok":true,"result":"accepted"})"); break;
        case CaptureControlResult::Unchanged:
            respond(reply, 200, R"({"ok":true,"result":"unchanged"})"); break;
        case CaptureControlResult::Busy:
            respond(reply, 409, R"({"ok":false,"error":"capture busy"})"); break;
        case CaptureControlResult::Unavailable:
            respond(reply, 409, R"({"ok":false,"error":"capture unavailable"})"); break;
        }
    }, Qt::QueuedConnection);
}

void PlaybackServer::onNewConnection(){
    while(auto* socket=server_.nextPendingConnection()){
        if(activeConnections_>=32){
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            respond(socket,503,R"({"ok":false,"error":"busy"})");continue;
        }
        ++activeConnections_;socket->setReadBufferSize(maxHeaders+maxBody+5);
        auto pending=std::make_shared<Pending>();
        connect(socket,&QTcpSocket::disconnected,this,[this,socket]{--activeConnections_;socket->deleteLater();});
        QTimer::singleShot(8000,socket,[this,socket,pending]{
            if(!pending->finished){pending->finished=true;respond(socket,408,R"({"ok":false,"error":"request timeout"})");}
        });
        connect(socket,&QTcpSocket::readyRead,this,[this,socket,pending]{
            if(pending->finished)return;
            pending->bytes+=socket->readAll();
            const auto end=pending->bytes.indexOf("\r\n\r\n");
            auto reject=[&](int code,const char* body){pending->finished=true;respond(socket,code,body);};
            if((end<0&&pending->bytes.size()>maxHeaders)||end>maxHeaders||pending->bytes.size()>maxHeaders+maxBody+4){
                reject(413,R"({"ok":false,"error":"request too large"})");return;
            }
            if(end<0)return;
            if(pending->expected<0){
                qint64 length=0;bool seen=false;
                for(const auto& raw:pending->bytes.left(end).split('\n')){
                    const auto line=raw.trimmed();const auto at=line.indexOf(':');
                    if(at>0&&line.left(at).toLower()=="content-length"){
                        const auto value=line.mid(at+1).trimmed();bool ok=false;
                        if(seen||value.isEmpty()||!std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';})){
                            reject(400,R"({"ok":false,"error":"invalid content length"})");return;
                        }
                        length=value.toLongLong(&ok);seen=true;
                        if(!ok||length>maxBody){reject(413,R"({"ok":false,"error":"request too large"})");return;}
                    }
                }
                if(pending->bytes.startsWith("POST ")&&!seen){reject(411,R"({"ok":false,"error":"content length required"})");return;}
                pending->expected=end+4+qsizetype(length);
            }
            if(pending->bytes.size()<pending->expected)return;
            pending->finished=true;
            if(pending->bytes.size()!=pending->expected){respond(socket,400,R"({"ok":false,"error":"unexpected request data"})");return;}
            handleRequest(socket,pending->bytes);
        });
    }
}
}
