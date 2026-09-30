#include "veyra/source/CaptureCompressedDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstring>
#include <emmintrin.h>
#include <format>

#include "veyra/Log.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/pipeline/CaptureUploadFrame.h"

namespace veyra::source {

namespace {

AVCodecID captureAvCodecId(CaptureCodec codec)
{
    switch (codec) {
    case CaptureCodec::Mjpeg: return AV_CODEC_ID_MJPEG;
    case CaptureCodec::H264: return AV_CODEC_ID_H264;
    case CaptureCodec::Hevc: return AV_CODEC_ID_HEVC;
    case CaptureCodec::Av1: return AV_CODEC_ID_AV1;
    case CaptureCodec::Vp9: return AV_CODEC_ID_VP9;
    case CaptureCodec::None: break;
    }
    return AV_CODEC_ID_NONE;
}

// swscale colour coefficients for the bitstream's declared matrix; the same
// coefficients are used for both sides so no hidden matrix conversion is
// introduced, only the range change.
int swsColorspaceOf(AVColorSpace space, unsigned height)
{
    switch (space) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: return SWS_CS_ITU601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
    default: break;
    }
    return height > 576 ? SWS_CS_ITU709 : SWS_CS_ITU601;
}

} // namespace

struct CaptureCompressedDecoder::Impl {
    CaptureCodec codec = CaptureCodec::None;
    unsigned width = 0, height = 0;
    media::FFmpegVideoDecoder decoder;
    AVCodecParameters* parameters = nullptr;
    SwsContext* sws = nullptr;
    bool hardwareRequested = false;
    bool hardware = false;
    bool hardwareChecked = false;
    bool waiting = false;
    bool depthWarningLogged = false;
    std::string error;
    uint64_t frames = 0, failures = 0, fallbacks = 0;
    double decodeMs = 0, convertMs = 0;

    ~Impl() { close(); }

