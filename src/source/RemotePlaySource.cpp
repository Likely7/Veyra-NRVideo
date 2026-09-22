#include "veyra/source/RemotePlaySource.h"

#include "veyra/Log.h"
#include "veyra/pipeline/ColorMetadata.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <format>
#include <limits>
#include <span>
#ifdef _WIN32
#include <d3d12.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
#include <libavutil/hwcontext.h>
#ifdef _WIN32
#include <libavutil/hwcontext_d3d12va.h>
#endif
}

namespace veyra::source {
namespace {

std::shared_ptr<AVFrame> cloneFrame(const AVFrame* frame)
{
    AVFrame* clone = av_frame_clone(frame);
    if (clone == nullptr) {
        return {};
    }
    return std::shared_ptr<AVFrame>(clone, [](AVFrame* value) {
        av_frame_free(&value);
    });
}

pipeline::SourcePixelFormat pixelFormatFromFrame(const AVFrame& frame)
{
    if(frame.format==AV_PIX_FMT_D3D12&&frame.hw_frames_ctx){
        const auto* context=reinterpret_cast<const AVHWFramesContext*>(frame.hw_frames_ctx->data);
        return context->sw_format==AV_PIX_FMT_NV12?pipeline::SourcePixelFormat::NV12:context->sw_format==AV_PIX_FMT_P010?pipeline::SourcePixelFormat::P010:pipeline::SourcePixelFormat::Unknown;
    }
    switch (frame.format) {
    case AV_PIX_FMT_NV12: return pipeline::SourcePixelFormat::NV12;
    case AV_PIX_FMT_YUV420P10LE:case AV_PIX_FMT_P010: return pipeline::SourcePixelFormat::P010;
    case AV_PIX_FMT_YUV420P: return pipeline::SourcePixelFormat::Yuv420P;
    case AV_PIX_FMT_YUYV422: return pipeline::SourcePixelFormat::Yuy2;
    case AV_PIX_FMT_BGRA: return pipeline::SourcePixelFormat::Bgra8;
    default: return pipeline::SourcePixelFormat::Unknown;
    }
}

} // namespace

RemotePlaySource::RemotePlaySource()
    : inbox_(std::make_shared<remoteplay::SessionInbox>())
{
}

RemotePlaySource::~RemotePlaySource()
{
    close();
}

bool RemotePlaySource::open(const SourceOpenDesc&)
{
    veyra::log::error("remoteplay", "RemotePlaySource requires connect() with PS5 credentials");
    return false;
}

bool RemotePlaySource::connect(const RemotePlayConnectDesc& desc)
{
    close();
    if (stopFailed_) {
        veyra::log::error("remoteplay", "connect refused: previous session did not stop successfully");
        return false;
    }
    if (desc.request.video.validate() || !remoteplay::validHost(desc.request.host)) {
        veyra::log::error("remoteplay", "connect rejected: invalid host or video profile");
        return false;
    }
    request_ = remoteplay::NativeConnectRequest{};
    request_.host = desc.request.host;
    request_.video = desc.request.video;
    request_.viewOnly = desc.request.viewOnly;
    request_.credentials.accountId = desc.request.credentials.accountId;
    request_.credentials.registrationKey = desc.request.credentials.registrationKey;
    request_.credentials.sessionKey = desc.request.credentials.sessionKey;
    decodeMode_=desc.decodeMode;decodeDevice_=desc.decodeDevice;hardwareFallback_=false;
    discardMedia_=desc.controlOnly;

    origin100ns_ = static_cast<std::uint64_t>(remoteplay::monotonic100ns());
    try {
        inbox_ = std::make_shared<remoteplay::SessionInbox>(desc.queueLimits);
    } catch (...) {
        request_.credentials = remoteplay::PairingCredentials{};
        veyra::log::error("remoteplay", "invalid queue limits");
        return false;
    }
    auto token = inbox_->begin(static_cast<remoteplay::HostTime>(origin100ns_));
    inbox_->streamProfile(request_.video);
    token_ = token; // Keep a second weak token; backend owns the moved copy.
    const auto started = backend_.start(request_, token, discardMedia_);
    if (!started.ok) {
        veyra::log::error("remoteplay", std::format("{} failed code={}", started.operation, started.code));
        close();
        return false;
    }
    info_ = SourceInfo{};
    info_.opened = true;
    info_.kind = pipeline::SourceKind::RemotePlay;
    info_.width = request_.video.width;
    info_.height = request_.video.height;
    info_.averageFps = static_cast<double>(request_.video.fps);
    info_.nominalRateNum = static_cast<int>(request_.video.fps);
    info_.nominalRateDen = 1;
    info_.timestampQuantum = 1.0 / info_.averageFps;
    info_.duration = pipeline::Rational::unknown();
    info_.hardwareDecodeActive = false;
    info_.color.pixelFormat = pipeline::SourcePixelFormat::Yuv420P;
    info_.color.range = pipeline::ColorRange::Limited;
    info_.color.rangeAssumed = true;
    info_.color.matrix = pipeline::YuvMatrix::BT709;
    info_.color.matrixAssumed = true;
    info_.color.displayReferred709 = true;
    info_.color.reconstructChroma = desc.highQualitySampling;
    info_.color.transfer = pipeline::TransferFunction::BT709;
    info_.color.transferAssumed = true;
    info_.color.primaries = pipeline::ColorPrimaries::BT709;
    info_.color.primariesAssumed = true;
    if(request_.video.codec==remoteplay::Codec::H265Hdr){
        info_.color.pixelFormat=pipeline::SourcePixelFormat::P010;info_.color.matrix=pipeline::YuvMatrix::BT2020NCL;
        info_.color.transfer=pipeline::TransferFunction::PQ;info_.color.primaries=pipeline::ColorPrimaries::BT2020;info_.color.displayReferred709=true;
    }
    clock_.reset(static_cast<remoteplay::HostTime>(origin100ns_), request_.video.fps);
    sequence_ = 0;
    fallbackSourceIndex_ = 0;
    decoderEpoch_ = 1;
    connected_ = true;
    decoderReady_ = false;
    waitingForFirstFrame_ = true;
    pendingOpenFlag_ = true;
    veyra::log::info("remoteplay", std::format("session started host={} profile={}x{}@{} codec={} requestedBitrateKbps={} (bandwidth request, not measured throughput)",
        request_.host, request_.video.width, request_.video.height, request_.video.fps,
        request_.video.codec == remoteplay::Codec::H264 ? "H264" : request_.video.codec == remoteplay::Codec::H265Hdr ? "H265_HDR" : "H265",request_.video.bitrateKbps));
    return true;
}

remoteplay::SessionInbox::Snapshot RemotePlaySource::sessionSnapshot() const
{
    return inbox_->snapshot();
}

bool RemotePlaySource::openDecoder(remoteplay::Codec codec, std::uint32_t width, std::uint32_t height)
{
    if(decodeMode_==RemotePlayConnectDesc::DecodeMode::Hardware&&!decodeDevice_){
        veyra::log::error("remoteplay-decode","hardware requested without the engine D3D12 device");return false;
    }
    if (codecContext_ != nullptr) {
        if (codecContext_->codec_id == (codec == remoteplay::Codec::H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC)
            && static_cast<std::uint32_t>(codecContext_->width) == width
            && static_cast<std::uint32_t>(codecContext_->height) == height) {
            return true;
        }
        flushDecoder();
        avcodec_free_context(&codecContext_);
        decoderReady_ = false;
    }
    av_frame_free(&decoderFrame_);
    const AVCodecID codecId = codec == remoteplay::Codec::H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
    const AVCodec* decoder = avcodec_find_decoder(codecId);
    if (decoder == nullptr) {
        veyra::log::error("remoteplay", "FFmpeg decoder unavailable");
        return false;
    }
    codecContext_ = avcodec_alloc_context3(decoder);
    if (codecContext_ == nullptr) {
        return false;
    }
    codecContext_->codec_id = codecId;
    codecContext_->width = static_cast<int>(width);
    codecContext_->height = static_cast<int>(height);
    codecContext_->time_base = AVRational{1, static_cast<int>(request_.video.fps)};
    codecContext_->pkt_timebase = codecContext_->time_base;
    // Allow the 1088-line coded padding of 1080p H.264; bound allocation before decode.
    codecContext_->max_pixels = 1920LL * 1088;
    codecContext_->thread_count = 1;
    codecContext_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
#ifdef _WIN32
    if(decodeMode_!=RemotePlayConnectDesc::DecodeMode::Software&&!hardwareFallback_&&decodeDevice_){
        AVBufferRef* device=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D12VA);
        int result=AVERROR(ENOMEM);
        if(device){
            auto* context=reinterpret_cast<AVHWDeviceContext*>(device->data);
            auto* native=reinterpret_cast<AVD3D12VADeviceContext*>(context->hwctx);
            native->device=decodeDevice_.get();native->device->AddRef();
            result=av_hwdevice_ctx_init(device);
        }
        if(result>=0){
            codecContext_->hw_device_ctx=device;
            codecContext_->get_format=[](AVCodecContext*,const AVPixelFormat* formats){
                for(auto p=formats;*p!=AV_PIX_FMT_NONE;++p)if(*p==AV_PIX_FMT_D3D12)return *p;
                return AV_PIX_FMT_NONE;
            };
        }else{
            av_buffer_unref(&device);hardwareFallback_=true;
            if(decodeMode_==RemotePlayConnectDesc::DecodeMode::Hardware){
                veyra::log::error("remoteplay-decode",std::format("requested hardware unavailable code={}; choose automatic or software",result));
                avcodec_free_context(&codecContext_);return false;
            }
            veyra::log::warn("remoteplay-decode",std::format("D3D12VA device init failed code={}; falling back to software",result));
        }
    }
#endif
    const int opened = avcodec_open2(codecContext_, decoder, nullptr);
    if (opened < 0) {
        veyra::log::error("remoteplay", std::format("FFmpeg remote decoder open failed code={}", opened));
        const bool retrySoftware=codecContext_->hw_device_ctx&&!hardwareFallback_&&decodeMode_==RemotePlayConnectDesc::DecodeMode::Automatic;
        avcodec_free_context(&codecContext_);
        if(retrySoftware){hardwareFallback_=true;return openDecoder(codec,width,height);}
        return false;
    }
    decoderFrame_ = av_frame_alloc();
    if (decoderFrame_ == nullptr) {
        avcodec_free_context(&codecContext_);
        return false;
    }
    decoderReady_ = true;
    info_.width = width;
    info_.height = height;
    veyra::log::info("remoteplay", std::format("{} decoder opened codec={} {}x{} (active format confirmed on first output)",codecContext_->hw_device_ctx?"D3D12VA":"software", decoder->name, width, height));
    return true;
}

