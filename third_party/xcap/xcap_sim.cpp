#include "xcap_sim.h"
#include "xcap.h"
#include <timeapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <fstream>
#include <thread>
#pragma comment(lib, "winmm.lib")

namespace xcap {
namespace {

int64_t qpc() { LARGE_INTEGER v; QueryPerformanceCounter(&v); return v.QuadPart; }
int64_t qpcFreq() { static const int64_t f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return v.QuadPart; }(); return f; }
constexpr double kPpm = 46e-6;                                   // the real card runs this much fast
uint64_t ticksAt(int64_t q) { return uint64_t(double(q) * (250e6 / double(qpcFreq())) * (1 + kPpm)); }
int64_t qpcAt(double ticks) { return int64_t(ticks / ((250e6 / double(qpcFreq())) * (1 + kPpm))); }

struct Sim {
    std::mutex lock;
    std::wstring file;
    std::map<std::string, std::string> keys;
    uint32_t regs[0x1000 / 4]{};
    std::unique_ptr<xcap_ctl> ctl;
    std::vector<uint8_t> ring, audio;
    xcap_start_in in{};
    xcap_start_out out{};
    std::atomic<bool> quit{false};
    std::thread video, sound;
    std::vector<uint8_t> picture;                                // two frames wide
    std::vector<uint8_t> clip; uint32_t clipFrames = 0, clipMeta[16]{}; std::string clipName;   // recorded frames (xcap_test record)
    uint32_t beat = 0;
    int64_t readAt = 0;

    double num(const char* k, double d) { auto i = keys.find(k); return i == keys.end() ? d : atof(i->second.c_str()); }
    void refresh() {                                             // lock held
        const int64_t now = qpc();
        if (readAt && now - readAt < qpcFreq() / 10) return;
        readAt = now;
        std::ifstream f(file);
        std::string w;
        std::map<std::string, std::string> k;
        while (f >> w) if (auto p = w.find('='); p != std::string::npos) k[w.substr(0, p)] = w.substr(p + 1);
        if (!k.empty() || keys.empty()) keys = std::move(k);
        const bool locked = num("lock", 1) != 0, hdr = num("hdr", 1) != 0;
        uint32_t* m = &regs[XCAP_REG_HDMI0 / 4];
        m[XCAP_META_FLAGS] = hdr ? 0x23u : 0u;                   // HDR infoframe, BT.2020, PQ
        m[XCAP_META_LIGHT] = hdr ? 1000u | 400u << 16 : 0;
        m[XCAP_META_MASTER_LUM] = hdr ? 1000u | 50u << 16 : 0;
        m[XCAP_META_RATE] = uint32_t(num("rate", 59940));
        m[XCAP_META_GEOM] = locked ? 3840u | 2160u << 16 : 0;
        m[XCAP_META_FW] = 0x30000u;
        m[XCAP_META_BEAT] = ++beat;
        m[XCAP_META_AUDIO] = 480u | uint32_t(num("audio", 2)) << 16 | 1u << 24;
        const uint32_t host = regs[XCAP_REG_HOST_CTRL / 4];
        if (clipFrames) for (int i : { XCAP_META_FLAGS, XCAP_META_PRIM_GB, XCAP_META_PRIM_GB + 1, XCAP_META_PRIM_GB + 2, XCAP_META_PRIM_GB + 3, XCAP_META_MASTER_LUM, XCAP_META_LIGHT })
            m[i] = hdr ? clipMeta[i] : 0;
        m[XCAP_META_STATE] = (locked ? 1u : 0) | 1u << 1 | (host & 0x10u) | (host & 0x20u ? 0x20u | (host >> 8 & 255u) << 24 : 0) | 0xC0u | 12u << 8 | 97u << 16;
    }

