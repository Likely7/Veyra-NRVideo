// Parser of the XCAP card's audio words: PCM, IEC 61937 in two channels, HBR in eight.
#include "veyra/source/XcapAudio.h"
#include <cstdio>
#include <vector>
using namespace veyra::source::xcapture;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static uint32_t word(unsigned ch, uint32_t s24) { return 0x80000000u | ch << 24 | (s24 & 0xFFFFFF); }
struct Seen { size_t pcmFrames = 0, carrierWords = 0, nonzeroPreamble = 0; unsigned pcmChannels = 0; std::vector<uint16_t> carrier; std::vector<std::pair<Codec, unsigned>> changes; std::vector<int32_t> pcm; };
static AudioParser make(Seen& s) {
    AudioParser::Output o;
    o.pcm = [&](const int32_t* v, unsigned c, size_t n) { s.pcmFrames += n; s.pcmChannels = c; s.pcm.insert(s.pcm.end(), v, v + n * c); };
    o.carrier = [&](Codec, unsigned, const uint16_t* w, size_t n) { s.carrierWords += n; s.carrier.insert(s.carrier.end(), w, w + n); };
    o.changed = [&](Codec c, unsigned ch) { s.changes.emplace_back(c, ch); };
    return AudioParser(std::move(o));
}
// bursts of `period` 16-bit words spread over `ch` channels, with preamble Pa Pb Pc Pd
static std::vector<uint32_t> bursts(unsigned ch, unsigned period, uint16_t pc, unsigned count, std::vector<uint16_t>& plain) {
    std::vector<uint32_t> out;
    uint16_t v = 0x1111;
    for (unsigned b = 0; b < count; ++b) for (unsigned i = 0; i < period; ++i) {
        const uint16_t w = i == 0 ? 0xF872 : i == 1 ? 0x4E1F : i == 2 ? pc : i == 3 ? 0x2000 : (i < period / 2 ? v++ : 0);
        plain.push_back(w);
        out.push_back(word(i % ch, uint32_t(w) << 8));
    }
    return out;
}
int main() {
    { Seen s; auto p = make(s); std::vector<uint32_t> w;
      for (int i = 0; i < 1000; ++i) { w.push_back(word(0, uint32_t(i * 1000))); w.push_back(word(1, uint32_t(-i * 1000))); }
      p.push(w.data(), w.size());
      CHECK(s.pcmChannels == 2 && s.pcmFrames >= 999 && s.carrierWords == 0);
      bool ok = true; for (size_t i = 0; i < s.pcmFrames; ++i) ok &= s.pcm[i * 2] == int(i) * 1000 && s.pcm[i * 2 + 1] == -int(i) * 1000; CHECK(ok); }
    { Seen s; auto p = make(s); std::vector<uint32_t> w;
      for (int i = 0; i < 400; ++i) for (unsigned c = 0; c < 8; ++c) w.push_back(word(c, uint32_t(i * 8 + int(c))));
      // arbitrary packet sizes must not matter
      for (size_t i = 0; i < w.size(); i += 37) p.push(w.data() + i, std::min<size_t>(37, w.size() - i));
      CHECK(s.pcmChannels == 8 && s.pcmFrames == 400); bool ok = true; for (size_t i = 0; i < s.pcm.size(); ++i) ok &= s.pcm[i] == int(i); CHECK(ok); }
    const struct { unsigned ch, period; uint16_t pc; Codec codec; } cases[] = {
        {2, 3072, 1, Codec::Ac3}, {2, 12288, 21, Codec::Eac3}, {8, 30720, 22, Codec::Mat}, {2, 1024, 11, Codec::Dts}, {8, 4096, 17, Codec::DtsHd}};
    for (const auto& c : cases) {
        Seen s; auto p = make(s); std::vector<uint16_t> plain; const auto w = bursts(c.ch, c.period, c.pc, 6, plain);
        for (size_t i = 0; i < w.size(); i += 508) p.push(w.data() + i, std::min<size_t>(508, w.size() - i));
        CHECK(p.codec() == c.codec && p.channels() == c.ch);
        // everything after the first recognised burst start is forwarded bit for bit
        CHECK(s.carrier.size() + 4 * c.ch >= plain.size() - c.period);
        // a stereo frame is complete only when the next one starts, so the very last one is pending
        const size_t pending = c.ch == 2 ? 2 : 0, skip = plain.size() - pending - s.carrier.size();
        bool exact = s.carrier.size() + pending <= plain.size();
        for (size_t i = 0; exact && i < s.carrier.size(); ++i) exact = s.carrier[i] == plain[skip + i];
        CHECK(exact);
        for (size_t i = 0; i < s.pcm.size(); ++i) if (uint32_t(s.pcm[i]) >> 8 == 0xF872) ++s.nonzeroPreamble;
        CHECK(s.nonzeroPreamble == 0);
    }
    { // compressed, then PCM again
      Seen s; auto p = make(s); std::vector<uint16_t> plain; auto w = bursts(2, 3072, 1, 4, plain);
      for (int i = 0; i < 40000; ++i) { w.push_back(word(0, 5000)); w.push_back(word(1, 6000)); }
      p.push(w.data(), w.size());
      CHECK(p.codec() == Codec::Pcm && s.pcmFrames > 10000); }
    for (const double ppm : {800.0, -800.0}) for (const unsigned ch : {2u, 8u}) {   // clock drift: only whole bursts come and go
        const unsigned period = ch == 2 ? 12288 : 30720, rate = 192000, chunk = 160 * ch;       // E-AC-3 / MAT
        CarrierQueue q;
        q.reset(ch, rate, 960 * ch);
        q.setTarget(size_t(rate) * ch * 40 / 1000);
        uint64_t sent = 0; double due = 0; std::vector<uint16_t> in(chunk), out(960 * ch), played;
        for (int step = 0; step < 120000; ++step) {                                             // 100 s
            for (unsigned i = 0; i < chunk; ++i, ++sent) { const unsigned k = unsigned(sent % period); const uint64_t n = sent / period;
                in[i] = k == 0 ? 0xF872 : k == 1 ? 0x4E1F : k == 2 ? 21 : k == 3 ? uint16_t(n) : uint16_t(k ^ (n * 7)); }
            q.push(in.data(), chunk);
            due += chunk * (1 - ppm * 1e-6);
            while (due >= double(out.size())) { due -= double(out.size()); q.pull(out.data(), out.size()); played.insert(played.end(), out.begin(), out.end()); }
        }
        // every played burst is complete and the order is kept
        size_t at = 0; while (at + 1 < played.size() && !(played[at] == 0xF872 && played[at + 1] == 0x4E1F)) ++at;
        bool whole = true; int64_t last = -1; unsigned skips = 0, twice = 0, count = 0;
        for (; at + period <= played.size(); at += period, ++count) {
            whole &= played[at] == 0xF872 && played[at + 1] == 0x4E1F;
            const uint16_t n = played[at + 3];
            for (unsigned k = 4; k < period && whole; ++k) whole = played[at + k] == uint16_t(k ^ (n * 7));
            if (last >= 0) { const int d = int(uint16_t(n - uint16_t(last))); if (d == 0) ++twice; else if (d == 2) ++skips; else if (d != 1) whole = false; }
            last = n;
        }
        printf("drift %+.0f ppm, %u channels: %u bursts played, %u left out, %u repeated, underruns %llu, trims %llu\n", ppm, ch, count, skips, twice, (unsigned long long)q.underruns, (unsigned long long)q.trims);
        CHECK(whole && !q.underruns && !q.trims && (ppm > 0 ? skips > 0 && !twice : twice > 0 && !skips));
    }
    const auto sup = passthroughSupport();
    printf("endpoint passthrough: AC-3 %d  E-AC-3 %d  MAT %d  DTS %d  DTS-HD %d\n", sup.ac3, sup.eac3, sup.mat, sup.dts, sup.dtsHd);
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures;
}
