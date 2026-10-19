#include "veyra/sink/BitstreamAudio.h"

#include "veyra/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <climits>
#include <cstring>
#include <format>

namespace veyra::sink {
namespace {

AVCodecID codecIdFor(BitstreamKind kind) {
    switch (kind) {
    case BitstreamKind::Ac3: return AV_CODEC_ID_AC3;
    case BitstreamKind::Eac3: return AV_CODEC_ID_EAC3;
    case BitstreamKind::TrueHd: return AV_CODEC_ID_TRUEHD;
    case BitstreamKind::Dts: return AV_CODEC_ID_DTS;
    case BitstreamKind::DtsHd: return AV_CODEC_ID_DTS;
    case BitstreamKind::None: break;
    }
    return AV_CODEC_ID_NONE;
}

// IEC 61937 burst: 4-byte sync (0xF8724E1F) + 2-byte data type + 2-byte length
// in bits, then the padded payload.
constexpr uint8_t kIecSync[4] = {0xF8, 0x72, 0x4E, 0x1F};
constexpr size_t kIecHeader = 8;

} // namespace

BitstreamKind classifyBitstreamSubtype(uint32_t subtypeData1) {
    switch (subtypeData1) {
    case 0x00000092u: return BitstreamKind::Ac3;      // WAVE_FORMAT_DOLBY_AC3_SPDIF
    case 0x00002000u: return BitstreamKind::Ac3;      // AC-3 in a WAVEFORMATEXTENSIBLE subtype
    case 0x0000000Au: return BitstreamKind::Eac3;     // E-AC-3 / Dolby Digital Plus
    case 0x0000010Au: return BitstreamKind::Eac3;     // DD+ carrying Atmos
    case 0x0000000Cu: return BitstreamKind::TrueHd;   // TrueHD / MLP (Atmos over TrueHD)
    case 0x00000008u: return BitstreamKind::Dts;      // DTS
    case 0x0000000Bu: return BitstreamKind::DtsHd;    // DTS-HD
    case 0x0000010Bu: return BitstreamKind::DtsHd;    // DTS:X (extension 1)
    case 0x0000030Bu: return BitstreamKind::DtsHd;    // DTS:X (extension 2)
    default: return BitstreamKind::None;
    }
}

const char* bitstreamKindName(BitstreamKind kind) {
    switch (kind) {
    case BitstreamKind::Ac3: return "AC-3";
    case BitstreamKind::Eac3: return "E-AC-3/DD+";
    case BitstreamKind::TrueHd: return "TrueHD/MLP";
    case BitstreamKind::Dts: return "DTS";
    case BitstreamKind::DtsHd: return "DTS-HD";
    case BitstreamKind::None: break;
    }
    return "none";
}

int bitstreamPreferenceOrder(BitstreamKind kind) {
    switch (kind) {
    case BitstreamKind::TrueHd: return 60;
    case BitstreamKind::Eac3: return 50;
    case BitstreamKind::DtsHd: return 40;
    case BitstreamKind::Dts: return 30;
    case BitstreamKind::Ac3: return 20;
    case BitstreamKind::None: break;
    }
    return 0;
}

bool bitstreamIsIec61937(uint32_t subtypeData1) {
    return subtypeData1 == 0x00000092u;  // S/PDIF framed AC-3 carrier
}

BitstreamKind classifyIec61937DataType(uint16_t dataType) {
    switch (dataType) {
    case 0x01: return BitstreamKind::Ac3;
    case 0x15: return BitstreamKind::Eac3;
    case 0x0B:
    case 0x0C:
    case 0x0D: return BitstreamKind::Dts;
    case 0x11: return BitstreamKind::DtsHd;
    case 0x16: return BitstreamKind::TrueHd;
    default: return BitstreamKind::None;
    }
}

void Iec61937Probe::feed(const uint8_t* data, size_t bytes) {
    if (verdict_ != 0 || data == nullptr || bytes == 0) return;
    if (scanned_ >= kMaxScanBytes) {
        verdict_ = -1;
        log::info("capture-audio-carrier", std::format(
            "no IEC 61937 burst in the first {} bytes of the carrier; treating it as linear PCM", scanned_));
        return;
    }
    tail_.insert(tail_.end(), data, data + bytes);
    size_t cursor = 0;
    const size_t size = tail_.size();
    while (cursor + kIecHeader <= size) {
        if (scanned_ + cursor >= kMaxScanBytes) break;
        if (std::memcmp(tail_.data() + cursor, kIecSync, 4) != 0) {
            ++cursor;
            continue;
        }
        const uint16_t dataType = uint16_t(tail_[cursor + 4]) | (uint16_t(tail_[cursor + 5]) << 8);
        const size_t payloadBits = size_t(tail_[cursor + 6]) | (size_t(tail_[cursor + 7]) << 8);
        const bool plausible = payloadBits >= 8 && classifyIec61937DataType(dataType) != BitstreamKind::None;
        if (plausible) {
            if (bursts_ == 0) type_ = dataType;
            if (dataType == type_) {
                ++bursts_;
                if (bursts_ >= 2) {
                    verdict_ = 1;
                    scanned_ += cursor + kIecHeader;
                    log::info("capture-audio-carrier", std::format(
                        "IEC 61937 detected on the PCM-labelled carrier: dataType=0x{:02X} ({}) bursts={} scannedBytes={}",
                        type_, bitstreamKindName(classifyIec61937DataType(type_)), bursts_, scanned_));
                    tail_.clear();
                    return;
                }
            }
        }
        // A false sync inside random PCM must not stall the scan.
        cursor += 4;
    }
    scanned_ += cursor;
    // Keep only what a burst header could still straddle.
    if (cursor < size) tail_.erase(tail_.begin(), tail_.begin() + cursor);
    else tail_.clear();
    // Reaching the budget inside this call must be as final as reaching it on
    // the next one, otherwise a single large delivery would leave the verdict
    // pending forever.
    if (verdict_ == 0 && scanned_ >= kMaxScanBytes) {
        verdict_ = -1;
        log::info("capture-audio-carrier", std::format(
            "no IEC 61937 burst in the first {} bytes of the carrier; treating it as linear PCM", scanned_));
    }
}

bool Iec61937Probe::concluded() const { return verdict_ != 0; }
bool Iec61937Probe::detected() const { return verdict_ == 1; }
uint16_t Iec61937Probe::dataType() const { return verdict_ == 1 ? type_ : 0; }
size_t Iec61937Probe::scannedBytes() const { return scanned_; }
unsigned Iec61937Probe::burstCount() const { return bursts_; }

struct BitstreamDecoder::Impl {
    AVCodecContext* context = nullptr;
    AVCodecParserContext* parser = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVChannelLayout outLayout{};
    bool iec61937 = false;
    bool outLayoutValid = false;
    std::vector<uint8_t> iecPending;
    std::vector<uint8_t> parserScratch;
    std::vector<uint8_t> inputScratch;
    uint64_t formatRevision=0;
    int outputRate=0,outputSampleFormat=-1;
    std::string lastError;
    uint64_t frames = 0;
    uint64_t bytesIn = 0;

