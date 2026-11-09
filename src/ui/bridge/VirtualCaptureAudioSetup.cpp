#include "VirtualCaptureAudioSetup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <windows.h>
#include <shellapi.h>

namespace veyra::ui {
namespace {
constexpr qsizetype downloadLimit = 2 * 1024 * 1024;
constexpr auto packageUrl = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip";
constexpr auto packageHash = "bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90";
constexpr auto installerHash = "734c35dfa6d98f48782a451633ceb471166ec70d60482fd89a1123d0ee3c4f41";

QString systemProgram(const QString& name) {
    wchar_t system[MAX_PATH]{};
    const auto size = GetSystemDirectoryW(system, MAX_PATH);
    return size && size < MAX_PATH ? QDir(QString::fromWCharArray(system, int(size))).filePath(name) : QString();
}
}

VirtualCaptureAudioSetup::VirtualCaptureAudioSetup(QObject* parent) : QObject(parent) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (reply_) reply_->abort();
        else if (extractor_.state() != QProcess::NotRunning) extractor_.kill();
    });
    connect(&extractor_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(tr("无法启动 Windows 解压工具"), true);
    });
    connect(&extractor_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (code == 0 && status == QProcess::NormalExit) launchInstaller();
        else finish(tr("虚拟扬声器安装包解压失败"), true);
    });
    connect(&installer_, &QWinEventNotifier::activated, this, [this] {
        DWORD code = 0;
        const auto process = installer_.handle();
        const bool read = GetExitCodeProcess(process, &code);
        if (!read) code = GetLastError();
        installer_.setEnabled(false);
        installer_.setHandle(nullptr);
        CloseHandle(process);
        const bool ok = read && (code == 0 || code == ERROR_SUCCESS_REBOOT_REQUIRED);
        finish(ok ? tr("安装器已关闭。若已完成安装或卸载，请按 VB-Audio 提示重启 Windows")
                  : tr("VB-Audio 安装器未完成操作（%1）").arg(code), !ok);
    });
}

VirtualCaptureAudioSetup::~VirtualCaptureAudioSetup() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    extractor_.disconnect(this);
    if (extractor_.state() != QProcess::NotRunning) {
        extractor_.kill();
        extractor_.waitForFinished(1000);
    }
    if (const auto process = installer_.handle()) {
        installer_.setEnabled(false);
        installer_.setHandle(nullptr);
        // The elevated installer may still be reading its driver files.
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT && staging_) staging_->setAutoRemove(false);
        CloseHandle(process);
    }
}

void VirtualCaptureAudioSetup::start(const QString& dataDirectory) {
    if (busy()) return;
    if (!QDir().mkpath(dataDirectory)) {
        emit completed(tr("无法创建虚拟扬声器安装目录"), true);
        return;
    }
    staging_ = std::make_unique<QTemporaryDir>(QDir(dataDirectory).filePath(QStringLiteral("vb-cable-XXXXXX")));
    if (!staging_->isValid()) {
        finish(tr("无法创建虚拟扬声器安装目录"), true);
        return;
    }
    QNetworkRequest request{QUrl(QString::fromLatin1(packageUrl))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("User-Agent", "Veyra-Virtual-Audio-Setup");
    reply_ = network_.get(request);
    reply_->setReadBufferSize(downloadLimit + 1);
    connect(reply_, &QIODevice::readyRead, this, [this] {
        download_ += reply_->read(downloadLimit + 1 - download_.size());
        if (download_.size() > downloadLimit) reply_->abort();
    });
    connect(reply_, &QNetworkReply::finished, this, &VirtualCaptureAudioSetup::downloadFinished);
    deadline_.start(30000);
    emit busyChanged();
}

void VirtualCaptureAudioSetup::downloadFinished() {
    deadline_.stop();
    auto* reply = reply_;
    reply_ = nullptr;
    if (reply->isOpen() && download_.size() <= downloadLimit)
        download_ += reply->read(downloadLimit + 1 - download_.size());
    const bool ok = reply->error() == QNetworkReply::NoError &&
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
    reply->deleteLater();
    if (!ok) {
        finish(tr("下载 VB-Audio 安装包失败，请检查网络后重试"), true);
        return;
    }
    if (QCryptographicHash::hash(download_, QCryptographicHash::Sha256).toHex() != packageHash) {
        finish(tr("VB-Audio 安装包校验失败，请从官网获取"), true);
        return;
    }
    const auto archive = staging_->filePath(QStringLiteral("driver.zip"));
    QFile file(archive);
    if (!file.open(QIODevice::WriteOnly) || file.write(download_) != download_.size() || !file.flush()) {
        file.close();
        finish(tr("无法保存虚拟扬声器安装包"), true);
        return;
    }
    file.close();
    download_.clear();
    const auto tar = systemProgram(QStringLiteral("tar.exe"));
    if (tar.isEmpty()) {
        finish(tr("找不到 Windows 解压工具"), true);
        return;
    }
    deadline_.start(15000);
    extractor_.start(tar, {QStringLiteral("-xf"), archive, QStringLiteral("-C"), staging_->path()});
}

void VirtualCaptureAudioSetup::launchInstaller() {
    deadline_.stop();
    const auto setup = staging_->filePath(QStringLiteral("VBCABLE_Setup_x64.exe"));
    QFile file(setup);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file) || hash.result().toHex() != installerHash) {
        file.close();
        finish(tr("VB-Audio 安装器校验失败"), true);
        return;
    }
    file.close();
    const auto executable = QDir::toNativeSeparators(setup).toStdWString();
    const auto directory = QDir::toNativeSeparators(staging_->path()).toStdWString();
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    // The official setup manifest requests administrator rights. Windows
    // presents its normal authorization prompt when opening this exact file.
    request.lpFile = executable.c_str();
    request.lpDirectory = directory.c_str();
    request.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&request) || !request.hProcess) {
        const DWORD error = GetLastError();
        finish(error == ERROR_CANCELLED ? tr("已取消管理员授权")
                                       : tr("无法打开 VB-Audio 安装器（%1）").arg(error), error != ERROR_CANCELLED);
        return;
    }
    installer_.setHandle(request.hProcess);
    installer_.setEnabled(true);
}

void VirtualCaptureAudioSetup::finish(const QString& message, bool warning) {
    deadline_.stop();
    download_.clear();
    staging_.reset();
    emit busyChanged();
    emit completed(message, warning);
}

bool VirtualCaptureAudioSetup::openRecordingSettings() {
    const auto control = systemProgram(QStringLiteral("control.exe"));
    return !control.isEmpty() && QProcess::startDetached(control, {QStringLiteral("mmsys.cpl,,1")});
}
}