    // MJPEG: full-range 4:2:2 / 4:2:0 planes of the target size go straight to
    // NV12 (luma row copies; chroma rows averaged in pairs for 4:2:2, then
    // interleaved). swscale's generic scaler did the same reformat several
    // times slower, and this writer never reads the target, so the target may
    // be write-combined GPU upload memory.
    bool planarToNv12(const AVFrame& frame, AVFrame* target)
    {
        const bool is422 = frame.format == AV_PIX_FMT_YUVJ422P || frame.format == AV_PIX_FMT_YUV422P;
        const bool is420 = frame.format == AV_PIX_FMT_YUVJ420P || frame.format == AV_PIX_FMT_YUV420P;
        if (codec != CaptureCodec::Mjpeg || !(is422 || is420) || frame.width != int(width) || frame.height != int(height) ||
            width % 2 || height % 2 || !frame.data[0] || !frame.data[1] || !frame.data[2] || !target->data[0] || !target->data[1] ||
            target->linesize[0] < int(width) || target->linesize[1] < int(width)) return false;
        for (unsigned y = 0; y < height; ++y)
            std::memcpy(target->data[0] + ptrdiff_t(y) * target->linesize[0], frame.data[0] + ptrdiff_t(y) * frame.linesize[0], width);
        const unsigned chromaWidth = width / 2;
        for (unsigned row = 0; row < height / 2; ++row) {
            const unsigned sourceRow = is422 ? row * 2 : row;
            const uint8_t* u0 = frame.data[1] + ptrdiff_t(sourceRow) * frame.linesize[1];
            const uint8_t* v0 = frame.data[2] + ptrdiff_t(sourceRow) * frame.linesize[2];
            const uint8_t* u1 = is422 ? u0 + frame.linesize[1] : u0;
            const uint8_t* v1 = is422 ? v0 + frame.linesize[2] : v0;
            uint8_t* out = target->data[1] + ptrdiff_t(row) * target->linesize[1];
            unsigned x = 0;
            for (; x + 16 <= chromaWidth; x += 16) {
                const __m128i u = _mm_avg_epu8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(u0 + x)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(u1 + x)));
                const __m128i v = _mm_avg_epu8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(v0 + x)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(v1 + x)));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 2 * x), _mm_unpacklo_epi8(u, v));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 2 * x + 16), _mm_unpackhi_epi8(u, v));
            }
            for (; x < chromaWidth; ++x) {
                out[2 * x] = uint8_t((unsigned(u0[x]) + u1[x] + 1) >> 1);
                out[2 * x + 1] = uint8_t((unsigned(v0[x]) + v1[x] + 1) >> 1);
            }
        }
        pipeline::sampleCaptureUploadProxy(*target, frame.data[0], size_t(frame.linesize[0]), false);
        return true;
    }

    void copyFrameProperties(const AVFrame& frame, AVFrame* target, bool targetFullRange)
    {
        target->pts = frame.pts;
        target->best_effort_timestamp = frame.best_effort_timestamp;
        target->pkt_dts = frame.pkt_dts;
        target->duration = frame.duration;
        target->time_base = frame.time_base;
        target->flags = frame.flags;
        target->colorspace = frame.colorspace;
        target->color_primaries = frame.color_primaries;
        target->color_trc = frame.color_trc;
        target->color_range = targetFullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }

    void close()
    {
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        if (parameters) { avcodec_parameters_free(&parameters); parameters = nullptr; }
        decoder.close();
        codec = CaptureCodec::None;
        width = height = 0;
        hardwareRequested = hardware = hardwareChecked = waiting = false;
        depthWarningLogged = false;
        frames = failures = fallbacks = 0;
        decodeMs = convertMs = 0;
        error.clear();
    }

    bool openSoftwareDecoder()
    {
        if (parameters == nullptr) return false;
        return decoder.openSoftware(parameters, 1, 10000000, 1, true);
    }

    bool convertToNv12(const AVFrame& frame, AVFrame* target)
    {
        AVPixelFormat sourceFormat = AVPixelFormat(frame.format);
        if (sourceFormat == AV_PIX_FMT_YUVJ422P) sourceFormat = AV_PIX_FMT_YUV422P;
        else if (sourceFormat == AV_PIX_FMT_YUVJ420P) sourceFormat = AV_PIX_FMT_YUV420P;
        else if (sourceFormat == AV_PIX_FMT_YUVJ444P) sourceFormat = AV_PIX_FMT_YUV444P;
        if (const auto* description = av_pix_fmt_desc_get(sourceFormat)) {
            if (description->comp[0].depth > 8 && !depthWarningLogged) {
                depthWarningLogged = true;
                log::warn("capture-decode", std::format(
                    "codec={} bitstream is {}-bit; converting to 8-bit NV12 because the capture ingress contract is 8-bit SDR (a P010/HDR capture contract is not implemented)",
                    captureCodecKey(codec), description->comp[0].depth));
            }
        }
        if (planarToNv12(frame, target)) { copyFrameProperties(frame, target, true); return true; }
        // swscale cannot supply the analysis pixels of an upload-backed target.
        pipeline::useCaptureUploadCpuPlanes(*target);
        sws = sws_getCachedContext(sws, frame.width, frame.height, sourceFormat,
            int(width), int(height), AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws == nullptr) {
            error = "swscale context creation failed";
            ++failures;
            return false;
        }
        const int* coefficients = sws_getCoefficients(swsColorspaceOf(frame.colorspace, unsigned(frame.height)));
        // MJPEG samples are full-range JPEG planes and the declared ingress
        // contract for that path is full-range NV12. Compressed video keeps
        // the bitstream's own range and lands on the standard limited NV12.
        const int sourceFullRange = (codec == CaptureCodec::Mjpeg || frame.color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
        const int targetFullRange = codec == CaptureCodec::Mjpeg ? 1 : 0;
        sws_setColorspaceDetails(sws, coefficients, sourceFullRange, coefficients, targetFullRange, 0, 1 << 16, 1 << 16);
        uint8_t* destination[4] = { target->data[0], target->data[1], nullptr, nullptr };
        const int destinationStride[4] = { target->linesize[0], target->linesize[1], 0, 0 };
        if (sws_scale(sws, frame.data, frame.linesize, 0, frame.height, destination, destinationStride) != frame.height) {
            error = "swscale conversion failed";
            ++failures;
            return false;
        }
        copyFrameProperties(frame, target, targetFullRange != 0);
        return true;
    }
};

