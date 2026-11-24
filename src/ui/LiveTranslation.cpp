// SPDX-License-Identifier: GPL-3.0-only
// Based on the Windows Live Captions translation flow from jjjxxxttt, PR #203.
// On-screen OCR and subtitle presentation follow VI Translator (MIT).
#include <winsock2.h>
#include "LiveTranslation.h"
#include "WinCaptionReader.h"
#include "LocalWorkerJob.h"
#include "veyra/Log.h"
#include "veyra/ui/LiveTranslationPrompt.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QOperatingSystemVersion>
#include <QTcpServer>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <utility>
#include <vector>
#include <windows.h>
#include <iphlpapi.h>

namespace veyra::ui {
namespace {
constexpr auto localAlias = "veyra-translation";
constexpr qint64 captionLifetimeMs = 7000;
constexpr qint64 maxQueuedAgeUs = 4000000;
constexpr qint64 maxSpeechAgeUs = 6000000;
constexpr qint64 minSpeechShowMs = 950;
constexpr qint64 minBurstShowMs = 520;
constexpr qsizetype maxSpeechQueue = 24;
constexpr qsizetype maxJoinedSpeech = 72;
constexpr qint64 maxResponseBytes = 128 * 1024;
constexpr int localScreenTimeoutMs = 30000, screenRetryDelayMs = 1500, maxScreenRetries = 2;
constexpr qint64 screenClearMs = 800;

bool languageCode(const QString& value) {
    if (value.isEmpty() || value.size() > 15) return false;
    return std::all_of(value.begin(), value.end(), [](QChar ch) {
        const auto c = ch.unicode();
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
    }) && QLocale(value).language() != QLocale::C;
}

QString languageName(const QString& code) {
    if (code == QLatin1String("zh-CN")) return QStringLiteral("Simplified Chinese");
    if (code == QLatin1String("zh-TW")) return QStringLiteral("Traditional Chinese");
    if (code == QLatin1String("pt-BR")) return QStringLiteral("Brazilian Portuguese");
    return QLocale::languageToString(QLocale(code).language());
}

QUrl completionEndpoint(const QString& value) {
    QUrl url(value.trimmed(), QUrl::StrictMode);
    if (url.scheme() == QLatin1String("http") && url.host() == QLatin1String("localhost"))
        url.setHost(QStringLiteral("127.0.0.1"));
    const bool loopback = QHostAddress(url.host()).isLoopback();
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment() || url.hasQuery()
        || (url.scheme() != QLatin1String("https") && !(loopback && url.scheme() == QLatin1String("http"))))
        return {};
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (!path.endsWith(QLatin1String("/chat/completions"))) {
        if (path.isEmpty()) path = QStringLiteral("/v1");
        path += QStringLiteral("/chat/completions");
    }
    url.setPath(path);
    return url;
}


int translationThreads() { return std::clamp(QThread::idealThreadCount() / 4, 1, 4); }

QString sameLanguageCaption(const QString& text, const QString& source, const QString& target) {
    if (!languageCode(source)) return {};
    const QLocale from(source), to(target);
    if (from.language() != to.language()) return {};
    if (to.language() != QLocale::Chinese) return from.script() == to.script() ? text : QString{};

    // Whisper returns "zh" even when its text uses either Chinese script.
    // Preserve the wording and map the script locally for the target locale.
    const bool traditional = to.script() == QLocale::TraditionalHanScript;
    const DWORD flags = traditional ? LCMAP_TRADITIONAL_CHINESE : LCMAP_SIMPLIFIED_CHINESE;
    const auto* input = reinterpret_cast<const wchar_t*>(text.utf16());
    const int length = int(text.size());
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, input, length, nullptr, 0, nullptr, nullptr, 0);
    if (size <= 0) return text;
    QString converted(size, Qt::Uninitialized);
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, input, length,
        reinterpret_cast<wchar_t*>(converted.data()), size, nullptr, nullptr, 0);
    if (written <= 0) return text;
    converted.resize(written);
    return converted;
}

qint64 displayDuration(const QString& text) {
    return std::clamp<qint64>(text.size() * 1000 / 15, 2000, captionLifetimeMs);
}
qint64 percentile(const std::vector<qint64>& sorted, unsigned percent) {
    return sorted.empty() ? 0 : sorted[(sorted.size() * percent + 99) / 100 - 1];
}

bool loopbackListenerIsOwned(quint16 port, DWORD pid) {
    DWORD bytes = 0;
    if (GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0)
        != ERROR_INSUFFICIENT_BUFFER || bytes > 1024 * 1024) return false;
    std::vector<DWORD> buffer((bytes + sizeof(DWORD) - 1) / sizeof(DWORD));
    if (GetExtendedTcpTable(buffer.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0)
        != NO_ERROR) return false;
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (ntohs(u_short(row.dwLocalPort)) == port && row.dwOwningPid == pid) return true;
    }
    return false;
}

QByteArray chatRequest(const QString& text, const QStringList& context, const QString& model,
                       const QString& source, const QString& target, bool local) {
    QString instruction;
    // The local model follows a Chinese prompt more consistently for Chinese
    // outputs. The UI language never changes the selected translation target.
    const bool simplified = target == QLatin1String("zh-CN");
    const bool traditional = target == QLatin1String("zh-TW");
    if (local && (simplified || traditional)) {
        const QStringView input = QLocale(source).language() == QLocale::English ? translationPrompt::english : u"";
        instruction = QStringView(translationPrompt::chinese).toString().arg(input.toString(),
            QStringView(simplified ? translationPrompt::simplified : translationPrompt::traditional).toString());
        if (!context.isEmpty())
            instruction += QStringView(translationPrompt::previousChinese).toString().arg(input.toString(), context.join(u'\n'));
    } else {
        instruction = QStringLiteral(
            "Translate the user message into %1. Preserve names, places, numbers and the original meaning. "
            "Return only the translation in %1.").arg(languageName(target));
        if (languageCode(source)) instruction += QStringLiteral(" Input language: %1.").arg(languageName(source));
        if (!context.isEmpty())
            instruction += QStringLiteral("\nPrevious subtitles for reference only:\n%1").arg(context.join(u'\n'));
    }
    // The target sentence stays alone in the user message.
    QJsonObject request{
        {"model", model}, {"stream", false},
        {"messages", QJsonArray{
            QJsonObject{{"role", "system"}, {"content", instruction}},
            QJsonObject{{"role", "user"}, {"content", text}}
        }}
    };
    if (local) {
        request.insert(QStringLiteral("temperature"), 0.0);
        request.insert(QStringLiteral("max_tokens"), int(std::min<qsizetype>(160, text.size() * 3 / 2 + 16)));
    }
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}
}

