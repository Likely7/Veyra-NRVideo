#pragma once
#include "veyra/gfx/ReflexSession.h"
#include <chrono>
#include <vector>
namespace nvidia_latency_test {
using R=veyra::gfx::ReflexSession;
struct Event {uint64_t frame;uint32_t type;uint64_t present;bool async;};
inline std::vector<R::SleepParams> modes;
inline std::vector<Event> events;
inline int sleeps=0;inline bool failSleep=false,failQueue=false,failDisable=false,failNotify=false;
inline int __cdecl set(IUnknown*,R::SleepParams* p){if(!p->low&&failDisable)return -1;modes.push_back(*p);return 0;}
inline int sleepDelayUs=0;
inline int __cdecl sleep(IUnknown*){
    ++sleeps;
    if(sleepDelayUs>0){const auto end=std::chrono::steady_clock::now()+std::chrono::microseconds(sleepDelayUs);while(std::chrono::steady_clock::now()<end){}}
    return failSleep?-1:0;
}
inline int __cdecl marker(IUnknown* target,R::MarkerParams* p){const bool queue=target==reinterpret_cast<IUnknown*>(uintptr_t(2));events.push_back({p->frame,p->type,0,queue});return queue&&failQueue?-1:0;}
inline int __cdecl notify(ID3D12CommandQueue*,uint32_t type){return type==3&&!failNotify?0:-1;}
template<class Check>void run(Check check){
    using namespace veyra::engine;
    PresentationSettings p;check(p.valid()&&p.nvidiaLowLatency==NvidiaLowLatency::Off,"NVIDIA low latency defaults off");
    p.nvidiaLowLatency=NvidiaLowLatency(2);check(!p.valid(),"invalid NVIDIA low latency mode rejected");
    check(savedNvidiaLowLatency(-1)==NvidiaLowLatency::Off&&savedNvidiaLowLatency(99)==NvidiaLowLatency::Off&&savedNvidiaLowLatency(0)==NvidiaLowLatency::Off&&savedNvidiaLowLatency(1)==NvidiaLowLatency::On&&savedNvidiaLowLatency(2)==NvidiaLowLatency::On,"legacy Boost migrates to On; unknown saved modes stay off");
    {
        PresentationSettings kept;kept.nvidiaLowLatency=NvidiaLowLatency::On;
        check(!syncNvidiaLowLatencyQueue(kept,false)&&!kept.enabled&&kept.nvidiaLowLatency==NvidiaLowLatency::On,"another adapter keeps its plain low-queue switch off and only retains the NVIDIA choice");
        check(syncNvidiaLowLatencyQueue(kept,true)&&kept.enabled,"back on NVIDIA the retained On owns the low queue again");
        PresentationSettings plain;plain.enabled=true;
        check(!syncNvidiaLowLatencyQueue(plain,false)&&plain.enabled,"another adapter keeps its plain low queue on");
        check(syncNvidiaLowLatencyQueue(plain,true)&&!plain.enabled,"NVIDIA 关 never leaves a hidden low queue running");
    }
    modes.clear();events.clear();sleeps=0;failSleep=failQueue=failDisable=failNotify=false;
    auto* device=reinterpret_cast<ID3D12Device*>(uintptr_t(1));auto* queue=reinterpret_cast<ID3D12CommandQueue*>(uintptr_t(2));
    R r({set,sleep,marker,notify});
    check(r.begin()==0&&modes.empty()&&events.empty(),"off makes no driver calls");
    check(r.enable(device)&&modes.back().low&&!modes.back().boost&&!modes.back().interval,"on requests native low latency without Boost or frame cap");
    const auto id=r.begin();check(id&&events.size()==1&&events[0].type==0,"input starts with simulation marker only");
    r.renderStart(id);r.renderEnd(id);r.blitStart(id);r.blitEnd(id);r.presentStart(id,false);r.presentEnd(id,false);
    check(events.size()==6&&events[1].type==1&&events[2].type==2&&events[3].type==3&&events[4].type==4&&events[5].type==5,"legacy driver source markers follow actual processing and Present boundaries");
    check(r.enable(device,true,queue)&&modes.back().low&&!modes.back().boost,"FG enables out-of-band queue tracking with the driver Boost field zero");
    const auto next=r.begin();r.renderStart(next);r.renderEnd(next);
    for(unsigned i=0;i<5;++i){r.blitStart(next);r.blitEnd(next);r.presentStart(next,true);r.presentEnd(next,true);}
    r.blitStart(next);r.blitEnd(next);r.presentStart(next,false);r.presentEnd(next,false);
    check(sleeps==2&&r.sleepCount()==1&&r.generatedPresents()==5,"6X presentation sleeps once for the source, never for five generated frames");
    bool valid=true;unsigned generated=0;
    for(const auto& e:events)if(e.async){if(e.present||e.frame!=next)valid=false;if(e.type==11)++generated;}
    check(valid&&generated==5,"out-of-band markers retain source ID and leave driver-internal Present fields zero");
    check(r.disable()&&!r.active()&&!modes.back().low&&!modes.back().boost,"off revokes low latency and Boost together");
    const auto count=events.size();r.presentStart(next,true);r.begin();check(events.size()==count&&sleeps==2,"disabled mode stops periodic Sleep and markers");
    check(r.enable(device,true,queue),"re-enable after off");const auto stale=events.size();r.mark(next,4);check(events.size()==stale,"stale in-flight marker IDs ignored after a mode change");
    failSleep=true;check(r.begin()==0&&!r.active()&&r.failed()&&!r.disablePending(),"Sleep failure disables driver mode and reports fallback");failSleep=false;
    check(r.enable(device,true,queue),"retry native mode after failure");const auto failed=r.begin();failQueue=true;r.blitStart(failed);check(!r.active()&&r.failed(),"out-of-band marker failure stops native mode");failQueue=false;
    check(r.enable(device),"enable before disable failure");failDisable=true;
    check(!r.disable()&&r.disablePending()&&!r.enable(device,true),"failed disable is not reported as off or re-enabled over pending state");
    failDisable=false;check(r.disable()&&!r.disablePending(),"failed disable can be retried cleanly");
    failNotify=true;
    check(r.enable(device,false,queue)&&r.active(),"without frame generation a refused queue notification keeps native Sleep and markers");
    {
        const auto id=r.begin();const auto before=events.size();
        r.renderStart(id);r.renderEnd(id);r.blitStart(id);r.blitEnd(id);r.presentStart(id,false);r.presentEnd(id,false);
        bool inBand=events.size()==before+5;
        for(auto k=before;inBand&&k<events.size();++k)inBand=!events[k].async&&events[k].type==k-before+1;
        check(inBand,"a refused queue notification falls back to in-band submit and Present markers");
    }
    check(!r.enable(device,true,queue)&&!r.active(),"frame generation still requires the out-of-band queue");
    failNotify=false;
    check(std::all_of(modes.begin(),modes.end(),[](const auto& p){return p.boost==0;}),"every enable, disable and failure-recovery NVAPI call leaves Boost off");
    R old({set,sleep,marker,nullptr});check(!old.enable(device,true,queue)&&!old.active(),"FG does not masquerade as enabled on a driver without out-of-band queue notification");
    // A driver Sleep that holds every source past the stall limit (field log
    // 2026-11-12: 26 ms at 59.94 fps) ends low latency; short ones never do.
    check(r.enable(device)&&r.active()&&!r.stalled(),"enable before the sleep stall check");
    for(int i=0;i<40;++i)r.begin(1000000);
    check(r.active()&&!r.stalled(),"sleeps within the stall limit keep low latency on");
    for(int i=0;i<32;++i){sleepDelayUs=i%16<R::kStallSleeps-1?1500:0;r.begin(1000);}
    sleepDelayUs=0;
    check(r.active()&&!r.stalled(),"fewer than ten long sleeps in any sixteen keep low latency on");
    sleepDelayUs=1500;unsigned calls=0;
    while(calls<R::kStallWindow&&r.active()){r.begin(1000);++calls;}
    sleepDelayUs=0;
    check(!r.active()&&r.stalled()&&r.failed()&&!r.disablePending()&&calls<=R::kStallSleeps,"ten sleeps over the limit end low latency and report a stall");
    check(!modes.back().low&&!modes.back().boost,"a stall revokes the driver mode");
    check(r.enable(device)&&r.active()&&!r.stalled()&&!r.failed(),"re-enable clears the stall state");
    sleepDelayUs=1500;for(int i=0;i<20;++i)r.begin();sleepDelayUs=0;
    check(r.active(),"without a stall limit (files) long sleeps are not judged");
}
}
