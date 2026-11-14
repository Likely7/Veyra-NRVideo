#define NOMINMAX
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/LivePairLatency.h"
#include "veyra/engine/LivePresentationResetPolicy.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/engine/CaptureHalfRate.h"
#include "veyra/engine/CapturePreviewClock.h"
#include "veyra/engine/ContentDedup.h"
#include "veyra/pipeline/PictureCompare.h"
#include "veyra/pipeline/FrameBatch.h"
#include <algorithm>
#include <array>
#include <vector>
#include "veyra/engine/FrameRateWindow.h"
#include "veyra/engine/FrameFlowWindow.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/PresentationSettings.h"
#include <cmath>
#include <chrono>
#include <limits>
#include <cstdio>
int main(){
    using veyra::engine::livePairHoldMs;
    int failed=0;
    auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failed;};
    using veyra::engine::CaptureHalfRate;
    for(int64_t interval:{166667LL,333333LL}){
        veyra::engine::PresentationScheduler scheduler;
        veyra::engine::PresentationCadence cadence;
        scheduler.reset(1,0,1000000,0,false);
        int presented=0;
        for(int i=0;i<240;++i){
            const auto now=1000000+i*interval;
            const auto due=scheduler.cadenceDeadline(i*interval,now);
            if(!cadence.rateSkipsCandidate(now,due,interval,interval)){
                cadence.submitted(now);++presented;
            }
        }
        check(presented==240,"unpaced capture remains presentable after initial anchor");
        check(!cadence.rateSkipsCandidate(100000000,1000000,interval,interval),"late completion recovers instead of starving on expired media deadline");
        // The cap runs on its own absolute grid (rateSubmitted), not on the
        // spacing anchor that submitted() moves.
        cadence.submitted(100000000);cadence.rateSubmitted(100000000,interval*4);
        check(cadence.rateSkipsCandidate(100000001,100000001,interval,interval*4),"active cap still skips candidates before its next slot");
        cadence.reset();
        check(!cadence.rateSkipsCandidate(100000001,100000001,interval,interval*4),"reset admits first frame immediately");
    }
    for(double fps:{60.0,60000.0/1001.0}){
        CaptureHalfRate sampler;const auto dt=int64_t(std::llround(10000000.0/fps));int kept=0;bool ordered=true;
        for(int i=0;i<600;++i){const bool accepted=sampler.accept(i*dt+(i%3-1)*1000,dt,false);kept+=accepted;ordered&=accepted==(i%2==0);}
        check(kept==300&&ordered,"60/59.94 transport selects one original-PTS frame per pair despite jitter");
        check(sampler.accept(0,dt,false),"backward timestamp reanchors capture sampling");
        check(sampler.accept(dt,dt,true),"history boundary cannot be swallowed by sampling");
        sampler.reset();check(sampler.accept(dt,dt,false),"reset accepts first new frame immediately");
    }
    {
        // Phase onto first copies (2026-10-27). picture(i) is the number of sample i.
        const int64_t dt=166667;
        auto run=[&](auto picture,int start,int count,std::vector<int>& kept){
            CaptureHalfRate sampler;
            for(int i=start;i<start+count;++i){const uint64_t id=picture(i);if(sampler.accept(i*dt+(i%3-1)*1000,dt,false,&id))kept.push_back(i);}
            return sampler.shifts();
        };
        // 30 fps game: samples 2k and 2k+1 show picture k; the grid starts on a second copy.
        std::vector<int> wrong;const auto shifts=run([](int i){return uint64_t(i/2);},1,400,wrong);
        bool firstsAfter=true,noneLost=true;
        for(size_t n=0;n+1<wrong.size();++n)noneLost&=wrong[n+1]/2-wrong[n]/2<=1;
        for(size_t n=6;n<wrong.size();++n)firstsAfter&=wrong[n]%2==0;
        check(shifts==1&&firstsAfter&&noneLost,"30 fps content moves onto first copies once and loses no picture");
        check(wrong.size()>=199&&wrong.size()<=201,"30 fps content keeps 30 per second");
        std::vector<int> right;
        check(run([](int i){return uint64_t(i/2);},0,400,right)==0&&right.size()==200&&std::all_of(right.begin(),right.end(),[](int i){return i%2==0;}),
              "first copies already: no move");
        // 60 fps game: every sample new; the picture numbers change nothing.
        std::vector<int> sixty;CaptureHalfRate plain;int plainKept=0;bool same=true;size_t at=0;
        run([](int i){return uint64_t(i);},0,600,sixty);
        for(int i=0;i<600;++i){const bool accepted=plain.accept(i*dt+(i%3-1)*1000,dt,false);plainKept+=accepted;if(accepted)same&=at<sixty.size()&&sixty[at++]==i;}
        check(same&&size_t(plainKept)==sixty.size(),"60 fps content keeps the plain every-second-sample grid");
        // 45 fps in 60 Hz (new,new,new,repeat): never three second copies in a row.
        std::vector<int> fortyFive;
        check(run([](int i){return uint64_t(i-i/4);},0,600,fortyFive)==0&&fortyFive.size()==300,"irregular content keeps the plain grid");
        // A still screen is no evidence that the half-rate grid sits on second
        // copies. Resuming with true 60 fps must not insert an extra sample.
        CaptureHalfRate still;unsigned stillKept=0;
        for(int i=0;i<720;++i){
            const uint64_t id=i<240?uint64_t(i/2):i<421?1000:uint64_t(1000+i-420);
            stillKept+=still.accept(i*dt,dt,false,&id);
        }
        check(still.shifts()==0&&stillKept==360,
              "half-rate still-to-60 transition does not mistake a hold for second-copy phase");
    }
    check(!CaptureHalfRate::supported(30)&&!CaptureHalfRate::supported(18)&&!CaptureHalfRate::supported(120)&&CaptureHalfRate::supported(59.94),"60-to-30 setting does not halve other transport modes");
    {
        constexpr int64_t dt=166667;
        CaptureHalfRate sampler;unsigned kept=0;bool spaced=true;int last=-2;
        for(int i=1;i<=1200;++i){
            // The card still sends 60 callbacks: its reported PTS runs 25% fast.
            if(sampler.accept(i*208333,dt,false,nullptr,uint64_t(i))){++kept;spaced&=i-last>=2;last=i;}
        }
        check(kept==600&&spaced,"half-rate uses callback sequence despite driver timestamp drift");
        CaptureHalfRate late;last=-2;spaced=true;
        for(int i=1;i<=600;++i){
            if(i%11==0)continue; // reader missed an input; don't invent a source reset
            if(late.accept(i*dt,dt,false,nullptr,uint64_t(i))){spaced&=i-last>=2;last=i;}
        }
        check(spaced,"half-rate mailbox misses never trigger an adjacent catch-up sample");
        check(!veyra::engine::presentationGenerationResetRequired(false,static_cast<veyra::pipeline::FrameFlags>(veyra::pipeline::FrameFlagBits::Drop)),
              "mailbox-only loss is not a new cadence or presentation generation");
    }
    veyra::engine::FrameRateWindow rate;rate.reset(0);
    for(int i=1;i<=60;++i)rate.complete(i*166666);
    check(std::abs(rate.rate(10000000)-60)<.01,"one second counts 60 actual completion events");
    check(rate.rate(15000000)==30&&rate.rate(20000000)==0,"stalled GPU rate decays to zero without new events");
    rate.reset(20000000);check(rate.rate(21000000)==0,"revision reset discards old FPS");
    for(int i=1;i<=30;++i)rate.complete(20000000+i*333333);
    check(std::abs(rate.rate(30000000)-30)<.01,"30 real completions are not multiplied by FG");
    rate.reset(0);for(int i=1;i<=1000;++i)rate.complete(i*10000);
    check(rate.rate(10000000)==1000,"completion rate has no 60 or 120fps cap");
    using veyra::source::captureDuration;using veyra::engine::liveSourceInterval100ns;
    using veyra::source::captureDiscontinuity;
    {
        using veyra::engine::CapturePreviewClock;
        constexpr int64_t period=166834, hostStart=10000000, wrongPeriod=208333;
        CapturePreviewClock normal, vrr, broken;
        CaptureHalfRate rawSampler, fixedSampler;
        bool original=true, variable=true, stable=true;
        unsigned transitions=0, rawKept=0, fixedKept=0;
        int64_t lastFixed=0, vrrPts=0;
        for(uint64_t i=1;i<=1200;++i){
            const auto frame=static_cast<int64_t>(i);
            const auto jitter=(frame%3-1)*2500;
            const auto correctPts=frame*period;
            const auto arrival=hostStart+correctPts+jitter;
            const auto raw=frame*wrongPeriod;
            const auto unchanged=normal.observe(i,correctPts,arrival,period,false);
            original&=!unchanged.corrected&&!unchanged.discontinuity&&unchanged.pts==correctPts;
            vrrPts+=frame%2?208333:166834;
            const auto variableFrame=vrr.observe(i,vrrPts,hostStart+vrrPts,period,false);
            variable&=!variableFrame.corrected&&!variableFrame.discontinuity&&variableFrame.pts==vrrPts;
            const auto fixed=broken.observe(i,raw,arrival,period,false);
            if(fixed.discontinuity){++transitions;fixedSampler.reset();}
            if(i>120){
                stable&=fixed.corrected&&std::llabs(fixed.pts-lastFixed-period)<=period/64;
                rawKept+=rawSampler.accept(raw,period,false);
                fixedKept+=fixedSampler.accept(fixed.pts,period,false);
            }
            lastFixed=fixed.pts;
        }
        check(original,"capture preview clock leaves normal 59.94 PTS intact");
        check(variable,"capture preview clock preserves genuine varying arrival and PTS intervals");
        check(stable&&transitions==1&&fixedKept==540&&rawKept>=670,
              "capture preview clock restores 59.94 and 29.97 sampling with 20.83 ms driver PTS");
        const auto stalled=broken.observe(1201,1200*wrongPeriod+10000,hostStart+1200*period+5310000,period,false);
        check(!stalled.corrected&&stalled.discontinuity,"capture preview clock preserves a real 531 ms stall even when PTS hides it");

        CapturePreviewClock recovered;
        for(uint64_t i=1;i<=64;++i)recovered.observe(i,int64_t(i)*wrongPeriod,hostStart+int64_t(i)*period,period,false);
        bool restored=false;
        for(uint64_t i=65;i<=112;++i){
            const int64_t raw=64*wrongPeriod+int64_t(i-64)*period;
            const auto f=recovered.observe(i,raw,hostStart+int64_t(i)*period,period,false);
            restored|=f.discontinuity&&!f.corrected&&f.pts==raw;
        }
        check(restored&&!recovered.correcting(),"capture preview clock returns to recovered driver timing");

        CapturePreviewClock skipped;
        bool corrected=false;
        for(uint64_t i=1;i<=64;++i){
            if(i%4==0)continue;
            corrected=skipped.observe(i,int64_t(i)*wrongPeriod,hostStart+int64_t(i)*period,period,false).corrected;
        }
        check(corrected,"capture preview clock accounts for skipped mailbox sequence numbers");
    }
    veyra::source::CaptureDriverDiscontinuity driverFlag;
    auto flag=[&](bool flagged=true,bool native=true,bool timed=true,double pts=1.0/60,double arrival=1.0/60){
        return driverFlag.observe(flagged,native,timed,true,pts,arrival,60);
    };
    check(flag()&&flag()&&!flag(),"persistent native flag requires three continuous timestamp pairs");
    bool stable=true;for(int i=0;i<10000;++i)stable&=!flag();
    check(stable,"persistent flag cannot keep frame generation in warmup");
    check(flag(true,true,true,-.01),"backward time preserves flagged reset");
    check(flag()&&flag()&&!flag(),"clock break requires new continuity evidence");
    check(flag(true,true,true,.1),"missing source frames preserve flagged reset");
    check(flag(true,true,true,1.0/60,.2),"arrival stall preserves flagged reset even with smooth PTS");
    check(flag(true,true,false),"missing sample timestamps preserve driver reset");
    stable=true;for(int i=0;i<20;++i)stable&=flag(true,false);
    check(stable,"compressed reference chains never suppress driver discontinuity");
    check(!flag(false)&&flag(),"isolated flag after unflagged sample always resets");
    driverFlag.reset();check(flag(),"reconnect clears persistent flag evidence");
    driverFlag.reset();
    check(driverFlag.observe(true,true,true,false,0,0,60),"first sample keeps its discontinuity");
    check(flag(true,true,true,.0164827,.0164222)&&
          flag(true,true,true,.0171206,.0172319)&&
          !flag(true,true,true,.0165042,.0164009),"user log initial callback cadence exits repeated reset");
    check(captureDiscontinuity(true,true,flag(),true,1,1+1.0/60,60),"filter cannot erase an unconsumed mailbox boundary");
    check(!captureDiscontinuity(false,true,flag(),true,1,1+1.0/60,60),"after boundary delivery continuous flagged frames can form a pair");
    check(!captureDiscontinuity(true,false,false,true,1,1+1.0/60,60),"continuous callbacks remain continuous while mailbox is overwritten");
    check(captureDiscontinuity(true,true,false,true,1,1+1.0/60,60),"driver discontinuity survives mailbox overwrite");
    check(!captureDiscontinuity(false,true,false,true,1,1+1.0/60,60),"consumed driver discontinuity does not leak to next packet");
    check(captureDiscontinuity(false,false,true,true,1,1+1.0/60,60),"driver discontinuity retained at normal cadence");
    check(captureDiscontinuity(false,false,false,true,1,0.5,60),"backwards callback clock remains hard discontinuity");
    check(captureDiscontinuity(false,false,false,true,1,1.2,60),"callback gap remains hard discontinuity");
    driverFlag.reset();
    unsigned resets=0;double vrrPts=0;bool vrrContinuous=true;
    for(int i=0;i<2400;++i){
        const double delta=1./(i%4==0?48.:i%4==1?120.:i%4==2?60.:90.);
        const bool boundary=driverFlag.observe(true,true,true,i>0,delta,delta+.0002,120);
        resets+=boundary;
        vrrContinuous&=!veyra::source::captureClockBreak(vrrPts,vrrPts+delta,120);
        const auto interval=veyra::engine::capturePairInterval100ns({83333,10000000},120,vrrPts*1000,(vrrPts+delta)*1000,false);
        if(std::abs(interval-delta*1e7)>1)++failed;
        vrrPts+=delta;
    }
    check(vrrContinuous,"2400 variable 48-120 Hz intervals preserve source history");
    check(resets==3,"120 Hz format with 48-120 Hz VRR exits persistent driver-flag warmup");
    check(driverFlag.observe(true,true,true,true,.2,.2,120),"VRR still resets on a real 200 ms stall");
    check(driverFlag.observe(true,true,true,true,.02,.09,120),"VRR does not hide source/arrival disagreement");
    check(veyra::source::captureClockBreak(1,.9,120),"VRR preserves backward-clock reset");
    check(veyra::engine::capturePairInterval100ns({83333,10000000},120,0,20,true)==83333,"reset does not reuse stale A/B interval");
    // Caller passes reset||skipped: a span across frames the player dropped is
    // not the source cadence (60 Hz capture, 100 ms gap -> keep 16.7 ms phase).
    check(veyra::engine::capturePairInterval100ns({166667,10000000},60,1000,1100,true)==166667,"skip gap does not stretch the presentation phase");
    constexpr auto drop=static_cast<veyra::pipeline::FrameFlags>(veyra::pipeline::FrameFlagBits::Drop);
    constexpr auto resize=static_cast<veyra::pipeline::FrameFlags>(veyra::pipeline::FrameFlagBits::Resize);
    check(veyra::pipeline::breaksHistory(drop),"mailbox overwrite resets temporal history");
    check(!veyra::engine::presentationGenerationResetRequired(false,drop),"missing future input retains an earlier complete interpolation pair");
    check(!veyra::engine::presentationGenerationResetRequired(false,0),"preview skip alone does not invalidate leased earlier output");
    check(veyra::engine::presentationGenerationResetRequired(true,drop),"explicit reset invalidates old output even when accompanied by a drop");
    check(veyra::engine::presentationGenerationResetRequired(false,resize|drop),"drop cannot mask a resource boundary");
    check(!veyra::engine::presentationDrainRequired(false,drop),"mailbox overwrite retains queued real presentation");
    check(veyra::engine::presentationDrainRequired(false,resize),"resize drains resource-bound presentation");
    check(!veyra::engine::generatedPresentationCurrent(4,5),"history reset suppresses queued generated frame");
    check(veyra::engine::generatedPresentationCurrent(5,5),"current generation remains presentable");
    check(veyra::pipeline::FramePacket{}.duration.isUnknown(),"unset packet duration is unknown, not known zero");
    check(captureDuration(100,166767,true,333333).to100ns()==166667,"sample duration wins over nominal");
    check(captureDuration(100,100,false,333333).to100ns()==333333,"missing sample stop uses nominal30fps");
    check(captureDuration(100,90,true,0).isUnknown(),"invalid sample and absent nominal stay unknown");
    check(liveSourceInterval100ns({1,1000},60)==10000,"1000fps packet not clamped to120fps");
    check(liveSourceInterval100ns({0,1},1000)==10000,"1000fps nominal fallback accepted");
    check(liveSourceInterval100ns({0,1},60)==166667,"known zero cannot turn60fps into120fps");
    check(liveSourceInterval100ns(captureDuration(0,0,false,333333),60)==333333,"30fps packet retained by scheduler");
    check(liveSourceInterval100ns({INT64_MAX,1},60)==1000000,"huge duration clamps before integer conversion");
    for(unsigned factor:{2u,4u,6u}){
        veyra::engine::PresentationScheduler scheduler;
        int64_t pts=0,previousDeadline=0;bool ordered=true;
        for(unsigned i=0;i<100;++i){
            const int64_t dt=i%2?200000:400000;pts+=dt;
            const auto phase=veyra::engine::livePhaseInterval100ns({dt,10000000},30,true);
            scheduler.resetPair(1,pts,pts,phase);
            if(i)ordered&=scheduler.deadline(pts-dt+dt/factor)>previousDeadline;
            previousDeadline=scheduler.deadline(pts);
        }
        check(ordered,"jittery compositor input cannot schedule a new pair before the preceding real frame");
    }
    check(veyra::engine::livePhaseInterval100ns({1,30},60,false)==333333,"device sample duration still overrides nominal capture rate");
    check(livePairHoldMs(false,1822.09,22.64)==0,"no-FG never waits on stale source PTS");
    check(livePairHoldMs(false,1e12,0)==0,"no-FG absolute PTS cannot add latency");
    check(livePairHoldMs(true,100,90)==10,"50fps FG pair half interval");
    check(std::abs(livePairHoldMs(true,100,100-1000./120)-1000./120)<1e-8,"60fps FG pair half interval");
    check(livePairHoldMs(true,1e12,0)<=1000./30,"timestamp jump has bounded hold");
    check(livePairHoldMs(true,10,20)==0,"backwards PTS cannot sleep negatively");
    check(livePairHoldMs(true,std::numeric_limits<double>::infinity(),0)==0,"nonfinite PTS rejected");
    {
        using veyra::engine::LiveArrivalSmoother;
        const int64_t interval=333667,lead=LiveArrivalSmoother::kLead100ns;
        LiveArrivalSmoother clean;bool exact=true;
        for(int i=0;i<300;++i)exact&=clean.anchor(i*interval,i*interval+5000000,interval)==i*interval+5000000-lead;
        check(exact,"callbacks without jitter anchor where they arrive, less the lead");
        clean.reset();check(clean.anchor(0,7000000,interval)==7000000-lead,"after a reset the first callback is the anchor");
        // Field log 2026-10-31: 29.972 fps, callback sd 1.87 ms, PTS 800 ppm off the
        // host clock (either way here). 100 ns units.
        const auto gaussian=[](uint32_t& seed,double sd){double sum=0;for(int i=0;i<12;++i){seed=seed*1664525u+1013904223u;sum+=double(seed>>8)/double(1u<<24);}return (sum-6)*sd;};
        for(double ppm:{800.0,-800.0,0.0}){
            LiveArrivalSmoother smoother;uint32_t seed=7;double shift=0,spread=0,raw=0;unsigned count=0;
            for(unsigned n=0;n<6000;++n){
                const double line=n*333643.0+40000,callback=line+gaussian(seed,18700);
                const auto anchor=smoother.anchor(int64_t(n*333643.0*(1-ppm*1e-6)),int64_t(callback),interval);
                if(n<300)continue;
                shift+=double(anchor+lead)-line;spread+=std::abs(double(anchor+lead)-line);raw+=std::abs(callback-line);++count;
            }
            printf("anchor at %.0f ppm: mean %.3f ms from the callback line, mean distance %.3f ms, callback %.3f ms\n",ppm,shift/count/10000,spread/count/10000,raw/count/10000);
            check(std::abs(shift/count)<=1000,"the anchor keeps the mean callback whichever clock is faster: no added delay");
            check(spread*2<raw,"the anchor carries less than half of the callback jitter");
        }
        LiveArrivalSmoother jumps;
        for(int i=0;i<100;++i)jumps.anchor(i*interval,i*interval+50000,interval);
        check(jumps.anchor(100*interval,100*interval+140000,interval)==100*interval+50000-lead,"one callback 9 ms off the line does not move it");
        check(jumps.anchor(101*interval,101*interval+50000,interval)==101*interval+50000-lead&&jumps.snaps()==0,"and the next one is back on the line");
        check(jumps.anchor(102*interval,102*interval+140000,interval)==102*interval+50000-lead&&jumps.anchor(103*interval,103*interval-40000,interval)==103*interval+50000-lead,"callbacks off the line on opposite sides do not move it");
        check(jumps.anchor(104*interval,104*interval-40000,interval)==104*interval-40000-lead&&jumps.snaps()==1,"a second one on the same side moves it at once");
        for(int i=105;i<115;++i)jumps.anchor(i*interval,i*interval+10000,interval);
        int64_t settled=0;
        for(int i=115;i<195;++i)settled=jumps.anchor(i*interval,i*interval+10000,interval)-i*interval;
        check(std::abs(settled-(10000-lead))<=1000,"a 5 ms step within the limit is tracked within 90 pairs");
        LiveArrivalSmoother fast;
        for(int i=0;i<20;++i)fast.anchor(i*83333,i*83333+50000,83333);
        check(fast.anchor(20*83333,20*83333+85000,83333)==20*83333+50000-lead&&fast.anchor(21*83333,21*83333+85000,83333)==21*83333+85000-lead,"the limit follows a shorter interval");
        using veyra::engine::liveGeneratedGrace100ns;
        check(liveGeneratedGrace100ns(1000000,1000000)==100000&&liveGeneratedGrace100ns(1000000,1010000)==100000,"on time or early callback: a generated frame keeps 10 ms from its due time");
        check(liveGeneratedGrace100ns(1030000,1000000)==130000,"late callback: its frame expires when it did on the callback anchor");
        using veyra::engine::liveFramesShareRefresh;
        const double refresh=166667;
        check(liveFramesShareRefresh(1020000,1100000,1000000,refresh,10000),"no vblank between the two presents: one refresh shows them");
        check(!liveFramesShareRefresh(1120000,1300000,1000000,refresh,10000),"a vblank between them: both are shown");
        check(liveFramesShareRefresh(1160000,1300000,1000000,refresh,10000),"a present inside the latch misses that vblank");
        check(!liveFramesShareRefresh(1020000,1100000,1000000,0,10000),"no refresh grid selects nothing");
        check(liveFramesShareRefresh(1300000,1250000,1000000,refresh,10000),"a generated frame ready after its real frame cannot be shown");
        using veyra::engine::liveSmoothedCapacityWaitMs;
        check(liveSmoothedCapacityWaitMs(3.125)==2&&liveSmoothedCapacityWaitMs(4.17)==2,"high refresh MFG keeps the 2 ms capacity wait");
        check(std::abs(liveSmoothedCapacityWaitMs(16.68)-1.03)<.01&&liveSmoothedCapacityWaitMs(33.4)==1,"60 Hz 2X waits about 1 ms, and never less");
        check(liveSmoothedCapacityWaitMs(8.33)>1.5&&liveSmoothedCapacityWaitMs(8.33)<2,"120 Hz 2X lies between");
        check(liveSmoothedCapacityWaitMs(16.68,0)==liveSmoothedCapacityWaitMs(16.68),"no callback jitter: the same wait");
        check(liveSmoothedCapacityWaitMs(16.68,.04)<1.2,"a steady card barely waits longer");
        check(std::abs(liveSmoothedCapacityWaitMs(16.68,.48)-2.5)<.01,"sd 0.6 ms (mean distance 0.48 ms) waits 2.5 ms");
        check(liveSmoothedCapacityWaitMs(16.68,1.5)==3&&liveSmoothedCapacityWaitMs(33.4,20)==3,"and nothing waits more than 3 ms");
        check(liveSmoothedCapacityWaitMs(4.17,5)==2&&liveSmoothedCapacityWaitMs(8.33,5)==liveSmoothedCapacityWaitMs(8.33)&&liveSmoothedCapacityWaitMs(12.4,5)==liveSmoothedCapacityWaitMs(12.4),"above 80 fps the wait ignores the jitter");
        const auto measuredJitter=[&](double sdMs){
            LiveArrivalSmoother smoother;uint32_t seed=7;
            for(unsigned n=0;n<2000;++n)smoother.anchor(int64_t(n*333643.0*(1-800e-6)),int64_t(n*333643.0+40000+gaussian(seed,sdMs*10000)),interval);
            return smoother.jitterMs();
        };
        check(liveSmoothedCapacityWaitMs(16.68,measuredJitter(.05))<1.25,"the smoother's measure of sd 0.05 ms stays near 1 ms");
        check(std::abs(liveSmoothedCapacityWaitMs(16.68,measuredJitter(.6))-2.5)<.4,"its measure of sd 0.6 ms lands near 2.5 ms");
        check(liveSmoothedCapacityWaitMs(16.68,measuredJitter(1.87))==3,"its measure of sd 1.87 ms reaches the limit");
        // Present(0,0) on 60.005 Hz fed with those pairs on the adaptive phase. VFG
        // medium: generated ready 8.5 ms after the callback, 0.3 ms of wake-up jitter.
        // The panel shows the last present before each refresh. A real frame replaced
        // there still reached it inside the next generated frame, so it counts too.
        struct Shown{unsigned lost=0,lostReal=0,count=0;double mean=0,callbackP95=0;};
        const auto display=[&](bool smooth,double ppm){
            const double refresh=1e7/60.0049,source=1e7/29.9722,ready=85000,delay=ready+source/2+5000,latch=3000;
            struct Item{double time;bool real;double capture,callback;};
            std::vector<Item> items;items.reserve(36000);uint32_t seed=11;LiveArrivalSmoother smoother;
            for(unsigned n=0;n<18000;++n){
                const double capture=n*source,callback=capture+40000+gaussian(seed,18700);
                const double anchor=smooth?double(smoother.anchor(int64_t(capture*(1-ppm*1e-6)),int64_t(callback),int64_t(source))):callback;
                const double real=anchor+delay;
                items.push_back({std::max(real-source/2,callback+ready)+std::abs(gaussian(seed,3000)),false,capture,callback});
                items.push_back({real+std::abs(gaussian(seed,3000)),true,capture,callback});
            }
            std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){return a.time<b.time;});
            Shown r;std::vector<double> fromCallback;std::vector<const Item*> replaced;const Item* pending=nullptr;double sum=0;
            double scan=(std::floor((items[0].time+latch)/refresh+1e-9)+1)*refresh;
            for(const auto& item:items){
                while(scan<=item.time+latch){
                    if(pending){
                        if(pending->real)replaced.push_back(pending);
                        for(const auto* shown:replaced){sum+=scan-shown->capture;fromCallback.push_back(scan-shown->callback);}
                    }
                    pending=nullptr;replaced.clear();scan+=refresh;
                }
                if(pending){++r.lost;if(pending->real){++r.lostReal;replaced.push_back(pending);}}
                pending=&item;
            }
            std::sort(fromCallback.begin(),fromCallback.end());
            r.count=unsigned(items.size());r.mean=sum/fromCallback.size();r.callbackP95=fromCallback[(fromCallback.size()-1)*95/100];
            return r;
        };
        for(double ppm:{800.0,-800.0}){
            const auto before=display(false,ppm),after=display(true,ppm);
            printf("60 Hz display at %.0f ppm: lost %.2f%% -> %.2f%%, real %.2f%% -> %.2f%%, capture-to-panel mean %.2f -> %.2f ms, callback P95 %.2f -> %.2f ms\n",ppm,100.0*before.lost/before.count,100.0*after.lost/after.count,100.0*before.lostReal/before.count,100.0*after.lostReal/after.count,before.mean/10000,after.mean/10000,before.callbackP95/10000,after.callbackP95/10000);
            check(after.lost*10<=before.lost*8,"60 Hz replay: at least a fifth fewer frames are lost");
            check(after.lostReal*5<=before.lostReal,"60 Hz replay: real frames are hardly lost any more");
            check(after.mean<=before.mean,"60 Hz replay: capture-to-panel latency does not grow");
            check(after.callbackP95<=before.callbackP95+2000,"60 Hz replay: the latency this application reports does not grow at P95");
        }
    }
    {
        // 60 fps window capture without FG (2026-10-29): the first frame was read
        // 52 ms after its compositor time, later frames 1 ms after theirs. Same
        // sequence as the engine: anchor only when this epoch and mode have none.
        using veyra::engine::livePacesSourcePts;
        check(!livePacesSourcePts(false,false,true)&&!livePacesSourcePts(false,true,false),"screen and physical capture without FG are ready-driven");
        check(livePacesSourcePts(false,false,false)&&livePacesSourcePts(true,false,true)&&livePacesSourcePts(true,true,false),"file replay, streams and FG pairs keep their PTS clock");
        auto worstHold=[](bool screen){
            veyra::engine::PresentationScheduler timeline;const bool paced=livePacesSourcePts(false,false,screen);
            int64_t read=0,worst=std::numeric_limits<int64_t>::min();
            for(int i=0;i<600;++i){
                const int64_t pts=i*166667;read=std::max(read,pts)+(i?10000:520000);const int64_t ready=read+5000;
                if(!timeline.anchored(1,paced))timeline.reset(1,pts,read,0,paced);
                if(i>=60)worst=std::max(worst,std::max(timeline.deadline(pts),timeline.cadenceDeadline(pts,ready))-ready);
            }
            return worst;
        };
        check(worstHold(true)<=0,"late first screen read is not held on every later frame");
        check(worstHold(false)>=500000,"file replay keeps the PTS clock of its first anchor");
        // FG on/off rebuilds the graph and its epochs restart at 1: the last FG pair
        // anchor (B read 52 ms late, one interval of phase) must not pace capture
        // after FG. The next frame is read 1 ms after its compositor time.
        veyra::engine::PresentationScheduler timeline;timeline.resetPair(1,1000000,1520000,166667);
        const bool paced=livePacesSourcePts(false,false,true);
        if(!timeline.anchored(1,paced))timeline.reset(1,5000000,5010000,0,paced);
        check(timeline.deadline(5000000)<=5015000&&timeline.cadenceDeadline(5000000,5015000)<=5015000,"FG pair anchor in a repeated epoch does not hold capture after FG is off");
        check(timeline.anchored(1,false)&&!timeline.anchored(1),"anchor records its pacing mode");
    }
    {
        // PictureCompare.h: a sample repeats the last new picture only when every
        // component of the whole picture is within the capture-noise band.
        using veyra::pipeline::PictureReference;using veyra::pipeline::PicturePlane;using veyra::pipeline::PictureComponents;
        std::vector<uint8_t> base(256*64);for(size_t i=0;i<base.size();++i)base[i]=uint8_t(40+(i*7919)%160);
        auto bytes=[](PictureReference& r,const std::vector<uint8_t>& picture,unsigned rows=64){PicturePlane plane{picture.data(),256,256,rows};return r.observe(&plane,1,PictureComponents::Bytes);};
        PictureReference ref;const auto first=bytes(ref,base);
        // T911 1440p NV12 repeats: ~18% of pixels +-1, a few hundred +-2, rarely one at 3.
        auto noisy=base;for(size_t i=0;i<noisy.size();i+=5)noisy[i]=uint8_t(noisy[i]+(i%3==0?2:i%3==1?-2:1));noisy[777]=uint8_t(noisy[777]+3);
        const auto repeat=bytes(ref,noisy);
        check(repeat.repeat&&repeat.id==first.id&&repeat.max8==3&&repeat.over==0,"picture within the noise band everywhere keeps its number");
        auto moved=base;moved[4321]=uint8_t(moved[4321]+4);
        const auto changed=bytes(ref,moved);
        check(!changed.repeat&&changed.id!=first.id&&changed.over==1,"one component beyond the band is a new picture");
        // +1 level per sample, judged against the last NEW picture: every 4th sample is new.
        PictureReference drift;auto fade=base;int fresh=0;
        for(int k=0;k<12;++k){for(auto& v:fade)v=uint8_t(v+1);fresh+=!bytes(drift,fade).repeat;}
        check(fresh==3,"a slow fade accumulates past the band instead of slipping through it");
        std::vector<uint16_t> deep(128*32);for(size_t i=0;i<deep.size();++i)deep[i]=uint16_t((64+(i*31)%800)<<6);
        PictureReference words;const unsigned band=PictureReference::bandFor(10,6);
        auto observeWords=[&](const std::vector<uint16_t>& picture){PicturePlane plane{reinterpret_cast<const uint8_t*>(picture.data()),256,256,32};return words.observe(&plane,1,PictureComponents::Words,band);};
        const auto w0=observeWords(deep);auto wNoise=deep;for(size_t i=0;i<wNoise.size();i+=3)wNoise[i]=uint16_t(wNoise[i]+(12<<6));
        const auto w1=observeWords(wNoise);auto wMove=deep;wMove[100]=uint16_t(wMove[100]+(13<<6));const auto w2=observeWords(wMove);
        check(band==768&&w1.repeat&&w1.id==w0.id&&!w2.repeat&&w2.over==1,"P010: 12 levels of a 10-bit value are within the band, 13 are not");
        PictureReference exact;PicturePlane e0{base.data(),256,256,64},e1{noisy.data(),256,256,64};exact.observe(&e0,1,PictureComponents::Exact);
        check(!exact.observe(&e1,1,PictureComponents::Exact).repeat,"packed 10-bit / 5-6-bit layouts compare exactly");
        check(!bytes(ref,base,32).repeat,"a different layout is a new picture");
        PictureReference one{1},two{2};check(bytes(one,base).id!=bytes(two,base).id,"picture numbers of different references never collide");
        check(PictureReference::toleranceFor(3840ull*2160*3/2)==63&&PictureReference::toleranceFor(1920ull*1080*3/2)==15&&PictureReference::toleranceFor(base.size())==0,
            "outlier tolerance: 63 components in 4K NV12, 15 in 1080p, none in a small picture");
        // 4K NV12 (T911, 2026-11-02): a repeat now and then has one component 4 levels off.
        std::vector<uint8_t> luma(3840*2160),chroma(3840*1080);
        for(size_t i=0;i<luma.size();++i)luma[i]=uint8_t(40+(i*7919+i/3840*31)%160);
        for(size_t i=0;i<chroma.size();++i)chroma[i]=uint8_t(90+(i*104729)%60);
        auto nv12=[](PictureReference& r,const std::vector<uint8_t>& y,const std::vector<uint8_t>& uv){
            PicturePlane planes[2]{{y.data(),3840,3840,2160},{uv.data(),3840,3840,1080}};return r.observe(planes,2,PictureComponents::Bytes);};
        auto outliers=[&](unsigned count,int levels){auto y=luma;for(unsigned k=0;k<count;++k){auto& v=y[(size_t(k)*1048573+12345)%y.size()];v=uint8_t(v+levels);}return y;};
        PictureReference card;const auto shown=nv12(card,luma,chroma);
        const auto one4=nv12(card,outliers(1,4),chroma);
        check(one4.repeat&&one4.id==shown.id&&one4.over==1&&one4.max8==4,"4K: a repeat with one component 4 levels off keeps its number");
        const auto many=nv12(card,outliers(63,9),chroma);
        check(many.repeat&&many.id==shown.id&&many.over==63,"4K: 63 isolated components beyond the band are still the same picture");
        const auto past=nv12(card,outliers(64,9),chroma);
        check(!past.repeat&&past.id!=shown.id&&past.over==64,"4K: the 64th makes a new picture");
        {// The smallest real change seen in the field (a still screen, 156 components) stays a new picture.
         PictureReference still;nv12(still,luma,chroma);auto y=luma;for(unsigned row=0;row<12;++row)for(unsigned x=0;x<13;++x)y[(1000+row)*3840+2000+x]+=12;
         const auto r=nv12(still,y,chroma);check(!r.repeat&&r.over==156,"4K: a 13x12 change of a still screen is a new picture");}
        {// A small element changing on every sample, back and forth (14 components): one picture.
         PictureReference blink;const auto a=nv12(blink,luma,chroma);bool same=true;
         for(int k=1;k<8;++k){auto y=luma;if(k%2)for(unsigned x=0;x<14;++x)y[500*3840+300+x]+=60;same&=nv12(blink,y,chroma).id==a.id;}
         check(same,"4K: a 14-component element blinking every sample is not a new picture");}
        {// Below the tolerance a change accumulates against the last NEW picture until it counts.
         PictureReference grow;const auto a=nv12(grow,luma,chroma);auto y=luma;int samples=0;bool fresh=false;
         while(!fresh&&samples<20){for(unsigned x=0;x<10;++x)y[800*3840+size_t(samples)*10+x]+=20;fresh=!nv12(grow,y,chroma).repeat;++samples;}
         check(fresh&&samples==7,"4K: a growing 10-component change becomes a new picture once 70 components differ");}
        {// P010 at 4K: the tolerance counts words, not bytes.
         std::vector<uint16_t> y16(3840*2160),uv16(3840*1080);for(size_t i=0;i<y16.size();++i)y16[i]=uint16_t((64+(i*31)%800)<<6);for(size_t i=0;i<uv16.size();++i)uv16[i]=uint16_t(512<<6);
         auto p010=[](PictureReference& r,const std::vector<uint16_t>& y,const std::vector<uint16_t>& uv){
             PicturePlane planes[2]{{reinterpret_cast<const uint8_t*>(y.data()),7680,7680,2160},{reinterpret_cast<const uint8_t*>(uv.data()),7680,7680,1080}};
             return r.observe(planes,2,PictureComponents::Words,PictureReference::bandFor(10,6));};
         PictureReference deep4k;const auto a=p010(deep4k,y16,uv16);auto y=y16;for(unsigned k=0;k<63;++k)y[size_t(k)*99991]=uint16_t(y[size_t(k)*99991]+(20<<6));
         const auto b=p010(deep4k,y,uv16);y[5]=uint16_t(y[5]+(20<<6));const auto c=p010(deep4k,y,uv16);
         check(b.repeat&&b.id==a.id&&b.over==63&&!c.repeat&&c.over==64,"4K P010: 63 words beyond the band repeat, 64 are new");}
    }
    {
        // ContentDedup on picture numbers: synthetic pictures sampled at a capture rate.
        // "60->30" is CaptureHalfRate (tested above), not this.
        using veyra::engine::ContentDedup;using veyra::pipeline::PictureReference;using veyra::pipeline::PicturePlane;using veyra::pipeline::PictureComponents;
        // A 64x36 picture per id; noise changes every component of every sample by up to +-noise.
        auto picture=[](uint64_t id,int noise,uint64_t sample){
            std::array<uint8_t,64*36> pixels{};
            for(unsigned i=0;i<pixels.size();++i){
                uint64_t h=(id+1)*0x9E3779B97F4A7C15ull+i*0xBF58476D1CE4E5B9ull;h^=h>>31;h*=0x94D049BB133111EBull;h^=h>>29;
                int v=16+int(h%224);
                if(noise){uint64_t n=(sample+7)*0xD6E8FEB86659FD93ull+i*0x2545F4914F6CDD1Dull;n^=n>>32;v+=int(n%unsigned(2*noise+1))-noise;}
                pixels[i]=uint8_t(std::clamp(v,0,255));
            }
            return pixels;
        };
        auto number=[](PictureReference& numbers,const std::array<uint8_t,64*36>& pixels){PicturePlane plane{pixels.data(),64,64,36};return numbers.observe(&plane,1,PictureComponents::Bytes).id;};
        struct Run{int kept=0,keptRepeats=0,droppedReal=0,regularSpans=0,spans=0;bool monotonic=true,neverLate=true;std::vector<int64_t> intervals;};
        // contentOf(sample) gives the picture shown by sample i (ground truth).
        auto run=[&](double captureFps,auto contentOf,int count,int noise,int64_t hint,int64_t expectedPeriod){
            ContentDedup dedup;PictureReference numbers;Run r;
            const int64_t dt=int64_t(std::llround(1e7/captureFps));int64_t lastContent=-1;int64_t lastPicture=-1;int64_t pendingPicture=-1;
            for(int i=0;i<count;++i){
                const int64_t pts=int64_t(i)*dt+1000;const int64_t id=contentOf(i);const uint64_t shown=number(numbers,picture(uint64_t(id),noise,uint64_t(i)));
                const auto d=dedup.observe(&shown,pts,dt,i==0,hint);
                if(d.keep){
                    ++r.kept;if(id==lastPicture)++r.keptRepeats;
                    if(pendingPicture>=0&&pendingPicture!=id)++r.droppedReal; // a newer picture superseded an unkept one
                    pendingPicture=-1;lastPicture=id;
                    r.monotonic&=d.contentPts>lastContent;r.neverLate&=d.contentPts<=pts;lastContent=d.contentPts;
                    if(i>60&&d.interval){++r.spans;r.intervals.push_back(d.interval);if(expectedPeriod&&std::llabs(d.interval-expectedPeriod)<=5000)++r.regularSpans;}
                }else if(id!=lastPicture)pendingPicture=id;
            }
            return r;
        };
        {
            // Content runs at a genuinely varying rate, with repeated transport
            // samples between changes. Failed fixed-period fitting must not
            // erase the measured content rate or suppress any new picture.
            constexpr std::array<int,11> runs{1,4,1,3,1,2,5,2,1,3,4};
            constexpr int64_t dt=166667;
            ContentDedup variable;uint64_t id=1;int left=runs[0],step=0,unknown=0,lost=0;
            int64_t last=-1;bool ordered=true;
            for(int i=0;i<1800;++i){
                const bool changed=i==0||left==0;
                if(left==0){++id;left=runs[++step%runs.size()];}
                const auto d=variable.observe(&id,i*dt,dt,i==0);
                lost+=changed&&!d.keep;
                if(d.keep){ordered&=d.contentPts>last&&d.contentPts<=i*dt;last=d.contentPts;}
                if(i>120&&variable.contentFps()==0)++unknown;
                --left;
            }
            check(lost==0&&ordered,"variable content retains each changed picture with monotonic bounded timestamps");
            check(unknown==0&&variable.contentFps()>20&&variable.contentFps()<30,
                  "variable content reports its observed rate even when no fixed cadence fits");
        }
        const int n=1200;
        for(double captureFps:{58.0,60000.0/1001.0,60.0}){
            for(double fps:{24.0,25.0,30000.0/1001.0,30.0,40.0,45.0,50.0,60.0}){
                const auto effective=std::min(fps,captureFps);
                const auto r=run(captureFps,[&](int i){return int64_t(std::floor(i*effective/captureFps+1e-9));},n,1,0,int64_t(std::llround(1e7/effective)));
                check(r.droppedReal==0&&r.keptRepeats==0&&r.monotonic&&r.neverLate,
                      "content-rate matrix keeps every unique picture once with bounded monotonic PTS");
                const auto expected=int64_t(std::floor((n-1)*effective/captureFps+1e-9))+1;
                check(r.kept==expected,"content-rate matrix matches independent 24/25/29.97/30/40/45/50/60 picture truth");
            }
        }
        {
            ContentDedup missed;constexpr int64_t dt=166667;
            for(uint64_t i=1;i<=600;i+=2){const auto id=(1ull<<56)+i;missed.observe(&id,int64_t(i)*dt,dt,i==1,0,i);}
            check(std::abs(missed.contentFps()-60)<.1,
                  "capture picture serials distinguish engine mailbox losses from lower source content rate");
        }
        {
            ContentDedup forty;double maxError=0;
            for(int i=0;i<1200;++i){const uint64_t id=uint64_t(i*2/3);forty.observe(&id,i*166667,166667,i==0);if(i>120)maxError=std::max(maxError,std::abs(forty.contentFps()-40));}
            check(maxError<.1,"stable 40-in-60 reporting is not quantized into 40/41 FPS flicker");
        }
        {const auto r=run(60.0,[](int i){return int64_t(i/2);},n,0,0,333333);
         check(r.kept==n/2&&r.keptRepeats==0&&r.droppedReal==0,"dedup 30-in-60: every real picture kept once, every repeat dropped");
         check(r.monotonic&&r.neverLate&&r.regularSpans==r.spans,"dedup 30-in-60: content spans exactly 33.3 ms, never later than the sample");}
        {const auto r=run(58.0,[](int i){return int64_t(std::floor(i*30.0/58.0+1e-9));},n,0,0,333333);
         check(r.keptRepeats==0&&r.droppedReal==0&&std::abs(r.kept-int(std::floor((n-1)*30.0/58.0))-1)<=1,"dedup 30-in-58: all real pictures kept, no repeats");
         check(r.monotonic&&r.neverLate&&r.regularSpans*100>=r.spans*95,"dedup 30-in-58: uneven 34.5/17.2 ms samples become even 33.3 ms content spans");}
        {const auto r=run(60.0,[](int i){return int64_t(std::floor(i*40.0/60.0+1e-9));},n,0,0,250000);
         check(r.keptRepeats==0&&r.droppedReal==0&&std::abs(r.kept-n*2/3)<=1,"dedup 40-in-60: all real pictures kept, no repeats");
         check(r.monotonic&&r.neverLate&&r.regularSpans*100>=r.spans*95,"dedup 40-in-60: alternating 33/17 ms samples become even 25 ms content spans");}
        {
            // 40 unique pictures sampled at 60 Hz, with a two-step 3X GPU batch.
            // The presentation deadline of each batch must follow content PTS,
            // while readiness still depends on when the sampled picture arrives.
            struct TimingRun { double lowFps; int missed; };
            const auto present=[&](bool smooth, int contentRate){
                using namespace veyra::engine;
                ContentDedup cadence;
                LiveArrivalSmoother arrival;
                LivePairLatency phase;
                PresentationScheduler scheduler;
                FramePaceWindow window;
                constexpr int64_t transport=166667, hostStart=1000000;
                constexpr std::array<int64_t,3> gpuReady{60000,100000,25000};
                int64_t prevPts=0, prevPresent=0;
                int missed=0;
                for(int i=0;i<n;++i){
                    const int64_t sample=i*transport;
                    const uint64_t id=uint64_t(i*contentRate/60);
                    const auto frame=cadence.observe(&id,sample,transport,i==0);
                    if(!frame.keep)continue;
                    const auto callback=hostStart+sample;
                    const auto anchor=smooth?arrival.contentAnchor(frame.contentPts,sample,callback,transport):callback;
                    if(!frame.interval){prevPts=frame.contentPts;continue;}
                    const auto delay=phase.select(callback,frame.interval,frame.interval,3,smooth?transport/2:0);
                    scheduler.resetPair(1,frame.contentPts,anchor,delay);
                    int64_t requiredDelay=0;
                    for(unsigned j=0;j<gpuReady.size();++j){
                        const auto pts=j==2?frame.contentPts:
                            veyra::pipeline::FrameBatch::interpolate(prevPts,frame.contentPts,j+1,3);
                        const auto deadline=scheduler.deadline(pts);
                        const auto ready=callback+gpuReady[j];
                        requiredDelay=std::max(requiredDelay,ready-anchor+frame.contentPts-pts);
                        if(i>120){
                            missed+=ready>deadline;
                            prevPresent=std::max({prevPresent+1,deadline,ready});
                            window.complete(prevPresent);
                        }
                    }
                    phase.observe(callback,requiredDelay);
                    prevPts=frame.contentPts;
                }
                return TimingRun{window.lowFps(prevPresent).value_or(0),missed};
            };
            const auto legacy=present(false,40), fixed=present(true,40);
            check(legacy.lowFps<70&&fixed.lowFps>105&&fixed.missed==0,
                "dedup 40-in-60 3X: content anchor removes cross-batch stutter and retains readiness");
            const auto steady=present(true,30);
            check(steady.lowFps>88&&steady.missed==0,"dedup 30-in-60 3X: steady cadence stays smooth");
        }
        {const auto r=run(60.0,[](int i){return int64_t(i);},n,0,0,166667);
         check(r.kept==n&&r.regularSpans==r.spans,"dedup true 60: nothing dropped, timing unchanged");}
        {// +-1 per sample: two samples of one picture differ by up to 2, like the T911 (card_diff.py).
         const auto r=run(60.0,[](int i){return int64_t(i/2);},n,1,0,333333);
         check(r.kept==n/2&&r.keptRepeats==0&&r.droppedReal==0,"dedup 30-in-60 with capture noise inside the band: repeats dropped, real pictures kept");}
        {const auto r=run(60.0,[](int i){return int64_t(i/2);},n,6,0,333333);
         check(r.kept==n&&r.droppedReal==0,"dedup 30-in-60 with noise beyond the band: every sample kept, nothing lost");}
        {// Hitch: one picture shown for four samples, then the 30 fps cadence resumes one slot later.
         const auto r=run(60.0,[](int i){return int64_t(i<301?i/2:(i<304?150:(i-1)/2));},n,0,0,333333);
         check(r.keptRepeats==0&&r.droppedReal==0&&r.monotonic&&r.neverLate,"dedup hitch: no real picture lost, timestamps stay ordered and never late");}
        {// Mailbox overwrite: the first sample of a picture is never read; its second still counts as new.
         ContentDedup dedup;int kept=0;
         for(int i=0;i<120;++i){if(i%4==2)continue;const uint64_t shown=uint64_t(i/2);kept+=dedup.observe(&shown,i*166667+1000,166667,i==0).keep;}
         check(kept==60,"dedup keeps a picture whose first sample the engine never read");}
        {// Still picture after 30 fps content: held on the 33.3 ms cadence (steady GPU work), not once per 150-250 ms.
         ContentDedup dedup;int kept=0;
         for(int i=0;i<600;++i){const uint64_t shown=i<240?uint64_t(i/2):1000u;const bool keep=dedup.observe(&shown,i*166667+1000,166667,i==0).keep;if(i>=300)kept+=keep;}
         check(kept>=148&&kept<=152&&dedup.stats().held>=140,"dedup still picture after 30 fps content is held at a steady 30 fps");}
        {
            ContentDedup recovering;constexpr int64_t dt=166667;const uint64_t picture=7;
            int64_t firstHeldSpan=0,firstHeldCadence=0;
            for(int i=0;i<30;++i){
                const auto d=recovering.observe(&picture,i*dt,dt,i==0);
                if(d.keep&&d.holdInterval&&!firstHeldSpan){firstHeldSpan=d.interval;firstHeldCadence=d.holdInterval;}
            }
            check(firstHeldSpan>firstHeldCadence&&firstHeldCadence==2*dt,
                  "static refresh distinguishes its preceding hold gap from the ongoing content cadence");
            const auto phase=veyra::engine::capturePairInterval100ns({firstHeldCadence,10000000},60,0,double(firstHeldSpan)/10000,true);
            check(phase==2*dt,"post-boundary static hold cannot stretch subsequent pair scheduling to the hitch duration");
        }
        {// Still picture after 40 fps content in 60 Hz: held on the 25 ms grid (33/17 ms samples).
         ContentDedup dedup;int kept=0;
         for(int i=0;i<600;++i){const uint64_t shown=i<240?uint64_t(std::floor(i*40.0/60.0+1e-9)):1000u;const bool keep=dedup.observe(&shown,i*166667+1000,166667,i==0).keep;if(i>=300)kept+=keep;}
         check(kept>=196&&kept<=204,"dedup still picture after 40 fps content is held at 40 fps");}
        {// Still picture from the start, no learned rate: half the 60 Hz transport.
         ContentDedup dedup;const uint64_t shown=3;int kept=0;
         for(int i=0;i<240;++i)kept+=dedup.observe(&shown,i*166667+1000,166667,i==0).keep;
         check(kept>=116&&kept<=122,"dedup still picture with no learned rate keeps 30 of a 60 Hz capture");}
        {// No CPU picture: everything is kept.
         ContentDedup dedup;int kept=0;for(int i=0;i<60;++i)kept+=dedup.observe(nullptr,i*166667+1000,166667,i==0).keep;
         check(kept==60,"dedup without a picture keeps every sample");}
    }
    {// 1% low of the presents (FramePaceWindow), as RTSS/PresentMon compute it.
        using veyra::engine::FramePaceWindow;
        FramePaceWindow pace;int64_t t=1000000;
        for(int i=0;i<60;++i){pace.complete(t);t+=166667;}
        check(!pace.lowFps(t),"1% low stays unknown until 100 present intervals");
        for(int i=0;i<600;++i){pace.complete(t);t+=166667;}
        const auto steady=pace.lowFps(t);
        check(steady&&std::abs(*steady-60.0)<0.1,"steady 60 Hz presents give a 60 fps 1% low");
        // 1 in 50 presents takes 50 ms: 2% of intervals, so the 99th percentile is one of them.
        FramePaceWindow hitch;t=1000000;
        for(int i=0;i<600;++i){hitch.complete(t);t+=(i%50==49)?500000:166667;}
        const auto low=hitch.lowFps(t);
        check(low&&std::abs(*low-20.0)<0.1,"hitches every 50 frames drag the 1% low to 20 fps");
        // A pause is not a slow frame: the gap is dropped and the run restarts.
        FramePaceWindow paused;t=1000000;
        for(int i=0;i<300;++i){paused.complete(t);t+=166667;}
        t+=30000000;
        for(int i=0;i<300;++i){paused.complete(t);t+=166667;}
        const auto resumed=paused.lowFps(t);
        check(resumed&&std::abs(*resumed-60.0)<0.1,"a three-second pause does not count as a slow frame");
        // Only the last ten seconds count.
        FramePaceWindow old;t=1000000;
        for(int i=0;i<300;++i){old.complete(t);t+=333333;}
        for(int i=0;i<700;++i){old.complete(t);t+=166667;}
        const auto recent=old.lowFps(t);
        check(recent&&std::abs(*recent-60.0)<0.1,"intervals older than ten seconds leave the 1% low");
        // Presents begun exactly on a 120 fps grid whose calls take 0.15-0.55 ms:
        // spaced by return, the call's own jitter sets the 1% low; by start, it does not.
        veyra::engine::FrameCompletionRates byStart(0),byReturn(0);t=1000000;
        veyra::diagnostics::FrameFlowMetrics startMetrics,returnMetrics;
        for(int i=0;i<1200;++i){const int64_t call=1500+(i*7919)%4000;byStart.presented(i%2==0,t+call,t);byReturn.presented(i%2==0,t+call);t+=83333;}
        byStart.snapshot(startMetrics,t);byReturn.snapshot(returnMetrics,t);
        check(startMetrics.presentLow1Fps&&*startMetrics.presentLow1Fps>119.9&&returnMetrics.presentLow1Fps&&*returnMetrics.presentLow1Fps<116,
              "1% low spaces presents by the call start, as RTSS and PresentMon, not by the call's return");
        check(std::abs(startMetrics.presentSubmitFps-returnMetrics.presentSubmitFps)<0.2,"the submit rate counts the same presents either way");
    }
    {// PictureCompare.h: a new picture stops the comparison early; its reference copy can wait.
        using veyra::pipeline::PictureReference;using veyra::pipeline::PicturePlane;using veyra::pipeline::PictureComponents;
        std::vector<uint8_t> a(3840*64),b(3840*64);
        for(size_t i=0;i<a.size();++i){a[i]=uint8_t(40+(i*7919)%160);b[i]=uint8_t(a[i]+20);}
        PicturePlane pa{a.data(),3840,3840,64},pb{b.data(),3840,3840,64};
        PictureReference ref;const auto first=ref.observe(&pa,1,PictureComponents::Bytes,PictureReference::kBand8,true);
        check(first.commit&&!first.repeat,"a deferred new picture asks for its reference copy");
        ref.commit(&pa,1);
        const auto again=ref.observe(&pa,1,PictureComponents::Bytes,PictureReference::kBand8,true);
        check(again.repeat&&again.id==first.id&&!again.commit,"after commit the same picture repeats");
        const auto moved=ref.observe(&pb,1,PictureComponents::Bytes,PictureReference::kBand8,true);
        check(!moved.repeat&&moved.commit&&moved.over>=PictureReference::kDecidedOver&&moved.over<=2*3840&&moved.id!=first.id,
              "a frame changed everywhere is decided after kDecidedOver components, not after the whole picture");
        const auto uncommitted=ref.observe(&pb,1,PictureComponents::Bytes,PictureReference::kBand8,true);
        check(!uncommitted.repeat&&uncommitted.id!=moved.id,"a skipped commit makes the next sample new, never a false repeat");
        ref.commit(&pb,1);
        check(ref.observe(&pb,1,PictureComponents::Bytes).repeat,"the committed picture is the reference again");
        auto sparse=b;for(int k=0;k<200;++k)sparse[size_t(k)*997%sparse.size()]=uint8_t(sparse[size_t(k)*997%sparse.size()]+9);
        PicturePlane ps{sparse.data(),3840,3840,64};
        const auto few=ref.observe(&ps,1,PictureComponents::Bytes);
        check(!few.repeat&&few.over==200,"a small real change below kDecidedOver keeps its exact count");
        // Timing only (no assertion): what a 4K NV12 capture callback spends before it
        // can publish the frame, for a repeat and for a moving new picture.
        std::vector<uint8_t> y0(3840*2160),uv0(3840*1080),y1(y0.size()),uv1(uv0.size());
        for(size_t i=0;i<y0.size();++i){y0[i]=uint8_t(40+(i*7919)%160);y1[i]=uint8_t(y0[i]+9);}
        for(size_t i=0;i<uv0.size();++i){uv0[i]=uint8_t(90+(i*104729)%60);uv1[i]=uint8_t(uv0[i]+5);}
        PicturePlane f0[2]{{y0.data(),3840,3840,2160},{uv0.data(),3840,3840,1080}},f1[2]{{y1.data(),3840,3840,2160},{uv1.data(),3840,3840,1080}};
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        double repeatMs=1e9,deferredMs=1e9,commitMs=1e9,immediateMs=1e9;
        for(int k=0;k<12;++k){
            PictureReference timed;timed.observe(f0,2,PictureComponents::Bytes);
            auto t0=std::chrono::steady_clock::now();timed.observe(f0,2,PictureComponents::Bytes);auto t1=std::chrono::steady_clock::now();
            repeatMs=std::min(repeatMs,ms(t0,t1));
            t0=std::chrono::steady_clock::now();timed.observe(f1,2,PictureComponents::Bytes,PictureReference::kBand8,true);t1=std::chrono::steady_clock::now();
            timed.commit(f1,2);const auto t2=std::chrono::steady_clock::now();
            deferredMs=std::min(deferredMs,ms(t0,t1));commitMs=std::min(commitMs,ms(t1,t2));
            t0=std::chrono::steady_clock::now();timed.observe(f0,2,PictureComponents::Bytes);t1=std::chrono::steady_clock::now();
            immediateMs=std::min(immediateMs,ms(t0,t1));
        }
        std::printf("PICTURE_TIMING 4K NV12 repeatMs=%.3f newBeforePublishMs=%.3f deferredCopyMs=%.3f newImmediateMs=%.3f (best of 12)\n",repeatMs,deferredMs,commitMs,immediateMs);
    }
    return failed?1:0;
}
