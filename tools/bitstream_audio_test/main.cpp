// Dolby/DTS bitstream decode test (diagnostic; never shipped in the player).
//
// There is no bitstream-capable capture card on this machine, so the device path
// cannot be exercised. What CAN be proven locally is the decode half:
//   1. encode a 5.1 test signal with the same FFmpeg the player links,
//   2. feed the resulting AC-3 bitstream through veyra::sink::BitstreamDecoder
//      in chunks (exercising the incremental parser path),
//   3. check the decoded channel count, frame count and per-channel amplitude
//      ordering, so a wrong channel map or a silent decode fails loudly,
//   4. repeat with IEC 61937 burst framing (the S/PDIF carrier) to prove the
//      unwrapper as well.
//
// Usage: veyra_bitstream_audio_test
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

#include "veyra/sink/BitstreamAudio.h"

using veyra::sink::BitstreamDecoder;
using veyra::sink::BitstreamKind;

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 6;
constexpr int kSeconds = 2;
// Distinct per-channel amplitudes so a channel swap or a silent channel is
// visible in the decoded RMS. LFE gets a low tone because the encoder low-passes
// that channel.
constexpr double kAmplitudes[kChannels] = {0.50, 0.40, 0.30, 0.25, 0.20, 0.10};
constexpr double kTones[kChannels] = {440.0, 440.0, 440.0, 60.0, 440.0, 440.0};

// Encodes the test signal with libavcodec; returns the raw bitstream bytes.
bool encodeTestStream(BitstreamKind kind, std::vector<uint8_t>& out, std::string& why,
                      std::vector<size_t>* packetSizes = nullptr) {
    const AVCodecID codecId = kind == BitstreamKind::Ac3 ? AV_CODEC_ID_AC3 : AV_CODEC_ID_EAC3;
    const AVCodec* codec = avcodec_find_encoder(codecId);
    if (codec == nullptr) {
        why = "this FFmpeg build has no AC-3/E-AC-3 encoder (decoders only)";
        return false;
    }
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (context == nullptr) {
        why = "encoder context allocation failed";
        return false;
    }
    context->sample_rate = kSampleRate;
    context->bit_rate = kind == BitstreamKind::Ac3 ? 448000 : 640000;
    av_channel_layout_default(&context->ch_layout, kChannels);
    // FFmpeg n9 no longer exposes AVCodec::sample_fmts, so probe the formats the
    // AC-3/E-AC-3 encoders accept instead of advertising a preferred one.
    bool opened = false;
    for (AVSampleFormat format : {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S16P}) {
        context->sample_fmt = format;
        if (avcodec_open2(context, codec, nullptr) >= 0) { opened = true; break; }
    }
    if (!opened) {
        why = "avcodec_open2(encoder) failed for fltp/s32p/s16p";
        avcodec_free_context(&context);
        return false;
    }
    const int frameSize = context->frame_size > 0 ? context->frame_size : 1536;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    if (frame == nullptr || packet == nullptr) {
        why = "frame/packet allocation failed";
        avcodec_free_context(&context);
        av_frame_free(&frame);
        av_packet_free(&packet);
        return false;
    }
    frame->format = context->sample_fmt;
    frame->sample_rate = context->sample_rate;
    frame->nb_samples = frameSize;
    if (av_channel_layout_copy(&frame->ch_layout, &context->ch_layout) < 0 ||
        av_frame_get_buffer(frame, 0) < 0) {
        why = "frame buffer allocation failed";
        avcodec_free_context(&context);
        av_frame_free(&frame);
        av_packet_free(&packet);
        return false;
    }
    const int totalFrames = kSampleRate * kSeconds;
    int produced = 0;
    bool ok = true;
    while (produced < totalFrames && ok) {
        if (av_frame_make_writable(frame) < 0) {
            why = "av_frame_make_writable failed";
            ok = false;
            break;
        }
        const bool planar = av_sample_fmt_is_planar(context->sample_fmt) == 1;
        for (int ch = 0; ch < kChannels && ok; ++ch) {
            auto* plane = reinterpret_cast<float*>(frame->data[planar ? ch : 0]);
            for (int i = 0; i < frameSize; ++i) {
                const double t = double(produced + i) / kSampleRate;
                const float value = float(kAmplitudes[ch] * std::sin(2.0 * 3.14159265358979323846 * kTones[ch] * t));
                if (planar) plane[i] = value;
                else plane[size_t(i) * kChannels + ch] = value;
            }
        }
        produced += frameSize;
        if (avcodec_send_frame(context, frame) < 0) {
            why = "avcodec_send_frame failed";
            ok = false;
            break;
        }
        for (;;) {
            const int received = avcodec_receive_packet(context, packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) break;
            if (received < 0) {
                why = "avcodec_receive_packet failed";
                ok = false;
                break;
            }
            if (packetSizes != nullptr) packetSizes->push_back(size_t(packet->size));
            out.insert(out.end(), packet->data, packet->data + packet->size);
            av_packet_unref(packet);
        }
    }
    if (ok) {
        avcodec_send_frame(context, nullptr);
        for (;;) {
            const int received = avcodec_receive_packet(context, packet);
            if (received < 0) break;
            if (packetSizes != nullptr) packetSizes->push_back(size_t(packet->size));
            out.insert(out.end(), packet->data, packet->data + packet->size);
            av_packet_unref(packet);
        }
    }
    avcodec_free_context(&context);
    av_frame_free(&frame);
    av_packet_free(&packet);
    if (!ok || out.empty()) {
        if (why.empty()) why = "encoder produced no data";
        return false;
    }
    return true;
}

