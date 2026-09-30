// SPDX-License-Identifier: GPL-3.0-only
// The PC streaming model (hosts, pairing, apps, settings, starting a stream) against the mock host, through
// the same Qt interface the QML dialog uses. Needs no display (offscreen platform) and no real host.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

#include "MockHost.h"
#include "veyra/ui/MoonlightModel.h"

using namespace veyra;
using namespace veyra::moonlight::mock;
using veyra::ui::MoonlightModel;

namespace {

int g_failures = 0, g_checks = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("FAIL %s\n", what); }
    else if (qEnvironmentVariableIsSet("VEYRA_TEST_VERBOSE")) std::printf("ok   %s\n", what);
    std::fflush(stdout);
}

void progress(const char* text) {
    if (qEnvironmentVariableIsSet("VEYRA_TEST_VERBOSE")) { std::puts(text); std::fflush(stdout); }
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

bool idle(MoonlightModel& model) { return !model.state().value("busy").toBool(); }
QString statusOf(MoonlightModel& model) { return model.state().value("status").toString(); }

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTemporaryDir data;
    qputenv("VEYRA_MOONLIGHT_DATA", data.path().toLocal8Bit());
    qputenv("VEYRA_MOONLIGHT_NO_DISCOVERY", "1");

    MockHost host("");
    const QString address = QStringLiteral("127.0.0.1:%1").arg(host.httpPort);
    QString hostId;
    {
        MoonlightModel model;
        std::optional<MoonlightModel::Launch> launched;
        int startedSignals = 0;
        QObject::connect(&model, &MoonlightModel::started, [&] { ++startedSignals; });
        model.setLaunchHandler([&](MoonlightModel::Launch launch) { launched = std::move(launch); return true; });

        model.load();
        check(waitFor([&] { return idle(model); }), "load finishes");
        check(model.hosts().isEmpty(), "no hosts at first");

        // --- adding a host
        model.addHost("not a host");
        check(!statusOf(model).isEmpty() && model.hosts().isEmpty(), "an address with spaces is refused");
        model.addHost(address);
        check(waitFor([&] { return model.hosts().size() == 1; }), "a host appears after adding it by address");
        const QVariantMap entry = model.hosts().value(0).toMap();
        hostId = entry.value("id").toString();
        check(entry.value("name").toString() == "DESKTOP-TEST" && entry.value("state").toString() == "unpaired", "it is listed by the name the host reports, unpaired");
        check(model.state().value("selected").toString() == hostId, "and selected");
        check(entry.value("saved").toBool(), "a manually added host is saved");

        // --- pairing with the PIN the dialog shows
        model.pair(hostId);
        const QString pin = model.state().value("pin").toString();
        check(model.state().value("pairing").toBool() && pin.size() == 4, "pairing shows a 4 digit PIN at once");
        host.setPin(pin.toStdString());
        check(waitFor([&] { return model.state().value("paired").toBool() && idle(model); }), "pairing with the right PIN succeeds");
        check(model.state().value("pin").toString().isEmpty() && !model.state().value("pairing").toBool(), "the PIN is cleared afterwards");

        // --- the app list arrives by itself after pairing
        check(waitFor([&] { return model.apps().size() == 3 && idle(model); }), "the app list is read after pairing");
        const QVariantList apps = model.apps();
        check(apps.value(0).toMap().value("name").toString() == "Desktop" && apps.value(0).toMap().value("hdr").toBool(), "first app is the Desktop with HDR");
        check(apps.value(1).toMap().value("name").toString() == QString::fromUtf8("Tom & Jerry <Deluxe> 中"), "entities in names are decoded");
        check(model.state().value("runningApp").toInt() == 7, "the host's running game is reported");

        // --- settings
        check(model.set("fps", 120) && model.set("res", "4k") && model.set("bitrate", 100) && model.set("codec", 2), "valid settings are accepted");
        check(!model.set("fps", 55) && !model.set("res", "8k") && !model.set("audio", 3) && !model.set("nonsense", 1), "invalid settings are refused");
        const QVariantMap cfg = model.state().value("settings").toMap();
        check(cfg.value("fps").toInt() == 120 && cfg.value("res").toString() == "4k" && cfg.value("bitrate").toInt() == 100, "settings are read back");

        // --- starting: another game runs on the host (id 7, not the app chosen)
        check(model.connectStream(881448767), "connectStream starts the job");
        check(waitFor([&] { return idle(model); }), "the launch check finishes");
        check(!launched && statusOf(model).contains(QString::fromUtf8("另一个游戏")), "another running game blocks a new launch with an explanation");

        // --- HDR with a codec that cannot carry it
        host.currentGame = 0;
        model.set("hdr", true);
        model.set("codec", 1);   // H.264
        model.connectStream(881448767);
        check(waitFor([&] { return idle(model); }), "second launch check finishes");
        check(!launched && statusOf(model).contains("HDR"), "HDR with H.264 is refused before anything is launched");

        // --- a normal launch
        model.set("codec", 2);   // HEVC + HDR: the mock reports 10 bit HEVC
        model.connectStream(881448767);
        check(waitFor([&] { return launched.has_value(); }), "the launch handler is called");
        if (launched) {
            const auto& d = launched->desc;
            check(d.appId == 881448767 && !d.resume && d.options.width == 3840 && d.options.height == 2160 && d.options.fps == 120, "4K120 goes to the session");
            check(d.options.bitrateKbps == 100000 && d.options.hdr && d.options.codec == moonlight::CodecChoice::Hevc, "bitrate, HDR and codec go to the session");
            check(d.identity.valid() && !d.serverCertPem.empty() && d.host.paired && d.host.uniqueId == "ABCD1234EF567890", "credentials, pinned certificate and a fresh serverinfo go along");
            check(launched->label.contains("DESKTOP-TEST") && launched->label.contains("Desktop"), "the label names the host and the app");
        }
        check(startedSignals == 1, "started() is emitted once");

        // --- resuming the running game
        launched.reset();
        host.currentGame = 881448767;
        check(waitFor([&] { return idle(model); }), "idle again");
        model.connectStream(881448767);
        check(waitFor([&] { return launched.has_value(); }), "resume launches");
        check(launched && launched->desc.resume, "launching the app that already runs is a resume");
    }

    progress("block 1 destroyed");
    // --- everything survives a restart
    {
        MoonlightModel model;
        progress("second model constructed");
        model.load();
        progress("second model loaded");
        check(waitFor([&] { return idle(model); }), "second model loads");
        check(model.hosts().size() == 1, "the saved host is back");
        const QVariantMap entry = model.hosts().value(0).toMap();
        check(entry.value("paired").toBool() && entry.value("id").toString() == hostId, "still paired");
        check(waitFor([&] { return model.hosts().value(0).toMap().value("state").toString() != "checking" && idle(model); }), "the status refresh finishes");
        check(model.state().value("settings").toMap().value("fps").toInt() == 120, "settings were saved per host");
        check(model.state().value("lastLabel").toString().contains("Desktop"), "the last stream is remembered for resuming");
        check(waitFor([&] { return model.apps().size() == 3; }), "apps reload from the saved pairing (pinned certificate works)");

        // --- a host that no longer knows us
        host.forgetPairing();
        model.refresh();
        check(waitFor([&] { return idle(model) && model.hosts().value(0).toMap().value("state").toString() == "unpaired"; }), "a host that forgot us shows as unpaired");

        // --- forgetting
        model.forget(hostId);
        check(model.hosts().isEmpty(), "forgetting removes the host at once");
        check(waitFor([&] { return idle(model); }), "the unpair request finishes");
    }
    {
        MoonlightModel model;
        model.load();
        check(waitFor([&] { return idle(model); }) && model.hosts().isEmpty(), "a forgotten host stays forgotten");
    }

    std::printf("moonlight model tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
