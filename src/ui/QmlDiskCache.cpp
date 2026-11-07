#include "veyra/ui/QmlDiskCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStringList>

namespace veyra::ui {

QmlDiskCacheChoice chooseQmlDiskCache(const QString& moduleDir, const QString& cacheRoot,
                                      const QByteArray& explicitPath, const QByteArray& disableCache) {
    QmlDiskCacheChoice choice;
    if (!explicitPath.isEmpty()) {
        choice.note = QStringLiteral("QML_DISK_CACHE_PATH is set; kept");
        return choice;
    }
    if (!disableCache.isEmpty() && disableCache != "0") {
        choice.note = QStringLiteral("QML_DISABLE_DISK_CACHE is set; no disk cache");
        return choice;
    }
    if (cacheRoot.isEmpty()) {
        choice.note = QStringLiteral("no cache folder; Qt default kept");
        return choice;
    }
    const QDir module(moduleDir);
    if (!module.exists()) {
        choice.note = QStringLiteral("no QML module at ") + moduleDir + QStringLiteral("; Qt default kept");
        return choice;
    }

    QStringList files;
    QDirIterator it(moduleDir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) files << module.relativeFilePath(it.next());
    files.sort();   // the same tree gives the same digest whatever the listing order

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& name : files) {
        QFile file(module.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            choice.note = QStringLiteral("cannot read ") + name + QStringLiteral("; Qt default kept");
            return choice;
        }
        const QByteArray header = name.toUtf8() + '\0' + QByteArray::number(file.size()) + '\0';
        hash.addData(header);
        if (!hash.addData(&file)) {
            choice.note = QStringLiteral("cannot read ") + name + QStringLiteral("; Qt default kept");
            return choice;
        }
        ++choice.files;
        choice.bytes += file.size();
    }
    choice.digest = QString::fromLatin1(hash.result().toHex().left(16));
    choice.directory = QDir(cacheRoot).filePath(QStringLiteral("qmlcache-") + choice.digest);
    return choice;
}

} // namespace veyra::ui
