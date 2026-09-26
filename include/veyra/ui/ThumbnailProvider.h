#pragma once

#include <QQuickImageProvider>
#include <QString>

#include <mutex>

namespace veyra::ui {

// Image.asynchronous keeps demux and software decode off the QML render thread.
// This decoder owns its own FFmpeg context and never reads the playback swapchain.
class ThumbnailProvider final : public QQuickImageProvider {
public:
    ThumbnailProvider();
    void setSource(const QString& path);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::mutex mutex_;
    QString source_;
};

} // namespace veyra::ui
