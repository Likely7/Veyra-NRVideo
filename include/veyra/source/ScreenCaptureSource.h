#pragma once
#include "veyra/source/IFrameSource.h"
#include <memory>
#include <vector>

namespace veyra::source {
enum class ScreenTargetKind { Window, Monitor };
enum class ScreenCaptureMethod { Wgc, Duplication };
struct ScreenCaptureOptions {
    ScreenTargetKind kind=ScreenTargetKind::Window;
    ScreenCaptureMethod method=ScreenCaptureMethod::Wgc;
    uint64_t target=0;
    unsigned fps=0; // 0 follows the captured target's display refresh rate
    bool cursor=true;
    // No yellow WGC capture border (Windows 11). Monitor captures always drop it;
    // a window capture drops it in 游戏模式. Carried in the URI's cursor field (bit 1).
    bool borderless=false;
    unsigned left=0,top=0,right=0,bottom=0; // crop margins in source pixels
    std::wstring uri() const;
    static bool parse(const std::wstring&,ScreenCaptureOptions&);
};
struct ScreenCaptureTarget {
    ScreenTargetKind kind;
    uint64_t handle;
    uint32_t processId=0;
    std::wstring name;
    unsigned width=0,height=0,refresh=0;
    // A window on another Windows virtual desktop (cloaked by the shell, still
    // captured by WGC). desktopNumber is that desktop's 1-based Task View position
    // (0 if Windows does not say), desktopName the user's name for it (may be empty).
    // Such windows are listed after those on the shown desktop.
    bool otherDesktop=false;
    unsigned desktopNumber=0;
    std::wstring desktopName;
};
struct ScreenCaptureMetrics {uint64_t received=0,delivered=0,dropped=0;double ageMs=0;};
// While a monitor capture runs, Veyra's own windows are excluded from it. A window
// shown after the capture opened (the control pill appears ~0.1 s later) is passed
// here by the GUI thread before it becomes visible; outside a capture it is a no-op.
void excludeWindowFromScreenCapture(void* window); // an HWND
class ScreenCaptureSource final:public IFrameSource {
public:
    ScreenCaptureSource();~ScreenCaptureSource() override;
    static std::vector<ScreenCaptureTarget> targets(ScreenTargetKind);
    bool open(const SourceOpenDesc&) override;
    const SourceInfo& info()const override;
    SourceReadStatus read(pipeline::FramePacket&,const AVFrame**) override;
    bool seek(const pipeline::Rational&) override{return false;}
    void close()noexcept override;
    std::wstring status()const;
    ScreenCaptureMetrics metrics()const;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