    ~Impl() { close(); }

    void close() {
        if (resampler != nullptr) swr_free(&resampler);
        if (frame != nullptr) av_frame_free(&frame);
        if (packet != nullptr) av_packet_free(&packet);
        if (parser != nullptr) av_parser_close(parser);
        if (context != nullptr) avcodec_free_context(&context);
        av_channel_layout_uninit(&outLayout);
        outLayoutValid = false;
    }

    // Appends the payload of every complete IEC 61937 burst to `out` and keeps
    // an incomplete tail for the next push.
    void unwrapIec61937(const uint8_t* data, size_t bytes, std::vector<uint8_t>& out) {
        size_t i = 0;
        while (i + kIecHeader <= bytes) {
            if (std::memcmp(data + i, kIecSync, 4) != 0) {
                // Not a burst header: treat the rest as raw codec data.
                out.insert(out.end(), data + i, data + bytes);
                return;
            }
            const size_t bits = size_t(data[i + 6]) | (size_t(data[i + 7]) << 8);
            const size_t payload = bits / 8;
            if (payload == 0) {
                i += kIecHeader;
                continue;
            }
            if (i + kIecHeader + payload > bytes) break;  // incomplete burst
            out.insert(out.end(), data + i + kIecHeader, data + i + kIecHeader + payload);
            i += kIecHeader + payload;
        }
        if (i < bytes) iecPending.assign(data + i, data + bytes);
    }

