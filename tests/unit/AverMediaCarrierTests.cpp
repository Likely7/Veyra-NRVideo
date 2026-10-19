#include "veyra/source/AverMediaAudioCarrier.h"
#include "veyra/source/CaptureAudioDeliveryPolicy.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <climits>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}
using namespace veyra;
namespace {
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Output {unsigned channels;uint32_t rate,mask;bool discontinuity;std::string codec;std::vector<float> pcm;};
std::vector<std::vector<uint8_t>> encode(AVCodecID id,uint64_t mask,int rate){
    const auto* codec=avcodec_find_encoder(id);require(codec!=nullptr,"encoder available");
    auto* c=avcodec_alloc_context3(codec);require(c!=nullptr,"encoder context");
    c->sample_rate=rate;c->sample_fmt=AV_SAMPLE_FMT_FLTP;c->bit_rate=mask==3?192000:448000;
    av_channel_layout_from_mask(&c->ch_layout,mask);
    require(avcodec_open2(c,codec,nullptr)>=0,"open real encoder");
    AVFrame* f=av_frame_alloc();AVPacket* packet=av_packet_alloc();
    f->nb_samples=c->frame_size;f->format=c->sample_fmt;f->sample_rate=rate;
    av_channel_layout_copy(&f->ch_layout,&c->ch_layout);require(av_frame_get_buffer(f,0)>=0,"encoder buffer");
    std::vector<std::vector<uint8_t>> packets;
    auto drain=[&]{while(avcodec_receive_packet(c,packet)>=0){packets.emplace_back(packet->data,packet->data+packet->size);av_packet_unref(packet);}};
    for(int n=0;n<12;++n){
        av_frame_make_writable(f);f->pts=int64_t(n)*f->nb_samples;
        for(int ch=0;ch<c->ch_layout.nb_channels;++ch){auto* values=reinterpret_cast<float*>(f->extended_data[ch]);
            for(int i=0;i<f->nb_samples;++i)values[i]=float(0.05*std::sin((i+f->pts)*6.28318530718*(180+ch*70)/rate));}
        require(avcodec_send_frame(c,f)>=0,"encode frame");drain();
    }
    avcodec_send_frame(c,nullptr);drain();av_packet_free(&packet);av_frame_free(&f);avcodec_free_context(&c);
    require(packets.size()>=10,"real encoded packets");return packets;
}
std::vector<uint8_t> burst(const std::vector<uint8_t>& packet,unsigned type){
    std::vector<uint8_t> out(std::max<size_t>(6144,8+((packet.size()+1)&~size_t(1))),0);
    out[0]=0x72;out[1]=0xf8;out[2]=0x1f;out[3]=0x4e;out[4]=uint8_t(type);
    const size_t pd=type==0x15?packet.size():packet.size()*8;out[6]=uint8_t(pd);out[7]=uint8_t(pd>>8);
    for(size_t i=0;i<packet.size();i+=2){out[8+i]=i+1<packet.size()?packet[i+1]:0;out[9+i]=packet[i];}return out;
}
struct CodecFormat {unsigned channels=0,rate=0;uint32_t mask=0;};
CodecFormat referenceFormat(AVCodecID id,const std::vector<std::vector<uint8_t>>& packets){
    // AC3 records acmod, not Windows' side/back speaker mask. Obtain the codec's
    // canonical decoded layout independently, rather than expecting the input
    // encoder's Windows mask to survive the compressed format unchanged.
    auto* ctx=avcodec_alloc_context3(avcodec_find_decoder(id));
    require(ctx&&avcodec_open2(ctx,ctx->codec,nullptr)>=0,"reference decoder");
    auto* f=av_frame_alloc();auto* p=av_packet_alloc();CodecFormat result;
    for(const auto& bytes:packets){
        av_new_packet(p,int(bytes.size()));std::copy(bytes.begin(),bytes.end(),p->data);
        require(avcodec_send_packet(ctx,p)>=0,"reference packet");
        while(avcodec_receive_frame(ctx,f)>=0){result={unsigned(f->ch_layout.nb_channels),unsigned(f->sample_rate),uint32_t(f->ch_layout.u.mask)};av_frame_unref(f);}av_packet_unref(p);
    }
    av_packet_free(&p);av_frame_free(&f);avcodec_free_context(&ctx);return result;
}
}
int main(){try{
    source::CaptureAudioDeliveryPolicy delivery;
    require(delivery.delivered(false)&&delivery.discontinuity(false),"one rejected block keeps stream and marks next discontinuity");
    require(delivery.delivered(true)&&!delivery.discontinuity(false),"accepted block clears rejection gap");
    for(unsigned n=1;n<delivery.kRecoveryThreshold;++n)require(delivery.delivered(false),"temporary rejected blocks do not rebuild graph");
    require(!delivery.delivered(false),"persistent rejected blocks request bounded recovery");
    require(delivery.delivered(true)&&delivery.rejected()==0,"successful delivery restores budget");
    for(auto codec:{AV_CODEC_ID_AC3,AV_CODEC_ID_EAC3}){
        const auto packets=encode(codec,0x60f,48000);
        const auto kind=codec==AV_CODEC_ID_AC3?sink::BitstreamKind::Ac3:sink::BitstreamKind::Eac3;
        sink::BitstreamDecoder direct,parsed;std::vector<float> a,b;
        require(direct.open(kind)&&parsed.open(kind),"latency comparison decoders");
        require(direct.pushFramed(packets[0].data(),packets[0].size(),a),"complete codec frame directly accepted");
        require(direct.decodedFrames()==1536&&a.size()==1536*6,"complete frame emits immediately without next header");
        require(parsed.push(packets[0].data(),packets[0].size(),b)&&parsed.decodedFrames()==0,"byte-stream parser keeps one frame");
        require(parsed.push(packets[1].data(),packets[1].size(),b)&&parsed.decodedFrames()==1536,"byte-stream parser emits after next header");
        std::cout<<"FRAMED "<<avcodec_get_name(codec)<<" immediateFrames="<<direct.decodedFrames()<<" parserFirst=0 parserSecond="<<parsed.decodedFrames()<<" samplesPerFrame=1536 rate=48000\n";
        std::vector<uint8_t> joined;for(size_t n=0;n<6;++n)joined.insert(joined.end(),packets[n].begin(),packets[n].end());
        sink::BitstreamDecoder aggregate;require(aggregate.open(kind),"aggregate decoder");a.clear();
        require(aggregate.pushFramed(joined.data(),joined.size(),a),"complete multi-frame packet accepted");
        std::cout<<"AGGREGATE "<<avcodec_get_name(codec)<<" frames="<<aggregate.decodedFrames()<<" expected="<<1536*6<<'\n';
        require(aggregate.decodedFrames()==1536*6&&a.size()==1536*6*6,"every frame in complete aggregate decoded");
        if(codec==AV_CODEC_ID_EAC3){
            uint64_t frames=0;source::AverMediaAudioCarrier grouped([&](const source::VendorPcmBlock& block){frames+=block.frames;return true;});
            const auto wire=burst(joined,0x15);
            require(grouped.feed(wire,{2,16,48000},0)&&grouped.feed(wire,{2,16,48000},.2),"EAC3 multi-frame IEC bursts");
            require(frames==1536*6,"EAC3 carrier does not discard aggregate tail");
        }
        uint8_t byte=0;require(!direct.pushFramed(&byte,size_t(INT_MAX)+1,a),"oversized framed packet rejected before allocation");
        require(!direct.pushFramed(nullptr,1,a),"null non-empty framed packet rejected");
    }
    using source::avermedia::DeviceInfo;
    const std::wstring video=L"\\\\?\\usb#vid_07ca&pid_2553&mi_00#5&abc&0&0000#{guid}";
    const std::wstring audio=L"\\\\?\\usb#vid_07ca&pid_2553&mi_02#5&abc&0&0002#{guid}";
    std::vector<DeviceInfo> devices{{L"Live Gamer Ultra 2.1-Audio",audio},{L"Live Gamer Ultra 2.1-Audio",L"\\\\?\\usb#vid_07ca&pid_2553&mi_02#5&other&0&0002#{guid}"}};
    require(source::avermedia::selectDevice(devices,L"Live Gamer Ultra 2.1-Video",video)==0,"USB sibling matches exact card");
    require(!source::avermedia::selectDevice(devices,L"Live Gamer Ultra 2.1-Audio",L"opaque"),"identical card names ambiguous");
    require(!source::avermedia::selectDevice(devices,L"Live Gamer Ultra 2.1-Audio",L"\\\\?\\usb#vid_07ca&pid_2553&mi_00#5&absent&0&0000#{guid}"),"no wrong-card name fallback");
    require(!source::avermedia::eligible(L"Microphone",L"opaque",0),"unrelated microphone unchanged");
    require(!source::avermedia::eligible(L"Live Gamer",L"vid_1234",0),"explicit other vendor rejected");
    require(!source::avermedia::eligible(L"Live Gamer",video,1),"PCM-only does not arm component");
    std::vector<Output> outputs;
    source::AverMediaAudioCarrier carrier([&](const source::VendorPcmBlock& b){
        require(b.frames<=1536,"bounded output block");require(b.mask!=0,"actual speaker mask");
        outputs.push_back({b.channels,b.rate,b.mask,b.discontinuity,std::string(b.codec),{b.samples,b.samples+b.frames*b.channels}});return true;
    });
    const source::avermedia::SampleInfo stereo{2,16,48000};
    std::vector<uint8_t> pcm(1920*4,0);for(size_t i=0;i<pcm.size();i+=2){pcm[i]=0x00;pcm[i+1]=0x20;}
    require(carrier.feed(pcm,stereo,0),"initial PCM");require(!outputs.empty(),"PCM delivered without four-second preroll");
    require(outputs.front().discontinuity&&outputs.front().pcm.front()==0.25f,"PCM value and initial discontinuity");
    auto feedCodec=[&](AVCodecID codec,uint64_t mask,int rate,unsigned type,double start){
        const auto encoded=encode(codec,mask,rate);size_t before=outputs.size();size_t total=0;
        for(const auto& packet:encoded){const auto wire=burst(packet,type);size_t pos=0;
            for(size_t n:{size_t(1),size_t(3),size_t(1919),size_t(7),size_t(8192)}){
                const size_t bytes=std::min(n,wire.size()-pos);if(!bytes)break;
                require(carrier.feed(std::span(wire).subspan(pos,bytes),stereo,start+double(total)/192000),"split IEC feed");pos+=bytes;total+=bytes;
            }
            if(pos<wire.size()){require(carrier.feed(std::span(wire).subspan(pos),stereo,start+double(total)/192000),"IEC remainder");total+=wire.size()-pos;}
        }
        require(outputs.size()>before+2,"real decoded audio exists");
        const auto& last=outputs.back();const auto expected=referenceFormat(codec,encoded);
        std::cout<<"CODEC "<<avcodec_get_name(codec)<<" encoderMask=0x"<<std::hex<<mask<<" decodedMask=0x"<<last.mask<<std::dec<<" channels="<<last.channels<<" rate="<<last.rate<<'\n';
        require(last.channels==expected.channels&&last.rate==expected.rate&&last.mask==expected.mask,"decoded channel layout/rate matches independent codec reference");
        require(!last.codec.empty(),"diagnostics name compressed input");
        for(float v:last.pcm)require(std::isfinite(v)&&std::abs(v)<1,"decoded PCM finite and bounded");
        for(unsigned ch=0;ch<last.channels;++ch){double energy=0;for(size_t i=ch;i<last.pcm.size();i+=last.channels)energy+=last.pcm[i]*last.pcm[i];require(energy>0.001,"every decoded channel contains signal");}
        require(std::any_of(outputs.begin()+before,outputs.end(),[](auto& o){return o.discontinuity;}),"format boundary resets history");
    };
    feedCodec(AV_CODEC_ID_AC3,0x3f,48000,1,1);
    feedCodec(AV_CODEC_ID_AC3,3,44100,1,2);
    feedCodec(AV_CODEC_ID_AC3,0x60f,48000,1,3);
    auto before=outputs.size();require(carrier.feed(pcm,stereo,8),"compressed to PCM");
    require(outputs.size()==before+1&&outputs.back().codec.empty()&&outputs.back().channels==2,"switch to PCM session");
    require(outputs.back().pcm.size()<=960*2&&outputs.back().discontinuity,"only newest 20ms after compressed loss");
    feedCodec(AV_CODEC_ID_EAC3,0x3f,48000,0x15,9);
    before=outputs.size();require(carrier.feed(std::span(pcm).first(pcm.size()-1),stereo,15),"PCM return with split sample frame");
    require(outputs.size()==before+1&&outputs.back().discontinuity,"fragmented PCM return is one fresh epoch");
    require(std::all_of(outputs.back().pcm.begin(),outputs.back().pcm.end(),[](float v){return v==0.25f;}),"PCM fallback preserves frame alignment");
    require(carrier.feed(std::span(pcm).last(1),stereo,15.01)&&carrier.feed(pcm,stereo,15.02),"remaining partial sample retained");
    require(std::all_of(outputs.back().pcm.begin(),outputs.back().pcm.end(),[](float v){return v==0.25f;}),"later PCM remains aligned");
    carrier.reset();outputs.clear();
    std::vector<uint8_t> unsupported(6144,0);unsupported[0]=0x72;unsupported[1]=0xf8;unsupported[2]=0x1f;unsupported[3]=0x4e;unsupported[4]=0x16;unsupported[6]=0x10;
    require(carrier.feed(unsupported,stereo,0)&&carrier.feed(unsupported,stereo,0.1),"unknown HD carrier detected");
    require(outputs.empty()&&!carrier.state().error.empty(),"unsupported compressed bytes never played as noise");
    carrier.reset();outputs.clear();std::vector<uint8_t> raw24{0,0,0x40,0,0,0xc0};
    require(carrier.feed(raw24,{2,24,96000},0),"24-bit metadata");
    require(outputs.back().rate==96000&&outputs.back().pcm[0]==0.5f&&outputs.back().pcm[1]==-0.5f,"actual 24-bit sample conversion");
    std::vector<uint8_t> tooLarge(source::AverMediaAudioCarrier::kMaxBytes+1);require(!carrier.feed(tooLarge,stereo,0),"oversized callback rejected");
    source::AverMediaAudioCarrier rejected([](const source::VendorPcmBlock&){return false;});require(!rejected.feed(pcm,stereo,0),"session error requests recovery");
    carrier.reset();outputs.clear();bool progress=true;
    const auto invalidAc3=burst(std::vector<uint8_t>(256,0),1);
    for(unsigned n=0;n<80&&progress;++n)progress=carrier.feed(invalidAc3,stereo,n*.032);
    require(!progress&&outputs.empty(),"continuous undecodable carrier has a bounded recovery trigger");
    carrier.reset();outputs.clear();
    for(unsigned n=0;n<2000;++n){std::vector<uint8_t> noise(511,0);require(carrier.feed(noise,stereo,n*.01),"continuous quiet PCM");require(carrier.state().retainedBytes<=12,"bounded persistent PCM buffer");}
    std::cout<<"PASS "<<checks<<" checks: identity, real AC3/EAC3, fragmented carriers, 6/2ch and rate changes, PCM transitions, malformed input\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
