#include "veyra/ui/PlaybackServer.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int delivered=0, failed=0, checks=0;
    QString last; int quality=0;
    veyra::ui::PlaybackServer server([&](const QString& url,int q,const QString& cookie){
        ++delivered;last=url;quality=q;
        if(cookie.contains('\n'))++failed;
    });
    if(!server.listen(0)) return 1;
    const QByteArray host="127.0.0.1:"+QByteArray::number(server.serverPort());
    const QByteArray origin="chrome-extension://"+QByteArray(32,'a');
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::cerr<<name<<"\n";}};
    auto request=[&](QByteArray bytes,bool fragment=false){
        QTcpSocket socket;QEventLoop loop;QByteArray reply;
        QObject::connect(&socket,&QTcpSocket::connected,&loop,[&]{
            if(fragment){socket.write(bytes.left(17));QTimer::singleShot(5,&socket,[&]{socket.write(bytes.mid(17));});}
            else socket.write(bytes);
        });
        QObject::connect(&socket,&QTcpSocket::readyRead,&loop,[&]{reply+=socket.readAll();});
        QObject::connect(&socket,&QTcpSocket::disconnected,&loop,&QEventLoop::quit);
        QTimer::singleShot(2000,&loop,&QEventLoop::quit);
        socket.connectToHost("127.0.0.1",server.serverPort());loop.exec();
        app.processEvents();return reply;
    };
    auto post=[&](QByteArray body,QByteArray extra=QByteArray()){
        return QByteArray("POST /play HTTP/1.1\r\nHost: ")+host+"\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n"+extra+"\r\n"+body;
    };
    check(request("GET /ping HTTP/1.1\r\nHost: "+host+"\r\n\r\n").startsWith("HTTP/1.1 200"),"ping");
    auto body=QJsonDocument(QJsonObject{{"url","https://www.bilibili.com/video/BV1test00000"},{"quality",80},{"cookie","SESSDATA=test"}}).toJson(QJsonDocument::Compact);
    auto reply=request(post(body,"Origin: "+origin+"\r\n"),true);
    check(reply.startsWith("HTTP/1.1 200"),"fragmented extension request");
    check(reply.contains("Access-Control-Allow-Origin: "+origin),"scoped CORS");
    check(!reply.contains("Allow-Origin: *"),"no wildcard CORS");
    check(delivered==1&&quality==80&&last.startsWith("https://"),"dispatch exactly once");
    check(request(post(body,"Origin: https://evil.example\r\n")).startsWith("HTTP/1.1 403"),"web origin denied");
    check(request(post(body,"Origin: null\r\n")).startsWith("HTTP/1.1 403"),"null origin denied");
    check(request("GET /ping HTTP/1.1\r\nHost: evil.example\r\n\r\n").startsWith("HTTP/1.1 403"),"DNS rebinding denied");
    check(request(post(body,"Host: "+host+"\r\n")).startsWith("HTTP/1.1 400"),"duplicate host");
    check(request(post(body,"Content-Length: 0\r\n")).startsWith("HTTP/1.1 400"),"duplicate length");
    check(request("POST /play HTTP/1.1\r\nHost: "+host+"\r\n\r\n").startsWith("HTTP/1.1 411"),"missing length");
    check(request("POST /play HTTP/1.1\r\nHost: "+host+"\r\nContent-Length: 65537\r\n\r\n").startsWith("HTTP/1.1 413"),"bounded body");
    check(request("GET /ping HTTP/1.1\r\nHost: "+host+"\r\nX: "+QByteArray(9000,'a')).startsWith("HTTP/1.1 413"),"bounded headers");
    check(request(post(body,"Transfer-Encoding: chunked\r\n")).startsWith("HTTP/1.1 400"),"chunked denied");
    check(request(post("{bad")).startsWith("HTTP/1.1 400"),"invalid JSON");
    check(request(post(R"({"url":"file:///C:/Windows/win.ini"})")).startsWith("HTTP/1.1 400"),"file URL denied");
    check(request(post(R"({"url":"https://name:secret@example.com"})")).startsWith("HTTP/1.1 400"),"URL credentials denied");
    check(request(post(R"({"url":"https://example.com","cookie":"bad\r\nInjected: yes"})")).startsWith("HTTP/1.1 400"),"cookie injection denied");
    check(request(post(R"({"url":"https://example.com","quality":-1})")).startsWith("HTTP/1.1 400"),"quality range");
    check(delivered==1,"rejections never dispatch");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
