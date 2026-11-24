// SPDX-License-Identifier: GPL-3.0-only
#include "TranslationAssets.h"
#include "WinCaptionReader.h"
#include "veyra/Log.h"
#include "veyra/FileIdentity.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <windows.h>
#include <algorithm>
#include <array>
#include <string_view>
#include <format>
#include <utility>

namespace veyra::ui {
namespace {
constexpr ModelFile runtimeFiles[]{
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
    {"ggml-vulkan.dll", "345812d6e453bd23c9b12e8996babed0769c0eaeb916e29decdfbd35c8d4522d"},
    {"ggml.dll", "803f2b54b29e42d170f39b838e183ba86de7820eff6685f522a2cbacb4ed390b"},
    {"libomp.dll", "a12116ba72d1d6820407cf30be23da04ce79d6bb8a71a5ee71759c5a1faa6f1c"},
    {"LICENSE-LLVM-OpenMP", "fdad1758a9e1f9d5a81e18879b3406772115edc92c24bfa36b70c654f325e8e4"},
    {"llama-common.dll", "b65150c7c880ae189d4d27be3f5c150eaef3bf05cf9e7bb24314d2d1046fa230"},
    {"llama-server-impl.dll", "5fe61e4b18fb85ec19610b86be59dcc3b4c7e911966ff2587f9c9e7fcd0bdeb3"},
    {"llama-server.exe", "0852cfc8238376bc061335500246fb8319841a8fd03812d0eaa2a9019faef6a6"},
    {"llama.dll", "ad279db20507ae5771c4d208de0e897281ef78f66a9acf970d68781180776bf8"},
    {"mtmd.dll", "a697f9b7d106ad80b339d0408415f911973044d59bf9fefb8167264353687817"},
};
constexpr auto cpuRuntimeFiles = [] {
    std::array<ModelFile, std::size(runtimeFiles) - 1> files{};
    size_t index = 0;
    for (const auto& file : runtimeFiles)
        if (std::string_view(file.name) != "ggml-vulkan.dll") files[index++] = file;
    return files;
}();
constexpr ModelAsset legacyCpuRuntime{
    "llama-b12240", "https://github.com/ggml-org/llama.cpp/releases/download/b12240/llama-b12240-bin-win-cpu-x64.zip",
    24931979, "ecfd6e90619d44cdb417e073056e48a467086cdf756129e5d75fd50f14e424b6", cpuRuntimeFiles, "", 0, true};
constexpr ModelAsset translationAssets[]{
    {"llama-b12240-vulkan", "https://github.com/ggml-org/llama.cpp/releases/download/b12240/llama-b12240-bin-win-vulkan-x64.zip",
     37892170, "9ea8f113961f32024e0c34409432d3bd350590b040259520bb0b98aeba3a8ac7", runtimeFiles, "", 0, true},
    {"qwen2.5-1.5b-instruct-q4_k_m.gguf",
     "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf",
     1117320736, "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e"},
};

bool regularPath(const QString& path, bool directory) {
    const DWORD attributes = GetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && bool(attributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

bool verifyFile(const QString& path, qint64 bytes, const char* sha256, bool contents) {
    if (!regularPath(path, false) || (bytes >= 0 && QFileInfo(path).size() != bytes)) return false;
    if (!contents) return true;
    std::string hash;
    uint64_t hashedBytes = 0;
    return computeFileSha256(QDir::toNativeSeparators(path).toStdWString(), hash, hashedBytes)
        && (bytes < 0 || hashedBytes == uint64_t(bytes))
        && QByteArray::fromStdString(hash).toLower() == sha256;
}

bool verifyMembers(const QString& directory, const ModelAsset& asset, bool contents) {
    if (!regularPath(directory, true)) return false;
    const QDir dir(directory);
    if (!asset.allowAdditionalFiles &&
        dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).size()
        != qsizetype(asset.members.size())) return false;
    return std::ranges::all_of(asset.members, [&](const ModelFile& file) {
        const auto name = QString::fromLatin1(file.name);
        for (auto parent = name.lastIndexOf(u'/'); parent >= 0; parent = name.lastIndexOf(u'/', parent - 1)) {
            if (!parent || !regularPath(dir.filePath(name.left(parent)), true)) return false;
        }
        return verifyFile(dir.filePath(name), file.bytes, file.sha256, contents);
    });
}

std::unique_ptr<QLockFile> tryModelLock(const QString& root) {
    auto lock = std::make_unique<QLockFile>(QDir(root).filePath(QStringLiteral("download.lock")));
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) return {};
    return lock;
}
}

ModelAssets::ModelAssets(QString root, std::span<const ModelAsset> assets)
    : root_(std::move(root)), assets_(assets) {
    extract_.setStandardOutputFile(QProcess::nullDevice());
    extract_.setStandardErrorFile(QProcess::nullDevice());
    extract_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
    connect(&extract_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (downloading() && error == QProcess::FailedToStart) retry(tr("无法解压模型文件"));
    });
    connect(&extract_, &QProcess::finished, this, &ModelAssets::finishArchive);
    refreshInstalled();
}

