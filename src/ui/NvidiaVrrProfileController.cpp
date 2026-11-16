#include "veyra/ui/NvidiaVrrProfileController.h"
#include "veyra/Log.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QThread>
#include <memory>
#include <optional>

namespace veyra::ui {
namespace {
using gfx::VrrProfileSnapshot;
QJsonArray values(const std::array<gfx::VrrProfileValue, 2>& items) {
    QJsonArray array;
    for (const auto& v : items) array.append(QJsonObject{{"value", int(v.value)}, {"local", v.local}});
    return array;
}
QByteArray encode(const VrrProfileSnapshot& state) {
    return QJsonDocument(QJsonObject{{"version", 1}, {"executable", QString::fromStdWString(state.executable)},
        {"profile", QString::fromStdWString(state.profileName)}, {"exists", state.exists},
        {"settingCount", qint64(state.settingCount)}, {"settings", values(state.settings)}, {"global", values(state.global)}}).toJson();
}
struct Backup { bool exists = false; std::optional<VrrProfileSnapshot> state; };
Backup readBackup(const QString& path, const QString& executable) {
    Backup result; result.exists = QFileInfo::exists(path);
    if (!result.exists) return result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32768) return result;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || object["version"].toInt(-1) != 1
        || object["executable"].toString() != executable || !object["exists"].isBool()
        || !object["profile"].isString() || object["profile"].toString().isEmpty()
        || object["profile"].toString().size() >= 2048) return result;
    const double count = object["settingCount"].toDouble(-1);
    if (count < 0 || count > UINT32_MAX || count != double(uint32_t(count))) return result;
    VrrProfileSnapshot before;
    before.executable = executable.toStdWString(); before.profileName = object["profile"].toString().toStdWString();
    before.exists = object["exists"].toBool(); before.isolated = true; before.settingCount = uint32_t(count);
    for (const auto* key : {"settings", "global"}) {
        const auto array = object[key].toArray();
        if (array.size() != 2) return result;
        auto& target = key[0] == 's' ? before.settings : before.global;
        for (int k = 0; k < 2; ++k) {
            const auto value = array[k].toObject(); const int number = value["value"].toInt(-1);
            if (number < 0 || number > 2 || value["value"].toDouble(-1) != number || !value["local"].isBool()) return result;
            target[size_t(k)] = {uint32_t(number), value["local"].toBool()};
        }
    }
    if (!before.exists && (before.settingCount || before.settings[0].local || before.settings[1].local)) return result;
    result.state = std::move(before);
    return result;
}
bool saveBackup(const QString& path, const VrrProfileSnapshot& state) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto data = encode(state);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
struct Outcome {
    gfx::VrrProfileResult read, mutation;
    bool inspected = false, didMutate = false, conflict = false, storageError = false, canRestore = false;
};
} // namespace

