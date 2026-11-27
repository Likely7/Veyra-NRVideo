#include "veyra/engine/EngineController.h"
#include "veyra/ui/PlaylistModel.h"
#include "veyra/ui/NetworkHistory.h"
#include "veyra/ui/PlayerUiFacade.h"
#include "veyra/ui/QmlPlayerBridge.h"
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QWindow>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
int assertions = 0, failures = 0;
QStringList warnings;
void messages(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) warnings << message;
    std::fprintf(stderr, "%s\n", message.toUtf8().constData());
}
void check(bool condition, const char* message) {
    ++assertions; std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}
QQuickItem* find(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) return item;
    for (auto* child : item->childItems()) if (auto* found = find(child, name)) return found;
    return nullptr;
}
QPoint point(QQuickItem* item) { return item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint(); }
bool click(QQuickWindow* win, const char* name, bool twice = false) {
    auto* item = find(win->contentItem(), QString::fromUtf8(name));
    if (!item) { check(false, name); return false; }
    if (twice) QTest::mouseDClick(win, Qt::LeftButton, Qt::NoModifier, point(item));
    else QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, point(item));
    QTest::qWait(80); return true;
}
void dropUrls(QQuickWindow* win, const QList<QUrl>& urls) {
    QMimeData mime; mime.setUrls(urls);
    QDragEnterEvent enter(QPoint(180, 240), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &enter);
    check(enter.isAccepted(), "native URL drag accepted");
    QDragMoveEvent move(QPoint(180, 240), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &move);
    QDropEvent event(QPointF(180, 240), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &event);
    check(event.isAccepted(), "native URL drop accepted");
}
void drop(QQuickWindow* win, const QString& path) { dropUrls(win, {QUrl::fromLocalFile(path)}); }

