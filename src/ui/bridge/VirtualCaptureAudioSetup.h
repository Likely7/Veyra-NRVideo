#pragma once

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
    void launchInstaller();
    void finish(const QString& message, bool warning);

    QProcess extractor_;
    QTimer deadline_;
    QWinEventNotifier installer_;
    std::unique_ptr<QTemporaryDir> staging_;
};
}
