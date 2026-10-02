#define NOMINMAX
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/LivePresentationResetPolicy.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/engine/CaptureHalfRate.h"
#include "veyra/engine/FrameRateWindow.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/PresentationSettings.h"
#include <cmath>
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
    check(!CaptureHalfRate::supported(30)&&!CaptureHalfRate::supported(18)&&!CaptureHalfRate::supported(120)&&CaptureHalfRate::supported(59.94),"60-to-30 setting does not halve other transport modes");
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
    return failed?1:0;
}