pipeline::FramePacket RemotePlaySource::makePacket(const remoteplay::VideoSample& sample,
    std::uint64_t sourceIndex, std::uint64_t epoch, bool reset)
{
    pipeline::FramePacket packet{};
    packet.sequence = 0;
    packet.sourceKind = pipeline::SourceKind::RemotePlay;
    packet.sourceEpoch = epoch;
    packet.arrivalHost100ns = sample.arrival100ns;
    const auto stamp = clock_.video(sourceIndex, sample.arrival100ns);
    if (stamp) {
        if(sourceIndex%600==0)veyra::log::info("remoteplay-clock",std::format("wireIndex={} ptsMs={:.3f} arrivalMinusPtsMs={:.3f} nominalFps={} (arrival-disciplined local estimate, not PS5 native PTS)",sourceIndex,double(stamp->pts100ns)/10000,double(sample.arrival100ns-static_cast<int64_t>(origin100ns_)-stamp->pts100ns)/10000,info_.averageFps));
        packet.pts = pipeline::Rational{stamp->pts100ns, 10000000};
        packet.duration = pipeline::Rational{stamp->duration100ns, 10000000};
        if (stamp->discontinuity) packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
    } else {
        veyra::log::error("remoteplay-clock",std::format("invalid local timestamp wireIndex={} arrival100ns={} origin100ns={}",sourceIndex,sample.arrival100ns,origin100ns_));
        packet.pts = pipeline::Rational::unknown();
        packet.duration = pipeline::Rational::unknown();
    }
    if (pendingOpenFlag_) {
        packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Open);
        pendingOpenFlag_ = false;
    }
    if (reset) packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
    packet.colorInfo = info_.color;
    return packet;
}

