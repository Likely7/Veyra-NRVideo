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
class CaptureHalfRate {
public:
    static constexpr unsigned kSecondCopies=3;
    void reset(){anchored_=false;havePicture_=false;shift_=false;secondCopies_=0;}
    static bool supported(double fps){return std::isfinite(fps)&&fps>=59&&fps<=61;}
    // picture: the sample's picture number, nullptr when unknown.
    bool accept(int64_t pts,int64_t transportInterval,bool boundary,const uint64_t* picture=nullptr){
        const bool repeat=picture&&havePicture_&&*picture==lastPicture_;
        havePicture_=picture!=nullptr;if(picture)lastPicture_=*picture;
        if(boundary||!anchored_||pts<last_||pts-last_>transportInterval*6){
            anchored_=true;next_=pts+transportInterval*2;last_=pts;shift_=false;secondCopies_=repeat?1:0;return true;
        }
        last_=pts;
        if(pts+transportInterval/4<next_){
            if(!shift_||!picture||repeat)return false;
            // The first copy of the next picture: move the grid onto it.
            shift_=false;secondCopies_=0;next_=pts+transportInterval*2;++shifts_;return true;
        }
        const auto period=transportInterval*2;
        next_+=((pts+transportInterval/4-next_)/period+1)*period;
        secondCopies_=repeat?secondCopies_+1:0;
        shift_=secondCopies_>=kSecondCopies;
        ++grid_;secondKept_+=repeat;
        return true;
    }
    uint64_t shifts()const{return shifts_;}
    // Samples the grid kept, and how many of them were second copies (diagnostics).
    uint64_t gridKept()const{return grid_;}
    uint64_t secondCopiesKept()const{return secondKept_;}
private:
    bool anchored_=false,havePicture_=false,shift_=false;int64_t next_=0,last_=0;
    uint64_t lastPicture_=0,shifts_=0,grid_=0,secondKept_=0;unsigned secondCopies_=0;
};
}