    void buildPicture() {                                        // v210: 16 bytes hold 6 pixels
        const size_t groups = 2 * 640;
        picture.resize(groups * 16 * 2160);
        auto pack = [](uint32_t a, uint32_t b, uint32_t c) { return a | b << 10 | c << 20; };
        for (uint32_t y = 0; y < 2160; ++y) {
            auto* p = reinterpret_cast<uint32_t*>(&picture[y * groups * 16]);
            for (uint32_t g = 0; g < groups; ++g, p += 4) {
                uint32_t l[6];
                for (uint32_t k = 0; k < 6; ++k) {
                    const uint32_t x = (g * 6 + k) % 3840;
                    const double r = std::sqrt(std::pow(double(x % 480) - 240, 2) + std::pow(double(y % 360) - 180, 2));
                    double v = 300 + 120 * std::sin(x * 0.013) + 90 * std::sin(y * 0.017);
                    if (r < 90) v = 520 + 180 * std::sin(r * 0.35);
                    if (((x / 60) ^ (y / 45)) % 7 == 0) v += 60;
                    l[k] = uint32_t(std::clamp(v, 64.0, 769.0));        // 769 = 1000 nit in limited-range PQ
                }
                const uint32_t cb = 512 + uint32_t(60 * std::sin(g * 0.08) + 60), cr = 512 + uint32_t(60 * std::cos(y * 0.01) + 60);
                p[0] = pack(cb, l[0], cr); p[1] = pack(l[1], cb, l[2]); p[2] = pack(cr, l[3], cb); p[3] = pack(l[4], cr, l[5]);
            }
        }
    }

    static void until(int64_t when, HANDLE timer) {
        for (;;) {
            const int64_t left = when - qpc();
            if (left <= 0) return;
            const double us = double(left) * 1e6 / double(qpcFreq());
            if (us > 700) {
                LARGE_INTEGER due; due.QuadPart = -int64_t((us - 500) * 10);
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, 5);
            } else YieldProcessor();
        }
    }

