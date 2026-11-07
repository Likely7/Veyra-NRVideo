// The QML disk cache folder must change with the QML module's contents, even
// when a release archive gives the new files the old size and time stamp
// (2.1.3 over 2.1.2, 2026-11-06), and must leave explicit Qt settings alone.
#include "veyra/ui/QmlDiskCache.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <iostream>

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
// Writes like an archive extraction: fixed contents, then the archive's time stamp.
bool write(const QString& path, const QByteArray& contents, const QDateTime& stamp) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate)) return false;
    const bool written = file.write(contents) == contents.size();
    file.flush();
    return written && file.setFileTime(stamp, QFileDevice::FileModificationTime);
}
veyra::ui::QmlDiskCacheChoice choose(const QString& module, const QString& cacheRoot,
                                     const QByteArray& explicitPath = {}, const QByteArray& disable = {}) {
    return veyra::ui::chooseQmlDiskCache(module, cacheRoot, explicitPath, disable);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 2) { std::cerr << "usage: veyra_qml_cache_tests <new scratch directory>\n"; return 2; }
    const QString root = QDir::cleanPath(args.at(1));
    if (QFileInfo::exists(root)) { std::cerr << "scratch directory already exists\n"; return 2; }
    const QDateTime stamp(QDate(1980, 1, 1), QTime(0, 0));
    const QString a = root + "/a/qml/Veyra", b = root + "/b/qml/Veyra", cacheRoot = root + "/cache";

    bool ok = check(write(a + "/ProPage.qml", "Item { implicitWidth: 150 }\n", stamp)
                    && write(a + "/sub/Helper.js", "var picked = 1\n", stamp)
                    && write(a + "/qmldir", "module Veyra\n", stamp), "write module a");
    // The same files in another folder, written in another order.
    ok &= check(write(b + "/qmldir", "module Veyra\n", stamp)
                && write(b + "/sub/Helper.js", "var picked = 1\n", stamp)
                && write(b + "/ProPage.qml", "Item { implicitWidth: 150 }\n", stamp), "write module b");

    const auto first = choose(a, cacheRoot);
    ok &= check(first.files == 3 && first.digest.size() == 16 && first.note.isEmpty(), "a module gets a digest");
    ok &= check(QDir::cleanPath(first.directory) == cacheRoot + "/qmlcache-" + first.digest, "the folder sits in the cache root");
    ok &= check(choose(a, cacheRoot).directory == first.directory, "the same files give the same folder");
    ok &= check(choose(b, cacheRoot).directory == first.directory, "install folder and listing order do not matter");

    // 2.1.2 -> 2.1.3: same name, same size, same time stamp, new contents.
    ok &= check(write(a + "/ProPage.qml", "Item { implicitWidth: 210 }\n", stamp), "rewrite ProPage");
    ok &= check(QFileInfo(a + "/ProPage.qml").lastModified() == stamp && QFileInfo(a + "/ProPage.qml").size() == QFileInfo(b + "/ProPage.qml").size(),
                "the rewrite keeps the archive's size and time stamp");
    const auto changed = choose(a, cacheRoot);
    ok &= check(!changed.directory.isEmpty() && changed.directory != first.directory, "new contents get a new folder");

    ok &= check(QFile::rename(a + "/sub/Helper.js", a + "/sub/Helper2.js"), "rename a file");
    const auto renamed = choose(a, cacheRoot);
    ok &= check(renamed.directory != changed.directory, "a renamed file gets a new folder");
    ok &= check(write(a + "/NrWebPad.qml", "Item {}\n", stamp), "add a file");
    const auto added = choose(a, cacheRoot);
    ok &= check(added.files == 4 && added.directory != renamed.directory, "an added file gets a new folder");

    const auto kept = choose(a, cacheRoot, "D:/elsewhere");
    ok &= check(kept.directory.isEmpty() && kept.note.contains("QML_DISK_CACHE_PATH"), "an explicit QML_DISK_CACHE_PATH is kept");
    ok &= check(choose(a, cacheRoot, {}, "1").directory.isEmpty(), "a disabled disk cache stays disabled");
    ok &= check(!choose(a, cacheRoot, {}, "0").directory.isEmpty(), "QML_DISABLE_DISK_CACHE=0 is not a request to disable");
    const auto missing = choose(root + "/missing/qml/Veyra", cacheRoot);
    ok &= check(missing.directory.isEmpty() && missing.note.contains("no QML module"), "no module keeps Qt's default");
    ok &= check(choose(a, QString()).directory.isEmpty(), "no cache folder keeps Qt's default");

    std::cout << (ok ? "PASS" : "FAIL") << " qml disk cache folder\n";
    return ok ? 0 : 1;
}
