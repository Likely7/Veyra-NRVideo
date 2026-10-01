#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/ExportQueue.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace veyra::engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
ExportJobSnapshot finish(ExportJobManager& manager){
    const auto limit=GetTickCount64()+90000;
    ExportJobSnapshot s;
    do{s=manager.poll();if(!s.active())return s;Sleep(10);}while(GetTickCount64()<limit);
    throw std::runtime_error("export exceeded 90 second deadline");
}
void collected(const ExportJobSnapshot& s){
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,s.workerPid);
    if(process){const auto state=WaitForSingleObject(process,0);CloseHandle(process);require(state==WAIT_OBJECT_0,"terminal published before process exit");}
}
}
int wmain(int argc,wchar_t** argv){
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(argc==3&&std::wstring_view(argv[1])==L"--export-worker"){
        const int result=runExportWorker(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
        if(SUCCEEDED(com))CoUninitialize();return result;
    }
    if(argc<4)return 2;
    bool passed=false;
    try{
        const std::wstring mode=argv[1],input=argv[2],output=argv[3];
        ExportJobManager manager;EnhancementSettings settings;settings.nr=settings.sr=false;settings.multiplier=1;
        ExportMediaOptions media;
        media.container=mode.find(L"mkv")!=std::wstring::npos?ExportContainer::Matroska:ExportContainer::Mp4;
        media.audio.policy=ExportTrackPolicy::All;media.subtitles.policy=ExportTrackPolicy::All;
        const bool hevc=mode.find(L"hevc")!=std::wstring::npos;
        if(mode==L"queue"){
            std::filesystem::create_directories(output);
            ExportQueue queue(manager);
            const auto a=queue.add(input),b=queue.add(input),c=queue.add(input);
            require(queue.move(c,0),"queue reorder refused");
            require(!queue.busy()&&manager.poll().workerPid==0,"adding implicitly exported");
            std::wstring error;require(queue.start(output,settings,false,veyra::sink::ExportRateControl::Cq,error),"batch refused");
            const auto late=queue.add(input);
            std::vector<uint64_t> order;
            uint64_t job=0;
            const auto deadline=GetTickCount64()+90000;
            while(queue.needsTick()&&GetTickCount64()<deadline){
                queue.tick();const auto s=queue.snapshot();
                if(s.jobId&&s.jobId!=job){job=s.jobId;for(const auto& item:queue.items())if(item.job==job){order.push_back(item.id);require(!queue.remove(item.id),"active row removed");break;}}
                Sleep(10);
            }
            require(!queue.busy(),"batch did not finish");
            require(order==std::vector<uint64_t>{c,a,b},"actual worker launch order differs from reordered model");
            require(queue.find(late)->state==ExportItemState::Ready,"late item joined frozen batch");
            require(queue.successes()==3&&queue.failures()==0&&queue.successEvent()==1,"whole-batch success event wrong");
            for(int i=0;i<10;++i)queue.tick();require(queue.successEvent()==1,"duplicate completion event");
            const auto missing=queue.add(input+L".missing");
            require(queue.start(output,settings,false,veyra::sink::ExportRateControl::Cq,error),"mixed batch refused");
            while(queue.needsTick()&&GetTickCount64()<deadline){queue.tick();Sleep(10);}
            require(queue.find(missing)->state==ExportItemState::Failed,"missing file not reported");
            require(queue.successes()==1&&queue.failures()==1&&queue.successEvent()==1,"mixed batch emitted successful batch event");
            std::wcout<<L"PASS actual order C/A/B, late membership, stable job IDs, mixed failure, completion de-duplication\n";
        }else if(mode==L"lifecycle"){
            // A pre-existing user/legacy partial must survive every cancellation.
            {std::ofstream legacy(std::filesystem::path(output+L".partial"));legacy<<"protected legacy partial";}
            SetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_EXIT_DELAY_MS",L"1200");
            uint64_t lastJob=0;
            for(int round=0;round<3;++round){
                require(manager.start(input,output,settings,false,0),"start after cancel refused");
                auto initial=manager.poll();require(initial.jobId!=lastJob,"retry reused job id");lastJob=initial.jobId;
                if(round>0){
                    manager.watching(true);if(round==1)manager.pause(true);
                    const auto deadline=GetTickCount64()+20000;
                    while(GetTickCount64()<deadline){auto s=manager.poll();require(s.active(),"export finished before requested cancel phase");if(round==1?s.state==ExportState::Paused:s.sourceFrames>=5)break;Sleep(10);}
                }
                const auto requested=GetTickCount64();manager.cancel();
                require(manager.poll().active(),"cancel skipped resource collection state");
                require(!manager.start(input,output,settings,false),"second worker launched while cancelling");
                auto s=finish(manager);collected(s);
                require(s.state==ExportState::Cancelled,"cancel outcome incorrect");
                require(GetTickCount64()-requested>=1000,"delayed worker result stopped polling early");
                require(!std::filesystem::exists(s.temporaryPath),"owned partial leaked on cancel");
                require(!std::filesystem::exists(output),"cancel created final file");
                require(std::filesystem::file_size(output+L".partial")==24,"foreign partial changed");
                std::wcout<<L"cancel round="<<round<<L" job="<<s.jobId<<L" collected=true\n";
            }
            require(manager.start(input,output,settings,false,20),"same output retry failed");
            auto s=finish(manager);collected(s);require(s.state==ExportState::Succeeded,"final retry did not succeed");
            require(s.encoded==20&&std::filesystem::exists(output),"final output incomplete");
            require(!manager.start(input,output,settings,false),"existing output overwritten");
            require(manager.poll().message.find(L"已存在")!=std::wstring::npos,"startup reason was hidden");
        }else if(mode==L"lifecycle-boundary"){
            SetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_SAVE_DELAY_MS",L"1500");
            require(manager.start(input,output,settings,false,2),"save-boundary start refused");
            const auto deadline=GetTickCount64()+15000;
            ExportJobSnapshot s;
            do{s=manager.poll();require(s.active(),"missed final-save boundary");if(s.progress>=.999)break;Sleep(5);}while(GetTickCount64()<deadline);
            require(s.progress>=.999,"final-save boundary never reached");manager.cancel();s=finish(manager);collected(s);
            require(s.state==ExportState::Cancelled&&!std::filesystem::exists(output)&&!std::filesystem::exists(s.temporaryPath),"final-save cancel left final/temporary output");
            SetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_SAVE_DELAY_MS",nullptr);
            require(manager.start(input,output,settings,false,2),"cleanup-failure start refused");
            const auto owned=manager.poll().temporaryPath;
            HANDLE lock=CreateFileW(owned.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            require(lock!=INVALID_HANDLE_VALUE,"could not hold deletion-denying handle");
            manager.cancel();s=finish(manager);collected(s);CloseHandle(lock);
            require(s.state==ExportState::Cancelled&&std::filesystem::exists(owned)&&s.message.find(owned)!=std::wstring::npos,"cleanup failure hid owned file location");
            SetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_EXIT_DELAY_MS",L"1200");
            require(manager.start(input,output,settings,false,2),"old undeletable partial blocked retry");
            const auto successDeadline=GetTickCount64()+15000;
            do{s=manager.poll();if(s.active()&&std::filesystem::exists(output))break;Sleep(5);}while(GetTickCount64()<successDeadline);
            require(s.active()&&s.progress>=.999&&std::filesystem::exists(output),"could not observe saved output before process exit");
            manager.cancel();s=finish(manager);collected(s);
            require(s.state==ExportState::Succeeded&&std::filesystem::exists(output),"late cancel invalidated safely saved success");
            std::wstring reason;require(removeExportTemporaryFile(owned,reason),"released owned partial cleanup failed");
            std::wcout<<L"PASS final-save cancellation, explicit cleanup failure, retry with old partial, late-success cancel race\n";
        }else{
            const double from=mode.find(L"trim")!=std::wstring::npos?0.7:0;
            const double to=from>0?1.7:0;
            if(mode.find(L"none")!=std::wstring::npos)media.audio.policy=media.subtitles.policy=ExportTrackPolicy::None;
            if(mode.find(L"selected")!=std::wstring::npos){media.audio.policy=media.subtitles.policy=ExportTrackPolicy::Selected;media.audio.count=media.subtitles.count=1;media.audio.indices[0]=2;media.subtitles.indices[0]=4;}
            // "reject": the user picked every subtitle track by hand, so a track
            // the container cannot hold must fail the export. "skip": the default
            // "all tracks" policy leaves it out and says so in the result.
            if(mode.find(L"reject")!=std::wstring::npos){
                std::atomic<bool> stop{false};const auto info=ExportStreams::probe(input,ExportMediaOptions{},0,0,stop);
                media.subtitles.policy=ExportTrackPolicy::Selected;media.subtitles.count=0;
                for(const auto& t:info.tracks)if(t.subtitle&&media.subtitles.count<media.subtitles.indices.size())media.subtitles.indices[media.subtitles.count++]=t.index;
                require(media.subtitles.count>0,"reject source has no subtitle track");
            }
            require(manager.start(input,output,settings,hevc,0,-1,from,to,veyra::sink::ExportRateControl::Cq,media),"start refused");
            auto s=finish(manager);collected(s);
            std::wcout<<L"state="<<int(s.state)<<L" encoded="<<s.encoded<<L" message="<<s.message<<L"\n";
            if(mode.find(L"reject")!=std::wstring::npos){require(s.state==ExportState::Failed,"incompatible source was not rejected");require(!std::filesystem::exists(output),"rejection produced final file");require(!s.message.empty(),"failure reason missing");}
            else require(s.state==ExportState::Succeeded,"export failed");
            if(mode.find(L"skip")!=std::wstring::npos)require(s.message.find(L"未保留")!=std::wstring::npos,"skipped tracks not reported");
        }
        passed=true;
    }catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';}
    if(SUCCEEDED(com))CoUninitialize();return passed?0:1;
}