    void videoLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        const uint32_t w = in.geom & 0xFFFF, h = in.geom >> 16, S = out.slices, stride = in.stride;
        (void)w;
        uint32_t desc = 0, seq = 0;
        uint64_t n = 0;
        double start = double(ticksAt(qpc())) + 250e6 * 0.02;
        uint64_t total = 0;
        while (!quit) {
            double period, lines, step, repeat; bool lockedNow; uint32_t meta[16];
            {
                std::lock_guard g(lock);
                refresh();
                lockedNow = !(in.ctrl & XCAP_CTRL_SRC_HDMI) || num("lock", 1) != 0;
                period = 250e6 * 1000.0 / num("rate", 59940) ;
                lines = period / 2250;
                step = num("step", 12);
                repeat = std::max(1.0, num("repeat", 2));
                if (const std::string want = keys.count("clip") ? keys["clip"] : std::string(); want != clipName) {
                    clipName = want; clipFrames = 0; clip.clear();
                    if (!want.empty()) {
                        std::ifstream f(want, std::ios::binary); f.seekg(0, std::ios::end); const auto n = size_t(f.tellg()); f.seekg(0);
                        xcap_frame_header hd{}; f.read(reinterpret_cast<char*>(&hd), sizeof hd);
                        // only whole 3840x2160 v210 frames: videoLoop() takes 2160 rows of 10240 bytes out of each
                        if (f && hd.magic == XCAP_HDR_MAGIC && hd.stride == 10240 && hd.data_bytes == 10240u * 2160u && hd.reserved1 && n != size_t(-1) && n >= XCAP_HDR_BYTES + size_t(hd.data_bytes) * hd.reserved1) {
                            clip.resize(size_t(hd.data_bytes) * hd.reserved1); f.seekg(XCAP_HDR_BYTES); f.read(reinterpret_cast<char*>(clip.data()), std::streamsize(clip.size()));
                            std::memcpy(clipMeta, hd.meta, sizeof clipMeta); clipFrames = hd.reserved1; readAt = 0; refresh();
                        }
                    }
                }
                std::memcpy(meta, &regs[((in.ctrl & XCAP_CTRL_SRC_HDMI) ? XCAP_REG_HDMI0 : XCAP_REG_META0) / 4], sizeof(meta));
            }
            if (!lockedNow) { Sleep(20); start = double(ticksAt(qpc())) + period; continue; }
            uint8_t* slot = ring.data() + (n % in.slots) * out.frame_bytes;
            const uint64_t pictureNo = uint64_t(total / uint64_t(repeat));
            const bool same = total % uint64_t(repeat) != 0;
            const size_t shift = size_t(pictureNo * uint64_t(step / 6) % 640) * 16;
            auto* head = reinterpret_cast<xcap_frame_header*>(slot);
            std::memset(head, 0, XCAP_HDR_BYTES);
            head->magic = XCAP_HDR_MAGIC; head->seq = seq; head->ts_first_pixel = uint64_t(start); head->geom = in.geom; head->stride = stride;
            head->pixfmt = in.pixfmt; head->data_bytes = in.data_bytes; std::memcpy(head->meta, meta, sizeof(meta));
            const uint32_t sliceLines = in.slice_bytes / stride;
            for (uint32_t s = 0; s < S && !quit; ++s) {
                const uint32_t y0 = s * sliceLines, y1 = std::min(h, y0 + sliceLines);
                for (uint32_t y = y0; y < y1; ++y) {
                    uint8_t* row = slot + XCAP_HDR_BYTES + size_t(y) * stride;
                    if (y >= 2160) break;                                   // the pictures here have 2160 rows (start() checks, too)
                    if (clipFrames && in.pixfmt == XCAP_PIXFMT_V210 && stride == 10240) { std::memcpy(row, &clip[(pictureNo % clipFrames) * size_t(10240) * 2160 + size_t(y) * 10240], 10240); continue; }
                    if (in.pixfmt == XCAP_PIXFMT_V210) std::memcpy(row, &picture[size_t(y) * 640 * 32 + shift], std::min<size_t>(stride, 10240));
                    if (y >= 100 && y < 260 && in.pixfmt == XCAP_PIXFMT_V210 && stride >= 2000) std::memset(row + 400, 0x20, 1600);      // a HUD that stands still
                }
                if (s + 1 == S) {
                    auto* t = reinterpret_cast<xcap_frame_trailer*>(slot + out.frame_bytes - XCAP_TRL_BYTES);
                    std::memset(t, 0, XCAP_TRL_BYTES);
                    t->magic = XCAP_TRL_MAGIC; t->seq = seq; t->flags = same ? XCAP_FRAME_REPEAT : 0;
                    t->ts_last_pixel = t->ts_last_out = uint64_t(start + lines * h);
                    t->bands = S;
                }
                until(qpcAt(start + lines * y1 + 250 * 5), timer);          // 5 us through PCIe
                desc = (desc + 1) & 0xFFFFFF;
                MemoryBarrier();
                ctl->hw_desc_count = desc;
                SetEvent(HANDLE(uintptr_t(in.event)));
            }
            start += period;
            ++n; ++seq; ++total;
        }
        CloseHandle(timer);
    }

    void soundLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        std::vector<uint16_t> carrier;
        std::vector<uint32_t> pcm; std::string pcmName; size_t pcmAt = 0;      // recorded audio words (xcap_test record), replayed as they came
        std::string name;
        size_t at = 0;
        uint64_t frame = 0;
        uint32_t seq = 0;
        double start = double(ticksAt(qpc()));
        const double ppm = [&] { std::lock_guard g(lock); refresh(); return num("audioppm", 0); }();
        while (!quit) {
            unsigned ch, rate;
            {
                std::lock_guard g(lock);
                refresh();
                ch = unsigned(num("audio", 2));
                rate = unsigned(num("crate", 48000));
                const auto want = keys.count("carrier") ? keys["carrier"] : std::string();
                if (want != name) {
                    name = want; carrier.clear(); at = 0;
                    if (!want.empty()) { std::ifstream f(want, std::ios::binary); f.seekg(0, std::ios::end); const auto n = size_t(f.tellg()); f.seekg(0); carrier.resize(n / 2); f.read(reinterpret_cast<char*>(carrier.data()), std::streamsize(carrier.size() * 2)); }
                }
                if (carrier.empty() || rate < 8000 || rate > 768000) rate = 48000;
                if (const auto want = keys.count("pcm") ? keys["pcm"] : std::string(); want != pcmName) {
                    pcmName = want; pcm.clear(); pcmAt = 0;
                    if (!want.empty()) {
                        std::ifstream f(want, std::ios::binary); f.seekg(0, std::ios::end); const auto n = size_t(f.tellg()); f.seekg(0);
                        pcm.resize(n / 4); f.read(reinterpret_cast<char*>(pcm.data()), std::streamsize(pcm.size() * 4));
                        size_t a = 0; while (a < pcm.size() && XCAP_AUD_CHANNEL(pcm[a]) != 0) ++a;              // start and wrap on channel 0
                        size_t b = pcm.size(); while (b > a && XCAP_AUD_CHANNEL(pcm[b - 1]) != ch - 1) --b;
                        pcm = std::vector<uint32_t>(pcm.begin() + ptrdiff_t(a), pcm.begin() + ptrdiff_t(b));
                    }
                }
            }
            if (ch != 8 && ch != 2) { Sleep(10); start = double(ticksAt(qpc())); continue; }
            const unsigned frames = std::clamp<unsigned>(unsigned(uint64_t(rate) * 38 / 48000), 1, XCAP_AUD_WORDS / ch);   // 0.79 ms, or a full packet
            const double ticks = double(frames) * 250e6 / rate * (1 + ppm * 1e-6);
            auto* p = reinterpret_cast<xcap_audio_packet*>(audio.data() + (seq % XCAP_AUD_RING) * XCAP_AUD_PACKET);
            p->magic = XCAP_AUD_MAGIC; p->seq = seq; p->ts = uint32_t(uint64_t(start)); p->words = 0;
            if (!pcm.empty()) for (unsigned i = 0; i < frames * ch && p->words < XCAP_AUD_WORDS; ++i) { p->word[p->words++] = pcm[pcmAt]; pcmAt = (pcmAt + 1) % pcm.size(); }
            else for (unsigned i = 0; i < frames; ++i, ++frame) for (unsigned c = 0; c < ch && p->words < XCAP_AUD_WORDS; ++c) {
                uint32_t v;
                if (!carrier.empty()) { v = uint32_t(carrier[at]) << 8; at = (at + 1) % carrier.size(); }
                else v = uint32_t(int32_t(6000 * std::sin(double(frame % 48000) * 6.283185307 * (300 + 100 * c) / 48000))) & 0xFFFFFF;
                p->word[p->words++] = 0x80000000u | c << 24 | v;
            }
            start += ticks;
            until(qpcAt(start), timer);
            ++seq;
            MemoryBarrier();
            ctl->aud_hw_count = seq & 0xFFFFFF;
            if (in.audio_event) SetEvent(HANDLE(uintptr_t(in.audio_event)));
        }
        CloseHandle(timer);
    }

    void stop() {
        quit = true;
        if (video.joinable()) video.join();
        if (sound.joinable()) sound.join();
        if (ctl) ctl->state = 2;
    }
    // What the driver refuses (CheckStart), and what the pictures made here cannot fill.
    static bool valid(const xcap_start_in& i) {
        if (i.size != sizeof(i) || i.slots < 2 || i.slots > XCAP_MAX_SLOTS) return false;
        if (!i.data_bytes || i.data_bytes > 64u * 1024 * 1024 || (i.data_bytes & 15)) return false;
        if (!i.slice_bytes || (i.slice_bytes & 15) || i.slice_bytes > i.data_bytes) return false;
        if ((i.data_bytes + i.slice_bytes - 1) / i.slice_bytes > XCAP_MAX_BANDS) return false;
        const uint32_t h = i.geom >> 16;
        if (i.stride < 16 || !h || h > 2160 || uint64_t(i.stride) * h > i.data_bytes) return false;
        return true;
    }
    bool start(const xcap_start_in& i, xcap_start_out& o) {
        stop();
        if (!valid(i)) return false;
        in = i;
        out = {};
        out.frame_bytes = XCAP_HDR_BYTES + ((i.data_bytes + 4095) & ~4095u) + XCAP_TRL_BYTES;
        out.slices = (i.data_bytes + i.slice_bytes - 1) / i.slice_bytes;
        out.descs_per_ring = i.slots * out.slices;
        out.contiguous_kb = 0;
        ctl = std::make_unique<xcap_ctl>();
        std::memset(ctl.get(), 0, sizeof(xcap_ctl));
        ctl->state = 1;
        for (uint32_t s = 0; s < i.slots; ++s) for (uint32_t k = 0; k < out.slices; ++k) ctl->slice_end[s][k] = s * out.slices + k + 1;
        ring.assign(size_t(out.frame_bytes) * i.slots, 0);
        audio.assign(size_t(XCAP_AUD_RING) * XCAP_AUD_PACKET, 0);
        out.ctl = uintptr_t(ctl.get());
        for (uint32_t s = 0; s < i.slots; ++s) out.slot[s] = uintptr_t(ring.data() + size_t(s) * out.frame_bytes);
        out.audio_ring = i.audio ? uintptr_t(audio.data()) : 0;
        if (picture.empty()) buildPicture();
        quit = false;
        video = std::thread([this] { videoLoop(); });
        if (i.audio) sound = std::thread([this] { soundLoop(); });
        o = out;
        return true;
    }
    ~Sim() { stop(); }
};

