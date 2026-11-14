#include "veyra/ui/NvidiaVrrProfileController.h"
#include <windows.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <mutex>

using namespace veyra;
namespace {
struct FakeDriver {
    std::mutex mutex;
    gfx::VrrProfileSnapshot state;
    int inspections = 0, repairs = 0, restores = 0, repairStatus = 0;
    void reset() {
        const std::lock_guard lock(mutex);
        state = {}; state.exists = state.isolated = true;
        state.profileName = L"Veyra isolated test"; state.settingCount = 2;
        state.settings = {gfx::VrrProfileValue{1, true}, gfx::VrrProfileValue{1, true}};
        state.global = {gfx::VrrProfileValue{2, true}, gfx::VrrProfileValue{2, true}};
        inspections = repairs = restores = repairStatus = 0;
    }
} driver;
std::filesystem::path path(const QString& value) { return std::filesystem::path(value.toStdWString()); }
QString record(const QTemporaryDir& directory) {
    QDir folder(directory.filePath("driver-profiles"));
    const auto names = folder.entryList({QStringLiteral("vrr-*.json")}, QDir::Files);
    return names.size() == 1 ? folder.filePath(names.front()) : QString();
}
struct HeldFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    void close() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; } }
    ~HeldFile() { close(); }
};
} // namespace

// No production driver adapter is linked into this test executable.
namespace veyra::gfx {
VrrProfileResult inspectVrrProfile(const std::wstring& executable) {
    const std::lock_guard lock(driver.mutex);
    ++driver.inspections; driver.state.executable = executable;
    VrrProfileResult result; result.snapshot = driver.state; return result;
}
VrrProfileResult repairVrrProfile(const VrrProfileSnapshot& expected) {
    const std::lock_guard lock(driver.mutex);
    ++driver.repairs;
    VrrProfileResult result;
    if (driver.repairStatus) { result.error = VrrProfileError::Driver; result.driverStatus = driver.repairStatus; return result; }
    if (driver.state != expected) { result.error = VrrProfileError::Changed; return result; }
    driver.state.settings = {VrrProfileValue{2, true}, VrrProfileValue{2, true}};
    result.snapshot = driver.state; result.changed = true; return result;
}
VrrProfileResult restoreVrrProfile(const VrrProfileSnapshot& original, const VrrProfileSnapshot& expected) {
    const std::lock_guard lock(driver.mutex);
    ++driver.restores;
    VrrProfileResult result;
    const auto plan = planVrrProfileRestore(original, driver.state);
    if (driver.state != expected || !plan.allowed) { result.error = VrrProfileError::Changed; return result; }
    driver.state.settings = original.settings;
    result.snapshot = driver.state; result.changed = plan.changes(); return result;
}
} // namespace veyra::gfx

class ControllerTests final : public QObject {
    Q_OBJECT
private slots:
    void init() { driver.reset(); }
    void constructionDoesNotQueryOrWrite() {
        QTemporaryDir directory;
        ui::NvidiaVrrProfileController controller(path(directory.path()));
        QVERIFY(!controller.busy()); QVERIFY(!controller.canRepair()); QVERIFY(!controller.canRestore());
        const std::lock_guard lock(driver.mutex);
        QCOMPARE(driver.inspections, 0); QCOMPARE(driver.repairs, 0); QCOMPARE(driver.restores, 0);
    }
    void explicitRepairAndRestore() {
        QTemporaryDir directory;
        ui::NvidiaVrrProfileController controller(path(directory.path()));
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.canRepair()); QVERIFY(!controller.canRestore());
        controller.repair(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.canRepair()); QVERIFY(controller.canRestore());
        QVERIFY(!record(directory).isEmpty());
        controller.repair(); // Guard rejects a repeat without writing.
        { const std::lock_guard lock(driver.mutex); QCOMPARE(driver.repairs, 1); }
        controller.restore(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.canRepair()); QVERIFY(!controller.canRestore()); QVERIFY(record(directory).isEmpty());
        const std::lock_guard lock(driver.mutex);
        QCOMPARE(driver.state.settings[0].value, 1u); QCOMPARE(driver.state.settings[1].value, 1u);
        QCOMPARE(driver.state.global[0].value, 2u); QCOMPARE(driver.state.global[1].value, 2u);
    }
    void invalidDataDirectoryNeverReportsASharedProfile() {
        QTemporaryDir directory;
        QFile obstruction(directory.filePath("not-a-directory"));
        QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.close();
        ui::NvidiaVrrProfileController controller(path(obstruction.fileName()));
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.canRepair());
        controller.repair(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.canRepair()); QVERIFY(controller.detail().contains(QStringLiteral("数据目录")));
        const std::lock_guard lock(driver.mutex);
        QCOMPARE(driver.inspections, 1); QCOMPARE(driver.repairs, 0);
    }
    void deniedWriteRetainsOriginalAndAllowsRecordCleanup() {
        QTemporaryDir directory;
        ui::NvidiaVrrProfileController controller(path(directory.path()));
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        { const std::lock_guard lock(driver.mutex); driver.repairStatus = -175; }
        QSignalSpy notices(&controller, &ui::NvidiaVrrProfileController::notice);
        controller.repair(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.summary().contains(QStringLiteral("管理员"))); QVERIFY(controller.canRestore());
        QCOMPARE(notices.count(), 1); QVERIFY(notices.front()[1].toBool());
        QVERIFY(!record(directory).isEmpty());
        controller.restore(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(record(directory).isEmpty()); QVERIFY(!controller.canRestore());
        const std::lock_guard lock(driver.mutex);
        QCOMPARE(driver.state.settings[0].value, 1u); QCOMPARE(driver.state.global[0].value, 2u);
    }
    void failedRecordRemovalCanBeRetried() {
        QTemporaryDir directory;
        ui::NvidiaVrrProfileController controller(path(directory.path()));
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        controller.repair(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        const auto filename = record(directory); QVERIFY(!filename.isEmpty());
        HeldFile held;
        held.handle = CreateFileW(reinterpret_cast<LPCWSTR>(filename.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held.handle != INVALID_HANDLE_VALUE);
        controller.restore(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(QFile::exists(filename)); QVERIFY(controller.canRestore());
        held.close();
        controller.restore(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!QFile::exists(filename)); QVERIFY(!controller.canRestore());
        const std::lock_guard lock(driver.mutex);
        QCOMPARE(driver.state.settings[0].value, 1u); QCOMPARE(driver.state.global[0].value, 2u);
    }
    void subsequentUserEditsAndSharedProfilesArePreserved() {
        QTemporaryDir directory;
        ui::NvidiaVrrProfileController controller(path(directory.path()));
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        controller.repair(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        { const std::lock_guard lock(driver.mutex); driver.state.settings[1] = {0, true}; }
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.canRepair()); QVERIFY(!controller.canRestore());
        controller.restore();
        { const std::lock_guard lock(driver.mutex); QCOMPARE(driver.restores, 0); QCOMPARE(driver.state.settings[1].value, 0u); }
        { const std::lock_guard lock(driver.mutex); driver.state.isolated = false; }
        controller.check(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.canRepair()); QVERIFY(!controller.canRestore());
        controller.repair();
        const std::lock_guard lock(driver.mutex); QCOMPARE(driver.repairs, 1);
    }
};
QTEST_GUILESS_MAIN(ControllerTests)
#include "ControllerTests.moc"