// Wraps raw AC-3 bytes into IEC 61937 bursts (8-byte header + padded payload).
std::vector<uint8_t> toIec61937(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    // 6144 bytes is the standard S/PDIF burst payload size for AC-3.
    const size_t burst = 6144;
    for (size_t offset = 0; offset < raw.size(); offset += burst) {
        const size_t chunk = (std::min)(burst, raw.size() - offset);
        const size_t padded = (chunk + 1) & ~size_t{1};
        const size_t headerSize = out.size();
        out.resize(headerSize + 8, 0);
        out[headerSize + 0] = 0xF8;
        out[headerSize + 1] = 0x72;
        out[headerSize + 2] = 0x4E;
        out[headerSize + 3] = 0x1F;
        out[headerSize + 4] = 0x00;  // data type: AC-3
        out[headerSize + 5] = 0x00;
        const uint16_t bits = uint16_t((chunk * 8) & 0xFFFF);
        out[headerSize + 6] = uint8_t(bits & 0xFF);
        out[headerSize + 7] = uint8_t(bits >> 8);
        out.insert(out.end(), raw.begin() + offset, raw.begin() + offset + chunk);
        out.resize(out.size() + (padded - chunk), 0);
    }
    return out;
}

struct DecodeResult {
    bool ok = false;
    unsigned channels = 0;
    uint64_t frames = 0;
    std::vector<double> rms;
    std::string error;
};

DecodeResult decodeStream(const std::vector<uint8_t>& bytes, BitstreamKind kind, bool iec61937, int chunks) {
    DecodeResult result{};
    BitstreamDecoder decoder;
    if (!decoder.open(kind, iec61937)) {
        result.error = decoder.lastError();
        return result;
    }
    std::vector<float> pcm;
    const size_t perChunk = bytes.size() / size_t(chunks > 0 ? chunks : 1);
    for (size_t offset = 0; offset < bytes.size();) {
        const size_t take = (std::min)(perChunk == 0 ? bytes.size() : perChunk, bytes.size() - offset);
        if (!decoder.push(bytes.data() + offset, take, pcm)) {
            result.error = decoder.lastError();
            return result;
        }
        offset += take;
    }
    result.channels = decoder.channels();
    result.frames = decoder.decodedFrames();
    if (result.channels == 0 || result.frames == 0) {
        result.error = "decoder produced no audio";
        return result;
    }
    result.rms.assign(result.channels, 0.0);
    const uint64_t frames = result.frames;
    for (uint64_t frame = 0; frame < frames; ++frame) {
        for (unsigned ch = 0; ch < result.channels; ++ch) {
            const float sample = pcm[size_t(frame) * result.channels + ch];
            result.rms[ch] += double(sample) * double(sample);
        }
    }
    for (auto& value : result.rms) value = std::sqrt(value / double(frames));
    result.ok = true;
    return result;
}

void report(const char* label, const DecodeResult& result) {
    std::printf("[bitstream-test] %s channels=%u frames=%llu", label, result.channels,
                static_cast<unsigned long long>(result.frames));
    for (size_t i = 0; i < result.rms.size(); ++i) std::printf(" ch%zu=%.4f", i, result.rms[i]);
    std::printf("%s%s\n", result.error.empty() ? "" : " error=", result.error.c_str());
}

