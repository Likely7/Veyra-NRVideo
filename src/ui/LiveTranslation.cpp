// SPDX-License-Identifier: GPL-3.0-only
// Based on the Windows Live Captions translation flow from jjjxxxttt, PR #203.
// The on-screen text source (subtitles read from the picture by OCR in the engine,
// ScreenTextCapture) follows VI Translator's capture flow (MIT License).
#include <winsock2.h>
#include "LiveTranslation.h"
#include "WinCaptionReader.h"
#include "veyra/Log.h"

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
#include <utility>
#include <vector>
#include <windows.h>
#include <iphlpapi.h>

namespace veyra::ui {
namespace {
constexpr auto localAlias = "veyra-translation";
constexpr qint64 captionLifetimeMs = 6500;
constexpr qint64 maxResponseBytes = 128 * 1024;
constexpr int cacheEntries = 300;
// On-screen source: a CPU model can need several seconds for a line, so its requests get
// longer; a failed line is tried again a couple of times instead of being left untranslated.
constexpr int localScreenTimeoutMs = 30000, retryDelayMs = 1500, maxRetries = 2;
// The engine reports an on-screen line gone once it has not been read for this long
// (ScreenText.h, LineTracker::clearAfterMs): the line really left that much earlier.
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

// A region inside the picture, large enough to hold a line of text.
QRectF boundedRegion(QRectF r) {
    if (!std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.width()) || !std::isfinite(r.height()))
        return {0.08, 0.70, 0.84, 0.27};
    const double x = std::clamp(r.x(), 0.0, 0.95), y = std::clamp(r.y(), 0.0, 0.95);
    return {x, y, std::clamp(r.width(), 0.05, 1.0 - x), std::clamp(r.height(), 0.05, 1.0 - y)};
}

bool sameCaption(const QString& earlier, const QString& latest) {
    return earlier == latest || (earlier.size() >= 8 && latest.startsWith(earlier));
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

QByteArray chatRequest(const QString& text, const QString& model, const QString& source, const QString& target, bool local) {
    QString instruction = QStringLiteral("Translate the subtitle into %1. Preserve the meaning and tone. Return only the translation.")
        .arg(languageName(target));
    if (languageCode(source)) instruction += QStringLiteral(" Input language: %1.").arg(languageName(source));
    QJsonObject request{
        {"model", model}, {"stream", false},
        {"messages", QJsonArray{
            QJsonObject{{"role", "system"}, {"content", instruction}},
            QJsonObject{{"role", "user"}, {"content", text}}
        }}
    };
    if (local) {
        request.insert(QStringLiteral("temperature"), 0.1);
        request.insert(QStringLiteral("max_tokens"), 220);
    }
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}
}

LiveTranslation::LiveTranslation(const QString& directory, QObject* parent)
    : QObject(parent), assets_(directory),
      settings_(QDir(directory).filePath(QStringLiteral("live-translation.ini")), QSettings::IniFormat) {
    provider_ = std::clamp(settings_.value(QStringLiteral("provider"), int(Local)).toInt(), int(Local), int(MyMemory));
    sourceLanguage_ = settings_.value(QStringLiteral("source")).toString();
    targetLanguage_ = settings_.value(QStringLiteral("target"), QLocale().bcp47Name()).toString();
    if (!sourceLanguage_.isEmpty() && !languageCode(sourceLanguage_)) sourceLanguage_ = QStringLiteral("en");
    if (provider_ == MyMemory && sourceLanguage_.isEmpty()) sourceLanguage_ = QStringLiteral("en");
    if (targetLanguage_ == QLatin1String("zh")) targetLanguage_ = QStringLiteral("zh-CN");
    if (!languageCode(targetLanguage_)) targetLanguage_ = QStringLiteral("en");
    showOriginal_ = settings_.value(QStringLiteral("original"), true).toBool();
    useGpu_ = settings_.value(QStringLiteral("gpu"), true).toBool();
    apiBase_ = settings_.value(QStringLiteral("apiBase")).toString();
    apiModel_ = settings_.value(QStringLiteral("apiModel")).toString();
    // Without Windows Live Captions (Windows 10), the on-screen source is the one that works.
    captionSource_ = std::clamp(settings_.value(QStringLiteral("captionSource"),
        int(supported() || !screenTextSupported() ? WindowsCaptions : ScreenText)).toInt(), int(WindowsCaptions), int(ScreenText));
    screenRegion_ = std::clamp(settings_.value(QStringLiteral("screenRegion"), int(BottomSubtitles)).toInt(),
        int(BottomSubtitles), int(CustomRegion));
    if (settings_.contains(QStringLiteral("screenRect")))
        customRegion_ = boundedRegion(settings_.value(QStringLiteral("screenRect")).toRectF());

    localNetwork_.setProxy(QNetworkProxy::NoProxy);
    reader_.setStandardErrorFile(QProcess::nullDevice());
    server_.setStandardOutputFile(QProcess::nullDevice());
    server_.setStandardErrorFile(QProcess::nullDevice());
    auto childFlags = [](QProcess::CreateProcessArguments* args) {
        args->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    };
    reader_.setCreateProcessArgumentsModifier(childFlags);
    server_.setCreateProcessArgumentsModifier(childFlags);
    connect(&assets_, &TranslationAssets::changed, this, &LiveTranslation::changed);
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
        const HANDLE job = CreateJobObjectW(nullptr, nullptr);
        const HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(server_.processId()));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool assigned = job && child
            && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit))
            && AssignProcessToJobObject(job, child);
        if (child) CloseHandle(child);
        if (assigned) serverJob_ = job;
        else {
            if (job) CloseHandle(job);
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
    connect(&server_, &QProcess::finished, this, [this](int code) {
        if (serverJob_) CloseHandle(std::exchange(serverJob_, nullptr));
        if (enabled_ && provider_ == Local && serverPort_) {
            // Stopped on the graphics card (its driver or runtime refused the model):
            // tick() starts the same model again on the CPU.
            if (useGpu_ && !gpuFailed_) {
                gpuFailed_ = true;
                serverPort_ = 0;
                serverKey_.clear();
                log::info("live-translation", std::format("local server stopped on the GPU code={}, using the CPU", code));
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
    cancelRequest();
    stopLocal();
    for (auto* process : {&reader_, &server_}) {
        if (process->state() == QProcess::NotRunning) continue;
        process->kill();
        process->waitForFinished(1500);
    }
}

bool LiveTranslation::supported() {
    const auto version = QOperatingSystemVersion::current();
    return version >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621) &&
        QFileInfo::exists(windowsSystemExecutable(L"LiveCaptions.exe"));
}

bool LiveTranslation::screenTextSupported() {
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10;
}

bool LiveTranslation::sourceSupported() const {
    return captionSource_ == ScreenText ? screenTextSupported() : supported();
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

QString LiveTranslation::status() const {
    if (provider_ == Local && (assets_.downloading() || (!modelInstalled() && !assets_.status().isEmpty())))
        return assets_.status();
    return status_;
}

void LiveTranslation::setStatus(QString status) {
    if (status_ == status) return;
    status_ = std::move(status);
    emit changed();
}

void LiveTranslation::setEnabled(bool value) {
    if (enabled_ == value) return;
    if (value && !sourceSupported()) {
        setStatus(captionSource_ == ScreenText ? tr("画面文字识别需要 Windows 10 或更新版本")
                                              : tr("实时翻译需要 Windows 11 22H2 或更新版本"));
        return;
    }
    if (value && provider_ == OpenAi && (completionEndpoint(apiBase_).isEmpty() || apiModel_.isEmpty())) {
        setStatus(tr("请填写 HTTPS 接口地址和模型名，本机服务可使用 HTTP"));
        return;
    }
    enabled_ = value;
    resetCaption();
    raw_.clear();
    captionBuffer_.clear();
    readerStarted_ = false;
    captionsOpen_ = false;
    serverFailed_ = false;
    gpuFailed_ = false;
    screenError_.clear();
    if (enabled_) {
        setStatus(captionSource_ == ScreenText ? tr("正在启动画面文字识别…") : tr("请打开 Windows 实时字幕"));
        timer_.start();
    } else {
        timer_.stop();
        if (reader_.state() != QProcess::NotRunning) reader_.kill();
        stopLocal();
        status_.clear();
    }
    emit changed();
}

void LiveTranslation::setProvider(int value) {
    if (value < Local || value > MyMemory || provider_ == value) return;
    setEnabled(false);
    provider_ = value;
    if (provider_ == MyMemory && sourceLanguage_.isEmpty()) setSourceLanguage(QStringLiteral("en"));
    settings_.setValue(QStringLiteral("provider"), value);
    emit changed();
}

void LiveTranslation::setSourceLanguage(QString value) {
    if ((!value.isEmpty() && !languageCode(value)) || sourceLanguage_ == value) return;
    sourceLanguage_ = std::move(value);
    settings_.setValue(QStringLiteral("source"), sourceLanguage_);
    const auto text = raw_;
    raw_.clear();
    resetCaption();
    acceptCaption(text);
    emit changed();
}

void LiveTranslation::setTargetLanguage(QString value) {
    if (!languageCode(value) || targetLanguage_ == value) return;
    targetLanguage_ = std::move(value);
    settings_.setValue(QStringLiteral("target"), targetLanguage_);
    const auto text = raw_;
    raw_.clear();
    resetCaption();
    acceptCaption(text);
    emit changed();
}

void LiveTranslation::setCaptionSource(int value) {
    if (value < WindowsCaptions || value > ScreenText || captionSource_ == value) return;
    setEnabled(false);
    captionSource_ = value;
    settings_.setValue(QStringLiteral("captionSource"), value);
    emit changed();
}

void LiveTranslation::setScreenRegion(int value) {
    if (value < BottomSubtitles || value > CustomRegion || screenRegion_ == value) return;
    screenRegion_ = value;
    settings_.setValue(QStringLiteral("screenRegion"), value);
    emit changed();
}

void LiveTranslation::setCustomRegion(QRectF value) {
    value = boundedRegion(value);
    if (customRegion_ == value) return;
    customRegion_ = value;
    settings_.setValue(QStringLiteral("screenRect"), value);
    emit changed();
}

void LiveTranslation::acceptScreenText(const QString& text, bool ready, const QString& error,
                                       const QString& reading, int words) {
    if (!enabled_ || captionSource_ != ScreenText) return;
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
        else if (caption_.isEmpty()) setStatus(tr("正在等待画面字幕…"));
    }
    acceptCaption(text.left(500));
}

void LiveTranslation::setShowOriginal(bool value) {
    if (showOriginal_ == value) return;
    showOriginal_ = value;
    settings_.setValue(QStringLiteral("original"), value);
    emit changed();
}

void LiveTranslation::setUseGpu(bool value) {
    if (useGpu_ == value) return;
    useGpu_ = value;
    settings_.setValue(QStringLiteral("gpu"), value);
    // A running model keeps its device until it is started again.
    if (provider_ == Local && enabled_) {
        stopLocal();
        gpuFailed_ = false;
    }
    emit changed();
}

void LiveTranslation::setApiBase(QString value) {
    value = value.trimmed().left(1024);
    // Avoid saving credentials or query tokens in the configuration file.
    if (apiBase_ == value) return;
    if (provider_ == OpenAi) setEnabled(false);
    apiBase_ = std::move(value);
    if (apiBase_.isEmpty() || !completionEndpoint(apiBase_).isEmpty())
        settings_.setValue(QStringLiteral("apiBase"), apiBase_);
    emit changed();
}

void LiveTranslation::setApiModel(QString value) {
    value = value.trimmed().left(200);
    if (apiModel_ == value) return;
    if (provider_ == OpenAi) setEnabled(false);
    apiModel_ = std::move(value);
    settings_.setValue(QStringLiteral("apiModel"), apiModel_);
    emit changed();
}

void LiveTranslation::setApiKey(const QString& value) {
    if (value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'))) return;
    const auto key = value.trimmed().left(2048).toUtf8();
    if (apiKey_ == key) return;
    if (provider_ == OpenAi) setEnabled(false);
    apiKey_ = key;
}

void LiveTranslation::openWindowsCaptions() {
    if (supported() && !QProcess::startDetached(windowsSystemExecutable(L"LiveCaptions.exe"), {}))
        setStatus(tr("请按 Win + Ctrl + L 打开系统字幕"));
}

void LiveTranslation::downloadModel() {
    if (sourceSupported() && provider_ == Local && server_.state() == QProcess::NotRunning)
        assets_.download();
}

void LiveTranslation::cancelModelDownload() { assets_.cancel(); }

void LiveTranslation::setPlayback(quint64 session, bool active, bool discontinuity) {
    if (session_ == session && active_ == active && !discontinuity) return;
    session_ = session;
    active_ = active;
    // Keep the last raw line as a baseline until Windows has a new line. A line on
    // the picture is taken again as it is: it is still on screen.
    if (enabled_) resetCaption();
    if (captionSource_ != ScreenText) return;
    raw_.clear();
    if (enabled_ && active_ && captionsOpen_ && screenError_.isEmpty()) setStatus(tr("正在等待画面字幕…"));
}

void LiveTranslation::readCaptions() {
    captionBuffer_ += reader_.readAllStandardOutput();
    if (captionBuffer_.size() > 32 * 1024) captionBuffer_.clear();
    for (qsizetype end; (end = captionBuffer_.indexOf('\n')) >= 0;) {
        const auto message = QJsonDocument::fromJson(captionBuffer_.first(end)).object();
        captionBuffer_.remove(0, end + 1);
        if (!enabled_ || !message.contains(QStringLiteral("ready"))) continue;
        lastCaptionReadAt_ = clock_.elapsed();
        const bool ready = message.value(QStringLiteral("ready")).toBool();
        if (ready != captionsOpen_) {
            captionsOpen_ = ready;
            if (!ready) resetCaption();
            setStatus(ready ? tr("正在等待语音…") : tr("请打开 Windows 实时字幕"));
        }
        acceptCaption(message.value(QStringLiteral("text")).toString().left(300));
    }
}

void LiveTranslation::acceptCaption(const QString& text) {
    if (raw_ == text) return;
    raw_ = text;
    if (!enabled_ || !active_ || !captionsOpen_) return;
    if (captionSource_ == ScreenText) { acceptScreenLine(text); return; }
    if (text.isEmpty()) { resetCaption(); return; }
    if (!sameCaption(caption_, text)) { translated_.clear(); retries_ = 0; }
    caption_ = text;
    newTextAt_ = clock_.elapsed();
    emit captionChanged();
}

// Each new line on the picture gets a number; the line before it ended now (or, when the
// picture holds no text any more, when the engine last saw it). A line already translated
// goes to the presenter at once; the others are requested in tick().
void LiveTranslation::acceptScreenLine(const QString& text) {
    if (text == current_) return;
    const qint64 now = clock_.elapsed();
    if (!current_.isEmpty())
        presenter_.ended(currentSeq_, text.isEmpty() ? std::max(currentStart_, now - screenClearMs) : now);
    current_ = text;
    retries_ = 0;
    retryAt_ = 0;
    if (text.isEmpty()) {
        currentSeq_ = 0;
        return;
    }
    currentSeq_ = ++lineSeq_;
    currentStart_ = now;
    newTextAt_ = now;
    if (const auto known = knownTranslation(text)) {
        requestedSeq_ = currentSeq_;
        present(text, *known, currentSeq_, currentStart_);
    }
}

std::optional<QString> LiveTranslation::knownTranslation(const QString& text) const {
    if (!sourceLanguage_.isEmpty() && sourceLanguage_.compare(targetLanguage_, Qt::CaseInsensitive) == 0) return text;
    if (const auto hit = cache_.constFind(cacheKey(text)); hit != cache_.constEnd()) return *hit;
    return std::nullopt;
}

void LiveTranslation::present(const QString& original, const QString& translation, quint64 seq, qint64 start) {
    // Time to read it: about a second plus 40 ms a character, 1.2 to 3.5 s.
    const qint64 read = std::clamp<qint64>(1000 + 40 * qint64(std::max(original.size(), translation.size())), 1200, 3500);
    presenter_.ready(seq, start, ShownLine{original, translation}, clock_.elapsed(), read);
    stepPresenter();
}

void LiveTranslation::stepPresenter() {
    switch (presenter_.tick(clock_.elapsed())) {
    case CaptionPresenter<ShownLine>::Act::Show:
        caption_ = presenter_.shown()->original;
        translated_ = presenter_.shown()->translation;
        setStatus(tr("实时翻译运行中"));
        emit captionChanged();
        break;
    case CaptionPresenter<ShownLine>::Act::Hide:
        caption_.clear();
        translated_.clear();
        emit captionChanged();
        break;
    case CaptionPresenter<ShownLine>::Act::None:
        break;
    }
}

void LiveTranslation::resetCaption() {
    cancelRequest();
    retries_ = 0;
    requestedText_.clear();
    requestedAt_ = 0;
    current_.clear();
    currentSeq_ = requestedSeq_ = 0;
    retryAt_ = 0;
    presenter_.clear(lineSeq_);
    if (caption_.isEmpty() && translated_.isEmpty()) return;
    caption_.clear();
    translated_.clear();
    emit captionChanged();
}

void LiveTranslation::cancelRequest() {
    if (auto* reply = std::exchange(request_, nullptr)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void LiveTranslation::tick() {
    const auto now = clock_.elapsed();
    const bool screen = captionSource_ == ScreenText;
    if (!screen && !readerStarted_ && reader_.state() == QProcess::NotRunning) {
        readerStarted_ = true;
        reader_.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--read-live-captions")});
    }
    // A live caption ages out; a line on the picture stays for as long as it is shown.
    if (!screen && !caption_.isEmpty() && now - newTextAt_ > captionLifetimeMs) resetCaption();
    if (!active_) {
        if (serverPort_) stopLocal();
        setStatus(tr("播放或连接片源后开始翻译"));
        return;
    }
    if (screen) {
        if (!screenError_.isEmpty()) { setStatus(screenError_); return; }
        if (!captionsOpen_ || now - lastCaptionReadAt_ > 4000) {
            resetCaption();
            setStatus(tr("正在启动画面文字识别…"));
            return;
        }
        // What is shown changes with time, whatever the translator is doing.
        stepPresenter();
    } else if (!captionsOpen_) {
        if (reader_.state() != QProcess::NotRunning) setStatus(tr("请打开 Windows 实时字幕"));
        return;
    } else if (now - lastCaptionReadAt_ > 4000) {
        resetCaption();
        setStatus(tr("正在等待 Windows 字幕响应…"));
        return;
    }
    if (provider_ == Local) {
        if (downloading()) return;
        if (!modelInstalled()) { setStatus(tr("请先下载本地翻译模型")); return; }
        if (serverFailed_) return;
        if (!serverPort_ && server_.state() == QProcess::NotRunning) startLocal();
        if (!serverJob_ || server_.state() != QProcess::Running) return;
    }
    if (screen) { tickScreen(now); return; }
    if (caption_.isEmpty()) return;
    if (caption_ == requestedText_ || request_) return;
    // Live captions grow word by word: wait for a pause before sending.
    if (now - requestedAt_ < 850 || (now - newTextAt_ < 300 && now - requestedAt_ < 1400)) return;
    if (provider_ == Local && !loopbackListenerIsOwned(serverPort_, DWORD(server_.processId()))) {
        serverNotListening(now);
        return;
    }
    requestTranslation(caption_);
}

// On-screen source: one request at a time, always for the line on the picture now. A line
// replaced while another was being translated is skipped (it is gone); the one being
// translated is not aborted - the presenter still shows it when it comes.
void LiveTranslation::tickScreen(qint64 now) {
    auto listening = [this] { return provider_ != Local || loopbackListenerIsOwned(serverPort_, DWORD(server_.processId())); };
    if (current_.isEmpty()) {
        // The model is up and nothing is on screen yet: say so instead of "loading".
        if (provider_ == Local && status_ == tr("正在加载本地翻译模型…") && serverPort_ && listening())
            setStatus(tr("正在等待画面字幕…"));
        return;
    }
    if (request_ || currentSeq_ == requestedSeq_ || now < retryAt_) return;
    if (!listening()) {
        serverNotListening(now);
        return;
    }
    requestTranslation(current_, currentSeq_, currentStart_);
}

// The local server is still not answering. Too long on the graphics card (a driver that
// hangs while it prepares its shaders): the CPU instead. Too long on the CPU: give up.
void LiveTranslation::serverNotListening(qint64 now) {
    const bool gpu = useGpu_ && !gpuFailed_;
    if (now - serverStartedAt_ <= (gpu ? 45000 : 20000)) return;
    stopLocal();
    if (gpu) {
        gpuFailed_ = true;
        log::info("live-translation", "local server did not start on the GPU, using the CPU");
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
    // On the graphics card (Vulkan) the whole model runs there: a line takes a fraction of a
    // second instead of seconds. Without a usable card llama.cpp stays on the CPU.
    const bool gpu = useGpu_ && !gpuFailed_;
    const int threads = std::clamp(QThread::idealThreadCount() / 2, 2, 8);
    const auto n = QString::number(threads);
    server_.setProcessEnvironment(env);
    server_.setWorkingDirectory(assets_.runtimePath());
    serverStartedAt_ = clock_.elapsed();
    log::info("live-translation", std::format("local server start gpu={} threads={}", gpu, threads));
    setStatus(tr("正在加载本地翻译模型…"));
    server_.start(QDir(assets_.runtimePath()).filePath(QStringLiteral("llama-server.exe")),
        {QStringLiteral("--model"), assets_.modelPath(), QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
         QStringLiteral("--port"), QString::number(serverPort_), QStringLiteral("--threads"), n,
         QStringLiteral("--threads-batch"), n, QStringLiteral("--parallel"), QStringLiteral("1"),
         QStringLiteral("--alias"), QString::fromLatin1(localAlias), QStringLiteral("--ctx-size"), QStringLiteral("1024"),
         QStringLiteral("--n-gpu-layers"), gpu ? QStringLiteral("99") : QStringLiteral("0"), QStringLiteral("--offline"),
         QStringLiteral("--no-webui"), QStringLiteral("--log-disable")});
}

void LiveTranslation::stopLocal() {
    serverPort_ = 0;
    serverKey_.clear();
    if (serverJob_) CloseHandle(std::exchange(serverJob_, nullptr));
    if (server_.state() != QProcess::NotRunning) server_.kill();
}

void LiveTranslation::requestTranslation(const QString& text, quint64 seq, qint64 start) {
    requestedAt_ = clock_.elapsed();
    requestedText_ = text;
    requestedSeq_ = seq;
    if (!seq) {
        if (const auto known = knownTranslation(text)) {
            translated_ = *known;
            setStatus(tr("实时翻译运行中"));
            emit captionChanged();
            return;
        }
    }
    QUrl url;
    QByteArray token, payload;
    if (provider_ == MyMemory) {
        if (sourceLanguage_.isEmpty()) {
            setStatus(tr("MyMemory 需要选择原文语言"));
            return;
        }
        url = QUrl(QStringLiteral("https://api.mymemory.translated.net/get"));
        QUrlQuery query;
        QString input = text;
        while (input.toUtf8().size() > 500) input.chop(1);
        if (!input.isEmpty() && input.back().isHighSurrogate()) input.chop(1);
        query.addQueryItem(QStringLiteral("q"), QString::fromLatin1(QUrl::toPercentEncoding(input)));
        query.addQueryItem(QStringLiteral("langpair"), sourceLanguage_ + QLatin1Char('|') + targetLanguage_);
        url.setQuery(query);
    } else if (provider_ == Local) {
        url = completionEndpoint(QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort_));
        token = serverKey_;
        payload = chatRequest(text, QString::fromLatin1(localAlias), sourceLanguage_, targetLanguage_, true);
    } else {
        url = completionEndpoint(apiBase_);
        token = apiKey_;
        payload = chatRequest(text, apiModel_, sourceLanguage_, targetLanguage_, false);
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(captionSource_ == ScreenText && provider_ == Local ? localScreenTimeoutMs : 8000);
    if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token);
    auto& manager = provider_ == Local || QHostAddress(url.host()).isLoopback() || url.host() == QLatin1String("localhost")
        ? localNetwork_ : remoteNetwork_;
    if (payload.isEmpty()) {
        request_ = manager.get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request_ = manager.post(request, payload);
    }
    auto* reply = request_;
    reply->setReadBufferSize(maxResponseBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > maxResponseBytes) reply->abort();
    });
    const qint64 sentAt = clock_.elapsed();
    connect(reply, &QNetworkReply::finished, this, [this, reply, text, seq, start, sentAt] {
        request_ = nullptr;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Timing and outcome only: the text itself is never logged.
        log::info("live-translation", std::format("request provider={} source={} http={} error={} ms={} chars={} line={}",
            provider_, captionSource_, code, int(reply->error()), clock_.elapsed() - sentAt, text.size(),
            seq ? (seq == currentSeq_ ? "current" : "earlier") : "live"));
        receiveTranslation(reply, text, seq, start);
        reply->deleteLater();
    });
}

QString LiveTranslation::parseTranslation(const QByteArray& body, QString* problem) const {
    const auto json = QJsonDocument::fromJson(body).object();
    QString translation;
    if (provider_ == MyMemory) {
        const int status = json.value(QStringLiteral("responseStatus")).toVariant().toInt();
        if (status == 200)
            translation = json.value(QStringLiteral("responseData")).toObject().value(QStringLiteral("translatedText")).toString();
        else if (status == 429 || json.value(QStringLiteral("quotaFinished")).toBool())
            *problem = tr("MyMemory 今日免费额度已用完，请换用其他翻译引擎");
        // Over the quota MyMemory may still answer 200 with a warning in place of the text.
        if (translation.startsWith(QLatin1String("MYMEMORY WARNING"))) {
            translation.clear();
            *problem = tr("MyMemory 今日免费额度已用完，请换用其他翻译引擎");
        }
    } else {
        const auto choices = json.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty())
            translation = choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    }
    if (translation.trimmed().isEmpty() && problem->isEmpty()) *problem = tr("翻译接口未返回译文");
    return translation.simplified().left(500);
}

void LiveTranslation::receiveTranslation(QNetworkReply* reply, const QString& text, quint64 seq, qint64 start) {
    if (!enabled_) return;
    const bool screen = seq != 0;
    // Windows Live Captions: only the line still being shown is of use.
    const bool current = screen ? seq == currentSeq_ : active_ && sameCaption(text, caption_);
    if (!screen && !current) return;
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (provider_ == Local && code == 503 && clock_.elapsed() - serverStartedAt_ < 45000) {
        if (!current) return;
        requestedText_.clear();
        if (screen) {
            requestedSeq_ = 0;
            retryAt_ = clock_.elapsed() + 500;
        }
        setStatus(tr("正在加载本地翻译模型…"));
        return;
    }
    if (code < 200 || code >= 300 || reply->error() != QNetworkReply::NoError) {
        if (!current) return;
        setStatus(code ? tr("翻译接口返回 HTTP %1").arg(code) : tr("翻译请求失败，请检查连接"));
        if (screen && retries_ < maxRetries) {
            // The same line again shortly (tick() waits for retryAt_).
            ++retries_;
            requestedSeq_ = 0;
            retryAt_ = clock_.elapsed() + retryDelayMs;
        }
        return;
    }
    QString problem;
    const auto result = parseTranslation(reply->readAll(), &problem);
    if (result.isEmpty()) {
        if (current) setStatus(problem);
        return;
    }
    remember(text, result);
    if (screen) {
        // Shown even when its line has gone meanwhile (the presenter decides for how long).
        present(text, result, seq, start);
        return;
    }
    translated_ = result;
    setStatus(tr("实时翻译运行中"));
    emit captionChanged();
}

QString LiveTranslation::cacheKey(const QString& text) const {
    QString key = QString::number(provider_) + QLatin1Char('\x1f') + sourceLanguage_ + QLatin1Char('\x1f') + targetLanguage_;
    if (provider_ == OpenAi) key += QLatin1Char('\x1f') + apiBase_ + QLatin1Char('\x1f') + apiModel_;
    return key + QLatin1Char('\x1f') + text;
}

void LiveTranslation::remember(const QString& text, const QString& translation) {
    const auto key = cacheKey(text);
    if (!cache_.contains(key)) cacheOrder_.append(key);
    cache_.insert(key, translation);
    while (cacheOrder_.size() > cacheEntries) cache_.remove(cacheOrder_.takeFirst());
}
} // namespace veyra::ui
