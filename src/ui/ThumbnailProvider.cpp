#include "veyra/ui/ThumbnailProvider.h"

#include <QImage>

#include "veyra/engine/PosterFrame.h"
#include "veyra/Log.h"

#include <format>

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

// Ids:  "<gen>/<ms>"                   a frame of the playing source
//       "cover/<gen>"                   the playing source's embedded cover art
//       "file/<gen>/<base64url>/<ms>"   a frame of any file (export queue rows)
QImage ThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize&) {
    if (size) *size = {};
    QString path;
    {
        std::lock_guard lock(mutex_);
        path = source_;
    }
    QImage result;
    if (id.startsWith(QLatin1String("cover/"))) {
        result = coverArt(path);
        // No cover: a transparent pixel, so the plate stays black without the
        // image element reporting a provider failure for every file.
        if (result.isNull()) { result = QImage(1, 1, QImage::Format_ARGB32); result.fill(Qt::transparent); }
    } else if (id.startsWith(QLatin1String("file/"))) {
        bool ok = false;
        const int64_t ms = id.section(QLatin1Char('/'), 3, 3).toLongLong(&ok);
        const QString file = QString::fromUtf8(QByteArray::fromBase64(id.section(QLatin1Char('/'), 2, 2).toLatin1(),
                                                                      QByteArray::Base64UrlEncoding));
        if (ok && ms >= 0 && !file.isEmpty()) result = frameAt(file, ms);
    } else {
        bool validTime = false;
        const int64_t milliseconds = id.section(QLatin1Char('/'), 1, 1).toLongLong(&validTime);
        if (!path.isEmpty() && validTime && milliseconds >= 0) result = frameAt(path, milliseconds);
    }
    if (size) *size = result.size();
    return result;
}

// The attached picture a container carries (MP4 covr / ID3 APIC, and MKV
// image attachments, which FFmpeg exposes the same way). Decoded with FFmpeg so
// no Qt image plugin is involved. Empty when the file has none: the bar then
// stays black rather than showing a stand-in.
QImage ThumbnailProvider::coverArt(const QString& path) {
    QImage result;
    if (path.isEmpty()) return result;
    AVFormatContext* format = nullptr;
    AVCodecContext* decoder = nullptr;
    AVFrame* frame = nullptr;
    const QByteArray encodedPath = path.toUtf8();
    if (avformat_open_input(&format, encodedPath.constData(), nullptr, nullptr) < 0) return result;
    for (unsigned i = 0; i < format->nb_streams && result.isNull(); ++i) {
        AVStream* stream = format->streams[i];
        if (!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) || stream->attached_pic.size <= 0) continue;
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        // The bundled FFmpeg carries no PNG decoder: Qt reads the attached bytes
        // itself (PNG is built into QtGui; JPEG when the image plugin is present).
        const auto fromBytes = [&]() {
            QImage image = QImage::fromData(stream->attached_pic.data, stream->attached_pic.size);
            if (!image.isNull()) result = image.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            veyra::log::info("thumb-cover", std::format("stream={} qt-decoded={}", i, !result.isNull()));
        };
        if (!codec) { fromBytes(); continue; }
        decoder = avcodec_alloc_context3(codec);
        frame = av_frame_alloc();
        if (decoder) decoder->thread_count = 1;   // one picture: no frame threading to wait on
        int received = -1;
        if (decoder && frame && avcodec_parameters_to_context(decoder, stream->codecpar) >= 0 &&
            avcodec_open2(decoder, codec, nullptr) >= 0 &&
            avcodec_send_packet(decoder, &stream->attached_pic) >= 0) {
            // Drain: a single-picture decoder may hold its frame until the end of stream.
            received = avcodec_receive_frame(decoder, frame);
            if (received == AVERROR(EAGAIN)) { avcodec_send_packet(decoder, nullptr); received = avcodec_receive_frame(decoder, frame); }
        }
        veyra::log::info("thumb-cover", std::format("stream={} codec={} decoded={}", i, codec->name, received >= 0));
        if (received < 0) fromBytes();
        else {
            std::vector<uint8_t> rgba;
            uint32_t width = 0, height = 0;
            if (engine::makePosterFrame(frame, 160, 160, rgba, width, height))
                result = QImage(rgba.data(), int(width), int(height), int(width * 4), QImage::Format_RGBA8888).copy();
        }
        av_frame_free(&frame);
        avcodec_free_context(&decoder);
    }
    avformat_close_input(&format);
    return result;
}

QImage ThumbnailProvider::frameAt(const QString& path, int64_t milliseconds) {
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
    return result;
}

} // namespace veyra::ui