ModelAssets::~ModelAssets() {
    clearReply();
    extract_.disconnect(this);
    if (extract_.state() != QProcess::NotRunning) {
        extract_.kill();
        extract_.waitForFinished(1500);
    }
}

bool ModelAssets::setAssets(std::span<const ModelAsset> assets) {
    if (downloading()) return false;
    assets_ = assets;
    status_.clear();
    refreshInstalled();
    return true;
}

QString ModelAssets::path(const QString& root, const ModelAsset& asset) {
    return QDir(root).filePath(QString::fromLatin1(asset.destination));
}
bool ModelAssets::verify(const QString& root, const ModelAsset& asset, bool contents) {
    const QString target = path(root, asset);
    if (!asset.members.empty()) return verifyMembers(target, asset, contents);
    if (verifyFile(target, asset.bytes, asset.sha256, contents)) return true;
    return std::ranges::any_of(asset.fallbacks, [&](const ModelSource& source) {
        return source.bytes >= 0 && source.sha256[0]
            && verifyFile(target, source.bytes, source.sha256, contents);
    });
}
void ModelAssets::refreshInstalled() {
    installed_ = std::ranges::all_of(assets_, [&](const ModelAsset& asset) { return verify(root_, asset, false); });
}
qint64 ModelAssets::totalBytes() const {
    qint64 result = 0;
    for (const auto& asset : assets_) result += asset.bytes;
    return result;
}

void ModelAssets::download() {
    if (downloading() || extract_.state() != QProcess::NotRunning) return;
    const QDir root(root_);
    if (!root.mkpath(QStringLiteral("."))) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    lock_ = tryModelLock(root_);
    if (!lock_) {
        finish(tr("另一个 Veyra 窗口正在下载模型"));
        return;
    }
    qint64 required = 0;
    for (const auto& asset : assets_) {
        if (verify(root_, asset, asset.verifyExisting)) continue;
        qint64 archiveBytes = asset.bytes;
        for (const auto& source : asset.fallbacks) archiveBytes = std::max(archiveBytes, source.bytes);
        required += archiveBytes;
        for (const auto& member : asset.members)
            required += member.bytes >= 0 ? member.bytes : asset.bytes / qint64(asset.members.size()) * 2;
    }
    const auto free = QStorageInfo(root_).bytesAvailable();
    if (free >= 0 && free < required) {
        finish(tr("磁盘可用空间不足，至少需要 %1 MB").arg((required + 999999) / 1000000));
        return;
    }
    stage_ = std::make_unique<QTemporaryDir>(root.filePath(QStringLiteral("download-XXXXXX")));
    if (!stage_->isValid()) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    cancelled_ = false;
    asset_ = 0;
    complete_ = received_ = 0;
    next();
}

void ModelAssets::next() {
    while (asset_ < assets_.size() && verify(root_, assets_[asset_], assets_[asset_].verifyExisting))
        complete_ += assets_[asset_++].bytes;
    if (asset_ == assets_.size()) {
        finish(tr("本地模型已就绪"));
        return;
    }
    source_ = 0;
    startSource();
}

const ModelSource* ModelAssets::fallback() const {
    return source_ ? &assets_[asset_].fallbacks[source_ - 1] : nullptr;
}

bool ModelAssets::downloadsMembers() const {
    const auto* alternate = fallback();
    return alternate && alternate->memberFiles;
}

void ModelAssets::startSource() {
    const auto& asset = assets_[asset_];
    const auto* alternate = fallback();
    transfer_ = std::make_unique<QTemporaryDir>(stage_->filePath(QStringLiteral("payload-XXXXXX")));
    if (!transfer_->isValid()) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    member_ = 0;
    verifiedBytes_ = 0;
    sourceBytes_ = alternate && alternate->bytes >= 0 ? alternate->bytes : asset.bytes;
    if (downloadsMembers()) {
        sourceBytes_ = 0;
        for (const auto& member : asset.members) sourceBytes_ += member.bytes;
    }
    received_ = complete_;
    startFile();
}

