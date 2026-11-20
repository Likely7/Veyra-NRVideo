// SPDX-License-Identifier: GPL-3.0-only
#include "TranslationAssets.h"
#include "WinCaptionReader.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <windows.h>
#include <iterator>
#include <utility>

namespace veyra::ui {
namespace {
struct Asset {
    const char* name;
    const char* url;
    qint64 bytes;
    const char* sha256;
};
constexpr Asset runtime{
    "runtime.zip",
    "https://github.com/ggml-org/llama.cpp/releases/download/b12240/llama-b12240-bin-win-cpu-x64.zip",
    24931979,
    "ecfd6e90619d44cdb417e073056e48a467086cdf756129e5d75fd50f14e424b6"
};
constexpr Asset model{
    "qwen2.5-1.5b-instruct-q4_k_m.gguf",
    "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf",
    1117320736,
    "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e"
};

// Only the server and its CPU runtime are extracted from the pinned archive.
// DLLs are checked again before executing, including the CPU variants ggml loads.
constexpr struct { const char* name; const char* sha256; } runtimeFiles[]{
    {"ggml-base.dll", "c65ded825b8b9d462f02f3445d3ee8b7cf7ae5970de12e9d0b41b36ce2e78b08"},
    {"ggml-cpu-alderlake.dll", "9d5214cec94fae675b65f199e577bf4654abde25b3724ac123a3c217775315f7"},
    {"ggml-cpu-cannonlake.dll", "f881d42081b1e273f3db77f853fa1f2a3adfecfc2334b85e8b60429e746e1074"},
    {"ggml-cpu-cascadelake.dll", "c9df7f4afce140f7fd729b33a2bd50a04a66c08b2eecf7e8f84b4b29f2287b12"},
    {"ggml-cpu-cooperlake.dll", "855db9d7398c1557110ce5d089985464578ca1c411bb2ed9601d3d94fef9238c"},
    {"ggml-cpu-haswell.dll", "8ba18d3ee0e48c70fc6a5938849a4fa2fc76ea9f35354f2eef4e81ee5c956f11"},
    {"ggml-cpu-icelake.dll", "9036f4492ff203dff1b262ea9c396528f5f6b21e6f2e74cf627a75c63ef6847a"},
    {"ggml-cpu-ivybridge.dll", "e5dd48d9b7d8ef9d4b935d5b173abfbfd5d6f9835c42dbdfe22e3832bfebd627"},
    {"ggml-cpu-piledriver.dll", "8d26aa9483b2100d16ef5a1a36a7086ba7f16be9f91eed49c9435682d6cbc1b8"},
    {"ggml-cpu-sandybridge.dll", "78465e1110577b1895db788f7162a8416b3aeae1c932ba9173af3fed071340e0"},
    {"ggml-cpu-sapphirerapids.dll", "0332884106e73a02958c9177c36c0af406eaea3ef68370af24c998629ee6ea36"},
    {"ggml-cpu-skylakex.dll", "a3ff9b4754d7633f7d276729598372d20d31949699628d9cf543b3656fdf87fe"},
    {"ggml-cpu-sse42.dll", "0151293ddf02869f9e7632f9598745a88d1df07ed9f85fcba4d6631882473e89"},
    {"ggml-cpu-x64.dll", "c06523f6820f6db09fc5e3d23e8c0e7d8bc5f3e2065207928672f38b57469e83"},
    {"ggml-cpu-zen4.dll", "95b38de9b7a2bf2a1e1f875a4bf6229964d4ad04b2723c51da9d86aedc5659ea"},
    {"ggml.dll", "803f2b54b29e42d170f39b838e183ba86de7820eff6685f522a2cbacb4ed390b"},
    {"libomp.dll", "a12116ba72d1d6820407cf30be23da04ce79d6bb8a71a5ee71759c5a1faa6f1c"},
    {"LICENSE-LLVM-OpenMP", "fdad1758a9e1f9d5a81e18879b3406772115edc92c24bfa36b70c654f325e8e4"},
    {"llama-common.dll", "b65150c7c880ae189d4d27be3f5c150eaef3bf05cf9e7bb24314d2d1046fa230"},
    {"llama-server-impl.dll", "5fe61e4b18fb85ec19610b86be59dcc3b4c7e911966ff2587f9c9e7fcd0bdeb3"},
    {"llama-server.exe", "0852cfc8238376bc061335500246fb8319841a8fd03812d0eaa2a9019faef6a6"},
    {"llama.dll", "ad279db20507ae5771c4d208de0e897281ef78f66a9acf970d68781180776bf8"},
    {"mtmd.dll", "a697f9b7d106ad80b339d0408415f911973044d59bf9fefb8167264353687817"},
};
bool verifyFiles(const QString& folder) {
    const QDir dir(folder);
    if (dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).size()
        != qsizetype(std::size(runtimeFiles))) return false;
    for (const auto& file : runtimeFiles) {
        const QString path = dir.filePath(QString::fromLatin1(file.name));
        if (QFileInfo(path).isSymLink()) return false;
        QFile input(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input) || hash.result().toHex() != file.sha256)
            return false;
    }
    return true;
}
}

