// SPDX-License-Identifier: GPL-3.0-only
// Based on the Windows Live Captions translation flow from jjjxxxttt, PR #203.
#include <winsock2.h>
#include "LiveTranslation.h"
#include "WinCaptionReader.h"

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
#include <utility>
#include <vector>
#include <windows.h>
#include <iphlpapi.h>

namespace veyra::ui {
namespace {
constexpr auto localAlias = "veyra-translation";
constexpr qint64 captionLifetimeMs = 6500;
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
    apiBase_ = settings_.value(QStringLiteral("apiBase")).toString();
    apiModel_ = settings_.value(QStringLiteral("apiModel")).toString();

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
    if (value && !supported()) {
        setStatus(tr("实时翻译需要 Windows 11 22H2 或更新版本"));
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
    if (enabled_) {
        setStatus(tr("请打开 Windows 实时字幕"));
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
    if (supported() && provider_ == Local && server_.state() == QProcess::NotRunning)
        assets_.download();
}

void LiveTranslation::cancelModelDownload() { assets_.cancel(); }

void LiveTranslation::setPlayback(quint64 session, bool active, bool discontinuity) {
    if (session_ == session && active_ == active && !discontinuity) return;
    session_ = session;
    active_ = active;
    // Keep the last raw line as a baseline until Windows has a new line.
    if (enabled_) resetCaption();
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
    if (text.isEmpty()) { resetCaption(); return; }
    if (!sameCaption(caption_, text)) translated_.clear();
    caption_ = text;
    newTextAt_ = clock_.elapsed();
    emit captionChanged();
}

void LiveTranslation::resetCaption() {
    cancelRequest();
    requestedText_.clear();
    requestedAt_ = 0;
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
    if (!readerStarted_ && reader_.state() == QProcess::NotRunning) {
        readerStarted_ = true;
        reader_.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--read-live-captions")});
    }
    if (!caption_.isEmpty() && now - newTextAt_ > captionLifetimeMs) resetCaption();
    if (!active_) {
        if (serverPort_) stopLocal();
        setStatus(tr("播放或连接片源后开始翻译"));
        return;
    }
    if (!captionsOpen_) {
        if (reader_.state() != QProcess::NotRunning) setStatus(tr("请打开 Windows 实时字幕"));
        return;
    }
    if (now - lastCaptionReadAt_ > 4000) {
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
    if (caption_.isEmpty() || caption_ == requestedText_ || request_ || now - requestedAt_ < 850
        || (now - newTextAt_ < 300 && now - requestedAt_ < 1400))
        return;
    if (provider_ == Local && !loopbackListenerIsOwned(serverPort_, DWORD(server_.processId()))) {
        if (now - serverStartedAt_ > 20000) {
            stopLocal();
            serverFailed_ = true;
            setStatus(tr("本地翻译服务启动超时，请重新启用"));
        }
        return;
    }
    requestTranslation();
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
    const int threads = std::clamp(QThread::idealThreadCount() / 4, 1, 4);
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

void LiveTranslation::requestTranslation() {
    const auto text = caption_;
    requestedAt_ = clock_.elapsed();
    requestedText_ = text;
    if (sourceLanguage_.compare(targetLanguage_, Qt::CaseInsensitive) == 0) {
        translated_ = text;
        setStatus(tr("实时翻译运行中"));
        emit captionChanged();
        return;
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
    request.setTransferTimeout(8000);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, text] {
        request_ = nullptr;
        receiveTranslation(reply, text);
        reply->deleteLater();
    });
}

void LiveTranslation::receiveTranslation(QNetworkReply* reply, const QString& text) {
    if (!enabled_ || !active_ || !sameCaption(text, caption_)) return;
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (provider_ == Local && code == 503 && clock_.elapsed() - serverStartedAt_ < 20000) {
        requestedText_.clear();
        setStatus(tr("正在加载本地翻译模型…"));
        return;
    }
    if (code < 200 || code >= 300 || reply->error() != QNetworkReply::NoError) {
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
        if (!choices.isEmpty())
            translation = choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    }
    if (translation.trimmed().isEmpty()) {
        setStatus(tr("翻译接口未返回译文"));
        return;
    }
    translated_ = translation.simplified().left(500);
    setStatus(tr("实时翻译运行中"));
    emit captionChanged();
}
} // namespace veyra::ui
