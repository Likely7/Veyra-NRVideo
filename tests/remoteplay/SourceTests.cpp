#include "veyra/source/RemotePlaySource.h"
#include "veyra/pipeline/ColorMetadata.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <d3d12.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}
extern "C" {
#include <libavformat/avformat.h>
}
#include <fstream>
#include <iostream>
#include <iterator>
#include <filesystem>
#include <source_location>
#include <string_view>
using namespace veyra;
using namespace veyra::remoteplay;
using namespace veyra::source;
static bool testHardware=false;
namespace veyra::source {
struct RemotePlaySourceTestAccess {
static inline unsigned checks=0;
static void check(bool ok, const std::source_location where=std::source_location::current()) {
    ++checks;
    if(!ok) throw std::runtime_error("RemotePlay source regression failed at line " + std::to_string(where.line()));
}
static std::vector<uint8_t> bytes(const char* path) {
    std::ifstream f(std::filesystem::u8path(path),std::ios::binary);check(f.good()); return {std::istreambuf_iterator<char>(f),{}};
}
static void setup(RemotePlaySource& s) {
    if(testHardware){
        ID3D12Device* device=nullptr;
        check(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
        s.decodeDevice_=std::shared_ptr<ID3D12Device>(device,[](auto* value){value->Release();});
        s.decodeMode_=RemotePlayConnectDesc::DecodeMode::Hardware;
    }
    s.origin100ns_=monotonic100ns();
    s.token_=s.inbox_->begin(static_cast<HostTime>(s.origin100ns_));
    s.token_->connected(); s.connected_=true;
    s.clock_.reset(static_cast<HostTime>(s.origin100ns_),60);
    s.info_.width=1280;s.info_.height=720;
    s.initializeColorContract(Codec::H264, false);
    s.info_.color.matrix=pipeline::YuvMatrix::BT601;
    s.info_.color.range=pipeline::ColorRange::Limited;
    s.info_.color.transfer=pipeline::TransferFunction::BT709;
}
static void pcm() {
    RemotePlaySource s;setup(s);
    PcmBlock b;b.generation=s.token_->generation();b.channels=2;b.rate=48000;
    b.arrival100ns=static_cast<HostTime>(s.origin100ns_)+300000;
    for(int i=0;i<480;++i){b.samples.push_back(static_cast<int16_t>(i));b.samples.push_back(static_cast<int16_t>(i));}
    s.token_->audio(std::move(b));float out[960]{};double pts=0;
    auto n1=s.pullAudio(out,100,&pts);auto n2=s.pullAudio(out,380,nullptr);
    std::cout<<"PCM supplied=480 pulled="<<n1+n2<<" first_pts_ms="<<pts<<" expected_relative_ms=30\n";
    check(n1==100 && n2==380 && std::abs(pts-30.0)<0.001);
    for(int i=0;i<380;++i) check(std::abs(out[i*2]-(i+100)/32768.0f)<1e-6f);
    // Network arrival jitter must not change steady sample cadence.
    PcmBlock next;next.generation=s.token_->generation();next.channels=2;next.rate=48000;
    next.firstSample=480;next.arrival100ns=static_cast<HostTime>(s.origin100ns_)+490000;
    next.samples.resize(960,17);check(s.token_->audio(std::move(next)));
    check(s.pullAudio(out,480,&pts)==480 && std::abs(pts-40.0)<0.001);
    // A genuine new segment is returned separately, not concatenated across a gap.
    for(int i=0;i<2;++i){PcmBlock x;x.generation=s.token_->generation();x.channels=2;x.rate=48000;
        x.firstSample=960+static_cast<uint64_t>(i)*480;x.arrival100ns=static_cast<HostTime>(s.origin100ns_)+600000+i*100000;
        x.discontinuity=i==1;x.samples.resize(960,18);check(s.token_->audio(std::move(x)));}
    float large[1920];check(s.pullAudio(large,960,&pts)==480);
    check(s.pullAudio(large,480,&pts)==480 && std::abs(pts-70.0)<0.001);
}
static void colorContracts() {
    for(bool reconstruct:{false,true}) for(auto codec:{Codec::H264,Codec::H265,Codec::H265Hdr}) {
        RemotePlaySource s;s.initializeColorContract(codec,reconstruct);
        const auto& c=s.info().color;const bool hdr=codec==Codec::H265Hdr;
        check(c.reconstructChroma==reconstruct);
        check(c.isHdrPath()==hdr && c.preserveSdrCodeValues==!hdr);
        check(c.range==pipeline::ColorRange::Limited && c.rangeAssumed);
        check(c.transfer==(hdr?pipeline::TransferFunction::PQ:pipeline::TransferFunction::BT709));
        check(c.matrix==(hdr?pipeline::YuvMatrix::BT2020NCL:pipeline::YuvMatrix::BT709));
        check(c.primaries==(hdr?pipeline::ColorPrimaries::BT2020:pipeline::ColorPrimaries::BT709));
        if(!hdr)check(pipeline::workingTransferCode(c)==1);
    }
    pipeline::ColorDescription reference;reference.transfer=pipeline::TransferFunction::BT709;reference.displayReferred709=true;
    check(pipeline::workingTransferCode(reference)==3); // The general BT.1886 branch still exists.
    std::cout<<"INITIAL_SDR_HDR_COLOR_CONTRACTS_PASS\n";
}
static void splitTest(const char* prefix, Codec codec=Codec::H264, bool expectHdr=false) {
    RemotePlaySource s;setup(s);s.initializeColorContract(codec,false);s.request_.video.codec=codec;auto config=bytes((std::string(prefix)+"-config.bin").c_str());
    auto au=bytes((std::string(prefix)+"-au.bin").c_str());
    VideoSample c;c.generation=s.token_->generation();c.width=1280;c.height=720;
    c.codec=codec;c.arrival100ns=monotonic100ns();c.kind=SampleKind::CodecConfig;c.payload=PaddedBytes::copy(config);
    VideoSample f;f.generation=c.generation;f.width=1280;f.height=720;
    f.codec=codec;f.arrival100ns=monotonic100ns();f.wireFrameIndex=65535;f.payload=PaddedBytes::copy(au);
    const auto ca=s.token_->video(std::move(c));const auto fa=s.token_->video(std::move(f));
    pipeline::FramePacket p;const AVFrame* frame=nullptr;auto r=s.read(p,&frame);
    std::cout<<(codec==Codec::H264?"SPLIT_H264":"SPLIT_H265")<<" config_accepted="<<ca<<" au_accepted="<<fa<<" read_status="<<int(r)
        <<" frame="<<bool(frame)<<" session_state="<<int(s.sessionSnapshot().state)<<"\n";
    check(ca && fa && r==SourceReadStatus::Frame && frame && s.sessionSnapshot().state==SessionState::Streaming);
    check(p.colorInfo.isHdrPath()==expectHdr);
    check(p.colorInfo.preserveSdrCodeValues==!expectHdr);
    check(s.info().color.preserveSdrCodeValues==p.colorInfo.preserveSdrCodeValues);
    if(expectHdr) {
        check(p.colorInfo.transfer==pipeline::TransferFunction::PQ && p.colorInfo.primaries==pipeline::ColorPrimaries::BT2020);
        check(p.colorInfo.pixelFormat==pipeline::SourcePixelFormat::P010);
    } else check(pipeline::workingTransferCode(p.colorInfo)==1);
    std::cout<<"DECODED_COLOR_CONTRACT codec="<<int(codec)<<" hdr="<<expectHdr<<" preserveSdrCodes="<<p.colorInfo.preserveSdrCodeValues<<" workingTransfer="<<pipeline::workingTransferCode(p.colorInfo)<<"\n";
    if(testHardware){check(frame->format==AV_PIX_FMT_D3D12&&s.info().hardwareDecodeActive);std::cout<<"REAL_D3D12_DECODE_PASS codec="<<int(codec)<<"\n";}
    // Decode across the real 16-bit wrap, retaining a valid increasing PTS.
    const auto firstPts=p.pts.to100ns();
    VideoSample wrapped;wrapped.generation=s.token_->generation();wrapped.width=1280;wrapped.height=720;wrapped.codec=codec;
    wrapped.arrival100ns=monotonic100ns();wrapped.wireFrameIndex=0;wrapped.payload=PaddedBytes::copy(au);
    check(s.token_->video(std::move(wrapped)));check(s.read(p,&frame)==SourceReadStatus::Frame);
    check(!p.pts.isUnknown() && p.pts.to100ns()>firstPts);
    if(codec!=Codec::H264)return;
    // Same actual decoder/stream, with SPS/PPS prepended to AU in one packet.
    RemotePlaySource combined;setup(combined);combined.openDecoder(Codec::H264,1920,1080);
    config.insert(config.end(),au.begin(),au.end());pipeline::FramePacket sp;sp.pts={0,60};sp.colorInfo=combined.info_.color;
    bool ok=combined.submitPacket(config,0,sp);
    std::cout<<"COMBINED_H264 ok="<<ok<<" decoded="<<combined.ready_.size();
    if(!combined.ready_.empty()){
        auto& d=combined.ready_.front();std::cout<<" av_matrix="<<int(d.frame->colorspace)
          <<" packet_matrix="<<int(d.packet.colorInfo.matrix)<<" av_range="<<int(d.frame->color_range)
          <<" packet_range="<<int(d.packet.colorInfo.range)
          <<" resolved_matrix="<<int(pipeline::resolveFrameColor(*d.frame,d.packet.colorInfo).matrix)
          <<" info_width="<<combined.info_.width<<" decoded_width="<<d.frame->width;
    }
    std::cout<<"\n";
    pipeline::FramePacket decodedPacket;const AVFrame* decodedView=nullptr;
    auto rr=combined.read(decodedPacket,&decodedView);
    check(rr==SourceReadStatus::Frame && decodedView && combined.info_.width==1280 && combined.sessionSnapshot().state==SessionState::Streaming);
    std::cout<<"REAL_FRAME_STATE read_status="<<int(rr)<<" frame="<<bool(decodedView)
        <<" session_state="<<int(combined.sessionSnapshot().state)<<" streaming_enum="<<int(SessionState::Streaming)<<"\n";
}
static void reorder(const char* path) {
    RemotePlaySource s;setup(s);s.openDecoder(Codec::H264,1280,720);
    AVFormatContext* fmt=nullptr;if(avformat_open_input(&fmt,path,nullptr,nullptr)<0)throw std::runtime_error("open fixture");
    AVPacket* p=av_packet_alloc();uint64_t n=0;int mismatches=0,outputs=0;
    while(av_read_frame(fmt,p)>=0){pipeline::FramePacket sp;sp.pts={static_cast<int64_t>(n),60};sp.colorInfo=s.info_.color;
        if(!s.submitPacket({p->data,static_cast<size_t>(p->size)},n,sp))throw std::runtime_error("submit AU");
        while(!s.ready_.empty()) {auto d=std::move(s.ready_.front());s.ready_.pop_front();++outputs;
            if(d.frame->pts!=d.packet.pts.num){++mismatches;std::cout<<"REORDER actual_packet_id="<<d.frame->pts<<" assigned="<<d.packet.pts.num<<"\n";}}
        ++n;av_packet_unref(p);
    }
    av_packet_free(&p);avformat_close_input(&fmt);
    check(n==12 && outputs==10 && mismatches==0);
    std::cout<<"REORDER input="<<n<<" output_before_eof="<<outputs<<" wrong_pts="<<mismatches<<"\n";
}
static void compareDecode(const char* prefix,Codec codec){
    RemotePlaySource software,hardware;
    testHardware=false;setup(software);testHardware=true;setup(hardware);
    const auto config=bytes((std::string(prefix)+"-config.bin").c_str());
    auto data=config;const auto au=bytes((std::string(prefix)+"-au.bin").c_str());data.insert(data.end(),au.begin(),au.end());
    pipeline::FramePacket stamp;stamp.pts={0,60};
    for(auto* source:{&software,&hardware}){
        check(source->openDecoder(codec,1280,720));stamp.colorInfo=source->info_.color;
        check(source->submitPacket(data,0,stamp)&&source->ready_.size()==1);
    }
    auto* cpu=software.ready_.front().frame.get();auto* gpu=hardware.ready_.front().frame.get();
    AVFrame* downloaded=av_frame_alloc();check(downloaded&&av_hwframe_transfer_data(downloaded,gpu,0)>=0);
    // Diagnostic-only readback: compare identical coded YUV before the common
    // colour shader. Product hardware playback never performs this transfer.
    check(cpu->width==downloaded->width&&cpu->height==downloaded->height&&downloaded->format==AV_PIX_FMT_NV12&&cpu->format==AV_PIX_FMT_YUV420P);
    int maxError=0;
    for(int y=0;y<cpu->height;++y)for(int x=0;x<cpu->width;++x)maxError=std::max(maxError,std::abs(int(cpu->data[0][y*cpu->linesize[0]+x])-int(downloaded->data[0][y*downloaded->linesize[0]+x])));
    for(int y=0;y<cpu->height/2;++y)for(int x=0;x<cpu->width/2;++x)for(int c=0;c<2;++c)maxError=std::max(maxError,std::abs(int(cpu->data[c+1][y*cpu->linesize[c+1]+x])-int(downloaded->data[1][y*downloaded->linesize[1]+2*x+c])));
    av_frame_free(&downloaded);check(maxError<=2);
    check(software.ready_.front().packet.pts.to100ns()==hardware.ready_.front().packet.pts.to100ns());
    check(software.info_.color.matrix==hardware.info_.color.matrix&&software.info_.color.range==hardware.info_.color.range&&software.info_.color.transfer==hardware.info_.color.transfer);
    std::cout<<"SOFT_HARD_YUV_COMPARE_PASS codec="<<int(codec)<<" max_error="<<maxError<<" diagnostic_readback_only=1\n";
}
static void fallback(const char* prefix){
    RemotePlaySource missing;missing.decodeMode_=RemotePlayConnectDesc::DecodeMode::Hardware;
    check(!missing.openDecoder(Codec::H264,1280,720));
    std::cout<<"HARDWARE_WITHOUT_DEVICE_REJECTED\n";
    auto data=bytes((std::string(prefix)+"-config.bin").c_str());const auto au=bytes((std::string(prefix)+"-au.bin").c_str());data.insert(data.end(),au.begin(),au.end());
    for(bool automatic:{true,false}){
        RemotePlaySource s;testHardware=true;setup(s);s.decodeMode_=automatic?RemotePlayConnectDesc::DecodeMode::Automatic:RemotePlayConnectDesc::DecodeMode::Hardware;
        check(s.openDecoder(Codec::H264,1280,720));
        // Force FFmpeg format negotiation to fail, without changing runtime
        // binaries or adding a product-only fake hardware-success path.
        s.codecContext_->get_format=[](AVCodecContext*,const AVPixelFormat*){return AV_PIX_FMT_NONE;};
        VideoSample config;config.generation=s.token_->generation();config.width=1280;config.height=720;config.codec=Codec::H264;config.arrival100ns=monotonic100ns();config.kind=SampleKind::CodecConfig;config.payload=PaddedBytes::copy(bytes((std::string(prefix)+"-config.bin").c_str()));check(s.token_->video(std::move(config)));
        VideoSample sample;sample.generation=s.token_->generation();sample.width=1280;sample.height=720;sample.codec=Codec::H264;sample.arrival100ns=monotonic100ns();sample.wireFrameIndex=1;sample.payload=PaddedBytes::copy(au);
        check(s.token_->video(std::move(sample)));pipeline::FramePacket packet;const AVFrame* frame=nullptr;
        const auto result=s.read(packet,&frame);
        if(automatic){
            check(result==SourceReadStatus::Waiting&&s.hardwareFallback_&&!s.codecContext_);
            check(s.openDecoder(Codec::H264,1280,720)&&!s.codecContext_->hw_device_ctx);
            packet.pts={1,60};check(s.submitPacket(data,2,packet)&&s.ready_.size()==1&&!s.info_.hardwareDecodeActive);
        }else check(result==SourceReadStatus::Error);
        std::cout<<"HARDWARE_FAILURE_POLICY_PASS automatic="<<automatic<<"\n";
    }
}
};
}
int wmain(int argc,wchar_t**argv) {
    std::vector<std::string> paths;std::string hdrPrefix;
    for(int i=1;i<argc;++i){
        if(std::wstring_view(argv[i])==L"--hardware"){testHardware=true;continue;}
        if(std::wstring_view(argv[i])==L"--hdr10-prefix"){
            if(++i>=argc)return 2;
            const auto u=std::filesystem::path(argv[i]).u8string();hdrPrefix.assign(u.begin(),u.end());continue;
        }
        const auto u=std::filesystem::path(argv[i]).u8string();paths.emplace_back(u.begin(),u.end());
    }
    if(paths.size()<2||paths.size()>3||(testHardware&&paths.size()!=3))return 2;
    std::cout<<"SOURCE_TEST: real FFmpeg, direct inbox injection; no PS5/WASAPI/presentation validation; hardware="<<testHardware<<"\n";
    try{
        RemotePlaySourceTestAccess::colorContracts();RemotePlaySourceTestAccess::pcm();
        RemotePlaySourceTestAccess::splitTest(paths[0].c_str());RemotePlaySourceTestAccess::reorder(paths[1].c_str());
        if(paths.size()==3){
            RemotePlaySourceTestAccess::splitTest(paths[2].c_str(),Codec::H265);
            RemotePlaySourceTestAccess::splitTest(paths[2].c_str(),Codec::H265Hdr); // Actual 8-bit SDR fallback.
        }
        if(!hdrPrefix.empty())RemotePlaySourceTestAccess::splitTest(hdrPrefix.c_str(),Codec::H265Hdr,true);
        if(testHardware){RemotePlaySourceTestAccess::compareDecode(paths[0].c_str(),Codec::H264);RemotePlaySourceTestAccess::compareDecode(paths[2].c_str(),Codec::H265);RemotePlaySourceTestAccess::fallback(paths[0].c_str());}
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
    std::cout<<"REMOTEPLAY_SOURCE_REGRESSIONS_PASS checks="<<RemotePlaySourceTestAccess::checks<<"\n";return 0;
}
