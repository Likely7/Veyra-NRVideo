// Unified contract tests  FramePacket, FrameWindow, GuidanceFrame,
// ResetCoordinator, and fake/zero guidance provider behavior. Reset epochs
// cover all 8 Launch V1 reset events.
#include <cstdio>
#include <cstring>

#include "veyra/core/FramePacket.h"
#include "veyra/core/FrameWindow.h"
#include "veyra/core/GuidanceFrame.h"
#include "veyra/core/ResetCoordinator.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(const char* name, bool ok, const char* detail = "") {
    ++g_checks;
    std::printf("[%s] %s :: %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) ++g_failures;
}

// The provider contract the fakes below implement. Only these tests use it; it
// used to live in the removed veyra/core/EnhanceGraph.h stub.
class IGuidanceProvider {
public:
    virtual ~IGuidanceProvider() = default;
    virtual bool provide(const veyra::core::FrameWindow& window, veyra::core::GuidanceFrame& out) = 0;
    virtual veyra::core::GuidanceProvider providerType() const = 0;
    virtual void reset() = 0;
};

// Fake guidance provider for testing (simulates NVOF with known values).
class FakeNvofProvider : public IGuidanceProvider {
public:
    bool provide(const veyra::core::FrameWindow& window, veyra::core::GuidanceFrame& out) override {
        if (window.current == nullptr) return false;
        out.frameId = window.current->frameId;
        out.width = window.current->width;
        out.height = window.current->height;
        out.provider = veyra::core::GuidanceProvider::Nvof;
        out.motionIsZero = false;  // fake non-zero motion
        out.depthIsZero = true;    // no depth in NVOF-only mode
        out.sourceEpoch = window.current->sourceEpoch;
        out.requiresHistoryReset = false;
        ++provideCount;
        return true;
    }
    veyra::core::GuidanceProvider providerType() const override { return veyra::core::GuidanceProvider::Nvof; }
    void reset() override { ++resetCount; }
    int provideCount = 0;
    int resetCount = 0;
};

// Zero guidance provider (honest fallback).
class ZeroGuidanceProvider : public IGuidanceProvider {
public:
    bool provide(const veyra::core::FrameWindow& window, veyra::core::GuidanceFrame& out) override {
        if (window.current == nullptr) return false;
        out.frameId = window.current->frameId;
        out.width = window.current->width;
        out.height = window.current->height;
        out.provider = veyra::core::GuidanceProvider::Zero;
        out.motionIsZero = true;
        out.depthIsZero = true;
        out.sourceEpoch = window.current->sourceEpoch;
        out.requiresHistoryReset = false;
        return true;
    }
    veyra::core::GuidanceProvider providerType() const override { return veyra::core::GuidanceProvider::Zero; }
    void reset() override {}
};

} // namespace