NvidiaVrrProfileController::NvidiaVrrProfileController(const std::filesystem::path& dataDirectory, QObject* parent)
    : QObject(parent) {
    executable_ = QDir::toNativeSeparators(QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath()).toLower();
    const auto key = QString::fromLatin1(QCryptographicHash::hash(executable_.toUtf8(), QCryptographicHash::Sha256).toHex());
    backupPath_ = QDir(QString::fromStdWString(dataDirectory.wstring())).filePath(QStringLiteral("driver-profiles/vrr-%1.json").arg(key));
}
QString NvidiaVrrProfileController::summary() const {
    if (busy_) return tr("正在处理驱动配置…");
    if (!checked_) return tr("尚未检查");
    if (storageError_) return tr("无法保存或清理恢复记录");
    if (error_ == gfx::VrrProfileError::Busy) return tr("驱动配置正在被占用");
    if (driverStatus_ == -175) return tr("需要管理员权限");
    if (error_ != gfx::VrrProfileError::None) return tr("未能确认驱动配置");
    if (backupConflict_) return tr("恢复记录与当前配置不一致");
    if (!state_.isolated) return tr("当前配置无法单独修改");
    return state_.configured() ? tr("已允许窗口和全屏") : tr("尚未配置窗口和全屏");
}
QString NvidiaVrrProfileController::detail() const {
    if (error_ == gfx::VrrProfileError::Busy) return tr("另一个软件实例正在处理驱动配置，请稍后重试。");
    if (driverStatus_ == -175) return tr("请以管理员身份启动本软件后重试；完成配置后可恢复普通方式启动。");
    if (error_ == gfx::VrrProfileError::Unavailable) return tr("无法读取 NVIDIA 驱动配置；请检查显卡和驱动是否支持此功能。");
    if (error_ == gfx::VrrProfileError::Changed || backupConflict_)
        return tr("配置或恢复记录发生变化，已停止本次操作。请重新检查，必要时使用 Profile Inspector 手动处理。");
    if (error_ == gfx::VrrProfileError::UnsafeProfile || (readable_ && !state_.isolated))
        return tr("配置未独立绑定当前程序路径，可能由其他程序共用。请使用 Profile Inspector 单独配置本软件。");
    if (storageError_) return tr("请检查软件数据目录是否可写。恢复记录保留在该目录的 driver-profiles 文件夹中。");
    if (error_ != gfx::VrrProfileError::None)
        return tr("操作结果尚未确认，请重新检查。已有恢复记录会保留，不会自动重试写入。");
    if (restartRequired_) return tr("驱动配置已保存，请手动重启本软件。是否实际生效仍以驱动和显示器报告为准。");
    return tr("仅为当前软件路径配置窗口和全屏 G-SYNC，可恢复原设置；显示器和驱动的 G-SYNC 总开关仍需开启。");
}
void NvidiaVrrProfileController::check() { run(Operation::Check); }
void NvidiaVrrProfileController::repair() { if (canRepair()) run(Operation::Repair); }
void NvidiaVrrProfileController::restore() { if (canRestore()) run(Operation::Restore); }
void NvidiaVrrProfileController::run(Operation operation) {
    if (busy_) return;
    busy_ = true; emit changed();
    const auto executable = executable_, path = backupPath_;
    auto result = std::make_shared<Outcome>();
    // No HWND, graphics device, focus change, automatic restart or per-frame
    // callback. A stalled driver call cannot hold the GUI/render thread.
    auto* worker = QThread::create([executable, path, operation, result] {
        try {
            // Serialize the backup/write pair across player instances too.
            std::unique_ptr<QLockFile> lock;
            if (operation != Operation::Check) {
                if (!QDir().mkpath(QFileInfo(path).absolutePath())) { result->storageError = true; return; }
                lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
                lock->setStaleLockTime(0);
                if (!lock->tryLock(0)) { result->read.error = gfx::VrrProfileError::Busy; return; }
            }
            auto backup = readBackup(path, executable);
            result->read = gfx::inspectVrrProfile(executable.toStdWString());
            result->inspected = true;
            if (!result->read) return;
            const auto before = result->read.snapshot;
            result->conflict = backup.exists && (!backup.state || backup.state->executable != before.executable);
            if (operation == Operation::Repair && !before.configured()) {
                // Preserve the FIRST original values across checks/retries,
                // including a failed write after the backup succeeded.
                if (backup.exists && (!backup.state || *backup.state != before)) result->conflict = true;
                if (result->conflict) return;
                if (!gfx::planVrrProfileRepair(before).allowed) {
                    result->read.error = gfx::VrrProfileError::UnsafeProfile; return;
                }
                if (!backup.exists && !saveBackup(path, before)) { result->storageError = true; return; }
                result->mutation = gfx::repairVrrProfile(before); result->didMutate = true;
                result->read = gfx::inspectVrrProfile(executable.toStdWString());
            } else if (operation == Operation::Restore) {
                if (!backup.state) { result->conflict = true; return; }
                result->mutation = gfx::restoreVrrProfile(*backup.state, before); result->didMutate = true;
                if (result->mutation && !QFile::remove(path)) result->storageError = true;
                result->read = gfx::inspectVrrProfile(executable.toStdWString());
            }
            backup = readBackup(path, executable);
            if (backup.state && result->read) {
                const auto plan = gfx::planVrrProfileRestore(*backup.state, result->read.snapshot);
                // An already-restored record still needs a cleanup action,
                // e.g. after its removal failed or a repair never saved.
                result->canRestore = plan.allowed;
                result->conflict = !plan.allowed && *backup.state != result->read.snapshot;
            }
        } catch (...) { result->read.error = gfx::VrrProfileError::Driver; }
    });
    connect(worker, &QThread::finished, this, [this, result, operation] {
        checked_ = true; readable_ = result->inspected && bool(result->read); state_ = result->read.snapshot;
        const auto& status = result->didMutate && !result->mutation ? result->mutation : result->read;
        error_ = status.error; driverStatus_ = status.driverStatus;
        backupConflict_ = result->conflict; storageError_ = result->storageError;
        restoreAvailable_ = result->canRestore;
        if (result->didMutate && result->mutation && result->mutation.changed) restartRequired_ = true;
        busy_ = false; emit changed();
        log::info("vrr-profile", "operation=" + std::to_string(int(operation)) + " status=" + std::to_string(driverStatus_)
            + " error=" + std::to_string(int(error_)) + " changed=" + std::to_string(result->mutation.changed));
        if (operation != Operation::Check) emit notice(summary() + QStringLiteral("。") + detail(),
            error_ != gfx::VrrProfileError::None || storageError_ || backupConflict_);
    });
    // Value copies only; destroying the controller cancels its completion
    // callback without waiting for a vendor call at shutdown.
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
} // namespace veyra::ui
