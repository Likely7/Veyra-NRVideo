// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The "PC 串流" side of the UI: known hosts (saved, or found on the network), pairing, the app list of
// the selected host, the stream-core settings and starting a stream. All network work runs on worker
// threads and reports back on the UI thread; nothing here blocks the interface. The effect chain is not
// part of it (it is the software's own, shared by every source).
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "veyra/moonlight/Discovery.h"
#include "veyra/moonlight/IdentityStore.h"
#include "veyra/source/MoonlightSessionSource.h"

namespace veyra::ui {

class MoonlightModel final : public QObject {
    Q_OBJECT
    // {busy, status, pin, pairing, selected, settings{...}, discovering, stream{...}, ready}
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    // [{id, name, address, state: online|unpaired|busy|offline|checking, paired, running, gpu, selected}]
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY changed)
    // [{id, name, running, hdr}] of the selected host
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
public:
    struct Launch {
        source::MoonlightConnectDesc desc;
        QString label;          // "host · app", for the source title
        bool captureInput = true;
    };
    // Starts the stream (the bridge opens the engine session); false if it could not.
    using LaunchFn = std::function<bool(Launch)>;

    explicit MoonlightModel(QObject* parent = nullptr);
    ~MoonlightModel() override;

    void setLaunchHandler(LaunchFn handler) { launch_ = std::move(handler); }
    // Fed by the bridge while a session runs (a few times a second).
    void updateStream(bool active, const source::MoonlightStats& stats);

    QVariantMap state() const;
    QVariantList hosts() const;
    QVariantList apps() const { return apps_; }

    // Dialog opened / closed: loads the store, starts the network search and a status refresh.
    Q_INVOKABLE void load();
    Q_INVOKABLE void unload();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addHost(const QString& address);
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void pair(const QString& id);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void forget(const QString& id);
    Q_INVOKABLE void quitApp();
    Q_INVOKABLE bool connectStream(int appId);
    Q_INVOKABLE bool resumeLast();
    Q_INVOKABLE bool set(const QString& key, const QVariant& value);

    // For "继续上次" and the home card.
    int hostCount() const { return int(hosts_.size()); }
    QString lastLabel() const { return lastLabel_; }

signals:
    void changed();
    void started();   // the stream was handed to the player; the dialog can close
    void notice(const QString& text, bool isError);

private:
    struct Host {
        QString id, name, address, mac, cert, gpu, version;
        int port = moonlight::kDefaultHttpPort, httpsPort = moonlight::kDefaultHttpsPort;
        int codecSupport = moonlight::kServerH264;
        bool paired = false, nvidia = false, saved = false;
        QVariantMap settings;
        // not persisted
        bool online = false, checking = false;
        int currentGame = 0;
    };
    void loadStore();
    bool saveStore() const;
    Host* find(const QString& id);
    const Host* find(const QString& id) const;
    Host* findByAddress(const QString& address, int port);
    QVariantMap settingsOf(const Host* host) const;
    source::MoonlightStreamOptions optionsFor(const Host* host) const;
    void startJob(const QString& status, std::function<std::function<void()>(const std::atomic<bool>&)> work);
    void probe(const QString& address, int port, bool manual);
    void discovered(const QString& address, int port);
    void applyInfo(Host& host, const moonlight::ServerInfo& info);
    void reloadApps();
    moonlight::Identity identity();
    bool launchSelected(int appId);

    LaunchFn launch_;
    std::vector<Host> hosts_;
    QVariantList apps_;
    QVariantMap defaults_;
    QString selected_, status_, pin_, lastHost_, lastLabel_;
    int lastApp_ = 0;
    bool busy_ = false, pairing_ = false, loaded_ = false, streamActive_ = false;
    QVariantMap stream_;
    int runningApp_ = 0;

    std::mutex identityMutex_;
    std::optional<moonlight::Identity> identity_;
    std::atomic<bool> cancel_{false};
    moonlight::Discovery discovery_;
    std::vector<std::jthread> probes_;   // short serverinfo checks of found hosts
    std::jthread worker_;                // last: joined first
};

} // namespace veyra::ui