void ModelAssets::startFile() {
    const auto& asset = assets_[asset_];
    const auto* alternate = fallback();
    auto url = QString::fromLatin1(alternate ? alternate->url : asset.url);
    QString target;
    expectedBytes_ = sourceBytes_;
    expectedSha_ = alternate && alternate->sha256[0] ? alternate->sha256 : asset.sha256;
    if (downloadsMembers()) {
        const auto& member = asset.members[member_];
        url += QString::fromLatin1(member.name);
        target = transfer_->filePath(QStringLiteral("model/") + QString::fromLatin1(member.name));
        expectedBytes_ = member.bytes;
        expectedSha_ = member.sha256;
    } else if (asset.members.empty()) {
        target = path(root_, asset);
    } else {
        target = transfer_->filePath(QStringLiteral("archive"));
    }
    bytes_ = 0;
    hash_.reset();
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    file_ = std::make_unique<QSaveFile>(target);
    if (!file_->open(QIODevice::WriteOnly)) {
        finish(tr("无法保存模型下载文件"));
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(8);
    request.setTransferTimeout(asset.fallbacks.empty() ? 45000 : 25000);
    request.setRawHeader("Accept-Encoding", "identity");
    reply_ = network_.get(request);
    reply_->setReadBufferSize(1024 * 1024);
    connect(reply_, &QNetworkReply::readyRead, this, &ModelAssets::readFile);
    connect(reply_, &QNetworkReply::finished, this, &ModelAssets::finishFile);
    status_ = tr("正在下载模型与运行库…") + QStringLiteral(" · ") + request.url().host();
    emit changed();
}

void ModelAssets::readFile() {
    if (!reply_ || !file_) return;
    const auto data = reply_->readAll();
    if (data.size() > expectedBytes_ - bytes_) {
        retry(tr("下载文件的大小与预期不符"));
        return;
    }
    if (file_->write(data) != data.size()) {
        finish(tr("无法保存模型下载文件"));
        return;
    }
    hash_.addData(data);
    bytes_ += data.size();
    const auto total = complete_ + assets_[asset_].bytes * (verifiedBytes_ + bytes_) / std::max<qint64>(1, sourceBytes_);
    if (total / (1024 * 1024) != received_ / (1024 * 1024)) {
        received_ = total;
        emit changed();
    }
}

void ModelAssets::finishFile() {
    auto* completed = reply_;
    readFile();
    if (!completed || reply_ != completed) return;
    if (completed->error() != QNetworkReply::NoError) {
        log::warn("model-assets", std::format("download failed host={} http={} network={}",
            completed->request().url().host().toStdString(),
            completed->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), int(completed->error())));
        retry(tr("模型下载失败，请检查网络后重试"));
        return;
    }
    if (bytes_ != expectedBytes_ || hash_.result().toHex() != expectedSha_) {
        retry(tr("下载文件校验失败，请重新下载"));
        return;
    }
    clearReply();
    if (!file_->commit()) {
        finish(tr("无法保存模型下载文件"));
        return;
    }
    file_.reset();
    const auto& asset = assets_[asset_];
    if (downloadsMembers()) {
        verifiedBytes_ += expectedBytes_;
        if (++member_ < asset.members.size()) startFile();
        else installDirectory();
    } else if (asset.members.empty()) {
        transfer_.reset();
        complete_ += assets_[asset_++].bytes;
        next();
    } else {
        extractArchive();
    }
}

void ModelAssets::retry(QString reason) {
    if (source_ >= assets_[asset_].fallbacks.size()) {
        finish(std::move(reason));
        return;
    }
    clearReply();
    file_.reset();
    ++source_;
    log::info("model-assets", std::format("trying fallback {} for {}", source_, assets_[asset_].destination));
    startSource();
}

void ModelAssets::extractArchive() {
    const QString target = transfer_->filePath(QStringLiteral("model"));
    if (!QDir().mkpath(target)) {
        finish(tr("无法解压模型文件"));
        return;
    }
    const auto& asset = assets_[asset_];
    QStringList arguments{QStringLiteral("-xf"), transfer_->filePath(QStringLiteral("archive")),
                          QStringLiteral("-C"), target};
    if (asset.stripComponents)
        arguments << QStringLiteral("--strip-components") << QString::number(asset.stripComponents);
    for (const auto& member : asset.members)
        arguments << QString::fromLatin1(asset.archivePrefix) + QString::fromLatin1(member.name);
    received_ = complete_ + asset.bytes;
    status_ = tr("正在解压并验证模型…");
    extract_.start(windowsSystemExecutable(L"tar.exe"), arguments);
    emit changed();
}