bool RemotePlaySource::drainDecoder(std::uint64_t sourceIndex, const pipeline::FramePacket& sourcePacket)
{
    for (;;) {
        const int result = avcodec_receive_frame(codecContext_, decoderFrame_);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return true;
        }
        if (result < 0) {
            veyra::log::error("remoteplay", std::format("avcodec_receive_frame failed code={}", result));
            return false;
        }
        auto clone = cloneFrame(decoderFrame_);
        if (!clone) return false;
        const auto stamp = std::find_if(packetStamps_.begin(), packetStamps_.end(),
            [&](const PacketStamp& value) { return value.id == decoderFrame_->pts; });
        if (stamp == packetStamps_.end()) {
            veyra::log::error("remoteplay", "decoded frame has no matching packet timestamp");
            return false;
        }
        pipeline::FramePacket packet = stamp->packet;
        packetStamps_.erase(stamp);
        packet.sequence = ++sequence_;
        auto fallback=info_.color;
        // A previous stream profile's explicit siting must not survive a new
        // frame with unspecified siting; PS5 fallback is documented as left.
        fallback.chromaLocation=pipeline::ChromaLocation::Unknown;
        if(request_.video.codec==remoteplay::Codec::H265Hdr&&pixelFormatFromFrame(*decoderFrame_)!=pipeline::SourcePixelFormat::P010){
            // A negotiated 8-bit fallback is not automatically a PQ signal.
            fallback.transfer=pipeline::TransferFunction::BT709;fallback.transferAssumed=true;
            fallback.matrix=pipeline::YuvMatrix::BT709;fallback.matrixAssumed=true;
            fallback.primaries=pipeline::ColorPrimaries::BT709;fallback.primariesAssumed=true;
        }
        packet.colorInfo = pipeline::resolveFrameColor(*decoderFrame_, fallback);
        if(sequence_==1)veyra::log::info("remoteplay-sampling",std::format("fine={} chromaLocation={} assumedLeft={}",packet.colorInfo.reconstructChroma,int(packet.colorInfo.chromaLocation),packet.colorInfo.chromaLocation==pipeline::ChromaLocation::Unknown));
        packet.colorInfo.pixelFormat = pixelFormatFromFrame(*decoderFrame_);
        if (decoderFrame_->width <= 0 || decoderFrame_->height <= 0 ||
            decoderFrame_->width > 1920 || decoderFrame_->height > 1080 ||
            packet.colorInfo.pixelFormat == pipeline::SourcePixelFormat::Unknown ||
            (packet.colorInfo.isHdrPath()&&request_.video.codec!=remoteplay::Codec::H265Hdr)) return false;
        if (info_.width != static_cast<uint32_t>(decoderFrame_->width) ||
            info_.height != static_cast<uint32_t>(decoderFrame_->height)) {
            info_.width = static_cast<uint32_t>(decoderFrame_->width);
            info_.height = static_cast<uint32_t>(decoderFrame_->height);
            packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Resize);
        }
        info_.color = packet.colorInfo;
        const bool hardware=decoderFrame_->format==AV_PIX_FMT_D3D12;
        if(sequence_==1||info_.hardwareDecodeActive!=hardware)veyra::log::info("remoteplay-decode",std::format("actual={} pixelFormat={} {}x{} fallback={}",hardware?"D3D12VA":"software",decoderFrame_->format,decoderFrame_->width,decoderFrame_->height,hardwareFallback_));
        info_.hardwareDecodeActive=hardware;
