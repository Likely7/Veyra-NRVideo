// SPDX-License-Identifier: GPL-3.0-only
// 性能诊断 session: see include/veyra/ui/PerfDiagnostics.h and docs/PERF_DIAGNOSTICS.md.
#include "veyra/ui/PerfDiagnostics.h"
#include "PerfDiagnosticsSystem.h"
#include "veyra/Log.h"
#include "veyra/diagnostics/DiagnosticTrace.h"
#include "veyra/ui/UiLanguage.h"
#include <windows.h>
#include <shellapi.h>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <format>

namespace veyra::ui {
namespace {
std::atomic<bool> g_cancelWorker{false};
std::atomic<HANDLE> g_workerChild{nullptr};

QString analyzerExe() { return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("veyra_frame_analyzer.exe")); }

std::wstring quoted(const QString& text) { return L"\"" + QDir::toNativeSeparators(text).toStdWString() + L"\""; }

// Runs a child with only its log file inherited; returns the exit code or -1.
int runChild(const QString& exe, const std::wstring& arguments, const QString& workingDir, const QString& logPath,
             DWORD timeoutMs, DWORD priorityClass) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    const HANDLE log = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(logPath).utf16()), FILE_APPEND_DATA,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return -1;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> attributeBuffer(attributeBytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
    HANDLE handles[1] = {log};
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr)) {
        CloseHandle(log);
        return -1;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = log;
    startup.StartupInfo.hStdError = log;
    std::wstring command = quoted(exe) + L" " + arguments;
    PROCESS_INFORMATION process{};
    const std::wstring cwd = QDir::toNativeSeparators(workingDir).toStdWString();
    const BOOL created = CreateProcessW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(exe).utf16()), command.data(),
                                        nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | priorityClass,
                                        nullptr, cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(log);
    if (!created) return -1;
    CloseHandle(process.hThread);
    g_workerChild = process.hProcess;
    DWORD waited = 0, wait = WAIT_TIMEOUT;
    while (waited < timeoutMs) {
        wait = WaitForSingleObject(process.hProcess, 250);
        if (wait != WAIT_TIMEOUT || g_cancelWorker) break;
        waited += 250;
    }
    if (wait != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 124); WaitForSingleObject(process.hProcess, 3000); }
    DWORD code = DWORD(-1);
    GetExitCodeProcess(process.hProcess, &code);
    g_workerChild = nullptr;
    CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 ? int(code) : -1;
}

bool sensitiveName(const QString& name) {
    // camelCase words: remotePlayPin -> remote play pin
    static const QRegularExpression split(QStringLiteral("(?<=[a-z0-9])(?=[A-Z])"));
    static const QSet<QString> words = {QStringLiteral("pin"), QStringLiteral("host"), QStringLiteral("account"), QStringLiteral("token"),
        QStringLiteral("password"), QStringLiteral("secret"), QStringLiteral("credential"), QStringLiteral("email"), QStringLiteral("serial"),
        QStringLiteral("address"), QStringLiteral("ip"), QStringLiteral("mac"), QStringLiteral("psn"), QStringLiteral("regist"),
        QStringLiteral("registration"), QStringLiteral("cookie"), QStringLiteral("code"), QStringLiteral("url"), QStringLiteral("login")};
    for (const auto& word : name.split(split)) if (words.contains(word.toLower())) return true;
    return false;
}

QJsonValue simpleValue(const QVariant& value) {
    switch (value.metaType().id()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
        return double(value.toLongLong());
    case QMetaType::Double: case QMetaType::Float: return value.toDouble();
    case QMetaType::QString: {
        const QString text = value.toString();
        return text.size() > 400 ? perfdiag::redactText(text.left(400)) + QStringLiteral("…") : perfdiag::redactText(text);
    }
    default: return QJsonValue(QJsonValue::Undefined);
    }
}

