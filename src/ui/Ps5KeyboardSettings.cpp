#include "veyra/ui/Ps5KeyboardSettings.h"
#include "veyra/ui/Ps5PlayerKeys.h"
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QWindow>

namespace veyra::ui {
namespace {
const QString presetName = QStringLiteral("GTA VI · PS5 · 0.6.87");
constexpr auto storeName = "ps5-keyboard-mouse.json";
bool saveJson(const QString& path,const QJsonObject& data) {
    QSaveFile file(path);
    const auto bytes=QJsonDocument(data).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
}
Ps5KeyboardSettings::Ps5KeyboardSettings(QString directory,QString presetPath,std::function<Session()> provider,
                                       WindowInputCapture::MouseGrab mouseGrab)
    : directory_(std::move(directory)), provider_(std::move(provider)), capture_(*this,true,nullptr,std::move(mouseGrab)) {
    QFile preset(presetPath);
    if(preset.open(QIODevice::ReadOnly))preset_=QJsonDocument::fromJson(preset.readAll()).object();
    if(preset_.isEmpty())error_=tr("无法加载已校准的 GTA VI 控制配置。");
    QFile store(QDir(directory_).filePath(storeName));
    if(store.open(QIODevice::ReadOnly))store_=QJsonDocument::fromJson(store.readAll()).object();
    connect(&capture_,&WindowInputCapture::capturedChanged,this,[this](bool on){
        if(!on && playing_){
            overlayVisible_ = false;
            playing_->stop(); // joins the worker after its final published frame
            if(playing_ == provider_())
                situation_=playingContexts_.value(playing_->situation(),situation_);
            playing_.reset();
        }
        emit sessionChanged();
    });
    auto* timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,&Ps5KeyboardSettings::refresh);
    timer->start(250);
}
Ps5KeyboardSettings::~Ps5KeyboardSettings(){ release(); edit(false); }
QString Ps5KeyboardSettings::selected() const { return store_.value("selected").toString(presetName); }
QStringList Ps5KeyboardSettings::profiles() const {
    return QStringList{presetName}+store_.value("copies").toObject().keys();
}
QStringList Ps5KeyboardSettings::situations() const {
    const auto doc = selectedProfile(store_);
    auto names = doc.value("contexts").toVariant().toStringList();
    // Portable Mapper appends its exclusive console_menu after the named contexts.
    if (doc.value("console_menu").isObject()) names.append(QStringLiteral("system_nav"));
    return names;
}
QJsonObject Ps5KeyboardSettings::selectedProfile(const QJsonObject& store) const {
    const auto name = store.value("selected").toString(presetName);
    return name == presetName ? preset_ : store.value("copies").toObject().value(name).toObject();
}
QVariantMap Ps5KeyboardSettings::profile() const { return selectedProfile(store_).toVariantMap(); }
int Ps5KeyboardSettings::dpi() const {return store_.value("dpi").toInt(1600);}
bool Ps5KeyboardSettings::fail(QString error){error_=std::move(error);emit changed();return false;}
bool Ps5KeyboardSettings::commit(QJsonObject next,bool editedCopy){
    if(!QDir().mkpath(directory_) || !saveJson(QDir(directory_).filePath(storeName),next))
        return fail(tr("无法保存键盘和鼠标设置。"));
    const bool profileChanged=next.value("selected").toString(presetName)!=selected();
    if(captured() && (profileChanged || editedCopy))release();
    store_=std::move(next);
    pendingStore_ = {};
    shortcutConflict_.clear();
    if(profileChanged || editedCopy){
        const auto contexts=situations();
        if(!editedCopy || !contexts.contains(situation_)){
            situation_=contexts.value(0);
            emit sessionChanged();
        }
    }
    error_.clear(); emit changed(); return true;
}
bool Ps5KeyboardSettings::choose(const QString& name){
    if(!profiles().contains(name))return false;
    auto next=store_;next["selected"]=name;return stageChange(next,false);
}
bool Ps5KeyboardSettings::validate(const QByteArray& bytes){
    stickmouse_mapper_profile* p=nullptr;
    if(stickmouse_mapper_profile_load({bytes.constData(),uint32_t(bytes.size())},&p)){
        const auto error=stickmouse_mapper_last_error();
        return fail(QString::fromUtf8(error.data,error.len));
    }
    stickmouse_mapper_profile_free(p);return true;
}
bool Ps5KeyboardSettings::saveCopy(QString name,const QVariantMap& input){
    return writeCopy(std::move(name),QJsonObject::fromVariantMap(input),true);
}
bool Ps5KeyboardSettings::writeCopy(QString name,const QJsonObject& doc,bool editedCopy){
    name=name.trimmed();
    if(name.isEmpty() || name==presetName)return fail(tr("请输入个人副本的名称。"));
    if(name!=selected() && profiles().contains(name))return fail(tr("已有控制配置使用此名称。"));
    if(!validate(QJsonDocument(doc).toJson(QJsonDocument::Compact)))return false;
    auto next=store_,copies=next.value("copies").toObject();
    copies[name]=doc;next["copies"]=copies;next["selected"]=name;
    return stageChange(next,editedCopy);
}
bool Ps5KeyboardSettings::acknowledgeInputCheck() {
    if (inputCheckOffered()) return true;
    auto next = store_;
    next["inputCheckOffered"] = true;
    return commit(next);
}
bool Ps5KeyboardSettings::setShowShortcutHints(bool show) {
    if (show == showShortcutHints()) return true;
    auto next = store_;
    next["showShortcutHints"] = show;
    return commit(next);
}
bool Ps5KeyboardSettings::setDpi(int value){
    auto bytes=QJsonDocument(QJsonObject::fromVariantMap(profile())).toJson(QJsonDocument::Compact);
    stickmouse_mapper_profile* p=nullptr;
    if(stickmouse_mapper_profile_load({bytes.constData(),uint32_t(bytes.size())},&p))return false;
    const bool valid=value>0 && !stickmouse_mapper_profile_set_dpi(p,uint32_t(value));
    stickmouse_mapper_profile_free(p);
    if(!valid)return fail(tr("鼠标 DPI 超出支持范围。"));
    auto next=store_;next["dpi"]=value;return commit(next);
}
bool Ps5KeyboardSettings::importProfile(QUrl url,QString name){
    QFile file(url.toLocalFile());
    if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)return fail(tr("无法读取此控制配置。"));
    const auto bytes=file.readAll();
    if(!validate(bytes))return false;
    name=name.trimmed();
    if (name.isEmpty()) {
        const auto base = QFileInfo(file).completeBaseName();
        const auto existing = profiles();
        name = base;
        for (int suffix = 2; existing.contains(name); ++suffix)
            name = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
    }
    if(profiles().contains(name))return fail(tr("请为导入的副本选择新名称。"));
    return writeCopy(name,QJsonDocument::fromJson(bytes).object(),false);
}
bool Ps5KeyboardSettings::exportProfile(QUrl url){
    if(!saveJson(url.toLocalFile(),QJsonObject::fromVariantMap(profile())))return fail(tr("无法导出此控制配置。"));
    error_.clear();emit changed();return true;
}
void Ps5KeyboardSettings::refresh(){
    refreshSession(provider_());
}
void Ps5KeyboardSettings::refreshSession(const Session& session){
    if(playing_ && (!playing_->active() || playing_!=session) && captured())release();
    if(editing_ && session)session->pausePhysical(true);
    QString now=situation_;
    if(connection_.lock()!=session || available_!=bool(session))
        now=session ? situations().value(0) : QString{};
    connection_=session;
    if(captured() && playing_)
        now=playingContexts_.value(playing_->situation(),now);
    if(available_!=bool(session)||now!=situation_){
        available_=bool(session);situation_=now;emit sessionChanged();
    }
}
void Ps5KeyboardSettings::release(){capture_.capture(false);}
QString Ps5KeyboardSettings::keyCode(int qtKey, int nativeScanCode) const {
    return ps5EventKeyName(qtKey, nativeScanCode);
}
void Ps5KeyboardSettings::setPlayerWindow(QWindow* window, bool shortcutsEnabled) {
    if (captured() && (!shortcutsEnabled || playerWindow_ != window)) release();
    playerWindow_ = window;
    capture_.listenForLocalKeys(window && shortcutsEnabled);
    if (window && shortcutsEnabled)
        capture_.setWindow(GetAncestor(reinterpret_cast<HWND>(window->winId()), GA_ROOT));
}
void Ps5KeyboardSettings::edit(bool open){
    editing_=open;
    if(auto session=provider_())session->pausePhysical(open);
    if(open)release();
    if (open && shortcutConflict_.isEmpty()) hasShortcutConflict(store_, false);
    if (!open) cancelShortcutConflict();
}
bool Ps5KeyboardSettings::resume(){
    auto session=provider_();
    refreshSession(session);
    auto* window=QGuiApplication::focusWindow();
    if(!window || (playerWindow_ && window != playerWindow_) || QGuiApplication::modalWindow() || !session || editing_)
        return fail(tr("请连接 PS5 控制通道，然后切换到播放器窗口。"));
    if (hasShortcutConflict(store_, false)) return fail(tr("请先解决播放器快捷键冲突。"));
    if (captured() && playing_ == session) return true;
    const auto p=profile();
    const auto contexts=situations();
    if(!contexts.contains(situation_))situation_=contexts.value(0);
    const auto bytes=QJsonDocument(QJsonObject::fromVariantMap(p)).toJson(QJsonDocument::Compact);
    playing_=session;
    playingContexts_=contexts;
    const auto error=session->start({bytes.constData(),size_t(bytes.size())},dpi(),situation_.toStdString());
    if(!error.empty()){playing_.reset();return fail(QString::fromStdString(error));}
    capture_.setWindow(GetAncestor(reinterpret_cast<HWND>(window->winId()),GA_ROOT));
    if(!capture_.capture(true)){session->stop();playing_.reset();return fail(tr("无法在此窗口捕获输入。"));}
    error_.clear();emit changed();return true;
}
void Ps5KeyboardSettings::correct(QString name){
    refreshSession(provider_());
    if(!situations().contains(name))return;
    if(captured() && !playing_->select(name.toStdString()))return;
    situation_=name;emit sessionChanged();
}
void Ps5KeyboardSettings::key(uint32_t vk,uint32_t scan,bool extended,bool down){
    if(playing_ && (vk==VK_PAUSE || (scan>0 && scan<128)))playing_->key(vk==VK_PAUSE?256:uint16_t(scan+(extended?128:0)),down);
}
bool Ps5KeyboardSettings::localKey(uint32_t scan, bool extended, bool down, bool repeat) {
    const auto key = ps5PlayerKeyName(scan, extended);
    if (key.isEmpty()) return false;
    if (captured() && key == overlayKey()) {
        if (down && !repeat) {
            overlayVisible_ = !overlayVisible_;
            emit sessionChanged();
        }
        return true;
    }
    if (key != controlKey() || editing_) return false;
    if (!captured() && (!playerWindow_ || QGuiApplication::focusWindow() != playerWindow_ ||
                        QGuiApplication::modalWindow() || !provider_())) return false;
    if (down && !repeat) {
        if (captured()) release();
        else emit controlRequested();
    }
    return true;
}
void Ps5KeyboardSettings::mouseMove(int dx,int dy){if(playing_)playing_->motion(dx,dy);}
void Ps5KeyboardSettings::mouseButton(int button,bool down){
    constexpr uint8_t buttons[]{0,0,2,1,3,4};
    if(playing_ && button>=1 && button<=5)playing_->button(buttons[button],down);
}
void Ps5KeyboardSettings::scroll(int delta,bool horizontal){if(playing_)playing_->wheel(delta,horizontal);}
}
