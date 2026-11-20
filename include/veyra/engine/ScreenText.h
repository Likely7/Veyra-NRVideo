#pragma once
// On-screen subtitle reading for live translation: how a crop of the picture is
// prepared for OCR, how the readings are cleaned up, and when a line counts as
// settled or gone. Portable C++ (no Windows, no D3D12): the OCR engine itself is
// passed in, so the logic is testable without one.
//
// Ported from VI Translator (https://github.com/vi-translator/vi-translator-desktop,
// src-tauri/src/ocr.rs and the area loop in src-tauri/src/main.rs, v1.2.3), MIT License,
// Copyright (c) 2026 Fouad Almalki. See THIRD_PARTY_NOTICES.md. The GTA VI specific
// parts (speaker names, button icons, highlight colours, game screens) are left out.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace veyra::engine::screentext {

// RGBA8 crop of the picture (display-referred, 8 bits per channel).
struct Crop {
    uint32_t w=0,h=0;
    std::vector<uint8_t> rgba;
};

struct Rect { uint32_t x=0,y=0,w=0,h=0; };

// A word in crop pixels, with the OCR's line number (0 = first line).
struct Word {
    std::wstring text;
    Rect rect;
    uint32_t line=0;
};

// A word as an OCR engine reports it, in the prepared image's pixels.
struct RawWord {
    uint32_t line=0;
    std::wstring text;
    double x=0,y=0,w=0,h=0;
};

// How pixels become the black-on-white picture the OCR reads.
enum class Prep {
    Brightest,     // brightest channel: white and coloured letters both stay bright (outlined subtitles)
    WhiteOrColour, // only near-white or strongly coloured pixels: drops scenery behind see-through boxes
    Outlined,      // only bright pixels inside a dark outline: letters over a bright floor or sky
};

struct Prepared {
    std::vector<uint8_t> bgra; // dark text on white, BGRA8
    uint32_t w=0,h=0;
    double back=1;             // prepared pixels -> crop pixels
};

// Reads a prepared BGRA8 picture; may throw (the caller treats that as nothing read).
using Engine=std::function<std::vector<RawWord>(const uint8_t* bgra,uint32_t w,uint32_t h)>;

struct Reading {
    std::vector<Word> words;
    bool sure=true; // the passes agree; unsure readings wait longer before they are shown
};

Prepared preprocess(const Crop& crop,Prep mode,uint32_t maxDim);
std::vector<Word> recognizeWith(const Crop& crop,Prep mode,const Engine& engine,uint32_t maxDim);
// Reads the crop twice (Brightest and WhiteOrColour), a third time (Outlined) when
// the two disagree or look wrong, and keeps the most word-like reading.
// english: the heuristics that only hold for English text (plain ASCII letters,
// lost apostrophes) apply.
Reading recognize(const Crop& crop,const Engine& engine,uint32_t maxDim,bool english=true);

// Subtitles are centred: words in a block off to the side (a menu entry, a HUD
// panel) are dropped; nothing is left when even the centred block is off-centre.
// cropX: where the crop starts in the frame; frameW: the frame's width (crop pixels' scale).
std::vector<Word> centeredWords(const std::vector<Word>& words,double cropX,double frameW);
// Words joined into one line, OCR noise dropped (empty when it is mostly not letters).
std::wstring clean(const std::vector<Word>& words,bool english=true);
// Apostrophes the OCR drops in subtitle fonts ("dont" -> "don't", "were going" -> "we're going").
std::wstring fixContractions(const std::wstring& text);
// False when the words could not be English (another script misread as Latin letters, icons, a map).
bool looksLikeWords(const std::wstring& text);
// Same line, allowing for OCR differences (edit distance, case-insensitive, > 85% alike).
bool similar(const std::wstring& a,const std::wstring& b);
bool agree(const std::wstring& a,const std::wstring& b);
std::pair<int,int> quality(const std::vector<Word>& words,bool english=true); // (good, bad) characters
std::vector<Word> dropJunk(std::vector<Word> words);
// Veyra addition: with three or more words, a "word" more than twice the median height
// is scenery read as a letter (a lamp, a sign); it would pull the line off-centre.
std::vector<Word> dropTallWords(std::vector<Word> words);
bool wordsOutOfOrder(const std::vector<Word>& words);
std::vector<Word> fillGaps(const std::vector<Word>& best,const std::vector<const std::vector<Word>*>& others,bool english=true);
std::vector<bool> outlinedLetters(const Crop& crop);

// When a reading is a new line to show, and when the shown line is gone.
// A line is shown once it has been read alike for stableMs (three times as long
// when the reading is unsure: text still fading in); it is cleared once no text
// was read for clearAfterMs (not just one blink of the OCR).
class LineTracker {
public:
    int64_t stableMs=300,clearAfterMs=800;
    // One reading of the area at nowMs (empty text: nothing read). True when shown() changed.
    bool feed(const std::wstring& text,bool sure,int64_t nowMs);
    const std::wstring& shown()const{return current_;}
    void reset();
private:
    std::wstring candidate_,current_;
    int64_t candidateSince_=-1,lastSeen_=-1;
};

} // namespace veyra::engine::screentext