QString causeText(const QString& id) {
    if (id == QLatin1String("input")) return PerfDiagnostics::tr("输入断档（采集或解码没按时送来新画面）");
    if (id == QLatin1String("engine")) return PerfDiagnostics::tr("引擎排队（CPU 侧处理晚了）");
    if (id == QLatin1String("gpu")) return PerfDiagnostics::tr("GPU 处理超时");
    if (id == QLatin1String("present")) return PerfDiagnostics::tr("送显调用阻塞（驱动、系统或叠加层）");
    if (id == QLatin1String("pacing")) return PerfDiagnostics::tr("节拍等待");
    if (id == QLatin1String("discard")) return PerfDiagnostics::tr("生成帧被跳过或丢弃（补帧没赶上）");
    if (id == QLatin1String("reset")) return PerfDiagnostics::tr("重建（改设置、切换或跳转）");
    return PerfDiagnostics::tr("未能归因");
}

// The dialog shows each rule in the interface language; the report files stay
// in Chinese with the same rule IDs (docs/PERF_DIAGNOSTICS.md).
std::pair<QString, QString> findingText(const QString& id, const QJsonObject& params) {
    using P = PerfDiagnostics;
    if (id == QLatin1String("R1")) return {P::tr("NVIDIA Smooth Motion 正作用于 Veyra"), P::tr("在 NVIDIA App → 图形 → 程序设置里对 Veyra（veyra_qml_ui.exe）关闭 Smooth Motion。")};
    if (id == QLatin1String("R2")) return {P::tr("%1 注入了 Veyra").arg(params.value(QStringLiteral("product")).toString()), P::tr("关闭它，或在它的设置里把 veyra_qml_ui.exe 加入排除列表。")};
    if (id == QLatin1String("R3")) return {P::tr("驱动可能强制关闭了垂直同步（会撕裂）"), P::tr("NVIDIA 控制面板 → 管理 3D 设置 → 程序设置：把 veyra_qml_ui.exe 的“垂直同步”改为“使用 3D 应用程序设置”。")};
    if (id == QLatin1String("R4")) return {P::tr("显示器的可变刷新正在跟随 Veyra"), P::tr("若画面闪烁或有果冻感：在 NVIDIA 控制面板对 Veyra 设“显示器技术：固定刷新”，或在 Veyra 里开启可变刷新。")};
    if (id == QLatin1String("R5")) return {P::tr("卡顿按固定周期出现（约每 %1 秒）").arg(params.value(QStringLiteral("periodSeconds")).toDouble(), 0, 'f', 1), P::tr("通常是输出帧率与屏幕刷新率不成整数倍造成的拍频：让输出帧率等于刷新率或其整数分之一，或开启可变刷新。")};
    if (id == QLatin1String("R6")) return {P::tr("采集卡送帧时间不均匀"), P::tr("换 USB 口（直连机身 USB 3）或换线，关闭 USB 选择性暂停；可换一张采集卡对比。")};
    if (id == QLatin1String("R7")) return {P::tr("采集卡时钟与系统时钟漂移"), P::tr("长时间运行后若帧率减半，拔插采集卡即可恢复；可向采集卡厂商反馈。")};
    if (id == QLatin1String("R8")) return {P::tr("去重判定不稳定"), P::tr("30 帧游戏可改用“60→30”；或把采集格式改为 NV12/P010 再试自动去重。")};
    if (id == QLatin1String("R9")) return {P::tr("补帧超出 GPU 预算"), P::tr("降低补帧档位（VFG 高→中）或倍数，或少开一层 NR、把内部处理分辨率降到 1080p 或自动。")};
    if (id == QLatin1String("R10")) return {P::tr("别的程序在大量使用 GPU"), P::tr("关闭或暂停这些程序（游戏、录屏、壁纸等）后再看；同一台电脑上玩游戏时可限制游戏帧率。")};
    if (id == QLatin1String("R11")) return {P::tr("GPU 被限制频率（电源、功耗或温度）"), P::tr("笔记本请接电源；Windows 电源模式选“最佳性能”；检查散热。")};
    if (id == QLatin1String("R12")) return {P::tr("CPU 不足或引擎线程被拖住"), P::tr("关闭后台占用 CPU 的程序；同一台电脑上玩游戏时 CPU 可能被游戏占满。")};
    if (id == QLatin1String("R13")) return {P::tr("记录期间多次改设置或跳转，每次都会重建"), P::tr("这类停顿是正常的重建过程；比较流畅度时请看不改设置的一段。")};
    if (id == QLatin1String("R14")) return {P::tr("画面由系统合成（窗口模式）"), P::tr("全屏播放可减少约一帧的显示延迟，也能避免窗口下补帧被合成器丢帧。")};
    if (id == QLatin1String("R15")) return {P::tr("启动或首次开启效果时编译耗时很长"), P::tr("首次运行或驱动更新后会重新编译着色器，等一次跑完即可；每次都很慢请检查驱动的着色器缓存设置。")};
    if (id == QLatin1String("R16")) return {P::tr("显存紧张"), P::tr("降低处理分辨率或补帧档位，关闭同时运行的游戏或其他占用显存的程序。")};
    if (id == QLatin1String("R17")) return {P::tr("“1% low”读数在台阶式跳动，帧时间本身稳定"), P::tr("界面的 1% low 取最近 10 秒帧间隔的第 99 百分位；长间隔次数刚好跨过门槛时读数会成台阶跳，不代表变卡。")};
    if (id == QLatin1String("R18")) return {P::tr("卡顿主要发生在 GPU 处理"), P::tr("降低增强档位或分辨率；报告的“GPU 阶段”里可以看到哪一步最耗时。")};
    if (id == QLatin1String("G0")) return {P::tr("整体流畅"), P::tr("这段记录里没有发现明显问题。")};
    if (id == QLatin1String("G1")) return {P::tr("记录不完整"), P::tr("数据仍可分析；如果反复出现，请缩短诊断时长。")};
    if (id == QLatin1String("G2")) return {P::tr("卡顿主要发生在：%1").arg(causeText(params.value(QStringLiteral("cause")).toString())), P::tr("请把诊断包发给开发者以便定位。")};
    return {};
}

