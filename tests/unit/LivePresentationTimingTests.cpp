#define NOMINMAX
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/LivePresentationResetPolicy.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/engine/CaptureHalfRate.h"
#include "veyra/engine/ContentDedup.h"
#include "veyra/pipeline/PictureCompare.h"
#include <algorithm>
#include <array>
#include <vector>
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
        const int n=1200;
        {const auto r=run(60.0,[](int i){return int64_t(i/2);},n,0,0,333333);
         check(r.kept==n/2&&r.keptRepeats==0&&r.droppedReal==0,"dedup 30-in-60: every real picture kept once, every repeat dropped");
         check(r.monotonic&&r.neverLate&&r.regularSpans==r.spans,"dedup 30-in-60: content spans exactly 33.3 ms, never later than the sample");}
        {const auto r=run(58.0,[](int i){return int64_t(std::floor(i*30.0/58.0+1e-9));},n,0,0,333333);
         check(r.keptRepeats==0&&r.droppedReal==0&&std::abs(r.kept-int(std::floor((n-1)*30.0/58.0))-1)<=1,"dedup 30-in-58: all real pictures kept, no repeats");
         check(r.monotonic&&r.neverLate&&r.regularSpans*100>=r.spans*95,"dedup 30-in-58: uneven 34.5/17.2 ms samples become even 33.3 ms content spans");}
        {const auto r=run(60.0,[](int i){return int64_t(std::floor(i*40.0/60.0+1e-9));},n,0,0,250000);
         check(r.keptRepeats==0&&r.droppedReal==0&&std::abs(r.kept-n*2/3)<=1,"dedup 40-in-60: all real pictures kept, no repeats");
         check(r.monotonic&&r.neverLate&&r.regularSpans*100>=r.spans*95,"dedup 40-in-60: alternating 33/17 ms samples become even 25 ms content spans");}
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
    return failed?1:0;
}