// The five non-LFE channels must keep the encoded amplitude ordering; the LFE
// (channel 3) is low-passed by the encoder, so it is only checked for presence.
bool amplitudeOrderingHolds(const DecodeResult& result) {
    if (result.rms.size() != kChannels) return false;
    const int order[kChannels - 1] = {0, 1, 2, 4};  // compare 0>1>2>4>5
    for (int i = 0; i < 4; ++i) {
        const double higher = result.rms[order[i]];
        const double lower = i == 3 ? result.rms[5] : result.rms[order[i + 1]];
        if (!(higher > lower * 1.05)) return false;
    }
    return result.rms[3] > 0.005;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[bitstream-test] ffmpeg avcodec=%u swresample linked, decoders: ac3=%d eac3=%d dts=%d truehd=%d\n",
                avcodec_version(),
                avcodec_find_decoder(AV_CODEC_ID_AC3) ? 1 : 0,
                avcodec_find_decoder(AV_CODEC_ID_EAC3) ? 1 : 0,
                avcodec_find_decoder(AV_CODEC_ID_DTS) ? 1 : 0,
                avcodec_find_decoder(AV_CODEC_ID_TRUEHD) ? 1 : 0);

    bool ok = true;
    for (BitstreamKind kind : {BitstreamKind::Ac3, BitstreamKind::Eac3}) {
        std::vector<uint8_t> bitstream;
        std::vector<size_t> packetSizes;
        std::string why;
        if (!encodeTestStream(kind, bitstream, why, &packetSizes)) {
            std::printf("[bitstream-test] %s encode skipped: %s\n", veyra::sink::bitstreamKindName(kind), why.c_str());
            ok = false;
            continue;
        }
        std::printf("[bitstream-test] %s encoded bytes=%llu\n", veyra::sink::bitstreamKindName(kind),
                    static_cast<unsigned long long>(bitstream.size()));
        const auto raw = decodeStream(bitstream, kind, false, 3);
        report(veyra::sink::bitstreamKindName(kind), raw);
        if (!raw.ok || raw.channels != kChannels || !amplitudeOrderingHolds(raw)) {
            std::printf("[bitstream-test] FAIL %s: decode/channel/amplitude check\n", veyra::sink::bitstreamKindName(kind));
            ok = false;
        }
        if (kind == BitstreamKind::Ac3) {
            // The parser holds a finished AC-3 frame until the next header.
            // pushFramed must emit that frame on the same call.
            if (packetSizes.size() >= 2 && packetSizes[0] > 0 && packetSizes[1] > 0) {
                BitstreamDecoder direct;
                BitstreamDecoder parsed;
                std::vector<float> directPcm;
                std::vector<float> parsedPcm;
                const bool opened = direct.open(kind) && parsed.open(kind);
                const bool first = opened
                    && direct.pushFramed(bitstream.data(), packetSizes[0], directPcm)
                    && parsed.push(bitstream.data(), packetSizes[0], parsedPcm);
                const uint64_t parserAfterFirst = parsed.decodedFrames();
                const bool directImmediate = first && direct.decodedFrames() >= 1536 && parserAfterFirst == 0;
                const bool parserHeld = directImmediate
                    && parsed.push(bitstream.data() + packetSizes[0], packetSizes[1], parsedPcm)
                    && parsed.decodedFrames() >= 1536;
                std::printf("[bitstream-test] framed-latency directFrames=%llu parserAfterFirst=%llu parserAfterSecond=%llu\n",
                            static_cast<unsigned long long>(direct.decodedFrames()),
                            static_cast<unsigned long long>(parserAfterFirst),
                            static_cast<unsigned long long>(parsed.decodedFrames()));
                if (!parserHeld) {
                    std::printf("[bitstream-test] FAIL AC-3 framed path did not beat the parser hold\n");
                    ok = false;
                }
            }
            const auto framed = decodeStream(toIec61937(bitstream), kind, true, 2);
            report("AC-3 (IEC 61937)", framed);
            if (!framed.ok || framed.channels != kChannels || !amplitudeOrderingHolds(framed)) {
                std::printf("[bitstream-test] FAIL AC-3 IEC 61937 unwrap/decode\n");
                ok = false;
            }
        }
    }
    std::printf("[bitstream-test] VERDICT %s\n", ok ? "bitstream decode + channel mapping verified locally"
                                                    : "bitstream decode verification incomplete");
    return ok ? 0 : 1;
}
