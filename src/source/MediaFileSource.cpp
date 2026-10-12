#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/source/DolbyVisionRpu.h"

#include <climits>
#include <cstddef>
#include <cmath>
#include <format>
#include <string_view>

#include "veyra/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mastering_display_metadata.h>
}

#include <thread>
#include <algorithm>
namespace veyra::source {
namespace {
// File playback software decode: frame threading up to four workers regardless
// of resolution (1080p60 H.264/HEVC on one thread fell below real time when
// hardware decode was unavailable, sweep 2026-09-22 C2). Live capture keeps
// its own low-latency single-thread policy in CaptureCompressedDecoder.
unsigned softwareDecodeThreads(int, int) {
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    return std::clamp(cores / 2u, 2u, 4u);
}
}


namespace {

int64_t rationalToUs(const pipeline::Rational& r)
{
    if (!r.known || r.den == 0) { return 0; }
    return static_cast<int64_t>(static_cast<long double>(r.num) * 1000000.0L
        / static_cast<long double>(r.den) + (r.num >= 0 ? 0.5L : -0.5L));
}

} // namespace

MediaFileSource::~MediaFileSource()
{
    close();
}

bool MediaFileSource::fallbackToSoftware(std::string_view reason)
{
    if (!decoder_.hardwareActive() || path_.empty()) {
        return false;
    }
    if (requireHardwareDecode_) {
        veyra::log::error("source-file", std::format(
            "hardware decode required by the user setting; refusing the software fallback (reason={})", reason));
        return false;
    }
    // Mid-stream failures (driver/pool errors after frames were delivered)
    // reopen in software and seek back to the last delivered PTS; the caller
    // sees a Discontinuity flag so temporal history resets (sweep B5).
    const bool midStream = framesRead_ != 0;
    const int64_t resumeUs = lastPtsUs_;
    veyra::log::warn("source-file", std::format(
        "D3D12VA {} fallback to software reason={} path=redacted resumeUs={}", midStream ? "mid-stream" : "first-frame", reason, resumeUs));
    decoder_.close();
    demuxer_.close();
    if (!demuxer_.open(path_)) {
        veyra::log::error("source-file", "software fallback could not reopen demuxer");
        return false;
    }
    const auto* params = demuxer_.videoCodecParameters();
    if (params == nullptr || !decoder_.openSoftware(params, demuxer_.videoTimeBaseNum(),
            demuxer_.videoTimeBaseDen(), softwareDecodeThreads(params->width, params->height))) {
        veyra::log::error("source-file", "software fallback decoder open failed");
        return false;
    }
    info_.hardwareDecodeActive = false;
    if (midStream && resumeUs != INT64_MIN) {
        if (!demuxer_.seekToUs(std::max<int64_t>(0, resumeUs))) veyra::log::warn("source-file", "software fallback could not seek back; continuing from stream start");
        decoder_.flushBuffers();
        // The engine treats Seek as a hard history reset; frames before the
        // resume point are dropped by the caller's discardBefore logic.
        pendingSeekFlag_ = true;
        lastPtsUs_ = INT64_MIN;
        ++epoch_;
    }
    info_.videoDecodePath = "software";
    info_.containerName = demuxer_.formatName();
    info_.videoPixelFormatName = params->format >= 0 && av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format))
        ? av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format)) : "unknown";
    draining_ = false;
    eofSignalled_ = false;
    pendingSeekFlag_ = false;
    lastPtsUs_ = INT64_MIN;
    return true;
}

