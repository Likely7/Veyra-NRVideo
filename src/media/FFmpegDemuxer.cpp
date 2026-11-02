#include "veyra/media/FFmpegDemuxer.h"

#include <windows.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/dict.h>
#include <cmath>
#include <cstdlib>
}

#include <format>

#include "veyra/Log.h"

namespace veyra::media {

namespace {
// Custom (P5): count the HEVC layers inside one access unit. Dolby Vision
// Profile 7 carries the enhancement layer as a second NAL layer inside the same
// access unit; the hardware decoder drops it, so this is the only proof that the
// data reaches us at all. Gated by VEYRA_DOVI_EL_TRACE, never on in production.
struct HevcLayerScan { unsigned nal=0, enhancement=0, maxLayer=0; };

// Matroska stores HEVC length-prefixed (hvcC), not as Annex B start codes: the
// prefix width lives in byte 21 of the configuration record.
unsigned hevcLengthSize(const AVCodecParameters* par) {
    const uint8_t* ext = par ? par->extradata : nullptr;
    const int size = par ? par->extradata_size : 0;
    if (ext && size > 21 && ext[0] == 1) return unsigned(ext[21] & 3) + 1u;
    return 0;
}

void countOneNal(const uint8_t* nal, unsigned available, HevcLayerScan& out) {
    if (nal == nullptr || available < 2) return;
    // HEVC NAL header: forbidden(1) type(6) layer(6) tid+1(3)
    const unsigned type = (unsigned(nal[0]) >> 1) & 0x3F;
    const unsigned layer = ((unsigned(nal[0]) & 1u) << 5) | (unsigned(nal[1]) >> 3);
    if (type < 1 || type > 63) return;
    ++out.nal;
    if (layer > 0) ++out.enhancement;
    if (layer > out.maxLayer) out.maxLayer = layer;
}

HevcLayerScan scanHevcLayers(const uint8_t* data, int size, unsigned lengthSize) {
    HevcLayerScan out;
    if (data == nullptr || size < 6) return out;
    if (lengthSize >= 1 && lengthSize <= 4) {
        int pos = 0;
        while (pos + int(lengthSize) + 2 <= size) {
            uint32_t length = 0;
            for (unsigned b = 0; b < lengthSize; ++b) length = (length << 8) | data[pos + b];
            pos += int(lengthSize);
            if (length == 0 || pos + int(length) > size) break;
            countOneNal(data + pos, length, out);
            pos += int(length);
        }
        return out;
    }
    // Annex B fallback (raw .hevc input): start-code delimited.
    for (int i = 0; i + 2 < size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1) continue;
        countOneNal(data + i + 3, unsigned(size - i - 3), out);
        i += 2;
    }
    return out;
}

// Rotation from the "rotate" tag or the display matrix in the stream's side data.
int rotationFrom(const AVCodecParameters* parameters, const char* rotateTag)
{
    // av_display_rotation_get reads the container's display matrix (the same
    // metadata players use); it returns degrees counter-clockwise.
    if (rotateTag != nullptr && *rotateTag != '\0') {
        const int value = std::atoi(rotateTag);
        if (value != 0) return ((value % 360) + 360) % 360;
    }
    if (parameters == nullptr) return 0;
    // FFmpeg 6+ exposes stream side data through the packet-side-data API.
    const AVPacketSideData* side = parameters->coded_side_data;
    const int sideCount = parameters->nb_coded_side_data;
    const uint8_t* matrix = nullptr;
    for (int i = 0; i < sideCount; ++i) {
        if (side[i].type == AV_PKT_DATA_DISPLAYMATRIX && side[i].size >= 9 * int(sizeof(int32_t))) { matrix = side[i].data; break; }
    }
    if (matrix == nullptr) return 0;
    const double degrees = av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix));
    if (!std::isfinite(degrees)) return 0;
    int normalized = int(std::lround(-degrees)) % 360;
    if (normalized < 0) normalized += 360;
    return normalized;
}
} // namespace

FFmpegDemuxer::~FFmpegDemuxer()
{
    close();
}

