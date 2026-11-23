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

struct ModelSource {
    const char* url;
    bool memberFiles = false; // URL prefix for the asset's existing members.
    qint64 bytes = -1;        // Optional pinned identity for an alternate file.
    const char* sha256 = "";
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
    std::span<const ModelSource> fallbacks{};
    bool allowAdditionalFiles = false;
};

class ModelAssets final : public QObject {
    Q_OBJECT
public:
    ModelAssets(QString root, std::span<const ModelAsset> assets);
    ~ModelAssets() override;
    bool setAssets(std::span<const ModelAsset> assets);
    bool installed() const { return installed_; }
    void refreshInstalled();
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
    void startSource();
    void startFile();
    const ModelSource* fallback() const;
    bool downloadsMembers() const;
    void retry(QString reason);
    void clearReply();
    void installDirectory();
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
    std::unique_ptr<QTemporaryDir> stage_, transfer_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    bool installed_ = false, cancelled_ = false;
    size_t asset_ = 0, source_ = 0, member_ = 0;
    qint64 expectedBytes_ = 0, sourceBytes_ = 0, verifiedBytes_ = 0;
    const char* expectedSha_ = "";
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
    void refreshInstalled();
    bool installed() const { return vulkanInstalled() || cpuInstalled_; }
    bool vulkanInstalled() const { return downloads.installed(); }

    ModelAssets downloads;
private:
    const QString root_;
    bool cpuInstalled_ = false;
};
}
