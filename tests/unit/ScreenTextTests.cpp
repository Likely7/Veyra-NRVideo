// On-screen subtitle reading (ScreenText.h): the cases VI Translator's own tests cover
// (ocr.rs), plus the pass selection and line timing with a scripted OCR engine.
#include "veyra/engine/ScreenText.h"

#include <clocale>
#include <cstdio>
#include <string>
#include <vector>

using namespace veyra::engine::screentext;

namespace {
int failures = 0;
void check(bool passed, const char* name) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
}
Word w(const wchar_t* text, uint32_t x, uint32_t y, uint32_t width, uint32_t line = 0) {
    return {text, {x, y, width, 24}, line};
}
std::wstring texts(const std::vector<Word>& words) {
    std::wstring out;
    for (const auto& word : words) { if (!out.empty()) out += L' '; out += word.text; }
    return out;
}

void contractions() {
    check(fixContractions(L"Cmon mister, were giving you everything you want.") ==
          L"C'mon mister, we're giving you everything you want.", "lost apostrophes put back");
    check(fixContractions(L"They were giving it away.") == L"They were giving it away.", "a real 'were' stays");
    check(fixContractions(L"Dont even think about it. Im serious, youre dead.") ==
          L"Don't even think about it. I'm serious, you're dead.", "several contractions in one line");
    check(fixContractions(L"Were going in!") == L"We're going in!", "line start without subject");
    check(fixContractions(L"Go to the guard.") == L"Go to the guard.", "plain line untouched");
}

void words() {
    check(!looksLikeWords(L"bjlwg"), "Arabic menu misread as Latin is not words");
    check(!looksLikeWords(L"ulnJiJl hjluJ9"), "capitals inside lowercase are not words");
    check(looksLikeWords(L"Go to the guard."), "a sentence is words");
    check(looksLikeWords(L"Hmm."), "vowel-less interjection");
    check(looksLikeWords(L"LSPD"), "acronym");
    check(looksLikeWords(L"Strengths"), "long consonant run of a real word");
    check(looksLikeWords(L"$500"), "no words at all is left as it is");
    check(clean({w(L"my.leg", 0, 0, 60), w(L"hurts", 70, 0, 50)}) == L"my leg hurts", "glued words split at the dot");
    check(clean({w(L"e.g.", 0, 0, 40), w(L"this", 50, 0, 40)}) == L"e.g. this", "abbreviation kept");
    check(clean({w(L"|", 0, 0, 4), w(L"~", 10, 0, 4)}).empty(), "noise is dropped");
    check(clean({w(L"你好", 0, 0, 40)}, false) == L"你好", "non-English text kept with two letters");
}

void similarity() {
    check(similar(L"Go to the guard.", L"Go to the guard"), "missing full stop is the same line");
    check(similar(L"GO TO THE GUARD.", L"go to the guard."), "case-insensitive");
    check(!similar(L"Go to the guard.", L"Get in the car."), "different lines");
    check(!similar(L"", L"x"), "empty is never similar");
    check(agree(L"Do not move. Idiot.", L"Do not move."), "a missed grey line still agrees");
    check(agree(L"", L""), "nothing read by both agrees");
    check(!agree(L"", L"to aim uold"), "one read nothing");
}

void centred() {
    // A 1920 frame, the area from x 192: the subtitle in the middle, a stats panel at the right.
    const std::vector<Word> panel{w(L"STRENGTH", 1291, 10, 110), w(L"Joey:", 490, 95, 58), w(L"to", 900, 95, 20),
                                  w(L"the", 925, 95, 36), w(L"action", 966, 95, 80), w(L"RATING", 1291, 70, 76)};
    check(texts(centeredWords(panel, 192, 1920)) == L"Joey: to the action", "side panel left out of the subtitle");
    const std::vector<Word> both{w(L"ADE", 3, 41, 45), w(L"STAGE", 57, 41, 74), w(L"2", 140, 41, 14),
                                 w(L"STRENGTH", 1292, 28, 90), w(L"RATING", 1292, 93, 64), w(L"350", 1481, 93, 33)};
    check(centeredWords(both, 192, 1920).empty(), "panels on both sides are not a subtitle");
    const std::vector<Word> line{w(L"Come", 700, 10, 60), w(L"on,", 768, 10, 40), w(L"let's", 816, 10, 60), w(L"go.", 884, 10, 40)};
    check(centeredWords(line, 192, 1920).size() == 4, "a centred line is kept whole");
    check(centeredWords({w(L"Browse", 1400, 10, 90)}, 192, 1920).empty(), "text only at the side is not a subtitle");
    // Veyra: a long line with its last words missed (or a game that does not centre exactly)
    // is still the subtitle; a short line that stays on one side is not.
    std::vector<Word> partial;
    for (uint32_t i = 0; i < 9; ++i) partial.push_back(w(L"word", 500 + i * 90, 10, 80));
    check(centeredWords(partial, 192, 1920).size() == 9, "a line across the middle, a little off-centre, is kept whole");
    const std::vector<Word> left{w(L"Objective:", 40, 10, 120), w(L"reach", 170, 10, 70), w(L"the", 250, 10, 40),
                                 w(L"boat", 300, 10, 60)};
    check(centeredWords(left, 192, 1920).empty(), "a line on the left side is not a subtitle");
}