QString stamp() { return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss")); }

bool writeJson(const QString& path, const QJsonDocument& doc) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    return file.commit();
}

// The tail of the log written since `offset` (handles one rotation to *.old.log),
// plus the start of the current session for context.
void copyLog(const QString& logPath, qint64 offset, const QString& outSlice, const QString& outHead) {
    QFile out(outSlice);
    if (!out.open(QIODevice::WriteOnly)) return;
    QFileInfo info(logPath);
    const QString oldPath = info.dir().filePath(info.completeBaseName() + QStringLiteral(".old.") + info.suffix());
    QFile log(logPath);
    if (!log.open(QIODevice::ReadOnly)) return;
    const auto redact = [](const QByteArray& bytes) { return perfdiag::redactText(QString::fromUtf8(bytes)).toUtf8(); };
    if (log.size() < offset) {
        // Rotated during the recording: the earlier part moved to *.old.log.
        QFile old(oldPath);
        if (old.open(QIODevice::ReadOnly) && old.size() > offset) { old.seek(offset); out.write(redact(old.readAll())); }
        out.write(redact(log.readAll()));
    } else {
        log.seek(offset);
        while (!log.atEnd()) out.write(redact(log.read(8 << 20)));
    }
    // Context: the lines of this run before the recording started, from its
    // "[app] Veyra QML session started" marker (at most 4 MB).
    QFile head(outHead);
    if (!head.open(QIODevice::WriteOnly)) return;
    const qint64 window = std::min<qint64>(offset, 4 << 20);
    if (window <= 0 || log.size() < offset) return;
    log.seek(offset - window);
    const QByteArray before = log.read(window);
    qsizetype start = before.lastIndexOf("[app] Veyra QML session started");
    if (start >= 0) {
        // Include the [app] lines that precede the marker in the same run.
        qsizetype back = start;
        for (int lines = 0; lines < 12 && back > 0; ++lines) {
            const auto previous = before.lastIndexOf('\n', back - 1);
            if (previous < 0) { back = 0; break; }
            if (before.indexOf("[app]", previous) > before.indexOf('\n', previous + 1)) break;
            back = previous;
        }
        start = back > 0 ? back + 1 : 0;
    } else {
        start = std::max<qsizetype>(0, before.indexOf('\n') + 1);
    }
    head.write(redact(before.mid(start)));
}
} // namespace