pipeline::ColorDescription MediaFileSource::parseColor(const AVCodecParameters* params) const
{
    pipeline::ColorDescription cd;
    switch (params->format) {
    case AV_PIX_FMT_NV12: cd.pixelFormat = pipeline::SourcePixelFormat::NV12; break;
    case AV_PIX_FMT_P010: cd.pixelFormat = pipeline::SourcePixelFormat::P010; break;
    case AV_PIX_FMT_YUV420P: cd.pixelFormat = pipeline::SourcePixelFormat::Yuv420P; break;
    case AV_PIX_FMT_YUYV422: cd.pixelFormat = pipeline::SourcePixelFormat::Yuy2; break;
    case AV_PIX_FMT_BGRA: cd.pixelFormat = pipeline::SourcePixelFormat::Bgra8; break;
    default: cd.pixelFormat = pipeline::SourcePixelFormat::Unknown; break;
    }
    switch (params->color_range) {
    case AVCOL_RANGE_MPEG: cd.range = pipeline::ColorRange::Limited; break;
    case AVCOL_RANGE_JPEG: cd.range = pipeline::ColorRange::Full; break;
    default: cd.range = pipeline::ColorRange::Unknown; break;
    }
    switch (params->color_space) {
    case AVCOL_SPC_BT470BG: cd.matrix = pipeline::YuvMatrix::BT601; break;
    case AVCOL_SPC_SMPTE170M: cd.matrix = pipeline::YuvMatrix::BT601; break;
    case AVCOL_SPC_BT709: cd.matrix = pipeline::YuvMatrix::BT709; break;
    case AVCOL_SPC_BT2020_NCL: cd.matrix = pipeline::YuvMatrix::BT2020NCL; break;
    case AVCOL_SPC_BT2020_CL: cd.matrix = pipeline::YuvMatrix::BT2020CL; break;
    default: cd.matrix = pipeline::YuvMatrix::Unknown; break;
    }
    switch (params->color_trc) {
    case AVCOL_TRC_BT709: cd.transfer = pipeline::TransferFunction::BT709; break;
    case AVCOL_TRC_IEC61966_2_1: cd.transfer = pipeline::TransferFunction::SRGB; break;
    case AVCOL_TRC_BT2020_10: cd.transfer = pipeline::TransferFunction::BT2020_10; break;
    case AVCOL_TRC_LINEAR: cd.transfer = pipeline::TransferFunction::Linear; break;
    default: cd.transfer = pipeline::TransferFunction::Unknown; break;
    }
    switch (params->color_primaries) {
    case AVCOL_PRI_BT470M:
    case AVCOL_PRI_BT470BG: cd.primaries = pipeline::ColorPrimaries::BT601_625; break;
    case AVCOL_PRI_SMPTE170M: cd.primaries = pipeline::ColorPrimaries::BT601_525; break;
    case AVCOL_PRI_BT709: cd.primaries = pipeline::ColorPrimaries::BT709; break;
    case AVCOL_PRI_BT2020: cd.primaries = pipeline::ColorPrimaries::BT2020; break;
    default: cd.primaries = pipeline::ColorPrimaries::Unknown; break;
    }

    if (cd.primaries == pipeline::ColorPrimaries::Unknown) {
        cd.primaries = cd.matrix==pipeline::YuvMatrix::BT2020NCL||cd.matrix==pipeline::YuvMatrix::BT2020CL?pipeline::ColorPrimaries::BT2020:pipeline::ColorPrimaries::BT709;
        cd.primariesAssumed = true;
    }
    // Standard desktop SDR playback retains RGB signal levels after range
    // expansion and YUV matrix conversion. Match the working decode to the
    // sink encode; don't apply a reference-monitor gamma adjustment here.
    cd.preserveSdrCodeValues = true;
    if(const auto* data=av_packet_side_data_get(params->coded_side_data,params->nb_coded_side_data,AV_PKT_DATA_MASTERING_DISPLAY_METADATA);data&&data->size>=sizeof(AVMasteringDisplayMetadata)){
        const auto* m=reinterpret_cast<const AVMasteringDisplayMetadata*>(data->data);
        if(m->has_luminance&&m->max_luminance.den>0)cd.hdrMasteringPeakNits=float(av_q2d(m->max_luminance));
    }
    if(const auto* data=av_packet_side_data_get(params->coded_side_data,params->nb_coded_side_data,AV_PKT_DATA_CONTENT_LIGHT_LEVEL);data&&data->size>=sizeof(AVContentLightMetadata)){
        const auto* m=reinterpret_cast<const AVContentLightMetadata*>(data->data);cd.hdrMaxCllNits=float(m->MaxCLL);cd.hdrMaxFallNits=float(m->MaxFALL);
    }
    AVFrame metadata{};metadata.format=params->format;metadata.height=info_.height;metadata.color_range=params->color_range;metadata.colorspace=params->color_space;metadata.color_trc=params->color_trc;
    return pipeline::resolveFrameColor(metadata,cd);
}