#ifdef _WIN32
        if(sequence_==1&&hardware){
            auto* native=reinterpret_cast<AVD3D12VAFrame*>(decoderFrame_->data[0]);
            if(native&&native->texture){const auto d=native->texture->GetDesc();
                veyra::log::info("remoteplay-texture",std::format("visible={}x{} resource={}x{} array={} mip={} slice={} format={} flags=0x{:X} crop={}/{}/{}/{} fence={}",decoderFrame_->width,decoderFrame_->height,d.Width,d.Height,d.DepthOrArraySize,d.MipLevels,native->subresource_index,unsigned(d.Format),unsigned(d.Flags),decoderFrame_->crop_left,decoderFrame_->crop_top,decoderFrame_->crop_right,decoderFrame_->crop_bottom,native->sync_ctx.fence_value));
            }
        }
#endif
        inbox_->decoderBackend(hardware,hardwareFallback_);
        packet.color.resource = nullptr;
        // The AU contract is one picture per input. Unknown/reused timestamps
        // are rejected above instead of fabricating timing for extra pictures.
        (void)sourcePacket;
        (void)sourceIndex;
        if (ready_.size() >= 4) return false;
        packet.decodedHost100ns=remoteplay::monotonic100ns();
        ready_.push_back({std::move(clone), packet});
        inbox_->frameDecoded(remoteplay::monotonic100ns());
    }
}