PerfDiagnostics::PerfDiagnostics(Hooks hooks, QObject* parent) : QObject(parent), hooks_(std::move(hooks)) {
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, [this] {
        emit tick();
        if (elapsedSeconds() >= durationSeconds_) finish(false);
        else if (elapsedSeconds() % 2 == 0) bridgeChanged();
    });
    sampleTimer_.setInterval(250);
    connect(&sampleTimer_, &QTimer::timeout, this, &PerfDiagnostics::sampleEngine);
}

PerfDiagnostics::~PerfDiagnostics() { shutdown(); }

int PerfDiagnostics::elapsedSeconds() const {
    if (state_ != QLatin1String("recording") || !startedHost_) return 0;
    return int((diagnostics::traceHostNow() - startedHost_) / 10000000);
}

QString PerfDiagnostics::outputRoot() const {
    const QString root = hooks_.outputRoot ? hooks_.outputRoot() : QString();
    return QDir(root.isEmpty() ? QDir::homePath() : root).filePath(QStringLiteral("诊断"));
}

bool PerfDiagnostics::analyzerAvailable() const { return QFileInfo::exists(analyzerExe()); }

bool PerfDiagnostics::start(int minutes) { return begin(outputRoot(), std::clamp(minutes, 1, 30) * 60); }

bool PerfDiagnostics::startIn(const QString& root, int seconds) { return begin(root, std::clamp(seconds, 30, 1800)); }

void PerfDiagnostics::setState(const QString& state, const QString& message) {
    state_ = state;
    message_ = message;
    emit changed();
}

bool PerfDiagnostics::begin(const QString& root, int seconds) {
    if (state_ == QLatin1String("recording") || state_ == QLatin1String("analyzing")) return false;
    if (worker_.joinable()) worker_.join();
    findings_.clear(); headline_.clear(); zip_.clear(); events_ = {}; marks_ = 0; changeCounts_.clear();
    QDir rootDir(root);
    QString name = QStringLiteral("Veyra诊断_%1").arg(stamp());
    for (int n = 2; rootDir.exists(name) || QFileInfo::exists(rootDir.filePath(name + QStringLiteral(".zip"))); ++n)
        name = QStringLiteral("Veyra诊断_%1-%2").arg(stamp()).arg(n);
    if (!rootDir.mkpath(QStringLiteral("."))) {
        setState(QStringLiteral("failed"), tr("无法创建诊断文件夹：%1").arg(QDir::toNativeSeparators(rootDir.filePath(name))));
        return false;
    }
    rootDir.mkpath(name + QStringLiteral("/raw"));
    folder_ = rootDir.filePath(name);
    const QString raw = QDir(folder_).filePath(QStringLiteral("raw"));

    // Environment and settings first: they describe the state the recording starts in.
    QJsonObject snapshot;
    snapshot["schema"] = QStringLiteral("veyra-diagnostic-snapshot/1");
    snapshot["requestedSeconds"] = seconds;
    snapshot["system"] = perfdiag::systemSnapshot();
    if (hooks_.context) snapshot["veyra"] = hooks_.context();
    lastProperties_ = bridgeProperties();
    snapshot["interface"] = lastProperties_;
    writeJson(QDir(raw).filePath(QStringLiteral("snapshot.json")), QJsonDocument(snapshot));

    logPath_ = hooks_.logFile ? hooks_.logFile() : QString();
    if (hooks_.flushLog) hooks_.flushLog();
    logOffset_ = QFileInfo(logPath_).size();
    veyra::log::info("perf-diagnostics", std::format("recording started seconds={} folder={}", seconds,
        perfdiag::redactText(QDir::toNativeSeparators(folder_)).toStdString()));

    const auto trace = std::filesystem::path(QDir::toNativeSeparators(QDir(raw).filePath(QStringLiteral("trace.csv"))).toStdWString());
    // The trace's own limit is a safety net past the interface timer.
    if (!diagnostics::startDiagnosticTrace(trace, std::chrono::seconds(seconds + 60))) {
        setState(QStringLiteral("failed"), tr("诊断记录无法开始（可能已有一次记录在进行）"));
        return false;
    }
    startedHost_ = diagnostics::diagnosticTraceStatus().startedHost;
    sampler_ = std::make_unique<perfdiag::TelemetrySampler>();
    const auto telemetry = std::filesystem::path(QDir::toNativeSeparators(QDir(raw).filePath(QStringLiteral("telemetry.csv"))).toStdWString());
    if (!sampler_->start(telemetry)) veyra::log::warn("perf-diagnostics", "telemetry file could not be created");
    durationSeconds_ = seconds;
    sampleEngine();
    timer_.start();
    sampleTimer_.start();
    setState(QStringLiteral("recording"));
    emit tick();
    return true;
}

