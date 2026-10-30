#pragma once
#include "veyra/gfx/ReflexSession.h"
#include <vector>
namespace nvidia_latency_test {
using R=veyra::gfx::ReflexSession;
struct Event {uint64_t frame;uint32_t type;uint64_t present;bool async;};
inline std::vector<R::SleepParams> modes;
inline std::vector<Event> events;
inline int sleeps=0;inline bool failSleep=false,failQueue=false,failDisable=false;
inline int __cdecl set(IUnknown*,R::SleepParams* p){if(!p->low&&failDisable)return -1;modes.push_back(*p);return 0;}
inline int __cdecl sleep(IUnknown*){++sleeps;return failSleep?-1:0;}
inline int __cdecl marker(IUnknown* target,R::MarkerParams* p){const bool queue=target==reinterpret_cast<IUnknown*>(uintptr_t(2));events.push_back({p->frame,p->type,0,queue});return queue&&failQueue?-1:0;}
inline int __cdecl notify(ID3D12CommandQueue*,uint32_t type){return type==3?0:-1;}
template<class Check>void run(Check check){
    using namespace veyra::engine;
    PresentationSettings p;check(p.valid()&&p.nvidiaLowLatency==NvidiaLowLatency::Off,"NVIDIA low latency defaults off");
    p.nvidiaLowLatency=NvidiaLowLatency(3);check(!p.valid(),"invalid NVIDIA low latency mode rejected");
    check(savedNvidiaLowLatency(-1)==NvidiaLowLatency::Off&&savedNvidiaLowLatency(99)==NvidiaLowLatency::Off&&savedNvidiaLowLatency(2)==NvidiaLowLatency::Boost,"invalid saved mode falls back to off rather than accidentally enabling Boost");
    modes.clear();events.clear();sleeps=0;failSleep=failQueue=failDisable=false;
    auto* device=reinterpret_cast<ID3D12Device*>(uintptr_t(1));auto* queue=reinterpret_cast<ID3D12CommandQueue*>(uintptr_t(2));
    R r({set,sleep,marker,notify});
    check(r.begin()==0&&modes.empty()&&events.empty(),"off makes no driver calls");
    check(r.enable(device)&&modes.back().low&&!modes.back().boost&&!modes.back().interval,"on requests native low latency without Boost or frame cap");
    const auto id=r.begin();check(id&&events.size()==1&&events[0].type==0,"input starts with simulation marker only");
    r.renderStart(id);r.renderEnd(id);r.blitStart(id);r.blitEnd(id);r.presentStart(id,false);r.presentEnd(id,false);
    check(events.size()==6&&events[1].type==1&&events[2].type==2&&events[3].type==3&&events[4].type==4&&events[5].type==5,"legacy driver source markers follow actual processing and Present boundaries");
    check(r.enable(device,true,true,queue)&&modes.back().boost&&r.boost(),"Boost plus FG enables native Boost and out-of-band queue tracking");
    const auto next=r.begin();r.renderStart(next);r.renderEnd(next);
    for(unsigned i=0;i<5;++i){r.blitStart(next);r.blitEnd(next);r.presentStart(next,true);r.presentEnd(next,true);}
    r.blitStart(next);r.blitEnd(next);r.presentStart(next,false);r.presentEnd(next,false);
    check(sleeps==2&&r.sleepCount()==1&&r.generatedPresents()==5,"6X presentation sleeps once for the source, never for five generated frames");
    bool valid=true;unsigned generated=0;
    for(const auto& e:events)if(e.async){if(e.present||e.frame!=next)valid=false;if(e.type==11)++generated;}
    check(valid&&generated==5,"out-of-band markers retain source ID and leave driver-internal Present fields zero");
    check(r.disable()&&!r.active()&&!modes.back().low&&!modes.back().boost,"off revokes low latency and Boost together");
    const auto count=events.size();r.presentStart(next,true);r.begin();check(events.size()==count&&sleeps==2,"disabled mode stops periodic Sleep and markers");
    check(r.enable(device,false,true,queue),"re-enable after off");const auto stale=events.size();r.mark(next,4);check(events.size()==stale,"stale in-flight marker IDs ignored after a mode change");
    failSleep=true;check(r.begin()==0&&!r.active()&&r.failed()&&!r.disablePending(),"Sleep failure disables driver mode and reports fallback");failSleep=false;
    check(r.enable(device,true,true,queue),"retry native mode after failure");const auto failed=r.begin();failQueue=true;r.blitStart(failed);check(!r.active()&&r.failed(),"out-of-band marker failure stops native mode");failQueue=false;
    check(r.enable(device),"enable before disable failure");failDisable=true;
    check(!r.disable()&&r.disablePending()&&!r.enable(device,true),"failed disable is not reported as off or re-enabled over pending state");
    failDisable=false;check(r.disable()&&!r.disablePending(),"failed disable can be retried cleanly");
    R old({set,sleep,marker,nullptr});check(!old.enable(device,false,true,queue)&&!old.active(),"FG does not masquerade as enabled on a driver without out-of-band queue notification");
}
}