bool MediaFileSource::open(const SourceOpenDesc& desc)
{
    close();
    path_ = desc.path;
    preferHardwareDecode_ = desc.preferHardwareDecode && desc.d3d12Device != nullptr && desc.d3d12Queue != nullptr;
    requireHardwareDecode_ = desc.requireHardwareDecode;
    if (!demuxer_.open(desc.path)) {
        veyra::log::error("source-file", "demuxer open failed");
        return false;
    }
    const AVCodecParameters* params = demuxer_.videoCodecParameters();
    if (params == nullptr) {
        veyra::log::error("source-file", "no video codec parameters");
        return false;
    }

    bool decoderOpen = false;
    if (desc.preferHardwareDecode && desc.d3d12Device != nullptr && desc.d3d12Queue != nullptr) {
        // HEVC uses D3D11VA on purpose. On some drivers (RTX 5070 + 616.56) the
        // first HEVC picture of the D3D12VA decoder faults the driver and the
        // SHARED D3D12 device is removed, which took the whole session down
        // instead of falling back (docs/DIAG_HEVC_D3D12VA_AND_EXPORT_CFR_2026-09-17.md).
        // The D3D11VA path decodes on its own device and shares the surfaces as
        // NT handles, so a failure there can only cost a software fallback.
        if (params->codec_id == AV_CODEC_ID_HEVC) {
            decoderOpen = decoder_.openD3D11VA(params, demuxer_.videoTimeBaseNum(),
                demuxer_.videoTimeBaseDen(), desc.d3d12AdapterLuid,
                static_cast<ID3D12Device*>(desc.d3d12Device));
            if (decoderOpen) {
                veyra::log::info("source-file", "D3D11VA decode active (NT-handle surfaces shared to the D3D12 graph)");
            } else {
                veyra::log::warn("source-file", "D3D11VA open failed; falling back to software decode (explicit)");
            }
        } else {
            decoderOpen = decoder_.openD3D12VA(params, demuxer_.videoTimeBaseNum(),
                demuxer_.videoTimeBaseDen(),
                static_cast<ID3D12Device*>(desc.d3d12Device),
                static_cast<ID3D12CommandQueue*>(desc.d3d12Queue));
            if (decoderOpen) {
                veyra::log::info("source-file", "D3D12VA decode active (shared device)");
            } else {
                veyra::log::warn("source-file", "D3D12VA open failed; falling back to software decode (explicit)");
            }
        }
    }
    if (!decoderOpen && desc.requireHardwareDecode) {
        veyra::log::error("source-file", "hardware decode required by the user setting and not available; not falling back to software");
        return false;
    }
    if (!decoderOpen) {
        decoderOpen = decoder_.openSoftware(params, demuxer_.videoTimeBaseNum(),
            demuxer_.videoTimeBaseDen(), softwareDecodeThreads(params->width, params->height));
    }
    if (!decoderOpen) {
        veyra::log::error("source-file", "decoder open failed");
        close();
        return false;
    }

    info_ = SourceInfo{};
    info_.opened = true;
    info_.kind = pipeline::SourceKind::File;
    info_.width = static_cast<uint32_t>(decoder_.width());
    info_.height = static_cast<uint32_t>(decoder_.height());
    // Display aspect for the window fit: coded size, the stream's sample aspect
    // ratio (anamorphic DVD/HD content), then 90/270 rotations swap the axes.
    // A stream without a usable ratio keeps displayAspect = 0 (caller uses the
    // coded size), which is what every pre-existing caller already assumed.
    {
        int num = 0, den = 0;
        demuxer_.sampleAspect(num, den);
        info_.rotationDegrees = demuxer_.rotationDegrees();
        if (num > 0 && den > 0 && info_.width > 0 && info_.height > 0) {
            double aspect = (double(info_.width) * num) / (double(info_.height) * den);
            if (info_.rotationDegrees == 90 || info_.rotationDegrees == 270) aspect = 1.0 / aspect;
            if (std::isfinite(aspect) && aspect > 0.0) info_.displayAspect = aspect;
        }
        veyra::log::info("source-file", std::format("display aspect={:.4f} sar={}/{} rotation={}", info_.displayAspect, num, den, info_.rotationDegrees));
    }
    const int64_t durUs = demuxer_.durationUs();
    if (durUs > 0) {
        info_.duration = pipeline::Rational{durUs, 1000000};
    } else {
        info_.duration = pipeline::Rational::unknown();
    }
    info_.averageFps = demuxer_.averageFps();
    info_.nominalRateNum = demuxer_.nominalRateNum();
    info_.nominalRateDen = demuxer_.nominalRateDen();
    info_.timestampQuantum = demuxer_.videoTimeBaseDen() > 0 ? double(demuxer_.videoTimeBaseNum()) / demuxer_.videoTimeBaseDen() : 0;
    info_.hardwareDecodeActive = decoder_.hardwareActive();
    info_.videoDecodePath = decoder_.decodePathName();
    info_.containerName = demuxer_.formatName();
    info_.videoCodecName = avcodec_get_name(params->codec_id);
    info_.videoPixelFormatName = params->format >= 0 && av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format))
        ? av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format)) : "unknown";
    info_.color = parseColor(params);
    if(const auto* side=av_packet_side_data_get(params->coded_side_data,params->nb_coded_side_data,AV_PKT_DATA_DOVI_CONF)){
        if(side->size<offsetof(AVDOVIDecoderConfigurationRecord,dv_bl_signal_compatibility_id)+sizeof(uint8_t)){
            errorMessage_=L"Dolby Vision 配置信息不完整";return false;
        }
        const auto& config=*reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(side->data);
        auto& dv=info_.dolbyVision;dv.present=true;dv.profile=config.dv_profile;dv.level=config.dv_level;
        dv.baseLayer=config.bl_present_flag!=0;dv.enhancementLayer=config.el_present_flag!=0;
        dv.rpuDeclared=config.rpu_present_flag!=0;dv.compatibility=config.dv_bl_signal_compatibility_id;
        log::info("source-dovi",std::format("profile={} level={} compatibility={} bl={} el={} rpuDeclared={} route={} nativeOutput=0 rpuApplied=0",dv.profile,dv.level,dv.compatibility,dv.baseLayer,dv.enhancementLayer,dv.rpuDeclared,int(dv.route())));
        if(dv.route()==DolbyBaseLayer::Unsupported){errorMessage_=dv.description();return false;}
        // Profile 5 (and any routed DoVi whose base layer omits VUI color tags)
        // keeps the real color in the RPU, which compatibility playback does
        // not apply. Pin the base layer to the routed HDR container instead of
        // letting it fall back to BT.709 SDR (which then mismatches / washes
        // out). Explicit, non-assumed tags from P7/P8/P10 are left untouched.
        if(dv.route()==DolbyBaseLayer::Hdr10){
            auto& c=info_.color;
            if(c.transfer==pipeline::TransferFunction::Unknown||c.transferAssumed){
                c.transfer=pipeline::TransferFunction::PQ;c.transferAssumed=true;}
            if(c.matrix==pipeline::YuvMatrix::Unknown||c.matrixAssumed){
                c.matrix=pipeline::YuvMatrix::BT2020NCL;c.matrixAssumed=true;}
            if(c.primaries==pipeline::ColorPrimaries::Unknown||c.primariesAssumed){
                c.primaries=pipeline::ColorPrimaries::BT2020;c.primariesAssumed=true;}
        }
    }
    // Profile-5 conversion state belongs to the stream position, never to the
    // previous open: a stale value would let a fresh play start from parameters
    // this file did not provide.
    info_.color.dolbyVisionP5 = {};

    sequence_ = 0;
    framesRead_ = 0;
    seekCount_ = 0;
    draining_ = false;
    eofSignalled_ = false;
    pendingSeekFlag_ = false;
    lastPtsUs_ = INT64_MIN;
    epoch_ = 1;
    ++epoch_; // fresh open = new epoch

    veyra::log::info("source-file", std::format(
        "opened {}x{} dur={}s avgFps={:.3f} container={} codec={} pixelFmt={} hw={} matrix={}{} range={}{} transfer={}{}",
        info_.width, info_.height,
        info_.duration.isUnknown() ? -1.0 : info_.duration.toDouble(),
        info_.averageFps, info_.containerName, info_.videoCodecName, info_.videoPixelFormatName, info_.hardwareDecodeActive,
        static_cast<int>(info_.color.matrix), info_.color.matrixAssumed ? "(assumed)" : "",
        static_cast<int>(info_.color.range), info_.color.rangeAssumed ? "(assumed)" : "",
        static_cast<int>(info_.color.transfer), info_.color.transferAssumed ? "(assumed)" : ""));
    return true;
}

