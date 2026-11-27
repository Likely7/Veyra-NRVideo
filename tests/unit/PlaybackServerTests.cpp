#include "veyra/ui/PlaybackServer.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>

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

    using Action = veyra::ui::CaptureControlAction;
    using Result = veyra::ui::CaptureControlResult;
    QList<Action> actions;
    bool recording = false, busy = false;
    int stateReads = 0;
    server.setControlHandlers([&](Action action) {
        actions.append(action);
        switch (action) {
        case Action::Screenshot: return busy ? Result::Busy : Result::Accepted;
        case Action::RecordStart:
            if (recording) return Result::Unchanged;
            recording = true;
            return Result::Accepted;
        case Action::RecordStop:
            if (!recording) return Result::Unchanged;
            recording = false;
            return Result::Accepted;
        case Action::ReplaySave: return Result::Unavailable;
        }
        return Result::Unavailable;
    }, [&] {
        ++stateReads;
        return QJsonObject{{"recording", recording}};
    });

    const auto control = [&](QByteArray method, QByteArray path, QByteArray extra = {}, QByteArray data = {}) {
        return request(method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra +
                       "Content-Length: " + QByteArray::number(data.size()) + "\r\n\r\n" + data);
    };
    const auto delayWrite = [] {
        QEventLoop loop;
        QTimer::singleShot(360, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const QByteArray shot = "/control/screenshot", state = "/control/state";
    check(server.controlAvailable() && !server.controlEnabled() && server.controlToken().isEmpty(), "control is opt-in");
    check(control("POST", shot).startsWith("HTTP/1.1 404") && actions.isEmpty(), "disabled actions do not dispatch");
    check(control("GET", state).startsWith("HTTP/1.1 404") && stateReads == 0, "disabled state does not dispatch");
    check(server.setControlEnabled(true) && server.controlToken().size() == 64, "enable an independent 256-bit token");
    const auto token = server.controlToken();
    const QByteArray bearer = "Authorization: Bearer " + token + "\r\n";
    check(control("POST", shot).startsWith("HTTP/1.1 403"), "a missing token is denied");
    check(control("GET", state, "Authorization: Bearer wrong\r\n").startsWith("HTTP/1.1 403"), "state also requires a token");
    check(control("POST", shot, bearer + bearer).startsWith("HTTP/1.1 400"), "duplicate authorization is rejected");
    reply = control("OPTIONS", shot, "Origin: " + origin + "\r\n");
    check(reply.startsWith("HTTP/1.1 403") && !reply.contains("Access-Control-Allow-Origin"), "control never issues CORS approval");
    check(control("POST", shot, bearer + "Origin: \r\n").startsWith("HTTP/1.1 403"), "empty Origin is still refused");
    check(control("POST", shot, bearer + "Origin : https://example.invalid\r\n").startsWith("HTTP/1.1 400"), "malformed names are not allowed");
    check(control("POST", shot, bearer + "Origin: " + origin + "\r\n").startsWith("HTTP/1.1 403"), "extension origin refused with token");
    check(control("POST", shot, bearer + "Sec-Fetch-Site: same-origin\r\n").startsWith("HTTP/1.1 403"), "browser Fetch Metadata refused with token");
    check(request("POST " + shot + " HTTP/1.1\r\nHost: external.invalid\r\n" + bearer +
                  "Content-Length: 0\r\n\r\n").startsWith("HTTP/1.1 403"), "control requires a loopback Host");
    check(control("POST", shot, bearer + "Content-Type: application/json\r\n", R"({"path":"other.png"})").startsWith("HTTP/1.1 400"),
          "control cannot supply output paths");
    check(control("POST", shot, bearer, QByteArray(129, ' ')).startsWith("HTTP/1.1 413"), "control body is bounded");
    check(actions.isEmpty() && stateReads == 0, "security and framing rejections never reach handlers");

    reply = control("GET", state, bearer);
    check(reply.startsWith("HTTP/1.1 200") && reply.contains(R"("recording":false)") && !reply.contains(token), "authorized minimal state without token");
    check(control("GET", state, bearer).startsWith("HTTP/1.1 429") && stateReads == 1, "state burst is limited");
    check(control("POST", shot, bearer + "Content-Type: application/json\r\n", "{}").startsWith("HTTP/1.1 202") &&
          actions == QList<Action>{Action::Screenshot}, "accepted screenshot dispatches once");
    check(control("POST", shot, bearer).startsWith("HTTP/1.1 429") && actions.size() == 1, "write burst is limited");
    delayWrite();
    busy = true;
    check(control("POST", shot, bearer).contains(R"("error":"capture busy")"), "busy is not reported as accepted");
    delayWrite();
    check(control("POST", "/control/record/start", bearer).startsWith("HTTP/1.1 202") && recording, "record start accepted");
    delayWrite();
    check(control("POST", "/control/record/start", bearer).contains(R"("result":"unchanged")") && recording, "repeat start is idempotent");
    delayWrite();
    check(control("POST", "/control/record/stop", bearer).startsWith("HTTP/1.1 202") && !recording, "record stop accepted");
    delayWrite();
    check(control("POST", "/control/replay/save", bearer).contains(R"("error":"capture unavailable")"), "unavailable replay is not reported as accepted");
    check(control("POST", "/control/record/toggle", bearer).startsWith("HTTP/1.1 404"), "remote recording actions are explicit");

    check(server.setControlEnabled(false) && server.controlToken().isEmpty(), "disable erases the session token");
    const auto dispatched = actions.size();
    check(control("POST", shot, bearer).startsWith("HTTP/1.1 404") && actions.size() == dispatched, "disable revokes the credential");
    check(server.setControlEnabled(true) && server.controlToken() != token, "re-enabling rotates the token");
    check(control("POST", shot, bearer).startsWith("HTTP/1.1 403") && actions.size() == dispatched, "prior token is no longer valid");
    check(control("GET", state, "Authorization: bearer " + server.controlToken() + "\r\n").startsWith("HTTP/1.1 200"), "the current token works with a case-insensitive scheme");
    check(request(post(body,"Origin: "+origin+"\r\n")).startsWith("HTTP/1.1 200") && delivered == 3, "extension playback stays compatible");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