bool RemotePlaySource::submitPacket(std::span<const std::uint8_t> bytes,
    std::uint64_t sourceIndex, const pipeline::FramePacket& sourcePacket)
{
    if (!decoderReady_ || bytes.empty()) return false;
    inbox_->decodeStarted(remoteplay::monotonic100ns(),sourcePacket.arrivalHost100ns);
    struct Finish {remoteplay::SessionInbox& inbox;~Finish(){inbox.decodeFinished(remoteplay::monotonic100ns());}} finish{*inbox_};
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr || av_new_packet(packet, static_cast<int>(bytes.size())) < 0) {
        av_packet_free(&packet);
        return false;
    }
    std::memcpy(packet->data, bytes.data(), bytes.size());
    if (packetStamps_.size() >= 64 || nextPacketId_ == INT64_MAX) {
        av_packet_free(&packet);
        return false;
    }
    packet->pts = nextPacketId_++;
    packet->dts = AV_NOPTS_VALUE;
    packetStamps_.push_back({packet->pts, sourcePacket});
    bool ok = true;
    for (;;) {
        const int send = avcodec_send_packet(codecContext_, packet);
        if (send == 0) {
            ok = drainDecoder(sourceIndex, sourcePacket);
            break;
        }
        if (send == AVERROR(EAGAIN)) {
            if (!drainDecoder(sourceIndex, sourcePacket)) { ok = false; break; }
            continue;
        }
        veyra::log::error("remoteplay", std::format("avcodec_send_packet failed code={}", send));
        ok = false;
        break;
    }
    av_packet_free(&packet);
    return ok;
}