TranslationAssets::TranslationAssets(const QString& dataDirectory, QObject* parent)
    : QObject(parent), root_(QDir(dataDirectory).filePath(QStringLiteral("live-translation"))) {
    extract_.setStandardOutputFile(QProcess::nullDevice());
    extract_.setStandardErrorFile(QProcess::nullDevice());
    extract_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
    connect(&extract_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (downloading() && error == QProcess::FailedToStart)
            finish(tr("无法解压翻译运行库"));
    });
    connect(&extract_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        if (!downloading() || !stage_) return;
        if (cancelled_) { finish(tr("模型下载已取消")); return; }
        const QString unpacked = stage_->filePath(QStringLiteral("runtime"));
        if (exit != QProcess::NormalExit || code != 0 || !verifyFiles(unpacked)) {
            finish(tr("翻译运行库校验失败"));
            return;
        }
        const QString previous = stage_->filePath(QStringLiteral("previous"));
        const QString target = runtimePath();
        const bool replacing = QFileInfo::exists(target);
        if (replacing && !QDir().rename(target, previous)) {
            finish(tr("翻译运行库正在使用，请稍后重试"));
            return;
        }
        if (!QDir().rename(unpacked, target)) {
            if (replacing) QDir().rename(previous, target);
            finish(tr("无法保存翻译运行库"));
            return;
        }
        next();
    });
}

TranslationAssets::~TranslationAssets() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    extract_.disconnect(this);
    if (extract_.state() != QProcess::NotRunning) {
        extract_.kill();
        extract_.waitForFinished(1500);
    }
}

QString TranslationAssets::runtimePath() const { return QDir(root_).filePath(QStringLiteral("llama-b12240")); }
QString TranslationAssets::modelPath() const { return QDir(root_).filePath(QString::fromLatin1(model.name)); }
bool TranslationAssets::installed() const {
    return QFileInfo(modelPath()).size() == model.bytes
        && QFileInfo::exists(QDir(runtimePath()).filePath(QStringLiteral("llama-server.exe")));
}
bool TranslationAssets::verifyRuntime() const { return installed() && verifyFiles(runtimePath()); }
qint64 TranslationAssets::totalBytes() const { return runtime.bytes + model.bytes; }

void TranslationAssets::download() {
    if (downloading() || extract_.state() != QProcess::NotRunning) return;
    QDir dir(root_);
    if (!dir.mkpath(QStringLiteral("."))) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    auto lock = std::make_unique<QLockFile>(dir.filePath(QStringLiteral("download.lock")));
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) {
        finish(tr("另一个 Veyra 窗口正在下载模型"));
        return;
    }
    lock_ = std::move(lock);
    cancelled_ = false;
    const auto free = QStorageInfo(root_).bytesAvailable();
    if (free >= 0 && free < model.bytes + 3 * runtime.bytes && !installed()) {
        finish(tr("磁盘可用空间不足，下载需要约 1.2 GB"));
        return;
    }
    received_ = 0;
    stage_ = std::make_unique<QTemporaryDir>(dir.filePath(QStringLiteral("download-XXXXXX")));
    if (!stage_->isValid()) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    next();
}

