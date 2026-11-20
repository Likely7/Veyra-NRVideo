#pragma once

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <memory>

class QLockFile;
class QSaveFile;
class QTemporaryDir;

namespace veyra::ui {
class TranslationAssets final : public QObject {
    Q_OBJECT
public:
    explicit TranslationAssets(const QString& dataDirectory, QObject* parent = nullptr);
    ~TranslationAssets() override;
    QString runtimePath() const;
    QString modelPath() const;
    bool installed() const;
    bool verifyRuntime() const;
    qint64 totalBytes() const;
    qint64 received() const { return received_; }
    bool downloading() const { return bool(lock_); }
    QString status() const { return status_; }
    void download();
    void cancel();
signals:
    void changed();
private:
    void next();
    void startFile(bool runtime);
    void readFile();
    void finishFile();
    void finish(QString status);
    void extractRuntime();

    const QString root_;
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QProcess extract_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QTemporaryDir> stage_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    bool runtimeFile_ = false;
    bool cancelled_ = false;
    qint64 bytes_ = 0;
    qint64 received_ = 0;
    QString status_;
};
}