void ModelAssets::finishArchive(int code, QProcess::ExitStatus exit) {
    if (!downloading() || !stage_) return;
    if (cancelled_) { finish(tr("模型下载已取消")); return; }
    if (exit != QProcess::NormalExit || code != 0) {
        retry(tr("解压后的模型文件校验失败"));
        return;
    }
    installDirectory();
}

void ModelAssets::installDirectory() {
    const auto& asset = assets_[asset_];
    const QString unpacked = transfer_->filePath(QStringLiteral("model"));
    if (!verifyMembers(unpacked, asset, asset.verifyExisting)) {
        retry(tr("解压后的模型文件校验失败"));
        return;
    }
    const QString previous = stage_->filePath(QStringLiteral("previous-%1").arg(asset_));
    const QString target = path(root_, asset);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        finish(tr("无法创建模型下载目录"));
        return;
    }
    const bool replacing = QFileInfo::exists(target);
    if (replacing && !QDir().rename(target, previous)) {
        finish(tr("模型文件正在使用，请稍后重试"));
        return;
    }
    if (!QDir().rename(unpacked, target)) {
        if (replacing) QDir().rename(previous, target);
        finish(tr("无法保存模型下载文件"));
        return;
    }
    transfer_.reset();
    complete_ += assets_[asset_++].bytes;
    next();
}

void ModelAssets::clearReply() {
    if (auto* reply = std::exchange(reply_, nullptr)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void ModelAssets::finish(QString status) {
    clearReply();
    file_.reset();
    transfer_.reset();
    stage_.reset();
    lock_.reset();
    received_ = 0;
    status_ = std::move(status);
    refreshInstalled();
    emit changed();
}

void ModelAssets::cancel() {
    if (!downloading()) return;
    if (extract_.state() != QProcess::NotRunning) {
        cancelled_ = true;
        extract_.kill();
    } else finish(tr("模型下载已取消"));
}

bool ModelAssets::remove(const ModelAsset& asset) {
    if (downloading() || asset.members.empty()) return false;
    auto lock = tryModelLock(root_);
    const QString storage = path(root_, asset);
    if (!lock || !regularPath(storage, true)) return false;
    // Atomically detach the model first. Delete only the known files.
    QTemporaryDir stage(QDir(root_).filePath(QStringLiteral("remove-XXXXXX")));
    if (!stage.isValid()) return false;
    const QString retired = stage.filePath(QStringLiteral("model"));
    if (!QDir().rename(storage, retired)) return false;
    QDir contents(retired);
    bool removed = true;
    for (const auto& member : asset.members) {
        const QString name = QString::fromLatin1(member.name);
        if (contents.exists(name) && !contents.remove(name)) removed = false;
    }
    if (!QDir().rmdir(retired)) {
        // Preserve user-added files and report failure if the directory could
        // not be removed. They never become part of a verified model.
        QDir().rename(retired, storage);
        stage.setAutoRemove(false);
        removed = false;
    }
    status_ = removed ? tr("识别模型已删除") : tr("无法完整删除模型文件");
    refreshInstalled();
    emit changed();
    return removed;
}

TranslationAssets::TranslationAssets(const QString& dataDirectory)
    : downloads(QDir(dataDirectory).filePath(QStringLiteral("live-translation")), translationAssets),
      root_(QDir(dataDirectory).filePath(QStringLiteral("live-translation"))) {
    refreshInstalled();
}
void TranslationAssets::refreshInstalled() {
    downloads.refreshInstalled();
    cpuInstalled_ = ModelAssets::verify(root_, legacyCpuRuntime, false) &&
                    ModelAssets::verify(root_, translationAssets[1], false);
}
QString TranslationAssets::runtimePath() const {
    return ModelAssets::path(root_, vulkanInstalled() ? translationAssets[0] : legacyCpuRuntime);
}
QString TranslationAssets::modelPath() const { return ModelAssets::path(root_, translationAssets[1]); }
bool TranslationAssets::verifyRuntime() const {
    return installed() && ModelAssets::verify(root_, vulkanInstalled() ? translationAssets[0] : legacyCpuRuntime, true);
}
} // namespace veyra::ui
