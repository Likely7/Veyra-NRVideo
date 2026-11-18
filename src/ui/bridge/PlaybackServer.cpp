#include "veyra/ui/PlaybackServer.h"
#include "veyra/Log.h"
#include <QHostAddress>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <format>
#include <memory>

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
}
PlaybackServer::PlaybackServer(PlayHandler handler,QObject* parent)
    :QObject(parent),handler_(std::move(handler)){
    server_.setMaxPendingConnections(32);
    connect(&server_,&QTcpServer::newConnection,this,&PlaybackServer::onNewConnection);
}
PlaybackServer::~PlaybackServer(){server_.close();}
bool PlaybackServer::listen(quint16 port){
    if(!server_.listen(QHostAddress::LocalHost,port)){
        log::warn("playback-server",std::format("loopback listen failed: {}",server_.errorString().toStdString()));return false;
    }
    log::info("playback-server",std::format("listening on 127.0.0.1:{}",server_.serverPort()));return true;
}
void PlaybackServer::respond(QTcpSocket* socket,int status,const QByteArray& body,const QByteArray& origin){
    const char* reason=status==200?"OK":status==400?"Bad Request":status==403?"Forbidden":
        status==404?"Not Found":status==408?"Request Timeout":status==411?"Length Required":
        status==413?"Content Too Large":status==415?"Unsupported Media Type":"Service Unavailable";
    QByteArray head=QByteArray::fromStdString(std::format("HTTP/1.1 {} {}\r\n",status,reason));
    head+="Content-Type: application/json; charset=utf-8\r\n";
    head+="Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n";
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
    if(method=="OPTIONS"&&(path=="/play"||path=="/ping")){respond(socket,200,"{}",origin);return;}
    if(method=="GET"&&path=="/ping"){respond(socket,200,R"({"ok":true,"app":"veyra"})",origin);return;}
    if(method!="POST"||path!="/play"){respond(socket,404,R"({"ok":false,"error":"not found"})",origin);return;}
    if(headers.value("content-type").split(';').first().trimmed().toLower()!="application/json"){
        respond(socket,415,R"({"ok":false,"error":"expected application/json"})",origin);return;
    }
    QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(request.mid(end+4),&error);
    const auto object=doc.object();
    const auto url=object.value("url").toString().trimmed(),cookie=object.value("cookie").toString();
    const int quality=object.value("quality").toInt(0);
    const QUrl parsed(url,QUrl::StrictMode);
    if(error.error!=QJsonParseError::NoError||!doc.isObject()||!parsed.isValid()||
       (parsed.scheme()!="http"&&parsed.scheme()!="https")||parsed.host().isEmpty()||
       !parsed.userInfo().isEmpty()||url.size()>8192||cookie.size()>32768||
       cookie.contains('\r')||cookie.contains('\n')||cookie.contains(QChar(0))||quality<0||quality>1000){
        respond(socket,400,R"({"ok":false,"error":"invalid playback request"})",origin);return;
    }
    QMetaObject::invokeMethod(this,[handler=handler_,url,quality,cookie]{if(handler)handler(url,quality,cookie);},Qt::QueuedConnection);
    // Never log session cookies, signed query strings or URL passwords.
    log::info("playback-server",std::format("play requested: quality={} host={}",quality,parsed.host().toStdString()));
    respond(socket,200,R"({"ok":true})",origin);
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
