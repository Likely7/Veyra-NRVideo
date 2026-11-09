#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QWinEventNotifier>
#include <memory>

namespace veyra::ui {

class VirtualCaptureAudioSetup final : public QObject {
    Q_OBJECT
public:
    explicit VirtualCaptureAudioSetup(QObject* parent);
    ~VirtualCaptureAudioSetup() override;
    bool busy() const { return bool(staging_); }
    void start(const QString& dataDirectory);
    static bool openRecordingSettings();
signals:
    void busyChanged();
    void completed(const QString& message, bool warning);
private:
    void downloadFinished();
    void launchInstaller();
    void finish(const QString& message, bool warning);

    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QByteArray download_;
    QProcess extractor_;
    QTimer deadline_;
    QWinEventNotifier installer_;
    std::unique_ptr<QTemporaryDir> staging_;
};
}