bool FFmpegDemuxer::openShared(std::shared_ptr<DemuxSession> session)
{
    if (opened()) {
        close();
    }
    if (!session || session->videoStream() < 0) {
        return false;
    }
    packet_ = av_packet_alloc();
    if (packet_ == nullptr) {
        return false;
    }
    session_ = std::move(session);
    videoStreamIndex_ = session_->videoStream();
    stats_ = DemuxerStats{};
    const auto* stream = session_->stream(videoStreamIndex_);
    log::info("media", std::format("demuxer: opened shared session codecId={} streams={} videoStream={} durationUs={} timeBase={}/{} format={}",
        stream && stream->parameters ? static_cast<int>(stream->parameters->codec_id) : -1, session_->streams().size(), videoStreamIndex_,
        session_->durationUs(), stream ? stream->timeBaseNum : 0, stream ? stream->timeBaseDen : 0, session_->formatName()));
    return true;
}

bool FFmpegDemuxer::open(const std::wstring& path,const std::atomic<bool>* cancelled)
{
    if (opened()) {
        close();
    }

    // avformat_open_input takes a UTF-8 path; convert from wide first.
    std::string utf8Path;
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
        utf8Path.resize(static_cast<size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), utf8Path.data(), size, nullptr, nullptr);
    }
    AVFormatContext* raw = avformat_alloc_context();
    if(!raw)return false;
    const bool network=networkMediaInput(path);
    interrupt_.configure(network,cancelled);
    raw->interrupt_callback={InputInterrupt::callback,&interrupt_};
    AVDictionary* options=nullptr;
    if(network)networkMediaOptions(&options);
    interrupt_.armConnect();
    const int openResult = avformat_open_input(&raw, utf8Path.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (openResult < 0) {
        char errorText[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(openResult, errorText, sizeof(errorText));
        log::error("media", std::format("demuxer: avformat_open_input failed code={} text={}", openResult, errorText));
        return false;
    }

    interrupt_.armProbe();
    const int infoResult=avformat_find_stream_info(raw, nullptr);
    interrupt_.disarm();
    if (infoResult < 0) {
        log::error("media", "demuxer: avformat_find_stream_info failed");
        avformat_close_input(&raw);
        return false;
    }

    const int streamIndex = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        log::error("media", "demuxer: no video stream");
        avformat_close_input(&raw);
        return false;
    }

    packet_ = av_packet_alloc();
    if (packet_ == nullptr) {
        avformat_close_input(&raw);
        return false;
    }

    context_ = raw;
    videoStreamIndex_ = streamIndex;
    stats_ = DemuxerStats{};
    const AVStream* stream = raw->streams[streamIndex];
    log::info("media", std::format("demuxer: opened codecId={} streams={} videoStream={} durationUs={} timeBase={}/{}",
        static_cast<int>(stream->codecpar->codec_id), raw->nb_streams, streamIndex,
        stream->duration >= 0 ? stream->duration : raw->duration,
        stream->time_base.num, stream->time_base.den));
    return true;
}

void FFmpegDemuxer::close()
{
    if (packet_ != nullptr) {
        av_packet_free(&packet_);
    }
    if (context_ != nullptr) {
        avformat_close_input(&context_);
    }
    session_.reset();
    videoStreamIndex_ = -1;
    havePendingPacket_ = false;
    packetDiscontinuity_ = false;
}

int64_t FFmpegDemuxer::durationUs() const
{
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        if (stream && stream->duration > 0 && stream->timeBaseDen > 0) {
            return av_rescale_q(stream->duration, { stream->timeBaseNum, stream->timeBaseDen }, { 1, 1000000 });
        }
        return session_->durationUs();
    }
    if (context_ == nullptr) {
        return 0;
    }
    const AVStream* stream = context_->streams[videoStreamIndex_];
    if (stream->duration > 0 && stream->time_base.den > 0) {
        return av_rescale_q(stream->duration, stream->time_base, { 1, 1000000 });
    }
    return context_->duration; // AV_TIME_BASE units == microseconds
}

bool FFmpegDemuxer::seekable() const {
    if (session_) return session_->seekable();
    return context_&&context_->pb&&(context_->pb->seekable&AVIO_SEEKABLE_NORMAL);
}

double FFmpegDemuxer::averageFps() const
{
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        if (!stream || stream->avgRateDen == 0) return 0.0;
        return static_cast<double>(stream->avgRateNum) / stream->avgRateDen;
    }
    if (context_ == nullptr) {
        return 0.0;
    }
    const AVStream* stream = context_->streams[videoStreamIndex_];
    if (stream->avg_frame_rate.den == 0) {
        return 0.0;
    }
    return static_cast<double>(stream->avg_frame_rate.num) / stream->avg_frame_rate.den;
}

