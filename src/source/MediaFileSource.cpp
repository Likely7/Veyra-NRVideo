#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/source/DolbyVisionRpu.h"
#include "veyra/pipeline/DolbyVisionP7.h"

#include <climits>
#include <cstddef>
#include <cmath>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <string_view>
#include <vector>

#include "veyra/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/hwcontext_d3d12va.h>
}

#include <thread>
#include <algorithm>
namespace veyra::source {
namespace {
// Custom (P5): dump the RPU compositing parameters of the first frames so the
// BL+EL reconstruction can be prototyped and validated outside the renderer.
// Gated by VEYRA_DOVI_MAPPING_DUMP=<path>; never on in production.
void dumpDoviMapping(const AVDOVIMetadata& meta, unsigned frameIndex) {
    static const char* path = std::getenv("VEYRA_DOVI_MAPPING_DUMP");
    static std::ofstream out;
    static bool opened = false;
    static unsigned written = 0;
    if (path == nullptr || path[0] == '\0') return;
    if (!opened) { out.open(path, std::ios::trunc); opened = true; }
    if (!out || written >= 12) return;
    ++written;
    const auto& h = *av_dovi_get_header(&meta);
    const auto& m = *av_dovi_get_mapping(&meta);
    out << "{\"frame\":" << frameIndex
        << ",\"coefLog2Denom\":" << int(h.coef_log2_denom)
        << ",\"blBitDepth\":" << int(h.bl_bit_depth)
        << ",\"elBitDepth\":" << int(h.el_bit_depth)
        << ",\"vdrBitDepth\":" << int(h.vdr_bit_depth)
        << ",\"disableResidual\":" << int(h.disable_residual_flag)
        << ",\"extMappingIdc\":" << int(h.ext_mapping_idc_0_4)
        << ",\"blFullRange\":" << int(h.bl_video_full_range_flag)
        << ",\"nlqMethod\":" << int(m.nlq_method_idc)
        << ",\"curves\":[";
    for (int c = 0; c < 3; ++c) {
        const auto& cv = m.curves[c];
        const int pieces = cv.num_pivots > 0 ? cv.num_pivots - 1 : 0;
        if (c) out << ',';
        out << "{\"numPivots\":" << int(cv.num_pivots) << ",\"pivots\":[";
        for (int i = 0; i < int(cv.num_pivots); ++i) { if (i) out << ','; out << cv.pivots[i]; }
        out << "],\"mappingIdc\":[";
        for (int i = 0; i < pieces; ++i) { if (i) out << ','; out << int(cv.mapping_idc[i]); }
        out << "],\"polyOrder\":[";
        for (int i = 0; i < pieces; ++i) { if (i) out << ','; out << int(cv.poly_order[i]); }
        out << "],\"polyCoef\":[";
        for (int i = 0; i < pieces; ++i)
            for (int k = 0; k < 3; ++k) { out << (i || k ? "," : "") << cv.poly_coef[i][k]; }
        out << "],\"mmrOrder\":[";
        for (int i = 0; i < pieces; ++i) { if (i) out << ','; out << int(cv.mmr_order[i]); }
        out << "],\"mmrConstant\":[";
        for (int i = 0; i < pieces; ++i) { if (i) out << ','; out << cv.mmr_constant[i]; }
        out << "],\"mmrCoef\":[";
        for (int i = 0; i < pieces; ++i)
            for (int o = 0; o < 3; ++o)
                for (int j = 0; j < 7; ++j) out << (i || o || j ? "," : "") << cv.mmr_coef[i][o][j];
        out << "]}";
    }
    out << "],\"nlq\":[";
    for (int c = 0; c < 3; ++c) {
        const auto& n = m.nlq[c];
        if (c) out << ',';
        out << "{\"offset\":" << n.nlq_offset << ",\"inMax\":" << n.vdr_in_max
            << ",\"slope\":" << n.linear_deadzone_slope
            << ",\"threshold\":" << n.linear_deadzone_threshold << "}";
    }
    out << "]}\n";
}

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

// Custom (P5): the Dolby Vision enhancement layer travels inside each access unit
// as private NAL type 63, framed as 0x7E 0x01 followed by a complete standard NAL.
// Strip the framing and hand the decoder only the inner NALs.
std::vector<std::vector<uint8_t>> extractEnhancementNals(const uint8_t* data, int size, unsigned lengthSize) {
    std::vector<std::vector<uint8_t>> out;
    if (data == nullptr || size < 8 || lengthSize < 1 || lengthSize > 4) return out;
    int pos = 0;
    while (pos + int(lengthSize) + 3 <= size) {
        uint32_t length = 0;
        for (unsigned b = 0; b < lengthSize; ++b) length = (length << 8) | data[pos + b];
        pos += int(lengthSize);
        if (length == 0 || pos + int(length) > size) break;
        const uint8_t* nal = data + pos;
        if (((nal[0] >> 1) & 0x3F) == 63 && length >= 4 && nal[0] == 0x7E && nal[1] == 0x01) {
            const int inner = int(length) - 2;
            if (pos + 2 + inner <= size) out.emplace_back(data + pos + 2, data + pos + 2 + inner);
        }
        pos += int(length);
    }
    return out;
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
    deinterlacer_.close();filterDraining_=false;
    // Custom (P7 fix): the frames held for pairing belong to the hardware decoder
    // that is about to be destroyed, and each one pins one of its d3d11va
    // surfaces. Drop them before the device goes away.
    for (auto& held : baseWaiting_) av_frame_free(&held.second);
    baseWaiting_.clear();
    if (deliveredHeld_ != nullptr) av_frame_free(&deliveredHeld_);
    decoder_.close();
    if (!demuxer_.shared()) {
        demuxer_.close();
        if (!demuxer_.open(path_,cancelled_)) {
            veyra::log::error("source-file", "software fallback could not reopen demuxer");
            return false;
        }
    }
    const auto* params = demuxer_.videoCodecParameters();
    if (params == nullptr || !decoder_.openSoftware(params, demuxer_.videoTimeBaseNum(),
            demuxer_.videoTimeBaseDen(), softwareDecodeThreads(params->width, params->height))) {
        veyra::log::error("source-file", "software fallback decoder open failed");
        return false;
    }
    info_.hardwareDecodeActive = false;
    if (midStream && resumeUs != INT64_MIN) {
        // A live session cannot go back: the software decoder continues at the current position.
        if (demuxer_.shared() && !demuxer_.seekable()) veyra::log::warn("source-file", "software fallback on a live source continues at the current position");
        else if (!demuxer_.seekToUs(std::max<int64_t>(0, resumeUs))) veyra::log::warn("source-file", "software fallback could not seek back; continuing from stream start");
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

// Custom (P7): FFmpeg's own Dolby Vision splitter. It emits exactly the
// enhancement layer bitstream (private NAL type 63 with its two-byte wrapper
// stripped), takes the EL's parameter sets from the container's hvcE record and
// halves the extent for a 4K base layer. Nothing has to be re-derived from the
// stream, so the EL decoder also survives a seek without losing its parameter
// sets. Requires libavformat to have surfaced AV_PKT_DATA_HEVC_CONF.
bool MediaFileSource::openEnhancementSplitter(const AVCodecParameters* params) {
    closeEnhancementSplitter();
    if (params == nullptr) return false;
    const AVBitStreamFilter* filter = av_bsf_get_by_name("dovi_split");
    if (filter == nullptr) {
        veyra::log::info("source-dovi-el", "dovi_split bitstream filter unavailable; using the built-in NAL walk");
        return false;
    }
    AVBSFContext* context = nullptr;
    if (av_bsf_alloc(filter, &context) < 0) return false;
    const auto fail = [&context]() {
        av_bsf_free(&context);
        return false;
    };
    if (avcodec_parameters_copy(context->par_in, params) < 0) return fail();
    // Without the enhancement layer's own configuration record the filter would
    // hand the EL decoder the base layer's hvcC: the wrong extent and the wrong
    // parameter sets. The walk sniffs them from the stream instead.
    if (av_packet_side_data_get(context->par_in->coded_side_data,
            context->par_in->nb_coded_side_data, AV_PKT_DATA_HEVC_CONF) == nullptr) {
        veyra::log::info("source-dovi-el", "container carries no hvcE record; using the built-in NAL walk");
        return fail();
    }
    context->time_base_in = AVRational{demuxer_.videoTimeBaseNum(), demuxer_.videoTimeBaseDen()};
    if (av_opt_set(context, "mode", "el", AV_OPT_SEARCH_CHILDREN) < 0) return fail();
    if (av_bsf_init(context) < 0) return fail();
    elSplitter_ = context;
    elSplitterActive_ = true;
    veyra::log::info("source-dovi-el", std::format("dovi_split filter ready mode=el EL {}x{} extradata={} bytes",
        context->par_out->width, context->par_out->height, context->par_out->extradata_size));
    return true;
}

void MediaFileSource::closeEnhancementSplitter() {
    av_bsf_free(&elSplitter_);
    elSplitterActive_ = false;
}

// Owned packet with this access unit's enhancement layer, or null when the unit
// carries none (the filter reports EAGAIN, which is the normal sparse case).
AVPacket* MediaFileSource::splitEnhancementPacket(const AVPacket* packet) {
    if (elSplitter_ == nullptr || packet == nullptr) return nullptr;
    AVPacket* input = av_packet_clone(packet);
    if (input == nullptr) return nullptr;
    const int sent = av_bsf_send_packet(elSplitter_, input);
    av_packet_free(&input);   // the filter holds the reference either way
    if (sent < 0) {
        av_bsf_flush(elSplitter_);
        return nullptr;
    }
    AVPacket* el = av_packet_alloc();
    if (el == nullptr) return nullptr;
    const int received = av_bsf_receive_packet(elSplitter_, el);
    if (received < 0) {
        if (received != AVERROR(EAGAIN) && received != AVERROR_EOF) av_bsf_flush(elSplitter_);
        av_packet_free(&el);
        return nullptr;
    }
    return el;
}

// Fallback splitter: the enhancement layer travels as private NAL type 63 framed
// as 0x7E 0x01 followed by a complete standard NAL, and its VPS/SPS/PPS appear in
// the first access unit that carries one.
AVPacket* MediaFileSource::buildEnhancementPacket(const AVPacket* packet) {
    if (packet == nullptr || packet->data == nullptr) return nullptr;
    const auto nals = extractEnhancementNals(packet->data, packet->size, 4);
    if (nals.empty()) return nullptr;
    if (!elLayerOpen_) {
        std::vector<const std::vector<uint8_t>*> sets;
        for (const auto& nal : nals) {
            if (nal.size() < 2) continue;
            const unsigned type = (nal[0] >> 1) & 0x3F;
            if (type == 32 || type == 33 || type == 34) sets.push_back(&nal);
        }
        if (sets.size() < 3) return nullptr;  // parameter sets not complete yet
        std::vector<uint8_t> extradata;
        for (const auto* set : sets) {
            elParamSets_.push_back(*set);
            const uint8_t startCode[4] = {0, 0, 0, 1};
            extradata.insert(extradata.end(), startCode, startCode + 4);
            extradata.insert(extradata.end(), set->begin(), set->end());
        }
        AVCodecParameters* par = avcodec_parameters_alloc();
        if (par == nullptr) return nullptr;
        par->codec_type = AVMEDIA_TYPE_VIDEO;
        par->codec_id = AV_CODEC_ID_HEVC;
        par->format = AV_PIX_FMT_YUV420P10LE;
        par->extradata = static_cast<uint8_t*>(av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (par->extradata == nullptr) { avcodec_parameters_free(&par); return nullptr; }
        std::memcpy(par->extradata, extradata.data(), extradata.size());
        par->extradata_size = int(extradata.size());
        const bool opened = openEnhancementDecoder(par, 0, 0);
        avcodec_parameters_free(&par);
        if (!opened) {
            elLayerEnabled_ = false;
            return nullptr;
        }
    }
    std::vector<uint8_t> accessUnit;
    // Only VCL NALs (types 0-31) belong in the packet stream. The type 63 payloads
    // also carry SEI and other private data; handing those to the decoder made it
    // discard whole access units.
    for (const auto& nal : nals) {
        if (nal.size() < 2) continue;
        const unsigned type = (nal[0] >> 1) & 0x3F;
        if (type > 31) continue;
        const uint8_t startCode[4] = {0, 0, 0, 1};
        accessUnit.insert(accessUnit.end(), startCode, startCode + 4);
        accessUnit.insert(accessUnit.end(), nal.begin(), nal.end());
    }
    if (accessUnit.empty()) return nullptr;
    AVPacket* elPacket = av_packet_alloc();
    if (elPacket == nullptr) return nullptr;
    if (av_new_packet(elPacket, int(accessUnit.size())) < 0) { av_packet_free(&elPacket); return nullptr; }
    std::memcpy(elPacket->data, accessUnit.data(), accessUnit.size());
    elPacket->pts = packet->pts;
    elPacket->dts = packet->dts;
    return elPacket;
}

bool MediaFileSource::openEnhancementDecoder(const AVCodecParameters* elParams, int timeBaseNum, int timeBaseDen) {
    if (elParams == nullptr) return false;
    // lowLatency: frame-level threading buffers frames before emitting them, which
    // made the decoded enhancement layer lag the base frame by one or two and
    // painted stale residuals onto the picture. tolerateErrors: the residual stream
    // is sparse by design, so its reference chain is incomplete on purpose.
    elLayerOpen_ = elDecoder_.openSoftware(elParams, timeBaseNum, timeBaseDen, 2,
        /*lowLatency=*/true, /*tolerateErrors=*/true);
    if (!elLayerOpen_) {
        veyra::log::error("source-dovi-el", "enhancement layer decoder refused the parameter sets");
        return false;
    }
    veyra::log::info("source-dovi-el", std::format("enhancement layer decoder opened extradata={} bytes {}x{}",
        elParams->extradata_size, elDecoder_.width(), elDecoder_.height()));
    return true;
}

// Custom (P7): pairing tolerance instead of exact equality. The EL decoder runs
// one frame behind the base decoder by construction, so the oldest waiting base
// frame is the one that gets its layer first; see read().
bool MediaFileSource::takeElNear(int64_t pts, AVFrame** outFrame) {
    if (outFrame == nullptr || pts == AV_NOPTS_VALUE) return false;
    const auto it = elByPts_.lower_bound(pts - elMatchTolerance_);
    if (it == elByPts_.end() || it->first - elMatchTolerance_ > pts) return false;
    av_frame_free(outFrame);
    *outFrame = it->second;
    elByPts_.erase(it);
    return true;
}

// Custom (P7): an enhancement frame older than the OLDEST waiting base frame can
// never be used again: its own base picture has already been delivered. The bound
// has to be the oldest waiting frame, not whichever frame is being probed, or the
// layer of an older frame still in the queue would be thrown away.
void MediaFileSource::dropStaleEl(int64_t oldestBasePts) {
    if (oldestBasePts == AV_NOPTS_VALUE) return;
    while (!elByPts_.empty() && elByPts_.begin()->first + elMatchTolerance_ < oldestBasePts) {
        av_frame_free(&elByPts_.begin()->second);
        elByPts_.erase(elByPts_.begin());
    }
}

// Custom (P5): decode the Dolby Vision enhancement layer that rides inside the
// same access units as the base layer. Any failure disables the path instead of
// disturbing playback.
bool MediaFileSource::feedEnhancementLayer(const AVPacket* packet) {
    const auto elStart = std::chrono::steady_clock::now();

    if (!elLayerEnabled_ || packet == nullptr || packet->data == nullptr || packet->size <= 0) return true;
    // The enhancement layer is sparse by design here: only about half the base
    // frames carry one, in a repeating pattern. An empty packet is normal, and it
    // must report "no EL for this frame" so the caller does not reuse the texture
    // uploaded for the previous one.
    AVPacket* elPacket = elSplitterActive_ ? splitEnhancementPacket(packet) : buildEnhancementPacket(packet);
    if (elPacket == nullptr) return false;
    const bool sent = elDecoder_.sendPacket(elPacket);
    av_packet_free(&elPacket);
    if (!sent) {
        if (++elLayerMisses_ == 30) {
            veyra::log::warn("source-dovi-el", "enhancement layer decoder rejects packets; disabling");
            elLayerEnabled_ = false;
            closeEnhancementLayer();
        }
        return true;
    }
    bool gotFrame = false;
    for (;;) {
        const AVFrame* decoded = elDecoder_.receiveFrame();
        if (decoded == nullptr) break;
        // Custom (P5): queue by timestamp; the base frame that owns this layer may
        // still be in flight (the EL decoder runs a frame behind).
        AVFrame* copy = av_frame_clone(decoded);
        if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr) {
            static unsigned storeDbg = 0;
            if (++storeDbg <= 8) {
                const int64_t rawBet = decoded->best_effort_timestamp;
                const int64_t rawPts = decoded->pts;
                const int64_t chosen = rawBet != AV_NOPTS_VALUE ? rawBet : rawPts;
                log::info("source-dovi-el", std::format(
                    "store candidate clone={} bet={} pts={} chosen={} cache={}",
                    copy ? 1 : 0, rawBet, rawPts, chosen, elByPts_.size()));
            }
        }
        if (copy != nullptr) {
            elLastWidth_ = uint32_t(copy->width);
            elLastHeight_ = uint32_t(copy->height);
            const int64_t elPts = copy->best_effort_timestamp != AV_NOPTS_VALUE
                ? copy->best_effort_timestamp : copy->pts;
            if (elPts != AV_NOPTS_VALUE) {
                auto it = elByPts_.find(elPts);
                if (it != elByPts_.end()) av_frame_free(&it->second);
                elByPts_[elPts] = copy;
                while (elByPts_.size() > 8) {
                    av_frame_free(&elByPts_.begin()->second);
                    elByPts_.erase(elByPts_.begin());
                }
            } else {
                av_frame_free(&copy);
            }
            ++elLayerFrames_;
            gotFrame = true;
            if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr) {
                const int64_t fp = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                    ? decoded->best_effort_timestamp : decoded->pts;
                if (packet->pts != AV_NOPTS_VALUE && fp != packet->pts) {
                    static unsigned mismatches = 0;
                    if (++mismatches <= 5)
                        log::warn("source-dovi-el", std::format(
                            "EL frame pts {} != packet pts {}", fp, packet->pts));
                }
            }
        }
    }
    // Custom (P5): once, report what the enhancement layer actually contains. A
    // frame of pure 512 would mean "zero residual" and the composite would have to
    // be identical to the base layer, so this distinguishes "no EL on this frame"
    // from "EL present but ignored".
    {
        static bool residualLogged = false;
        if (!residualLogged && elFrame_ != nullptr && elFrame_->data[0] != nullptr &&
            elFrame_->format == AV_PIX_FMT_YUV420P10LE) {
            const auto* row = reinterpret_cast<const uint16_t*>(elFrame_->data[0]);
            const int stridePx = elFrame_->linesize[0] / 2;
            int minV = 1 << 20, maxV = -(1 << 20);
            int64_t sum = 0, count = 0;
            for (int y = 0; y < elFrame_->height; y += 8) {
                for (int x = 0; x < elFrame_->width; x += 8) {
                    const int v = int(row[size_t(y) * stridePx + x]) & 0x3FF;
                    minV = std::min(minV, v);
                    maxV = std::max(maxV, v);
                    sum += v;
                    ++count;
                }
            }
            // Frames without an EL decode to a flat 512 (zero residual), so keep
            // looking until one actually carries a signal.
            if (count > 0 && maxV > minV) {
                residualLogged = true;
                log::info("source-dovi-el", std::format(
                    "EL luma samples: min={} max={} mean={:.1f} (offset {} => residual {}..{})",
                    minV, maxV, double(sum) / double(count), 512, minV - 512, maxV - 512));
            }
        }
    }
    static double elTotalMs = 0.0;
    static unsigned elPackets = 0;
    elTotalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - elStart).count();
    ++elPackets;
    if (elLayerFrames_ <= 3 || (elLayerFrames_ && elLayerFrames_ % 100 == 0))
        veyra::log::info("source-dovi-el", std::format("decoded={} status={} format={} {}x{} avgCost={:.2f}ms over {} packets held={} waiting={}",
            elLayerFrames_, int(elDecoder_.receiveStatus()),
            elFrame_ ? int(elFrame_->format) : -1,
            elFrame_ ? elFrame_->width : 0, elFrame_ ? elFrame_->height : 0,
            elPackets ? elTotalMs / double(elPackets) : 0.0, elPackets,
            deliveredHeld_ != nullptr ? 1 : 0, baseWaiting_.size()));
    return gotFrame;
}

void MediaFileSource::closeEnhancementLayer() {
    elDecoder_.close();
    if (elFrame_ != nullptr) { av_frame_free(&elFrame_); elFrame_ = nullptr; }
    elParamSets_.clear();
    elLayerOpen_ = false;
}


// Custom (P5): pull every decodable enhancement frame into the PTS table.
void MediaFileSource::drainElFrames() {
    if (!elLayerOpen_) return;
    for (;;) {
        const AVFrame* decoded = elDecoder_.receiveFrame();
        if (decoded == nullptr) break;
        AVFrame* copy = av_frame_clone(decoded);
        if (copy == nullptr) continue;
        const int64_t elPts = copy->best_effort_timestamp != AV_NOPTS_VALUE
            ? copy->best_effort_timestamp : copy->pts;
        if (elPts == AV_NOPTS_VALUE) { av_frame_free(&copy); continue; }
        auto it = elByPts_.find(elPts);
        if (it != elByPts_.end()) av_frame_free(&it->second);
        elByPts_[elPts] = copy;
        while (elByPts_.size() > 8) {
            av_frame_free(&elByPts_.begin()->second);
            elByPts_.erase(elByPts_.begin());
        }
    }
}

// Custom (P5): hold a base frame for at most a couple of frames while its
// enhancement layer catches up, then deliver it without one. This is the only way
// to pair them: the EL decoder reorders, so the layer for frame N is only decoded
// after frame N has already been produced by the base decoder.
SourceReadStatus MediaFileSource::read(pipeline::FramePacket& out, const AVFrame** decodedFrame) {
    if (decodedFrame != nullptr) *decodedFrame = nullptr;
    // Custom (P7 fix): the caller has finished with the frame it borrowed from
    // the previous call. Releasing it here is what keeps the fixed d3d11va
    // surface array from running dry; see the member comment in the header.
    if (deliveredHeld_ != nullptr) av_frame_free(&deliveredHeld_);
    if (!elLayerEnabled_) return readBase(out, decodedFrame);

    // Custom (P7): a MEL layer declares in its RPU that the layer carries no
    // residual at all, so composing it only burns decode time. readBase() raises
    // the flag once the evidence is consistent, and the EL path is dropped here,
    // before anything is handed out.
    if (elMelDrop_) {
        elMelDrop_ = false;
        elLayerEnabled_ = false;
        veyra::log::info("source-dovi-el",
            "RPU declares no usable residual on the observed frames; MEL layer, composing skipped");
        closeEnhancementLayer();
        for (auto& entry : elByPts_) av_frame_free(&entry.second);
        elByPts_.clear();
        return readBase(out, decodedFrame);
    }

    auto ptsOf = [](const AVFrame* f) -> int64_t {
        if (f == nullptr) return AV_NOPTS_VALUE;
        return f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    };

    for (int spin = 0; spin < 4; ++spin) {
        drainElFrames();
        // The oldest waiting base frame defines what is still usable.
        if (!baseWaiting_.empty()) dropStaleEl(ptsOf(baseWaiting_.front().second));
        // A held frame whose layer has arrived can be delivered now.
        for (auto it = baseWaiting_.begin(); it != baseWaiting_.end(); ++it) {
            AVFrame* el = nullptr;
            if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr) {
                static unsigned waitDbg = 0;
                if (++waitDbg <= 10) {
                    const AVFrame* wf = it->second;
                    std::string keys;
                    for (const auto& kv : elByPts_) keys += std::format("{} ", kv.first);
                    log::info("source-dovi-el", std::format(
                        "wait check pts={} bet={} frame_pts={} cached=[{}]",
                        ptsOf(wf),
                        wf ? wf->best_effort_timestamp : int64_t(-999),
                        wf ? wf->pts : int64_t(-999), keys));
                }
            }
            if (takeElNear(ptsOf(it->second), &el)) {
                if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr) {
                    static unsigned okDbg = 0;
                    if (++okDbg <= 8)
                        log::info("source-dovi-el", std::format("PAIRED held pts={} el={}", ptsOf(it->second), el?1:0));
                }
                // Ownership moves to the member: takeElNear removed it from the
                // table, so someone has to free it on the next match.
                if (elFrame_ != nullptr) av_frame_free(&elFrame_);
                elFrame_ = el;
                out = it->first;
                out.doviEl = elFrame_;
                if (decodedFrame != nullptr) *decodedFrame = it->second;
                deliveredHeld_ = it->second;   // Custom (P7 fix): freed on the next read()
                baseWaiting_.erase(it);
                return SourceReadStatus::Frame;
            }
        }
        // Held too long: deliver the oldest without a layer rather than stall.
        if (baseWaiting_.size() >= 3) {
            out = baseWaiting_.front().first;
            out.doviEl = nullptr;
            if (decodedFrame != nullptr) *decodedFrame = baseWaiting_.front().second;
            deliveredHeld_ = baseWaiting_.front().second;
            baseWaiting_.pop_front();
            return SourceReadStatus::Frame;
        }

