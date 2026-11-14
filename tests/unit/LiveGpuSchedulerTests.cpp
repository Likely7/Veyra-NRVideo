#include "veyra/engine/LiveGpuScheduler.h"
#include "veyra/engine/TimingWindow.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include "veyra/engine/LivePairLatency.h"
#include "veyra/engine/PreviewFrameReadiness.h"
#include "veyra/engine/PresentationGeometry.h"
#include <iostream>
#include <memory>
#include <atomic>
int main(){
    using Scheduler=veyra::engine::LiveGpuScheduler;using State=Scheduler::State;
    int failures=0;auto check=[&](bool pass,const char* text){std::cout<<(pass?"PASS ":"FAIL ")<<text<<'\n';failures+=!pass;};
    {
        using namespace veyra;
        using R=engine::PreviewFrameReadiness;
        pipeline::BatchFrame generated;generated.kind=pipeline::FrameKind::Generated;generated.validity=pipeline::GenerationValidity::Pending;
        unsigned polls=0;auto pending=[&]{++polls;return false;};
        check(engine::previewFrameReadiness(generated,false,true,pending)==R::Expired&&polls==0,"expired pending generation never waits on GPU readiness");
        check(engine::previewFrameReadiness(generated,true,false,pending)==R::Suppressed&&polls==0,"old history generation never waits on GPU readiness");
        check(engine::previewFrameReadiness(generated,false,false,pending)==R::Pending&&polls==1,"current unexpired generation keeps its producer fence");
        check(engine::previewFrameReadiness(generated,false,false,[&]{generated.validity=pipeline::GenerationValidity::Disabled;return true;})==R::Invalid,"provider-disabled output is never presented");
        pipeline::BatchFrame real;
        check(engine::previewFrameReadiness(real,true,true,pending)==R::Pending&&polls==2,"original never bypasses its producer fence or expires with generated output");
        check(engine::previewFrameReadiness(real,true,true,[]{return true;})==R::Ready,"ready original survives expiration and history suppression");
        const auto fit=engine::presentationRegion(1920,1080,1000,1000,2560,1440,{});
        check(fit.left==0&&fit.right==2560&&fit.top==314&&fit.bottom==1124,"retained wide buffer uses square client letterbox mapping");
        const auto zoom=engine::presentationRegion(1920,1080,1000,1000,2560,1440,{2,.5f,.5f});
        check(zoom.left==0&&zoom.right==2560&&zoom.top==0&&zoom.bottom==1440,"zoom region clips to retained buffer");
        const auto outside=engine::presentationRegion(1920,1080,1000,1000,2560,1440,{1,3,.5f});
        check(outside.left==outside.right,"offscreen image cannot invent an out-of-bounds provider rectangle");
    }
    Scheduler scheduler;unsigned finished=0,secondRan=0,firstSteps=0;bool gpuReady=false;
    const auto owner=std::this_thread::get_id();bool sameOwner=true;
    auto lease=std::make_shared<int>(1);std::weak_ptr<int> weak=lease;
    check(scheduler.push([&,lease](int64_t now){++firstSteps;sameOwner&=std::this_thread::get_id()==owner;if(!gpuReady)return Scheduler::Step{State::Pending,now+2};return Scheduler::Step{now<100?State::Pending:State::Complete,100,true};},[&]{++finished;}),"accept first leased GPU batch");
    lease.reset();scheduler.advance(0);
    check(firstSteps==1&&scheduler.wakeAt()==2&&!weak.expired(),"GPU-pending step yields immediately and retains lease");
    check(!scheduler.preciseWake(),"GPU polling never requests a spin tail");
    check(scheduler.push([&](int64_t){++secondRan;return Scheduler::Step{State::Complete};},[&]{++finished;}),"next batch accepted while current batch awaits GPU");
    check(!scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[]{})&&scheduler.occupancy()==2,"capacity includes both current and next batch");
    gpuReady=true;scheduler.advance(20);
    check(scheduler.wakeAt()==100&&secondRan==0,"deadline wait yields without presenting early or reordering batches");
    check(scheduler.preciseWake(),"ready frame deadline requests precision without moving its timestamp");
    unsigned submission=0;++submission;
    check(submission==1&&finished==0,"owner remains available to submit work during future presentation deadline");
    scheduler.advance(100);
    check(finished==2&&secondRan==1&&weak.expired()&&scheduler.occupancy()==0&&sameOwner,"due work finishes on owner and releases leases exactly once");
    check(!scheduler.preciseWake(),"finished queue does not leave a precision wake armed");
    scheduler.advance(200);check(finished==2,"completed jobs cannot execute or finalize twice");
    unsigned cancelled=0;
    scheduler.push([](int64_t){return Scheduler::Step{State::Pending,1000,true};},[&]{++cancelled;});
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++cancelled;});
    scheduler.advance(300);scheduler.cancel();scheduler.cancel();
    check(cancelled==2&&scheduler.occupancy()==0&&scheduler.wakeAt()==0,"cancellation finalizes pending and queued batches exactly once without waiting");
    check(!scheduler.preciseWake(),"cancellation clears a pending precision wake");
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++finished;});scheduler.advance(400);
    check(finished==3,"new work accepted after cancellation");
    std::atomic<bool> rejected=false;std::thread foreign([&]{try{scheduler.advance(500);}catch(const std::logic_error&){rejected=true;}});foreign.join();
    check(rejected,"foreign thread cannot advance GPU owner state");
    unsigned failedFinished=0;
    scheduler.push([](int64_t){return Scheduler::Step{State::Failed};},[&]{++failedFinished;});
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++failedFinished;});scheduler.advance(600);
    check(scheduler.failed()&&failedFinished==2&&scheduler.occupancy()==0,"failure propagates and finalizes queued work");
    check(!scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[]{}),"failed scheduler rejects new work");
    veyra::engine::TimingWindow timing;for(unsigned i=0;i<100;++i)timing.add(i);check(timing.p95()==94,"percentile order statistic");timing.clear();timing.add(6);check(timing.p95()==6,"reset removes old timing window");
    veyra::engine::FgRecoveryBudget budget;
    {
        veyra::engine::FgRecoveryBudget prefix;
        prefix.complete(18,true,false,10000000,10);
        check(!prefix.admitFile(10000000,10070000,10250000,27778,0,0,3,false,0),"late first subframe rejects even when the last output fits");
        check(prefix.admitFile(10000000,10100000,10250000,27778,0,0,3,false,0),"first deadline uses prefix cost rather than full MFG group");
        check(!prefix.admitFile(10000000,10100000,10140000,27778,0,0,3,false,0),"last subframe also has to fit when later FG work is slower");
        check(!prefix.admitFile(10000000,10100000,10250000,27778,0,0,3,false,5),"pending previous batch cannot be mistaken for free first-frame headroom");
        check(!prefix.admitFile(10000000,10000000,10250000,27778,100,0,3,false,0),"CPU delay cannot erase future first interpolation work");
        prefix.complete(4,true,true,10000000);
        check(prefix.admitFile(10000000,9900000,10010000,27778,0,0,3,true,0),"history seed has no generated prefix to present");
        check(!prefix.admit(10000000,9900000,0,0),"reject overload before seed budget check");
        check(prefix.canAdmit(10000000,10010000,0,0,true),"cheap seed fits when complete group cannot");
        check(!prefix.canAdmit(10000000,9900000,0,0,true),"expired seed deadline still rejects");
        check(prefix.admit(10000000,10200000,0,0)&&prefix.recovering(),"first admitted group begins recovery");
        check(prefix.canAdmit(10000000,10010000,0,0,true)&&prefix.recovering(),"seed affordability query cannot falsely complete recovery");
    }
    {
        // Reduced (2X) group: base 8 ms + one measured first interpolation
        // 3 ms must reach the pair midpoint; the full group (base + 10) need not.
        veyra::engine::FgRecoveryBudget reduced;
        reduced.complete(18,true,false,10000000,10);
        check(!reduced.admitFile(10000000,10100000,10150000,27778,0,0,3,false,0),"full group over budget for the last output");
        check(reduced.canAdmitReduced(10000000,10150000,83333,0,0,3,0),"midpoint reachable with base plus one interpolation");
        check(!reduced.canAdmitReduced(10000000,10020000,83333,0,0,3,0),"midpoint too early rejects the reduced group even with one-output grace");
        check(!reduced.canAdmitReduced(10000000,10150000,83333,0,0,std::nullopt,0),"unmeasured first interpolation cannot admit a reduced group");
        check(!reduced.canAdmitReduced(10000000,10150000,83333,0,0,3,15),"queued earlier GPU work consumes the reduced deadline");
        check(!reduced.recovering(),"reduced affordability query does not touch recovery state");
    }
    {
        veyra::engine::FgRecoveryBudget queued;
        queued.complete(10,true,false,10000000,4);
        check(queued.admit(10000000,10020000,0,0),"idle GPU can fit the measured batch");
        check(!queued.admit(10000000,10020000,0,0,false,5),"uncompleted earlier work consumes the new pair deadline");
        check(queued.admit(10000000,10020000,5,0,false,5),"elapsed queue work is counted only once");
        check(!queued.admit(10000000,9920000,100,0,false,5),"CPU time cannot erase unsubmitted FG even with queued work");
        check(queued.admit(10000000,10020000,0,0,false,-5),"negative queue prediction cannot invent extra GPU work");
    }
    {
        using Window=veyra::engine::TimingWindow;
        const Window::Clock::time_point start{};
        Window present{std::chrono::seconds(1)};
        present.add(80,start);
        check(present.p95(start)==80,"temporary GPU blit stall is visible to admission");
        check(!budget.admit(10000000,10200000,0,present.p95(start)),"GPU blit stall limits FG admission");
        check(present.p95(start+std::chrono::seconds(1))==0,"Present stall expires without successful FG or new samples");
        present.add(2,start+std::chrono::seconds(1));
        check(budget.admit(20000000,20200000,0,present.p95(start+std::chrono::seconds(1))),"admission recovers after expired Present stall");
        present.clear();check(present.p95(start+std::chrono::seconds(1))==0,"new metrics window clears admission samples");
        budget.reset();
    }
    budget.fgCost(3,10000000);budget.complete(31,true,false,10000000);
    check(budget.processingAllowance(10000000,166667)==166667,"live processing allowance is bounded to one source interval");
    check(budget.predicted(10000000)&&std::abs(*budget.predicted(10000000)-31)<.001,"base and FG cost do not double count additional generation");
    check(!budget.admit(10000000,10100000,0,0),"insufficient pair deadline enters limited state");
    check(budget.admit(10100000,10600000,0,0)&&budget.recovering(),"fresh deadline admits warmup without a fixed cooldown");
    check(budget.admit(10266667,10766667,0,0)&&!budget.recovering(),"second consecutive source pair exits recovery");
    budget.complete(200,true,true,13000000,180);
    check(*budget.predicted(13000000)<32,"expensive reset warmup does not poison steady cost");
    budget.complete(std::nullopt,true,true,13000001,190);
    check(*budget.predicted(13000001)<32,"warmup FG timestamp without graph timestamp cannot poison steady cost");
    budget.complete(10,false,false,24000000);
    check(budget.processingAllowance(24000000,166667)==100000,"live deadline includes measured enhancement before interpolation cadence");
    check(*budget.predicted(24000000)<14,"old slow completion expires even when no FG succeeds");
    check(!budget.admit(24000000,23800000,0,0),"recovery never admits an already expired deadline");
    budget.reset();check(!budget.predicted(25000000),"settings revision reset clears prior backend costs");
    check(budget.processingAllowance(25000000,166667)==0,"unknown GPU work cannot invent a processing allowance");
    budget.fgCost(20,25000000);budget.complete(12,true,false,25000000,3);
    check(budget.predicted(25000000)==29,"subtract same-frame FG time instead of another frame's higher percentile");
    check(!budget.admit(25000000,25050000,100,0),"long CPU stall cannot discount FG that has not been submitted");
    budget.complete(std::nullopt,true,false,36000000);
    check(!budget.predicted(36000000),"missing GPU timing cannot retain a stale CPU polling cost");
    check(!budget.admit(36000000,35800000,0,0),"unknown cost still rejects expired presentation deadlines");
    for(unsigned multiplier:{2u,4u,6u}){
        budget.reset();const double fg=double(multiplier-1)*1.5;
        budget.complete(5+fg,true,false,40000000,fg);
        check(budget.predicted(40000000)==5+fg,"completion samples steady FG with its matching graph identity");
        for(int64_t t=40100000;t<42100000;t+=166667){
            budget.complete(200,true,true,t,190);
            check(budget.admit(t,t+166667,0,1),"repeated reset cost cannot trap healthy 2X/4X/6X pairs in rejection");
            budget.complete(5+fg,true,false,t+1,fg);
        }
        budget.complete(100,true,false,42200000,90);
        check(!budget.admit(42200000,42366667,0,1),"real steady GPU overload remains limited");
    }
    budget.reset();
    budget.complete(20.719,true,false,50000000,11);
    budget.complete(7,true,true,50000001,2);
    check(!budget.admit(50000002,50084552,1.217,.047),"observed 6X deadline rejects the full steady batch");
    check(budget.admit(50000002,50084552,1.217,.047,true),"measured single-call recovery fits without discounting steady MFG cost");
    check(budget.predicted(50000002)==20.719&&budget.predicted(50000002,true)==7,"recovery and steady costs remain independent");
    budget.complete(100,false,true,50000003);
    check(budget.predicted(50000003,true)==7,"rejected reset without FG cannot become a warmup timing sample");
    check(!budget.admit(50000002,49800002,1.217,.047,true),"cheap recovery cannot bypass an expired deadline");
    check(budget.predicted(60000002,true)==budget.predicted(60000002),"expired recovery measurement falls back to steady estimate");
    {
        using veyra::engine::FgCostSettling;
        FgCostSettling settling;const int64_t pair=333667;int64_t t=100000000;
        check(!settling.observe(false,true,t),"the first evaluation is a steady sample");
        check(!settling.observe(false,true,t+=pair),"continuous evaluations are steady samples");
        check(!settling.observe(true,false,t+=pair)&&!settling.observe(false,true,t+=pair),"one skipped pair hides nothing");
        check(!settling.observe(false,false,t+=pair),"a group without FG is no sample of FG");
        t+=FgCostSettling::kIdle100ns;
        check(settling.observe(false,true,t)&&settling.observe(false,true,t+=pair)&&!settling.observe(false,true,t+=pair),"after a second without an evaluation the next two are warmup");
        check(!settling.holding(t),"a probe holds nothing until it fails");
        const auto failProbe=[&]{
            for(int i=0;i<60&&settling.holding(t);++i)t+=pair;
            t+=FgCostSettling::kIdle100ns;
            settling.observe(false,true,t);settling.observe(false,true,t+=pair);settling.observe(false,true,t+=pair);
            settling.observe(true,false,t+=pair);
            return settling.holdUntil()-t;
        };
        check(settling.observe(true,false,t+=pair)==false&&settling.holdUntil()-t==20000000,"a probe rejected right after settling retries after 2 s");
        check(failProbe()==40000000&&failProbe()==80000000&&failProbe()==80000000,"further failed probes retry after 4 s, then every 8 s");
        check(settling.holding(t)&&!settling.holding(t+80000000),"the hold ends on time");
        t+=80000000+FgCostSettling::kIdle100ns;
        settling.observe(false,true,t);settling.observe(false,true,t+=pair);
        for(unsigned i=0;i<FgCostSettling::kProbeGroups;++i)check(!settling.observe(false,true,t+=pair),"evaluations after settling are steady samples");
        settling.observe(true,false,t+=pair);
        check(!settling.holding(t),"a probe that ran on clears the failures: a later skip holds nothing");
        t+=FgCostSettling::kIdle100ns;
        settling.observe(false,true,t);settling.observe(false,true,t+=pair);settling.observe(false,true,t+=pair);settling.observe(true,false,t+=pair);
        check(settling.holdUntil()-t==20000000,"the next failed probe starts over at 2 s");
        settling.reset();check(!settling.holding(t)&&!settling.observe(false,true,t+FgCostSettling::kIdle100ns),"settings revision reset clears everything");
        // Field log 2026-10-31, 4K30 VFG High on an RTX 5060 Ti: base 3.4 ms with FG and
        // 1.45 ms without, 23.1 ms running continuously, 39.6 ms (fullscreen) or 28.5 ms
        // (window) after 2 s of skipping. One second of FG at 33 ms starts it, as a
        // hiccup did. afterSkip is the cost after one skipped pair; the log has no such
        // sample, so it is varied. Returns evaluated pairs of the last 20 s of 60 s.
        const auto replay=[](double continuous,double isolated,double afterSkip,bool settle,unsigned& maxPer2s){
            veyra::engine::FgRecoveryBudget replayBudget;FgCostSettling cost;
            const int64_t interval=333667;
            bool previous=true;int64_t lastEvaluated=100000000;unsigned evaluated=0,window=0;maxPer2s=0;
            for(unsigned n=0;n<1800;++n){
                const int64_t now=100000000+n*interval;
                const auto allowance=replayBudget.processingAllowance(now,interval);
                const bool admitted=(!settle||!cost.holding(now))&&replayBudget.admit(now,now+interval/2+allowance-5000,.2,.45);
                const double fg=n<30?33:now-lastEvaluated>=10000000?isolated:previous?continuous:afterSkip;
                const int64_t done=now+int64_t((admitted?3.4+fg:1.45)*10000);
                const bool warmup=settle&&cost.observe(!admitted,admitted,done);
                if(admitted){replayBudget.complete(3.4+fg,true,warmup,done,fg);lastEvaluated=done;}
                else replayBudget.complete(1.45,false,false,done);
                previous=admitted;
                if(n>=1200){evaluated+=admitted;window+=admitted;if(n%60==59){maxPer2s=std::max(maxPer2s,window);window=0;}}
            }
            return evaluated;
        };
        unsigned per2s=0;
        const auto trapped=replay(23.1,39.6,23.1,false,per2s);
        printf("VFG trap replay: earlier rule %u of 600 pairs evaluated, most in 2 s %u\n",trapped,per2s);
        check(trapped<=20,"replay: counting the lone probe keeps VFG at 30 fps");
        const auto recoveredFull=replay(23.1,39.6,23.1,true,per2s);
        printf("VFG trap replay: fullscreen %u of 600\n",recoveredFull);
        check(recoveredFull==600,"replay: fullscreen probes at 39.6 ms recover full FG");
        check(replay(23.1,28.5,23.1,true,per2s)==600,"replay: windowed probes at 28.5 ms recover full FG");
        check(replay(23.1,39.6,39.6,true,per2s)==600,"replay: also if one skipped pair cost as much as a long skip");
        const auto overload=replay(46,46,46,true,per2s);
        printf("VFG trap replay: really too slow %u of 600, most in 2 s %u\n",overload,per2s);
        check(overload<=9&&per2s<=3,"replay: a mode that really is too slow settles at three groups per 8 s");
        unsigned before2s=0;
        check(replay(46,46,46,true,per2s)<=replay(46,46,46,false,before2s),"replay: a mode too slow evaluates no more often than the lone probe did");
        check(replay(29,29,29,true,per2s)<=9,"replay: 29 ms misses the 10 ms bound and stays rejected");
        check(replay(29,46,46,true,per2s)<=9,"replay: so does 29 ms with a slower first evaluation");
        check(replay(25.5,25.5,25.5,true,per2s)==600,"replay: 25.5 ms fits the 10 ms bound and runs on");
    }
    veyra::engine::LivePairLatency phase;
    check(phase.select(10000000,420000,333333,4)==420000,"unknown live readiness keeps legacy phase");
    for(int i=0;i<8;++i)phase.observe(10000000+i,370000);
    check(phase.select(10000008,420000,333333,4)==415000,"phase advance is gradual rather than a burst");
    for(int i=0;i<32;++i)phase.select(10000100+i,420000,333333,4);
    check(phase.select(10000200,420000,333333,4)==375000,"4X keeps measured readiness plus jitter margin");
    phase.observe(10000201,415000); // legacy 420000 minus the 0.5 ms margin
    check(phase.select(10000202,420000,333333,4)==420000,"slow batch immediately restores conservative phase");
    check(phase.select(21000000,420000,333333,4)==420000,"stale readiness cannot shorten a new pair");
    for(unsigned multiplier:{2u,4u,6u}){
        phase.reset();
        const int64_t interval=333667,ready=120000;
        const int64_t needed=ready+interval*(multiplier-1)/multiplier;
        for(int i=0;i<8;++i)phase.observe(30000000+i,needed);
        auto last=interval+90000;bool bounded=true;
        for(int i=0;i<80;++i){const auto delay=phase.select(30000100+i,interval+90000,interval,multiplier);
            bounded&=delay>=needed&&delay<=interval+90000&&last-delay<=5000;last=delay;}
        check(bounded,"2X/4X/6X preserves readiness and bounded phase at 29.97 Hz");
        phase.reset();check(phase.select(30001000,interval+90000,interval,multiplier)==interval+90000,"reset discards earlier readiness");
    }
    {// LiveDeadlineMargin: automatic extra (0-1 ms) so 99 of 100 generated frames are on time.
        using veyra::engine::LiveDeadlineMargin;
        // Base slack samples (deadline without extra minus readiness); one generated frame per 1/60 s.
        const auto feed=[](LiveDeadlineMargin& m,int64_t& now,int count,auto slackOf){
            for(int i=0;i<count;++i){now+=166667;const int64_t extra=m.extra(),deadline=now+extra;m.observe(now,deadline,deadline-extra-slackOf(i),extra);}
        };
        LiveDeadlineMargin easy;int64_t now=100000000;
        check(easy.extra()==LiveDeadlineMargin::kStart100ns,"the margin starts at 0.25 ms");
        feed(easy,now,1200,[](int){return int64_t(20000);});
        check(easy.extra()==0,"generated frames 2 ms early give the whole margin back");
        LiveDeadlineMargin late;now=100000000;
        // One frame in twenty is 0.3 ms late without extra, the rest 0.4 ms early.
        feed(late,now,600,[](int i){return i%20==7?int64_t(-3000):int64_t(4000);});
        const auto settled=late.extra();
        check(settled>=3000&&settled<=3000+LiveDeadlineMargin::kGuard100ns,"5% of frames 0.3 ms late: the margin covers them and no more");
        LiveDeadlineMargin rare;now=100000000;
        feed(rare,now,600,[](int i){return i%240==9?int64_t(-8000):int64_t(4000);});
        check(rare.extra()==0,"one late frame in 240 does not buy margin for all the others");
        LiveDeadlineMargin overload;now=100000000;
        feed(overload,now,600,[](int){return int64_t(-50000);});
        check(overload.extra()==LiveDeadlineMargin::kMax100ns,"a GPU that cannot keep up never costs more than 1 ms");
        LiveDeadlineMargin warm;now=100000000;
        feed(warm,now,110,[](int){return int64_t(-20000);});
        check(warm.extra()==LiveDeadlineMargin::kStart100ns,"the first 2 s (feature warmup) learn nothing");
        LiveDeadlineMargin step;now=100000000;
        feed(step,now,300,[](int){return int64_t(4000);});
        const auto before=step.extra();feed(step,now,15,[](int){return int64_t(-9000);});
        check(step.extra()-before<=2*LiveDeadlineMargin::kRise100ns,"the margin rises in steps, no sudden jump of the deadline");
        step.reset();check(step.extra()==LiveDeadlineMargin::kStart100ns,"reset returns to the starting margin");
    }
    phase.observe(40000000,-1);phase.observe(40000000,30000000);
    check(phase.select(40000001,420000,333333,4)==420000,"invalid observations cannot train phase");
    {// FgRecoveryBudget::phaseAllowance: room for the generated frames' own interpolation (2026-11-13).
        veyra::engine::FgRecoveryBudget fit;
        check(fit.phaseAllowance(50000000,333333)==0,"no measured cost, no phase allowance");
        fit.fgCost(16,50000000);fit.complete(21.5,true,false,50000000,16);
        check(fit.phaseAllowance(50000000,333333)==160000,"base + FG within one interval: the phase may cover the FG cost");
        check(fit.phaseAllowance(50000000,166667)==0,"the same cost at 60 fps does not fit and keeps the legacy bound");
        veyra::engine::FgRecoveryBudget over;
        over.fgCost(32,50000000);over.complete(37,true,false,50000000,32);
        check(over.phaseAllowance(50000000,333333)==0,"a group the GPU cannot sustain earns no later deadlines");
        veyra::engine::LivePairLatency heavy;
        const int64_t legacy=388000,interval=333333,needed=406000;
        for(int i=0;i<16;++i)heavy.observe(60000000+i,needed);
        check(heavy.select(60000020,legacy,interval,2)==legacy,"without the allowance the phase stops at the legacy bound");
        check(heavy.select(60000021,legacy,interval,2,fit.phaseAllowance(50000000,333333))==needed+veyra::engine::LivePairLatency::kMargin100ns,
              "with the FG allowance the phase covers the generated frames' readiness");
    }
    {// LiveFgRejectionHold: real frames without the FG phase after a second of rejections (2026-11-13).
        using veyra::engine::LiveFgRejectionHold;
        LiveFgRejectionHold hold;int64_t now=70000000;const int64_t pair=333333;
        bool any=false;
        for(int i=0;i<29;++i){any|=hold.observe(now,false);now+=pair;}
        check(!any&&!hold.held(),"under a second of rejections keeps the FG phase");
        bool held=false;for(int i=0;i<3;++i){held=hold.observe(now,false);now+=pair;}
        check(held&&hold.held(),"a second of rejections shows real frames without the FG phase");
        check(hold.observe(now,true)&&hold.held(),"a lone admitted probe stays held, its generated frames expire");now+=pair;
        check(hold.observe(now,false),"rejections after the probe are held at once");now+=pair;
        using veyra::engine::FgCostSettling;
        check(LiveFgRejectionHold::kResumePairs>FgCostSettling::kEvaluations+FgCostSettling::kProbeGroups,
              "resuming needs more admitted pairs in a row than one budget probe runs");
        bool probeHeld=true;
        for(unsigned i=0;i<FgCostSettling::kEvaluations+FgCostSettling::kProbeGroups;++i){probeHeld&=hold.observe(now,true);now+=pair;}
        probeHeld&=hold.observe(now,false);now+=pair;
        check(probeHeld&&hold.held(),"a whole budget probe (2 warmup + 4 measured pairs) stays held");
        bool resumed=false;
        for(unsigned i=0;i<LiveFgRejectionHold::kResumePairs;++i){resumed=!hold.observe(now,true);now+=pair;}
        check(resumed&&!hold.held(),"a few admitted pairs in a row resume FG on its phase");
        bool partial=false;for(int i=0;i<120;++i){partial|=hold.observe(now,i%3==2);now+=pair;}
        check(!partial,"FG admitted every third pair is never held");
        for(int i=0;i<40;++i){hold.observe(now,false);now+=pair;}
        hold.reset();check(!hold.held()&&!hold.observe(now,false),"reset clears the hold and the rejection streak");
    }
    return failures?1:0;
}