int FFmpegDemuxer::nominalRateNum() const {
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        return stream ? stream->nominalRateNum : 0;
    }
    return context_ && videoStreamIndex_ >= 0 ? context_->streams[videoStreamIndex_]->r_frame_rate.num : 0;
}
int FFmpegDemuxer::nominalRateDen() const {
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        return stream ? stream->nominalRateDen : 0;
    }
    return context_ && videoStreamIndex_ >= 0 ? context_->streams[videoStreamIndex_]->r_frame_rate.den : 0;
}

void FFmpegDemuxer::sampleAspect(int& num, int& den) const
{
    num = 0; den = 0;
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        if (!stream) return;
        if (stream->sarNum > 0 && stream->sarDen > 0) { num = stream->sarNum; den = stream->sarDen; return; }
        const auto* parameters = stream->parameters.get();
        if (parameters && parameters->sample_aspect_ratio.num > 0 && parameters->sample_aspect_ratio.den > 0) {
            num = parameters->sample_aspect_ratio.num; den = parameters->sample_aspect_ratio.den;
        }
        return;
    }
    if (context_ == nullptr || videoStreamIndex_ < 0) return;
    const AVRational sar = context_->streams[videoStreamIndex_]->sample_aspect_ratio;
    if (sar.num > 0 && sar.den > 0) { num = sar.num; den = sar.den; return; }
    // Fall back to the codec parameters when the stream field is unset.
    const AVRational coded = context_->streams[videoStreamIndex_]->codecpar->sample_aspect_ratio;
    if (coded.num > 0 && coded.den > 0) { num = coded.num; den = coded.den; }
}

int FFmpegDemuxer::rotationDegrees() const
{
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        return stream ? rotationFrom(stream->parameters.get(), stream->rotate.c_str()) : 0;
    }
    if (context_ == nullptr || videoStreamIndex_ < 0) return 0;
    const AVStream* stream = context_->streams[videoStreamIndex_];
    const AVDictionaryEntry* rotate = av_dict_get(stream->metadata, "rotate", nullptr, 0);
    return rotationFrom(stream->codecpar, rotate != nullptr ? rotate->value : nullptr);
}

const AVCodecParameters* FFmpegDemuxer::videoCodecParameters() const
{
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        return stream ? stream->parameters.get() : nullptr;
    }
    if (context_ == nullptr || videoStreamIndex_ < 0) {
        return nullptr;
    }
    return context_->streams[videoStreamIndex_]->codecpar;
}

int FFmpegDemuxer::videoTimeBaseNum() const
{
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        return stream ? stream->timeBaseNum : 0;
    }
    if (context_ == nullptr || videoStreamIndex_ < 0) {
        return 0;
    }
    return context_->streams[videoStreamIndex_]->time_base.num;
}

int FFmpegDemuxer::videoTimeBaseDen() const
{
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        return stream ? stream->timeBaseDen : 0;
    }
    if (context_ == nullptr || videoStreamIndex_ < 0) {
        return 0;
    }
    return context_->streams[videoStreamIndex_]->time_base.den;
}

std::string FFmpegDemuxer::formatName() const
{
    if (session_) {
        return session_->formatName();
    }
    if (context_ == nullptr || context_->iformat == nullptr || context_->iformat->name == nullptr) {
        return {};
    }
    return context_->iformat->name;
}

void FFmpegDemuxer::notePacket()
{
    ++stats_.packetsRead;
    AVRational base{};
    if (session_) {
        const auto* stream = session_->stream(videoStreamIndex_);
        base = stream ? AVRational{stream->timeBaseNum, stream->timeBaseDen} : AVRational{1, 1000000};
    } else {
        base = context_->streams[videoStreamIndex_]->time_base;
    }
    const int64_t ptsUs = av_rescale_q(packet_->pts != AV_NOPTS_VALUE ? packet_->pts : packet_->dts, base, { 1, 1000000 });
    if (stats_.packetsRead == 1) {
        stats_.firstPts = ptsUs;
    }
    else if (ptsUs < stats_.lastPts) {
        ++stats_.ptsNonMonotonicCount;
    }
    stats_.lastPts = ptsUs;
}