namespace {
// A region inside the picture, large enough to hold a line of text.
QRectF boundedRegion(QRectF r) {
    if (!std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.width()) || !std::isfinite(r.height()))
        return {0.08, 0.70, 0.84, 0.27};
    const double x = std::clamp(r.x(), 0.0, 0.95), y = std::clamp(r.y(), 0.0, 0.95);
    return {x, y, std::clamp(r.width(), 0.05, 1.0 - x), std::clamp(r.height(), 0.05, 1.0 - y)};
}

}

LiveTranslation::LiveTranslation(const QString& directory, QObject* parent)
    : QObject(parent), assets_(directory), asr_(directory),
      settings_(QDir(directory).filePath(QStringLiteral("live-translation.ini")), QSettings::IniFormat) {
    provider_ = std::clamp(settings_.value(QStringLiteral("provider"), int(Local)).toInt(), int(Local), int(MyMemory));
    sourceLanguage_ = settings_.value(QStringLiteral("source")).toString();
    targetLanguage_ = settings_.value(QStringLiteral("target"), QLocale().bcp47Name()).toString();
    if (!sourceLanguage_.isEmpty() && !languageCode(sourceLanguage_)) sourceLanguage_ = QStringLiteral("en");
    if (provider_ == MyMemory && sourceLanguage_.isEmpty()) sourceLanguage_ = QStringLiteral("en");
    if (targetLanguage_ == QLatin1String("zh")) targetLanguage_ = QStringLiteral("zh-CN");
    if (!languageCode(targetLanguage_)) targetLanguage_ = QStringLiteral("en");
    showOriginal_ = settings_.value(QStringLiteral("original"), true).toBool();
    apiBase_ = settings_.value(QStringLiteral("apiBase")).toString();
    apiModel_ = settings_.value(QStringLiteral("apiModel")).toString();

    useGpu_ = settings_.value(QStringLiteral("gpu"), true).toBool();
    const int legacySource = settings_.value(QStringLiteral("captionSource"), 0).toInt() == 1
        ? ScreenText : WindowsCaptions;
    const int defaultSource = settings_.allKeys().isEmpty() ? VeyraAudio : legacySource;
    recognitionSource_ = std::clamp(settings_.value(QStringLiteral("recognitionSource"), defaultSource).toInt(),
                                    int(VeyraAudio), int(ScreenText));
    if (!sourceSupported()) recognitionSource_ = VeyraAudio;
    screenRegion_ = std::clamp(settings_.value(QStringLiteral("screenRegion"), int(BottomSubtitles)).toInt(),
                               int(BottomSubtitles), int(CustomRegion));
    if (settings_.contains(QStringLiteral("screenRect")))
        customRegion_ = boundedRegion(settings_.value(QStringLiteral("screenRect")).toRectF());
    recognitionModel_ = std::clamp(settings_.value(QStringLiteral("recognitionModel"), int(asr::Parakeet)).toInt(),
        0, asr::modelCount - 1);
    recognitionThreads_ = std::clamp(settings_.value(QStringLiteral("recognitionThreads"), 0).toInt(), 0, 8);
    configureAsr();

    localNetwork_.setProxy(QNetworkProxy::NoProxy);
    reader_.setStandardErrorFile(QProcess::nullDevice());
    server_.setStandardOutputFile(QProcess::nullDevice());
    server_.setStandardErrorFile(QProcess::nullDevice());
    auto childFlags = [](QProcess::CreateProcessArguments* args) {
        args->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    };
    reader_.setCreateProcessArgumentsModifier(childFlags);
    server_.setCreateProcessArgumentsModifier(childFlags);
    connect(&assets_.downloads, &ModelAssets::changed, this, [this] {
        if (provider_ == Local && recognitionSource_ == VeyraAudio && !serverPort_) configureAsr();
        emit changed();
    });
    connect(&asr_, &LiveAsr::changed, this, &LiveTranslation::changed);
    connect(&asr_, &LiveAsr::audioDiscontinuity, this, &LiveTranslation::resetCaption);
    connect(&asr_, &LiveAsr::finalText, this, [this](const QString& text, const QString& language, qint64 endUs,
                                                   const std::vector<SpeechMark>& marks, bool early) {
        if (!enabled_ || recognitionSource_ != VeyraAudio || !active_) return;
        auto completed = sentences_.appendFinal(text, language, endUs, clock_.elapsed(), marks);
        if (early) earlySentences_ += int(completed.size());
        addSentences(std::move(completed));
    });
    connect(&asr_, &LiveAsr::draftText, this, [this](const QString& text) {
        if (recognitionSource_ != VeyraAudio) return;
        draftHideAt_ = text.isEmpty() ? 0 : clock_.elapsed() + 2500;
        if (text == speechDraft_) return;
        speechDraft_ = text;
        if (showOriginal_) emit captionChanged();
    });
    connect(&reader_, &QProcess::readyReadStandardOutput, this, &LiveTranslation::readCaptions);
    connect(&reader_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (enabled_ && error == QProcess::FailedToStart) setStatus(tr("无法启动 Windows 字幕读取器"));
    });
    connect(&reader_, &QProcess::finished, this, [this] {
        if (!enabled_ || !readerStarted_) return;
        resetCaption();
        captionsOpen_ = false;
        setStatus(tr("字幕读取已停止，请重新启用"));
    });
    connect(&server_, &QProcess::started, this, [this] {
        if (!enabled_ || provider_ != Local) { server_.kill(); return; }
        serverJob_ = attachLocalWorkerJob(DWORD(server_.processId()));
        if (!serverJob_) {
            serverFailed_ = true;
            server_.kill();
            setStatus(tr("无法启动本地翻译进程"));
        }
    });
    connect(&server_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (enabled_ && provider_ == Local && error == QProcess::FailedToStart) {
            serverFailed_ = true;
            setStatus(tr("无法启动本地翻译进程"));
        }
    });
    connect(&server_, &QProcess::finished, this, [this] {
        if (serverJob_) CloseHandle(std::exchange(serverJob_, nullptr));
        if (enabled_ && provider_ == Local && serverPort_) {
            if (usingGpu()) {
                gpuFailed_ = true;
                stopLocal();
                configureAsr();
                log::warn("live-translate", "Vulkan translation server exited, retrying on CPU");
                return;
            }
            serverFailed_ = true;
            setStatus(tr("本地翻译进程已停止，请重新启用"));
        }
    });
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &LiveTranslation::tick);
    clock_.start();
}