SourceReadStatus RemotePlaySource::read(pipeline::FramePacket& out, const AVFrame** decodedFrame)
{
    if (decodedFrame) *decodedFrame = nullptr;
    if (!connected_) return SourceReadStatus::Error;
    if (inbox_->takeIdrRequest(remoteplay::monotonic100ns())) {
        const auto idr = backend_.requestIdr();
        if (!idr.ok) {
            veyra::log::warn("remoteplay", std::format("{} code={}", idr.operation, idr.code));
            if (token_) inbox_->decodeFailed(token_->generation());
        }
    }
    if (!ready_.empty()) {
        auto item = std::move(ready_.front());
        ready_.pop_front();
        out = item.packet;
        lastFrame_ = std::move(item.frame);
        if (decodedFrame) *decodedFrame = lastFrame_.get();
        if (token_) inbox_->decodedFrameReady(token_->generation());
        return SourceReadStatus::Frame;
    }
    const auto now = remoteplay::monotonic100ns();
    auto queued = inbox_->tryVideo(now);
    if (!queued) {
        const auto state = inbox_->snapshot().state;
        if (state == remoteplay::SessionState::Failed || state == remoteplay::SessionState::Idle) return SourceReadStatus::Error;
        return SourceReadStatus::Waiting;
    }
    const auto& sample = queued->sample;
    if ((!decoderReady_ || queued->resetDecoder) && !openDecoder(sample.codec, sample.width, sample.height)) {
        inbox_->decodeFailed(sample.generation);
        return SourceReadStatus::Error;
    }
    if (queued->resetDecoder) {
        flushDecoder();
        ++decoderEpoch_;
        if(request_.video.codec==remoteplay::Codec::H265Hdr){
        info_.color.pixelFormat=pipeline::SourcePixelFormat::P010;info_.color.matrix=pipeline::YuvMatrix::BT2020NCL;
        info_.color.transfer=pipeline::TransferFunction::PQ;info_.color.primaries=pipeline::ColorPrimaries::BT2020;info_.color.displayReferred709=true;
    }
    clock_.reset(static_cast<remoteplay::HostTime>(origin100ns_), request_.video.fps);
        pendingOpenFlag_ = true;
    }
    const auto extended = sample.wireFrameIndex ? wireSequence_.observe(*sample.wireFrameIndex)
        : remoteplay::Sequence16Extender::Result{++fallbackSourceIndex_,true,false,0};
    if (!extended.accepted) { inbox_->decodeFailed(sample.generation); return SourceReadStatus::Waiting; }
    const std::uint64_t sourceIndex = extended.value;
    const pipeline::FramePacket sourcePacket = makePacket(sample, sourceIndex, decoderEpoch_, queued->resetDecoder);
    std::vector<uint8_t> combined;
    auto payload = sample.payload.bytes();
    if (queued->configBefore) {
        const auto config = queued->configBefore->payload.bytes();
        combined.reserve(config.size() + payload.size());
        combined.insert(combined.end(), config.begin(), config.end());
        combined.insert(combined.end(), payload.begin(), payload.end());
        payload = combined;
    }
    if (!submitPacket(payload, sourceIndex, sourcePacket)) {
        flushDecoder();
        if(codecContext_&&codecContext_->hw_device_ctx&&!hardwareFallback_){
            if(decodeMode_==RemotePlayConnectDesc::DecodeMode::Hardware){
                veyra::log::error("remoteplay-decode","requested hardware decode failed; choose automatic or software");
                inbox_->decodeFailed(sample.generation);return SourceReadStatus::Error;
            }
            hardwareFallback_=true;avcodec_free_context(&codecContext_);decoderReady_=false;
            info_.hardwareDecodeActive=false;
            veyra::log::warn("remoteplay-decode","hardware decode failed; switching to software at next keyframe");
        }
        inbox_->decodeFailed(sample.generation);
        return SourceReadStatus::Waiting;
    }
    if (ready_.empty()) return SourceReadStatus::Waiting;
    auto item = std::move(ready_.front());
    ready_.pop_front();
    out = item.packet;
    lastFrame_ = std::move(item.frame);
    if (decodedFrame) *decodedFrame = lastFrame_.get();
    waitingForFirstFrame_ = false;
    inbox_->decodedFrameReady(sample.generation);
    return SourceReadStatus::Frame;
}

