#pragma once
#include <cmath>
#include <cstdint>
namespace veyra::engine {
// Explicit 60/59.94 transport -> 30/29.97 sampling before any GPU work.
// Keeps original PTS. It does not claim to detect unique pictures or repair
// irregular game cadence. Quarter-slot tolerance absorbs timestamp jitter.
//
// Phase (2026-10-27): a 30 fps game in 60 Hz shows every picture twice, and
// every second sample may sit on the second copies, so each picture reaches the
// GPU 16.7 ms after it arrived. With picture numbers (PictureCompare.h) a kept
// sample that shows the picture of the sample just before it is such a second
// copy; after kSecondCopies in a row the next sample, a first copy, is taken as
// well and the grid continues from it. 60 fps content never repeats and
// irregular content does not repeat on every kept sample, so both keep the
// plain grid; without picture numbers nothing changes.
//
// With repeat skipping enabled (issue #152), games below 30 fps show some
// pictures three or four times. The plain grid then kept the same picture
// twice (frame generation interpolated a still pair: a visible stall, 2-5 per
// second at 26-29 fps), and the phase move above fired again and again, adding
// adjacent samples (31-32 kept per second). A due sample that repeats the last
// kept picture is therefore skipped while it is within kHitchSlots samples of
// that keep; the next new picture is taken instead and the grid restarts on it.
// Kept samples stay at least two slots apart with their original PTS, so a
// 50 ms step is interpolated as 50 ms. Nothing is inserted to make up 30: the
// kept rate is the game's own rate. Past the window the picture is still
// (pause, menu) and is held on the grid as before, keeping NR/FG work steady.
class CaptureHalfRate {
public:
    static constexpr unsigned kSecondCopies=3;
    // Same hitch window as ContentDedup at a 30 fps cadence: 2.5 periods = 5 slots.
    static constexpr unsigned kHitchSlots=5;
    void reset(){anchored_=false;havePicture_=false;previousNew_=false;shift_=false;secondCopies_=0;lastSequence_=nextSequence_=0;keptValid_=holding_=waiting_=false;}
    bool setSkipRepeats(bool enabled) {
        if(skipRepeats_==enabled)return false;
        skipRepeats_=enabled;
        reset();
        return true;
    }
    static bool supported(double fps){return std::isfinite(fps)&&fps>=59&&fps<=61;}
    // picture: the sample's picture number, nullptr when unknown.
    bool accept(int64_t pts,int64_t transportInterval,bool boundary,const uint64_t* picture=nullptr,uint64_t sequence=0){
        if(transportInterval<=0){reset();return true;}
        const bool repeat=picture&&havePicture_&&*picture==lastPicture_;
        const bool adjacent=sequence&&lastSequence_?sequence==lastSequence_+1:
            anchored_&&pts>last_&&pts-last_>=transportInterval*3/4&&pts-last_<=transportInterval*5/4;
        const bool secondCopy=repeat&&previousNew_&&adjacent;
        previousNew_=picture&&!repeat;
        havePicture_=picture!=nullptr;if(picture)lastPicture_=*picture;
        const bool sequenceRestart=sequence&&lastSequence_&&sequence<=lastSequence_;
        lastSequence_=sequence;
        if(boundary||!anchored_||sequenceRestart||pts<last_||pts-last_>transportInterval*6){
            anchored_=true;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;last_=pts;shift_=false;secondCopies_=0;
            holding_=waiting_=false;kept(pts,picture,sequence);return true;
        }
        last_=pts;
        if(sequence&&!nextSequence_)nextSequence_=sequence;
        const bool beforeDue=sequence?sequence<nextSequence_:pts+transportInterval/4<next_;
        if(beforeDue){
            if(!shift_||!picture||repeat||!adjacent)return false;
            // The first copy of the next picture: move the grid onto it.
            shift_=false;secondCopies_=0;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;++shifts_;
            holding_=false;kept(pts,picture,sequence);return true;
        }
        const bool repeatOfKept=picture&&keptValid_&&*picture==keptPicture_;
        if(skipRepeats_&&repeatOfKept&&!holding_){
            const bool withinHitch=sequence&&keptSequence_?sequence-keptSequence_<kHitchSlots:
                pts-keptPts_+transportInterval/4<transportInterval*int64_t(kHitchSlots);
            // Due, but the game has not drawn a new picture yet: wait for it.
            if(withinHitch){waiting_=true;++repeatsSkipped_;return false;}
            holding_=true; // still picture: hold it on the grid
        }
        const auto period=transportInterval*2;
        // After a skipped repeat the grid restarts on this sample; otherwise it
        // keeps its phase.
        if(waiting_)next_=pts+period;
        else next_+=((pts+transportInterval/4-next_)/period+1)*period;
        // Driver PTS can run at the wrong speed. When available the transport
        // sequence selects one callback per pair, preserving the original PTS.
        // A late reader consumes the newest sample once, then starts a new pair
        // there; it must not immediately take an adjacent catch-up sample.
        if(sequence)nextSequence_=sequence+2;
        // A sample taken after a skipped repeat is the first copy of a new
        // picture (or a held still), so the phase is already where it belongs.
        if(waiting_){shift_=false;secondCopies_=0;}
        else{secondCopies_=secondCopy?secondCopies_+1:0;shift_=secondCopies_>=kSecondCopies;secondKept_+=secondCopy;}
        if(!repeatOfKept)holding_=false;
        held_+=repeatOfKept;
        waiting_=false;
        ++grid_;kept(pts,picture,sequence);
        return true;
    }
    uint64_t shifts()const{return shifts_;}
    // Samples the grid kept, and how many of them were second copies (diagnostics).
    uint64_t gridKept()const{return grid_;}
    uint64_t secondCopiesKept()const{return secondKept_;}
    // Due samples skipped because they repeated the last kept picture, and
    // repeated pictures held by the selected policy.
    uint64_t repeatsSkipped()const{return repeatsSkipped_;}
    uint64_t repeatsHeld()const{return held_;}
private:
    void kept(int64_t pts,const uint64_t* picture,uint64_t sequence){
        keptValid_=picture!=nullptr;if(picture)keptPicture_=*picture;keptPts_=pts;keptSequence_=sequence;
    }
    bool anchored_=false,havePicture_=false,previousNew_=false,shift_=false;int64_t next_=0,last_=0;
    uint64_t lastSequence_=0,nextSequence_=0;
    uint64_t lastPicture_=0,shifts_=0,grid_=0,secondKept_=0;unsigned secondCopies_=0;
    bool skipRepeats_=true,keptValid_=false,holding_=false,waiting_=false;
    uint64_t keptPicture_=0,keptSequence_=0,repeatsSkipped_=0,held_=0;int64_t keptPts_=0;
};
}