int replayNetworkHistory(const QString& profile, const QString& address) {
    if (!QDir::isAbsolutePath(profile) || !QFile::exists(profile + "/ui-session.v1")) return 2;
    QWindow video;
    video.resize(320, 180);
    video.show();
    check(QTest::qWaitForWindowExposed(&video), "test video surface is presentable");
    veyra::engine::EngineController engine;
    veyra::ui::QmlPlayerBridge bridge(engine, std::filesystem::path(profile.toStdWString()));
    bridge.attachVideoWindow(video.winId());
    bridge.setMuted(true);
    bridge.setPreference("decode", "software");
    const auto framesAfter = [&](uint64_t previous) {
        return QTest::qWaitFor([&] {
            const auto snapshot = engine.snapshot();
            return snapshot.sessionId != previous && snapshot.metrics.flow.counters.realPresented >= 10 && !snapshot.failed;
        }, 12000);
    };

    const auto recent = bridge.recentFiles();
    auto* playlist = qobject_cast<veyra::ui::PlaylistModel*>(bridge.playlist());
    check(recent.size() == 1 && playlist && playlist->count() == 1, "fresh bridge restores the successful address only");
    if (recent.size() != 1 || !playlist || playlist->count() != 1) return 1;
    const auto recentRow = recent.front().toMap();
    check(recentRow.value("path").toString() == address && recentRow.value("exists").toBool(),
          "Recent restores the signed address");
    if (recentRow.value("path").toString() != address) return 1;
    auto session = engine.snapshot().sessionId;
    bridge.openPath(recentRow.value("path").toString());
    check(framesAfter(session), "Recent reopens the authenticated server and presents video");
    session = engine.snapshot().sessionId;
    bridge.stopPlayback();
    check(playlist->play(playlist->indexOfPath(address)) && framesAfter(session),
          "playlist entry reopens the authenticated server and presents video");
    bridge.stopPlayback();
    check(playlist->count() == 1 && bridge.recentFiles().size() == 1, "replaying the same address leaves a single row");
    std::printf("%s network replay checks=%d failures=%d\n", failures ? "FAIL" : "PASS", assertions, failures);
    return failures ? 1 : 0;
}
}
int main(int argc, char** argv) {
    const bool replay = argc == 4 && QByteArray(argv[2]) == "--network-replay";
    if (argc != 2 && !replay) return 2;
    // Disable the application's background update poll for the isolated test.
    char disableUpdate[] = "--portable-update-probe";
    char* appArgs[] = {argv[0], argv[1], disableUpdate, nullptr};
    int appArgCount = 3;
    QApplication app(appArgCount, appArgs);
    qInstallMessageHandler(messages);
    const auto path = QString::fromLocal8Bit(argv[1]);
    if (replay) return replayNetworkHistory(path, QString::fromLocal8Bit(argv[3]));
    if (!QDir::isAbsolutePath(path) || QFileInfo::exists(path)) return 2;
    QDir().mkpath(path);
    for (int i = 1; i <= 12; ++i) { QFile f(path + QString("/file%1.mp4").arg(i)); f.open(QIODevice::WriteOnly); f.write("metadata fixture"); }
    veyra::ui::PlaylistModel model;
    model.addPaths({path});
    for (int i = 0; i < 200 && model.busy(); ++i) QTest::qWait(10);
    check(model.count() == 12 && !model.busy(), "production model ready");
    const QString playing = model.pathAt(0);
    const QString profile = path + "/profile";
    if (!QDir().mkpath(profile)) return 2;
    const std::filesystem::path dataDirectory(profile.toStdWString());
    veyra::engine::EngineController playbackEngine;
    const QString address = QStringLiteral("https://synthetic-user:synthetic-password@history.example:8443/"
                                           "synthetic-path/video?token=MiXeD%2B");
    const auto network = veyra::ui::ProtectedNetworkAddress::protect(address.toStdWString());
    check(network.has_value(), "protect network history");
    if (!network) return 1;
    {
        veyra::ui::PlayerUiFacade history(playbackEngine, dataDirectory);
        history.noteRecentFile(playing.toStdWString());
        history.noteRecentFile((path + "/missing.mp4").toStdWString());
        history.noteRecentNetwork(*network);
        history.setReducedMotion(true);
        history.setDefaultPage(L"pro");
    }
    {
        QFile file(profile + "/ui-session.v1");
        check(file.open(QIODevice::ReadOnly), "read recent history storage");
        const auto bytes = file.readAll();
        check(bytes.contains("dpapi:") && bytes.contains("missing.mp4") && !bytes.contains("synthetic-") &&
              !bytes.contains("history.example") && !bytes.contains("MiXeD"), "stored Recent uses protected addresses");
    }
    veyra::ui::QmlPlayerBridge historyBridge(playbackEngine, dataDirectory);
    const auto recent = historyBridge.recentFiles();
    check(recent.size() == 3, "saved history loads a network address plus existing and missing files");
    if (recent.size() == 3) {
        const auto entry = recent.front().toMap();
        check(entry.value("path").toString() == address && entry.value("exists").toBool() &&
              entry.value("network").toBool() && entry.value("label").toString() == "https://history.example:8443",
              "restored Recent label hides signed URL details while keeping the exact playable address");
        check(!recent.at(1).toMap().value("exists").toBool() && recent.at(2).toMap().value("exists").toBool(),
              "legacy local presence checks are unchanged");
    }
    model.setPlayback(playing, "playing");
    QString requested;
    QObject::connect(&model, &veyra::ui::PlaylistModel::playRequested, [&](const auto& selected) { requested = selected; });
    QQmlEngine engine;
    engine.addImportPath(QString::fromUtf8(VEYRA_QML_DIR));
    engine.rootContext()->setContextProperty("testPlaylist", &model);
    engine.rootContext()->setContextProperty("historyBridge", &historyBridge);
    QQmlComponent component(&engine);
    component.setData(R"qml(
import QtQuick
import Veyra
Window {
    id: owner
    x: 40; y: 40; width: 800; height: 500; visible: true
    PlaylistWindow {
        owner: owner
        playlist: testPlaylist
        hasRecentFiles: historyBridge.recentFiles.length > 0
        onClearRecentFiles: historyBridge.clearRecentFiles()
    }
}
)qml", QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) { std::fprintf(stderr, "%s\n", component.errorString().toUtf8().constData()); return 1; }
    auto* owner = qobject_cast<QQuickWindow*>(object.get());
    auto* win = object->findChild<QQuickWindow*>("playlistWindow");
    check(win && owner && win->transientParent() == owner, "independent window belongs to player");
    if (!win) return 1;
    QMetaObject::invokeMethod(win, "open");
    check(QTest::qWaitForWindowExposed(win), "playlist window opens");
    QTest::qWait(250);
    auto* badge = find(win->contentItem(), "playlist-state-0");
    check(badge && badge->property("text").toString() == QString::fromUtf8("正在播放"), "real model state shown in row");
    auto* grip = find(win->contentItem(), "playlist-grip-0");
    auto* row2 = find(win->contentItem(), "playlist-row-2");
    if (grip && row2) {
        const auto first = point(grip), destination = QPoint(first.x(), point(row2).y());
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, first);
        QTest::mouseMove(win, destination, 100);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, destination);
        QTest::qWait(100);
        check(model.currentIndex() == 2 && model.pathAt(2) == playing, "mouse drag reorders while current file follows");
    } else check(false, "visible drag handles");
    model.setPlayback(playing, "paused"); QTest::qWait(50);
    badge = find(win->contentItem(), "playlist-state-2");
    check(badge && badge->property("text").toString() == QString::fromUtf8("已暂停"), "paused badge changes without replacing playlist");
    click(win, "playlist-select-1", true);
    check(veyra::ui::PlaylistModel::normalizedPath(requested) == model.pathAt(1), "double click emits playback request");
    const auto removed = model.pathAt(1); click(win, "playlist-remove-1");
    check(model.count() == 11 && QFileInfo::exists(removed), "remove button only removes list item");
    requested.clear();
    QDir().mkpath(path + "/追加"); QFile extra(path + "/追加/added.mkv"); extra.open(QIODevice::WriteOnly); extra.write("new"); extra.close();
    drop(win, path + "/追加");
    QTest::qWait(20);
    for (int i = 0; i < 200 && model.busy(); ++i) QTest::qWait(10);
    std::printf("DROP count=%d current=%d requested=%s status=%s\n", model.count(), model.currentIndex(), requested.toUtf8().constData(), model.status().toUtf8().constData());
    check(model.count() == 12 && requested.isEmpty() && model.pathAt(model.currentIndex()) == playing, "folder drop appends while playback selection stays");
    QDir().mkpath(path + "/outside");
    for (const auto& name : {"one.mp4", "two.mkv", "three.mp4"}) {
        QFile file(path + "/outside/" + name); file.open(QIODevice::WriteOnly); file.write("file drop fixture");
    }
    dropUrls(win, {QUrl::fromLocalFile(path + "/outside/one.mp4"), QUrl::fromLocalFile(path + "/outside/two.mkv")});
    QTest::qWait(20); for (int i = 0; i < 200 && model.busy(); ++i) QTest::qWait(10);
    check(model.count() == 14 && requested.isEmpty(), "multiple files from another folder append without playback");
    drop(win, path + "/outside/three.mp4");
    QTest::qWait(20); for (int i = 0; i < 200 && model.busy(); ++i) QTest::qWait(10);
    check(model.count() == 15 && requested.isEmpty(), "single file drop in playlist appends instead of switching video");
    // A spacing gap is not the beginning/end of the playlist. Drag a lower
    // row into a gap in the middle, where indexAt alone returns -1.
    auto* movedGrip = find(win->contentItem(), "playlist-grip-3");
    auto* firstRow = find(win->contentItem(), "playlist-row-1");
    if (movedGrip && firstRow) {
        const auto source = model.pathAt(3);
        const auto first = point(movedGrip);
        const auto gap = firstRow->mapToScene(QPointF(firstRow->width()/2, firstRow->height()+2)).toPoint();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, first);
        QTest::mouseMove(win, QPoint(first.x(), gap.y()), 100);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, QPoint(first.x(), gap.y()));
        QTest::qWait(80);
        check(model.pathAt(1) == source && model.pathAt(model.currentIndex()) == playing, "drop in row spacing stays near the drop instead of jumping to an end");
    } else check(false, "visible handles for gap reorder");
    win->resize(420, 320);
    QTest::qWait(100);
    auto* clearHistory = find(win->contentItem(), "playlist-clear-history");
    auto* historyDialog = find(win->contentItem(), "playlist-clear-history-confirm");
    check(clearHistory && clearHistory->isEnabled() && clearHistory->isVisible() &&
          clearHistory->mapToScene(QPointF(clearHistory->width(), clearHistory->height())).y() <= win->height(),
          "clear history is available at the minimum window size");
    const auto queuedItems = model.count();
    click(win, "playlist-clear-history");
    check(historyDialog && historyDialog->property("shown").toBool() && historyBridge.recentFiles().size() == 3,
          "opening confirmation leaves the history intact");
    QTest::keyClick(win, Qt::Key_Delete);
    QTest::keyClick(win, Qt::Key_Return);
    check(model.count() == queuedItems && requested.isEmpty(), "confirmation owns playlist shortcuts");
    QTest::keyClick(win, Qt::Key_Escape);
    QTest::qWait(250);
    check(historyDialog && !historyDialog->property("shown").toBool() && win->isVisible() &&
          historyBridge.recentFiles().size() == 3, "Escape cancels without closing the playlist");

    auto confirmClear = [&] {
        click(win, "playlist-clear-history");
        QTest::qWait(250);
        click(win, "playlist-clear-history-confirm-accept");
        QTest::qWait(250);
    };
    // A directory in place of the temporary file denies the atomic write.
    const QString blocker = "ui-session.v1.tmp";
    QSignalSpy notices(&historyBridge, &veyra::ui::QmlPlayerBridge::notice);
    check(QDir(profile).mkdir(blocker), "write failure fixture created");
    confirmClear();
    check(historyBridge.recentFiles().size() == 3 && !notices.isEmpty() && notices.last().at(1).toBool(),
          "a failed save reports the error and keeps history available");
    check(QDir(profile).rmdir(blocker), "write failure fixture removed");
    confirmClear();
    veyra::ui::PlayerUiFacade reloaded(playbackEngine, dataDirectory);
    check(historyBridge.recentFiles().isEmpty() && clearHistory && !clearHistory->isEnabled() &&
          reloaded.loadPreferences() && reloaded.recentFiles().empty(), "confirmed clear updates both the UI and stored history");
    check(model.count() == queuedItems && model.pathAt(model.currentIndex()) == playing && QFileInfo::exists(playing),
          "clearing history preserves the queue, current file and media");

    auto* list = find(win->contentItem(), "playlist-list");
    win->resize(620, 480); win->setPosition(win->x() + 25, win->y() + 30); QTest::qWait(100);
    check(win->width() == 620 && win->height() == 480 && list && list->width() > 560, "resized layout adapts without clipping controls");
    check(find(win->contentItem(), "playlist-title-drag") && find(win->contentItem(), "playlist-resize-10"), "system move and corner resize input handles exist");
    click(win, "playlist-close");
    check(!win->isVisible() && owner->isVisible(), "close hides playlist without closing player");
    QMetaObject::invokeMethod(win, "open"); QTest::qWait(100);
    check(win->isVisible() && win->width() == 620 && win->height() == 480, "reopen preserves window size");
    check(warnings.isEmpty(), "no QML binding/import/runtime warnings");
    win->grabWindow().save(path + "/playlist.png");
    owner->close(); QTest::qWait(100);
    check(!win->isVisible(), "closing owner also hides playlist");
    historyBridge.setInitialPage("pro");
    warnings.clear();
    engine.rootContext()->setContextProperty("veyra", &historyBridge);
    engine.rootContext()->setContextProperty("vyMusic", QVariantMap{
        {"available", false}, {"muted", false}, {"state", "idle"}});
    QQmlComponent shellComponent(&engine, QUrl::fromLocalFile(QString::fromUtf8(VEYRA_QML_DIR) + "/Veyra/Main.qml"));
    std::unique_ptr<QObject> shellObject(shellComponent.create());
    if (!shellObject) {
        std::fprintf(stderr, "%s\n", shellComponent.errorString().toUtf8().constData());
        return 1;
    }
    auto* shell = qobject_cast<QQuickWindow*>(shellObject.get());
    auto* shellPlaylist = shellObject->findChild<QQuickWindow*>("playlistWindow");
    auto* playlistShortcut = shellObject->findChild<QObject*>("open-playlist-shortcut");
    if (!shell || !shellPlaylist || !playlistShortcut) return 1;
    const auto activate = [&] {
        shellPlaylist->hide();
        shell->requestActivate();
        QTest::qWaitFor([&] { return shell->isActive(); });
    };
    shell->resize(1280, 800);
    activate();
    check(shell->property("page").toString() == "pro", "production shell starts in Professional view");
    check(QTest::qWaitFor([&] {
        const auto* transport = find(shell->contentItem(), "pro-transport");
        return transport && !transport->property("compact").toBool() &&
               find(shell->contentItem(), "pro-playlist");
    }), "wide Professional transport is laid out");
    if (click(shell, "pro-playlist"))
        check(shellPlaylist->isVisible() && shellPlaylist->transientParent() == shell,
              "Professional button opens the owned playlist");
    activate();
    QTest::keyClick(shell, Qt::Key_P, Qt::ControlModifier);
    check(QTest::qWaitFor([&] { return shellPlaylist->isVisible(); }), "default Ctrl+P opens the playlist");
    activate();
    check(historyBridge.setShortcut("playlist", "Ctrl+Shift+P"), "playlist shortcut is rebindable");
    QTest::keyClick(shell, Qt::Key_P, Qt::ControlModifier);
    check(!shellPlaylist->isVisible(), "old shortcut is inactive after rebinding");
    QTest::keyClick(shell, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    check(QTest::qWaitFor([&] { return shellPlaylist->isVisible(); }), "rebound shortcut opens the playlist");
    activate();
    shell->setProperty("fullLocked", true);
    QTest::keyClick(shell, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    check(!playlistShortcut->property("enabled").toBool() && !shellPlaylist->isVisible(),
          "fullscreen lock blocks the playlist shortcut");
    shell->setProperty("fullLocked", false);
    shell->resize(720, 560);
    check(QTest::qWaitFor([&] {
        const auto* transport = find(shell->contentItem(), "pro-transport");
        return transport && transport->property("compact").toBool();
    }), "compact Professional transport is laid out");
    if (click(shell, "pro-playlist"))
        check(shellPlaylist->isVisible(), "Professional button opens the playlist in compact transport");
    shell->close();
    check(warnings.isEmpty(), "production shell has no QML warnings");

    std::printf("%s playlist window assertions=%d\n", failures ? "FAIL" : "PASS", assertions);
    return failures ? 1 : 0;
}
