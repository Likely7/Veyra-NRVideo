#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <intrin.h>
#include <nmmintrin.h>
#include "xcap.h"
#include "xcap_sim.h"
#pragma comment(lib, "setupapi.lib")

namespace xcap {

std::vector<std::wstring> Device::enumerate() {
    std::vector<std::wstring> out;
    if (simEnabled()) { out.emplace_back(L"xcap-sim"); return out; }
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_XCAP, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA itf{ sizeof(itf) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_XCAP, i, &itf); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &itf, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<uint8_t> buf(need);
        auto* d = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        d->cbSize = sizeof(*d);
        if (SetupDiGetDeviceInterfaceDetailW(set, &itf, d, need, nullptr, nullptr)) out.emplace_back(d->DevicePath);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

bool Device::open(const std::wstring& path) {
    close();
    std::wstring p = path;
    if (p.empty()) {
        auto all = enumerate();
        if (all.empty()) { error_ = ERROR_FILE_NOT_FOUND; return false; }
        p = all[0];
    }
    h_ = p == L"xcap-sim" && simEnabled() ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (!h_) h_ = INVALID_HANDLE_VALUE;
    if (h_ == INVALID_HANDLE_VALUE) { error_ = GetLastError(); return false; }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return event_ && audioEvent_;
}

void Device::close() {
    stop();
    if (h_ != INVALID_HANDLE_VALUE) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (audioEvent_) { CloseHandle(audioEvent_); audioEvent_ = nullptr; }
}

bool Device::ioctl(DWORD code, const void* in, DWORD inLen, void* out, DWORD outLen) {
    if (simEnabled()) return simIoctl(code, in, out, error_);
    DWORD got = 0;
    if (DeviceIoControl(h_, code, const_cast<void*>(in), inLen, out, outLen, &got, nullptr)) return true;
    error_ = GetLastError();
    return false;
}

bool Device::info(xcap_info& out) { return ioctl(XCAP_IOCTL_INFO, nullptr, 0, &out, sizeof(out)); }
bool Device::read(uint32_t reg, uint32_t& value) {
    xcap_reg r{ reg, 0 };
    if (!ioctl(XCAP_IOCTL_REG_RD, &r, sizeof(r), &r, sizeof(r))) return false;
    value = r.value;
    return true;
}
bool Device::write(uint32_t reg, uint32_t value) {
    xcap_reg r{ reg, value };
    return ioctl(XCAP_IOCTL_REG_WR, &r, sizeof(r), nullptr, 0);
}
bool Device::clock(Clock& out) {
    xcap_clock c{};
    if (!ioctl(XCAP_IOCTL_CLOCK, nullptr, 0, &c, sizeof(c))) return false;
    out.ticks = c.fpga_ticks;
    out.qpc = int64_t(c.qpc_before + (c.qpc_after - c.qpc_before) / 2);
    out.errorQpc = int64_t(c.qpc_after - c.qpc_before) / 2;
    return true;
}

bool Device::start(const Config& config) {
    stop();
    cfg_ = config;
    xcap_start_in in{};
    in.size = sizeof(in);
    in.slots = config.slots;
    in.data_bytes = dataBytes();
    in.slice_bytes = stride() * config.sliceLines;
    in.geom = config.width | config.height << 16;
    in.stride = stride();
    in.pixfmt = config.pixfmt;
    in.ctrl = config.hdmi ? XCAP_CTRL_SRC_HDMI : XCAP_CTRL_TPG_EN;
    in.event = reinterpret_cast<uintptr_t>(event_);
    in.audio = config.audio ? 1u : 0u;
    in.audio_event = config.audio ? reinterpret_cast<uintptr_t>(audioEvent_) : 0;
    ResetEvent(event_);
    AcquireSRWLockExclusive(&views_);
    out_ = {};
    descs_ = 0;
    aud_ = 0;
    const bool ok = ioctl(XCAP_IOCTL_START, &in, sizeof(in), &out_, sizeof(out_));
    if (!ok) out_ = {};
    started_ = ok;
    ReleaseSRWLockExclusive(&views_);
    return ok;
}

void Device::stop() {
    if (!started_.exchange(false)) return;
    AcquireSRWLockExclusive(&views_);                 // nobody is inside slicesDone() / audioPackets() while the views go away
    ioctl(XCAP_IOCTL_STOP, nullptr, 0, nullptr, 0);
    out_ = {};
    ReleaseSRWLockExclusive(&views_);
}

// The card's 24-bit counter widened to 64 bits; any number of threads. A thread that read the counter
// earlier than another one already accounted for must not move the total (it would look like a wrap).
uint64_t Device::widen(std::atomic<uint64_t>& total, uint32_t now24) {
    uint64_t cur = total.load(std::memory_order_relaxed);
    for (;;) {
        const uint32_t delta = (now24 - uint32_t(cur)) & 0xFFFFFFu;
        if (!delta || delta >= 0x800000u) return cur;
        if (total.compare_exchange_weak(cur, cur + delta, std::memory_order_relaxed)) return cur + delta;
    }
}

uint64_t Device::slicesDone() {
    uint64_t done = 0;
    AcquireSRWLockShared(&views_);
    if (started_) {
        const xcap_ctl* c = ctl();
        const uint32_t now = c->hw_desc_count & 0xFFFFFFu;
        _ReadBarrier();
        const uint64_t descs = widen(descs_, now);
        const uint64_t rings = descs / out_.descs_per_ring;
        const uint32_t rest = uint32_t(descs % out_.descs_per_ring);
        const uint32_t S = out_.slices;
        // descriptors are cut at slice ends, so `rest` falls inside exactly one slice
        uint32_t lo = 0, hi = cfg_.slots * S;         // number of complete slices in this ring pass
        while (lo < hi) {
            const uint32_t mid = (lo + hi) / 2;
            if (c->slice_end[mid / S][mid % S] <= rest) lo = mid + 1; else hi = mid;
        }
        done = rings * cfg_.slots * S + lo;
    }
    ReleaseSRWLockShared(&views_);
    return done;
}

uint64_t Device::audioPackets() {
    uint64_t done = 0;
    AcquireSRWLockShared(&views_);
    if (started_ && out_.audio_ring) {
        const uint32_t now = ctl()->aud_hw_count & 0xFFFFFFu;
        _ReadBarrier();
        done = widen(aud_, now);
    }
    ReleaseSRWLockShared(&views_);
    return done;
}

bool Device::frameIntact(uint64_t frame) {
    const uint64_t done = slicesDone();
    AcquireSRWLockShared(&views_);
    const bool intact = started_ && done < (frame + cfg_.slots) * out_.slices;
    ReleaseSRWLockShared(&views_);
    return intact;
}

bool Device::running() {
    AcquireSRWLockShared(&views_);
    const bool on = started_ && ctl()->state == 1;
    ReleaseSRWLockShared(&views_);
    return on;
}

Device::Wait Device::waitSlices(uint64_t target, uint32_t timeoutMs, uint32_t spinUs) {
    static const int64_t freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER t0, t;
    QueryPerformanceCounter(&t0);
    for (;;) {
        if (!running()) return slicesDone() >= target ? Wait::Ok : Wait::Stopped;
        if (slicesDone() >= target) return Wait::Ok;
        if (spinUs) {
            LARGE_INTEGER s;
            QueryPerformanceCounter(&s);
            const int64_t until = s.QuadPart + freq * spinUs / 1000000;
            do {
                for (int i = 0; i < 16; ++i) _mm_pause();
                if (slicesDone() >= target) return Wait::Ok;
                QueryPerformanceCounter(&s);
            } while (s.QuadPart < until);
        }
        QueryPerformanceCounter(&t);
        const int64_t elapsed = (t.QuadPart - t0.QuadPart) * 1000 / freq;
        if (elapsed >= timeoutMs) return Wait::Timeout;
        WaitForSingleObject(event_, DWORD(timeoutMs - elapsed));
    }
}

void crcLanes(const uint8_t* data, size_t bytes, uint32_t crc[4]) {
    uint32_t a = crc[0], b = crc[1], c = crc[2], d = crc[3];
    const uint32_t* p = reinterpret_cast<const uint32_t*>(data);
    for (size_t n = bytes / 16; n; --n, p += 4) {
        a = _mm_crc32_u32(a, p[0]);
        b = _mm_crc32_u32(b, p[1]);
        c = _mm_crc32_u32(c, p[2]);
        d = _mm_crc32_u32(d, p[3]);
    }
    crc[0] = a; crc[1] = b; crc[2] = c; crc[3] = d;
}

} // namespace xcap