LiveTranslation::~LiveTranslation() {
    reader_.disconnect(this);
    server_.disconnect(this);
    asr_.disconnect(this);
    asr_.setEnabled(false);
    cancelRequest();
    stopLocal();
    for (auto* process : {&reader_, &server_}) {
        if (process->state() == QProcess::NotRunning) continue;
        process->kill();
        process->waitForFinished(1500);
    }
}

bool LiveTranslation::windowsCaptionsSupported() {
    const auto version = QOperatingSystemVersion::current();
    return version >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621) &&
        QFileInfo::exists(windowsSystemExecutable(L"LiveCaptions.exe"));
}

bool LiveTranslation::screenTextSupported() {
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10;
}

bool LiveTranslation::sourceSupported() const {
    if (recognitionSource_ == ScreenText) return screenTextSupported();
    return recognitionSource_ != WindowsCaptions || windowsCaptionsSupported();
}

QRectF LiveTranslation::screenTextRect() const {
    switch (screenRegion_) {
    case BottomSubtitles: return {0.08, 0.70, 0.84, 0.27};
    case BottomWide: return {0.02, 0.62, 0.96, 0.36};
    case TopSubtitles: return {0.08, 0.03, 0.84, 0.27};
    case WholePicture: return {0.0, 0.0, 1.0, 1.0};
    default: return customRegion_;
    }
}

// Subtitle bands keep only the centred block of words (a HUD panel or a menu
// entry at the side is not part of the line); other regions read everything.
bool LiveTranslation::screenTextCentered() const {
    return screenRegion_ == BottomSubtitles || screenRegion_ == TopSubtitles;
}

// OCR needs a language; "auto" reads English, the usual language of game text.
QString LiveTranslation::screenTextLanguage() const {
    return sourceLanguage_.isEmpty() ? QStringLiteral("en") : sourceLanguage_;
}

int LiveTranslation::screenTextIntervalMs() const {
    return screenRegion_ == WholePicture ? 600 : 250;
}

QString LiveTranslation::sourceLanguage() const {
    if (recognitionSource_ == VeyraAudio && recognitionModel_ == asr::Parakeet) return QStringLiteral("en");
    return sourceLanguage_;
}

QString LiveTranslation::status() const {
    auto& downloads = assets_.downloads;
    if (provider_ == Local && (downloads.downloading() || (!modelInstalled() && !downloads.status().isEmpty())))
        return downloads.status();
    return status_;
}

LiveTranslation::DisplayCaption LiveTranslation::displayCaption() const {
    if (recognitionSource_ == ScreenText)
        return screenPresenter_.shown() ? *screenPresenter_.shown() : DisplayCaption{};
    const auto now = clock_.elapsed();
    if (recognitionSource_ == VeyraAudio) {
        const auto draft = showOriginal_ ? speechDraft_ : QString{};
        if (speechShown_ && now < speechHideAt_)
            return {speechShown_->work.sentence.text, speechShown_->translated, draft};
        return {{}, {}, draft};
    }
    for (auto row = rows_.rbegin(); row != rows_.rend(); ++row) {
        if (!row->translated.isEmpty() && row->until > now) return {row->original, row->translated};
    }
    if (!rows_.empty() && rows_.back().originalUntil > now) return {rows_.back().original, {}};
    return {};
}

void LiveTranslation::setStatus(QString status) {
    if (status_ == status) return;
    status_ = std::move(status);
    emit changed();
}

void LiveTranslation::configureAsr() {
    const int cores = QThread::idealThreadCount();
    const bool cpuTranslation = provider_ == Local && !usingGpu();
    const int available = std::max(1, cores / 2 - (cpuTranslation ? translationThreads() : 0) - 1);
    const int threads = recognitionThreads_ ? recognitionThreads_ : std::clamp(cores / 4, 1, 4);
    asr_.configure(recognitionModel_, sourceLanguage(), std::min(threads, available), recognitionThreads_);
}

void LiveTranslation::setEnabled(bool value) {
    if (enabled_ == value) return;
    if (value && !sourceSupported()) {
        setStatus(recognitionSource_ == ScreenText ? tr("画面文字识别需要 Windows 10 或更新版本")
                                                   : tr("Windows 实时字幕需要 Windows 11 22H2 或更新版本"));
        return;
    }
    if (value && provider_ == OpenAi && (completionEndpoint(apiBase_).isEmpty() || apiModel_.isEmpty())) {
        setStatus(tr("请填写 HTTPS 接口地址和模型名，本机服务可使用 HTTP"));
        return;
    }
    enabled_ = value;
    raw_.clear();
    resetCaption();
    captionBuffer_.clear();
    readerStarted_ = false;
    captionsOpen_ = false;
    serverFailed_ = false;
    gpuFailed_ = false;
    screenError_.clear();
    if (!screenReading_.isEmpty() || screenWords_) {
        screenReading_.clear();
        screenWords_ = 0;
        emit previewChanged();
    }
    if (value) {
        assets_.refreshInstalled();
        configureAsr();
    }
    asr_.setEnabled(value && recognitionSource_ == VeyraAudio);
    if (value) {
        lastSummaryAt_ = clock_.elapsed();
        status_ = recognitionSource_ == WindowsCaptions ? tr("请打开 Windows 实时字幕")
            : recognitionSource_ == ScreenText ? tr("正在启动画面文字识别…") : QString{};
        timer_.start();
    } else {
        timer_.stop();
        if (reader_.state() != QProcess::NotRunning) reader_.kill();
        stopLocal();
        summarize(clock_.elapsed());
        status_.clear();
    }
    emit changed();
}

void LiveTranslation::setProvider(int value) {
    if (value < Local || value > MyMemory || provider_ == value) return;
    setEnabled(false);
    provider_ = value;
    if (provider_ == MyMemory && sourceLanguage_.isEmpty()) sourceLanguage_ = QStringLiteral("en");
    settings_.setValue(QStringLiteral("provider"), value);
    settings_.setValue(QStringLiteral("source"), sourceLanguage_);
    configureAsr();
    emit changed();
}

void LiveTranslation::setRecognitionSource(int value) {
    if (value < VeyraAudio || value > ScreenText || recognitionSource_ == value ||
        (value == WindowsCaptions && !windowsCaptionsSupported()) ||
        (value == ScreenText && !screenTextSupported())) return;
    const bool enabled = enabled_;
    setEnabled(false);
    recognitionSource_ = value;
    settings_.setValue(QStringLiteral("recognitionSource"), value);
    configureAsr();
    setEnabled(enabled);
    emit changed();
}