        pipeline::FramePacket pkt{};
        const AVFrame* frame = nullptr;
        const auto status = readBase(pkt, &frame);
        if (status != SourceReadStatus::Frame) return status;
        drainElFrames();

        AVFrame* el = nullptr;
        {
            static unsigned dbg = 0;
            if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr && ++dbg <= 12) {
                const int64_t bp = ptsOf(frame);
                std::string keys;
                for (const auto& kv : elByPts_) keys += std::format("{} ", kv.first);
                log::info("source-dovi-el", std::format("attempt base_pts={} cached=[{}] held={}", bp, keys, baseWaiting_.size()));
            }
        }
        if (takeElNear(ptsOf(frame), &el)) {
            if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr) {
                static unsigned okDbg2 = 0;
                if (++okDbg2 <= 8)
                    log::info("source-dovi-el", std::format("PAIRED direct pts={} el={}", ptsOf(frame), el?1:0));
            }
            if (elFrame_ != nullptr) av_frame_free(&elFrame_);
            elFrame_ = el;
            pkt.doviEl = elFrame_;
            out = pkt;
            if (decodedFrame != nullptr) *decodedFrame = frame;
            return SourceReadStatus::Frame;
        }

        // No layer yet: keep this frame and fetch the next one so the EL decoder
        // advances. The caller sees the frames in the same order, one frame later.
        AVFrame* held = av_frame_clone(frame);
        if (held == nullptr) {
            out = pkt;
            if (decodedFrame != nullptr) *decodedFrame = frame;
            return SourceReadStatus::Frame;
        }
        baseWaiting_.emplace_back(std::move(pkt), held);
    }

    // Defensive: deliver whatever is oldest so playback cannot stall.
    if (!baseWaiting_.empty()) {
        out = baseWaiting_.front().first;
        out.doviEl = nullptr;
        if (decodedFrame != nullptr) *decodedFrame = baseWaiting_.front().second;
        deliveredHeld_ = baseWaiting_.front().second;
        baseWaiting_.pop_front();
        return SourceReadStatus::Frame;
    }
    return readBase(out, decodedFrame);
}

