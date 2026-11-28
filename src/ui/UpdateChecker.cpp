#include "veyra/ui/UpdateChecker.h"
#include "veyra/base/PortableInstallPolicy.h"
#include "veyra/base/PortableLayout.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDebug>

namespace veyra::ui {
namespace {
constexpr qsizetype maxBody = 128 * 1024;
constexpr int maxAssets = 64;

QString canonical(const std::array<int, 3>& numbers) {
    return QString::number(numbers[0]) + QLatin1Char('.') + QString::number(numbers[1])
        + QLatin1Char('.') + QString::number(numbers[2]);
}

bool rateLimited(const QNetworkReply* reply, int status, const QByteArray& body) {
    if (status == 429) return true;
    if (status != 403 && status != 429) return false;
    if (reply->rawHeader("x-ratelimit-remaining") == "0") return true;
    if (!reply->rawHeader("retry-after").isEmpty()) return true;
    return body.toLower().contains("rate limit");
}

// A name that is not a single published file name is ignored. A legal name whose
// digest is present and not a lowercase sha256 still rejects the whole document.
// The signed manifest, not this list, is what allows a zip to be installed.
bool readAssets(const QJsonObject& release, bool& enumerated, QHash<QString, QByteArray>& digests) {
    enumerated = release.contains(QStringLiteral("assets"));
    digests.clear();
    if (!enumerated) return true;
    const auto assets = release.value(QStringLiteral("assets"));
    if (!assets.isArray()) return false;
    const auto rows = assets.toArray();
    if (rows.size() > maxAssets) return false;
    static const QRegularExpression digest(QStringLiteral("^sha256:[0-9a-f]{64}$"));
    for (const auto& row : rows) {
        if (!row.isObject()) return false;
        const auto item = row.toObject();
        const auto assetName = item.value(QStringLiteral("name")).toString();
        if (!veyra::portable::acceptableGitHubAssetName(assetName.toStdString())) continue;
        if (digests.contains(assetName)) return false;
        QByteArray hash;
        if (item.contains(QStringLiteral("digest")) && !item.value(QStringLiteral("digest")).isNull()) {
            const auto text = item.value(QStringLiteral("digest")).toString();
            if (!digest.match(text).hasMatch()) return false;
            hash = QByteArray::fromHex(text.mid(7).toLatin1());
        }
        digests.insert(assetName, hash);
    }
    return true;
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
    outcome_.clear();
    statusText_.clear();
    request(endpoint_ == veyra::portable::releaseApiUrl() ? veyra::portable::releaseApiUrl(route_) : endpoint_, route_);
    emit checkingChanged();
    emit statusChanged();
}
void UpdateChecker::request(const QUrl& url, veyra::portable::DownloadRoute redirectRoute) {
    usingMirror_ = url != endpoint_;
    redirectRoute_ = redirectRoute;
    body_.clear(); failure_.clear();
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Veyra-Update-Check");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // Redirects stay on the official API unless the user selected a mirror.
    // A mirror fallback request passes the GitHub route and cannot leave it.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(timeoutMs_);
    reply_ = network_.get(request);
    qInfo().noquote() << "[update-check-start]" << (manual_ ? "manual" : "automatic") << url.host();
    reply_->setReadBufferSize(maxBody + 1);
    connect(reply_, &QNetworkReply::redirected, this, [this](const QUrl& target) {
        if (veyra::portable::allowedApiRedirect(target, redirectRoute_)) reply_->redirectAllowed();
        else { failure_ = QStringLiteral("redirect-rejected"); reply_->abort(); }
    });
    connect(reply_, &QIODevice::readyRead, this, [this] {
        body_ += reply_->read(maxBody + 1 - body_.size());
        if (body_.size() > maxBody) { failure_ = QStringLiteral("response-too-large"); reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::finished, this, &UpdateChecker::finish);
    deadline_.start(timeoutMs_);
}
void UpdateChecker::publish(const QString& result, const QString& status, bool errorNotice) {
    outcome_ = result;
    statusText_ = status;
    if (!failure_.isEmpty()) {
        latestVersion_.clear();
        releaseUrl_.clear();
        notesSummary_.clear();
        portable_.setReleaseInventory(false, {});
    }
    body_.clear();
    qInfo().noquote() << "[update-check]" << result;
    emit releaseChanged();
    emit statusChanged();
    emit checkingChanged();
    if (manual_ && result != QLatin1String("available") && !status.isEmpty()) emit notice(status, errorNotice);
    emit completed(result);
}
void UpdateChecker::finish() {
    deadline_.stop();
    auto* reply = reply_;
    if (reply->isOpen()) body_ += reply->read(maxBody + 1 - body_.size());
    if (body_.size() > maxBody) failure_ = QStringLiteral("response-too-large");
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // /releases/latest is one object. A list would be a page of releases, and the
    // first item is not "latest". The Link header is not followed.
    if (failure_.isEmpty() && status >= 300 && status < 400) failure_ = QStringLiteral("redirect-rejected");
    if (failure_.isEmpty() && (reply->error() != QNetworkReply::NoError || status != 200)) {
        if (rateLimited(reply, status, body_)) failure_ = QStringLiteral("rate-limited");
        else if (status == 0) failure_ = QStringLiteral("network");
        else failure_ = QStringLiteral("http-%1-network-%2").arg(status).arg(int(reply->error()));
    }
    reply_ = nullptr;
    reply->deleteLater();
    if (failure_.isEmpty()) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(body_, &error);
        const auto obj = document.object();
        const auto tag = obj.value(QStringLiteral("tag_name")).toString();
        const auto compared = veyra::portable::compareRelease(currentVersion_.toStdString(), tag.toStdString());
        const auto remote = veyra::portable::releaseNumbers(tag.toStdString(), false);
        const QUrl url(obj.value(QStringLiteral("html_url")).toString());
        const bool validUrl = url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com")
            && url.userInfo().isEmpty() && url.port(-1) == -1 && !url.hasQuery() && !url.hasFragment()
            && url.path() == QStringLiteral("/Likely7/Veyra-NRVideo/releases/tag/") + tag;
        bool enumerated = false;
        QHash<QString, QByteArray> digests;
        const bool assets = error.error == QJsonParseError::NoError && document.isObject() && readAssets(obj, enumerated, digests);
        if (error.error != QJsonParseError::NoError || !document.isObject() || document.isArray()
            || !obj.value(QStringLiteral("draft")).isBool() || obj.value(QStringLiteral("draft")).toBool()
            || !obj.value(QStringLiteral("prerelease")).isBool() || obj.value(QStringLiteral("prerelease")).toBool()
            || !compared || !remote || !validUrl || !assets) {
            failure_ = QStringLiteral("invalid-release");
        } else if (*compared > 0) {
            latestVersion_ = canonical(*remote);
            releaseUrl_ = url;
            notesSummary_ = veyra::portable::sanitizeReleaseNotes(obj.value(QStringLiteral("body")).toString());
            portable_.setReleaseInventory(enumerated, digests);
            const auto text = tr("可更新：当前 %1，最新 %2").arg(currentVersion_, latestVersion_);
            outcome_ = QStringLiteral("available");
            statusText_ = text;
            body_.clear();
            qInfo().noquote() << "[update-check]" << outcome_;
            emit releaseChanged();
            emit statusChanged();
            emit updateAvailable(latestVersion_);
            emit checkingChanged();
            emit completed(outcome_);
            return;
        } else {
            latestVersion_.clear();
            releaseUrl_.clear();
            notesSummary_.clear();
            portable_.setReleaseInventory(false, {});
            publish(QStringLiteral("current"), tr("当前已是最新版本（%1）").arg(currentVersion_), false);
            return;
        }
    }
    if (!failure_.isEmpty() && usingMirror_) {
        qInfo().noquote() << "[update-check] mirror fallback" << failure_;
        request(endpoint_, veyra::portable::DownloadRoute::GitHub);
        return;
    }
    QString message = tr("检查更新失败，请检查网络后重试");
    if (failure_ == QLatin1String("timeout")) message = tr("检查更新超时，请稍后重试");
    else if (failure_ == QLatin1String("network")) message = tr("无法连接更新服务器，请检查网络后重试");
    else if (failure_ == QLatin1String("rate-limited")) message = tr("GitHub 暂时限制了更新检查，请稍后再试");
    else if (failure_ == QLatin1String("redirect-rejected") || failure_ == QLatin1String("invalid-release")
             || failure_ == QLatin1String("response-too-large"))
        message = tr("发布信息无法通过校验，已拒绝这次更新");
    publish(failure_, message, true);
}
void UpdateChecker::openRelease() {
    if (!releaseUrl_.isEmpty()) QDesktopServices::openUrl(releaseUrl_);
}
}