void LiveTranslation::setScreenRegion(int value) {
    if (value < BottomSubtitles || value > CustomRegion || screenRegion_ == value) return;
    screenRegion_ = value;
    settings_.setValue(QStringLiteral("screenRegion"), value);
    resetCaption();
    emit changed();
}

void LiveTranslation::setCustomRegion(QRectF value) {
    value = boundedRegion(value);
    if (customRegion_ == value) return;
    customRegion_ = value;
    settings_.setValue(QStringLiteral("screenRect"), value);
    resetCaption();
    emit changed();
}

void LiveTranslation::acceptScreenText(const QString& text, bool ready, const QString& error,
                                       const QString& reading, int words) {
    if (!enabled_ || recognitionSource_ != ScreenText) return;
    lastCaptionReadAt_ = clock_.elapsed();
    const auto preview = reading.left(300);
    if (preview != screenReading_ || words != screenWords_) {
        screenReading_ = preview;
        screenWords_ = words;
        emit previewChanged();
    }
    if (screenError_ != error) {
        screenError_ = error;
        if (!error.isEmpty()) {
            resetCaption();
            setStatus(error);
        }
    }
    if (!error.isEmpty()) return;
    if (ready != captionsOpen_) {
        captionsOpen_ = ready;
        if (!ready) resetCaption();
        else if (!screenPresenter_.shown()) setStatus(tr("正在等待画面字幕…"));
    }
    if (active_ && ready) acceptScreenLine(text.left(500));
}

void LiveTranslation::setUseGpu(bool value) {
    if (useGpu_ == value) return;
    useGpu_ = value;
    settings_.setValue(QStringLiteral("gpu"), value);
    // A running model keeps its device until it is started again.
    if (provider_ == Local && enabled_) {
        resetCaption();
        stopLocal();
        serverFailed_ = false;
        gpuFailed_ = false;
    }
    configureAsr();
    emit changed();
}

void LiveTranslation::setRecognitionModel(int value) {
    if (asr::assets(value).empty() || value == recognitionModel_ || asr_.downloading()) return;
    recognitionModel_ = value;
    settings_.setValue(QStringLiteral("recognitionModel"), value);
    resetCaption();
    configureAsr();
    emit changed();
}

void LiveTranslation::setRecognitionThreads(int value) {
    value = std::clamp(value, 0, 8);
    if (recognitionThreads_ == value) return;
    recognitionThreads_ = value;
    settings_.setValue(QStringLiteral("recognitionThreads"), value);
    if (recognitionSource_ == VeyraAudio) resetCaption();
    configureAsr();
    emit changed();
}

void LiveTranslation::setSourceLanguage(QString value) {
    if ((!value.isEmpty() && !languageCode(value)) || sourceLanguage_ == value) return;
    sourceLanguage_ = std::move(value);
    settings_.setValue(QStringLiteral("source"), sourceLanguage_);
    resetCaption();
    configureAsr();
    emit changed();
}

void LiveTranslation::setTargetLanguage(QString value) {
    if (!languageCode(value) || targetLanguage_ == value) return;
    targetLanguage_ = std::move(value);
    settings_.setValue(QStringLiteral("target"), targetLanguage_);
    resetCaption();
    emit changed();
}

void LiveTranslation::setShowOriginal(bool value) {
    if (showOriginal_ == value) return;
    showOriginal_ = value;
    settings_.setValue(QStringLiteral("original"), value);
    emit changed();
    emit captionChanged();
}

void LiveTranslation::setApiBase(QString value) {
    value = value.trimmed().left(1024);
    // Avoid saving credentials or query tokens in the configuration file.
    if (apiBase_ == value) return;
    if (provider_ == OpenAi) setEnabled(false);
    apiBase_ = std::move(value);
    cache_.clear();
    if (apiBase_.isEmpty() || !completionEndpoint(apiBase_).isEmpty())
        settings_.setValue(QStringLiteral("apiBase"), apiBase_);
    emit changed();
}

void LiveTranslation::setApiModel(QString value) {
    value = value.trimmed().left(200);
    if (apiModel_ == value) return;
    if (provider_ == OpenAi) setEnabled(false);
    apiModel_ = std::move(value);
    cache_.clear();
    settings_.setValue(QStringLiteral("apiModel"), apiModel_);
    emit changed();
}

