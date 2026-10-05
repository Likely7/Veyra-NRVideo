#include "veyra/sink/WasapiAudioSink.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

// A five-second 440 Hz PCM fixture. Exercise the product decoder, bounded
// buffer, tempo processor and media-PTS mapping, including seek/reuse.
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    using Clock=std::chrono::steady_clock;
    bool all=true;
    for(double rate:{.25,.5,1.,1.3,1.5,1.8,2.,3.,4.})for(double start:{0.,1200.}){
        veyra::sink::AudioPipeline audio;
        if(!audio.open(argv[1])||!audio.setPlaybackRate(rate))return 1;
        for(double invalid:{0.,-.5,.249,4.001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
            if(audio.setPlaybackRate(invalid))return 1;
        audio.setPaused(false);audio.startThread(nullptr,false,start);
        const unsigned channels=audio.pcmFormat().channels;
        std::vector<float> block(997*channels),pcm;
        double first=-1,last=-1;bool continuous=true;
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        while(Clock::now()<deadline){
            double pts=-1;const auto n=audio.pull(block.data(),997,&pts);
            if(n){
                if(first<0)first=pts;
                if(last>=0&&std::abs(pts-last)>1.)continuous=false;
                last=audio.lastPullEndPtsMs().value_or(pts+1000.*n/48000.);
                for(size_t i=0;i<n;++i)pcm.push_back(block[i*channels]);
            }else if(audio.decodingComplete())break;
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        audio.stopThread();
        unsigned crossings=0;double energy=0;
        // SoundTouch's beginning/end overlap includes silence. Measure pitch
        // over the middle half, while duration/PTS still cover every sample.
        const size_t begin=pcm.size()/4,end=pcm.size()*3/4;
        for(size_t i=1;i<pcm.size();++i){
            if(i>begin&&i<end&&pcm[i]>=0&&pcm[i-1]<0)++crossings;
            energy+=pcm[i]*pcm[i];
        }
        const double hz=end>begin?48000.*crossings/(end-begin):0;
        const double duration=1000.*pcm.size()/48000.;
        const bool ok=audio.decodingComplete()&&audio.overruns()==0&&continuous&&
            std::abs(first-start)<1&&std::abs(last-5000)<2&&
            std::abs(duration-(5000-start)/rate)<3&&std::abs(hz-440)<3&&energy>10;
        all&=ok;
        std::cout<<(ok?"PASS ":"FAIL ")<<"rate="<<rate<<" start="<<start<<" outputMs="<<duration
            <<" media="<<first<<".."<<last<<" Hz="<<hz<<" continuous="<<continuous<<" overruns="<<audio.overruns()<<std::endl;
    }
    CoUninitialize();return all?0:1;
}
