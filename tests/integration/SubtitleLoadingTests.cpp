#include "veyra/engine/Subtitles.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <stdexcept>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    using Clock=std::chrono::steady_clock;
    if(argc<2){std::cerr<<"Pass a media file with text subtitle tracks\n";return 2;}
    const auto start=Clock::now();
    SubtitleLoader loader;
    const bool twoCueFixture=argc==3&&std::wstring_view(argv[2])==L"--two-cue-fixture";
    auto readGate=std::make_shared<std::atomic<bool>>(false);
    if(twoCueFixture)loader.setReadGate(readGate);
    loader.request(argv[1]);
    const auto cancelled=loader.request(L"");
    auto await=[&](uint64_t id){
        const auto deadline=Clock::now()+std::chrono::seconds(20);
        while(Clock::now()<deadline){
            if(auto result=loader.poll()){
                if(result->generation!=id)throw std::runtime_error("stale generation published");
                if(result->complete)return *result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("subtitle loading timeout");
    };
    try{
        if(!await(cancelled).tracks.empty())return 1;
        if(twoCueFixture){
            const bool http=subtitleHttpContainer(argv[1]);
            if(http){
                loader.request(argv[1]);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if(loader.poll())throw std::runtime_error("HTTP subtitle scan started before playback buffered");
                if(!await(loader.request(L"")).tracks.empty())return 1;
            }
            readGate->store(true);
            const auto result=await(loader.request(argv[1]));
            if(result.tracks.size()!=1||!result.tracks.front().embedded)return 1;
            const auto& track=result.tracks.front();
            const bool ok=track.cues.size()==2&&textAt(track,1.0)==L"First embedded cue"&&
                          textAt(track,4.0)==L"Second embedded cue";
            std::cout<<"tracks="<<result.tracks.size()<<" cues="<<track.cues.size()
                     <<" httpReadGate="<<http<<" textMatches="<<ok<<'\n';
            return ok?0:1;
        }
        const auto interrupted=loader.request(argv[1]);
        const auto metadataDeadline=Clock::now()+std::chrono::seconds(20);
        bool metadataSeen=false;
        while(Clock::now()<metadataDeadline){
            if(auto metadata=loader.poll()){
                if(metadata->generation!=interrupted||metadata->complete)return 1;
                metadataSeen=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(!metadataSeen)return 1;
        const auto cancelStart=Clock::now();
        if(!await(loader.request(L"")).tracks.empty())return 1;
        const double cancelMs=std::chrono::duration<double,std::milli>(Clock::now()-cancelStart).count();
        std::cout<<"mid-scan cancellation ms="<<cancelMs<<'\n';
        if(cancelMs>2000)return 1;
        const auto id=loader.request(argv[1]);
        bool partialCues=false;
        SubtitleLoader::Result result;
        const auto completeDeadline=Clock::now()+std::chrono::seconds(20);
        while(Clock::now()<completeDeadline){
            if(auto update=loader.poll()){
                if(update->generation!=id)return 1;
                if(!update->complete)for(const auto& track:update->tracks)partialCues|=!track.cues.empty();
                if(update->complete){result=std::move(*update);break;}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::cout<<"progressive cues="<<partialCues<<'\n';
        if(!result.complete||(argc>2&&!partialCues))return 1;
        size_t usable=0,cues=0;
        for(const auto& track:result.tracks){
            usable+=track.usable();cues+=track.cues.size();
            if(track.starts.size()!=track.cues.size())return 1;
            double previous=-1e30;
            for(const auto& cue:track.cues){
                if(cue.begin<previous||cue.end<=cue.begin)return 1;
                previous=cue.begin;
            }
            if(track.usable()){
                const auto& cue=track.cues.front();
                const double at=std::isfinite(cue.end)?(cue.begin+cue.end)/2:cue.begin+.1;
                if(cuesAt(track,at).empty())return 1;
                if(!cue.bitmap&&textAt(track,at).empty())return 1;
            }
        }
        std::cout<<"tracks="<<result.tracks.size()<<" usable="<<usable<<" cues="<<cues
                 <<" elapsedMs="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<'\n';
        if(!usable||(argc>2&&usable!=size_t(std::wcstoul(argv[2],nullptr,10))))return 1;
        std::stop_source stop;stop.request_stop();
        if(!loadEmbeddedSubtitleTracks(argv[1],stop.get_token()).empty())return 1;
        // Destroy with work pending: cancellation must not wait for an entire scan.
        loader.request(argv[1]);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