bool FFmpegDemuxer::readVideoPacket(bool& endOfFile)
{
    endOfFile = false;
    if (session_) {
        // Offline / blocking consumers: wait for the session, ignore the buffering hold.
        for (;;) {
            const auto result = readVideoPacketEx(50, false);
            switch (result) {
            case ReadResult::Packet: return true;
            case ReadResult::Eof: endOfFile = true; return false;
            case ReadResult::Waiting: continue;
            default: return false;
            }
        }
    }
    if (context_ == nullptr) {
        return false;
    }
    for (;;) {
        // Explicit ownership: release the previous packet's buffers before
        // reading the next one. av_read_frame would do this implicitly, but
        // the demuxer owns packet_ and makes that release unconditional and
        // observable here (Playbook: every path frees exactly once - the
        // EOF/error paths below receive a blank packet either way).
        av_packet_unref(packet_);
        interrupt_.arm();
        const int result = av_read_frame(context_, packet_);
        interrupt_.disarm();
        if (result == AVERROR_EOF) {
            endOfFile = true;
            return false;
        }
        if (result < 0) {
            char errorText[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(result, errorText, sizeof(errorText));
            log::error("media", std::format("demuxer: av_read_frame failed code={} text={}", result, errorText));
            return false;
        }
        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }
        notePacket();
        // Custom (P5): does the enhancement layer reach us inside the packet?
        if (std::getenv("VEYRA_DOVI_EL_TRACE") != nullptr) {
            static const unsigned lengthSize = hevcLengthSize(context_->streams[videoStreamIndex_]->codecpar);
            const auto scan = scanHevcLayers(packet_->data, packet_->size, lengthSize);
            static unsigned withEl = 0, withoutEl = 0, logged = 0;
            if (scan.enhancement) ++withEl; else ++withoutEl;
            if (logged < 4 || stats_.packetsRead % 200 == 0) {
                ++logged;
                log::info("dovi-el", std::format(
                    "packet={} bytes={} lengthSize={} nal={} enhancementNal={} maxLayer={} withEl={} withoutEl={}",
                    stats_.packetsRead, packet_->size, lengthSize, scan.nal, scan.enhancement, scan.maxLayer, withEl, withoutEl));
            }
            // Matroska can carry the dependent layer as a BlockAdditional; FFmpeg
            // surfaces those as packet side data. Report them so the next step is
            // decided by evidence instead of guesswork.
            static unsigned sideReported = 0;
            if (sideReported < 3) {
                ++sideReported;
                log::info("dovi-el", std::format("  packet={} sideDataElems={}", stats_.packetsRead, packet_->side_data_elems));
                for (int i = 0; i < packet_->side_data_elems; ++i)
                    log::info("dovi-el", std::format("    side[{}] type={} size={}", i,
                        static_cast<int>(packet_->side_data[i].type), packet_->side_data[i].size));
            }
        }
        return true;
    }
}

FFmpegDemuxer::ReadResult FFmpegDemuxer::readVideoPacketEx(int waitMs, bool honourHold)
{
    if (!session_) {
        bool eof = false;
        if (readVideoPacket(eof)) return ReadResult::Packet;
        return eof ? ReadResult::Eof : ReadResult::Error;
    }
    av_packet_unref(packet_);
    DemuxSession::Meta meta;
    switch (session_->popVideo(packet_, meta, waitMs, honourHold)) {
    case DemuxSession::Pop::Packet:
        packetDiscontinuity_ = meta.discontinuity;
        notePacket();
        return ReadResult::Packet;
    case DemuxSession::Pop::Empty: return ReadResult::Waiting;
    case DemuxSession::Pop::Eof: return ReadResult::Eof;
    case DemuxSession::Pop::Cancelled: return ReadResult::Cancelled;
    case DemuxSession::Pop::Failed: break;
    }
    return ReadResult::Error;
}

bool FFmpegDemuxer::seekToUs(int64_t targetUs)
{
    if (session_) {
        if (!session_->seek(targetUs)) {
            log::error("media", std::format("demuxer: session seek failed targetUs={}", targetUs));
            return false;
        }
        packetDiscontinuity_ = false;
        return true;
    }
    if (context_ == nullptr) {
        return false;
    }
    const int64_t target = av_rescale_q(targetUs, { 1, 1000000 },
        context_->streams[videoStreamIndex_]->time_base);
    interrupt_.arm();
    const int result = av_seek_frame(context_, videoStreamIndex_, target, AVSEEK_FLAG_BACKWARD);
    interrupt_.disarm();
    if (result < 0) {
        char errorText[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, errorText, sizeof(errorText));
        log::error("media", std::format("demuxer: seek failed targetUs={} code={} text={}", targetUs, result, errorText));
        return false;
    }
    return true;
}

} // namespace veyra::media
