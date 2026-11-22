#include "veyra/engine/PresentTearingDetector.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace veyra::engine;

namespace {
using Change = void (*)(ScanoutWindow&);
int passed = 0, failed = 0;

void require(bool condition, const char* name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    if (condition) ++passed;
    else ++failed;
}

ScanoutWindow fixed() { return {1, 120, .24, 120, 60, 60, DisplaySync::Automatic, true, false, false}; }

bool detects(ScanoutWindow window, int count) {
    PresentTearingDetector detector;
    for (int i = 0; i < count; ++i) if (detector.sample(window)) return true;
    return false;
}

void resets(Change interrupt, const char* name) {
    PresentTearingDetector detector;
    const auto good = fixed();
    for (int i = 0; i < 4; ++i) detector.sample(good);
    auto bad = good;
    interrupt(bad);
    bool result = !detector.sample(bad);
    for (int i = 0; i < 4; ++i) result &= !detector.sample(good);
    require(result && detector.sample(good), name);
}

void replay(const char* filename) {
    std::ifstream input(filename);
    PresentTearingDetector detector;
    size_t rows = 0;
    int warnings = 0;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream columns(line);
        std::string time;
        ScanoutWindow window;
        unsigned mode = 0;
        if (!(columns >> time >> window.seconds >> window.refreshHz >> window.medianMs
            >> window.refreshes >> window.samples >> mode >> window.countersValid >> window.vrrCapable)) {
            require(false, "captured window is readable");
            return;
        }
        // Older replay receipts have no separate submission count or active flag.
        if (!(columns >> window.presentCalls >> window.vrrActive)) window.presentCalls = window.samples;
        window.sync = static_cast<DisplaySync>(mode);
        if (detector.sample(window)) {
            ++warnings;
            std::cout << "Replay hint time=" << time << " span=" << detector.seconds()
                      << " samples=" << detector.samples() << " lastP50=" << window.medianMs << '\n';
        }
        ++rows;
    }
    std::cout << "Replay rows=" << rows << '\n';
    require(rows > 30 && warnings == 1, "field trace raises one sustained timing hint");
}
}

int main(int argc, char** argv) {
    require(!detects(fixed(), 4) && detects(fixed(), 5), "five continuous seconds required");
    auto window = fixed();
    window.sync = DisplaySync::Vsync;
    require(detects(window, 5), "explicit VSync is eligible");
    window.sync = DisplaySync::Tearing;
    require(!detects(window, 10), "explicit Tearing suppresses the hint");
    window = fixed(); window.vrrCapable = true;
    require(!detects(window, 4) && detects(window, 5), "a capable monitor running at fixed refresh can warn");
    window.vrrActive = true;
    require(!detects(window, 10), "active VRR suppresses the hint");
    window.vrrActive = false; window.presentCalls = 108;
    require(!detects(window, 10), "90% submission rate is ambiguous on a VRR capable monitor");
    window.presentCalls = 107;
    require(detects(window, 5), "submission rate below the ceiling remains eligible");
    window = fixed(); window.samples = 5;
    require(!detects(window, 5) && detects(window, 6), "thirty samples are required");
    window = fixed(); window.medianMs = 1;
    require(!detects(window, 10), "one millisecond is outside the trigger");
    resets([](auto& w) { w.countersValid = false; }, "missing counters interrupt the window");
    resets([](auto& w) { w.refreshes = 7110230; }, "refresh counter discontinuity interrupts the window");
    resets([](auto& w) { w.refreshes = 89; }, "refreshes below the nominal cadence interrupt the window");
    resets([](auto& w) { w.seconds = 2.4; }, "a missing interval breaks continuity");
    resets([](auto& w) { w.medianMs = 10.25; }, "a normal composed sample breaks continuity");
    resets([](auto& w) { w.sync = DisplaySync::Vsync; }, "switching presentation mode restarts the window");
    PresentTearingDetector detector;
    int hints = 0;
    for (int i = 0; i < 12; ++i) hints += detector.sample(fixed());
    detector.resetWindow();
    for (int i = 0; i < 12; ++i) hints += detector.sample(fixed());
    require(hints == 1, "one hint per session including window and mode resets");
    if (argc == 2) replay(argv[1]);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
