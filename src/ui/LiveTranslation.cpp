// SPDX-License-Identifier: GPL-3.0-only
// Based on the Windows Live Captions translation flow from jjjxxxttt, PR #203.
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
constexpr qint64 maxResponseBytes = 128 * 1024;

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

    recognitionSource_ = settings_.value(QStringLiteral("recognitionSource"), int(VeyraAudio)).toInt() == WindowsCaptions &&
        windowsCaptionsSupported() ? WindowsCaptions : VeyraAudio;
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
    connect(&assets_.downloads, &ModelAssets::changed, this, &LiveTranslation::changed);
    connect(&asr_, &LiveAsr::changed, this, &LiveTranslation::changed);
    connect(&asr_, &LiveAsr::audioDiscontinuity, this, &LiveTranslation::resetCaption);
    connect(&asr_, &LiveAsr::finalText, this, [this](const QString& text, const QString& language, qint64 endUs) {
        if (!enabled_ || recognitionSource_ != VeyraAudio || !active_) return;
        addSentences(sentences_.appendFinal(text, language, endUs, clock_.elapsed()));
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

QString LiveTranslation::caption() const {
    return !rows_.empty() && clock_.elapsed() < rows_.back().originalUntil ? rows_.back().original : QString{};
}

QString LiveTranslation::translated() const {
    const auto now = clock_.elapsed();
    for (auto row = rows_.rbegin(); row != rows_.rend(); ++row) {
        if (!row->translated.isEmpty() && row->until > now) return row->translated;
    }
    return {};
}

void LiveTranslation::setStatus(QString status) {
    if (status_ == status) return;
    status_ = std::move(status);
    emit changed();
}

void LiveTranslation::configureAsr() {
    const int cores = QThread::idealThreadCount();
    const int available = std::max(1, cores / 2 - (provider_ == Local ? translationThreads() : 0) - 1);
    const int threads = recognitionThreads_ ? recognitionThreads_ : std::clamp(cores / 4, 1, 4);
    asr_.configure(recognitionModel_, sourceLanguage(), std::min(threads, available));
}

void LiveTranslation::setEnabled(bool value) {
    if (enabled_ == value) return;
    if (value && recognitionSource_ == WindowsCaptions && !windowsCaptionsSupported()) {
        setStatus(tr("Windows 实时字幕需要 Windows 11 22H2 或更新版本"));
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
    asr_.setEnabled(value && recognitionSource_ == VeyraAudio);
    if (value) {
        lastSummaryAt_ = clock_.elapsed();
        status_ = recognitionSource_ == WindowsCaptions ? tr("请打开 Windows 实时字幕") : QString{};
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
    if (value < VeyraAudio || value > WindowsCaptions || recognitionSource_ == value ||
        (value == WindowsCaptions && !windowsCaptionsSupported())) return;
    const bool enabled = enabled_;
    setEnabled(false);
    recognitionSource_ = value;
    settings_.setValue(QStringLiteral("recognitionSource"), value);
    configureAsr();
    setEnabled(enabled);
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

void LiveTranslation::addSentences(std::vector<LiveSentence> sentences) {
    if (sentences.empty()) return;
    const auto now = clock_.elapsed();
    for (auto& sentence : sentences) {
        if (!languageCode(sentence.language)) sentence.language = sourceLanguage();
        const auto id = ++nextSentence_;
        rows_.push_back({id, now + captionLifetimeMs, now + displayDuration(sentence.text), sentence.text, {}});
        if (rows_.size() > 2) rows_.pop_front();

        const QString cacheKey = QString::number(provider_) + u'\n' + sentence.language + u'\n'
            + targetLanguage_ + u'\n' + sentence.text;
        Work work{id, std::move(sentence), context_, cacheKey};
        context_ << work.sentence.text;
        if (context_.size() > 2) context_.removeFirst();
        ++sentenceCount_;

        if (!work.sentence.language.isEmpty() &&
            work.sentence.language.compare(targetLanguage_, Qt::CaseInsensitive) == 0) {
            displayTranslation(work, work.sentence.text);
        } else if (const auto* cached = cache_.object(cacheKey)) {
            ++cacheHits_;
            displayTranslation(work, *cached);
        } else {
            if (queue_.size() >= 3) {
                queue_.dequeue();
                ++overflow_;
                ++skippedSentences_;
            }
            queue_.enqueue(std::move(work));
        }
    }
    emit captionChanged();
}

void LiveTranslation::displayTranslation(const Work& work, QString translation) {
    const auto now = clock_.elapsed();
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
    if (rows_.size() != old) emit captionChanged();
    const auto minimumUs = sink::recordingClockUs() - maxQueuedAgeUs;
    while (!queue_.isEmpty() && queue_.head().sentence.endUs < minimumUs) {
        queue_.dequeue();
        ++stale_;
        ++skippedSentences_;
    }
}

void LiveTranslation::resetCaption() {
    cancelRequest();
    sentences_.reset(raw_);
    queue_.clear();
    context_.clear();
    if (rows_.empty()) return;
    rows_.clear();
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
    prune(now);
    addSentences(sentences_.flush(now));
    if (provider_ == Local) {
        if (downloading()) return;
        if (!modelInstalled()) { setStatus(tr("请先下载本地翻译模型")); return; }
        if (serverFailed_) return;
        if (recognitionSource_ == VeyraAudio && !asr_.installed()) return;
        if (!serverPort_ && server_.state() == QProcess::NotRunning) startLocal();
        if (!serverJob_ || server_.state() != QProcess::Running) return;
    }
    if (queue_.isEmpty() || request_) return;
    if (provider_ == Local && !loopbackListenerIsOwned(serverPort_, DWORD(server_.processId()))) {
        if (now - serverStartedAt_ > 20000) {
            stopLocal();
            serverFailed_ = true;
            setStatus(tr("本地翻译服务启动超时，请重新启用"));
        }
        return;
    }
    requestTranslation(queue_.dequeue());
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
         QStringLiteral("--n-gpu-layers"), QStringLiteral("0"), QStringLiteral("--offline"),
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
    requestedAt_ = clock_.elapsed();
    QUrl url;
    QByteArray token, payload;
    if (provider_ == MyMemory) {
        if (!languageCode(source)) {
            ++failures_;
            ++skippedSentences_;
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
    request.setTransferTimeout(8000);
    if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token);
    auto& manager = provider_ == Local || QHostAddress(url.host()).isLoopback()
        ? localNetwork_ : remoteNetwork_;
    if (payload.isEmpty()) request_ = manager.get(request);
    else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request_ = manager.post(request, payload);
    }
    ++requestedCount_;
    auto* reply = request_;
    reply->setReadBufferSize(maxResponseBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > maxResponseBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, work = std::move(work)] {
        request_ = nullptr;
        receiveTranslation(reply, work);
        reply->deleteLater();
    });
}

void LiveTranslation::receiveTranslation(QNetworkReply* reply, const Work& work) {
    if (!enabled_ || !active_) return;
    const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (provider_ == Local && code == 503 && clock_.elapsed() - serverStartedAt_ < 20000) {
        if (queue_.size() < 3) queue_.prepend(work);
        else { ++overflow_; ++skippedSentences_; }
        setStatus(tr("正在加载本地翻译模型…"));
        return;
    }
    if (requestTimes_.size() < 256) requestTimes_.push_back(clock_.elapsed() - requestedAt_);
    if (code < 200 || code >= 300 || reply->error() != QNetworkReply::NoError) {
        ++failures_;
        ++skippedSentences_;
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
    const bool echoed = provider_ == Local && work.sentence.text.count(u' ') >= 2 &&
        languageCode(work.sentence.language) &&
        QLocale(work.sentence.language).language() != QLocale(targetLanguage_).language() &&
        translation.compare(work.sentence.text, Qt::CaseInsensitive) == 0;
    if (echoed && !work.context.isEmpty() && queue_.size() < 3 &&
        sink::recordingClockUs() - work.sentence.endUs < maxQueuedAgeUs) {
        Work retry = work;
        retry.context.clear();
        queue_.prepend(std::move(retry));
        log::warn("live-translate", "local provider echoed the input, retry without context");
        return;
    }
    if (translation.isEmpty() || echoed) {
        ++failures_;
        ++skippedSentences_;
        setStatus(tr("翻译服务未返回译文"));
        return;
    }
    cache_.insert(work.cacheKey, new QString(translation));
    displayTranslation(work, std::move(translation));
}

void LiveTranslation::summarize(qint64 now) {
    lastSummaryAt_ = now;
    if (!sentenceCount_ && !requestedCount_ && !failures_) return;
    if (!requestTimes_.empty()) {
        const auto total = std::accumulate(requestTimes_.begin(), requestTimes_.end(), qint64{0});
        averageTranslationMs_ = double(total) / double(requestTimes_.size());
    }
    std::ranges::sort(requestTimes_);
    std::ranges::sort(displayDelays_);
    log::info("live-translate", std::format("provider={} sentences={} requests={} cacheHits={} requestP50ms={} requestP95ms={} overlayP50ms={} overlayP95ms={} stale={} overflow={} failed={}",
        provider_, sentenceCount_, requestedCount_, cacheHits_, percentile(requestTimes_, 50), percentile(requestTimes_, 95),
        percentile(displayDelays_, 50), percentile(displayDelays_, 95), stale_, overflow_, failures_));
    requestTimes_.clear();
    displayDelays_.clear();
    sentenceCount_ = requestedCount_ = cacheHits_ = stale_ = overflow_ = failures_ = 0;
    emit changed();
}
} // namespace veyra::ui