CaptureCompressedDecoder::CaptureCompressedDecoder() : p_(std::make_unique<Impl>()) {}
CaptureCompressedDecoder::~CaptureCompressedDecoder() = default;

bool CaptureCompressedDecoder::open(CaptureCodec codec, unsigned width, unsigned height,
    const uint8_t* extradata, size_t extradataBytes, ID3D12Device* device, ID3D12CommandQueue* queue)
{
    auto& p = *p_;
    p.close();
    if (codec == CaptureCodec::None || !width || !height) return false;
    p.codec = codec;
    p.width = width;
    p.height = height;
    p.parameters = avcodec_parameters_alloc();
    if (p.parameters == nullptr) {
        p.error = "avcodec_parameters_alloc failed";
        return false;
    }
    p.parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    p.parameters->codec_id = captureAvCodecId(codec);
    p.parameters->width = int(width);
    p.parameters->height = int(height);
    if (extradata != nullptr && extradataBytes != 0) {
        p.parameters->extradata = static_cast<uint8_t*>(av_mallocz(extradataBytes + AV_INPUT_BUFFER_PADDING_SIZE));
        if (p.parameters->extradata == nullptr) {
            p.error = "extradata allocation failed";
            p.close();
            return false;
        }
        std::memcpy(p.parameters->extradata, extradata, extradataBytes);
        p.parameters->extradata_size = int(extradataBytes);
    }
    // MJPEG stays software: UVC drivers disagree about 4:2:0 vs 4:2:2 and the
    // hardware path only exists for 4:2:0 on one vendor. Everything else gets
    // the D3D12VA session on the shared Veyra device, with the software
    // decoder as the recorded fallback.
    if (codec != CaptureCodec::Mjpeg && device != nullptr && queue != nullptr) {
        p.hardwareRequested = true;
        if (p.decoder.openD3D12VA(p.parameters, 1, 10000000, device, queue, true)) {
            p.hardware = true;
        } else {
            log::warn("capture-decode", std::format(
                "D3D12VA open failed codec={}; falling back to the software decoder", captureCodecKey(codec)));
        }
    }
    if (!p.hardware && !p.openSoftwareDecoder()) {
        p.error = std::format("no usable decoder for codec={}", captureCodecKey(codec));
        log::error("capture-decode", p.error);
        p.close();
        return false;
    }
    log::info("capture-decode", std::format(
        "compressed decoder opened codec={} {}x{} backend={} extradataBytes={}",
        captureCodecKey(codec), width, height, backendName(), extradataBytes));
    return true;
}

