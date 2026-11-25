#pragma once
#include "WindowInputCapture.h"
#include "Ps5InputDevices.h"
#include "veyra/remoteplay/KeyboardMouse.h"
#include <QJsonObject>
#include <QPointer>
#include <QWindow>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <memory>

namespace veyra::ui {
class Ps5KeyboardSettings final : public QObject, private moonlight::InputSink {
    Q_OBJECT
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY changed)
    Q_PROPERTY(QVariantMap presetProfile READ presetProfile CONSTANT)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QStringList situations READ situations NOTIFY changed)
    Q_PROPERTY(QString selected READ selected NOTIFY changed)
    Q_PROPERTY(int dpi READ dpi NOTIFY changed)
    Q_PROPERTY(QString overlayKey READ overlayKey NOTIFY changed)
    Q_PROPERTY(QString controlKey READ controlKey NOTIFY changed)
    Q_PROPERTY(bool showShortcutHints READ showShortcutHints NOTIFY changed)
    Q_PROPERTY(QStringList shortcutKeys READ shortcutKeys CONSTANT)
    Q_PROPERTY(QVariantMap shortcutConflict READ shortcutConflict NOTIFY changed)
    Q_PROPERTY(bool inputCheckOffered READ inputCheckOffered NOTIFY changed)
    Q_PROPERTY(QObject* inputDevices READ inputDevices CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY sessionChanged)
    Q_PROPERTY(bool captured READ captured NOTIFY sessionChanged)
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY sessionChanged)
    Q_PROPERTY(QString situation READ situation NOTIFY sessionChanged)
public:
    using Session=std::shared_ptr<remoteplay::KeyboardMouse>;
    Ps5KeyboardSettings(QString directory, QString presetPath, std::function<Session()> provider,
                        WindowInputCapture::MouseGrab mouseGrab = {});
    ~Ps5KeyboardSettings() override;
    QVariantMap profile() const;
    QVariantMap presetProfile() const { return preset_.toVariantMap(); }
    QStringList profiles() const;
    QStringList situations() const;
    QString selected() const;
    int dpi() const;
    QString overlayKey() const;
    QString controlKey() const;
    bool showShortcutHints() const { return store_.value("showShortcutHints").toBool(true); }
    Q_INVOKABLE bool setShowShortcutHints(bool show);
    QStringList shortcutKeys() const;
    QVariantMap shortcutConflict() const { return shortcutConflict_; }
    Q_INVOKABLE bool setShortcut(const QString& action, const QString& key);
    Q_INVOKABLE bool resolveShortcut(const QString& side, const QString& key);
    Q_INVOKABLE void cancelShortcutConflict();
    bool inputCheckOffered() const { return store_.value("inputCheckOffered").toBool(); }
    QObject* inputDevices() { return &devices_; }
    Q_INVOKABLE bool acknowledgeInputCheck();
    QString error() const { return error_; }
    bool available() const { return available_; }
    bool captured() const { return capture_.captured(); }
    bool overlayVisible() const { return overlayVisible_; }
    Q_INVOKABLE QString keyCode(int qtKey, int nativeScanCode) const;
    Q_INVOKABLE void setPlayerWindow(QWindow* window, bool shortcutsEnabled);
    QString situation() const { return situation_; }
    Q_INVOKABLE bool choose(const QString& name);
    Q_INVOKABLE bool saveCopy(QString name, const QVariantMap& profile);
    Q_INVOKABLE bool setDpi(int value);
    Q_INVOKABLE bool importProfile(QUrl file, QString name);
    Q_INVOKABLE bool exportProfile(QUrl file);
    Q_INVOKABLE void edit(bool open);
    Q_INVOKABLE bool resume();
    Q_INVOKABLE void release();
    Q_INVOKABLE void correct(QString name);
signals:
    void changed();
    void sessionChanged();
    void controlRequested();
private:
    bool fail(QString error);
    QJsonObject selectedProfile(const QJsonObject& store) const;
    bool stageChange(QJsonObject next, bool editedCopy, const QString& firstAction = {});
    bool hasShortcutConflict(const QJsonObject& next, bool editedCopy, const QString& firstAction = {});
    bool commit(QJsonObject next, bool editedCopy = false);
    bool writeCopy(QString name, const QJsonObject& doc, bool editedCopy);
    bool validate(const QByteArray& json);
    void refresh();
    void refreshSession(const Session& session);
    bool localKey(uint32_t scan, bool extended, bool down, bool repeat) override;
    void key(uint32_t vk,uint32_t scan,bool extended,bool down) override;
    void mouseMove(int dx,int dy) override;
    void mouseButton(int button,bool down) override;
    void scroll(int delta,bool horizontal) override;
    QString directory_, error_, situation_;
    QJsonObject preset_, store_;
    QJsonObject pendingStore_;
    QVariantMap shortcutConflict_;
    bool pendingEditedCopy_ = false;
    std::function<Session()> provider_;
    std::weak_ptr<remoteplay::KeyboardMouse> connection_;
    Session playing_;
    QStringList playingContexts_;
    bool editing_=false, available_=false;
    bool overlayVisible_ = false;
    QPointer<QWindow> playerWindow_;
    WindowInputCapture capture_;
    Ps5InputDevices devices_;
};
}
