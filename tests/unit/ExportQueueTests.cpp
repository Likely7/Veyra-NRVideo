#include "veyra/engine/ExportQueue.h"
#include <iostream>
#include <stdexcept>
using namespace veyra::engine;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    try{
        ExportJobManager worker;ExportQueue q(worker);
        std::vector<uint64_t> ids;
        for(int i=0;i<20;++i)ids.push_back(q.add(L"missing-test-file.mkv"));
        check(!q.busy()&&q.readyCount()==20&&worker.poll().workerPid==0,"adding launched a worker");
        check(q.move(ids[19],0)&&q.items()[0].id==ids[19],"reorder failed");
        check(q.setTrim(ids[0],1,2)&&q.setTrim(ids[1],3,4),"trim edit failed");
        check(q.find(ids[0])->start==1&&q.find(ids[1])->start==3,"trim leaked between files");
        check(!q.setTrim(ids[0],4,1),"invalid trim accepted");
        ExportTrackSelection selected;selected.policy=ExportTrackPolicy::Selected;selected.count=1;selected.indices[0]=2;
        check(q.setTracks(ids[0],true,selected),"track selection failed");
        check(q.find(ids[1])->media.audio.policy==ExportTrackPolicy::All,"track choice leaked between files");
        q.setContainer(ExportContainer::Matroska);
        EnhancementSettings settings;settings.nr=settings.sr=false;settings.multiplier=1;
        std::wstring error;check(q.start(argv[1],settings,false,veyra::sink::ExportRateControl::Cq,error),"batch setup failed");
        const auto batch=q.batchId();auto extra=q.add(L"next-batch.mp4");
        check(q.find(extra)->batch!=batch,"new item joined active batch");
        check(!q.setTrim(ids[0],0,0),"frozen trim changed");
        check(q.move(ids[1],0)&&q.items()[0].id==ids[1],"waiting reorder failed");
        q.cancel();check(!q.busy()&&q.readyCount()==21&&q.successEvent()==0,"cancel lost pending items or reported success");
        check(q.remove(ids[0])&&q.items().size()==20,"remove failed");
        q.clearWaiting();check(q.items().empty(),"clear failed");
        check(!q.start(argv[1],settings,false,veyra::sink::ExportRateControl::Cq,error),"empty batch accepted");
        std::cout<<"PASS: 20-item queue, stable IDs, ordering, frozen settings, per-file tracks/trim, cancel, clear\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
