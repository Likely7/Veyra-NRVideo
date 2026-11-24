// When each on-screen line's translation is shown (CaptionPresenter.h): VI Translator's
// presenter tests (main.rs, presenter_tests), plus the cases Veyra adds.
#include "../../src/ui/CaptionPresenter.h"

#include <cstdio>
#include <string>

using veyra::ui::CaptionPresenter;
using Act = CaptionPresenter<std::string>::Act;

namespace {
int failures = 0;
void check(bool passed, const char* name) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
}

// VI Translator's tests have no minimum time on screen: 0 here keeps their numbers.
void viTranslatorCases() {
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 600, 0);
        check(p.tick(600) == Act::Show && *p.shown() == "one", "translation shown when ready");
        p.ended(1, 3000); // the original was up 3 s
        check(p.tick(3100) == Act::None && p.tick(3590) == Act::None, "stays while it has not had the original's time");
        check(p.tick(3600) == Act::Hide && !p.shown(), "taken down as long after the original as it came after it");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 800, 0);
        p.tick(800);
        p.ended(1, 2000);
        p.ready(2, 2000, "two", 2500, 0);
        check(p.tick(2500) == Act::None, "next line waits until the current one had its time");
        check(p.tick(2800) == Act::Show && *p.shown() == "two", "then switches without a gap");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 500, 0);
        p.tick(500);
        p.ended(1, 1000);
        p.ready(2, 1000, "two", 3000, 0);
        check(p.tick(3000) == Act::Show && *p.shown() == "two", "next line switches at once when the current one had its time");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 320000, 0);
        p.tick(320000);
        p.ended(1, 321000);
        p.ready(2, 321000, "two", 321000, 0);
        check(p.tick(323900) == Act::None, "a very late line stays at most the lag limit after its original");
        check(p.tick(324000) == Act::Show, "and does not hold up the next line");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 500, 0);
        p.tick(500);
        p.ready(2, 900, "two", 900, 0);
        check(p.clear(2) && !p.shown(), "clearing takes the line down at once");
        check(p.tick(5000) == Act::None, "nothing left waiting after clearing");
        check(!p.ready(2, 900, "two", 5100, 0), "a translation of a cleared line is dropped");
        check(p.ready(3, 5200, "three", 5300, 0) && p.tick(5300) == Act::Show, "a line read after clearing is shown");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 500, 0);
        check(p.tick(500) == Act::Show, "first line shown");
        p.ready(2, 60000, "two", 60000, 0); // no end told for line 1
        check(p.tick(60000) == Act::None, "a line whose end was never told keeps its own lag");
        check(p.tick(60600) == Act::Show && *p.shown() == "two", "then gives way to the next");
    }
}

void veyraCases() {
    {
        // The reported case: a CPU model needs 3 s, the line is up 2 s. Before, the
        // translation came after the line had gone and was never shown.
        CaptionPresenter<std::string> p;
        p.ended(1, 2000);
        check(p.ready(1, 0, "late", 3000), "a translation after its line left is still taken");
        check(p.tick(3000) == Act::Show && *p.shown() == "late", "and shown");
        check(p.tick(4400) == Act::None, "for at least the minimum time on screen");
        check(p.tick(5000) == Act::Hide, "then for as long as its line was up");
    }
    {
        CaptionPresenter<std::string> p;
        p.ended(1, 1000);
        check(!p.ready(1, 0, "stale", 9500), "a line that left long ago is dropped");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 100);
        p.tick(100);
        p.ended(1, 300); // a short line
        check(p.tick(1000) == Act::None && p.tick(1600) == Act::Hide, "a short line stays long enough to read");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(2, 1000, "two", 1200);
        check(!p.ready(1, 0, "one", 1300), "an older line's reply never replaces a newer waiting one");
        p.tick(1300);
        check(!p.ready(1, 0, "one", 1400) && *p.shown() == "two", "nor a newer shown one");
        check(p.ready(3, 1500, "three", 1600) && p.ready(4, 1700, "four", 1800) && p.waiting(),
              "only the newest waiting line is kept");
        p.ended(2, 1500);
        check(p.tick(4000) == Act::Show && *p.shown() == "four", "a backlog is skipped to the newest line");
    }
    {
        CaptionPresenter<std::string> p;
        p.ready(1, 0, "one", 200);
        p.tick(200);
        check(p.tick(100000) == Act::None && *p.shown() == "one", "a line still on screen stays");
    }
}
}

int main() {
    viTranslatorCases();
    veyraCases();
    std::printf("%s (%d failed)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
