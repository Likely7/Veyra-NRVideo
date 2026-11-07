#pragma once

#include <QByteArray>
#include <QString>

namespace veyra::ui {

// Where Qt's QML disk cache goes for this build.
//
// Qt keeps compiled QML in one folder per user (<cache>/qmlcache), shared by
// every install, and trusts a compiled file while the source keeps its path and
// modification time; it never looks at the contents. The release archives stamp
// every file 1980-01-01, so 2.1.3 extracted over a 2.1.2 folder kept running the
// 2.1.2 compiled pages: the RTX Video SR families were mapped to DLSS SR by the
// old ProPage (field log 2026-11-06). A folder named after the contents of the
// application's QML module keeps every build's compiled QML apart.
struct QmlDiskCacheChoice {
    // Folder for QML_DISK_CACHE_PATH; empty leaves Qt's setting alone.
    QString directory;
    // First 16 hex digits of the SHA-256 over the module's paths and contents.
    QString digest;
    int files = 0;
    qint64 bytes = 0;
    // Why no folder was chosen, for the log.
    QString note;
};

// moduleDir: the application's own QML module (<app>/qml/Veyra). Qt's modules
// need no digest: a Qt upgrade changes the Qt version and compile hash that Qt
// already checks. cacheRoot: the user's cache folder. explicitPath and
// disableCache: the current QML_DISK_CACHE_PATH and QML_DISABLE_DISK_CACHE,
// which are respected.
QmlDiskCacheChoice chooseQmlDiskCache(const QString& moduleDir, const QString& cacheRoot,
                                      const QByteArray& explicitPath, const QByteArray& disableCache);

} // namespace veyra::ui