    bool ensureResampler() {
        if (resampler != nullptr) return true;
        AVChannelLayout in{};
        if (av_channel_layout_copy(&in, &outLayout) < 0) {
            lastError = "channel layout copy failed";
            return false;
        }
        AVChannelLayout target{};
        if (av_channel_layout_copy(&target, &in) < 0) {
            av_channel_layout_uninit(&in);
            lastError = "channel layout copy failed";
            return false;
        }
        const int result = swr_alloc_set_opts2(&resampler, &target, AV_SAMPLE_FMT_FLT, context->sample_rate,
                                               &in, context->sample_fmt, context->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&in);
        av_channel_layout_uninit(&target);
        if (result < 0 || resampler == nullptr || swr_init(resampler) < 0) {
            lastError = "swr_alloc_set_opts2/swr_init failed";
            if (resampler != nullptr) swr_free(&resampler);
            return false;
        }
        return true;
    }

    bool handleFrame(std::vector<float>& out) {
        if(outLayoutValid&&(av_channel_layout_compare(&outLayout,&frame->ch_layout)!=0
            ||outputRate!=frame->sample_rate||outputSampleFormat!=frame->format)){
            // A console can switch channel layout without changing codec.
            // Do not concatenate samples from two formats into one callback.
            out.clear();swr_free(&resampler);av_channel_layout_uninit(&outLayout);outLayoutValid=false;
        }
        if (!outLayoutValid) {
            if (av_channel_layout_copy(&outLayout, &frame->ch_layout) < 0) {
                lastError = "decoded frame has no usable channel layout";
                return false;
            }
            if (outLayout.order == AV_CHANNEL_ORDER_UNSPEC) {
                av_channel_layout_default(&outLayout, outLayout.nb_channels > 0 ? outLayout.nb_channels : 2);
            }
            outLayoutValid = true;
            outputRate=frame->sample_rate;outputSampleFormat=frame->format;++formatRevision;
        }
        if (!ensureResampler()) return false;
        const unsigned channels = unsigned(outLayout.nb_channels);
        const size_t base = out.size();
        const int capacity = int(av_rescale_rnd(
            swr_get_delay(resampler, context->sample_rate) + frame->nb_samples,
            context->sample_rate, context->sample_rate, AV_ROUND_UP));
        out.resize(base + size_t(capacity) * channels);
        uint8_t* dstPlanes[1] = {reinterpret_cast<uint8_t*>(out.data() + base)};
        const int converted = swr_convert(resampler, dstPlanes, capacity,
                                          const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (converted < 0) {
            lastError = "swr_convert failed";
            return false;
        }
        out.resize(base + size_t(converted) * channels);
        frames += uint64_t(converted);
        return true;
    }

    bool decodePacket(std::vector<float>& out) {
        const int sent = avcodec_send_packet(context, packet);
        if (sent < 0 && sent != AVERROR(EAGAIN)) {
            lastError = "avcodec_send_packet failed";
            return false;
        }
        for (;;) {
            const int received = avcodec_receive_frame(context, frame);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) break;
            if (received < 0) {
                lastError = "avcodec_receive_frame failed";
                return false;
            }
            if (!handleFrame(out)) return false;
        }
        av_frame_unref(frame);
        return true;
    }
};

BitstreamDecoder::BitstreamDecoder() : p_(std::make_unique<Impl>()) {}
BitstreamDecoder::~BitstreamDecoder() = default;

bool BitstreamDecoder::open(BitstreamKind kind, bool iec61937) {
    auto& p = *p_;
    p.close();
    p.iec61937 = iec61937;
    p.lastError.clear();
    const AVCodecID codecId = codecIdFor(kind);
    if (codecId == AV_CODEC_ID_NONE) {
        p.lastError = "no decoder for this bitstream family";
        return false;
    }
    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (codec == nullptr) {
        p.lastError = "this FFmpeg build has no decoder for the bitstream family";
        return false;
    }
    p.context = avcodec_alloc_context3(codec);
    if (p.context == nullptr || avcodec_open2(p.context, codec, nullptr) < 0) {
        p.lastError = "avcodec_open2 failed";
        p.close();
        return false;
    }
    p.parser = av_parser_init(codecId);
    p.packet = av_packet_alloc();
    p.frame = av_frame_alloc();
    if (p.packet == nullptr || p.frame == nullptr) {
        p.lastError = "packet/frame allocation failed";
        p.close();
        return false;
    }
    return true;
}

bool BitstreamDecoder::push(const uint8_t* data, size_t bytes, std::vector<float>& out) {
    auto& p = *p_;
    if (p.context == nullptr) return false;
    if (data == nullptr || bytes == 0) return true;
    p.bytesIn += bytes;

    const uint8_t* stream = data;
    size_t streamBytes = bytes;
    if (p.iec61937) {
        std::vector<uint8_t> combined;
        if (!p.iecPending.empty()) {
            combined.assign(p.iecPending.begin(), p.iecPending.end());
            p.iecPending.clear();
        }
        combined.insert(combined.end(), data, data + bytes);
        p.parserScratch.clear();
        p.unwrapIec61937(combined.data(), combined.size(), p.parserScratch);
        stream = p.parserScratch.data();
        streamBytes = p.parserScratch.size();
        if (streamBytes == 0) return true;
    }

    if (p.parser == nullptr) {
        // No parser for this codec (TrueHD): hand the buffer over as one packet.
        av_packet_unref(p.packet);
        if (av_new_packet(p.packet, int(streamBytes)) < 0) {
            p.lastError = "packet allocation failed";
            return false;
        }
        std::memcpy(p.packet->data, stream, streamBytes);
        return p.decodePacket(out);
    }

    // FFmpeg parsers require zero padding even when the caller supplies a
    // complete IEC payload. A vector's spare capacity is not readable padding.
    p.inputScratch.assign(stream,stream+streamBytes);
    p.inputScratch.resize(streamBytes+AV_INPUT_BUFFER_PADDING_SIZE,0);
    stream=p.inputScratch.data();
    size_t offset = 0;
    while (offset < streamBytes) {
        uint8_t* packetData = nullptr;
        int packetSize = 0;
        const int consumed = av_parser_parse2(p.parser, p.context, &packetData, &packetSize, stream + offset,
                                             int(streamBytes - offset), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (consumed < 0) {
            p.lastError = "av_parser_parse2 failed";
            return false;
        }
        offset += size_t(consumed);
        if(consumed==0&&packetSize<=0){p.lastError="codec parser made no progress";return false;}
        if (packetSize <= 0) continue;
        av_packet_unref(p.packet);
        if (av_new_packet(p.packet, packetSize) < 0) {
            p.lastError = "packet allocation failed";
            return false;
        }
        std::memcpy(p.packet->data, packetData, size_t(packetSize));
        if (!p.decodePacket(out)) return false;
    }
    return true;
}

bool BitstreamDecoder::pushFramed(const uint8_t* data, size_t bytes, std::vector<float>& out) {
    auto& p = *p_;
    if (p.context == nullptr) {p.lastError="decoder not opened";return false;}
    if (bytes == 0) return true;
    if (data == nullptr) {p.lastError="null framed packet";return false;}
    if (bytes > size_t(INT_MAX)) {
        p.lastError = "framed packet is too large";
        return false;
    }
    p.bytesIn += bytes;
    av_packet_unref(p.packet);
    if (av_new_packet(p.packet, int(bytes)) < 0) {
        p.lastError = "packet allocation failed";
        return false;
    }
    std::memcpy(p.packet->data, data, bytes);
    return p.decodePacket(out);
}

void BitstreamDecoder::reset() {
    auto& p = *p_;
    p.iecPending.clear();
    p.parserScratch.clear();
    swr_free(&p.resampler);av_channel_layout_uninit(&p.outLayout);p.outLayoutValid=false;
    if (p.context != nullptr) avcodec_flush_buffers(p.context);
    if (p.parser != nullptr) av_parser_close(p.parser);
    if (p.context != nullptr) p.parser = av_parser_init(p.context->codec_id);
}

bool BitstreamDecoder::opened() const { return p_->context != nullptr; }
unsigned BitstreamDecoder::channels() const {
    return p_->outLayoutValid ? unsigned(p_->outLayout.nb_channels) : 0;
}
unsigned BitstreamDecoder::sampleRate() const {
    return p_->context != nullptr ? unsigned(p_->context->sample_rate) : 0;
}
uint64_t BitstreamDecoder::decodedFrames() const { return p_->frames; }
uint32_t BitstreamDecoder::channelMask() const {
    return p_->outLayoutValid&&p_->outLayout.order==AV_CHANNEL_ORDER_NATIVE?uint32_t(p_->outLayout.u.mask):0;
}
uint64_t BitstreamDecoder::formatRevision() const { return p_->formatRevision; }
uint64_t BitstreamDecoder::decodedBytesIn() const { return p_->bytesIn; }
const std::string& BitstreamDecoder::lastError() const { return p_->lastError; }

} // namespace veyra::sink
