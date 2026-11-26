#include "veyra/ui/PlaybackServer.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>
#include <utility>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int delivered=0, failed=0, checks=0;
    QString last;
    veyra::ui::PlaybackServer server([&](const QString& url){++delivered;last=url;});
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
    auto body=QJsonDocument(QJsonObject{{"url","https://example.com/movie.mp4?a=1"}}).toJson(QJsonDocument::Compact);
    auto reply=request(post(body,"Origin: "+origin+"\r\n"),true);
    check(reply.startsWith("HTTP/1.1 200"),"fragmented extension request");
    check(reply.contains("Access-Control-Allow-Origin: "+origin),"scoped CORS");
    check(!reply.contains("Allow-Origin: *"),"no wildcard CORS");
    check(delivered==1&&last=="https://example.com/movie.mp4?a=1","dispatch exactly once");
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
    check(request(post(R"({"url":"https://example.com","cookie":"session=test"})")).startsWith("HTTP/1.1 400"),"credential transfer denied");
    check(delivered==1,"rejections never dispatch");
    check(request(post(R"({"url":"https://example.com/live.m3u8","quality":80,"cookie":""})")).startsWith("HTTP/1.1 200"),"legacy direct URL without credentials");
    check(delivered==2&&last=="https://example.com/live.m3u8","legacy direct URL delivered");

    // Capture control (/control/...): native local clients only.
    using Action=veyra::ui::PlaybackServer::ControlAction;
    QList<Action> actions;int stateReads=0;
    auto control=[&](QByteArray method,QByteArray path,QByteArray extra=QByteArray(),QByteArray body=QByteArray()){
        QByteArray bytes=method+" "+path+" HTTP/1.1\r\nHost: "+host+"\r\n"+extra;
        if(method=="POST")bytes+="Content-Length: "+QByteArray::number(body.size())+"\r\n";
        return request(bytes+"\r\n"+body);
    };
    auto jsonBody=[](const QByteArray& response){return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n")+4)).object();};
    check(control("GET","/control/state").startsWith("HTTP/1.1 404"),"control state absent without handlers");
    check(control("POST","/control/screenshot").startsWith("HTTP/1.1 404"),"control action absent without handlers");
    server.setControlHandlers([&](Action action){actions.push_back(action);},[&]{
        ++stateReads;
        return QJsonObject{{"replayEnabled",true},{"recording",QJsonObject{{"active",false}}},
                           {"replay",QJsonObject{{"seconds",42}}},{"lastScreenshot","C:/shots/a.png"}};
    });
    reply=control("GET","/control/state");
    auto state=jsonBody(reply);
    check(reply.startsWith("HTTP/1.1 200")&&reply.contains("Content-Type: application/json"),"control state");
    check(state.value("ok").toBool()&&state.value("replayEnabled").toBool()&&
          state.value("replay").toObject().value("seconds").toInt()==42&&
          state.value("recording").toObject().contains("active")&&!state.value("recording").toObject().value("active").toBool()&&
          state.value("lastScreenshot").toString()=="C:/shots/a.png","control state carries the handler's fields");
    check(stateReads==1&&actions.isEmpty(),"state read once and dispatches nothing");
    check(!reply.contains("Access-Control-"),"control state sends no CORS headers");
    const std::pair<const char*,Action> routes[]={
        {"/control/screenshot",Action::Screenshot},{"/control/replay/save",Action::ReplaySave},
        {"/control/record/start",Action::RecordStart},{"/control/record/stop",Action::RecordStop},
        {"/control/record/toggle",Action::RecordToggle}};
    for(const auto& [path,action]:routes){
        const auto before=actions.size();
        reply=control("POST",path);
        check(reply.startsWith("HTTP/1.1 200")&&jsonBody(reply).value("ok").toBool(),path);
        check(actions.size()==before+1&&actions.last()==action,"control action dispatched exactly once");
    }
    const auto accepted=actions.size();
    check(control("POST","/control/screenshot","Content-Type: application/json\r\n","{}").startsWith("HTTP/1.1 200")&&
          actions.size()==accepted+1&&actions.last()==Action::Screenshot,"control accepts a JSON object body");
    const auto dispatched=actions.size();
    check(control("POST","/control/screenshot",QByteArray(),"{bad").startsWith("HTTP/1.1 400"),"control invalid JSON");
    check(control("POST","/control/screenshot",QByteArray(),"[1]").startsWith("HTTP/1.1 400"),"control non-object body");
    reply=control("POST","/control/screenshot","Origin: "+origin+"\r\n");
    check(reply.startsWith("HTTP/1.1 403")&&!reply.contains("Access-Control-"),"control denies the extension origin");
    check(control("POST","/control/record/toggle","Origin: https://evil.example\r\n").startsWith("HTTP/1.1 403"),"control denies a web origin");
    check(control("POST","/control/replay/save","Origin: null\r\n").startsWith("HTTP/1.1 403"),"control denies a null origin");
    check(control("GET","/control/state","Origin: "+origin+"\r\n").startsWith("HTTP/1.1 403"),"control state denies an origin");
    check(request("POST /control/screenshot HTTP/1.1\r\nHost: evil.example\r\nContent-Length: 0\r\n\r\n").startsWith("HTTP/1.1 403"),"control DNS rebinding denied");
    check(request("POST /control/screenshot HTTP/1.1\r\nHost: "+host+"\r\n\r\n").startsWith("HTTP/1.1 411"),"control missing length");
    check(control("POST","/control/screenshot","Transfer-Encoding: chunked\r\n").startsWith("HTTP/1.1 400"),"control chunked denied");
    reply=control("OPTIONS","/control/screenshot");
    check(reply.startsWith("HTTP/1.1 404")&&!reply.contains("Access-Control-"),"control answers no preflight");
    check(control("GET","/control/screenshot").startsWith("HTTP/1.1 404"),"control action needs POST");
    check(control("POST","/control/state").startsWith("HTTP/1.1 404"),"control state needs GET");
    check(control("POST","/control/unknown").startsWith("HTTP/1.1 404"),"unknown control route");
    check(control("POST","/control/screenshot/").startsWith("HTTP/1.1 404"),"control route matched exactly");
    check(actions.size()==dispatched&&stateReads==1,"control rejections never dispatch");
    check(delivered==2,"control never reaches the play handler");
    check(request("GET /ping HTTP/1.1\r\nHost: "+host+"\r\n\r\n").startsWith("HTTP/1.1 200"),"ping unchanged with control handlers");
    check(request(post(body,"Origin: "+origin+"\r\n")).startsWith("HTTP/1.1 200")&&delivered==3,"play unchanged with control handlers");
    check(actions.size()==dispatched,"play never reaches the control handler");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
