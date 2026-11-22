#include "veyra/engine/EngineController.h"
#include "veyra/ui/QmlPlayerBridge.h"

#include <QApplication>
#include <QDir>
#include <QSignalSpy>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    const auto profile = app.arguments().at(1);
    if (!QDir::isAbsolutePath(profile) || QDir(profile).exists() || !QDir().mkpath(profile)) return 2;

    auto engine = std::make_unique<veyra::engine::EngineController>();
    veyra::ui::QmlPlayerBridge bridge(*engine, std::filesystem::path(profile.toStdWString()));
    const auto choices = bridge.fgBackendChoices();
    const auto rife = std::find_if(choices.cbegin(), choices.cend(), [](const auto& entry) {
        return entry.toMap().value("id").toString() == QStringLiteral("rife");
    });
    if (rife == choices.cend()) return 1;
    const auto row = rife->toMap();
    const bool disabled = row.value("disabled").toBool();
    const bool showsPath = row.value("note").toString().contains(QStringLiteral("runtime/rife"));
    const auto previous = bridge.fgBackendName();
    QSignalSpy notice(&bridge, &veyra::ui::QmlPlayerBridge::notice);
    bridge.setFgBackendName(QStringLiteral("rife"));
    const bool rejected = bridge.fgBackendName() == previous && !notice.isEmpty() &&
        notice.first().at(0).toString().contains(QStringLiteral("runtime/rife"));
    std::cout << "RIFE_WITHOUT_RUNTIME disabled=" << disabled << " menuPath=" << showsPath
              << " selectionRejected=" << rejected << '\n';
    return disabled && showsPath && rejected ? 0 : 1;
}
