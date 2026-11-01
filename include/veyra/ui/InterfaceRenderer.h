#pragma once

namespace veyra::ui {
// How the interface draws, decided in main.cpp before Qt starts (2026-10-26,
// docs/SOFTWARE_UI_DEFAULT_PLAN_2026-10-26.md). Software is the default: the
// interface then avoids a GPU scene-graph swapchain/device. Video rendering
// still uses the GPU; software drawing is not a guarantee against video-driver
// faults or all overlay interactions. 设置 → 界面绘制 = GPU
// draws it with the GPU again. The compatibility switches keep their meaning:
// OBS game capture and an overlay seen at start (监控软件兼容 = 自动) still draw
// in software when GPU is chosen, and VEYRA_UI_RHI still picks a GPU renderer.
struct InterfaceRendererInputs {
    bool gpuChosen=false;        // 设置 → 界面绘制 = GPU
    bool obsGameCapture=false;   // 设置 → OBS 游戏采集兼容, or --obs-game-capture
    bool overlayCompatAuto=true; // 设置 → 监控软件兼容 = 自动
    bool overlayRunning=false;   // RTSS / NVIDIA overlay / GamePP / 抖音直播伴侣 at start
    bool compatRestart=false;    // --overlay-compat-restart: an accepted compatibility restart
    bool rendererForced=false;   // VEYRA_UI_RHI
};
enum class InterfaceRendererReason { Gpu, Default, ObsGameCapture, OverlayCompat };
struct InterfaceRendererDecision {
    bool software=false;
    bool overlayCompat=false;    // the 监控软件兼容 rule applied (logged, shown in 设置)
    InterfaceRendererReason reason=InterfaceRendererReason::Gpu;
};
constexpr InterfaceRendererDecision decideInterfaceRenderer(const InterfaceRendererInputs& in) {
    InterfaceRendererDecision d;
    d.overlayCompat=!in.obsGameCapture&&in.overlayCompatAuto&&(in.overlayRunning||in.compatRestart)&&!in.rendererForced;
    if(in.obsGameCapture){d.software=true;d.reason=InterfaceRendererReason::ObsGameCapture;}
    else if(d.overlayCompat){d.software=true;d.reason=InterfaceRendererReason::OverlayCompat;}
    else if(!in.gpuChosen&&!in.rendererForced){d.software=true;d.reason=InterfaceRendererReason::Default;}
    return d;
}
}
