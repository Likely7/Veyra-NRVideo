// SPDX-License-Identifier: GPL-3.0-only
// All HTTP replies are synthetic. No Sony endpoint, real account or console is contacted.
#include "veyra/ui/PsnService.h"
#include "veyra/ui/RemotePlayResume.h"
#include "veyra/remoteplay/ChiakiBackend.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>

using namespace veyra;
namespace veyra::ui {
struct PsnServiceTestAccess {
    static QUrl login(PsnService& p){return p.authorizationUrl();}
    static void expire(PsnService& p){p.account_.expiry=1;}
    static void expireLogin(PsnService& p){p.loginStarted_=1;}
    static QByteArray refresh(const PsnService& p){return p.account_.refresh;}
    static QVariantList consoles(const QJsonObject& o){return PsnService::parseConsoles(o);}
};
}
namespace {
int assertions=0;
void check(bool ok,const char* label){++assertions;if(!ok)throw std::runtime_error(label);}
struct Response {QByteArray body;int status=200;int delay=0;};
class Reply final:public QNetworkReply {
public:
    Reply(const QNetworkRequest& request,Response response,QObject* parent):QNetworkReply(parent),body_(std::move(response.body)){
        setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,response.status);
        open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        if(response.status>=400)setError(QNetworkReply::ContentAccessDenied,"synthetic failure");
        QTimer::singleShot(response.delay,this,[this]{if(done_)return;done_=true;emit readyRead();setFinished(true);emit finished();});
    }
    void abort() override {if(done_)return;done_=true;setError(OperationCanceledError,"synthetic cancel");setFinished(true);emit finished();}
    qint64 bytesAvailable() const override{return body_.size()-position_+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char* data,qint64 max) override{
        const auto amount=std::min(max,qint64(body_.size()-position_));if(amount<=0)return -1;
        std::memcpy(data,body_.constData()+position_,size_t(amount));position_+=amount;return amount;
    }
private:QByteArray body_;qint64 position_=0;bool done_=false;
};
class Network final:public QNetworkAccessManager {
public:
    struct Sent{QNetworkRequest request;QByteArray body;};
    std::deque<Response> responses;QList<Sent> sent;
    void json(const QJsonObject& o,int status=200,int delay=0){responses.push_back({QJsonDocument(o).toJson(QJsonDocument::Compact),status,delay});}
protected:
    QNetworkReply* createRequest(Operation,const QNetworkRequest& req,QIODevice* body) override {
        sent.append({req,body?body->readAll():QByteArray{}});
        // Unexpected traffic fails locally; there is deliberately no base-class fallback.
        Response response{"{}",500,0};if(!responses.empty()){response=std::move(responses.front());responses.pop_front();}
        return new Reply(req,std::move(response),this);
    }
};
QString redirect(const QUrl& login,const QString& code="fixture-code"){
    QUrl url("https://remoteplay.dl.playstation.net/remoteplay/redirect");QUrlQuery q;
    q.addQueryItem("state",QUrlQuery(login).queryItemValue("state"));q.addQueryItem("code",code);url.setQuery(q);return url.toString(QUrl::FullyEncoded);
}
QJsonObject token(const QString& refresh="fixture-refresh-1"){
    return {{"access_token","fixture-access"},{"refresh_token",refresh},{"expires_in",3600}};
}
QJsonObject client(const QString& id,const QString& name="Test console",bool enabled=true){
    return {{"duid",id},{"device",QJsonObject{{"name",name},{"enabledFeatures",enabled?QJsonArray{"remotePlay"}:QJsonArray{}}}}};
}
void finish(ui::PsnService& p){
    QElapsedTimer time;time.start();while(p.busy()&&time.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}
    check(!p.busy(),"request completion deadline");
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try {
        const QVariantMap psnForm{{"connection", "psn"}, {"psnDuid", QString(64, 'a')}, {"quality", 3},
            {"codec", 1}, {"bitrate", 2}, {"decode", 0}, {"sampling", 1}, {"viewOnly", false}, {"accessToken", "do-not-save"}};
        const QString resumeAccount("AQAAAAAAAAA=");
        const auto resume = ui::remotePlayResumeForm(psnForm, resumeAccount);
        check(!resume.contains("accessToken"), "resume record excludes secrets");
        QVariantMap current{{"connection", "local"}, {"psnDuid", QString(64, 'b')}, {"host", "192.0.2.1"}};
        check(ui::applyRemotePlayResume(resume, current, resumeAccount), "restore PSN selection over current LAN dialog");
        check(current.value("connection") == "psn" && current.value("psnDuid") == QString(64, 'a'), "saved transport and console stay together");
        const auto beforeInvalid = current;
        auto invalidResume = resume; invalidResume["psnDuid"] = "broken";
        check(!ui::applyRemotePlayResume(invalidResume, current, resumeAccount) && current == beforeInvalid, "invalid destination cannot partly change form");
        check(!ui::applyRemotePlayResume({}, current) && current == beforeInvalid, "legacy missing transport requires manual selection");
        invalidResume = resume; invalidResume["quality"] = 100;
        check(!ui::applyRemotePlayResume(invalidResume, current, resumeAccount) && current == beforeInvalid, "invalid preferences cannot partly change form");
        auto ps4Resume = resume; ps4Resume["psnDuid"] = QString(64, '0');
        check(!ui::applyRemotePlayResume(ps4Resume, current, "AgAAAAAAAAA=") && current == beforeInvalid, "account switch cannot resume another primary PS4");
        check(!ui::applyRemotePlayResume(resume, current) && current == beforeInvalid, "signed-out resume cannot use prior PSN selection");
        check(ui::applyRemotePlayResume({{"connection", "local"}}, current) && current.value("connection") == "local", "saved LAN selection cannot follow current PSN mode");
        QTemporaryDir temp;check(temp.isValid(),"isolated temp directory");
        const auto directory=std::filesystem::path(temp.path().toStdWString());
        Network net;ui::PsnService psn(directory,nullptr,&net);
        const QString duid(64,'a');
        check(!psn.signedIn(),"new service signed out");
        auto login=ui::PsnServiceTestAccess::login(psn);
        check(login.host()=="auth.api.sonyentertainmentnetwork.com","Sony authorization host");
        check(QUrlQuery(login).queryItemValue("state").size()==64,"unpredictable state shape");
        check(QUrlQuery(login).queryItemValue("duid").size()==48,"upstream client duid shape");
        auto callback=redirect(login,"fixture+code/=");
        QStringList invalid{
            callback.replace("https://remoteplay.dl.playstation.net/","https://attacker.invalid/"),
            redirect(login)+"&code=duplicate",
            redirect(login)+"&state=duplicate",
            redirect(login).replace("https://","http://"),
            redirect(login).replace("remoteplay.dl.playstation.net/","remoteplay.dl.playstation.net:443/"),
            redirect(login)+"#fragment",
            redirect(login).replace("/remoteplay/redirect","/remoteplay/other"),
            redirect(login).replace("https://","https://user@")
        };
        for(const auto& bad:invalid){psn.completeLogin(bad);check(net.sent.isEmpty(),"reject mismatched callback without HTTP");check(psn.awaitingRedirect(),"bad paste does not consume current login");}
        ui::PsnServiceTestAccess::expireLogin(psn);psn.completeLogin(redirect(login));
        check(!psn.awaitingRedirect()&&net.sent.isEmpty(),"expired browser login rejected");
        login=ui::PsnServiceTestAccess::login(psn);
        net.json(token());net.json({{"user_id","18446744073709551615"}});
        net.json({{"clients",QJsonArray{client(duid),client(duid),client(QString(64,'b'),"Disabled",false),client("invalid")}}});
        psn.completeLogin(redirect(login,"fixture+code/="));finish(psn);
        check(net.sent.size()==3,"token, introspection, console list sequence");
        check(net.sent[0].body.contains("code=fixture%2Bcode%2F%3D"),"form encodes plus slash equals");
        check(net.sent[0].request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()==QNetworkRequest::ManualRedirectPolicy,"never forward credential redirects");
        check(net.sent[0].request.transferTimeout()==20000,"bounded HTTP transfer timeout");
        check(psn.signedIn(),"valid login accepted");
        check(psn.accountId()=="//////////8=","full unsigned Account ID little endian");
        check(psn.consoles().size()==2,"unique enabled PS5 and synthetic primary PS4");
        check(psn.metaObject()->indexOfProperty("accessToken")<0&&psn.metaObject()->indexOfProperty("refreshToken")<0,"tokens not QML properties");
        const auto beforeReplay=net.sent.size();psn.completeLogin(redirect(login));
        check(net.sent.size()==beforeReplay,"authorization code single use");
        remoteplay::NativeConnectRequest request;request.video.codec=remoteplay::Codec::H265Hdr;
        check(psn.prepareConnection(duid,request),"fresh PS5 request preparation");
        check(request.psn&&request.ps5&&request.host.empty()&&request.psnToken.view()=="fixture-access","remote request uses PSN token without host or LAN keys");
        check(!request.validate(),"PS5 native request valid");
        remoteplay::NativeConnectRequest ps4;ps4.video.codec=remoteplay::Codec::H265Hdr;
        check(psn.prepareConnection(QString(64,'0'),ps4),"primary PS4 request preparation");
        check(ps4.psn&&!ps4.ps5&&ps4.video.codec==remoteplay::Codec::H264&&!ps4.validate(),"PS4 target uses H264");
        remoteplay::NativeConnectRequest unlisted;
        check(!psn.prepareConnection(QString(64,'c'),unlisted),"unlisted console rejected");
        QFile encrypted(temp.filePath("psn-account.dat"));check(encrypted.open(QIODevice::ReadOnly),"encrypted file created");
        const auto data=encrypted.readAll();encrypted.close();
        check(!data.contains("fixture-access")&&!data.contains("fixture-refresh"),"no plaintext tokens in disk file");
        Network reloadNet;ui::PsnService reloaded(directory,nullptr,&reloadNet);
        check(reloaded.signedIn()&&reloaded.accountId()==psn.accountId(),"DPAPI account survives restart");
        check(ui::PsnServiceTestAccess::refresh(reloaded)=="fixture-refresh-1","encrypted refresh preserved");
        // Deny delete sharing so QSaveFile cannot replace the old account.
        const auto savedPath=directory/L"psn-account.dat";
        HANDLE locked=CreateFileW(savedPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked!=INVALID_HANDLE_VALUE,"lock old account for replacement-failure test");
        auto switchLogin=ui::PsnServiceTestAccess::login(psn);
        net.json(token("different-account-refresh"));net.json({{"user_id","42"}});
        psn.completeLogin(redirect(switchLogin));finish(psn);CloseHandle(locked);
        check(psn.accountId()=="//////////8="&&psn.signedIn(),"failed account switch retains old identity");
        check(psn.status().contains(QString::fromUtf8("未切换账号")),"failed account switch explicit");
        ui::PsnService switchReload(directory,nullptr,&reloadNet);
        check(switchReload.accountId()==psn.accountId(),"memory and saved identity agree after failed switch");
        ui::PsnServiceTestAccess::expire(reloaded);
        reloadNet.json(token("fixture-refresh-2"));reloadNet.json({{"clients",QJsonArray{client(duid)}}});
        remoteplay::NativeConnectRequest expired;
        check(!reloaded.prepareConnection(QString(64,'0'),expired)&&reloaded.busy(),"expired token refreshes before any connect");
        finish(reloaded);
        check(reloadNet.sent[0].body.contains("grant_type=refresh_token"),"refresh grant selected");
        check(ui::PsnServiceTestAccess::refresh(reloaded)=="fixture-refresh-2","rotated refresh token adopted");
        Network secondNet;ui::PsnService second(directory,nullptr,&secondNet);
        check(ui::PsnServiceTestAccess::refresh(second)=="fixture-refresh-2","rotation persisted atomically");
        net.json({{"clients",QJsonArray{client(duid)}}},200,50);
        psn.refreshConsoles();check(psn.busy(),"list pending");psn.cancel();
        check(!psn.busy()&&psn.signedIn(),"cancel preserves completed login");
        for(int i=0;i<70;++i){QCoreApplication::processEvents();QThread::msleep(1);}
        check(!psn.busy(),"canceled reply cannot resurrect state");
        const auto beforeCancel=net.sent.size();auto canceledLogin=ui::PsnServiceTestAccess::login(psn);psn.cancel();psn.completeLogin(redirect(canceledLogin));
        check(net.sent.size()==beforeCancel,"canceled OAuth callback rejected");
        net.responses.push_back({QByteArray(256*1024+1,'x'),200,0});psn.refreshConsoles();finish(psn);
        check(psn.status().contains(QString::fromUtf8("大小限制")),"oversized response bounded");
        net.json({{"clients",QJsonArray{}}},302);psn.refreshConsoles();finish(psn);
        check(psn.status().contains("302"),"redirect rejected without follow");
        net.json({},401);psn.refreshConsoles();finish(psn);
        check(psn.signedIn(),"rejected access keeps refresh capability");
        net.json(token("fixture-refresh-after-401"));net.json({{"clients",QJsonArray{client(duid)}}});
        const auto beforeRetry=net.sent.size();
        remoteplay::NativeConnectRequest rejectedAccess;
        check(!psn.prepareConnection(duid,rejectedAccess),"rejected access cannot start a session");finish(psn);
        check(net.sent[beforeRetry].body.contains("grant_type=refresh_token"),"401 forces refresh despite old future expiry");
        ui::PsnServiceTestAccess::expire(second);secondNet.json({},400);
        second.refreshConsoles();finish(second);check(!second.signedIn(),"revoked refresh stops remote connects");
        QFile local(temp.filePath("local-profile.dat"));check(local.open(QIODevice::WriteOnly),"local fixture");local.write("keep-local-profile");local.close();
        psn.signOut();check(!psn.signedIn()&&!QFile::exists(temp.filePath("psn-account.dat")),"signout removes only account");
        check(QFile::exists(temp.filePath("local-profile.dat")),"signout preserves LAN pairing");
        QFile corrupt(temp.filePath("psn-account.dat"));check(corrupt.open(QIODevice::WriteOnly),"corrupt fixture");corrupt.write("not-dpapi");corrupt.close();
        ui::PsnService broken(directory,nullptr,&net);check(!broken.signedIn()&&!broken.status().isEmpty(),"corrupt encrypted account handled");
        std::cout<<"PASS "<<assertions<<" assertions; synthetic HTTP, isolated DPAPI storage; no live PSN/console test\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<" after "<<assertions<<" assertions\n";return 1;}
}
