#include "veyra/engine/ExportTimeline.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>
using veyra::engine::ContinuousTimeline;
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}
// Frames one second apart, fed the way VideoExportJob feeds them; counts history resets.
std::vector<double> frames(std::initializer_list<double> raw, int* resets = nullptr) {
    ContinuousTimeline timeline;
    std::vector<double> out;
    for (const double r : raw) {
        bool stepped = false;
        out.push_back(timeline.map(r, out.empty() ? 0.0 : out.back() + 1.0, &stepped));
        if (resets) *resets += stepped;
    }
    return out;
}
}
int main() {
    int resets = 0;
    check(frames({0, 1, 2, 3}, &resets) == std::vector<double>{0, 1, 2, 3} && resets == 0, "monotonic timestamps pass through");
    resets = 0;
    check(frames({0, 1, 2, 0.5, 1.5, 2.5}, &resets) == std::vector<double>{0, 1, 2, 3, 4, 5} && resets == 1,
          "a splice that steps back realigns once and every later frame keeps that offset");
    resets = 0;
    check(frames({0, 1, 2, -50, 4, 5}, &resets) == std::vector<double>{0, 1, 2, 3, 4, 5} && resets == 1,
          "one bad timestamp is repaired, then the old timeline resumes");
    check(frames({0, 1, NAN, 3}) == std::vector<double>{0, 1, 2, 3}, "an unknown timestamp continues one interval on");
    check(frames({0, 1, 1, 2}) == std::vector<double>{0, 1, 2, 3}, "a repeated timestamp is never emitted twice");
    std::printf("%s export timeline\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