Sim& sim() { static Sim s; return s; }
}

bool simEnabled() {
    static const bool on = [] {
        wchar_t v[512]{};
        if (!GetEnvironmentVariableW(L"XCAP_SIM", v, 512)) return false;
        sim().file = v;
        timeBeginPeriod(1);
        return true;
    }();
    return on;
}

bool simIoctl(DWORD code, const void* in, void* out, DWORD& error) {
    Sim& s = sim();
    if (code == XCAP_IOCTL_START) {
        if (s.start(*static_cast<const xcap_start_in*>(in), *static_cast<xcap_start_out*>(out))) return true;
        error = ERROR_INVALID_PARAMETER;
        return false;
    }
    if (code == XCAP_IOCTL_STOP) { s.stop(); return true; }
    std::lock_guard g(s.lock);
    s.refresh();
    s.regs[XCAP_REG_ID / 4] = XCAP_ID_MAGIC;
    s.regs[XCAP_REG_VERSION / 4] = 0x00030100u;
    s.regs[XCAP_REG_VCLK_HZ / 4] = 300000000u;
    switch (code) {
    case XCAP_IOCTL_INFO: { auto* i = static_cast<xcap_info*>(out); *i = {}; i->drv_version = 0x00020000u; i->hw_version = 0x00030100u; i->link_speed = 3; i->link_width = 4; return true; }
    case XCAP_IOCTL_REG_RD: { auto r = *static_cast<const xcap_reg*>(in); if (r.offset >= 0x1000) break; r.value = s.regs[r.offset / 4]; *static_cast<xcap_reg*>(out) = r; return true; }
    case XCAP_IOCTL_REG_WR: { const auto r = *static_cast<const xcap_reg*>(in); if (r.offset >= 0x1000) break; s.regs[r.offset / 4] = r.value; s.readAt = 0; return true; }
    case XCAP_IOCTL_CLOCK: { auto* c = static_cast<xcap_clock*>(out); const int64_t q = qpc(); c->fpga_ticks = ticksAt(q); c->qpc_before = c->qpc_after = uint64_t(q); return true; }
    }
    error = ERROR_INVALID_FUNCTION;
    return false;
}

} // namespace xcap