SourceReadStatus MediaFileSource::read(pipeline::FramePacket& out, const AVFrame** decodedFrame)
{
    if (decodedFrame != nullptr) { *decodedFrame = nullptr; }
    out = pipeline::FramePacket{};
    if (!info_.opened || !errorMessage_.empty()) { return SourceReadStatus::Error; }

    const AVFrame* frame = nullptr;
    for (;;) {
        frame = decoder_.receiveFrame();
        if (frame != nullptr) { break; }
        if (decoder_.receiveStatus() == media::DecodeReceiveStatus::EndOfStream) {
            return SourceReadStatus::Eos;
        }
        if (decoder_.receiveStatus() == media::DecodeReceiveStatus::Error) {
            if (fallbackToSoftware("decoder-error")) {
                continue;
            }
            errorMessage_ = L"视频解码失败，已停止处理，请检查源文件是否损坏";
            return SourceReadStatus::Error;
        }
        if (draining_) {
            errorMessage_ = L"视频解码失败，已停止处理，请检查源文件是否损坏";
            return SourceReadStatus::Error;
        }
        bool eof = false;
        if (!demuxer_.readVideoPacket(eof)) {
            if (!eof) { errorMessage_ = L"源视频读取失败，已停止处理"; return SourceReadStatus::Error; }
            draining_ = true;
            if (!decoder_.sendPacket(nullptr)) {
                errorMessage_ = L"视频尾帧解码失败，已停止处理";
                return SourceReadStatus::Error;
            }
            continue;
        }
        if (demuxer_.currentPacket()->flags & AV_PKT_FLAG_CORRUPT) {
            veyra::log::error("source-file", "corrupt video packet; refusing to skip source data");
            errorMessage_ = L"源视频包含损坏数据，已停止处理";
            return SourceReadStatus::Error;
        }
        if (!decoder_.sendPacket(demuxer_.currentPacket())) {
            if (fallbackToSoftware("send-packet-error")) {
                continue;
            }
            errorMessage_ = L"视频解码失败，已停止处理，请检查源文件是否损坏";
            return SourceReadStatus::Error;
        }
    }

    if (decoder_.hardwareActive() && !decoder_.hardwareFrameImportable()) {
        if (fallbackToSoftware("unsupported-d3d12-surface")) {
            return read(out, decodedFrame);
        }
        errorMessage_ = L"硬件解码输出格式无法导入，且软件回退失败";
        return SourceReadStatus::Error;
    }

    if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags) {
        veyra::log::error("source-file", std::format("corrupt decoded frame flags={} decodeErrors={}", frame->flags, frame->decode_error_flags));
        errorMessage_ = L"源视频包含损坏画面，已停止处理";
        return SourceReadStatus::Error;
    }
    if (frame->width <= 0 || frame->height <= 0 || uint32_t(frame->width) != info_.width || uint32_t(frame->height) != info_.height) {
        veyra::log::error("source-file", std::format("frame extent changed: opened={}x{} decoded={}x{}; file processing stopped before upload", info_.width, info_.height, frame->width, frame->height));
        errorMessage_ = L"视频中途改变分辨率，当前文件处理不支持，已安全停止";
        return SourceReadStatus::Error;
    }

    ++sequence_;
    out.sequence = sequence_;
    const int tbNum = decoder_.frameTimeBaseNum();
    const int tbDen = decoder_.frameTimeBaseDen();
    // Prefer the container PTS; fall back to FFmpeg's best-effort estimate and
    // finally the packet DTS. Raw elementary streams and some TS/AVI muxes
    // leave pts unset on individual frames, which used to stop playback
    // outright (sweep 2026-09-22 B4). Still never invent a value.
    const int64_t stamp = frame->pts != AV_NOPTS_VALUE ? frame->pts
        : frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
        : frame->pkt_dts;
    if (stamp != AV_NOPTS_VALUE && tbDen > 0) {
        out.pts = pipeline::Rational{stamp * static_cast<int64_t>(tbNum), tbDen};
        if (frame->pts == AV_NOPTS_VALUE && (ptsFallbacks_++ % 300) == 0)
            veyra::log::info("source-file", std::format("frame pts missing; using {} (count={})", frame->best_effort_timestamp != AV_NOPTS_VALUE ? "best_effort_timestamp" : "pkt_dts", ptsFallbacks_));
    } else {
        out.pts = pipeline::Rational::unknown(); // never fabricate a timestamp
    }
    if (frame->duration > 0 && tbDen > 0) {
        out.duration = pipeline::Rational{frame->duration * static_cast<int64_t>(tbNum), tbDen};
    } else if (info_.averageFps > 0.0) {
        out.duration = pipeline::Rational{1000000, static_cast<int32_t>(1000000.0 * info_.averageFps + 0.5)};
    } else {
        out.duration = pipeline::Rational::unknown();
    }
    out.sourceKind = pipeline::SourceKind::File;
    out.colorInfo = pipeline::resolveFrameColor(*frame,info_.color);
    auto& dv=info_.dolbyVision;
    // Profile 5: the base layer is IPT-PQ-C2 and only the RPU says how to reach
    // display colour, so carry the per-frame conversion parameters the YUV pass
    // needs. Every other profile leaves this inactive and keeps the old path.
    // The shaping curves are re-sent per scene refresh, so this runs per frame;
    // the matrices arrive with them and are stable, but re-reading is cheap.
    if(dv.present&&dv.profile==5&&dv.baseLayer){
        // Per frame, from the identity: nothing another frame established may
        // leak into this one, so a frame converts the same way no matter how
        // playback reached it.
        info_.color.dolbyVisionP5=pipeline::DolbyVisionP5{};
        bool usable=false;
        if(const auto* side=av_frame_get_side_data(frame,AV_FRAME_DATA_DOVI_METADATA);
           side!=nullptr&&side->size>=sizeof(AVDOVIMetadata)){
            const auto* meta=reinterpret_cast<const AVDOVIMetadata*>(side->data);
            const auto built=buildDolbyVisionP5(*meta,info_.color.dolbyVisionP5);
            usable=built.usable;
            if(built.usable&&!dv.profile5Converted){
                // Only reported once the parameters really reproduce the RPU, so
                // the status line never claims a conversion that is not running.
                dv.profile5Converted=true;
                log::info("source-dovi","profile 5 base layer converted as IPT-PQ-C2: RPU reshaping (per-frame, projected on {1,sqrt(x),x}) -> RPU ycc_to_rgb -> PQ EOTF -> RPU rgb_to_lms x fixed HPE LMS->BT.2020 -> PQ OETF; the routed HDR10/SDR path then consumes it unchanged");
                const auto& p=info_.color.dolbyVisionP5;
                log::info("source-dovi",std::format(
                    "profile 5 constants ycc_to_rgb=[{:.6f},{:.6f},{:.6f}|{:.6f},{:.6f},{:.6f}|{:.6f},{:.6f},{:.6f}] "
                    "lms_to_rgb=[{:.6f},{:.6f},{:.6f}|{:.6f},{:.6f},{:.6f}|{:.6f},{:.6f},{:.6f}] "
                    "curve0=[{:.6f},{:.6f},{:.6f}] curve1=[{:.6f},{:.6f},{:.6f}] curve2=[{:.6f},{:.6f},{:.6f}]",
                    p.yccToRgb[0],p.yccToRgb[1],p.yccToRgb[2],p.yccToRgb[3],p.yccToRgb[4],p.yccToRgb[5],p.yccToRgb[6],p.yccToRgb[7],p.yccToRgb[8],
                    p.lmsToRgb[0],p.lmsToRgb[1],p.lmsToRgb[2],p.lmsToRgb[3],p.lmsToRgb[4],p.lmsToRgb[5],p.lmsToRgb[6],p.lmsToRgb[7],p.lmsToRgb[8],
                    p.curve[0][0],p.curve[0][1],p.curve[0][2],p.curve[1][0],p.curve[1][1],p.curve[1][2],p.curve[2][0],p.curve[2][1],p.curve[2][2]));
            }
            if(built.colorMetadataMissing&&!dv.profile5MissingColor){
                dv.profile5MissingColor=true;
                log::warn("source-dovi","profile 5 RPU carries no colour block; the base layer colour is undefined and the frame is refused");
            }
            if(built.unsupportedMapping&&!dv.profile5UnsupportedMapping){
                dv.profile5UnsupportedMapping=true;
                log::warn("source-dovi","profile 5 reshaping uses a mapping method this decoder cannot reproduce; such a frame is refused instead of converted with the identity");
            }
            if(built.placeholderMapping&&!dv.profile5Placeholder){
                dv.profile5Placeholder=true;
                log::info("source-dovi","profile 5 RPU carries a flat (placeholder) reshaping on this frame; the affected components use the identity");
            }
        }
        // A profile-5 base layer is IPT-PQ-C2 and carries no usable VUI colour:
        // reading it with the ordinary YUV matrix is what renders it green. The
        // tags pinned in open() only satisfy matches(), they convert nothing, so
        // refuse the frame when the RPU did not supply usable parameters instead
        // of silently returning a wrong picture.
        if(!usable){
            errorMessage_=L"Dolby Vision P5：这一帧没有可用的 RPU 转换参数（缺少 RPU、颜色块为空或整形方式不受支持），无法正确转换，已停止以避免错误颜色";
            log::error("source-dovi","profile 5 base layer has no usable RPU conversion parameters; refusing the ordinary YUV fallback");
            return SourceReadStatus::Error;
        }
        out.colorInfo.dolbyVisionP5=info_.color.dolbyVisionP5;
    }
    const bool hasRpu=av_frame_get_side_data(frame,AV_FRAME_DATA_DOVI_METADATA)||av_frame_get_side_data(frame,AV_FRAME_DATA_DOVI_RPU_BUFFER);
    if(hasRpu&&!dv.present){
        errorMessage_=L"检测到 Dolby Vision RPU，但缺少基础层兼容声明，无法确认颜色，已停止";
        return SourceReadStatus::Error;
    }
    if(hasRpu&&!dv.rpuObserved){dv.rpuObserved=true;log::info("source-dovi","decoded RPU observed; compatibility playback does not apply dynamic metadata");}
    if(!dv.matches(out.colorInfo)){
        errorMessage_=L"Dolby Vision 基础层颜色与兼容声明不匹配，已停止以避免错误颜色";
        return SourceReadStatus::Error;
    }
    // D3D12VA frames expose the underlying surface through AV_PIX_FMT_D3D12,
    // so resolveFrameColor cannot infer NV12/P010 from frame->format. HDR
    // hardware surfaces are P010 by contract; SDR surfaces are NV12. Software
    // AV1/ProRes paths retain the exact planar format mapping above.
    if (frame->format == AV_PIX_FMT_D3D12 && out.colorInfo.pixelFormat == pipeline::SourcePixelFormat::Unknown) {
        out.colorInfo.pixelFormat = out.colorInfo.isHdrPath()
            ? pipeline::SourcePixelFormat::P010 : pipeline::SourcePixelFormat::NV12;
    }
    info_.color = out.colorInfo;
    if (frame->format == AV_PIX_FMT_D3D12) {
        info_.videoPixelFormatName = out.colorInfo.pixelFormat == pipeline::SourcePixelFormat::P010 ? "p010(d3d12)" : "nv12(d3d12)";
    } else if (const auto* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format))) {
        info_.videoPixelFormatName = name;
    }
    if (framesRead_ == 0) {
        veyra::log::info("source-file", std::format(
            "first-frame codec={} format={} hw={} hdr={} matrix={} transfer={} range={} chromaLocation={}",
            info_.videoCodecName, info_.videoPixelFormatName, decoder_.hardwareActive(), out.colorInfo.isHdrPath(),
            static_cast<int>(out.colorInfo.matrix), static_cast<int>(out.colorInfo.transfer),
            static_cast<int>(out.colorInfo.range), static_cast<int>(out.colorInfo.chromaLocation)));
        if(out.colorInfo.isHdrPath()){
            const auto* parameters=demuxer_.videoCodecParameters();
            auto side=[&](AVFrameSideDataType frameType,AVPacketSideDataType packetType,size_t minimum,const char*& origin)->const uint8_t*{
                if(const auto* data=av_frame_get_side_data(frame,frameType);data&&data->size>=minimum){origin="frame";return data->data;}
                if(parameters)if(const auto* data=av_packet_side_data_get(parameters->coded_side_data,parameters->nb_coded_side_data,packetType);data&&data->size>=minimum){origin="stream";return data->data;}
                origin="unavailable";return nullptr;
            };
            const char *masteringOrigin=nullptr,*contentOrigin=nullptr;double minNits=-1,maxNits=-1;int64_t maxCll=-1,maxFall=-1;
            if(const auto* data=side(AV_FRAME_DATA_MASTERING_DISPLAY_METADATA,AV_PKT_DATA_MASTERING_DISPLAY_METADATA,sizeof(AVMasteringDisplayMetadata),masteringOrigin)){
                const auto& value=*reinterpret_cast<const AVMasteringDisplayMetadata*>(data);
                if(value.has_luminance&&value.min_luminance.den>0&&value.max_luminance.den>0){minNits=av_q2d(value.min_luminance);maxNits=av_q2d(value.max_luminance);}
            }
            if(const auto* data=side(AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,AV_PKT_DATA_CONTENT_LIGHT_LEVEL,sizeof(AVContentLightMetadata),contentOrigin)){
                const auto& value=*reinterpret_cast<const AVContentLightMetadata*>(data);maxCll=value.MaxCLL;maxFall=value.MaxFALL;
            }
            log::info("hdr-metadata",std::format("masteringSource={} masteringMinNits={} masteringMaxNits={} contentSource={} maxCLL={} maxFALL={} (-1=unavailable; 0 content level=unspecified); SDR mapping validates peak declarations separately",masteringOrigin,minNits,maxNits,contentOrigin,maxCll,maxFall));
        }
    }
    out.sourceEpoch = epoch_;

    uint32_t flags = 0;
    if (framesRead_ == 0) { flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Open); }
    if (pendingSeekFlag_) {
        flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Seek);
        pendingSeekFlag_ = false;
    }
    const int64_t ptsUs = rationalToUs(out.pts);
    if (lastPtsUs_ != INT64_MIN && !out.pts.isUnknown() && ptsUs < lastPtsUs_) {
        flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
    }
    if (!out.pts.isUnknown()) { lastPtsUs_ = ptsUs; }
    out.flags = flags;

    // The canonical linear working texture is produced by the graph's
    // ingress conversion; the source hands over the decoded frame view.
    out.color.resource = nullptr;
    // Hardware surfaces that do not travel inside the AVFrame (D3D11VA) are
    // published per frame; every other path clears the field so the ingress
    // never sees a stale texture from an earlier frame.
    if (decoder_.usingD3D11Frames()) {
        const auto& view = decoder_.hardwareSurface();
        out.hardwareSurface.texture = view.texture;
        out.hardwareSurface.subresourceIndex = view.subresourceIndex;
        out.hardwareSurface.waitFence = view.waitFence;
        out.hardwareSurface.waitValue = view.waitValue;
        out.hardwareSurface.textureWidth = view.textureWidth;
        out.hardwareSurface.textureHeight = view.textureHeight;
    } else {
        out.hardwareSurface = pipeline::HardwareSurfaceInput{};
    }

    ++framesRead_;
    if (decodedFrame != nullptr) { *decodedFrame = frame; }
    return SourceReadStatus::Frame;
}