void quality_and_order() {
    const auto [good, bad] = quality({w(L"Hello,", 0, 0, 50), w(L"wOrld", 60, 0, 50)});
    check(good == 5 && bad == 5, "good and bad characters counted");
    check(wordsOutOfOrder({w(L"to", 200, 0, 20), w(L"aim", 230, 0, 30), w(L"uold", 10, 0, 40)}), "right-to-left reading detected");
    check(!wordsOutOfOrder({w(L"Hold", 10, 0, 40), w(L"to", 60, 0, 20)}), "left-to-right reading");
    check(dropJunk({w(L"\"c'", 0, 0, 10), w(L"Go", 20, 0, 20)}).size() == 1, "quote fragments dropped");
    std::vector<Word> blob{w(L"Don't", 455, 172, 116), w(L"move.", 590, 172, 111), w(L"now.", 720, 172, 71), w(L"a", 1179, 80, 172)};
    blob[3].rect.h = 154; // a lamp read as a letter, 2.5 times the text's height
    for (int i = 0; i < 3; ++i) blob[size_t(i)].rect.h = 61;
    check(texts(dropTallWords(blob)) == L"Don't move. now.", "scenery taller than the text dropped");
    check(dropTallWords({w(L"Go", 0, 0, 20), w(L"A", 30, 0, 300)}).size() == 2, "two words are left as they are");
}

// A dark picture with a white outlined bar: the OCR stand-in reports what each pass is shown.
Crop picture() {
    Crop c;
    c.w = 200; c.h = 80;
    c.rgba.assign(size_t(c.w) * c.h * 4, 0);
    for (uint32_t y = 0; y < c.h; ++y)
        for (uint32_t x = 0; x < c.w; ++x) {
            uint8_t v = 40;
            if (y >= 30 && y < 50 && x >= 40 && x < 160) v = 240;
            uint8_t* p = &c.rgba[(size_t(y) * c.w + x) * 4];
            p[0] = p[1] = p[2] = v; p[3] = 255;
        }
    return c;
}

void passes() {
    const auto crop = picture();
    const auto prepared = preprocess(crop, Prep::Brightest, 2600);
    check(prepared.w == crop.w && prepared.h == crop.h && prepared.back == 1.0, "no scaling for an 80-row crop");
    check(prepared.bgra[(size_t(40) * prepared.w + 100) * 4] == 15 && prepared.bgra[0] == 215, "bright text becomes dark on light");
    const auto small = preprocess(crop, Prep::Brightest, 100);
    check(small.w <= 100 && small.h <= 100 && small.back == 2.0, "large crops halved to the engine's limit");
    Crop tiny = crop; tiny.h = 40; tiny.rgba.resize(size_t(tiny.w) * tiny.h * 4);
    check(preprocess(tiny, Prep::Brightest, 2600).h == 80, "short crops upscaled twice");

    int calls = 0;
    // Both passes agree: two calls, sure.
    Engine same = [&](const uint8_t*, uint32_t, uint32_t) {
        ++calls;
        return std::vector<RawWord>{{0, L"Get", 40, 30, 30, 20}, {0, L"in.", 80, 30, 30, 20}};
    };
    auto reading = recognize(crop, same, 2600);
    check(calls == 2 && reading.sure && texts(reading.words) == L"Get in.", "agreeing passes read once each and are sure");
    // The passes disagree: the outlined pass runs and the reading is unsure.
    calls = 0;
    Engine differ = [&](const uint8_t*, uint32_t, uint32_t) {
        ++calls;
        if (calls == 1) return std::vector<RawWord>{{0, L"Get", 40, 30, 30, 20}, {0, L"in.", 80, 30, 30, 20}};
        if (calls == 2) return std::vector<RawWord>{{0, L"6et", 40, 30, 30, 20}};
        return std::vector<RawWord>{};
    };
    reading = recognize(crop, differ, 2600);
    check(calls == 3 && !reading.sure && texts(reading.words) == L"Get in.", "disagreeing passes take a third look");
}

void tracker() {
    LineTracker t;
    check(!t.feed(L"Get in the car.", true, 0), "first sight is not shown yet");
    check(!t.feed(L"Get in the car", true, 250), "still settling");
    check(t.feed(L"Get in the car.", true, 500) && t.shown() == L"Get in the car.", "settled line shown");
    check(!t.feed(L"Get in the car.", true, 750), "same line, no change");
    check(!t.feed(L"", true, 1000) && t.shown() == L"Get in the car.", "one empty read is a blink");
    check(t.feed(L"", true, 1600) && t.shown().empty(), "gone after the clear delay");
    check(!t.feed(L"Hold on.", false, 2000) && !t.feed(L"Hold on.", false, 2400), "unsure reading waits longer");
    check(t.feed(L"Hold on.", false, 2950) && t.shown() == L"Hold on.", "unsure reading shown after three times the wait");
    check(!t.feed(L"Watch out!", true, 3200) && t.shown() == L"Hold on.", "new line keeps the old one until it settles");
    check(t.feed(L"Watch out!", true, 3500) && t.shown() == L"Watch out!", "new line replaces the old one");
    t.reset();
    check(t.shown().empty(), "reset clears");
}
} // namespace

int main() {
    std::setlocale(LC_ALL, "C.UTF-8"); // non-Windows builds classify non-ASCII letters through the C library
    contractions();
    words();
    similarity();
    centred();
    quality_and_order();
    passes();
    tracker();
    return failures ? 1 : 0;
}
