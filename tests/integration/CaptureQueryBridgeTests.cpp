#include "../../src/ui/QmlPlayerBridge_p.h"
#include <QApplication>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace veyra::ui {
struct CaptureQueryBridgeTest {
    static int run(QmlPlayerBridge& bridge) {
        auto& state = *bridge.impl_;
        int updates = 0;
        int warnings = 0;
        const auto captureChange = QObject::connect(&bridge, &QmlPlayerBridge::captureChanged,
                                                    &bridge, [&] { ++updates; });
        const auto captureNotice = QObject::connect(&bridge, &QmlPlayerBridge::notice, &bridge,
                                                    [&](const QString&, bool warning) { if (warning) ++warnings; });
        auto check = [](bool ok, const char* message) {
            std::cout << (ok ? "PASS " : "FAIL ") << message << '\n';
            return ok;
        };

        std::promise<QmlPlayerBridge::Impl::CaptureQuery> driver;
        state.captureQuery = driver.get_future();
        state.captureBusy = true;
        state.captureQueryKind = 2;
        state.captureQueryStarted = std::chrono::steady_clock::now();
        bridge.tickCapture();
        bool ok = check(bridge.captureQueryBusy() && !bridge.captureQueryTimedOut() && updates == 0,
                        "ordinary pending driver calls continue quietly");

        state.captureQueryStarted -= std::chrono::seconds(13);
        bridge.tickCapture();
        bridge.tickCapture();
        ok &= check(bridge.captureQueryBusy() && bridge.captureQueryTimedOut() && updates == 1 &&
                    !bridge.captureStatus().isEmpty(), "a stuck driver publishes one terminal wait status");
        ok &= check(!bridge.startCaptureSession() && warnings == 1, "connect reports the driver timeout");

        QmlPlayerBridge::Impl::CaptureQuery late;
        late.kind = 2;
        driver.set_value(std::move(late));
        bridge.tickCapture();
        ok &= check(!bridge.captureQueryBusy() && !bridge.captureQueryTimedOut() && updates == 2 &&
                    warnings == 1, "late resume reply clears the wait without starting a session");
        QObject::disconnect(captureChange);
        QObject::disconnect(captureNotice);
        return ok ? 0 : 1;
    }
};
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    const std::filesystem::path profile(app.arguments().at(1).toStdWString());
    std::error_code error;
    if (!profile.is_absolute() || !std::filesystem::create_directory(profile, error)) return 2;
    auto engine = std::make_unique<veyra::engine::EngineController>();
    veyra::ui::QmlPlayerBridge bridge(*engine, profile);
    return veyra::ui::CaptureQueryBridgeTest::run(bridge);
}
