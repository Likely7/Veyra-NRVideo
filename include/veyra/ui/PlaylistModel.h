#pragma once
#include <QAbstractListModel>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QUrl>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace veyra::ui {
// UI-owned metadata only. Directory enumeration runs on one cancellable worker;
// playback continues through QmlPlayerBridge's existing openPath/transport.
class PlaylistModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(int sortMode READ sortMode NOTIFY sortModeChanged)
    Q_PROPERTY(bool autoAdvance READ autoAdvance WRITE setAutoAdvance NOTIFY autoAdvanceChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, SizeRole, ModifiedRole, CurrentRole, StateRole };
    enum Sort { Manual, NameAscending, NameDescending, NewestFirst, OldestFirst, LargestFirst, SmallestFirst };
    Q_ENUM(Sort)
    explicit PlaylistModel(QString storageFile = {}, QObject* parent = nullptr);
    ~PlaylistModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(entries_.size()); }
    bool busy() const { return pending_ > 0; }
    QString status() const { return status_; }
    int currentIndex() const;
    int sortMode() const { return sort_; }
    bool autoAdvance() const { return autoAdvance_; }
    void setAutoAdvance(bool enabled);
    static bool supported(const QString& path);
    static QString normalizedPath(const QString& path);
    bool containsPath(const QString& path) const { return keys_.contains(keyOf(path)); }
    void setPlayback(const QString& path, const QString& state);
    // Returns false at the ends: never silently wraps or restarts the same file.
    bool advanceAfterEnd();
    Q_INVOKABLE void addPaths(const QStringList& paths);
    Q_INVOKABLE void addUrls(const QList<QUrl>& urls);
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int indexOfPath(const QString& path) const;
    Q_INVOKABLE bool play(int row);
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();
    Q_INVOKABLE bool move(int from, int to);
    Q_INVOKABLE bool remove(int row);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void sortBy(int mode);
    Q_INVOKABLE bool save();
signals:
    void countChanged();
    void busyChanged();
    void statusChanged();
    void currentChanged();
    void sortModeChanged();
    void autoAdvanceChanged();
    void playRequested(const QString& path);
    void error(const QString& text);
private:
    struct Entry { QString path, name, key; qint64 size = 0, modified = 0; };
    struct Job { QStringList paths; uint64_t generation = 0; };
    struct Result { std::vector<Entry> entries; int skipped = 0; bool limited = false; };
    static QString keyOf(const QString& path);
    static void order(std::vector<Entry>& entries, int mode);
    Result scan(const Job& job, std::stop_token stop) const;
    void workerLoop(std::stop_token stop);
    void accept(Result result, uint64_t generation);
    void setStatus(QString text);
    void load();
    bool playFrom(int row, int direction);
    QString storage_, status_, currentKey_, state_;
    std::vector<Entry> entries_;
    QSet<QString> keys_;
    QTimer saveTimer_;
    int pending_ = 0, sort_ = Manual;
    bool autoAdvance_ = true;
    bool unreadable_ = false;   // the stored list was not loaded; never overwrite it with this one
    std::atomic<uint64_t> generation_{1};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Job> jobs_;
    // Declared last; destructor explicitly joins before QObject destruction.
    std::jthread worker_;
};
}