void TranslationAssets::next() {
    if (!verifyFiles(runtimePath())) {
        startFile(true);
    } else if (QFileInfo(modelPath()).size() != model.bytes) {
        startFile(false);
    } else {
        finish(tr("本地模型已就绪"));
    }
}

void TranslationAssets::startFile(bool isRuntime) {
    runtimeFile_ = isRuntime;
    bytes_ = 0;
    received_ = runtimeFile_ ? 0 : runtime.bytes;
    hash_.reset();
    const Asset& asset = isRuntime ? runtime : model;
    file_ = std::make_unique<QSaveFile>(isRuntime ? stage_->filePath(QString::fromLatin1(runtime.name)) : modelPath());
    if (!file_->open(QIODevice::WriteOnly)) {
        finish(tr("无法保存模型下载文件"));
        return;
    }
    QNetworkRequest request(QUrl(QString::fromLatin1(asset.url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(8);
    request.setTransferTimeout(45000);
    request.setRawHeader("Accept-Encoding", "identity");
    reply_ = network_.get(request);
    reply_->setReadBufferSize(1024 * 1024);
    connect(reply_, &QNetworkReply::readyRead, this, &TranslationAssets::readFile);
    connect(reply_, &QNetworkReply::finished, this, &TranslationAssets::finishFile);
    status_ = isRuntime ? tr("正在下载本地推理运行库…") : tr("正在下载翻译模型…");
    emit changed();
}

void TranslationAssets::readFile() {
    if (!reply_ || !file_) return;
    const auto received = reply_->readAll();
    const Asset& asset = runtimeFile_ ? runtime : model;
    if (received.size() > asset.bytes - bytes_) {
        finish(tr("下载文件的大小与预期不符"));
        return;
    }
    if (file_->write(received) != received.size()) {
        finish(tr("无法保存模型下载文件"));
        return;
    }
    hash_.addData(received);
    bytes_ += received.size();
    const auto total = bytes_ + (runtimeFile_ ? 0 : runtime.bytes);
    if (total / (1024 * 1024) != received_ / (1024 * 1024)) {
        received_ = total;
        emit changed();
    }
}

void TranslationAssets::finishFile() {
    readFile();
    if (!reply_) return;
    if (reply_->error() != QNetworkReply::NoError) {
        finish(tr("模型下载失败，请检查网络后重试"));
        return;
    }
    reply_->deleteLater();
    reply_ = nullptr;
    const Asset& asset = runtimeFile_ ? runtime : model;
    if (bytes_ != asset.bytes || hash_.result().toHex() != asset.sha256) {
        finish(tr("下载文件校验失败，请重新下载"));
        return;
    }
    if (!file_->commit()) {
        finish(tr("无法保存模型下载文件"));
        return;
    }
    file_.reset();
    if (runtimeFile_) extractRuntime();
    else finish(tr("本地模型已就绪"));
}

void TranslationAssets::extractRuntime() {
    const QString target = stage_->filePath(QStringLiteral("runtime"));
    if (!QDir().mkpath(target)) {
        finish(tr("无法解压翻译运行库"));
        return;
    }
    QStringList arguments{QStringLiteral("-xf"), stage_->filePath(QString::fromLatin1(runtime.name)),
                          QStringLiteral("-C"), target};
    for (const auto& file : runtimeFiles) arguments << QString::fromLatin1(file.name);
    status_ = tr("正在验证翻译运行库…");
    emit changed();
    extract_.start(windowsSystemExecutable(L"tar.exe"), arguments);
}

void TranslationAssets::finish(QString status) {
    if (reply_) {
        auto* reply = std::exchange(reply_, nullptr);
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    file_.reset();
    stage_.reset();
    lock_.reset();
    received_ = 0;
    status_ = std::move(status);
    emit changed();
}

void TranslationAssets::cancel() {
    if (!downloading()) return;
    if (extract_.state() != QProcess::NotRunning) {
        // Clean up after the extractor closes its files.
        cancelled_ = true;
        extract_.kill();
    } else {
        finish(tr("模型下载已取消"));
    }
}
} // namespace veyra::ui
