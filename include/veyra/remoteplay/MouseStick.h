// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/remoteplay/Types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>

namespace veyra::remoteplay {

// Raw Input writes relative motion; the PS5 session samples it at send time.
// All timestamps use the local monotonic clock in microseconds.
class MouseStick {
public:
    struct Tuning {
        double horizontal = 1.0, vertical = 1.0, curve = 1.0, deadzone = 0.08, smoothingMs = 0;
        double turnBoost = 1.0, turnSpeed = 2400, dpi = 800, deadzoneShape = 0, adsScale = 1, walkScale = .45;
        int adsDelayMs = 0, unlock = 0, gyro = 0, adsAction = 16;
        bool carry = false;
        double inputDeadzone = 0;
        bool invertY = false;
        bool mouseLook = true;
        bool operator==(const Tuning&) const = default;
    };

    explicit MouseStick(Tuning tuning) : tuning_(tuning) {}

    void setTuning(Tuning tuning) {
        std::lock_guard lock(mutex_);
        tuning_ = tuning;
        clear();
    }

    void resetMotion() {
        std::lock_guard lock(mutex_);
        clear();
    }

    void move(int dx, int dy, int64_t nowUs) {
        if (dx == 0 && dy == 0) return;
        std::lock_guard lock(mutex_);
        if (!tuning_.mouseLook) return;

        const int64_t gap = lastMove_ < 0 ? 0 : nowUs-lastMove_;
        if(gap<0)return;
        if(used_&&gap==0){
            auto& last=reports_[newest_];last.dx+=dx;last.dy+=dy;
            if(!knownPeriod_)last.periodUs=std::clamp<int64_t>(int64_t(std::abs(last.dx)+std::abs(last.dy))*1000,1000,8000);
            return;
        }
        const double count = std::hypot(double(dx), double(dy));
        const bool firstFine = (lastMove_ < 0 || gap > 70000) && count == 1;
        // A slow physical move can produce one count every several USB polls.
        // Continuity follows nonzero reports here, independent of USB poll rate.
        const bool sparse = firstFine ||
            (gap >= 6500 && gap <= 70000 && count <= std::max(2., tuning_.dpi / 800.) &&
             count * 1000000. / gap <= 200. * tuning_.dpi / 800.);
        if (gap > 70000 || (!sparse && gap > 16000) || (sparse && !sparse_)) used_ = 0;
        int64_t interval=period_;
        if(firstFine)interval=28571; // One isolated count carries no report-period evidence.
        else if(sparse)interval=gap;
        else if (gap > 0 && gap <= 16000) {
            interval = std::max<int64_t>(125, gap);
            const auto reported = std::clamp<int64_t>(interval, 125, 8000);
            period_ = !knownPeriod_ || sparse_ ? reported : (period_ * 3 + reported + 2) / 4;
            if (used_ == 1 && (!knownPeriod_ || sparse_)) reports_[newest_].periodUs = reported;
            knownPeriod_ = true;
        }else if(!knownPeriod_){
            interval=std::clamp<int64_t>((std::abs(dx)+std::abs(dy))*1000LL,1000,8000);
        }
        sparse_=sparse;
        lastMove_=nowUs;
        newest_ = (newest_ + 1) % reports_.size();
        reports_[newest_] = {nowUs, std::max<int64_t>(interval, 125), double(dx), double(dy)};
        used_ = std::min(used_ + 1, reports_.size());
    }

    std::pair<int16_t, int16_t> sample(int64_t nowUs) {
        std::lock_guard lock(mutex_);
        return axes(nowUs,false);
    }

