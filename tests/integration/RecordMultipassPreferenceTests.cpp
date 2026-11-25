#include "veyra/Log.h"
#include "veyra/engine/EngineController.h"
#include "veyra/ui/QmlPlayerBridge.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
int checks = 0, failures = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
}
QJsonObject readPrefs(const QString& directory) {
    QFile file(directory + "/qml-preferences.v1.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString readLog(const QString& path) {
    veyra::Logger::instance().flush();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}
}

// 设置 → 播放 → 二次编码 writes recordMultipass. The key has to round-trip
// through the same preference file the bridge reloads, without moving bitrate,
// frame-rate, or encoder defaults.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString root = app.arguments().at(1);
    if (!QDir::isAbsolutePath(root) || QFile::exists(root) || !QDir().mkpath(root)) return 2;
    const QString profile = root + "/profile";
    const QString logPath = root + "/bridge.log";
    if (!veyra::Logger::instance().openFile(logPath.toStdWString(), false)) return 2;

    auto engine = std::make_unique<veyra::engine::EngineController>();
    auto makeBridge = [&](const QString& directory) {
        return std::make_unique<veyra::ui::QmlPlayerBridge>(*engine, std::filesystem::path(directory.toStdWString()));
    };

    {
        auto bridge = makeBridge(profile);
        check(!bridge->preferences().contains(QStringLiteral("recordMultipass")), "fresh profile leaves two-pass unset");
        check(bridge->recordingBitrateMbps() == 0 && bridge->recordingFpsLimit() == 0 && !bridge->recordingHevc(),
              "bitrate, fps, and encoder defaults stay automatic H.264");
        check(!bridge->replayEnabled(), "replay stays off");
        check(!bridge->setPreference(QStringLiteral("recordMultipass"), 3), "3 is outside 0..2");
        check(!bridge->setPreference(QStringLiteral("recordMultipass"), -1), "negative two-pass is refused");
        check(!bridge->setPreference(QStringLiteral("recordMultipass"), QStringLiteral("quarter")), "a label is not a mode");
        check(!bridge->preferences().contains(QStringLiteral("recordMultipass")), "refused values are not stored");
        check(!readLog(logPath).contains(QStringLiteral("recordMultipass=")), "refused values write no ui-prefs line");

        const int replayLogs = readLog(logPath).count(QStringLiteral("[ui-replay]"));
        check(bridge->setPreference(QStringLiteral("recordMultipass"), 1), "quarter-resolution two-pass is stored");
        check(readLog(logPath).count(QStringLiteral("[ui-replay]")) > replayLogs, "saving two-pass re-arms replay with the recording options");
        check(bridge->preferences().value(QStringLiteral("recordMultipass")).toInt() == 1, "memory reads the stored mode back");
        check(readPrefs(profile).value(QStringLiteral("recordMultipass")).toInt() == 1, "preference file reads the stored mode back");
        check(readLog(logPath).contains(QStringLiteral("[ui-prefs] recordMultipass=1")), "accepted mode is logged");
        check(bridge->recordingBitrateMbps() == 0 && bridge->recordingFpsLimit() == 0 && !bridge->recordingHevc() && !bridge->replayEnabled(),
              "storing two-pass does not change recording or replay defaults");

        check(bridge->setPreference(QStringLiteral("recordMultipass"), 2.0), "QML number 2 stores the full-resolution pass");
        check(bridge->preferences().value(QStringLiteral("recordMultipass")).toInt() == 2, "full-resolution mode is current");
        check(!bridge->setPreference(QStringLiteral("notARecordingKey"), 1), "unknown keys stay refused");
        check(bridge->preferences().value(QStringLiteral("recordMultipass")).toInt() == 2, "a refused key leaves the stored mode");
    }
    {
        auto bridge = makeBridge(profile);
        check(bridge->preferences().value(QStringLiteral("recordMultipass")).toInt() == 2, "reloaded profile keeps full-resolution two-pass");
        check(bridge->setPreference(QStringLiteral("recordMultipass"), 0), "off is stored again");
        check(bridge->preferences().value(QStringLiteral("recordMultipass")).toInt() == 0, "off reads back as 0");
        check(readPrefs(profile).value(QStringLiteral("recordMultipass")).toInt() == 0, "preference file reads off back as 0");
        check(bridge->recordingBitrateMbps() == 0 && bridge->recordingFpsLimit() == 0 && !bridge->recordingHevc() && !bridge->replayEnabled(),
              "turning two-pass off leaves the other recording defaults");
    }
    std::printf("SUMMARY: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
