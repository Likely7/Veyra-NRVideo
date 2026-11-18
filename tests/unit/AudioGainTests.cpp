#include "veyra/sink/AudioGain.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool near(float a,float b){return std::abs(a-b)<0.00001f;}
}
int main()try{
    using namespace veyra::sink;
    require(kMaxAudioGain==3.0f,"shared maximum is 300 percent");
    require(clampAudioGain(-1)==0&&clampAudioGain(9)==3,"gain bounds");
    require(clampAudioGain(std::numeric_limits<float>::quiet_NaN())==0,"NaN gain rejected");
    require(clampAudioGain(std::numeric_limits<float>::infinity())==0,"infinite gain rejected");
    for(unsigned channels:{1u,2u,6u,8u}){
        for(float requested:{0.0f,0.5f,1.0f,2.0f,3.0f}){
            // Low-level input: steady-state output must be exactly 3x at 300%,
            // with no loudness normalisation silently cancelling the boost.
            std::vector<float> pcm(1200*channels,0.1f);float gain=0;
            applyPcmGain(pcm.data(),1200,channels,requested,gain);
            require(near(gain,requested),"ramp reaches requested gain");
            limitPcmPeaks(pcm.data(),1200,channels);
            for(size_t f=800;f<1200;++f)for(unsigned c=0;c<channels;++c)
                require(near(pcm[f*channels+c],0.1f*requested),"real PCM gain on every channel");
            applyPcmGain(pcm.data(),1200,channels,0,gain);
            require(gain==0,"mute reaches zero from boosted gain");
            for(size_t i=800*channels;i<pcm.size();++i)require(pcm[i]==0,"mute is exact silence");
        }
        std::vector<float> one(960*channels,0.1f),split=one;float a=1,b=1;
        applyPcmGain(one.data(),960,channels,3,a);
        applyPcmGain(split.data(),137,channels,3,b);
        applyPcmGain(split.data()+137*channels,823,channels,3,b);
        require(one==split&&a==b,"smoothing is independent of block boundaries");
        std::vector<float> hot(240*channels,0.6f);float full=3;
        applyPcmGain(hot.data(),240,channels,3,full);
        require(near(hot[0],1.8f),"headroom retained until final output/mix");
        limitPcmPeaks(hot.data(),240,channels);
        for(float p:hot)require(p==1,"positive peaks bounded");
        hot.assign(hot.size(),-0.6f);applyPcmGain(hot.data(),240,channels,3,full);
        limitPcmPeaks(hot.data(),240,channels);for(float p:hot)require(p==-1,"negative peaks bounded");
        std::vector<float> tail(240*channels,0.1f);
        fadePcmTail(tail.data(),240,channels,3);
        for(unsigned c=0;c<channels;++c){
            require(near(tail[c],0.3f),"fade starts at full boosted gain");
            require(tail[239*channels+c]==0,"fade ends at silence");
        }
        tail.assign(tail.size(),0.3f);size_t remaining=240;
        fadePcmHead(tail.data(),240,channels,remaining);
        require(remaining==0&&near(tail.back(),0.3f),"recovery fade retains boosted target");
    }
    // Unity path remains bit-identical; unboosted sources keep old behaviour.
    std::array<float,6> normal{0.1f,-0.2f,0.3f,-0.4f,0.5f,-0.6f};const auto before=normal;float unity=1;
    applyPcmGain(normal.data(),3,2,1,unity);require(normal==before,"100 percent is unchanged");
    // Non-finite data may never be written to the endpoint by the peak guard.
    std::array<float,4> invalid{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-9.0f,9.0f};
    limitPcmPeaks(invalid.data(),2,2);require(invalid==std::array<float,4>{0,0,-1,1},"peak guard sanitises non-finite samples");
    std::cout<<"PASS: 0/50/100/200/300% PCM gain, mono/stereo/5.1/7.1, mute, ramps, fades, peak bounds, unity preservation\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
