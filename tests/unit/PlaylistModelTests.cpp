#include "veyra/ui/PlaylistModel.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <functional>

using veyra::ui::PlaylistModel;
namespace {
int assertions = 0, failures = 0;
void check(bool condition, const char* message) {
    ++assertions;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL %s\n", message); }
}
bool wait(const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start();
    do { QCoreApplication::processEvents(); if (ready()) return true; QThread::msleep(1); } while (timer.elapsed() < 15000);
    return false;
}
void make(const QString& path, int size, qint64 modified) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path); check(file.open(QIODevice::WriteOnly), "fixture create");
    check(file.write(QByteArray(size, 'x')) == size, "fixture bytes");
    check(file.setFileTime(QDateTime::fromMSecsSinceEpoch(modified), QFileDevice::FileModificationTime), "fixture date");
}
QString name(PlaylistModel& model, int row) { return model.data(model.index(row), PlaylistModel::NameRole).toString(); }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString root = QString::fromLocal8Bit(argv[1]);
    if (!QDir::isAbsolutePath(root) || QFileInfo(root).exists()) { std::fprintf(stderr, "Use a fresh absolute fixture directory\n"); return 2; }
    QDir().mkpath(root);
    const auto folder = root + "/影片 α";
    const qint64 time = 1700000000000LL;
    for (int i = 320; i >= 1; --i) make(folder + QString("/movie%1.MKV").arg(i), i, time + i * 2000);
    make(folder + "/子文件夹/中文片名.mp4", 900, time + 900000);
    make(folder + "/notes.txt", 12, time);
    const QString storage = root + "/playlist.json";
    QString playing, error;
    PlaylistModel model(storage);
    QObject::connect(&model, &PlaylistModel::playRequested, &app, [&](const auto& path) { playing = path; });
    QObject::connect(&model, &PlaylistModel::error, &app, [&](const auto& text) { error = text; });
    QElapsedTimer elapsed; elapsed.start(); model.addPaths({folder});
    const auto submitted = elapsed.elapsed();
    check(submitted < 50, "folder import returns to GUI within 50ms");
    check(model.busy() && model.count() == 0, "enumeration is queued off GUI thread");
    int heartbeats = 0; QTimer pulse; pulse.setInterval(1); QObject::connect(&pulse, &QTimer::timeout, [&] { ++heartbeats; }); pulse.start();
    check(wait([&] { return !model.busy(); }), "321 entries imported");
    const auto scanned = elapsed.elapsed();
    check(model.count() == 321 && heartbeats > 0, "200+ import includes nested media while UI timer runs");
    check(name(model, 0) == "movie1.MKV" && name(model, 1) == "movie2.MKV", "natural names keep 2 before 10");
    check(playing.isEmpty(), "import never starts playback");
    for (int i = 0; i < 320; ++i) check(name(model, i) == QString("movie%1.MKV").arg(i + 1), "all naturally ordered files present once");
    model.addPaths({folder, folder + "/movie2.MKV"});
    check(wait([&] { return !model.busy(); }) && model.count() == 321, "overlapping folder/file drops deduplicate");
    const QString chosen = model.pathAt(9);
    model.setPlayback(chosen, "playing");
    check(model.currentIndex() == 9 && model.data(model.index(9), PlaylistModel::StateRole) == "playing", "actual playback marker");
    QPersistentModelIndex persistent(model.index(9));
    check(model.move(9, 0) && model.currentIndex() == 0 && persistent.row() == 0, "drag reorder preserves current and persistent index");
    model.sortBy(PlaylistModel::LargestFirst);
    check(name(model, 0) == QString::fromUtf8("中文片名.mp4") && model.pathAt(persistent.row()) == chosen, "size sort preserves file identity");
    model.sortBy(PlaylistModel::SmallestFirst); check(name(model, 0) == "movie1.MKV", "smallest first");
    model.sortBy(PlaylistModel::NewestFirst); check(name(model, 0) == QString::fromUtf8("中文片名.mp4"), "newest first");
    model.sortBy(PlaylistModel::OldestFirst); check(name(model, 0) == "movie1.MKV", "oldest first");
    model.sortBy(PlaylistModel::NameDescending); check(name(model, model.count() - 1) == "movie1.MKV", "descending names");
    model.sortBy(PlaylistModel::NameAscending); check(name(model, 1) == "movie2.MKV", "ascending names");
    model.setPlayback(chosen, "paused"); check(model.data(model.index(model.currentIndex()), PlaylistModel::StateRole) == "paused", "paused is not falsely playing");
    const auto kept = model.pathAt(3);
    check(model.remove(3) && QFileInfo::exists(kept) && model.count() == 320, "remove never deletes disk file");
    check(model.pathAt(model.currentIndex()) == chosen, "removing another row preserves playing file");
    make(root + "/other/extra.mp4", 44, time);
    model.addUrls({QUrl::fromLocalFile(root + "/other"), QUrl("https://invalid.example/test.mkv")});
    check(wait([&] { return !model.busy(); }) && model.count() == 321 && playing.isEmpty(), "another folder appends without play and remote URLs ignored");
    check(model.pathAt(model.currentIndex()) == chosen, "append plus sort preserves current path");
    check(model.play(model.currentIndex()) && PlaylistModel::normalizedPath(playing) == chosen, "selection invokes shared playback command");
    playing.clear(); model.setAutoAdvance(false);
    check(!model.advanceAfterEnd() && playing.isEmpty(), "auto advance toggle off");
    model.setAutoAdvance(true); model.sortBy(PlaylistModel::NameAscending);
    model.setPlayback(model.pathAt(0), "ended");
    check(model.advanceAfterEnd() && playing.endsWith("movie1.MKV"), "next follows current list order");
    model.setPlayback(model.pathAt(model.count() - 1), "ended"); playing.clear();
    check(!model.advanceAfterEnd() && playing.isEmpty(), "last item does not wrap");
    model.setPlayback(chosen, "playing");
    const int before = model.count(); model.remove(model.currentIndex());
    check(model.currentIndex() == -1 && model.count() == before - 1 && !model.advanceAfterEnd(), "removing active item does not mislabel its successor");
    model.setAutoAdvance(false);
    check(model.save(), "atomic JSON save");
    {
        PlaylistModel restored(storage);
        check(restored.count() == model.count() && !restored.autoAdvance() && restored.currentIndex() == -1, "restore order/options without fake playing marker");
        for (int i = 0; i < model.count(); ++i) check(restored.pathAt(i) == model.pathAt(i), "persistent list order exact");
    }
    // Missing files remain visible after restart; playing one gives an error,
    // rather than silently deleting the user's list or opening a dead session.
    const QString missing = root + "/missing.json";
    QFile out(missing); out.open(QIODevice::WriteOnly);
    out.write(QJsonDocument(QJsonObject{{"version", 1}, {"entries", QJsonArray{QJsonObject{{"path", root + "/gone.mp4"}}}}}).toJson()); out.close();
    PlaylistModel absent(missing);
    QObject::connect(&absent, &PlaylistModel::error, [&](const auto& text) { error = text; });
    check(absent.count() == 1 && !absent.play(0) && !error.isEmpty(), "missing media is explicit, retained, and never played");
    QFile damaged(root + "/bad.json"); damaged.open(QIODevice::WriteOnly); damaged.write("{broken"); damaged.close();
    PlaylistModel invalid(root + "/bad.json"); check(invalid.count() == 0 && !invalid.status().isEmpty(), "invalid JSON handled");
    check(!invalid.save(), "a playlist that did not load is never overwritten");
    QFile keptDamaged(root + "/bad.json");
    check(keptDamaged.open(QIODevice::ReadOnly) && keptDamaged.readAll() == "{broken",
        "the unreadable playlist file keeps its bytes");
    model.clear(); check(model.count() == 0 && !model.busy(), "clear leaves no list or scan state");
    model.addPaths({folder}); model.clear(); model.addPaths({root + "/other"});
    check(wait([&] { return !model.busy(); }) && model.count() == 1 && name(model, 0) == "extra.mp4", "clear cancels stale queued scan results");
    model.clear(); model.addPaths({folder}); model.addPaths({root + "/other"});
    check(wait([&] { return !model.busy(); }) && model.count() == 322, "queued folder jobs both append");
    { PlaylistModel closing; closing.addPaths({folder}); } // joins without use-after-free callbacks
    QCoreApplication::processEvents();
    check(!model.move(-1, 1) && !model.remove(9000) && !model.play(-1), "invalid selections safely rejected");
    std::printf("%s playlist assertions=%d importReturnMs=%lld scan321Ms=%lld uiHeartbeats=%d\n",
                failures ? "FAIL" : "PASS", assertions, submitted, scanned, heartbeats);
    return failures ? 1 : 0;
}
