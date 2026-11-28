// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// 设置 → 关于 → 性能诊断: a timed recording of the frame trace, a once-per-second
// system sample, the environment snapshot and the log written meanwhile. When
// it ends, veyra_frame_analyzer.exe (next to this program) writes the report
// and the folder is packed into Veyra诊断_<date>_<time>.zip. Nothing is sent
// anywhere; the user decides whom to give the package to.
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace veyra::ui {
namespace perfdiag { class TelemetrySampler; struct EngineSample; }

class PerfDiagnostics : public QObject {
    Q_OBJECT
    // idle | recording | analyzing | done | failed
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY tick)
    Q_PROPERTY(int durationSeconds READ durationSeconds NOTIFY changed)
    Q_PROPERTY(int marks READ marks NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // [{severity: "high"|"medium"|"info", title, advice, evidence}], at most 3.
    Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
    Q_PROPERTY(QString headline READ headline NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QString zipPath READ zipPath NOTIFY changed)
    Q_PROPERTY(QString outputRoot READ outputRoot NOTIFY changed)
    Q_PROPERTY(bool analyzerAvailable READ analyzerAvailable CONSTANT)
public:
    struct Hooks {
        std::function<perfdiag::EngineSample()> sample;   // interface thread
        std::function<QJsonObject()> context;             // version, preferences, source …
        std::function<QString()> logFile;
        std::function<QString()> outputRoot;              // the recordings folder
        std::function<void()> flushLog;
    };
    explicit PerfDiagnostics(Hooks hooks, QObject* parent = nullptr);
    ~PerfDiagnostics() override;

    QString state() const { return state_; }
    int elapsedSeconds() const;
    int durationSeconds() const { return durationSeconds_; }
    int marks() const { return marks_; }
    QString message() const { return message_; }
    QVariantList findings() const { return findings_; }
    QString headline() const { return headline_; }
    QString folder() const { return folder_; }
    QString zipPath() const { return zip_; }
    QString outputRoot() const;
    bool analyzerAvailable() const;

    // 3, 5 or 10 minutes from the dialog; 1–30 accepted.
    Q_INVOKABLE bool start(int minutes);
    // Automation (--diagnostic-trace): any root, 30 s – 30 min.
    Q_INVOKABLE bool startIn(const QString& root, int seconds);
    // Ends early; the part recorded so far is analysed.
    Q_INVOKABLE void stop();
    Q_INVOKABLE void mark();
    Q_INVOKABLE void openReport() const;
    Q_INVOKABLE void openFolder() const;
    Q_INVOKABLE void copyPath();
    // Result seen: back to idle.
    Q_INVOKABLE void dismiss();

    // Ends a running recording without waiting for analysis (application exit).
    void shutdown();

signals:
    void changed();
    void tick();
    void marked(int number);
    void finished(bool ok);
    void notice(const QString& text, bool error);

private:
    bool begin(const QString& root, int seconds);
    void sampleEngine();
    void finish(bool early);
    void finishWorker(QJsonObject events, QString logFile, bool early);
    void finished_(bool ok, QString message, QString zip);
    void setState(const QString& state, const QString& message = {});
    QJsonObject bridgeProperties() const;
    void bridgeChanged();

    Hooks hooks_;
    QString state_ = QStringLiteral("idle"), message_, headline_, folder_, zip_;
    QVariantList findings_;
    int durationSeconds_ = 0, marks_ = 0;
    int64_t startedHost_ = 0;
    qint64 logOffset_ = 0;
    QString logPath_;
    QTimer timer_, sampleTimer_;
    std::unique_ptr<perfdiag::TelemetrySampler> sampler_;
    std::thread worker_;
    QJsonArray events_;
    QJsonObject lastProperties_;
    QHash<QString, int> changeCounts_;
};

}