bool MediaFileSource::seek(const pipeline::Rational& targetSeconds)
{
    if (!info_.opened) { return false; }
    if (targetSeconds.isUnknown() || targetSeconds.den == 0 || targetSeconds.isNegative()) {
        veyra::log::error("source-file", "seek target must be a known non-negative rational");
        return false;
    }
    const int64_t targetUs = rationalToUs(targetSeconds);
    if (!demuxer_.seekToUs(targetUs)) { return false; }
    decoder_.flushBuffers();
    // No profile-5 conversion state survives a seek: the next frame has to
    // convert from its own RPU, exactly like the same frame after a fresh open.
    info_.color.dolbyVisionP5 = {};
    errorMessage_.clear();
    draining_ = false;
    eofSignalled_ = false;
    pendingSeekFlag_ = true;
    lastPtsUs_ = INT64_MIN;
    ++epoch_;
    ++seekCount_;
    veyra::log::info("source-file", std::format("seek targetUs={} epoch={}", targetUs, epoch_));
    return true;
}

void MediaFileSource::close() noexcept
{
    errorMessage_.clear();
    decoder_.close();
    demuxer_.close();
    info_ = SourceInfo{};
    path_.clear();
    preferHardwareDecode_ = false;
    requireHardwareDecode_ = false;
}

} // namespace veyra::source
