#include "veyra/source/AverMediaAudioCarrier.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace veyra::source {
namespace {
constexpr uint8_t syncBytes[]{0x72,0xF8,0x1F,0x4E};
uint16_t word(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
bool sync(const uint8_t* p){return std::memcmp(p,syncBytes,4)==0;}
bool supported(sink::BitstreamKind k){return k==sink::BitstreamKind::Ac3||k==sink::BitstreamKind::Eac3||k==sink::BitstreamKind::Dts;}
}
AverMediaAudioCarrier::AverMediaAudioCarrier(Callback callback):callback_(std::move(callback)){}
void AverMediaAudioCarrier::reset(){
    buffer_.clear();packet_.clear();pcm_.clear();decoder_.reset();state_={};
    kind_=candidate_=sink::BitstreamKind::None;confirmations_=undecodedBursts_=0;compressed_=false;discontinuity_=true;lastIec_=0;revision_=0;input_={};
}
bool AverMediaAudioCarrier::emitPcm(std::span<const uint8_t> bytes,avermedia::SampleInfo info){
    // The public callback describes the samples. Never guess 16-bit/48k from
    // the size of a driver delivery (24-bit PCM is not a compressed carrier).
    if(info.channels<1||info.channels>8||info.rate<8000||info.rate>192000
        ||(info.bits!=16&&info.bits!=24&&info.bits!=32)){state_.error="unsupported vendor PCM format";return false;}
    const size_t sampleBytes=info.bits/8,align=sampleBytes*info.channels,frames=bytes.size()/align;
    // Limit each session push to 20 ms; never replay a four-second probe queue.
    const size_t blockFrames=std::max<size_t>(1,info.rate/50);
    for(size_t frame=0;frame<frames;frame+=blockFrames){
        const size_t count=std::min(blockFrames,frames-frame);pcm_.resize(count*info.channels);
        for(size_t i=0;i<pcm_.size();++i){
            const auto* p=bytes.data()+(frame*info.channels+i)*sampleBytes;
            uint32_t v=0;for(size_t j=0;j<sampleBytes;++j)v|=uint32_t(p[j])<<(8*j);
            const int32_t value=info.bits==16?int32_t(int16_t(v)):info.bits==24?int32_t(v<<8)>>8:int32_t(v);
            pcm_[i]=float(double(value)/std::ldexp(1.0,int(info.bits)-1));
        }
        const uint32_t mask=info.channels==1?4:info.channels==2?3:info.channels==6?0x3f:info.channels==8?0x63f:0;
        if(!mask){state_.error="vendor PCM has no declared speaker layout";return false;}
        VendorPcmBlock block{pcm_.data(),count,info.channels,info.rate,mask,discontinuity_,{}};
        if(callback_&&!callback_(block)){state_.error="capture audio session rejected PCM";return false;}
        discontinuity_=false;state_.pcmFrames+=count;
    }
    state_.channels=info.channels;state_.rate=info.rate;state_.codec="linear PCM";state_.bitstream=false;state_.error.clear();
    return true;
}
bool AverMediaAudioCarrier::emitBurst(std::span<const uint8_t> words,size_t payloadBytes,sink::BitstreamKind kind){
    if(kind!=candidate_){candidate_=kind;confirmations_=0;}
    if(confirmations_<2)++confirmations_;
    if(confirmations_<2)return true; // Count distinct complete payloads, not sync inside a payload.
    if(!supported(kind)){state_.error="unsupported IEC 61937 vendor carrier";return true;}
    if(!decoder_||kind!=kind_){
        decoder_=std::make_unique<sink::BitstreamDecoder>();
        if(!decoder_->open(kind,false)){state_.error=decoder_->lastError();return false;}
        kind_=kind;revision_=0;undecodedBursts_=0;discontinuity_=true;
    }
    packet_.resize(words.size());
    for(size_t i=0;i+1<words.size();i+=2){packet_[i]=words[i+1];packet_[i+1]=words[i];}
    packet_.resize(payloadBytes); // Odd byte payloads include a padded carrier word.
    pcm_.clear();
    // IEC supplies a complete payload boundary (EAC3 can aggregate frames).
    // push() would retain AC3 until the next header: 32ms at 48kHz.
    if(!decoder_->pushFramed(packet_.data(),packet_.size(),pcm_)){state_.error=decoder_->lastError();return false;}
    if(pcm_.empty()){
        if(++undecodedBursts_>=64){state_.error="no decoded audio after 64 carrier bursts";return false;}
    }else undecodedBursts_=0;
    state_.codec=sink::bitstreamKindName(kind);state_.bitstream=true;
    const auto channels=decoder_->channels(),rate=decoder_->sampleRate();
    if(!pcm_.empty()&&channels&&rate){
        discontinuity_=discontinuity_||revision_!=decoder_->formatRevision();revision_=decoder_->formatRevision();
        VendorPcmBlock block{pcm_.data(),pcm_.size()/channels,channels,rate,decoder_->channelMask(),discontinuity_,state_.codec};
        if(callback_&&!callback_(block)){state_.error="capture audio session rejected decoded carrier";return false;}
        discontinuity_=false;state_.decodedFrames+=block.frames;state_.channels=channels;state_.rate=rate;
    }
    state_.error.clear();return true;
}
bool AverMediaAudioCarrier::feed(std::span<const uint8_t> bytes,avermedia::SampleInfo info,double now){
    if(bytes.empty())return true;
    if(input_.channels!=info.channels||input_.bits!=info.bits||input_.rate!=info.rate){reset();input_=info;}
    const size_t align=size_t(info.channels)*(info.bits/8);
    if(!align||align>32||bytes.size()>kMaxBytes){state_.error="invalid or oversized vendor callback";return false;}
    if(buffer_.size()+bytes.size()>kMaxBytes){
        state_.droppedBytes+=buffer_.size();state_.error="vendor carrier buffer limit reached";return false;
    }
    // Only 16-bit stereo is an IEC carrier. Other declared PCM is converted
    // using its actual metadata and still goes through the shared session.
    if(info.bits!=16||info.channels!=2){
        if(compressed_){decoder_.reset();compressed_=false;discontinuity_=true;}
        buffer_.insert(buffer_.end(),bytes.begin(),bytes.end());
        const size_t complete=buffer_.size()/align*align;
        const bool ok=emitPcm(std::span(buffer_).first(complete),info);
        buffer_.erase(buffer_.begin(),buffer_.begin()+complete);state_.retainedBytes=buffer_.size();return ok;
    }
    buffer_.insert(buffer_.end(),bytes.begin(),bytes.end());
    size_t cursor=0;bool sawIec=false;
    while(cursor+8<=buffer_.size()){
        if(cursor%4||!sync(buffer_.data()+cursor)){++cursor;continue;}
        sawIec=true;
        const uint16_t pc=word(buffer_.data()+cursor+4),pd=word(buffer_.data()+cursor+6);
        const auto type=pc&0x1f;
        const auto kind=sink::classifyIec61937DataType(uint16_t(type));
        // E-AC3 and HD data lengths use bytes, AC3 and DTS-core use bits.
        const size_t payload=(type==0x15||type==0x11||type==0x16)?pd:(size_t(pd)+7)/8;
        if(!compressed_){
            // No compressed payload (including an unsupported one) is audible
            // as PCM. Discard a short raw prefix at the format boundary.
            compressed_=true;discontinuity_=true;decoder_.reset();candidate_=sink::BitstreamKind::None;confirmations_=0;
        }
        lastIec_=now;
        if(!pd||(pc&0x80)){cursor+=8;continue;} // pause/error: do not pin the parser at a zero length header.
        if(payload>kMaxBytes-8){++cursor;continue;}
        const size_t stored=(payload+1)&~size_t(1);
        if(cursor+8+stored>buffer_.size())break;
        ++state_.bursts;
        const bool ok=emitBurst(std::span(buffer_).subspan(cursor+8,stored),payload,kind);
        cursor+=8+stored;
        if(!ok){buffer_.erase(buffer_.begin(),buffer_.begin()+cursor);state_.retainedBytes=buffer_.size();return false;}
    }
    if(sawIec||compressed_){
        if(!sawIec&&now-lastIec_>=4.0){
            // The source changed to real PCM. Retain only its newest 20 ms,
            // reset codec history and reconfigure the same playback session.
            const size_t complete=buffer_.size()/align*align;
            const size_t tail=std::min(complete,size_t(info.rate/50)*align);
            decoder_.reset();kind_=candidate_=sink::BitstreamKind::None;confirmations_=0;compressed_=false;discontinuity_=true;
            const bool ok=emitPcm(std::span(buffer_).first(complete).last(tail),info);
            buffer_.erase(buffer_.begin(),buffer_.begin()+complete);state_.retainedBytes=buffer_.size();return ok;
        }
        // Preserve only a partial payload, or at most seven sync/header bytes.
        cursor=cursor/align*align;
        if(cursor)buffer_.erase(buffer_.begin(),buffer_.begin()+cursor);
    }else{
        // Keep two sample frames so a split sync cannot leak a header/payload
        // prefix to the speakers. This adds <0.1ms, not a multi-second backlog.
        const size_t complete=buffer_.size()>8?(buffer_.size()-8)/align*align:0;
        if(complete){const bool ok=emitPcm(std::span(buffer_).first(complete),info);if(!ok)return false;buffer_.erase(buffer_.begin(),buffer_.begin()+complete);}
    }
    state_.retainedBytes=buffer_.size();return true;
}
} // namespace veyra::source