void PerfDiagnostics::sampleEngine() {
    if (!sampler_ || !hooks_.sample) return;
    sampler_->push(hooks_.sample());
}

void PerfDiagnostics::mark() {
    if (state_ != QLatin1String("recording")) return;
    ++marks_;
    diagnostics::recordDiagnosticMark(uint32_t(marks_));
    QJsonObject event{{"type", "mark"}, {"number", marks_}, {"host", double(diagnostics::traceHostNow())},
                      {"utcMs", double(QDateTime::currentMSecsSinceEpoch())}};
    events_.append(event);
    veyra::log::info("perf-diagnostics", std::format("mark {}", marks_));
    emit changed();
    emit marked(marks_);
}

void PerfDiagnostics::stop() {
    if (state_ == QLatin1String("recording")) finish(true);
}

QJsonObject PerfDiagnostics::bridgeProperties() const {
    QJsonObject out;
    const QObject* bridge = parent();
    if (!bridge) return out;
    const QMetaObject* meta = bridge->metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        const QString name = QString::fromLatin1(property.name());
        if (!property.isReadable() || sensitiveName(name)) continue;
        const int type = property.metaType().id();
        if (type != QMetaType::Bool && type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::Double &&
            type != QMetaType::Float && type != QMetaType::QString && type != QMetaType::LongLong && type != QMetaType::ULongLong)
            continue;
        const QJsonValue value = simpleValue(property.read(bridge));
        if (!value.isUndefined()) out.insert(name, value);
    }
    return out;
}

// Every 2 s while recording: which interface settings changed, with old and new
// value and the trace time. Values that change all the time (statistics) are
// dropped after a few changes so only settings remain.
void PerfDiagnostics::bridgeChanged() {
    if (state_ != QLatin1String("recording")) return;
    const QJsonObject now = bridgeProperties();
    const double host = double(diagnostics::traceHostNow());
    for (auto it = now.begin(); it != now.end(); ++it) {
        const QJsonValue before = lastProperties_.value(it.key());
        if (before == it.value() || it.value().isDouble() && !before.isUndefined() && std::abs(before.toDouble() - it.value().toDouble()) < 1e-9)
            continue;
        int& count = changeCounts_[it.key()];
        if (++count > 6) continue;   // a statistic, not a setting
        events_.append(QJsonObject{{"type", "setting"}, {"name", it.key()}, {"previous", before}, {"value", it.value()},
                                   {"host", host}, {"utcMs", double(QDateTime::currentMSecsSinceEpoch())},
                                   {"volatile", count == 6}});
    }
    lastProperties_ = now;
}

void PerfDiagnostics::finish(bool early) {
    if (state_ != QLatin1String("recording")) return;
    timer_.stop();
    sampleTimer_.stop();
    bridgeChanged();
    events_.append(QJsonObject{{"type", early ? "stopped-early" : "completed"}, {"host", double(diagnostics::traceHostNow())},
                               {"utcMs", double(QDateTime::currentMSecsSinceEpoch())}, {"elapsedSeconds", elapsedSeconds()}});
    QJsonObject events{{"events", events_}, {"interfaceAtEnd", bridgeProperties()}};
    veyra::log::info("perf-diagnostics", std::format("recording ended early={} marks={}", early, marks_));
    if (hooks_.flushLog) hooks_.flushLog();
    setState(QStringLiteral("analyzing"), tr("正在分析…"));
    if (worker_.joinable()) worker_.join();
    g_cancelWorker = false;
    worker_ = std::thread([this, events, log = logPath_, early] { finishWorker(events, log, early); });
}

