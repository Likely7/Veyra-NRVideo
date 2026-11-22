// SPDX-License-Identifier: GPL-3.0-only
// Protocol endpoints and client identity adapted from chiaki-ng's PSNAccountID
// and holepunch.c at 0e16950165f06e5c3291537c2eeba6e852be7120.
// The combined Chiaki build is AGPL-3.0 with its OpenSSL exception; see notices.
#include "veyra/ui/PsnService.h"
#include "veyra/remoteplay/ChiakiBackend.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstring>

namespace veyra::ui {
namespace {
constexpr auto tokenUrl="https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";
constexpr auto redirectUrl="https://remoteplay.dl.playstation.net/remoteplay/redirect";
constexpr auto clientId="ba495a24-818c-472b-b12d-ff231c1b5745";
// Public Remote Play client identity from upstream, not a user credential.
constexpr auto clientSecret="mvaiZkRsAsI1IBkY";
constexpr auto scope="psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update";
constexpr qsizetype responseLimit=256*1024;
QString pathString(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }
void wipe(QByteArray& b) { b.fill('\0'); b.clear(); }
QString randomHex(int bytes) {
    QByteArray value(bytes, '\0');
    for(int i=0;i<bytes;++i)value[i]=char(QRandomGenerator::system()->generate()&255u);
    return QString::fromLatin1(value.toHex());
}
QByteArray basicAuth() { return "Basic "+(QByteArray(clientId)+':'+clientSecret).toBase64(); }
QString primaryPs4() { return QString(64, QLatin1Char('0')); }
QVariantMap primaryPs4Entry() {
    return {{"id",primaryPs4()},{"label",PsnService::tr("主 PS4（需在主机上启用远程游玩）")},{"ps5",false}};
}
struct ProtectedBlob {
    DATA_BLOB value{};
    ~ProtectedBlob() { if(value.pbData) { SecureZeroMemory(value.pbData,value.cbData); LocalFree(value.pbData); } }
};
}
PsnService::Account::~Account() { clear(); }
void PsnService::Account::clear() { wipe(access); wipe(refresh); id.clear(); expiry=0; }
PsnService::PsnService(std::filesystem::path storageDir,QObject* parent,QNetworkAccessManager* network)
    : QObject(parent), network_(network?network:new QNetworkAccessManager(this)),directory_(std::move(storageDir)) {
    loadAccount();
    if(signedIn()) {
        consoles_.append(primaryPs4Entry());
        status_=tr("已加载 PSN 登录。点击刷新主机获取 PS5，或选择主 PS4。");
    }
}
PsnService::~PsnService() { abortRequest(); }
bool PsnService::signedIn() const { return !account_.id.isEmpty() && validToken(account_.refresh); }
bool PsnService::validToken(const QByteArray& value) {
    return !value.isEmpty()&&value.size()<=8192&&std::all_of(value.begin(),value.end(),[](char c){return c>=33&&c<=126;});
}
bool PsnService::validDuid(const QString& value) {
    return value.size()==64&&std::all_of(value.begin(),value.end(),[](QChar c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});
}
QUrl PsnService::authorizationUrl() {
    loginState_=randomHex(32);
    loginStarted_=QDateTime::currentSecsSinceEpoch();
    QUrl url(QStringLiteral("https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"));
    QUrlQuery q;
    q.addQueryItem("service_entity","urn:service-entity:psn");
    q.addQueryItem("response_type","code");q.addQueryItem("client_id",clientId);
    q.addQueryItem("redirect_uri",redirectUrl);q.addQueryItem("scope",scope);
    q.addQueryItem("request_locale","en_US");q.addQueryItem("ui","pr");
    q.addQueryItem("service_logo","ps");q.addQueryItem("layout_type","popup");
    q.addQueryItem("smcid","remoteplay");q.addQueryItem("prompt","always");
    q.addQueryItem("PlatformPrivacyWs1","minimal");
    q.addQueryItem("duid","0000000700410080"+randomHex(16));
    q.addQueryItem("state",loginState_);url.setQuery(q);return url;
}
void PsnService::beginLogin() {
    if(busy_)return;
    const auto url=authorizationUrl();
    if(!QDesktopServices::openUrl(url)) {loginState_.clear();fail(tr("无法打开系统浏览器，请检查默认浏览器设置。"));return;}
    status_=tr("在 Sony 页面登录后，复制地址栏完整回调网址到下方。密码只在浏览器输入。");emit changed();
}
void PsnService::completeLogin(const QString& callbackUrl) {
    if(busy_)return;
    if(loginState_.isEmpty()||QDateTime::currentSecsSinceEpoch()-loginStarted_>600) {
        loginState_.clear();fail(tr("登录会话已过期，请重新点击登录 PSN。"));return;
    }
    const auto text=callbackUrl.trimmed();
    const QUrl url(text,QUrl::StrictMode);const QUrlQuery q(url);
    const auto items=q.queryItems(QUrl::FullyDecoded);
    const auto count=[&](const QString& key){return std::count_if(items.begin(),items.end(),[&](const auto& x){return x.first==key;});};
    const auto code=q.queryItemValue("code",QUrl::FullyDecoded).toUtf8();
    if(text.size()>16384||!url.isValid()||url.scheme()!="https"||url.host()!="remoteplay.dl.playstation.net"
       ||url.path()!="/remoteplay/redirect"||url.port(-1)!=-1||!url.userInfo().isEmpty()||url.hasFragment()
       ||count("state")!=1||q.queryItemValue("state",QUrl::FullyDecoded)!=loginState_
       ||count("code")!=1||q.hasQueryItem("error")||!validToken(code)) {
        fail(tr("回调网址无效或不属于本次登录。请复制当前登录页面的完整网址。"));return;
    }
    loginState_.clear();pending_.clear();tokenRequest(code,false);
}
void PsnService::abortRequest() {
    if(reply_) {
        auto* r=reply_.data();reply_.clear();r->disconnect(this);r->abort();r->deleteLater();
    }
    busy_=false;
}
void PsnService::cancel() {
    abortRequest();pending_.clear();loginState_.clear();refreshing_=false;
    status_=tr("已取消 PSN 操作。");emit changed();
}
void PsnService::fail(const QString& message) {
    busy_=false;refreshing_=false;pending_.clear();status_=message;emit changed();
}
void PsnService::requestJson(const QUrl& url,const QByteArray& authorization,const QByteArray& body,bool post,JsonHandler handler) {
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    req.setRawHeader("Authorization",authorization);req.setRawHeader("Accept","application/json");
    req.setRawHeader("Accept-Language","en-US");
    if(post)req.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    busy_=true;
    QNetworkReply* reply=post?network_->post(req,body):network_->get(req);reply_=reply;
    reply->setReadBufferSize(responseLimit+1);
    auto payload=std::make_shared<QByteArray>();
    auto oversized=std::make_shared<bool>(false);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply,payload,oversized]{
        if(reply_!=reply)return;
        payload->append(reply->readAll());
        if(payload->size()>responseLimit){*oversized=true;reply->abort();}
    });
    // Absolute deadline also bounds slow servers that keep sending tiny chunks.
    QTimer::singleShot(25000,reply,[reply]{if(!reply->isFinished())reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,payload,oversized,bearer=authorization.startsWith("Bearer "),handler=std::move(handler)] {
        if(reply_!=reply)return;
        reply_.clear();payload->append(reply->readAll());
        const int http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok=reply->error()==QNetworkReply::NoError&&http>=200&&http<300;
        reply->deleteLater();
        if(*oversized||payload->size()>responseLimit) {wipe(*payload);fail(tr("PSN 响应超出大小限制。"));return;}
        if(!ok) {
            wipe(*payload);
            if(bearer&&http==401) {
                account_.expiry=1; (void)saveAccount();
                fail(tr("PSN 访问令牌已失效。点击刷新主机或连接以刷新登录。"));return;
            }
            if(refreshing_&&(http==400||http==401||http==403)){account_.clear();accountId_.clear();consoles_.clear();}
            fail(http==400||http==401||http==403?tr("PSN 授权失效或被拒绝，请重新登录。")
                :tr("PSN 请求失败（HTTP %1）。请检查网络后重试。").arg(http));return;
        }
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(*payload,&error);wipe(*payload);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()){fail(tr("PSN 返回了无效数据。"));return;}
        handler(doc.object());
    });
    emit changed();
}
void PsnService::tokenRequest(const QByteArray& code,bool refresh) {
    refreshing_=refresh;pending_.clear();
    if(refresh){pending_.id=account_.id;pending_.refresh=account_.refresh;}
    QUrlQuery q;q.addQueryItem("grant_type",refresh?"refresh_token":"authorization_code");
    q.addQueryItem(refresh?"refresh_token":"code",QString::fromUtf8(code));
    q.addQueryItem("scope",scope);q.addQueryItem("redirect_uri",redirectUrl);
    status_=refresh?tr("正在刷新 PSN 登录…"):tr("正在完成 PSN 登录…");
    QByteArray form;
    for(const auto& item:q.queryItems()) {
        if(!form.isEmpty())form+='&';
        form+=QUrl::toPercentEncoding(item.first)+'='+QUrl::toPercentEncoding(item.second);
    }
    requestJson(QUrl(tokenUrl),basicAuth(),form,true,[this,refresh](const QJsonObject& o){
        const auto access=o.value("access_token").toString().toUtf8();
        auto refreshToken=o.value("refresh_token").toString().toUtf8();
        if(refresh&&refreshToken.isEmpty())refreshToken=account_.refresh;
        const double seconds=o.value("expires_in").toDouble(0);
        if(!validToken(access)||!validToken(refreshToken)||seconds<1||seconds>366.0*86400.0){fail(tr("PSN 登录响应缺少有效令牌或到期时间。"));return;}
        pending_.access=access;pending_.refresh=refreshToken;
        pending_.expiry=QDateTime::currentSecsSinceEpoch()+qint64(seconds);
        if(refresh)acceptAccount();else accountRequest();
    });
}
void PsnService::accountRequest() {
    // Sony's token introspection API uses a path segment. Never log this URL,
    // follow redirects, or send it to a host obtained from an API response.
    const auto encoded=QUrl::toPercentEncoding(QString::fromUtf8(pending_.access));
    requestJson(QUrl::fromEncoded(QByteArray(tokenUrl)+'/'+encoded),basicAuth(),{},false,[this](const QJsonObject& o){
        const auto id=remoteplay::accountIdFromDecimal(o.value("user_id").toString().toStdString());
        if(!id){fail(tr("无法取得 PSN Account ID，请重新登录。"));return;}
        pending_.id=QString::fromStdString(remoteplay::accountIdToBase64(*id));acceptAccount();
    });
}
void PsnService::acceptAccount() {
    const bool accountSwitch=account_.id!=pending_.id;
    const bool hadSavedAccount=QFile::exists(pathString(directory_/L"psn-account.dat"));
    Account previous;
    previous.access.swap(account_.access);previous.refresh.swap(account_.refresh);
    previous.id=account_.id;previous.expiry=account_.expiry;
    account_.access.swap(pending_.access);account_.refresh.swap(pending_.refresh);
    account_.id=pending_.id;account_.expiry=pending_.expiry;pending_.clear();
    accountId_=account_.id;refreshing_=false;saveFailed_=!saveAccount();
    if(saveFailed_&&accountSwitch&&hadSavedAccount) {
        // Never accept B in memory while an older A is the account loaded next
        // launch (in particular both accounts' primary PS4 use the same ID).
        account_.clear();account_.access.swap(previous.access);account_.refresh.swap(previous.refresh);
        account_.id=previous.id;account_.expiry=previous.expiry;accountId_=account_.id;
        fail(tr("新账号加密存档保存失败，未切换账号。请检查目录权限后重新登录。"));return;
    }
    consoles_.clear();consoles_.append(primaryPs4Entry());discovered_.clear();fetchConsoles();
}
QVariantList PsnService::parseConsoles(const QJsonObject& o) {
    QVariantList result;QSet<QString> seen;
    for(const auto& entry:o.value("clients").toArray()) {
        const auto client=entry.toObject(),device=client.value("device").toObject();
        const QString id=client.value("duid").toString().toLower();
        if(!validDuid(id)||id==primaryPs4()||seen.contains(id))continue;
        bool enabled=false;for(const auto& feature:device.value("enabledFeatures").toArray())if(feature.toString()=="remotePlay")enabled=true;
        if(!enabled)continue;
        auto name=device.value("name").toString().trimmed();if(name.isEmpty())name=QStringLiteral("PS5");
        name.remove(QChar('\n'));name.remove(QChar('\r'));
        result.append(QVariantMap{{"id",id},{"label",name.left(128)+QStringLiteral(" · PS5")},{"ps5",true}});seen.insert(id);
    }
    return result;
}
void PsnService::fetchConsoles(int offset) {
    status_=tr("正在获取 PSN 主机…");
    QUrl url(QStringLiteral("https://web.np.playstation.com/api/cloudAssistedNavigation/v2/users/me/clients"));
    QUrlQuery q;q.addQueryItem("platform","PS5");q.addQueryItem("includeFields","device");q.addQueryItem("limit","10");q.addQueryItem("offset",QString::number(offset));url.setQuery(q);
    requestJson(url,"Bearer "+account_.access,{},false,[this,offset](const QJsonObject& o){
        if(!o.value("clients").isArray()){fail(tr("PSN 主机列表格式无效。可重试刷新或选择主 PS4。"));return;}
        auto page=parseConsoles(o);
        for(const auto& c:page){bool duplicate=false;for(const auto& old:discovered_)if(old.toMap().value("id")==c.toMap().value("id"))duplicate=true;if(!duplicate)discovered_.append(c);}
        if(o.value("clients").toArray().size()==10&&offset<90){fetchConsoles(offset+10);return;}
        consoles_=discovered_;consoles_.append(primaryPs4Entry());discovered_.clear();busy_=false;
        status_=saveFailed_?tr("已登录，但加密存档写入失败；本次可用，重启后需重新登录。")
            :tr("请选择 PS5 或账户的主 PS4。首次可通过 PSN 连接，无需先在局域网配对。主机须已启用远程游玩。");
        emit changed();
    });
}
void PsnService::refreshConsoles() {
    if(busy_)return;
    if(!signedIn()){fail(tr("请先登录 PSN。"));return;}
    if(!validToken(account_.access)||account_.expiry<=QDateTime::currentSecsSinceEpoch()+120){tokenRequest(account_.refresh,true);return;}
    discovered_.clear();fetchConsoles();
}
bool PsnService::prepareConnection(const QString& duid,remoteplay::NativeConnectRequest& request) {
    if(busy_){status_=tr("请等待 PSN 操作完成后再连接。");emit changed();return false;}
    if(!signedIn()){fail(tr("请先登录 PSN。"));return false;}
    if(!validToken(account_.access)||account_.expiry<=QDateTime::currentSecsSinceEpoch()+120){tokenRequest(account_.refresh,true);return false;}
    const auto chosen=std::find_if(consoles_.begin(),consoles_.end(),[&](const QVariant& v){return v.toMap().value("id").toString()==duid;});
    if(chosen==consoles_.end()||!validDuid(duid)){fail(tr("请选择列表中的主机，或刷新主机列表。"));return false;}
    const auto id=remoteplay::accountIdFromBase64(account_.id.toStdString());if(!id){fail(tr("PSN Account ID 无效，请重新登录。"));return false;}
    request.psn=true;request.ps5=chosen->toMap().value("ps5").toBool();request.psnDuid=duid.toStdString();
    request.psnToken.assign(std::string_view(account_.access.constData(),size_t(account_.access.size())));
    request.credentials.accountId=*id;request.host.clear();
    if(!request.ps5)request.video.codec=remoteplay::Codec::H264;
    status_=tr("正在通过 PSN 连接…");emit changed();return true;
}
bool PsnService::saveAccount() {
    QJsonObject o{{"version",1},{"access",QString::fromUtf8(account_.access)},{"refresh",QString::fromUtf8(account_.refresh)},
                  {"account",account_.id},{"expiry",QString::number(account_.expiry)}};
    auto bytes=QJsonDocument(o).toJson(QJsonDocument::Compact);ProtectedBlob encrypted;
    DATA_BLOB input{DWORD(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())};
    const bool protectedOk=CryptProtectData(&input,L"Veyra PSN login",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.value)!=FALSE;
    wipe(bytes);if(!protectedOk)return false;
    if(!QDir().mkpath(pathString(directory_)))return false;
    QSaveFile file(pathString(directory_/L"psn-account.dat"));file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))return false;
    if(file.write(reinterpret_cast<const char*>(encrypted.value.pbData),encrypted.value.cbData)!=encrypted.value.cbData)return false;
    return file.commit();
}
void PsnService::loadAccount() {
    QFile file(pathString(directory_/L"psn-account.dat"));if(!file.exists())return;
    if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>65536){status_=tr("无法读取 PSN 加密存档，请重新登录。");return;}
    auto encrypted=file.readAll();DATA_BLOB input{DWORD(encrypted.size()),reinterpret_cast<BYTE*>(encrypted.data())};ProtectedBlob plain;
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.value)){status_=tr("无法解密 PSN 存档；请使用原 Windows 账户或重新登录。");return;}
    auto bytes=QByteArray(reinterpret_cast<const char*>(plain.value.pbData),plain.value.cbData);
    const auto o=QJsonDocument::fromJson(bytes).object();wipe(bytes);
    auto access=o.value("access").toString().toUtf8(),refresh=o.value("refresh").toString().toUtf8();
    const auto id=o.value("account").toString();bool expiryOk=false;const auto expiry=o.value("expiry").toString().toLongLong(&expiryOk);
    if(o.value("version").toInt()!=1||!validToken(access)||!validToken(refresh)||!remoteplay::accountIdFromBase64(id.toStdString())||!expiryOk||expiry<=0){wipe(access);wipe(refresh);status_=tr("PSN 存档格式无效，请重新登录。");return;}
    account_.access.swap(access);account_.refresh.swap(refresh);account_.id=id;account_.expiry=expiry;accountId_=id;
}
void PsnService::signOut() {
    cancel();const auto path=pathString(directory_/L"psn-account.dat");
    if(QFile::exists(path)&&!QFile::remove(path)){fail(tr("无法删除 PSN 加密存档，请检查目录权限。"));return;}
    account_.clear();accountId_.clear();consoles_.clear();discovered_.clear();
    status_=tr("已退出本机 PSN 登录；局域网配对和正在进行的串流不受影响。");emit changed();
}
}
