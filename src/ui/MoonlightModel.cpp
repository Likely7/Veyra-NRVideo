// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/MoonlightModel.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <algorithm>

#include "veyra/Log.h"
#include "veyra/moonlight/Client.h"
#include "veyra/moonlight/Pairing.h"
#include "veyra/moonlight/StreamConfig.h"

#include <format>

namespace veyra::ui {
namespace {

QString qs(const std::string& text) { return QString::fromUtf8(text.data(), qsizetype(text.size())); }
std::string utf8(const QString& text) { const QByteArray b = text.toUtf8(); return std::string(b.constData(), size_t(b.size())); }

QString describe(const std::exception& e) {
    if (const auto* transport = dynamic_cast<const moonlight::TransportError*>(&e)) {
        switch (transport->kind) {
        case moonlight::TransportError::Kind::Connect: return QObject::tr("连不上主机：请确认地址正确、主机已开机且 Sunshine 在运行，并且防火墙放行了 TCP 47984 / 47989。");
        case moonlight::TransportError::Kind::Timeout: return QObject::tr("主机没有及时回应，请检查网络。");
        case moonlight::TransportError::Kind::Tls: return QObject::tr("主机的证书与配对时保存的不一致（主机重装过，或这不是配对过的那台）。请删除这台主机后重新配对。");
        case moonlight::TransportError::Kind::Cancelled: return QObject::tr("已取消。");
        case moonlight::TransportError::Kind::Protocol: return QObject::tr("主机的回应无法识别（不是 Sunshine / GameStream 主机？）。");
        }
    }
    if (const auto* status = dynamic_cast<const moonlight::StatusError*>(&e)) {
        if (status->status == 401) return QObject::tr("主机不认识这台电脑，请重新配对。");
        return QObject::tr("主机拒绝了请求（%1）。").arg(status->status);
    }
    return QObject::tr("出错了：%1").arg(qs(e.what()));
}

constexpr int kVersion = 1;
// Testers asked for the stream at full rate by default (2026-10-01): 150 Mbps is what a
// wired gigabit LAN and Wi-Fi 6 carry comfortably; the slider goes to 500 Mbps.
constexpr int kDefaultBitrateMbps = 150;
constexpr int kMaxBitrateMbps = 500;

QVariantMap defaultSettings() {
    return QVariantMap{
        {"res", "1080p"}, {"fps", 60}, {"bitrate", kDefaultBitrateMbps}, {"codec", 0}, {"hdr", false},
        {"audio", 2}, {"gamepad", true}, {"sops", false}, {"hostAudio", false}, {"captureInput", true},
    };
}

bool validSetting(const QString& key, const QVariant& value) {
    if (key == "res") return QStringList{"720p", "1080p", "1440p", "4k", "native"}.contains(value.toString());
    if (key == "fps") return QList<int>{30, 60, 90, 120, 144}.contains(value.toInt());
    if (key == "bitrate") { const int v = value.toInt(); return v >= 1 && v <= kMaxBitrateMbps; }   // Mbps
    if (key == "codec") return value.toInt() >= 0 && value.toInt() <= 3;
    if (key == "audio") return QList<int>{2, 6, 8}.contains(value.toInt());
    return key == "hdr" || key == "gamepad" || key == "sops" || key == "hostAudio" || key == "captureInput";
}

QString stateOf(bool online, bool checking, bool paired, int currentGame) {
    if (checking && !online) return "checking";
    if (!online) return "offline";
    if (!paired) return "unpaired";
    return currentGame != 0 ? "busy" : "online";
}

} // namespace

MoonlightModel::MoonlightModel(QObject* parent) : QObject(parent), defaults_(defaultSettings()) {}

MoonlightModel::~MoonlightModel() {
    cancel_ = true;
    discovery_.stop();
    // members join their threads (worker_ and probes_) as they are destroyed
}

// --- persistence -----------------------------------------------------------------------------

void MoonlightModel::loadStore() {
    hosts_.clear();
    defaults_ = defaultSettings();
    QFile file(QString::fromStdWString((moonlight::dataDirectory() / L"hosts.json").wstring()));
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        log::warn("moonlight-ui", "hosts.json is damaged; starting without saved hosts");
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonObject savedDefaults = root.value("defaults").toObject();
    for (auto it = savedDefaults.begin(); it != savedDefaults.end(); ++it)
        if (validSetting(it.key(), it.value().toVariant())) defaults_[it.key()] = it.value().toVariant();
    const QJsonObject last = root.value("last").toObject();
    lastHost_ = last.value("host").toString();
    lastApp_ = last.value("app").toInt();
    lastLabel_ = last.value("label").toString();
    for (const QJsonValue& value : root.value("hosts").toArray()) {
        const QJsonObject o = value.toObject();
        Host h;
        h.id = o.value("id").toString();
        h.address = o.value("address").toString();
        if (h.id.isEmpty() || h.address.isEmpty() || find(h.id)) continue;
        h.name = o.value("name").toString();
        h.mac = o.value("mac").toString();
        h.cert = o.value("cert").toString();
        h.gpu = o.value("gpu").toString();
        h.version = o.value("version").toString();
        h.port = o.value("port").toInt(moonlight::kDefaultHttpPort);
        h.httpsPort = o.value("httpsPort").toInt(moonlight::kDefaultHttpsPort);
        h.codecSupport = o.value("codecSupport").toInt(moonlight::kServerH264);
        h.paired = o.value("paired").toBool() && !h.cert.isEmpty();
        h.nvidia = o.value("nvidia").toBool();
        h.saved = true;
        h.settings = defaults_;
        const QJsonObject s = o.value("settings").toObject();
        for (auto it = s.begin(); it != s.end(); ++it)
            if (validSetting(it.key(), it.value().toVariant())) h.settings[it.key()] = it.value().toVariant();
        hosts_.push_back(std::move(h));
    }
}

bool MoonlightModel::saveStore() const {
    QJsonObject root;
    root["version"] = kVersion;
    root["defaults"] = QJsonObject::fromVariantMap(defaults_);
    root["last"] = QJsonObject{{"host", lastHost_}, {"app", lastApp_}, {"label", lastLabel_}};
    QJsonArray array;
    for (const Host& h : hosts_) {
        if (!h.saved) continue;
        array.append(QJsonObject{{"id", h.id}, {"name", h.name}, {"address", h.address}, {"port", h.port}, {"httpsPort", h.httpsPort},
                                 {"mac", h.mac}, {"cert", h.cert}, {"gpu", h.gpu}, {"version", h.version}, {"codecSupport", h.codecSupport},
                                 {"paired", h.paired}, {"nvidia", h.nvidia}, {"settings", QJsonObject::fromVariantMap(h.settings)}});
    }
    root["hosts"] = array;
    std::error_code ec;
    std::filesystem::create_directories(moonlight::dataDirectory(), ec);
    QSaveFile file(QString::fromStdWString((moonlight::dataDirectory() / L"hosts.json").wstring()));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

MoonlightModel::Host* MoonlightModel::find(const QString& id) {
    for (Host& h : hosts_) if (h.id == id) return &h;
    return nullptr;
}
const MoonlightModel::Host* MoonlightModel::find(const QString& id) const {
    for (const Host& h : hosts_) if (h.id == id) return &h;
    return nullptr;
}
MoonlightModel::Host* MoonlightModel::findByAddress(const QString& address, int port) {
    for (Host& h : hosts_) if (h.address.compare(address, Qt::CaseInsensitive) == 0 && h.port == port) return &h;
    return nullptr;
}

moonlight::Identity MoonlightModel::identity() {
    std::lock_guard lock(identityMutex_);
    if (!identity_) {
        std::error_code ec;
        std::filesystem::create_directories(moonlight::dataDirectory(), ec);
        identity_ = moonlight::loadOrCreateIdentity(moonlight::dataDirectory() / L"client.identity");
    }
    return *identity_;
}

// --- state for QML ------------------------------------------------------------------------------

QVariantMap MoonlightModel::settingsOf(const Host* host) const {
    QVariantMap out = defaultSettings();
    const QVariantMap& source = host ? host->settings : defaults_;
    for (auto it = source.begin(); it != source.end(); ++it) out[it.key()] = it.value();
    return out;
}

QVariantMap MoonlightModel::state() const {
    const Host* host = find(selected_);
    return QVariantMap{
        {"busy", busy_}, {"status", status_}, {"pin", pin_}, {"pairing", pairing_}, {"selected", selected_},
        {"settings", settingsOf(host)}, {"discovering", discovery_.running()}, {"ready", loaded_},
        {"paired", host && host->paired}, {"online", host && host->online}, {"runningApp", runningApp_},
        {"hostName", host ? (host->name.isEmpty() ? host->address : host->name) : QString()},
        {"stream", stream_}, {"streaming", streamActive_}, {"lastLabel", lastLabel_},
        {"hdrHost", host && (host->codecSupport & (moonlight::kServerHevcMain10 | moonlight::kServerAv1Main10)) != 0},
        {"av1Host", host && (host->codecSupport & (moonlight::kServerAv1Main8 | moonlight::kServerAv1Main10)) != 0},
        {"hevcHost", host && (host->codecSupport & (moonlight::kServerHevc | moonlight::kServerHevcMain10)) != 0},
    };
}

QVariantList MoonlightModel::hosts() const {
    QVariantList out;
    for (const Host& h : hosts_) {
        out << QVariantMap{
            {"id", h.id}, {"name", h.name.isEmpty() ? h.address : h.name}, {"address", h.address},
            {"state", stateOf(h.online, h.checking, h.paired, h.currentGame)}, {"paired", h.paired}, {"saved", h.saved},
            {"running", h.currentGame != 0}, {"gpu", h.gpu}, {"selected", h.id == selected_},
        };
    }
    return out;
}

// --- worker plumbing -----------------------------------------------------------------------------

void MoonlightModel::startJob(const QString& status, std::function<std::function<void()>(const std::atomic<bool>&)> work) {
    if (busy_) {
        status_ = tr("正在处理上一个操作，请稍候，或先点取消。");
        emit changed();
        return;
    }
    busy_ = true;
    cancel_ = false;
    status_ = status;
    emit changed();
    if (worker_.joinable()) worker_.join();
    worker_ = std::jthread([this, work = std::move(work)] {
        std::function<void()> done;
        try {
            done = work(cancel_);
        } catch (const std::exception& e) {
            const QString text = describe(e);
            done = [this, text] { status_ = text; };
        }
        QMetaObject::invokeMethod(this, [this, done = std::move(done)] {
            busy_ = false;
            pairing_ = false;
            if (done) done();
            emit changed();
        }, Qt::QueuedConnection);
    });
}

void MoonlightModel::cancel() {
    cancel_ = true;
    if (busy_) { status_ = tr("正在取消…"); emit changed(); }
}

void MoonlightModel::applyInfo(Host& host, const moonlight::ServerInfo& info) {
    if (!info.hostname.empty()) host.name = qs(info.hostname);
    host.mac = qs(info.mac);
    host.gpu = qs(info.gpuModel);
    host.version = qs(info.appVersion);
    host.httpsPort = info.httpsPort;
    host.codecSupport = info.serverCodecModeSupport;
    host.nvidia = info.nvidiaServerSoftware;
    // The host may have forgotten us, but only an HTTPS answer can say so. Discovery and
    // first contact ask over plain HTTP, where Sunshine always reports "not paired";
    // trusting that turned every saved pairing into "unpaired" (field report 2026-10-01).
    if (info.pairStatusKnown) host.paired = host.paired && info.paired;
    host.currentGame = info.currentGame;
    host.online = true;
}

// A host that was asked once, off the job queue: used for found hosts and manual adds.
void MoonlightModel::probe(const QString& address, int port, bool manual) {
    std::erase_if(probes_, [](std::jthread& t) { return !t.joinable(); });
    // A host we already know is asked with its saved certificate (over HTTPS), so the
    // answer can confirm the pairing instead of only saying "online".
    QString cert;
    int httpsPort = moonlight::kDefaultHttpsPort;
    bool nvidia = false;
    if (const Host* known = findByAddress(address, port); known && known->paired) {
        cert = known->cert;
        httpsPort = known->httpsPort;
        nvidia = known->nvidia;
    }
    probes_.emplace_back([this, address, port, manual, cert, httpsPort, nvidia](std::stop_token stop) {
        try {
            const moonlight::Identity id = identity();
            moonlight::ServerClient client(id, moonlight::HostAddress{utf8(address), uint16_t(port)}, utf8(cert), uint16_t(httpsPort), !nvidia);
            client.setCancel(&cancel_);
            const moonlight::ServerInfo info = client.serverInfo(true);
            if (stop.stop_requested()) return;
            QMetaObject::invokeMethod(this, [this, address, port, manual, info] {
                Host* known = nullptr;
                for (Host& h : hosts_) if (h.id == qs(info.uniqueId)) known = &h;
                if (!known) known = findByAddress(address, port);
                if (!known) {
                    Host h;
                    h.id = qs(info.uniqueId);
                    h.address = address;
                    h.port = port;
                    hosts_.push_back(std::move(h));
                    known = &hosts_.back();
                }
                known->id = qs(info.uniqueId);
                known->address = address;
                known->port = port;
                known->settings = known->settings.isEmpty() ? defaults_ : known->settings;
                applyInfo(*known, info);
                if (manual) { known->saved = true; selected_ = known->id; saveStore(); reloadApps(); }
                else if (known->saved) saveStore();
                if (selected_.isEmpty() && known->paired) { selected_ = known->id; reloadApps(); }
                emit changed();
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            if (!manual || stop.stop_requested()) return;
            const QString text = describe(e);
            QMetaObject::invokeMethod(this, [this, text] { status_ = text; emit changed(); }, Qt::QueuedConnection);
        }
    });
}

void MoonlightModel::discovered(const QString& address, int port) { probe(address, port, false); }

// --- dialog lifecycle -----------------------------------------------------------------------------

void MoonlightModel::load() {
    if (!loaded_) {
        loadStore();
        loaded_ = true;
        if (selected_.isEmpty()) {
            if (const Host* last = find(lastHost_)) selected_ = last->id;
            else if (!hosts_.empty()) selected_ = hosts_.front().id;
        }
    }
    // VEYRA_MOONLIGHT_NO_DISCOVERY keeps the tests from picking up real hosts on the network.
    if (!discovery_.running() && qEnvironmentVariableIsEmpty("VEYRA_MOONLIGHT_NO_DISCOVERY")) {
        const bool started = discovery_.start([this](const moonlight::DiscoveredHost& host) {
            const QString address = qs(host.address);
            const int port = host.port;
            QMetaObject::invokeMethod(this, [this, address, port] { discovered(address, port); }, Qt::QueuedConnection);
        });
        if (!started) log::warn("moonlight-ui", "network search is not available; hosts must be added by address");
    }
    refresh();
    emit changed();
}

void MoonlightModel::unload() {
    discovery_.stop();
    emit changed();
}

void MoonlightModel::refresh() {
    if (hosts_.empty() || busy_) { emit changed(); return; }
    for (Host& h : hosts_) h.checking = true;
    // One job checks every known host in turn; a dead host costs its connect timeout, not the dialog.
    struct Target { QString id, address, cert; int port, httpsPort; bool nvidia; };
    std::vector<Target> targets;
    for (const Host& h : hosts_) targets.push_back({h.id, h.address, h.cert, h.port, h.httpsPort, h.nvidia});
    startJob(tr("正在检查主机…"), [this, targets](const std::atomic<bool>& cancel) -> std::function<void()> {
        struct Result { QString id; bool online = false; moonlight::ServerInfo info; };
        auto results = std::make_shared<std::vector<Result>>();
        const moonlight::Identity id = identity();
        for (const Target& t : targets) {
            if (cancel.load()) break;
            Result r;
            r.id = t.id;
            try {
                moonlight::ServerClient client(id, moonlight::HostAddress{utf8(t.address), uint16_t(t.port)}, utf8(t.cert), uint16_t(t.httpsPort), !t.nvidia);
                client.setCancel(&cancel);
                r.info = client.serverInfo(true);
                r.online = true;
            } catch (const std::exception&) {}
            results->push_back(std::move(r));
        }
        return [this, results] {
            for (Host& h : hosts_) h.checking = false;
            for (const Result& r : *results) {
                Host* h = find(r.id);
                if (!h) continue;
                if (r.online) applyInfo(*h, r.info);
                else { h->online = false; h->currentGame = 0; }
            }
            bool changedStore = false;
            for (const Host& h : hosts_) changedStore = changedStore || h.saved;
            if (changedStore) saveStore();
            status_.clear();
            const Host* selected = find(selected_);
            if (selected && selected->paired && selected->online) reloadApps();
        };
    });
}

// --- hosts -----------------------------------------------------------------------------------------

void MoonlightModel::addHost(const QString& address) {
    QString text = address.trimmed();
    int port = moonlight::kDefaultHttpPort;
    // "host", "host:port", "[v6]" and "[v6]:port"; a bare IPv6 literal has several colons and no port.
    if (text.startsWith('[')) {
        const int close = text.indexOf(']');
        if (close < 0) { status_ = tr("地址格式不对。"); emit changed(); return; }
        const QString rest = text.mid(close + 1);
        if (rest.startsWith(':')) port = rest.mid(1).toInt();
        text = text.mid(1, close - 1);
    } else if (text.count(':') == 1) {
        port = text.section(':', 1).toInt();
        text = text.section(':', 0, 0);
    }
    if (text.isEmpty() || text.size() > 253 || port < 1 || port > 65535 || text.contains(QRegularExpression("[\\s/\\\\]"))) {
        status_ = tr("请填写主机的 IP 地址或名称，例如 192.168.1.20。");
        emit changed();
        return;
    }
    status_ = tr("正在连接 %1…").arg(text);
    emit changed();
    probe(text, port, true);
}

void MoonlightModel::select(const QString& id) {
    if (selected_ == id) return;
    selected_ = id;
    apps_.clear();
    runningApp_ = 0;
    emit changed();
    const Host* h = find(id);
    if (h && h->paired && h->online) reloadApps();
}

void MoonlightModel::pair(const QString& id) {
    const Host* host = find(id);
    if (!host || busy_) return;
    pin_ = qs(moonlight::generatePin());
    pairing_ = true;
    selected_ = id;
    struct Copy { QString id, address, cert; int port, httpsPort; bool nvidia; };
    const Copy h{host->id, host->address, host->cert, host->port, host->httpsPort, host->nvidia};
    const std::string pin = utf8(pin_);
    startJob(tr("配对中：请在主机的 Sunshine 网页（https://%1:47990 的 PIN 页面）输入上面的 PIN。").arg(h.address),
             [this, h, pin](const std::atomic<bool>& cancel) -> std::function<void()> {
        const moonlight::Identity id = identity();
        // Still trusted with the certificate we saved? Then pairing again is not needed
        // (and pairing an already paired client is what left hosts unreachable before).
        if (!h.cert.isEmpty()) {
            try {
                moonlight::ServerClient check(id, moonlight::HostAddress{utf8(h.address), uint16_t(h.port)}, utf8(h.cert), uint16_t(h.httpsPort), !h.nvidia);
                check.setCancel(&cancel);
                const moonlight::ServerInfo info = check.serverInfo(true);
                if (info.pairStatusKnown && info.paired) {
                    log::info("moonlight-ui", "pair: the host still trusts this computer; no PIN needed");
                    const QString hostId = h.id;
                    return [this, hostId, info] {
                        pin_.clear();
                        if (Host* host = find(hostId)) {
                            applyInfo(*host, info);
                            host->paired = true;
                            host->saved = true;
                            saveStore();
                        }
                        status_ = tr("这台电脑和主机仍是配对状态，不需要重新配对。");
                        if (apps_.isEmpty()) reloadApps();
                    };
                }
            } catch (const std::exception&) {}
        }
        moonlight::ServerClient client(id, moonlight::HostAddress{utf8(h.address), uint16_t(h.port)}, {}, uint16_t(h.httpsPort), !h.nvidia);
        client.setCancel(&cancel);
        const moonlight::ServerInfo info = client.serverInfo(true);
        const moonlight::PairOutcome outcome = moonlight::pairWithHost(client, id, info, pin, &cancel);
        log::info("moonlight-ui", std::format("pair: result={} detail={}", int(outcome.result), outcome.detail));
        const QString hostId = h.id;
        return [this, outcome, hostId, info] {
            pin_.clear();
            Host* host = find(hostId);
            using R = moonlight::PairResult;
            switch (outcome.result) {
            case R::Paired:
                if (host) {
                    host->cert = qs(outcome.serverCertPem);
                    host->paired = true;
                    host->saved = true;
                    applyInfo(*host, info);
                    host->paired = true;
                    saveStore();
                    reloadApps();
                }
                status_ = tr("配对成功。");
                emit notice(tr("已和主机配对"), false);
                break;
            case R::PinWrong: status_ = tr("主机上输入的 PIN 和这里显示的不一致，请重新配对。"); break;
            case R::AlreadyInProgress: status_ = tr("主机正在和另一台设备配对，请稍后再试。"); break;
            case R::Cancelled: status_ = tr("已取消配对。"); break;
            case R::Failed: status_ = tr("配对失败（%1）。请确认主机上的 Sunshine 在运行，并在它的网页里输入了 PIN。").arg(qs(outcome.detail)); break;
            }
        };
    });
}

void MoonlightModel::forget(const QString& id) {
    const Host* host = find(id);
    if (!host || busy_) return;
    const QString hostId = host->id;
    struct Copy { QString address, cert; int port, httpsPort; bool nvidia, paired; };
    const Copy h{host->address, host->cert, host->port, host->httpsPort, host->nvidia, host->paired};
    startJob(tr("正在删除…"), [this, h](const std::atomic<bool>& cancel) -> std::function<void()> {
        // Tell the host to forget us too, when we can; a dead host must not keep the entry alive.
        bool told = false;
        if (h.paired) {
            try {
                moonlight::ServerClient client(identity(), moonlight::HostAddress{utf8(h.address), uint16_t(h.port)}, utf8(h.cert), uint16_t(h.httpsPort), !h.nvidia);
                client.setCancel(&cancel);
                client.unpair();
                told = true;
            } catch (const std::exception&) {}
        }
        return [this, told, h] { status_ = h.paired && !told ? tr("已从本机删除；主机没有回应，它那边的配对记录需要在 Sunshine 网页里自己清除。") : tr("已删除。"); };
    });
    // The list changes at once; the job only talks to the host.
    std::erase_if(hosts_, [&](const Host& x) { return x.id == hostId; });
    if (selected_ == hostId) { selected_.clear(); apps_.clear(); runningApp_ = 0; }
    if (lastHost_ == hostId) { lastHost_.clear(); lastLabel_.clear(); lastApp_ = 0; }
    saveStore();
    emit changed();
}

// --- apps ------------------------------------------------------------------------------------------

void MoonlightModel::reloadApps() {
    const Host* host = find(selected_);
    if (!host || !host->paired || busy_) return;
    struct Copy { QString id, address, cert; int port, httpsPort; bool nvidia; };
    const Copy h{host->id, host->address, host->cert, host->port, host->httpsPort, host->nvidia};
    startJob(tr("正在读取游戏列表…"), [this, h](const std::atomic<bool>& cancel) -> std::function<void()> {
        moonlight::ServerClient client(identity(), moonlight::HostAddress{utf8(h.address), uint16_t(h.port)}, utf8(h.cert), uint16_t(h.httpsPort), !h.nvidia);
        client.setCancel(&cancel);
        const moonlight::ServerInfo info = client.serverInfo(true);
        const std::vector<moonlight::AppEntry> apps = client.appList();
        return [this, h, info, apps] {
            Host* host = find(h.id);
            if (!host) return;
            applyInfo(*host, info);
            if (selected_ != h.id) return;
            apps_.clear();
            for (const auto& a : apps)
                apps_ << QVariantMap{{"id", a.id}, {"name", qs(a.name)}, {"running", info.currentGame != 0 && info.currentGame == a.id}, {"hdr", a.hdrSupported}};
            runningApp_ = info.currentGame;
            status_ = apps.empty() ? tr("主机上没有配置任何游戏或程序（在 Sunshine 网页的 Applications 里添加）。") : QString();
        };
    });
}

void MoonlightModel::quitApp() {
    const Host* host = find(selected_);
    if (!host || !host->paired || busy_) return;
    struct Copy { QString id, address, cert; int port, httpsPort; bool nvidia; };
    const Copy h{host->id, host->address, host->cert, host->port, host->httpsPort, host->nvidia};
    startJob(tr("正在退出主机上的游戏…"), [this, h](const std::atomic<bool>& cancel) -> std::function<void()> {
        moonlight::ServerClient client(identity(), moonlight::HostAddress{utf8(h.address), uint16_t(h.port)}, utf8(h.cert), uint16_t(h.httpsPort), !h.nvidia);
        client.setCancel(&cancel);
        client.quit();
        const moonlight::ServerInfo info = client.serverInfo(true);
        return [this, h, info] {
            if (Host* host = find(h.id)) applyInfo(*host, info);
            for (QVariant& a : apps_) { QVariantMap m = a.toMap(); m["running"] = false; a = m; }
            runningApp_ = info.currentGame;
            status_ = tr("已请求主机退出游戏。");
        };
    });
}

// --- settings ---------------------------------------------------------------------------------------

bool MoonlightModel::set(const QString& key, const QVariant& value) {
    if (!validSetting(key, value)) return false;
    QVariant stored = value;
    if (key == "hdr" || key == "gamepad" || key == "sops" || key == "hostAudio" || key == "captureInput") stored = value.toBool();
    else if (key != "res") stored = value.toInt();
    if (Host* host = find(selected_)) {
        if (host->settings.isEmpty()) host->settings = defaults_;
        host->settings[key] = stored;
    }
    defaults_[key] = stored;   // the next new host starts from the latest choice
    saveStore();
    emit changed();
    return true;
}

source::MoonlightStreamOptions MoonlightModel::optionsFor(const Host* host) const {
    const QVariantMap s = settingsOf(host);
    source::MoonlightStreamOptions o;
    const QString res = s.value("res").toString();
    if (res == "720p") { o.width = 1280; o.height = 720; }
    else if (res == "1440p") { o.width = 2560; o.height = 1440; }
    else if (res == "4k") { o.width = 3840; o.height = 2160; }
    else if (res == "native") {
        if (const QScreen* screen = QGuiApplication::primaryScreen()) {
            const QSize size = screen->size() * screen->devicePixelRatio();
            o.width = std::clamp(size.width() & ~1, 640, 7680);
            o.height = std::clamp(size.height() & ~1, 360, 4320);
        }
    } else { o.width = 1920; o.height = 1080; }
    o.fps = s.value("fps").toInt();
    const int mbps = s.value("bitrate").toInt();
    o.bitrateKbps = mbps > 0 ? mbps * 1000 : 0;
    o.codec = static_cast<moonlight::CodecChoice>(std::clamp(s.value("codec").toInt(), 0, 3));
    o.hdr = s.value("hdr").toBool();
    o.audioChannels = s.value("audio").toInt();
    o.sops = s.value("sops").toBool();
    o.localAudio = s.value("hostAudio").toBool();
    o.gamepadMask = s.value("gamepad").toBool() ? 1 : 0;
    o.av1HardwareDecode = false;   // AV1 only on request until the decoder reports what the GPU can do
    return o;
}

// --- starting a stream ---------------------------------------------------------------------------------

bool MoonlightModel::connectStream(int appId) { return launchSelected(appId); }

bool MoonlightModel::resumeLast() {
    if (!loaded_) { loadStore(); loaded_ = true; }
    if (lastHost_.isEmpty() || lastApp_ < 0 || !find(lastHost_)) return false;
    selected_ = lastHost_;
    return launchSelected(lastApp_);
}

bool MoonlightModel::launchSelected(int appId) {
    const Host* host = find(selected_);
    if (!host || !host->paired) { status_ = tr("请先和这台主机配对。"); emit changed(); return false; }
    if (busy_ || !launch_) return false;
    const source::MoonlightStreamOptions options = optionsFor(host);
    const bool captureInput = settingsOf(host).value("captureInput").toBool();
    QString appName;
    for (const QVariant& a : std::as_const(apps_)) if (a.toMap().value("id").toInt() == appId) appName = a.toMap().value("name").toString();
    if (appName.isEmpty() && appId == lastApp_ && host->id == lastHost_) appName = lastLabel_.section(" · ", 1);
    const QString hostName = host->name.isEmpty() ? host->address : host->name;
    struct Copy { QString id, address, cert; int port, httpsPort; bool nvidia; };
    const Copy h{host->id, host->address, host->cert, host->port, host->httpsPort, host->nvidia};
    startJob(tr("正在联系主机…"), [this, h, appId, options, captureInput, appName, hostName](const std::atomic<bool>& cancel) -> std::function<void()> {
        const moonlight::Identity id = identity();
        moonlight::ServerClient client(id, moonlight::HostAddress{utf8(h.address), uint16_t(h.port)}, utf8(h.cert), uint16_t(h.httpsPort), !h.nvidia);
        client.setCancel(&cancel);
        const moonlight::ServerInfo info = client.serverInfo();
        if (info.pairStatusKnown && !info.paired) {
            log::warn("moonlight-ui", "launch: the host no longer trusts this computer");
            const QString hostId = h.id;
            return [this, hostId] {
                if (Host* host = find(hostId)) { host->paired = false; saveStore(); }
                status_ = tr("主机不再认得这台电脑（可能在 Sunshine 里被删除了），请重新配对。");
            };
        }
        if (info.currentGame != 0 && info.currentGame != appId)
            return [this] { status_ = tr("主机正在运行另一个游戏。先选它继续，或点「退出游戏」结束它（未保存的进度会丢失），再开始新的。"); };
        if (moonlight::chooseVideoFormats(options.codec, options.hdr, info.serverCodecModeSupport, options.av1HardwareDecode) == 0)
            return [this, hdr = options.hdr] { status_ = hdr ? tr("这台主机没有 10 位（HDR）编码能力，或选择的编码不支持 HDR。请关闭 HDR，或换编码。")
                                                          : tr("主机不支持所选的编码，请改成「自动」。"); };
        auto launch = std::make_shared<Launch>();
        launch->desc.identity = id;
        launch->desc.address = moonlight::HostAddress{utf8(h.address), uint16_t(h.port)};
        launch->desc.httpsPort = info.httpsPort;
        launch->desc.serverCertPem = utf8(h.cert);
        launch->desc.host = info;
        launch->desc.appId = appId;
        launch->desc.resume = info.currentGame == appId && appId != 0;
        launch->desc.options = options;
        launch->label = appName.isEmpty() ? hostName : hostName + QStringLiteral(" · ") + appName;
        launch->captureInput = captureInput;
        const QString hostId = h.id;
        return [this, launch, hostId, appId] {
            const QString label = launch->label;   // the handler takes the whole launch
            if (!launch_ || !launch_(std::move(*launch))) {
                status_ = tr("无法开始串流，请查看日志。");
                return;
            }
            lastHost_ = hostId;
            lastApp_ = appId;
            lastLabel_ = label;
            saveStore();
            status_ = tr("正在建立串流…");
            emit started();
        };
    });
    return true;
}

// --- stream state from the bridge ---------------------------------------------------------------------

void MoonlightModel::updateStream(bool active, const source::MoonlightStats& s) {
    QVariantMap now;
    if (active) {
        const char* names[] = {"idle", "launching", "connecting", "streaming", "ended", "failed"};
        now = QVariantMap{
            {"state", names[size_t(s.state)]}, {"message", QString::fromStdWString(s.message)},
            {"codec", qs(s.codec)}, {"hdr", s.hdr}, {"hardware", s.hardwareDecode},
            {"width", int(s.width)}, {"height", int(s.height)}, {"fps", s.fps},
            {"receivedFps", s.receivedFps}, {"decodedFps", s.decodedFps}, {"videoMbps", s.videoMbps},
            {"hostMs", s.hostLatencyMs}, {"receiveMs", s.receiveMs}, {"queueMs", s.queueMs}, {"decodeMs", s.decodeMs},
            {"rttMs", s.rttMs}, {"rttVarianceMs", s.rttVarianceMs},
            {"lost", double(s.fecFailed)}, {"recovered", double(s.fecRecovered)}, {"packets", double(s.videoPackets)},
            {"dropped", double(s.dropped)}, {"decodeErrors", double(s.decodeErrors)},
        };
    }
    if (now == stream_ && active == streamActive_) return;
    stream_ = now;
    streamActive_ = active;
    emit changed();
}

} // namespace veyra::ui
