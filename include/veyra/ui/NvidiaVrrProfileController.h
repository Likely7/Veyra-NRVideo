#pragma once
#include "veyra/gfx/NvidiaVrrProfile.h"
#include <QObject>
#include <QString>
#include <filesystem>

namespace veyra::ui {
// Explicit, one-shot setup. Independent of the VRR presentation switch;
// never called from a render thread or a polling timer.
class NvidiaVrrProfileController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canRepair READ canRepair NOTIFY changed)
    Q_PROPERTY(bool canRestore READ canRestore NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
public:
    explicit NvidiaVrrProfileController(const std::filesystem::path& dataDirectory, QObject* parent = nullptr);
    bool busy() const { return busy_; }
    bool canRepair() const {
        if (busy_ || !readable_ || backupConflict_) return false;
        const auto plan = gfx::planVrrProfileRepair(state_);
        return plan.allowed && plan.changes();
    }
    bool canRestore() const { return !busy_ && restoreAvailable_; }
    QString summary() const;
    QString detail() const;
    Q_INVOKABLE void check();
    Q_INVOKABLE void repair();
    Q_INVOKABLE void restore();
signals:
    void changed();
    void notice(const QString& text, bool isError);
private:
    enum class Operation { Check, Repair, Restore };
    void run(Operation operation);
    QString executable_, backupPath_;
    gfx::VrrProfileSnapshot state_;
    gfx::VrrProfileError error_ = gfx::VrrProfileError::None;
    int driverStatus_ = 0;
    bool busy_ = false, checked_ = false, readable_ = false, restoreAvailable_ = false;
    bool backupConflict_ = false, storageError_ = false, restartRequired_ = false;
};
} // namespace veyra::ui