void PerfDiagnostics::finishWorker(QJsonObject events, QString logFile, bool early) {
    const auto status = diagnostics::stopDiagnosticTrace();
    if (sampler_) sampler_->stop();
    const QDir folder(folder_);
    const QString raw = folder.filePath(QStringLiteral("raw"));
    events["trace"] = QJsonObject{{"published", status.published}, {"recorded", double(status.recorded)}, {"dropped", double(status.dropped)},
                                  {"startedHost", double(status.startedHost)}, {"endedHost", double(status.endedHost)},
                                  {"startedUtcMs", double(status.startedUtcMs)}, {"durationReached", status.durationReached},
                                  {"telemetryRows", double(sampler_ ? sampler_->rows() : 0)},
                                  {"telemetrySources", QString::fromStdString(sampler_ ? sampler_->sources() : std::string())},
                                  {"early", early}};
    writeJson(QDir(raw).filePath(QStringLiteral("events.json")), QJsonDocument(events));
    if (!logFile.isEmpty())
        copyLog(logFile, logOffset_, QDir(raw).filePath(QStringLiteral("veyra-log.txt")), QDir(raw).filePath(QStringLiteral("veyra-log-head.txt")));

    QString message;
    bool analysed = false;
    if (g_cancelWorker) {
        message = tr("程序退出，未分析；原始数据已保存");
    } else if (!QFileInfo::exists(analyzerExe())) {
        message = tr("未找到分析器 veyra_frame_analyzer.exe，原始数据已保存");
    } else {
        const int code = runChild(analyzerExe(), L"analyze " + quoted(folder_) + L" --quiet", folder_,
                                  QDir(raw).filePath(QStringLiteral("analyzer.log")), 180000, BELOW_NORMAL_PRIORITY_CLASS);
        analysed = code == 0 && QFileInfo::exists(folder.filePath(QStringLiteral("report.json")));
        if (!analysed) message = tr("分析失败（代码 %1），原始数据已保存").arg(code);
    }
    // Pack with Windows' own tar.exe. Names inside are ASCII and relative so the
    // archive does not depend on the code page; the result is renamed after.
    QString zip;
    if (!g_cancelWorker) {
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        const QString tar = QString::fromWCharArray(system) + QStringLiteral("\\tar.exe");
        const QString temporary = QStringLiteral("veyra-diagnostic-%1-partial.zip").arg(GetCurrentProcessId());
        QStringList entries;
        for (const QString& entry : {QStringLiteral("report.html"), QStringLiteral("report.json"), QStringLiteral("SUMMARY.md"),
                                     QStringLiteral("frames.csv"), QStringLiteral("raw")})
            if (QFileInfo::exists(folder.filePath(entry))) entries << entry;
        std::wstring arguments = L"-a -c -f ..\\" + temporary.toStdWString();
        for (const auto& entry : entries) arguments += L" " + entry.toStdWString();
        QFile::remove(folder.filePath(QStringLiteral("../") + temporary));
        const int code = runChild(tar, arguments, folder_, QDir(raw).filePath(QStringLiteral("zip.log")), 180000, BELOW_NORMAL_PRIORITY_CLASS);
        const QString target = folder_ + QStringLiteral(".zip");
        const QString built = QDir(folder.filePath(QStringLiteral(".."))).absoluteFilePath(temporary);
        if (code == 0 && QFileInfo(built).size() > 0 && QFile::rename(built, target)) {
            zip = target;
        } else {
            QFile::remove(built);
            if (message.isEmpty()) message = tr("打包失败，文件夹里的数据仍可使用");
        }
    }
    QMetaObject::invokeMethod(this, [this, analysed, message, zip] { finished_(analysed, message, zip); }, Qt::QueuedConnection);
}