void LiveTranslation::setApiKey(const QString& value) {
    if (value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'))) return;
    const auto key = value.trimmed().left(2048).toUtf8();
    if (apiKey_ == key) return;
    if (provider_ == OpenAi) setEnabled(false);
    apiKey_ = key;
    cache_.clear();
}

void LiveTranslation::openWindowsCaptions() {
    if (recognitionSource_ == WindowsCaptions && windowsCaptionsSupported() &&
        !QProcess::startDetached(windowsSystemExecutable(L"LiveCaptions.exe"), {}))
        setStatus(tr("请按 Win + Ctrl + L 打开系统字幕"));
}

void LiveTranslation::downloadModel() {
    if (provider_ == Local && server_.state() == QProcess::NotRunning) assets_.downloads.download();
}
void LiveTranslation::cancelModelDownload() { assets_.downloads.cancel(); }
void LiveTranslation::downloadRecognitionModel() { asr_.download(); }
void LiveTranslation::deleteRecognitionModel() { asr_.removeModel(); }

void LiveTranslation::setPlayback(quint64 session, bool active, bool discontinuity) {
    const bool sourceChanged = session_ != session;
    if (!sourceChanged && active_ == active && !discontinuity) return;
    session_ = session;
    active_ = active;
    asr_.setPlayback(active, sourceChanged || discontinuity);
    if (enabled_) resetCaption();
}

void LiveTranslation::readCaptions() {
    captionBuffer_ += reader_.readAllStandardOutput();
    if (captionBuffer_.size() > 128 * 1024) captionBuffer_.clear();
    for (qsizetype end; (end = captionBuffer_.indexOf('\n')) >= 0;) {
        const auto message = QJsonDocument::fromJson(captionBuffer_.first(end)).object();
        captionBuffer_.remove(0, end + 1);
        if (!enabled_ || recognitionSource_ != WindowsCaptions || !message.contains(QStringLiteral("ready"))) continue;
        lastCaptionReadAt_ = clock_.elapsed();
        const bool ready = message.value(QStringLiteral("ready")).toBool();
        if (ready != captionsOpen_) {
            captionsOpen_ = ready;
            if (!ready) resetCaption();
            setStatus(ready ? tr("正在等待语音…") : tr("请打开 Windows 实时字幕"));
        }
        acceptCaption(message.value(QStringLiteral("text")).toString());
    }
}

void LiveTranslation::acceptCaption(const QString& text) {
    if (raw_ == text) return;
    raw_ = text;
    if (!active_ || !captionsOpen_) {
        sentences_.reset(raw_);
        return;
    }
    addSentences(sentences_.updateSnapshot(text, sourceLanguage(), sink::recordingClockUs(), clock_.elapsed()));
}

QString LiveTranslation::cacheKey(const QString& language, const QString& text) const {
    return QString::number(provider_) + u'\n' + language + u'\n' + targetLanguage_ + u'\n' + text;
}

void LiveTranslation::acceptScreenLine(const QString& text) {
    if (text == screenCurrent_) return;
    const qint64 now = clock_.elapsed();
    if (!screenCurrent_.isEmpty())
        screenPresenter_.ended(screenSequence_, text.isEmpty() ? std::max(screenStart_, now - screenClearMs) : now);
    screenCurrent_ = text;
    screenRetries_ = 0;
    screenRetryAt_ = 0;
    if (text.isEmpty()) {
        screenSequence_ = 0;
        return;
    }
    screenSequence_ = ++screenLine_;
    screenStart_ = now;
    const QString language = screenTextLanguage();
    const auto key = cacheKey(language, text);
    const auto direct = sameLanguageCaption(text, language, targetLanguage_);
    const auto* cached = cache_.object(key);
    if (direct.isEmpty() && !cached) return;
    screenRequested_ = screenSequence_;
    displayTranslation(Work{screenSequence_, {text, language, sink::recordingClockUs()}, {}, key, now},
                       direct.isEmpty() ? *cached : direct);
}

void LiveTranslation::stepScreenPresenter() {
    using Act = CaptionPresenter<DisplayCaption>::Act;
    switch (screenPresenter_.tick(clock_.elapsed())) {
    case Act::Show: setStatus(tr("实时翻译运行中")); emit captionChanged(); break;
    case Act::Hide: emit captionChanged(); break;
    case Act::None: break;
    }
}

void LiveTranslation::addSentences(std::vector<LiveSentence> sentences) {
    if (sentences.empty()) return;
    if (recognitionSource_ == VeyraAudio) {
        addSpeech(std::move(sentences));
        return;
    }
    const auto now = clock_.elapsed();
    for (auto& sentence : sentences) {
        if (!languageCode(sentence.language)) sentence.language = sourceLanguage();
        const auto id = ++nextSentence_;
        rows_.push_back({id, now + captionLifetimeMs, now + displayDuration(sentence.text), sentence.text, {}});
        if (rows_.size() > 2) {
            rows_.pop_front();
            discardHiddenWork();
        }

        const QString key = cacheKey(sentence.language, sentence.text);
        Work work{id, std::move(sentence), context_, key};
        context_ << work.sentence.text;
        if (context_.size() > 2) context_.removeFirst();
        ++sentenceCount_;

        const auto direct = sameLanguageCaption(work.sentence.text, work.sentence.language, targetLanguage_);
        if (!direct.isEmpty()) {
            displayTranslation(work, direct);
        } else if (const auto* cached = cache_.object(key)) {
            ++cacheHits_;
            displayTranslation(work, *cached);
        } else {
            queue_.enqueue(std::move(work));
        }
    }
    emit captionChanged();
}

void LiveTranslation::addSpeech(std::vector<LiveSentence> sentences) {
    for (auto& sentence : sentences)
        if (!languageCode(sentence.language)) sentence.language = sourceLanguage();
    for (size_t i = 0; i < sentences.size();) {
        Work work;
        work.id = ++nextSentence_;
        // The small local model can translate the earlier dialogue instead
        // of the current caption when both are in the prompt.
        if (provider_ != Local) work.context = context_;
        work.sentence = std::move(sentences[i++]);
        if (work.sentence.wordEndUs) work.wordEnds.push_back(work.sentence.wordEndUs);
        // Neighbouring short phrases from one recognition result fit in the
        // same subtitle and can share a translation request.
        for (; i < sentences.size(); ++i) {
            const auto& next = sentences[i];
            if (next.language != work.sentence.language ||
                work.sentence.text.size() + 1 + next.text.size() > maxJoinedSpeech) break;
            work.sentence.text += u' ' + next.text;
            work.sentence.endUs = next.endUs;
            if (next.wordEndUs) work.wordEnds.push_back(next.wordEndUs);
            ++work.phrases;
        }
        sentenceCount_ += work.phrases;
        work.cacheKey = cacheKey(work.sentence.language, work.sentence.text);
        if (provider_ != Local) {
            context_ << work.sentence.text;
            if (context_.size() > 2) context_.removeFirst();
        }
        if (queue_.size() >= maxSpeechQueue) skipSpeech(queue_.dequeue(), true);
        queue_.enqueue(std::move(work));
    }
}

bool LiveTranslation::resolveSpeech(const Work& work) {
    auto translation = sameLanguageCaption(work.sentence.text, work.sentence.language, targetLanguage_);
    if (translation.isEmpty()) {
        const auto* cached = cache_.object(work.cacheKey);
        if (!cached) return false;
        ++cacheHits_;
        translation = *cached;
    }
    queueSpeechResult(work, std::move(translation));
    return true;
}

void LiveTranslation::queueSpeechResult(const Work& work, QString translation) {
    const auto now = clock_.elapsed();
    if (speechReady_.size() >= size_t(maxSpeechQueue)) {
        const auto& evicted = speechReady_.front();
        if (!evicted.translated.isEmpty()) {
            stale_ += evicted.work.phrases;
            skippedSentences_ += evicted.work.phrases;
        }
        speechReady_.pop_front();
    }
    const auto after = std::ranges::upper_bound(speechReady_, work.id, {},
        [](const SpeechRow& row) { return row.work.id; });
    speechReady_.insert(after, SpeechRow{work, std::move(translation), now});
    advanceSpeech(now);
}

void LiveTranslation::skipSpeech(const Work& work, bool stale) {
    if (stale) stale_ += work.phrases;
    skippedSentences_ += work.phrases;
    if (showOriginal_) queueSpeechResult(work, {});
}

void LiveTranslation::pruneSpeech(qint64 now) {
    if (draftHideAt_ && now >= draftHideAt_) {
        draftHideAt_ = 0;
        speechDraft_.clear();
        if (showOriginal_) emit captionChanged();
    }
    const auto expired = sink::recordingClockUs() - maxSpeechAgeUs;
    if (speechRequest_ && speechRequest_->sentence.endUs < expired) {
        auto work = std::move(*speechRequest_);
        cancelRequest();
        skipSpeech(work, true);
    }
    while (!queue_.isEmpty() && queue_.head().sentence.endUs < expired) skipSpeech(queue_.dequeue(), true);
    advanceSpeech(now);
}

void LiveTranslation::advanceSpeech(qint64 now) {
    const auto nextSwapAt = speechReady_.size() >= 2 ? speechBurstSwapAt_ : speechSwapAt_;
    if (speechShown_ && now < nextSwapAt) return;
    while (!speechReady_.empty()) {
        // A fallback created under backlog pressure must not jump over an
        // older in-flight translation.
        const auto id = speechReady_.front().work.id;
        if ((speechRequest_ && speechRequest_->id < id) ||
            (!queue_.isEmpty() && queue_.head().id < id)) break;
        auto row = std::move(speechReady_.front());
        speechReady_.pop_front();
        if (!row.translated.isEmpty() && sink::recordingClockUs() - row.work.sentence.endUs > maxSpeechAgeUs) {
            stale_ += row.work.phrases;
            skippedSentences_ += row.work.phrases;
            row.translated.clear();
        }
        const bool fallback = row.translated.isEmpty();
        if (fallback && (!showOriginal_ || now - row.readyAt > minSpeechShowMs)) continue;
        const auto& line = fallback ? row.work.sentence.text : row.translated;
        speechSwapAt_ = now + (fallback ? minSpeechShowMs
            : std::clamp<qint64>(line.size() * 1000 / 32, minSpeechShowMs, 2200));
        // Catch up on dense dialogue without shortening a lone caption's
        // lifetime or skipping a translated phrase already ready to display.
        speechBurstSwapAt_ = now + std::clamp<qint64>(line.size() * 1000 / 52, minBurstShowMs, 1200);
        speechHideAt_ = now + (fallback ? minSpeechShowMs : displayDuration(line));
        if (!fallback) {
            const auto clockUs = sink::recordingClockUs();
            const auto delayMs = std::max<qint64>(0, (clockUs - row.work.sentence.endUs) / 1000);
            if (displayDelays_.size() < 256) displayDelays_.push_back(delayMs);
            for (const auto end : row.work.wordEnds)
                if (spokenDelays_.size() < 256) spokenDelays_.push_back(std::max<qint64>(0, (clockUs - end) / 1000));
            displayedSentences_ += row.work.phrases;
            if (qEnvironmentVariableIntValue("VEYRA_TEST_LIVE_TEXT_LOG") == 1)
                log::info("live-translate", std::format("test-id={} delayMs={} text={}", row.work.id, delayMs,
                    row.translated.toStdString()));
            setStatus(tr("实时翻译运行中"));
        }
        speechShown_ = std::move(row);
        emit captionChanged();
        return;
    }
    if (speechShown_ && now >= speechHideAt_) {
        speechShown_.reset();
        emit captionChanged();
    }
}

void LiveTranslation::displayTranslation(const Work& work, QString translation) {
    const auto now = clock_.elapsed();
    if (work.screenStart >= 0) {
        const auto readMs = std::clamp<qint64>(1000 + 40 * std::max(work.sentence.text.size(), translation.size()), 1200, 3500);
        screenPresenter_.ready(work.id, work.screenStart, {work.sentence.text, std::move(translation)}, now, readMs);
        stepScreenPresenter();
        return;
    }
    if (recognitionSource_ == VeyraAudio) {
        queueSpeechResult(work, std::move(translation));
        return;
    }
    const auto row = std::ranges::find(rows_, work.id, &Row::id);
    if (row == rows_.end() || row->until <= now) return;
    const bool first = row->translated.isEmpty();
    row->translated = std::move(translation);
    row->until = now + displayDuration(row->translated);
    row->originalUntil = row->until;
    const auto delayMs = std::max<qint64>(0, (sink::recordingClockUs() - work.sentence.endUs) / 1000);
    if (first && displayDelays_.size() < 256) displayDelays_.push_back(delayMs);
    if (qEnvironmentVariableIntValue("VEYRA_TEST_LIVE_TEXT_LOG") == 1)
        log::info("live-translate", std::format("test-id={} delayMs={} text={}", work.id, delayMs, row->translated.toStdString()));
    setStatus(tr("实时翻译运行中"));
    emit captionChanged();
}

void LiveTranslation::prune(qint64 now) {
    const auto old = rows_.size();
    std::erase_if(rows_, [now](const Row& row) { return row.until <= now; });
    if (rows_.size() != old) {
        discardHiddenWork();
        emit captionChanged();
    }
    const auto minimumUs = sink::recordingClockUs() - maxQueuedAgeUs;
    while (!queue_.isEmpty() && queue_.head().sentence.endUs < minimumUs) {
        queue_.dequeue();
        ++stale_;
        ++skippedSentences_;
    }
}

void LiveTranslation::discardHiddenWork() {
    const auto hidden = [this](quint64 id) { return std::ranges::find(rows_, id, &Row::id) == rows_.end(); };
    int discarded = int(queue_.removeIf([&](const Work& work) { return hidden(work.id); }));
    if (requestSentence_ && hidden(requestSentence_)) {
        cancelRequest();
        ++discarded;
    }
    hidden_ += discarded;
    skippedSentences_ += discarded;
}

void LiveTranslation::resetCaption() {
    cancelRequest();
    sentences_.reset(raw_);
    queue_.clear();
    context_.clear();
    screenCurrent_.clear();
    screenSequence_ = screenRequested_ = 0;
    screenRetries_ = 0;
    screenRetryAt_ = 0;
    const bool screenWasShown = screenPresenter_.clear(screenLine_);
    const bool wasShown = !rows_.empty() || screenWasShown || speechShown_ || !speechDraft_.isEmpty();
    rows_.clear();
    speechReady_.clear();
    speechShown_.reset();
    speechSwapAt_ = speechBurstSwapAt_ = speechHideAt_ = draftHideAt_ = 0;
    speechDraft_.clear();
    if (wasShown) emit captionChanged();
}

void LiveTranslation::cancelRequest() {
    requestSentence_ = 0;
    speechRequest_.reset();
    if (auto* reply = std::exchange(request_, nullptr)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void LiveTranslation::tick() {
    const auto now = clock_.elapsed();
    if (now - lastSummaryAt_ >= 10000) summarize(now);
    if (recognitionSource_ == WindowsCaptions && !readerStarted_ && reader_.state() == QProcess::NotRunning) {
        readerStarted_ = true;
        reader_.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--read-live-captions")});
    }
    if (!active_) {
        if (serverPort_) stopLocal();
        setStatus(tr("播放或连接片源后开始翻译"));
        return;
    }
    if (recognitionSource_ == WindowsCaptions) {
        if (!captionsOpen_) {
            if (reader_.state() != QProcess::NotRunning) setStatus(tr("请打开 Windows 实时字幕"));
            return;
        }
        if (now - lastCaptionReadAt_ > 4000) {
            resetCaption();
            setStatus(tr("正在等待 Windows 字幕响应…"));
            return;
        }
    }
    if (recognitionSource_ == ScreenText) {
        if (!screenError_.isEmpty()) { setStatus(screenError_); return; }
        if (!captionsOpen_ || now - lastCaptionReadAt_ > 4000) {
            resetCaption();
            setStatus(tr("正在启动画面文字识别…"));
            return;
        }
        stepScreenPresenter();
    } else {
        if (recognitionSource_ == VeyraAudio) pruneSpeech(now);
        else prune(now);
        addSentences(sentences_.flush(now));
        if (recognitionSource_ == VeyraAudio && !request_)
            while (!queue_.isEmpty() && resolveSpeech(queue_.head())) queue_.dequeue();
    }
    if (provider_ == Local) {
        if (downloading()) return;
        if (!modelInstalled()) { setStatus(tr("请先下载本地翻译模型")); return; }
        if (serverFailed_) return;
        if (recognitionSource_ == VeyraAudio && !asr_.installed()) return;
        if (!serverPort_ && server_.state() == QProcess::NotRunning) startLocal();
        if (!serverJob_ || server_.state() != QProcess::Running) return;
    }
    if (recognitionSource_ == ScreenText) {
        tickScreen(now);
        return;
    }
    if (queue_.isEmpty() || request_) return;
    if (provider_ == Local && !loopbackListenerIsOwned(serverPort_, DWORD(server_.processId()))) {
        serverNotListening(now);
        return;
    }
    requestTranslation(queue_.dequeue());
}

void LiveTranslation::tickScreen(qint64 now) {
    if (screenCurrent_.isEmpty()) {
        if (provider_ == Local && status_ == tr("正在加载本地翻译模型…") && serverPort_ &&
            loopbackListenerIsOwned(serverPort_, DWORD(server_.processId())))
            setStatus(tr("正在等待画面字幕…"));
        return;
    }
    if (request_ || screenSequence_ == screenRequested_ || now < screenRetryAt_) return;
    if (provider_ == Local && !loopbackListenerIsOwned(serverPort_, DWORD(server_.processId()))) {
        serverNotListening(now);
        return;
    }
    screenRequested_ = screenSequence_;
    const auto language = screenTextLanguage();
    requestTranslation(Work{screenSequence_, {screenCurrent_, language, sink::recordingClockUs()},
                            {}, cacheKey(language, screenCurrent_), screenStart_});
}

void LiveTranslation::serverNotListening(qint64 now) {
    const bool gpu = usingGpu();
    if (now - serverStartedAt_ <= (gpu ? 45000 : 20000)) return;
    stopLocal();
    if (gpu) {
        gpuFailed_ = true;
        configureAsr();
        log::warn("live-translate", "Vulkan translation server startup timed out, retrying on CPU");
        return;
    }
    serverFailed_ = true;
    setStatus(tr("本地翻译服务启动超时，请重新启用"));
}

void LiveTranslation::startLocal() {
    if (!assets_.verifyRuntime()) {
        serverFailed_ = true;
        setStatus(tr("本地运行库校验失败，请重新下载"));
        return;
    }
    QTcpServer endpoint;
    if (!endpoint.listen(QHostAddress::LocalHost)) {
        serverFailed_ = true;
        setStatus(tr("无法启动本地翻译进程"));
        return;
    }
    serverPort_ = endpoint.serverPort();
    endpoint.close();
    serverKey_ = QUuid::createUuid().toRfc4122().toHex();
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto& key : env.keys())
        if (key.startsWith(QLatin1String("LLAMA_"), Qt::CaseInsensitive) ||
            key.startsWith(QLatin1String("GGML_"), Qt::CaseInsensitive)) env.remove(key);
    env.insert(QStringLiteral("LLAMA_API_KEY"), QString::fromLatin1(serverKey_));
    const int threads = translationThreads();
    const auto n = QString::number(threads);
    server_.setProcessEnvironment(env);
    server_.setWorkingDirectory(assets_.runtimePath());
    serverStartedAt_ = clock_.elapsed();
    setStatus(tr("正在加载本地翻译模型…"));
    server_.start(QDir(assets_.runtimePath()).filePath(QStringLiteral("llama-server.exe")),
        {QStringLiteral("--model"), assets_.modelPath(), QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
         QStringLiteral("--port"), QString::number(serverPort_), QStringLiteral("--threads"), n,
         QStringLiteral("--threads-batch"), n, QStringLiteral("--parallel"), QStringLiteral("1"),
         QStringLiteral("--alias"), QString::fromLatin1(localAlias), QStringLiteral("--ctx-size"), QStringLiteral("1024"),
         QStringLiteral("--n-gpu-layers"), usingGpu() ? QStringLiteral("99") : QStringLiteral("0"), QStringLiteral("--offline"),
         QStringLiteral("--no-webui"), QStringLiteral("--log-disable")});
}

void LiveTranslation::stopLocal() {
    serverPort_ = 0;
    serverKey_.clear();
    if (serverJob_) CloseHandle(std::exchange(serverJob_, nullptr));
    if (server_.state() != QProcess::NotRunning) server_.kill();
}

void LiveTranslation::requestTranslation(Work work) {
    const auto& text = work.sentence.text;
    const auto& source = work.sentence.language;
    const bool speech = recognitionSource_ == VeyraAudio;
    requestedAt_ = clock_.elapsed();
    QUrl url;
    QByteArray token, payload;
    if (provider_ == MyMemory) {
        if (!languageCode(source)) {
            ++failures_;
            if (speech) skipSpeech(work, false);
            else ++skippedSentences_;
            setStatus(tr("MyMemory 需要选择原文语言"));
            return;
        }
        url = QUrl(QStringLiteral("https://api.mymemory.translated.net/get"));
        QUrlQuery query;
        QString input = text;
        while (input.toUtf8().size() > 500) input.chop(1);
        if (!input.isEmpty() && input.back().isHighSurrogate()) input.chop(1);
        query.addQueryItem(QStringLiteral("q"), QString::fromLatin1(QUrl::toPercentEncoding(input)));
        query.addQueryItem(QStringLiteral("langpair"), source + QLatin1Char('|') + targetLanguage_);
        url.setQuery(query);
    } else if (provider_ == Local) {
        url = completionEndpoint(QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort_));
        token = serverKey_;
        payload = chatRequest(text, work.context, QString::fromLatin1(localAlias), source, targetLanguage_, true);
    } else {
        url = completionEndpoint(apiBase_);
        token = apiKey_;
        payload = chatRequest(text, work.context, apiModel_, source, targetLanguage_, false);
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(work.screenStart >= 0 && provider_ == Local ? localScreenTimeoutMs : 8000);
    if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token);
    auto& manager = provider_ == Local || QHostAddress(url.host()).isLoopback()
        ? localNetwork_ : remoteNetwork_;
    if (payload.isEmpty()) request_ = manager.get(request);
    else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request_ = manager.post(request, payload);
    }
    ++requestedCount_;
    requestSentence_ = work.id;
    if (speech) speechRequest_ = work;
    auto* reply = request_;
    reply->setReadBufferSize(maxResponseBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > maxResponseBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, work = std::move(work)] {
        request_ = nullptr;
        requestSentence_ = 0;
        speechRequest_.reset();
        receiveTranslation(reply, work);
        reply->deleteLater();
    });
}