pipeline::ColorDescription MediaFileSource::parseColor(const AVCodecParameters* params) const
{    pipeline::ColorDescription cd;
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
    cancelled_ = desc.cancelled;
    deinterlaceEnabled_=desc.deinterlace;filterDraining_=false;
    preferHardwareDecode_ = desc.preferHardwareDecode && desc.d3d12Device != nullptr && desc.d3d12Queue != nullptr;
    requireHardwareDecode_ = desc.requireHardwareDecode;
    nonBlocking_ = desc.nonBlocking;
    corruptPolicy_ = desc.network.corrupt;
    resetCorruptionState();
    if (media::sharedDemuxInput(desc.path, desc.nonBlocking)) {
        // Network addresses and files on a server: one connection, read ahead of playback,
        // shared with the audio pipeline (docs/NETWORK_BUFFER_48_PLAN_2026-10-17.md).
        media::DemuxSession::OpenParams session;
        session.path = desc.path;
        session.settings = desc.network;
        session.cancelled = desc.cancelled;
        session.audioStream = desc.nonBlocking ? -2 : -1;   // offline readers (export, tools) take no audio
        std::wstring failure;
        auto opened = media::DemuxSession::open(session, &failure);
        if (!opened || !demuxer_.openShared(std::move(opened))) {
            errorMessage_ = failure.empty() ? L"无法打开视频，请查看诊断" : failure;
            veyra::log::error("source-file", "shared demux session open failed");
            return false;
        }
    } else if (!demuxer_.open(desc.path,cancelled_)) {
        veyra::log::error("source-file", "demuxer open failed");
        return false;
    }
    // Damaged data is tolerated on the player's network sources and MPEG-TS; export
    // and every offline user keep the strict stop.
    tolerant_ = desc.tolerateCorruption && (media::networkMediaInput(desc.path) || demuxer_.formatName().find("mpegts") != std::string::npos);
    veyra::log::info("source-file", "stage: demux opened, reading codec parameters");
    const AVCodecParameters* params = demuxer_.videoCodecParameters();
    if (params == nullptr) {
        veyra::log::error("source-file", "no video codec parameters");
        return false;
    }
    veyra::log::info("source-file", std::format("stage: codec={} {}x{} preferHw={} device={}",
        avcodec_get_name(params->codec_id), params->width, params->height,
        desc.preferHardwareDecode ? 1 : 0, desc.d3d12Device ? 1 : 0));

    bool decoderOpen = false;
    if (desc.preferHardwareDecode && desc.d3d12Device != nullptr && desc.d3d12Queue != nullptr) {
        // HEVC uses D3D11VA on purpose. On some drivers (RTX 5070 + 616.56) the
        // first HEVC picture of the D3D12VA decoder faults the driver and the
        // SHARED D3D12 device is removed, which took the whole session down
        // instead of falling back (docs/DIAG_HEVC_D3D12VA_AND_EXPORT_CFR_2026-09-17.md).
        // The D3D11VA path decodes on its own device and shares the surfaces as
        // NT handles, so a failure there can only cost a software fallback.
        const bool interlaced=params->field_order!=AV_FIELD_UNKNOWN&&params->field_order!=AV_FIELD_PROGRESSIVE;
        if (params->codec_id == AV_CODEC_ID_HEVC||interlaced) {
            decoderOpen = decoder_.openD3D11VA(params, demuxer_.videoTimeBaseNum(),
                demuxer_.videoTimeBaseDen(), desc.d3d12AdapterLuid,
                static_cast<ID3D12Device*>(desc.d3d12Device));
            if (decoderOpen) {
                veyra::log::info("source-file", "D3D11VA decode active (HEVC/interlaced compatibility; NT-handle surfaces shared to D3D12)");
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
        // Custom (P5): bring-up switch for the enhancement layer. Off unless the
        // environment asks for it, so BL playback stays byte identical until the
        // compositing path is complete.
        elLayerEnabled_ = dv.profile == 7 && dv.enhancementLayer && desc.decodeDolbyEnhancementLayer;
        if (elLayerEnabled_) {
            // The pairing path keeps up to three base frames in baseWaiting_ plus
            // the one handed to the caller, on top of whatever the decode loop
            // already retains. d3d11va allocates a FIXED surface array sized for
            // the codec alone, so the pool runs dry a dozen frames into playback
            // and avcodec_send_packet fails with ENOMEM (-12). Reserve headroom
            // before the first frame creates the hardware frames context.
            decoder_.setExtraHardwareFrames(10);
            // The EL frame inherits the base packet's timestamp, so pairing only
            // has to absorb a rounding difference, not a whole frame interval.
            elMatchTolerance_ = 2;
            elMelStrikes_ = 0;
            elMelDrop_ = false;
            // Prefer FFmpeg's splitter: it also supplies the EL's own parameter
            // sets, which the walk below can only sniff from the first access unit
            // that happens to carry them (and cannot recover after a seek).
            if (openEnhancementSplitter(params) &&
                !openEnhancementDecoder(elSplitter_->par_out,
                    demuxer_.videoTimeBaseNum(), demuxer_.videoTimeBaseDen())) {
                closeEnhancementSplitter();
                elLayerEnabled_ = false;
            }
            if (elLayerEnabled_) log::info("source-dovi-el", "enhancement layer decode requested");
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

SourceReadStatus MediaFileSource::readBase(pipeline::FramePacket& out, const AVFrame** decodedFrame)
{
    if (decodedFrame != nullptr) { *decodedFrame = nullptr; }
    out = pipeline::FramePacket{};
    if (!info_.opened || !errorMessage_.empty()) { return SourceReadStatus::Error; }

    {
        // A long gap between read() calls (pause, stall) must not count as "no output".
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReadCall_ > std::chrono::seconds(2)) noOutputSince_ = {};
        lastReadCall_ = now;
    }
readAgain:   // a damaged picture was dropped (corrupt policy WaitKeyframe): fetch the next one
    const AVFrame* frame = nullptr;
    // Whether the packet that produced this frame carried enhancement layer data;
    // stale EL must not be reused for a frame that has none.
    bool lastFrameHadEnhancementLayer = false;
    for (;;) {
        if(deinterlacer_.active()){
            const int result=deinterlacer_.receive(&frame);
            if(result>=0)break;
            if(result==AVERROR_EOF)return SourceReadStatus::Eos;
            if(result!=AVERROR(EAGAIN)||filterDraining_){errorMessage_=L"去隔行输出失败，请查看诊断";return SourceReadStatus::Error;}
        }
        frame = decoder_.receiveFrame();
        if(frame){
            if(deinterlaceEnabled_&&((frame->flags&AV_FRAME_FLAG_INTERLACED)||deinterlacer_.active())){
                const bool damaged=frame->decode_error_flags||(frame->flags&AV_FRAME_FLAG_CORRUPT);
                if(frame->width!=int(info_.width)||frame->height!=int(info_.height)||(damaged&&!tolerant_)){
                    errorMessage_=L"隔行视频画面损坏或尺寸改变，已停止处理";return SourceReadStatus::Error;
                }
                // Interlaced network TV (1080i IPTV) loses packets like any other stream:
                // the same damaged-picture policy applies before the picture reaches the filter.
                if(damaged&&!keepDamagedPicture(frame))continue;
                const auto* surface=decoder_.usingD3D11Frames()?&decoder_.hardwareSurface():nullptr;
                if(!deinterlacer_.active()){
                    AVRational rate{demuxer_.nominalRateNum(),demuxer_.nominalRateDen()};
                    if(demuxer_.averageFps()>0)rate=av_d2q(demuxer_.averageFps(),1000000);
                    if(frame->duration>0)av_reduce(&rate.num,&rate.den,decoder_.frameTimeBaseDen(),frame->duration*decoder_.frameTimeBaseNum(),INT_MAX);
                    if(!deinterlacer_.open(frame,surface,decoder_.frameTimeBaseNum(),decoder_.frameTimeBaseDen(),rate.num,rate.den)){
                        errorMessage_=L"去隔行组件不可用或不支持当前格式；请关闭去隔行或改用软解";return SourceReadStatus::Error;
                    }
                    info_.deinterlaced=true;info_.deinterlacePath=deinterlacer_.path();
                    info_.averageFps=av_q2d(rate)*2;info_.nominalRateNum=rate.num*2;info_.nominalRateDen=rate.den;
                }
                const int result=deinterlacer_.push(frame,surface);
                if(result<0){log::error("deinterlace",std::format("filter push failed code={}",result));errorMessage_=L"去隔行处理失败，请查看诊断";return SourceReadStatus::Error;}
                continue;
            }
            break;
        }
        if (decoder_.receiveStatus() == media::DecodeReceiveStatus::EndOfStream) {
            if(deinterlacer_.active()){
                if(deinterlacer_.push(nullptr)<0){errorMessage_=L"去隔行尾帧输出失败";return SourceReadStatus::Error;}
                filterDraining_=true;continue;
            }
            return SourceReadStatus::Eos;
        }
        if (decoder_.receiveStatus() == media::DecodeReceiveStatus::Error) {
            // Network data that the decoder rejects is skipped up to the next keyframe.
            if (tolerant_ && tolerateDecoderError()) {
                continue;
            }
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
        const auto got = demuxer_.readVideoPacketEx(nonBlocking_ ? 20 : 50, nonBlocking_);
        if (got == media::FFmpegDemuxer::ReadResult::Waiting) {
            // Playback is paused for buffering (or the next packet has not arrived):
            // nothing is lost, the caller retries and the picture stays on screen.
            if (nonBlocking_) {
                return SourceReadStatus::Waiting;
            }
            continue;
        }
        if (got == media::FFmpegDemuxer::ReadResult::Cancelled) {
            return SourceReadStatus::Error;   // the owner is stopping; no message
        }
        if (got == media::FFmpegDemuxer::ReadResult::Error) {
            const auto failure = demuxer_.shared() ? demuxer_.session()->failureMessage() : std::wstring{};
            errorMessage_ = failure.empty() ? L"源视频读取失败，已停止处理" : failure;
            return SourceReadStatus::Error;
        }
        if (got == media::FFmpegDemuxer::ReadResult::Eof) {
            draining_ = true;
            if (!decoder_.sendPacket(nullptr)) {
                errorMessage_ = L"视频尾帧解码失败，已停止处理";
                return SourceReadStatus::Error;
            }
            continue;
        }
        if (demuxer_.packetDiscontinuity()) {
            beginDiscontinuity();
        }
        // Packets keep arriving but nothing decodable comes out for 10 s: the connection
        // is delivering garbage. Start over on a fresh one (at most once per 30 s).
        if (tolerant_ && demuxer_.shared()) {
            const auto now = std::chrono::steady_clock::now();
            if (noOutputSince_ == std::chrono::steady_clock::time_point{}) noOutputSince_ = now;
            else if (now - noOutputSince_ >= std::chrono::seconds(10) && now - lastForcedReconnect_ >= std::chrono::seconds(30)) {
                lastForcedReconnect_ = now;
                noOutputSince_ = {};
                veyra::log::warn("source-file", "no decodable picture for 10 s while data keeps arriving; reconnecting");
                demuxer_.session()->requestReconnect("no decodable picture for 10 s");
            }
        }
        const AVPacket* packet = demuxer_.currentPacket();
        if (packet->flags & AV_PKT_FLAG_CORRUPT) {
            if (!tolerant_) {
                veyra::log::error("source-file", "corrupt video packet; refusing to skip source data");
                errorMessage_ = L"源视频包含损坏数据，已停止处理";
                return SourceReadStatus::Error;
            }
            ++corruptPacketsSeen_;
            const auto now = std::chrono::steady_clock::now();
            if (now - lastCorruptLog_ >= std::chrono::seconds(1)) {
                lastCorruptLog_ = now;
                veyra::log::warn("source-file", std::format("damaged video packet from the network (policy={} packetsSoFar={}); decoding continues",
                    corruptPolicy_ == media::CorruptPolicy::WaitKeyframe ? "wait-keyframe" : "conceal", corruptPacketsSeen_));
            }
            if (corruptPolicy_ == media::CorruptPolicy::WaitKeyframe) {
                startKeyframeWait();
            }
        }
        if (waitKeyframe_) {
            // A clean start after a damaged stretch is a keyframe that is not itself damaged. A stream
            // that never flags one must not freeze the picture: after 5 s decoding just continues.
            const bool clean = (packet->flags & AV_PKT_FLAG_KEY) && !(packet->flags & AV_PKT_FLAG_CORRUPT);
            if (!clean && std::chrono::steady_clock::now() - waitKeyframeSince_ < std::chrono::seconds(5)) {
                continue;
            }
            if (!clean) veyra::log::warn("source-file", "no clean keyframe within 5 s after damaged data; decoding continues without waiting");
            waitKeyframe_ = false;
        }
        // Custom (P5): feed the enhancement layer BEFORE the base layer so its
        // decoder has one frame of head start; otherwise the decoded EL frame lags
        // the base frame and stale residuals land on the picture.
        lastFrameHadEnhancementLayer = feedEnhancementLayer(packet);
        if (!decoder_.sendPacket(packet)) {
            if (tolerant_ && tolerateDecoderError()) {
                continue;
            }
            if (fallbackToSoftware("send-packet-error")) {
                continue;
            }
            errorMessage_ = L"视频解码失败，已停止处理，请检查源文件是否损坏";
            return SourceReadStatus::Error;
        }
    }

    // A drained filter owns its output independently of the decoder's latest
    // frame, which is cleared at decoder EOF. Validate the actual output.
    if (deinterlacer_.active() && frame->format == AV_PIX_FMT_D3D12) {
        const auto* surface = reinterpret_cast<const AVD3D12VAFrame*>(frame->data[0]);
        const auto desc = surface && surface->texture ? surface->texture->GetDesc() : D3D12_RESOURCE_DESC{};
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.MipLevels != 1 ||
            desc.SampleDesc.Count != 1 || desc.DepthOrArraySize != 1 ||
            (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) ||
            desc.Width < UINT64(frame->width) || desc.Height < UINT(frame->height)) {
            errorMessage_ = L"去隔行输出格式无法导入，已停止处理";
            return SourceReadStatus::Error;
        }
    } else if (!deinterlacer_.active() && decoder_.hardwareActive() && !decoder_.hardwareFrameImportable()) {
        if (fallbackToSoftware("unsupported-d3d12-surface")) {
            return read(out, decodedFrame);
        }
        errorMessage_ = L"硬件解码输出格式无法导入，且软件回退失败";
        return SourceReadStatus::Error;
    }

    // Deinterlaced output inherits the flags of a picture the policy already accepted
    // before the filter; checking it again would count the same loss once per field.
    if (!deinterlacer_.active() && ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags)) {
        if (!tolerant_) {
            veyra::log::error("source-file", std::format("corrupt decoded frame flags={} decodeErrors={}", frame->flags, frame->decode_error_flags));
            errorMessage_ = L"源视频包含损坏画面，已停止处理";
            return SourceReadStatus::Error;
        }
        if (!keepDamagedPicture(frame)) {
            goto readAgain;
        }
    }
    if (frame->width <= 0 || frame->height <= 0 || uint32_t(frame->width) != info_.width || uint32_t(frame->height) != info_.height) {
        veyra::log::error("source-file", std::format("frame extent changed: opened={}x{} decoded={}x{}; file processing stopped before upload", info_.width, info_.height, frame->width, frame->height));
        errorMessage_ = L"视频中途改变分辨率，当前文件处理不支持，已安全停止";
        return SourceReadStatus::Error;
    }

    ++sequence_;
    out.sequence = sequence_;
    const int tbNum = deinterlacer_.active()?deinterlacer_.timeBaseNum():decoder_.frameTimeBaseNum();
    const int tbDen = deinterlacer_.active()?deinterlacer_.timeBaseDen():decoder_.frameTimeBaseDen();
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
    // Level 1 (per-frame brightness). Read for every Dolby Vision profile and
    // logged under VEYRA_DOVI_TRACE so the wiring is provable.
    // The value belongs to THIS frame: assigning it before the extraction made
    // every frame carry the previous one's brightness, which the curve then
    // followed one frame late (and the first frame had none at all).
    doviFrameInfo = pipeline::DoviFrameInfo{};
    if (dv.present) {
        if (const auto* l1Side = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
            l1Side != nullptr && l1Side->size >= sizeof(AVDOVIMetadata)) {
            doviFrameInfo = extractDoviFrameInfo(
                *reinterpret_cast<const AVDOVIMetadata*>(l1Side->data), dv.profile);
            dumpDoviMapping(*reinterpret_cast<const AVDOVIMetadata*>(l1Side->data), sequence_);
        }
    }
    // Custom (P5): compare the base frame's timestamp with the enhancement frame''s.
    if (std::getenv("VEYRA_DOVI_EL_PTS") != nullptr && elFrame_ != nullptr && frame != nullptr) {
        static unsigned ptsPairs = 0;
        if (++ptsPairs <= 6) {
            const int64_t bp = frame->best_effort_timestamp != AV_NOPTS_VALUE
                ? frame->best_effort_timestamp : frame->pts;
            const int64_t ep = elFrame_->best_effort_timestamp != AV_NOPTS_VALUE
                ? elFrame_->best_effort_timestamp : elFrame_->pts;
            log::info("source-dovi-el", std::format("pair base_pts={} el_pts={} match={}",
                bp, ep, bp == ep ? 1 : 0));
        }
    }
    out.dovi = doviFrameInfo;
    // Custom (P5): hand the enhancement layer to whoever renders this frame. Null
    // for the base frames that carry none, which the reconstruction treats as zero
    // residual, and null entirely unless the EL path is enabled.
    out.doviEl = nullptr;
    if (elLayerEnabled_ && lastFrameHadEnhancementLayer && elFrame_ != nullptr && frame != nullptr) {
        const int64_t basePts = frame->best_effort_timestamp != AV_NOPTS_VALUE
            ? frame->best_effort_timestamp : frame->pts;
        const int64_t elPts = elFrame_->best_effort_timestamp != AV_NOPTS_VALUE
            ? elFrame_->best_effort_timestamp : elFrame_->pts;
        // Only a frame-for-frame match is usable. A lagging or repeated EL frame
        // would paint residuals from another picture onto this one.
        if (basePts != AV_NOPTS_VALUE && basePts == elPts) out.doviEl = elFrame_;
    }
    // Custom (P5): pack the reconstruction parameters now, where the RPU metadata
    // is still alive, so the renderer only has to read them. This must not depend on
    // the enhancement frame: pairing happens one layer up, so out.doviEl is still
    // null at this point even when this frame will get one.
    {
        if (const auto* metaSide = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
            metaSide != nullptr && metaSide->size >= sizeof(AVDOVIMetadata)) {
            const auto& meta = *reinterpret_cast<const AVDOVIMetadata*>(metaSide->data);
            pipeline::doviP7ConstantsSupported(*av_dovi_get_mapping(&meta), *av_dovi_get_header(&meta),
                elLastWidth_ != 0 ? elLastWidth_ : uint32_t(elFrame_ ? elFrame_->width : 1920),
                elLastHeight_ != 0 ? elLastHeight_ : uint32_t(elFrame_ ? elFrame_->height : 1080),
                out.doviP7);
            static bool p7Logged = false;
            if (out.doviP7.ready && !p7Logged) {
                p7Logged = true;
                log::info("source-dovi-el", std::format("reconstruction parameters packed nlqOffset={:.0f} slope={:.0f} coefDenom={:.0f}",
                    out.doviP7.params[0], out.doviP7.params[1], out.doviP7.params[4]));
            }
            // Custom (P7): MEL versus FEL. A MEL layer sets disable_residual_flag,
            // which means the layer carries no correction at all: decoding and
            // compositing it is pure cost. Require a consistent run before telling
            // read() to drop the path.
            if (elLayerEnabled_ && frame != nullptr) {
                if (av_dovi_get_header(&meta)->disable_residual_flag != 0) {
                    if (++elMelStrikes_ >= 24) elMelDrop_ = true;
                } else {
                    elMelStrikes_ = 0;
                }
            }
        }
    }
    if (std::getenv("VEYRA_DOVI_TRACE")) {
        static int doviProbeCount = 0;
        if (doviProbeCount < 3 || doviProbeCount % 200 == 0) {
            log::info("dovi-probe", std::format("n={} present={} profile={} valid={} maxPq={:.4f}",
                      doviProbeCount++, dv.present ? 1 : 0, static_cast<int>(dv.profile),
                      doviFrameInfo.valid ? 1 : 0, doviFrameInfo.maxPq));
        }
    }
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
		doviFrameInfo=extractDoviFrameInfo(*meta,info_.dolbyVision.profile);
		if(doviFrameInfo.valid&&std::getenv("VEYRA_DOVI_TRACE"))
			log::info("dovi-trace",std::format("profile={} minPq={:.4f} maxPq={:.4f} avgPq={:.4f}",
			           static_cast<int>(doviFrameInfo.profile),doviFrameInfo.minPq,doviFrameInfo.maxPq,doviFrameInfo.avgPq));
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
    if (pendingDiscontinuity_) {
        // First picture after a live reconnect: temporal history from the old connection
        // does not apply to it.
        flags |= static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
        pendingDiscontinuity_ = false;
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
    if (decoder_.usingD3D11Frames()&&frame->format==AV_PIX_FMT_D3D11) {
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
    noOutputSince_ = {};
    if (decodedFrame != nullptr) { *decodedFrame = frame; }
    return SourceReadStatus::Frame;
}

media::NetworkStats MediaFileSource::networkStats() const
{
    if (const auto& session = demuxer_.session()) return session->stats();
    return {};
}

void MediaFileSource::setPlaybackPositionMs(double ms)
{
    if (const auto& session = demuxer_.session()) {
        if (std::isfinite(ms) && ms >= 0) session->setPlaybackPositionUs(int64_t(ms * 1000.0));
    }
}

void MediaFileSource::resetCorruptionState()
{
    waitKeyframe_ = false;
    pendingDiscontinuity_ = false;
    corruptPacketsSeen_ = corruptFramesSeen_ = decoderErrorsSeen_ = 0;
    errorsInWindow_ = 0;
    errorWindowStart_ = lastCorruptLog_ = lastReadCall_ = noOutputSince_ = {};
    lastForcedReconnect_ = std::chrono::steady_clock::now();
}

// The first packet after a live reconnect: the old connection's decoder state and
// temporal history do not apply. The session has already cut to a keyframe and kept
// the timeline continuous.
void MediaFileSource::beginDiscontinuity()
{
    veyra::log::info("source-file", "network source reconnected: resetting the decoder at the new keyframe");
    deinterlacer_.close();
    filterDraining_ = false;
    decoder_.flushBuffers();   // the session starts the new connection at a keyframe
    waitKeyframe_ = false;
    pendingDiscontinuity_ = true;
    lastPtsUs_ = INT64_MIN;
    ++epoch_;
}

// A damaged packet the decoder rejects is a few lost frames, not a dead source: skip
// to the next keyframe. A burst of failures (a broken decoder or hardware path) is
// not tolerated, so the usual software fallback / error applies.
bool MediaFileSource::tolerateDecoderError()
{
    const auto now = std::chrono::steady_clock::now();
    if (errorsInWindow_ == 0 || now - errorWindowStart_ > std::chrono::seconds(2)) {
        errorWindowStart_ = now;
        errorsInWindow_ = 0;
    }
    ++decoderErrorsSeen_;
    if (++errorsInWindow_ > 12) {
        veyra::log::warn("source-file", std::format("decoder rejected {} packets within 2 s; treating it as a decoder failure", errorsInWindow_));
        return false;
    }
    if (decoder_.lastError() != AVERROR_INVALIDDATA && decoderErrorsSeen_ > 3 && decoder_.hardwareActive()) {
        return false;   // not damaged data: let the hardware fallback decide
    }
    startKeyframeWait();
    return true;
}

// Damaged data reached the decoder: drop what it holds and feed it again from the next clean keyframe.
void MediaFileSource::startKeyframeWait()
{
    deinterlacer_.close();
    filterDraining_ = false;
    decoder_.flushBuffers();
    waitKeyframe_ = true;
    waitKeyframeSince_ = std::chrono::steady_clock::now();
}

// Network loss: FFmpeg has concealed what it could. Keep that picture, or drop it and
// wait for the next keyframe, as the user chose. Only called for tolerant sources.
bool MediaFileSource::keepDamagedPicture(const AVFrame* frame)
{
    ++corruptFramesSeen_;
    const bool drop = corruptPolicy_ == media::CorruptPolicy::WaitKeyframe;
    if (const auto& session = demuxer_.session()) session->addCorruptFrames(1, drop ? 1 : 0);
    const auto now = std::chrono::steady_clock::now();
    if (now - lastCorruptLog_ >= std::chrono::seconds(1)) {
        lastCorruptLog_ = now;
        veyra::log::warn("source-file", std::format("damaged picture flags={} decodeErrors={} policy={} picturesSoFar={}",
            frame->flags, frame->decode_error_flags, drop ? "wait-keyframe" : "conceal", corruptFramesSeen_));
    }
    if (drop) startKeyframeWait();
    return !drop;
}

bool MediaFileSource::seek(const pipeline::Rational& targetSeconds)
{
    if (!info_.opened) { return false; }
    if((media::networkMediaInput(path_)||demuxer_.shared())&&!demuxer_.seekable())return false;
    if (targetSeconds.isUnknown() || targetSeconds.den == 0 || targetSeconds.isNegative()) {
        veyra::log::error("source-file", "seek target must be a known non-negative rational");
        return false;
    }
    const int64_t targetUs = rationalToUs(targetSeconds);
    if (!demuxer_.seekToUs(targetUs)) { return false; }
        // Custom (P5): the enhancement decoder keeps state across a seek too, and a
        // stale reference chain there produces garbage on the first new frame.
        if (elLayerOpen_) elDecoder_.flushBuffers();
        // Custom (P7): the splitter holds one packet in flight; a seek invalidates it.
        if (elSplitter_ != nullptr) av_bsf_flush(elSplitter_);
    // Custom (P7 fix): every frame decoded before the seek belongs to the old
    // position and is never delivered again, and each held clone keeps one
    // d3d11va surface checked out for the rest of the session.
    for (auto& held : baseWaiting_) av_frame_free(&held.second);
    baseWaiting_.clear();
    if (deliveredHeld_ != nullptr) av_frame_free(&deliveredHeld_);
    for (auto& entry : elByPts_) av_frame_free(&entry.second);
    elByPts_.clear();
    deinterlacer_.close();filterDraining_=false;
    decoder_.flushBuffers();
    // No profile-5 conversion state survives a seek: the next frame has to
    // convert from its own RPU, exactly like the same frame after a fresh open.
    info_.color.dolbyVisionP5 = {};
    errorMessage_.clear();
    draining_ = false;
    eofSignalled_ = false;
    pendingSeekFlag_ = true;
    pendingDiscontinuity_ = false;
    waitKeyframe_ = false;
    noOutputSince_ = {};
    lastPtsUs_ = INT64_MIN;
    ++epoch_;
    ++seekCount_;
    veyra::log::info("source-file", std::format("seek targetUs={} epoch={}", targetUs, epoch_));
    return true;
}

void MediaFileSource::close() noexcept
{
    errorMessage_.clear();
    deinterlacer_.close();filterDraining_=false;
    closeEnhancementLayer();       // Custom (P5): release the EL decoder and its frame
    closeEnhancementSplitter();    // Custom (P7): and the bitstream filter feeding it
    for (auto& entry : elByPts_) av_frame_free(&entry.second);
    elByPts_.clear();
    for (auto& held : baseWaiting_) av_frame_free(&held.second);
    baseWaiting_.clear();
    if (deliveredHeld_ != nullptr) av_frame_free(&deliveredHeld_);
    elLayerEnabled_ = false;
    elLayerFrames_ = 0;
    elLayerMisses_ = 0;
    decoder_.close();
    demuxer_.close();
    info_ = SourceInfo{};
    path_.clear();
    preferHardwareDecode_ = false;
    requireHardwareDecode_ = false;
    cancelled_ = nullptr;
}

} // namespace veyra::source
