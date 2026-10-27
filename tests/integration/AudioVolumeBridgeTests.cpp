#include "veyra/engine/EngineController.h"
#include "veyra/ui/QmlPlayerBridge.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
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
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString root = QString::fromLocal8Bit(argv[1]);
    // Only a new, explicitly supplied fixture directory; no real player profile.
    if (!QDir::isAbsolutePath(root) || QFile::exists(root) || !QDir().mkpath(root)) return 2;
    auto engine = std::make_unique<veyra::engine::EngineController>();
    const QString profile = root + "/main";
    auto makeBridge = [&](const QString& directory) {
        return std::make_unique<veyra::ui::QmlPlayerBridge>(*engine, std::filesystem::path(directory.toStdWString()));
    };
    {
        auto bridge = makeBridge(profile);
        check(bridge->maximumVolume() == 3.0, "production bridge exposes 300 percent maximum");
        check(bridge->volume() == 1.0 && !bridge->muted(), "fresh profile starts at unity, unmuted");
        bridge->setVolume(3.0);
        check(bridge->volume() == 3.0 && engine->snapshot().volume == 3.0f, "300 percent reaches real engine immediately");
        bridge->setMuted(true);
        check(bridge->muted() && engine->snapshot().muted, "mute reaches real engine without losing gain");
        QTest::qWait(650);
        const auto prefs = readPrefs(profile);
        check(prefs.value("audioVolume").toDouble() == 3.0 && prefs.value("audioMuted").toBool(), "debounced save persists volume and mute");
        bridge->setVolume(2.0);
        QTest::qWait(650);
        check(readPrefs(profile).value("audioVolume").toDouble() == 2.0, "debounced save atomically replaces existing preferences");
        bridge->setVolume(3.0);
    }
    {
        auto bridge = makeBridge(profile);
        check(bridge->volume() == 3.0 && bridge->muted() && engine->snapshot().volume == 3.0f, "production bridge reconstruction restores 300 percent and mute");
        bridge->setVolume(0.5);
        bridge->setMuted(false);
        // No event-loop wait: exercise destructor flush of pending save.
    }
    check(readPrefs(profile).value("audioVolume").toDouble() == 0.5 && !readPrefs(profile).value("audioMuted").toBool(), "exit before debounce flushes accepted state");
    {
        auto bridge = makeBridge(profile);
        check(bridge->volume() == 0.5 && !bridge->muted(), "flushed state survives reconstruction");
        bridge->setVolume(100.0);
        check(bridge->volume() == 3.0, "out-of-range input clamps to 300 percent");
        bridge->setVolume(-1.0);
        check(bridge->volume() == 0.0, "negative input clamps to zero");
        bridge->setVolume(2.0);
        bridge->setVolume(std::numeric_limits<double>::quiet_NaN());
        bridge->setVolume(std::numeric_limits<double>::infinity());
        check(bridge->volume() == 2.0, "nonfinite input leaves real bridge state unchanged");
        for (int i = 0; i < 20; ++i) bridge->setVolume(std::min(3.0, bridge->volume() + 0.05));
        check(std::abs(bridge->volume() - 3.0) < 0.00001, "rapid keyboard-style increments use immediate accepted state");
    }
    const QString invalid = root + "/invalid";
    QDir().mkpath(invalid);
    {
        QFile file(invalid + "/qml-preferences.v1.json");
        if (!file.open(QIODevice::WriteOnly)) return 2;
        file.write("{\"audioVolume\":4.0,\"audioMuted\":false}");
    }
    {
        auto bridge = makeBridge(invalid);
        check(bridge->volume() == 1.0 && engine->snapshot().volume == 1.0f, "invalid saved gain falls back to unity rather than automatic boost");
    }
    std::printf("SUMMARY: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