void LiveTranslation::receiveTranslation(QNetworkReply* reply, const Work& work) {
    if (!enabled_ || !active_) return;
    const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool screen = work.screenStart >= 0;
    const bool speech = recognitionSource_ == VeyraAudio;
    if (speech && sink::recordingClockUs() - work.sentence.endUs > maxSpeechAgeUs) {
        skipSpeech(work, true);
        return;
    }
    const auto now = clock_.elapsed();
    if (provider_ == Local && code == 503 && now - serverStartedAt_ < (usingGpu() ? 45000 : 20000)) {
        if (!screen) queue_.prepend(work);
        else if (work.id == screenSequence_) {
            screenRequested_ = 0;
            screenRetryAt_ = now + 350;
        }
        setStatus(tr("正在加载本地翻译模型…"));
        return;
    }
    const auto retryScreen = [&] {
        if (screen && work.id == screenSequence_ && screenRetries_ < maxScreenRetries) {
            ++screenRetries_;
            screenRequested_ = 0;
            screenRetryAt_ = now + screenRetryDelayMs;
        }
    };
    if (requestTimes_.size() < 256) requestTimes_.push_back(now - requestedAt_);
    if (code < 200 || code >= 300 || reply->error() != QNetworkReply::NoError) {
        ++failures_;
        if (speech) skipSpeech(work, false);
        else ++skippedSentences_;
        retryScreen();
        log::warn("live-translate", std::format("request failed provider={} http={} network={}",
            provider_, code, int(reply->error())));
        setStatus(code ? tr("翻译接口返回 HTTP %1").arg(code) : tr("翻译请求失败，请检查连接"));
        return;
    }
    const auto result = QJsonDocument::fromJson(reply->readAll()).object();
    QString translation;
    if (provider_ == MyMemory) {
        if (result.value(QStringLiteral("responseStatus")).toVariant().toInt() == 200)
            translation = result.value(QStringLiteral("responseData")).toObject().value(QStringLiteral("translatedText")).toString();
    } else {
        const auto choices = result.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) translation = choices.first().toObject().value(QStringLiteral("message"))
            .toObject().value(QStringLiteral("content")).toString();
    }
    translation = translation.simplified().left(500);
    const bool echoed = !screen && provider_ == Local && work.sentence.text.count(u' ') >= 2 &&
        languageCode(work.sentence.language) &&
        QLocale(work.sentence.language).language() != QLocale(targetLanguage_).language() &&
        translation.compare(work.sentence.text, Qt::CaseInsensitive) == 0;
    if (echoed && !work.context.isEmpty() &&
        sink::recordingClockUs() - work.sentence.endUs < (speech ? maxSpeechAgeUs : maxQueuedAgeUs)) {
        Work retry = work;
        retry.context.clear();
        queue_.prepend(std::move(retry));
        log::warn("live-translate", "local provider echoed the input, retry without context");
        return;
    }
    if (translation.isEmpty() || echoed) {
        ++failures_;
        if (speech) skipSpeech(work, false);
        else ++skippedSentences_;
        retryScreen();
        setStatus(tr("翻译服务未返回译文"));
        return;
    }
    cache_.insert(work.cacheKey, new QString(translation));
    displayTranslation(work, std::move(translation));
}

