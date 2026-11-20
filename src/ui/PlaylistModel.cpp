#include "veyra/ui/PlaylistModel.h"
#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <utility>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace veyra::ui {
namespace {
constexpr int maxEntries = 10000, maxJobs = 16, maxVisited = 100000;
bool reparse(const QFileInfo& info) {
    if (info.isSymLink()) return true;
#ifdef Q_OS_WIN
    const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()));
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}
}

PlaylistModel::PlaylistModel(QString storage, QObject* parent)
    : QAbstractListModel(parent), storage_(std::move(storage)) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(350);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { save(); });
    load();
    worker_ = std::jthread([this](std::stop_token stop) { workerLoop(stop); });
}
PlaylistModel::~PlaylistModel() {
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (saveTimer_.isActive()) save();
}
int PlaylistModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QString PlaylistModel::normalizedPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(QDir::fromNativeSeparators(path)).absoluteFilePath());
}
QString PlaylistModel::keyOf(const QString& path) {
    auto value = normalizedPath(path);
#ifdef Q_OS_WIN
    value = value.toCaseFolded();
#endif
    return value;
}
bool PlaylistModel::supported(const QString& path) {
    static const QSet<QString> suffixes{
        "mp4", "mkv", "mov", "avi", "webm", "ts", "m2ts", "mts", "m4v", "wmv",
        "mpg", "mpeg", "vob", "flv", "ogv", "asf", "3gp", "jpg", "jpeg", "png", "bmp", "webp"
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}
QVariant PlaylistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto& entry = entries_[index.row()];
    switch (role) {
    case Qt::DisplayRole: case NameRole: return entry.name;
    case PathRole: return QDir::toNativeSeparators(entry.path);
    case SizeRole: return QLocale().formattedDataSize(entry.size);
    case ModifiedRole: return QDateTime::fromMSecsSinceEpoch(entry.modified).toString("yyyy-MM-dd HH:mm");
    case CurrentRole: return entry.key == currentKey_;
    case StateRole: return entry.key == currentKey_ ? state_ : QString();
    default: return {};
    }
}
QHash<int, QByteArray> PlaylistModel::roleNames() const {
    return {{NameRole, "entryName"}, {PathRole, "entryPath"}, {SizeRole, "entrySize"},
            {ModifiedRole, "entryModified"}, {CurrentRole, "entryCurrent"}, {StateRole, "entryState"}};
}
int PlaylistModel::currentIndex() const {
    if (!currentKey_.isEmpty())
        for (int i = 0; i < count(); ++i) if (entries_[i].key == currentKey_) return i;
    return -1;
}
void PlaylistModel::setPlayback(const QString& path, const QString& state) {
    const QString key = path.isEmpty() ? QString() : keyOf(path);
    if (key == currentKey_ && state == state_) return;
    const int before = currentIndex();
    currentKey_ = key; state_ = state;
    const int after = currentIndex();
    if (before >= 0) emit dataChanged(index(before), index(before), {CurrentRole, StateRole});
    if (after >= 0 && after != before) emit dataChanged(index(after), index(after), {CurrentRole, StateRole});
    emit currentChanged();
}
void PlaylistModel::setAutoAdvance(bool enabled) {
    if (autoAdvance_ == enabled) return;
    autoAdvance_ = enabled; emit autoAdvanceChanged(); saveTimer_.start();
}
void PlaylistModel::setStatus(QString text) {
    if (status_ == text) return;
    status_ = std::move(text); emit statusChanged();
}
void PlaylistModel::addUrls(const QList<QUrl>& urls) {
    QStringList paths;
    for (const auto& url : urls) {
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    addPaths(paths);
}
void PlaylistModel::addPaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    if (pending_ >= maxJobs || count() >= maxEntries) {
        emit error(tr("列表或导入任务已达上限，请稍后重试或移除部分条目")); return;
    }
    // No directory/file existence checks here: a network folder can be slow.
    Job job; job.generation = generation_.load();
    for (const auto& path : paths) {
        if (!path.trimmed().isEmpty() && job.paths.size() < maxEntries) job.paths << normalizedPath(path);
    }
    if (job.paths.isEmpty()) return;
    const bool wasBusy = busy(); ++pending_;
    { std::lock_guard lock(mutex_); jobs_.push_back(std::move(job)); }
    if (!wasBusy) emit busyChanged();
    setStatus(tr("正在扫描文件夹…"));
    wake_.notify_one();
}
void PlaylistModel::order(std::vector<Entry>& entries, int mode) {
    QCollator collator; collator.setNumericMode(true); collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        if ((mode == NewestFirst || mode == OldestFirst) && a.modified != b.modified)
            return mode == NewestFirst ? a.modified > b.modified : a.modified < b.modified;
        if ((mode == LargestFirst || mode == SmallestFirst) && a.size != b.size)
            return mode == LargestFirst ? a.size > b.size : a.size < b.size;
        const int compared = collator.compare(a.name, b.name);
        if (compared) return mode == NameDescending ? compared > 0 : compared < 0;
        return a.key < b.key;
    });
}
PlaylistModel::Result PlaylistModel::scan(const Job& job, std::stop_token stop) const {
    Result result;
    QSet<QString> visited, found;
    QStringList directories;
    int examined = 0;
    auto cancelled = [&] { return stop.stop_requested() || job.generation != generation_.load(); };
    auto file = [&](const QFileInfo& info) {
        if (!info.isFile() || !supported(info.filePath()) || reparse(info)) { ++result.skipped; return; }
        const auto path = normalizedPath(info.absoluteFilePath()), key = keyOf(path);
        if (found.contains(key)) return;
        found.insert(key);
        result.entries.push_back({path, info.fileName(), key, info.size(), info.lastModified().toMSecsSinceEpoch()});
    };
    for (const auto& path : job.paths) {
        if (cancelled()) return {};
        const QFileInfo info(path);
        if (info.isDir() && !reparse(info)) directories << path;
        else file(info);
        if (int(result.entries.size()) >= maxEntries) { result.limited = true; break; }
    }
    while (!directories.isEmpty() && !result.limited && !cancelled()) {
        const auto path = directories.takeLast(), key = keyOf(path);
        if (visited.contains(key)) continue;
        visited.insert(key);
        QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::NoIteratorFlags);
        while (it.hasNext() && !cancelled()) {
            it.next(); const QFileInfo info = it.fileInfo();
            if (++examined > maxVisited) { result.limited = true; break; }
            if (info.isDir()) {
                if (!reparse(info) && directories.size() < maxEntries) directories << info.absoluteFilePath();
            } else file(info);
            if (int(result.entries.size()) >= maxEntries) { result.limited = true; break; }
        }
    }
    if (!cancelled()) order(result.entries, NameAscending);
    return result;
}
void PlaylistModel::workerLoop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [this] { return !jobs_.empty(); })) return;
            job = std::move(jobs_.front()); jobs_.pop_front();
        }
        auto result = scan(job, stop);
        if (stop.stop_requested()) return;
        QMetaObject::invokeMethod(this, [this, result = std::move(result), generation = job.generation]() mutable {
            accept(std::move(result), generation);
        }, Qt::QueuedConnection);
    }
}
void PlaylistModel::accept(Result result, uint64_t generation) {
    if (generation != generation_.load()) return;
    --pending_;
    std::vector<Entry> added;
    for (auto& entry : result.entries) {
        if (keys_.contains(entry.key)) continue;
        if (count() + int(added.size()) >= maxEntries) { result.limited = true; break; }
        keys_.insert(entry.key); added.push_back(std::move(entry));
    }
    if (!added.empty()) {
        beginInsertRows({}, count(), count() + int(added.size()) - 1);
        for (auto& entry : added) entries_.push_back(std::move(entry));
        endInsertRows(); emit countChanged(); emit currentChanged();
        if (sort_ != Manual) sortBy(sort_);
        saveTimer_.start();
    }
    if (!busy()) emit busyChanged();
    setStatus(busy() ? tr("正在扫描文件夹…")
                    : tr("已添加 %1 项 · 列表共 %2 项").arg(added.size()).arg(count()));
    if (result.limited) emit error(tr("已达到导入上限（最多10000项），其余文件未添加"));
    else if (added.empty() && !busy()) setStatus(tr("未添加新条目：文件已在列表中，或没有支持的媒体文件"));
}
QString PlaylistModel::pathAt(int row) const { return row >= 0 && row < count() ? entries_[row].path : QString(); }
int PlaylistModel::indexOfPath(const QString& path) const {
    const auto key = keyOf(path);
    for (int i = 0; i < count(); ++i) if (entries_[i].key == key) return i;
    return -1;
}
bool PlaylistModel::play(int row) {
    if (row < 0 || row >= count()) return false;
    const QString path = entries_[row].path;
    if (!QFileInfo(path).isFile()) { emit error(tr("文件不存在或已移动：%1").arg(QDir::toNativeSeparators(path))); return false; }
    emit playRequested(QDir::toNativeSeparators(path)); return true;
}
bool PlaylistModel::playFrom(int row, int direction) {
    for (; row >= 0 && row < count(); row += direction) {
        // Skip missing entries without opening a failed engine session for each.
        if (QFileInfo(entries_[row].path).isFile()) return play(row);
    }
    return false;
}
bool PlaylistModel::next() { const int at = currentIndex(); return playFrom(at < 0 ? 0 : at + 1, 1); }
bool PlaylistModel::previous() { const int at = currentIndex(); return playFrom(at < 0 ? count() - 1 : at - 1, -1); }
bool PlaylistModel::advanceAfterEnd() { return autoAdvance_ && currentIndex() >= 0 && next(); }
bool PlaylistModel::move(int from, int to) {
    if (from < 0 || to < 0 || from >= count() || to >= count() || from == to) return false;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    auto entry = std::move(entries_[from]); entries_.erase(entries_.begin() + from);
    entries_.insert(entries_.begin() + to, std::move(entry)); endMoveRows();
    if (sort_ != Manual) { sort_ = Manual; emit sortModeChanged(); }
    emit currentChanged(); saveTimer_.start(); return true;
}
bool PlaylistModel::remove(int row) {
    if (row < 0 || row >= count()) return false;
    beginRemoveRows({}, row, row); keys_.remove(entries_[row].key); entries_.erase(entries_.begin() + row); endRemoveRows();
    emit countChanged(); emit currentChanged(); setStatus({}); saveTimer_.start(); return true;
}
void PlaylistModel::clear() {
    ++generation_;
    { std::lock_guard lock(mutex_); jobs_.clear(); }
    const bool wasBusy = busy(); pending_ = 0;
    beginResetModel(); entries_.clear(); keys_.clear(); endResetModel();
    if (wasBusy) emit busyChanged();
    emit countChanged(); emit currentChanged();
    setStatus({}); saveTimer_.start();
}
void PlaylistModel::sortBy(int mode) {
    if (mode < Manual || mode > SmallestFirst) return;
    if (mode != Manual && count() > 1) {
        // A layout change keeps the identity of any persistent model indices.
        emit layoutAboutToBeChanged();
        const auto indices = persistentIndexList(); QStringList paths;
        for (const auto& item : indices) paths << entries_[item.row()].key;
        order(entries_, mode);
        QModelIndexList replacements;
        for (const auto& key : paths) {
            const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e.key == key; });
            replacements << index(int(it - entries_.begin()));
        }
        changePersistentIndexList(indices, replacements); emit layoutChanged(); emit currentChanged();
    }
    if (sort_ != mode) { sort_ = mode; emit sortModeChanged(); }
    saveTimer_.start();
}
bool PlaylistModel::save() {
    saveTimer_.stop();
    if (storage_.isEmpty()) return true;
    if (unreadable_) return false;   // status_ already tells the user the file was not loaded
    QJsonArray array;
    for (const auto& entry : entries_) array.append(QJsonObject{{"path", entry.path}, {"size", double(entry.size)}, {"modified", double(entry.modified)}});
    QSaveFile file(storage_);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"sort", sort_}, {"autoAdvance", autoAdvance_}, {"entries", array}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit error(tr("播放列表保存失败：%1").arg(file.errorString())); return false;
    }
    return true;
}
void PlaylistModel::load() {
    if (storage_.isEmpty()) return;
    QFile file(storage_);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        unreadable_ = true; status_ = tr("播放列表文件无法读取，已使用空列表"); return;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = doc.object();
    if (error.error != QJsonParseError::NoError || object.value("version").toInt() != 1 || !object.value("entries").isArray()) {
        unreadable_ = true; status_ = tr("播放列表文件损坏，已使用空列表"); return;
    }
    sort_ = std::clamp(object.value("sort").toInt(), int(Manual), int(SmallestFirst));
    autoAdvance_ = object.value("autoAdvance").toBool(true);
    for (const auto& value : object.value("entries").toArray()) {
        const auto entry = value.toObject(); const auto path = entry.value("path").toString();
        if (path.isEmpty() || !QDir::isAbsolutePath(path) || !supported(path)) continue;
        const auto normalized = normalizedPath(path), key = keyOf(normalized);
        if (keys_.contains(key)) continue;
        keys_.insert(key);
        entries_.push_back({normalized, QFileInfo(normalized).fileName(), key,
                           std::max(qint64(0), entry.value("size").toInteger()),
                           entry.value("modified").toInteger()});
        if (count() >= maxEntries) break;
    }
}
}