bool CaptureCompressedDecoder::decode(const uint8_t* data, size_t bytes, int64_t pts100ns,
    AVFrame* nv12Target, AVFrame** out, bool& hardware)
{
    auto& p = *p_;
    *out = nullptr;
    hardware = false;
    p.waiting = false;
    const bool receiveOnly = data == nullptr && bytes == 0;
    if (!p.decoder.opened() || (!receiveOnly && (data == nullptr || bytes == 0)) || nv12Target == nullptr) {
        p.error = "decode called without an open decoder or a writable target frame";
        ++p.failures;
        return false;
    }
    // Two attempts at most: the second one re-sends the same payload after a
    // hardware decoder whose first frame cannot be imported has been replaced
    // by the software decoder.
    const auto started = std::chrono::steady_clock::now();
    const auto average = [](double& value, std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
        const double ms = std::chrono::duration<double, std::milli>(to - from).count();
        value = value > 0 ? value * 0.95 + ms * 0.05 : ms;
    };
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!receiveOnly) {
            AVPacket* packet = av_packet_alloc();
            if (packet == nullptr) {
                p.error = "av_packet_alloc failed";
                ++p.failures;
                return false;
            }
            if (av_new_packet(packet, int(bytes)) < 0) {
                av_packet_free(&packet);
                p.error = "av_new_packet failed";
                ++p.failures;
                return false;
            }
            std::memcpy(packet->data, data, bytes);
            packet->pts = pts100ns;
            packet->dts = AV_NOPTS_VALUE; // capture PTS is not decode order for B frames
            const bool sent = p.decoder.sendPacket(packet);
            av_packet_free(&packet);
            if (!sent) {
                p.error = "decoder rejected the compressed payload";
                ++p.failures;
                return false;
            }
        }
        const AVFrame* frame = p.decoder.receiveFrame();
        if (frame == nullptr) {
            if (p.decoder.receiveStatus() == media::DecodeReceiveStatus::Error) {
                p.error = "decoder receive failed";
                ++p.failures;
            } else {
                p.error.clear();
                p.waiting = true;
            }
            return false;
        }
        if (p.hardware && !p.hardwareChecked) {
            p.hardwareChecked = true;
            if (!p.decoder.hardwareFrameImportable()) {
                log::warn("capture-decode", std::format(
                    "codec={} hardware frame is not an importable NV12/P010 surface; switching to the software decoder",
                    captureCodecKey(p.codec)));
                p.decoder.close();
                if (!p.openSoftwareDecoder()) {
                    p.error = "software fallback after an unimportable hardware frame failed";
                    ++p.failures;
                    return false;
                }
                p.hardware = false;
                p.decoder.recoverAtKeyframe();
                ++p.fallbacks;
                if (receiveOnly) { p.waiting=true; return false; }
                continue; // only a new key frame can rebuild the lost reference chain
            }
        }
        if (p.hardware) {
            *out = const_cast<AVFrame*>(frame);
            hardware = true;
            ++p.frames;
            p.error.clear();
            return true;
        }
        const auto decoded = std::chrono::steady_clock::now();
        if (!p.convertToNv12(*frame, nv12Target)) return false;
        average(p.decodeMs, started, decoded);
        average(p.convertMs, decoded, std::chrono::steady_clock::now());
        *out = nv12Target;
        hardware = false;
        ++p.frames;
        p.error.clear();
        return true;
    }
    p.error = "hardware-to-software fallback did not converge";
    ++p.failures;
    return false;
}

bool CaptureCompressedDecoder::opened() const { return p_->decoder.opened(); }
bool CaptureCompressedDecoder::hardwareActive() const { return p_->hardware; }
bool CaptureCompressedDecoder::waitingForInput() const { return p_->waiting; }
void CaptureCompressedDecoder::recoverAtKeyframe() { p_->decoder.recoverAtKeyframe(); }
const char* CaptureCompressedDecoder::backendName() const
{
    if (p_->hardware) return "d3d12va";
    if (p_->hardwareRequested) return "software(after d3d12va)";
    return p_->decoder.opened() ? "software" : "none";
}
const std::string& CaptureCompressedDecoder::lastError() const { return p_->error; }
uint64_t CaptureCompressedDecoder::framesDecoded() const { return p_->frames; }
uint64_t CaptureCompressedDecoder::decodeFailures() const { return p_->failures; }
uint64_t CaptureCompressedDecoder::hardwareFallbacks() const { return p_->fallbacks; }
double CaptureCompressedDecoder::decodeMsAverage() const { return p_->decodeMs; }
double CaptureCompressedDecoder::convertMsAverage() const { return p_->convertMs; }
void CaptureCompressedDecoder::close() { p_->close(); }

} // namespace veyra::source