void LiveTranslation::summarize(qint64 now) {
    lastSummaryAt_ = now;
    if (!sentenceCount_ && !requestedCount_ && !displayedSentences_ && !stale_ && !failures_) return;
    if (!requestTimes_.empty()) {
        const auto total = std::accumulate(requestTimes_.begin(), requestTimes_.end(), qint64{0});
        averageTranslationMs_ = double(total) / double(requestTimes_.size());
    }
    std::ranges::sort(requestTimes_);
    std::ranges::sort(displayDelays_);
    std::ranges::sort(spokenDelays_);
    log::info("live-translate", std::format("provider={} sentences={} requests={} cacheHits={} requestP50ms={} requestP95ms={} overlayP50ms={} overlayP95ms={} stale={} hidden={} failed={} shownSentences={} earlySentences={} wordTimeSamples={} wordEndP50ms={} wordEndP95ms={}",
        provider_, sentenceCount_, requestedCount_, cacheHits_, percentile(requestTimes_, 50), percentile(requestTimes_, 95),
        percentile(displayDelays_, 50), percentile(displayDelays_, 95), stale_, hidden_, failures_,
        displayedSentences_, earlySentences_, spokenDelays_.size(), percentile(spokenDelays_, 50), percentile(spokenDelays_, 95)));
    requestTimes_.clear();
    displayDelays_.clear();
    spokenDelays_.clear();
    earlySentences_ = displayedSentences_ = 0;
    sentenceCount_ = requestedCount_ = cacheHits_ = stale_ = hidden_ = failures_ = 0;
    emit changed();
}
} // namespace veyra::ui
