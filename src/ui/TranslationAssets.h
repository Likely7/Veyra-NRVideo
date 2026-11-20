// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <memory>
#include <span>

class QLockFile;
class QSaveFile;
class QTemporaryDir;

namespace veyra::ui {

struct ModelFile {
    const char* name;
    const char* sha256;
    qint64 bytes = -1;
};

struct ModelAsset {
    const char* destination;
    const char* url;
    qint64 bytes;
    const char* sha256;
    std::span<const ModelFile> members{};
    const char* archivePrefix = "";
    int stripComponents = 0;
    bool verifyExisting = false;
};

class ModelAssets final : public QObject {
    Q_OBJECT
public:
    ModelAssets(QString root, std::span<const ModelAsset> assets);
    ~ModelAssets() override;
    bool setAssets(std::span<const ModelAsset> assets);
    bool installed() const;
    qint64 totalBytes() const;
    qint64 received() const { return received_; }
    bool downloading() const { return bool(lock_); }
    QString status() const { return status_; }
    void download();
    void cancel();
    bool remove(const ModelAsset& asset);

    static QString path(const QString& root, const ModelAsset& asset);
    static bool verify(const QString& root, const ModelAsset& asset, bool contents);
signals:
    void changed();
private:
    void next();
    void readFile();
    void finishFile();
    void extractArchive();
    void finishArchive(int code, QProcess::ExitStatus status);
    void finish(QString status);

    const QString root_;
    std::span<const ModelAsset> assets_;
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QProcess extract_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QTemporaryDir> stage_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    bool cancelled_ = false;
    size_t asset_ = 0;
    qint64 bytes_ = 0;
    qint64 complete_ = 0;
    qint64 received_ = 0;
    QString status_;
};

class TranslationAssets final {
public:
    explicit TranslationAssets(const QString& dataDirectory);
    QString runtimePath() const;
    QString modelPath() const;
    bool verifyRuntime() const;

    ModelAssets downloads;
private:
    const QString root_;
};
}