void RemotePlaySource::flushDecoder() noexcept
{
    ready_.clear();
    packetStamps_.clear();
    if (codecContext_) avcodec_flush_buffers(codecContext_);
}

std::size_t RemotePlaySource::pullAudio(float* stereo, std::size_t frames, double* firstPtsMs)
{
    if (!stereo || frames == 0) return 0;
    std::size_t written = 0;
    while (written < frames) {
        if (!audioBlock_) {
            audioBlock_ = inbox_->tryAudio();
            audioOffset_ = 0;
        }
        if (!audioBlock_) break;
        auto* block = &*audioBlock_;
        if (!block->valid() || block->rate != 48000) { audioBlock_.reset(); continue; }
        if (audioOffset_ == 0) {
            const bool reset = !audioAnchorSample_ || block->discontinuity ||
                block->firstSample != audioNextSample_ || block->rate != audioRate_;
            if (reset && written) break;
            if (reset) {
                audioAnchorSample_ = block->firstSample;
                audioAnchorPts_ = block->arrival100ns - static_cast<int64_t>(origin100ns_);
                audioRate_ = block->rate;
            }
        }
        if (firstPtsMs && written == 0) {
            *firstPtsMs = static_cast<double>(audioAnchorPts_) / 10000.0 +
                1000.0 * static_cast<double>(block->firstSample - *audioAnchorSample_ + audioOffset_) / block->rate;
        }
        const auto copyFrames = std::min(frames - written, block->frames() - audioOffset_);
        for (std::size_t i = 0; i < copyFrames; ++i) {
            const auto sourceChannels = block->channels;
            const auto sourceIndex = (i + audioOffset_) * sourceChannels;
            stereo[(written + i) * 2] = static_cast<float>(block->samples[sourceIndex]) / 32768.0f;
            stereo[(written + i) * 2 + 1] = sourceChannels > 1
                ? static_cast<float>(block->samples[sourceIndex + 1]) / 32768.0f
                : stereo[(written + i) * 2];
        }
        written += copyFrames;
        audioOffset_ += copyFrames;
        audioNextSample_ = block->firstSample + audioOffset_;
        if (audioOffset_ == block->frames()) audioBlock_.reset();
    }
    return written;
}

void RemotePlaySource::close() noexcept
{
    connected_ = false;
    ready_.clear();
    lastFrame_.reset();
    if (token_) {
        try {
            inbox_->invalidate();
            const auto stopped = backend_.stop();
            stopFailed_ = !stopped.ok;
            if (stopped.ok) {
                inbox_->finishStop();
                token_.reset();
            } else {
                veyra::log::error("remoteplay", std::format("{} failed code={}; session remains stopping",stopped.operation,stopped.code));
            }
        } catch (...) { stopFailed_ = true; }
    }
    flushDecoder();
    av_frame_free(&decoderFrame_);
    avcodec_free_context(&codecContext_);
    decoderReady_ = false;
    decodeDevice_.reset();
    audioBlock_.reset(); audioAnchorSample_.reset(); audioOffset_ = 0;
    audioNextSample_ = 0; audioRate_ = 0; wireSequence_.reset();
    request_.credentials = remoteplay::PairingCredentials{};
    info_ = SourceInfo{};
}

std::size_t RemotePlayAudioSource::pull(float* stereo, std::size_t frames, double* firstPtsMs)
{
    const auto count = source_.pullAudio(stereo, frames, firstPtsMs);
    if (count && firstPtsMs && std::isfinite(*firstPtsMs)) {
        lastEndPtsMs_ = *firstPtsMs + 1000.0 * static_cast<double>(count) / 48000.0;
    }
    return count;
}

} // namespace veyra::source
