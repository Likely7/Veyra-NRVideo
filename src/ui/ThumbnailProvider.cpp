#include "veyra/ui/ThumbnailProvider.h"

#include <QImage>

#include "veyra/engine/PosterFrame.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cstdint>
#include <vector>

namespace veyra::ui {
namespace {
AVPixelFormat softwareFormat(AVCodecContext*, const AVPixelFormat* formats) {
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(*format);
        if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *format;
    }
    return AV_PIX_FMT_NONE;
}
} // namespace

ThumbnailProvider::ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

void ThumbnailProvider::setSource(const QString& path) {
    std::lock_guard lock(mutex_);
    source_ = path;
}

QImage ThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize&) {
    if (size) *size = {};
    QString path;
    {
        std::lock_guard lock(mutex_);
        path = source_;
    }
    bool validTime = false;
    const int64_t milliseconds = id.section(QLatin1Char('/'), 1, 1).toLongLong(&validTime);
    if (path.isEmpty() || !validTime || milliseconds < 0) return {};

    AVFormatContext* format = nullptr;
    AVCodecContext* decoder = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* candidate = nullptr;
    QImage result;
    const QByteArray encodedPath = path.toUtf8();
    if (avformat_open_input(&format, encodedPath.constData(), nullptr, nullptr) < 0) goto done;
    if (avformat_find_stream_info(format, nullptr) < 0) goto done;
    {
        const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIndex < 0) goto done;
        AVStream* stream = format->streams[streamIndex];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) goto done;
        decoder = avcodec_alloc_context3(codec);
        if (!decoder || avcodec_parameters_to_context(decoder, stream->codecpar) < 0) goto done;
        decoder->get_format = softwareFormat;
        if (avcodec_open2(decoder, codec, nullptr) < 0) goto done;
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        candidate = av_frame_alloc();
        if (!packet || !frame || !candidate) goto done;

        const int64_t target = av_rescale_q(milliseconds, AVRational{1, 1000}, stream->time_base) +
            (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time);
        if (av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD) < 0) goto done;
        avcodec_flush_buffers(decoder);

        bool reached = false;
        // Bounded work: a long GOP can be expensive, but never blocks playback.
        for (int n = 0; n < 240 && !reached && av_read_frame(format, packet) >= 0; ++n) {
            if (packet->stream_index == streamIndex && avcodec_send_packet(decoder, packet) >= 0) {
                while (avcodec_receive_frame(decoder, frame) >= 0) {
                    av_frame_unref(candidate);
                    if (av_frame_ref(candidate, frame) < 0) break;
                    const int64_t pts = frame->best_effort_timestamp;
                    reached = pts != AV_NOPTS_VALUE && pts >= target;
                    av_frame_unref(frame);
                    if (reached) break;
                }
            }
            av_packet_unref(packet);
        }
        if (candidate->width > 0 && candidate->height > 0) {
            std::vector<uint8_t> rgba;
            uint32_t width = 0, height = 0;
            if (engine::makePosterFrame(candidate, 144, 81, rgba, width, height))
                result = QImage(rgba.data(), int(width), int(height), int(width * 4), QImage::Format_RGBA8888).copy();
        }
    }
done:
    av_frame_free(&candidate);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    if (size) *size = result.size();
    return result;
}

} // namespace veyra::ui
