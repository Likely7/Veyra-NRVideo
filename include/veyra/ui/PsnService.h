// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QVariantList>
#include <QUrl>
#include <filesystem>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;
namespace veyra::remoteplay { struct NativeConnectRequest; }
namespace veyra::ui {
// Sony passwords stay in the browser. Only the current Windows user can decrypt
// this service's account file. Tokens never enter QML, logs or a command line.
class PsnService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool awaitingRedirect READ awaitingRedirect NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString accountId READ accountId NOTIFY changed)
    Q_PROPERTY(QVariantList consoles READ consoles NOTIFY changed)
public:
    explicit PsnService(std::filesystem::path storageDir, QObject* parent=nullptr,
                        QNetworkAccessManager* network=nullptr);
    ~PsnService() override;
    bool busy() const { return busy_; }
    bool signedIn() const;
    bool awaitingRedirect() const { return !loginState_.isEmpty(); }
    QString status() const { return status_; }
    QString accountId() const { return accountId_; }
    QVariantList consoles() const { return consoles_; }
    Q_INVOKABLE void beginLogin();
    Q_INVOKABLE void completeLogin(const QString& callbackUrl);
    Q_INVOKABLE void refreshConsoles();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void cancel();
    // Fills only transport/account fields (and PS4's H.264 restriction).
    // When a refresh is needed returns false; the user retries after changed().
    bool prepareConnection(const QString& duid, remoteplay::NativeConnectRequest& request);
signals:
    void changed();
private:
    friend struct PsnServiceTestAccess;
    struct Account {
        QByteArray access, refresh;
        QString id;
        qint64 expiry=0;
        ~Account();
        void clear();
    } account_, pending_;
    using JsonHandler = std::function<void(const QJsonObject&)>;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    std::filesystem::path directory_;
    QString accountId_, status_, loginState_;
    qint64 loginStarted_=0;
    bool busy_=false, refreshing_=false, saveFailed_=false;
    QVariantList consoles_, discovered_;
    QUrl authorizationUrl();
    void tokenRequest(const QByteArray& code, bool refresh);
    void accountRequest();
    void acceptAccount();
    void fetchConsoles(int offset=0);
    void requestJson(const QUrl&, const QByteArray& authorization,
                     const QByteArray& body, bool post, JsonHandler);
    void fail(const QString& message);
    void abortRequest();
    bool saveAccount();
    void loadAccount();
    static bool validToken(const QByteArray&);
    static bool validDuid(const QString&);
    static QVariantList parseConsoles(const QJsonObject&);
};
}
