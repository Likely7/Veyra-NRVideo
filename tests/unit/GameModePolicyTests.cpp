#include "veyra/ui/GameModePolicy.h"
#include "veyra/engine/PreviewView.h"
#include <cmath>
#include <iostream>

int main() {
    using namespace veyra::ui;
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* text) {
        ++checks;
        if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; }
    };
    // This matrix includes opening (no first video frame yet), all exit paths,
    // panel takeover, and the two different input contracts for scaled output.
    for (bool session : {false, true})
    for (bool fullscreen : {false, true})
    for (bool refused : {false, true})
    for (bool panel : {false, true})
    for (bool scaled : {false, true}) {
        const auto mode = gameInputMode(session, fullscreen, refused, panel, scaled);
        if (!session || !fullscreen || refused)
            check(mode == GameInputMode::Off, "exit/failure always restores input");
        else if (panel)
            check(mode == GameInputMode::Panel, "panel owns input regardless of scale");
        else
            check(mode == (scaled ? GameInputMode::Scaled : GameInputMode::Through),
                  "opening can take over without waiting for enhancement startup");
    }
    check(!gameViewportScaled(false, 0), "default keeps exact source coordinates");
    check(gameViewportScaled(true, 0), "dialog fill enables fullscreen viewport");
    for (int mode = 1; mode <= 6; ++mode)
        check(gameViewportScaled(false, mode), "explicit transforms cannot use absolute mouse passthrough");

    veyra::engine::PreviewView view;
    view.mode = 3;
    const auto stretched = view.renderedSize(1920, 1080, 800, 600);
    check(stretched.first == 1920 && stretched.second == 1080, "stretch fills both monitor dimensions");
    const auto corner = view.sourcePoint(1920, 1080, 1920, 1080, 800, 600);
    check(corner.first == 1 && corner.second == 1, "stretch retains the bottom-right source corner");
    view.mode = 2;
    const auto filled = view.renderedSize(1920, 1080, 800, 600);
    check(filled.first >= 1920 && filled.second >= 1080, "fill covers the monitor and may crop");
    view.mode = 0;
    const auto portrait = view.renderedSize(1920, 1080, 600, 900);
    check(std::abs(portrait.first - 720) < .01 && portrait.second == 1080,
          "portrait source fits with the resized aspect rather than the opening aspect");
    std::cout << "Game mode policy: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
