#include "veyra/ui/UpdateChecker.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QDebug>

namespace veyra::ui {
namespace {
constexpr qsizetype maxBody = 128 * 1024;
QVersionNumber versionNumber(const QString& text, bool local) {
    static const QRegularExpression stable(QStringLiteral("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$"));
    static const QRegularExpression installed(QStringLiteral("^v?([0-9]+\\.[0-9]+\\.[0-9]+)(?:\\.0)?(?:-[A-Za-z0-9.-]+)?$"));
    const auto match = (local ? installed : stable).match(text);
    return match.hasMatch() ? QVersionNumber::fromString(match.captured(1)) : QVersionNumber{};
}
}
UpdateChecker::UpdateChecker(QString currentVersion, QObject* parent, QUrl endpoint, int timeoutMs)
    : QObject(parent), currentVersion_(std::move(currentVersion)), endpoint_(std::move(endpoint)), timeoutMs_(timeoutMs),
      portable_(currentVersion_) {
    connect(this, &UpdateChecker::releaseChanged, &portable_, [this] { portable_.selectRelease(latestVersion_); });
    connect(&portable_, &PortableUpdate::notice, this, &UpdateChecker::notice);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (reply_) { failure_ = QStringLiteral("timeout"); reply_->abort(); }
    });
}
UpdateChecker::~UpdateChecker() {
    deadline_.stop();
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
}
void UpdateChecker::startAutomaticCheck() {
    if (automaticScheduled_ || QCoreApplication::arguments().contains(QStringLiteral("--portable-update-probe"))) return;
    automaticScheduled_ = true;
    QTimer::singleShot(2500, this, [this] {
        // A manual check made during startup already fulfils this run's check.
        if (!checking() && !manual_) check(false);
    });
}
void UpdateChecker::setDownloadRoute(veyra::portable::DownloadRoute route) {
    route_ = route;
    portable_.setDownloadRoute(route);
}
void UpdateChecker::check(bool manual) {
    if (checking()) { manual_ = manual_ || manual; return; }
    manual_ = manual;
    request(endpoint_ == veyra::portable::releaseApiUrl() ? veyra::portable::releaseApiUrl(route_) : endpoint_);
    emit checkingChanged();
}
void UpdateChecker::request(const QUrl& url) {
    usingMirror_ = url != endpoint_;
    body_.clear(); failure_.clear();
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Veyra-Update-Check");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, usingMirror_
        ? QNetworkRequest::ManualRedirectPolicy : QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(timeoutMs_);
    reply_ = network_.get(request);
    qInfo().noquote() << "[update-check-start]" << (manual_ ? "manual" : "automatic") << url.host();
    reply_->setReadBufferSize(maxBody + 1);
    connect(reply_, &QIODevice::readyRead, this, [this] {
        body_ += reply_->read(maxBody + 1 - body_.size());
        if (body_.size() > maxBody) { failure_ = QStringLiteral("response-too-large"); reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::finished, this, &UpdateChecker::finish);
    deadline_.start(timeoutMs_);
}
void UpdateChecker::finish() {
    deadline_.stop();
    auto* reply = reply_;
    if (reply->isOpen()) body_ += reply->read(maxBody + 1 - body_.size());
    if (body_.size() > maxBody) failure_ = QStringLiteral("response-too-large");
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (failure_.isEmpty() && (reply->error() != QNetworkReply::NoError || status != 200))
        failure_ = QStringLiteral("http-%1-network-%2").arg(status).arg(int(reply->error()));
    reply_ = nullptr;
    reply->deleteLater();
    QString result;
    if (failure_.isEmpty()) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(body_, &error);
        const auto obj = document.object();
        const auto tag = obj.value(QStringLiteral("tag_name")).toString();
        const auto remote = versionNumber(tag, false), local = versionNumber(currentVersion_, true);
        const QUrl url(obj.value(QStringLiteral("html_url")).toString());
        const bool validUrl = url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com")
            && url.userInfo().isEmpty() && url.port(-1) == -1 && !url.hasQuery() && !url.hasFragment()
            && url.path() == QStringLiteral("/Likely7/Veyra-NRVideo/releases/tag/") + tag;
        if (error.error != QJsonParseError::NoError || !document.isObject()
            || !obj.value(QStringLiteral("draft")).isBool() || obj.value(QStringLiteral("draft")).toBool()
            || !obj.value(QStringLiteral("prerelease")).isBool() || obj.value(QStringLiteral("prerelease")).toBool()
            || local.segmentCount() != 3 || remote.segmentCount() != 3 || !validUrl) {
            failure_ = QStringLiteral("invalid-release");
        } else if (QVersionNumber::compare(remote, local) > 0) {
            latestVersion_ = remote.toString(); releaseUrl_ = url;
            emit releaseChanged();
            emit updateAvailable(latestVersion_);
            result = QStringLiteral("available");
        } else {
            latestVersion_.clear(); releaseUrl_.clear(); emit releaseChanged();
            if (manual_) emit notice(tr("当前已是最新版本"), false);
            result = QStringLiteral("current");
        }
    }
    if (!failure_.isEmpty() && usingMirror_) {
        qInfo().noquote() << "[update-check] mirror fallback" << failure_;
        request(endpoint_);
        return;
    }
    if (!failure_.isEmpty()) {
        if (manual_) emit notice(tr("检查更新失败，请检查网络后重试"), true);
        result = failure_;
    }
    body_.clear();
    qInfo().noquote() << "[update-check]" << result;
    emit checkingChanged();
    emit completed(result);
}
void UpdateChecker::openRelease() {
    if (!releaseUrl_.isEmpty()) QDesktopServices::openUrl(releaseUrl_);
}
}