    void apply(ControllerState& pad,int64_t nowUs,bool menu=false) {
        std::lock_guard lock(mutex_);
        const bool ads=tuning_.adsAction==16?pad.l2!=0:tuning_.adsAction==17?pad.r2!=0:
            tuning_.adsAction>=0&&tuning_.adsAction<16&&(pad.buttons&(1u<<tuning_.adsAction));
        if(ads&&!adsWas_)adsSince_=nowUs;
        adsWas_=ads;
        const auto [x,y]=axes(nowUs,ads&&nowUs-adsSince_>=tuning_.adsDelayMs*1000LL);
        if(menu||menu_){pad.leftX=x;pad.leftY=y;}
        else if(tuning_.gyro==1||(tuning_.gyro==2&&ads)){
            const double gain=ads?tuning_.adsScale:1.;
            pad.motionValid=true;pad.motionTimestampUs=nowUs;
            pad.gyro={float(-filteredY_*gain*tuning_.vertical*(tuning_.invertY?-1:1)/tuning_.dpi),
                      float(-filteredX_*gain*tuning_.horizontal/tuning_.dpi),0};
        }else{pad.rightX=x;pad.rightY=y;}
    }
    void menu(bool active){std::lock_guard lock(mutex_);menu_=active;}
    double speed(){std::lock_guard lock(mutex_);return std::hypot(filteredX_,filteredY_);}

private:
    std::pair<int16_t,int16_t> axes(int64_t nowUs,bool ads){
        if(!used_)return {};
        const auto& last=reports_[newest_];
        const auto age=std::max<int64_t>(0,nowUs-last.timeUs);
        int64_t holdUs = std::clamp<int64_t>(period_ + 1500, 2500, 10000);
        if (sparse_) {
            // At low poll rates, the same hand speed can alternate between
            // one and two USB intervals per count. Cover the recent cadence.
            int64_t sparseGap = last.periodUs;
            for (size_t i = 1; i < std::min<size_t>(used_, 5); ++i) {
                const auto& previous = reports_[(newest_ + reports_.size() - i) % reports_.size()];
                sparseGap = std::max(sparseGap, std::min(previous.periodUs, 2 * last.periodUs));
            }
            holdUs = std::min<int64_t>(70000, sparseGap + std::max<int64_t>(2000, last.periodUs / 8));
        }
        const auto releaseUs=std::clamp<int64_t>(period_*3/2,4000,8000);
        const auto windowUs = sparse_ ? std::min<int64_t>(2 * last.periodUs, 70000)
                                      : std::clamp<int64_t>(period_ * 3 / 2, 6000, 12000);
        const double dt=std::clamp<double>(lastSampleUs_<0?4000:nowUs-lastSampleUs_,0.,16000.)/1000000.;
        lastSampleUs_=nowUs;
        const double gain=std::clamp(1.-double(age-holdUs)/releaseUs,0.,1.);
        if(!gain){filteredX_=filteredY_=0;if(!tuning_.carry||(!carryX_&&!carryY_))return {};}
        // Time-weight the newest reports. A 125 Hz mouse keeps the same
        // velocity on intervening 250 Hz sends; there is no per-send count bucket.
        double dx = 0, dy = 0;
        int64_t measuredUs = 0;
        for (size_t i = 0; i < used_ && measuredUs < windowUs; ++i) {
            const auto& report = reports_[(newest_ + reports_.size() - i) % reports_.size()];
            const int64_t overlap = std::min(report.periodUs, windowUs - measuredUs);
            const double fraction = double(overlap) / report.periodUs;
            dx += report.dx * fraction;
            dy += report.dy * fraction;
            measuredUs += overlap;
        }
        const double weight=1000000./measuredUs;
        const double alpha=tuning_.smoothingMs>0?-std::expm1(-1000.*dt/tuning_.smoothingMs):1.;
        filteredX_+=(dx*weight*gain-filteredX_)*alpha;
        filteredY_+=(dy*weight*gain-filteredY_)*alpha;
        const double scale = (ads ? tuning_.adsScale : 1.) / (tuning_.turnSpeed * tuning_.dpi / 800.);
        double x=filteredX_*tuning_.horizontal*scale, y=filteredY_*tuning_.vertical*scale*(tuning_.invertY?-1:1);
        const double mag=std::hypot(x,y);
        const double u=std::max(0.,mag-tuning_.inputDeadzone);
        const double k=std::clamp((u-.10)/.35,0.,1.);
        const double boosted=u*std::lerp(1.,tuning_.turnBoost,k*k*(3.-2.*k));
        if(mag>0){x*=boosted/mag;y*=boosted/mag;}
        if(tuning_.carry && dt>0){
            if(x*carryX_+y*carryY_<0)carryX_=carryY_=0;
            x+=carryX_/dt;y+=carryY_/dt;
            const double n=std::hypot(x,y);
            const double excess=n>1?std::min(.060,(n-1)*dt)/n:0;
            carryX_=x*excess;carryY_=y*excess;
        }
        const double n=std::hypot(x,y);
        if(n<=0)return {};
        const double deflection = std::min(n, 1.);
        // Aim curve shapes the center, meeting the linear turn range at the
        // same output so high curve values still allow quick turns.
        constexpr double aimRange = .45;
        const double response = deflection < aimRange
            ? aimRange * std::pow(deflection / aimRange, tuning_.curve) : deflection;
        const auto axis=[&](double v){
            const double dir=v/n, dz=tuning_.deadzone;
            const double axial=v==0?0.:std::copysign(dz,v);
            return (1-dz)*dir*response+std::lerp(dir*dz,axial,tuning_.deadzoneShape);
        };
        x=axis(x);y=axis(y);
        const double normal=32767./std::max(1.,std::hypot(x,y));
        return {int16_t(std::lround(x*normal)),int16_t(std::lround(y*normal))};
    }
    struct Report {
        int64_t timeUs;
        int64_t periodUs;
        double dx;
        double dy;
    };

    void clear() {
        used_ = 0;lastMove_=-1;carryX_=carryY_=0;adsWas_=false;
        lastSampleUs_ = -1;
        filteredX_ = filteredY_ = 0;
    }

    std::mutex mutex_;
    Tuning tuning_;
    std::array<Report, 256> reports_{};
    size_t newest_ = 0, used_ = 0;
    int64_t lastSampleUs_ = -1;
    int64_t lastMove_=-1,period_=3000,adsSince_=0;
    bool knownPeriod_=false,sparse_=false,adsWas_=false,menu_=false;
    double carryX_=0,carryY_=0;
    double filteredX_ = 0, filteredY_ = 0;
};

} // namespace veyra::remoteplay
