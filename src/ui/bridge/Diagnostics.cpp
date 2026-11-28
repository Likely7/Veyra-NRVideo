// SPDX-License-Identifier: GPL-3.0-only
// QmlPlayerBridge domain: 设置 → 关于 → 性能诊断. The session object owns the
// recording; this file only hands it what the bridge knows.
#include "../QmlPlayerBridge_p.h"
#include "../PerfDiagnosticsSystem.h"
#include "veyra/Log.h"
#include "veyra/diagnostics/DiagnosticTrace.h"
#include "veyra/ui/PerfDiagnostics.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace veyra::ui {

void QmlPlayerBridge::createPerfDiagnostics() {
    PerfDiagnostics::Hooks hooks;
    hooks.sample = [this] {
        const auto& p = impl_->snapshot;
        perfdiag::EngineSample s;
        s.host = diagnostics::traceHostNow();
        s.running = p.running; s.capture = p.capture; s.remotePlay = p.remotePlay; s.applying = p.applying;
        s.fgBudgetLimited = p.fgBudgetLimited;
        s.submitFps = p.submissionFps.value_or(-1.0);
        s.low1Fps = p.submissionLow1Fps.value_or(-1.0);
        s.captureFps = p.captureFps; s.lateMs = p.lateMs; s.lateP95Ms = p.lateP95Ms;
        s.schedulingWaitP95Ms = p.schedulingWaitP95Ms; s.processCpuP95Ms = p.processCpuP95Ms; s.presentCpuP95Ms = p.presentCpuP95Ms;
        s.nominalSourceFps = p.nominalSourceFps; s.displayRefreshHz = p.displayFgRefreshHz;
        s.frames = p.frames; s.generated = p.generated; s.captureReceived = p.captureReceived;
        s.captureDropped = p.captureDropped; s.captureRateSkipped = p.captureRateSkipped; s.previewSkipped = p.previewSkipped;
        s.displayVrr = p.displayVrr; s.compositionMode = p.displayCompositionMode; s.contentFps = p.contentFps;
        s.sourceWidth = p.sourceWidth; s.sourceHeight = p.sourceHeight;
        return s;
    };
    hooks.context = [this] {
        QJsonObject o;
        o["version"] = version();
        o["executable"] = perfdiag::redactText(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        o["dataDirectory"] = perfdiag::redactText(QDir::toNativeSeparators(dataDirectory()));
        o["logFile"] = perfdiag::redactText(QDir::toNativeSeparators(logFile()));
        o["diagnosticsReport"] = perfdiag::redactText(diagnosticsReport());
        // The saved preferences, with the account name taken out of every path.
        const QByteArray prefs = QJsonDocument(QJsonObject::fromVariantMap(impl_->prefs)).toJson(QJsonDocument::Compact);
        o["preferences"] = QJsonDocument::fromJson(perfdiag::redactText(QString::fromUtf8(prefs)).toUtf8()).object();
        const auto& s = impl_->snapshot;
        o["source"] = QJsonObject{{"running", s.running}, {"capture", s.capture}, {"remotePlay", s.remotePlay}, {"image", s.image},
                                  {"width", int(s.sourceWidth)}, {"height", int(s.sourceHeight)}, {"nominalFps", s.nominalSourceFps},
                                  {"hdr", s.sourceHdr}, {"contentFps", s.contentFps}};
        return o;
    };
    hooks.logFile = [this] { return logFile(); };
    hooks.outputRoot = [this] { return recordingDirectory(); };
    hooks.flushLog = [] { veyra::Logger::instance().flush(); };
    auto* session = new PerfDiagnostics(std::move(hooks), this);
    perfDiagnostics_ = session;
    connect(session, &PerfDiagnostics::notice, this, &QmlPlayerBridge::notice);
}

QObject* QmlPlayerBridge::perfDiagnostics() const { return perfDiagnostics_; }

} // namespace veyra::ui