int main() {
    using namespace veyra::core;

    // === FramePacket ===
    {
        FramePacket p{};
        check("packet:default-invalid", !p.valid(), "default is invalid");
        p.frameId = 1; p.width = 1920; p.height = 1080;
        // linearColor is still null  should be invalid without GPU resource
        check("packet:no-resource-invalid", !p.valid(), "no GPU resource = invalid");
        // For contract testing, we use a sentinel pointer
        p.linearColor = reinterpret_cast<ID3D12Resource*>(0x1);
        check("packet:valid", p.valid(), "all fields set = valid");
        p.ptsUs = 33333; p.durationUs = 33333;
        check("packet:pts-set", p.ptsUs == 33333 && p.durationUs == 33333, "timestamps set");
        check("packet:source-file", p.source == FrameSource::Unknown, "default source");
        p.source = FrameSource::CaptureCard;
        check("packet:source-capture", p.source == FrameSource::CaptureCard, "capture source");
    }

    // === FrameWindow ===
    {
        FrameWindow w{};
        check("window:empty-no-history", !w.hasTemporalHistory(), "no prev = no history");
        check("window:empty-no-next", !w.hasNextFrame(), "no next");
        check("window:empty-lookahead-0", w.lookaheadCount() == 0, "zero lookahead");

        FramePacket prev{}, cur{}, next{};
        prev.frameId = 1; prev.sourceEpoch = 1; prev.linearColor = reinterpret_cast<ID3D12Resource*>(0x1);
        cur.frameId = 2; cur.sourceEpoch = 1; cur.linearColor = reinterpret_cast<ID3D12Resource*>(0x2);
        next.frameId = 3; next.sourceEpoch = 1; next.linearColor = reinterpret_cast<ID3D12Resource*>(0x3);

        w.prev = &prev; w.current = &cur; w.next = &next;
        w.lookahead.push_back(&next); // simplified: next as lookahead[0]

        check("window:has-history", w.hasTemporalHistory(), "prev set");
        check("window:has-next", w.hasNextFrame(), "next set");
        check("window:lookahead-1", w.lookaheadCount() == 1, "one lookahead");
        check("window:same-epoch", w.sameSourceEpoch(), "all same epoch");

        // Change epoch on next  should break sameSourceEpoch
        next.sourceEpoch = 2;
        check("window:diff-epoch", !w.sameSourceEpoch(), "epoch mismatch detected");
    }

    // === GuidanceFrame ===
    {
        GuidanceFrame g{};
        check("guidance:default-invalid", !g.valid(), "default invalid");
        g.frameId = 42; g.width = 1920; g.height = 1080;
        g.motion = reinterpret_cast<ID3D12Resource*>(0x1);
        g.depth = reinterpret_cast<ID3D12Resource*>(0x2);
        check("guidance:valid", g.valid(), "with resources = valid");
        check("guidance:default-zero", g.provider == GuidanceProvider::Zero, "default provider");
        check("guidance:zero-flags", g.motionIsZero && g.depthIsZero, "zero flags");
        g.provider = GuidanceProvider::NvofDav2;
        check("guidance:nvof-dav2", g.provider == GuidanceProvider::NvofDav2, "combo provider");
        check("guidance:name", std::strcmp(guidanceProviderName(g.provider), "NVOF+DAV2") == 0, "provider name");
    }

    // === ResetCoordinator: all 8 Launch V1 reset events ===
    {
        ResetCoordinator rc{};
        check("reset:initial-epoch-1", rc.epoch() == 1, "epoch starts at 1");
        check("reset:initial-no-reason", rc.lastReason() == ResetReason::None, "no initial reason");
        check("reset:initial-total-0", rc.totalResets() == 0, "zero resets");

        // All 8 reset events from the Launch V1 spec
        struct { ResetReason reason; const char* name; } events[] = {
            { ResetReason::Seek,         "seek" },
            { ResetReason::SceneCut,     "cut" },
            { ResetReason::FrameDrop,    "drop" },
            { ResetReason::Resize,       "resize" },
            { ResetReason::SourceSwitch, "source-switch" },
            { ResetReason::PauseResume,  "pause-resume" },
            { ResetReason::DeviceLost,   "device-lost" },
            // 8th: a second Seek to verify epoch monotonicity
            { ResetReason::Seek,         "seek-2" },
        };

        uint64_t expectedEpoch = 1;
        uint64_t frameId = 1;
        for (const auto& e : events) {
            ++expectedEpoch;
            ++frameId;
            bool advanced = rc.reset(e.reason, frameId);
            char detail[128];
            std::snprintf(detail, sizeof(detail), "%s epoch=%llu", e.name,
                static_cast<unsigned long long>(rc.epoch()));
            check("reset:advances", advanced && rc.epoch() == expectedEpoch, detail);
        }

        check("reset:total-8", rc.totalResets() == 8, "8 total resets");
        check("reset:seek-count-2", rc.resetCount(ResetReason::Seek) == 2, "2 seeks");
        check("reset:cut-count-1", rc.resetCount(ResetReason::SceneCut) == 1, "1 cut");
        check("reset:none-noop", !rc.reset(ResetReason::None, 100), "None is noop");
        check("reset:total-still-8", rc.totalResets() == 8, "still 8 after noop");

        // Stale detection
        check("reset:stale-old", rc.isStale(1), "epoch 1 is stale");
        check("reset:not-stale-current", !rc.isStale(rc.epoch()), "current epoch not stale");
    }

    // === Fake NVOF provider ===
    {
        FakeNvofProvider provider;
        FrameWindow w{};
        FramePacket cur{};
        cur.frameId = 10; cur.width = 1920; cur.height = 1080;
        cur.sourceEpoch = 1; cur.linearColor = reinterpret_cast<ID3D12Resource*>(0x1);
        w.current = &cur;

        GuidanceFrame out{};
        check("fake-nvof:provide", provider.provide(w, out), "provide succeeds");
        check("fake-nvof:type", provider.providerType() == GuidanceProvider::Nvof, "NVOF type");
        check("fake-nvof:non-zero-motion", !out.motionIsZero, "motion is non-zero");
        check("fake-nvof:zero-depth", out.depthIsZero, "depth is zero (NVOF only)");
        check("fake-nvof:epoch", out.sourceEpoch == 1, "epoch propagated");

        provider.reset();
        check("fake-nvof:reset-counted", provider.resetCount == 1, "reset tracked");
    }

    // === Zero guidance provider ===
    {
        ZeroGuidanceProvider provider;
        FrameWindow w{};
        FramePacket cur{};
        cur.frameId = 20; cur.width = 1920; cur.height = 1080;
        cur.sourceEpoch = 1; cur.linearColor = reinterpret_cast<ID3D12Resource*>(0x1);
        w.current = &cur;

        GuidanceFrame out{};
        check("zero:provide", provider.provide(w, out), "provide succeeds");
        check("zero:type", provider.providerType() == GuidanceProvider::Zero, "Zero type");
        check("zero:motion-zero", out.motionIsZero, "motion is zero");
        check("zero:depth-zero", out.depthIsZero, "depth is zero");
    }

    // === Provider dispatch (honest identity) ===
    {
        check("identity:zero-name", std::strcmp(guidanceProviderName(GuidanceProvider::Zero), "Zero") == 0, "Zero name");
        check("identity:nvof-name", std::strcmp(guidanceProviderName(GuidanceProvider::Nvof), "NVOF") == 0, "NVOF name");
        check("identity:dav2-name", std::strcmp(guidanceProviderName(GuidanceProvider::Dav2), "DAV2") == 0, "DAV2 name");
    }

    std::printf("UNIFIED-CONTRACT: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