void PerfDiagnostics::finished_(bool ok, QString message, QString zip) {
    if (worker_.joinable()) worker_.join();
    sampler_.reset();
    zip_ = zip;
    findings_.clear();
    headline_.clear();
    QFile file(QDir(folder_).filePath(QStringLiteral("report.json")));
    if (ok && file.open(QIODevice::ReadOnly)) {
        const QJsonObject report = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject summary = report.value(QStringLiteral("summary")).toObject();
        const bool chinese = i18n::current().startsWith(QLatin1String("zh"));
        headline_ = tr("平均 %1 fps · 最慢 1% 平均 %2 fps · 卡顿 %3 次")
                        .arg(summary.value(QStringLiteral("averageFps")).toDouble(), 0, 'f', 1)
                        .arg(summary.value(QStringLiteral("low1FpsSlowestAverage")).toDouble(), 0, 'f', 1)
                        .arg(summary.value(QStringLiteral("hitches")).toInt());
        for (const auto& value : report.value(QStringLiteral("findings")).toArray()) {
            const QJsonObject finding = value.toObject();
            if (finding.value(QStringLiteral("severity")).toString() == QLatin1String("pass")) continue;
            QVariantMap item;
            const QString id = finding.value(QStringLiteral("id")).toString();
            const auto [title, advice] = findingText(id, finding.value(QStringLiteral("params")).toObject());
            item["id"] = id;
            item["severity"] = finding.value(QStringLiteral("severity")).toString();
            item["title"] = title.isEmpty() ? finding.value(QStringLiteral("title")).toString() : title;
            item["advice"] = advice.isEmpty() ? finding.value(QStringLiteral("advice")).toString() : advice;
            // The evidence is written by the analyzer in Chinese; other languages
            // see it in the report instead.
            item["evidence"] = chinese ? finding.value(QStringLiteral("evidence")).toString() : QString();
            findings_ << item;
            if (findings_.size() == 3) break;
        }
    }
    veyra::log::info("perf-diagnostics", std::format("finished analysed={} findings={} zip={}", ok, findings_.size(), !zip.isEmpty()));
    setState(ok ? QStringLiteral("done") : QStringLiteral("failed"), message);
    emit finished(ok);
}

void PerfDiagnostics::openReport() const {
    const QString report = QDir(folder_).filePath(QStringLiteral("report.html"));
    if (QFileInfo::exists(report)) QDesktopServices::openUrl(QUrl::fromLocalFile(report));
    else openFolder();
}

void PerfDiagnostics::openFolder() const {
    if (!zip_.isEmpty() && QFileInfo::exists(zip_)) {
        // Select the package in Explorer.
        const std::wstring args = L"/select,\"" + QDir::toNativeSeparators(zip_).toStdWString() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    const QString target = folder_.isEmpty() ? outputRoot() : folder_;
    QDir().mkpath(target);
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void PerfDiagnostics::copyPath() {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(QDir::toNativeSeparators(zip_.isEmpty() ? folder_ : zip_));
        emit notice(tr("路径已复制"), false);
    }
}

void PerfDiagnostics::dismiss() {
    if (state_ == QLatin1String("done") || state_ == QLatin1String("failed")) setState(QStringLiteral("idle"));
}

void PerfDiagnostics::shutdown() {
    if (state_ == QLatin1String("recording")) {
        timer_.stop();
        sampleTimer_.stop();
        g_cancelWorker = true;
        events_.append(QJsonObject{{"type", "application-exit"}, {"host", double(diagnostics::traceHostNow())}});
        QJsonObject events{{"events", events_}};
        if (hooks_.flushLog) hooks_.flushLog();
        state_ = QStringLiteral("analyzing");
        finishWorker(events, logPath_, true);   // raw data only, synchronously
    } else if (worker_.joinable()) {
        g_cancelWorker = true;
        if (HANDLE child = g_workerChild.load()) TerminateProcess(child, 125);
    }
    if (worker_.joinable()) worker_.join();
    if (sampler_) sampler_->stop();
}

}
