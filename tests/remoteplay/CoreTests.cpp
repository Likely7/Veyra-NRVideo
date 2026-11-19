// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Types.h"
#include "veyra/remoteplay/Timeline.h"
#include "veyra/remoteplay/VideoIngress.h"
#include "veyra/remoteplay/LatestMailbox.h"
#include "veyra/remoteplay/PacketPump.h"
#include "veyra/remoteplay/InputGate.h"
#include "veyra/remoteplay/SessionInbox.h"
#include "veyra/remoteplay/StreamRecovery.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
using namespace veyra::remoteplay;
namespace {
using Test=std::pair<const char*,void(*)()>;
std::vector<Test>& tests(){static std::vector<Test> value;return value;}
struct AddTest{AddTest(const char* n,void(*f)()){tests().emplace_back(n,f);}};
#define TEST(name) void name(); AddTest reg_##name(#name,name); void name()
#define CHECK(expr) do { if(!(expr))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #expr); } while(false)
#define THROWS(expr) do{bool caught=false;try{(void)(expr);}catch(const std::exception&){caught=true;}CHECK(caught);}while(false)
std::vector<std::uint8_t> configBytes(){return {0,0,0,1,0x67,0x42,0,0x1f,0,0,1,0x68,0xee,0x3c};}
VideoSample config(Generation g=1,HostTime t=0){
    VideoSample s;s.generation=g;s.kind=SampleKind::CodecConfig;s.width=1920;s.height=1080;
    s.payload=PaddedBytes::copy(configBytes());s.arrival100ns=t;return s;
}
VideoSample frame(std::uint16_t index,bool idr,HostTime t=0,Generation g=1){
    VideoSample s;s.generation=g;s.wireFrameIndex=index;s.width=1920;s.height=1080;s.arrival100ns=t;
    const std::array<std::uint8_t,6> bytes{0,0,1,static_cast<std::uint8_t>(idr?0x65:0x41),0x88,0x99};
    s.payload=PaddedBytes::copy(bytes);return s;
}
PcmBlock audio(std::uint64_t index=0,std::size_t count=480,Generation g=1){
    PcmBlock b;b.generation=g;b.firstSample=index;b.channels=2;b.rate=48000;b.samples.resize(count*2,42);return b;
}
void start(VideoIngress& q){q.begin(1);CHECK(q.push(config(),0)==PushStatus::ConfigStored);CHECK(q.push(frame(0,true),0)==PushStatus::Accepted);}

TEST(account_base64_exact){AccountId id{1,0,0,0,0,0,0,0};CHECK(accountIdToBase64(id)=="AQAAAAAAAAA=");CHECK(accountIdFromBase64("AQAAAAAAAAA=")==id);}
TEST(account_decimal_endian){auto id=accountIdFromDecimal("72623859790382856");CHECK(id);CHECK((*id==AccountId{8,7,6,5,4,3,2,1}));}
TEST(account_uint64_boundary){auto id=accountIdFromDecimal("18446744073709551615");CHECK(id);for(auto b:*id)CHECK(b==255);CHECK(!accountIdFromDecimal("18446744073709551616"));}
TEST(account_decimal_strict){for(auto s:{"","-1","+1"," 1","1 ","1a","1.0","184467440737095516150"})CHECK(!accountIdFromDecimal(s));CHECK(accountIdFromDecimal("0"));}
TEST(account_base64_strict){for(auto s:{"","AQAAAAAAAAA","AQAAAAAAAAA==","AQAAAAAAAAA=\n","AQAAAAAAAAB=","AQAAAAAAAA-=","AQAAAA=AAAA="})CHECK(!accountIdFromBase64(s));}
TEST(account_roundtrip_property){std::mt19937 rng(2731);for(int i=0;i<10000;++i){AccountId a{};for(auto& b:a)b=static_cast<std::uint8_t>(rng());CHECK(accountIdFromBase64(accountIdToBase64(a))==a);}}
TEST(pairing_pin_leading_zero){CHECK(parsePairingPin("00123456")==123456u);CHECK(parsePairingPin("00000000")==0u);}
TEST(pairing_pin_reject){for(auto s:{"1234567","123456789","12 45678","-1234567","1234567a"})CHECK(!parsePairingPin(s));}
TEST(host_validation){CHECK(validHost("192.168.1.2"));CHECK(validHost("ps5.local"));CHECK(validHost("console-1"));for(auto s:{"","256.1.1.1","192.168.001.2","https://ps5","user@host","host:9295","foo/bar","-foo","foo-","a..b","a.","127.1","a\nb"})CHECK(!validHost(s));}
TEST(profile_id_validation){CHECK(validProfileId("0123456789abcdef0123456789abcdef"));CHECK(!validProfileId("../../credentials"));CHECK(!validProfileId("0123456789ABCDEF0123456789ABCDEF"));}
TEST(video_profile_validation){VideoProfile p;CHECK(!p.validate());p.width=3840;CHECK(p.validate());p={};p.fps=120;CHECK(p.validate());p={};p.bitrateKbps=999;CHECK(p.validate());p={};p.codec=static_cast<Codec>(99);CHECK(p.validate());}
TEST(padding_ownership){std::vector<std::uint8_t> b{1,2,3};auto p=PaddedBytes::copy(b);b[0]=99;CHECK(p.bytes()[0]==1);CHECK(p.size()==3);CHECK(p.paddingIsZero());}
TEST(padding_reject_oversize){std::vector<std::uint8_t> b(8u*1024u*1024u+1);THROWS(PaddedBytes::copy(b));}
TEST(nal_h264_classification){const auto c=inspectAnnexB(configBytes(),Codec::H264);CHECK(c.valid&&c.hasConfig&&!c.hasPicture&&!c.idr);auto f=frame(1,true);const auto i=inspectAnnexB(f.payload.bytes(),Codec::H264);CHECK(i.valid&&i.hasPicture&&i.idr);}
TEST(nal_h265_idr_not_cra){for(auto type:{19u,20u,21u}){std::array<std::uint8_t,6>b{0,0,1,static_cast<std::uint8_t>(type<<1),1,99};auto n=inspectAnnexB(b,Codec::H265);CHECK(n.valid&&n.hasPicture);CHECK(n.idr==(type!=21));}}
TEST(nal_reject_malformed){for(const auto& b:std::vector<std::vector<std::uint8_t>>{{1,2,3},{0,0,1},{0,0,1,0xff},{0,0,1,0}})CHECK(!inspectAnnexB(b,Codec::H264).valid);std::array<std::uint8_t,5>b{0,0,1,38,0};CHECK(!inspectAnnexB(b,Codec::H265).valid);}
TEST(nal_random_input_bounds){std::mt19937 rng(11);for(int j=0;j<20000;++j){std::vector<std::uint8_t>b(rng()%128);for(auto& v:b)v=static_cast<std::uint8_t>(rng());(void)inspectAnnexB(b,Codec::H264);(void)inspectAnnexB(b,Codec::H265);}}
TEST(sequence_wrap){Sequence16Extender x;CHECK(x.observe(65534).first);CHECK(x.observe(65535).value==65535);CHECK(x.observe(0).value==65536);CHECK(x.observe(1).value==65537);}
TEST(sequence_duplicates_and_old){Sequence16Extender x;CHECK(x.observe(1).accepted);CHECK(!x.observe(1).accepted);CHECK(!x.observe(0).accepted);CHECK(!x.observe(32769).accepted);CHECK(x.observe(4).gap==2);}
TEST(sequence_multiple_wraps){Sequence16Extender x;for(std::uint64_t i=0;i<200000;++i){auto r=x.observe(static_cast<std::uint16_t>(i));CHECK(r.accepted&&r.value==i);}}
TEST(clock_common_origin){RemotePlayClock c;c.reset(1000000,60);auto v=c.video(123,1300000);CHECK(v&&v->pts100ns==300000);CHECK(c.audioPts(480,48000,1100000)==200000);CHECK(v->provenance==TimestampProvenance::LocalEstimated);}
TEST(clock_cadence_no_cumulative_rounding){RemotePlayClock c;c.reset(0,60);for(std::uint64_t i=0;i<60000;++i){const auto time=static_cast<std::int64_t>(i*10000000/60);auto s=c.video(i,time);CHECK(s&&s->pts100ns==time);}}
TEST(clock_gap_reanchors){RemotePlayClock c;c.reset(0,60);CHECK(c.video(0,0));auto s=c.video(60,10000000);CHECK(s&&s->discontinuity&&s->pts100ns==10000000);}
TEST(clock_long_session_drift_and_fg){
    for(unsigned fps:{30u,60u})for(int ppm:{-1400,1400}){
        RemotePlayClock c;c.reset(0,fps);
        veyra::engine::PresentationScheduler schedule;
        veyra::engine::FgRecoveryBudget budget;
        const int64_t interval=10000000/fps;
        schedule.reset(1,0,0,interval);
        int64_t previous=-1;unsigned admitted=0,measured=0;
        for(uint64_t i=0;i<fps*600u;++i){
            const auto base=int64_t(i*uint64_t(1000000+ppm)*10/fps);
            const auto jitter=i%97==11?20000:int64_t(i%7)*1000;
            const auto arrival=base+jitter;
            const auto stamp=c.video(i,arrival);
            CHECK(stamp&&!stamp->discontinuity&&stamp->pts100ns>previous);previous=stamp->pts100ns;
            budget.fgCost(2,arrival);budget.complete(12,true,false,arrival);
            const bool admittedNow=budget.admit(arrival+20000,schedule.deadline(stamp->pts100ns-interval/2),2,.5);
            if(i>fps*10u){CHECK(std::abs(stamp->pts100ns-base)<150000);++measured;admitted+=admittedNow;}
        }
        CHECK(admitted>measured*98/100);
    }
}
TEST(clock_reject_invalid){RemotePlayClock c;CHECK(!c.video(0,0));THROWS(c.reset(0,120));THROWS(c.reset(-1,60));c.reset(10,60);CHECK(!c.video(0,9));CHECK(c.video(1,20));CHECK(!c.video(1,20));CHECK(!c.video(2,19));CHECK(!c.audioPts(0,0,10));CHECK(!c.audioPts(std::numeric_limits<std::uint64_t>::max(),8000,10));}
TEST(queue_limits_validation){QueueLimits l;l.maxFrames=0;THROWS(VideoIngress{l});l={};l.maxConfigBytes=l.maxBytes+1;THROWS(VideoIngress{l});l={};l.maxAge100ns=0;THROWS(VideoIngress{l});}
TEST(config_not_counted_as_picture){VideoIngress q;q.begin(1);CHECK(q.push(config(),0)==PushStatus::ConfigStored);CHECK(q.stats().accessUnits==0);CHECK(q.stats().depth==0);CHECK(!q.tryPop(0));}
TEST(queue_starts_at_keyframe){VideoIngress q;q.begin(1);q.push(config(),0);CHECK(q.push(frame(1,false),0)==PushStatus::WaitingForIdr);CHECK(q.push(frame(2,true),0)==PushStatus::Accepted);auto v=q.tryPop(0);CHECK(v&&v->configBefore&&v->resetDecoder);CHECK(q.stats().accepted==1);}
TEST(queue_fifo_not_latest_compressed){VideoIngress q;start(q);q.push(frame(1,false),0);q.push(frame(2,false),0);for(std::uint16_t i=0;i<3;++i){auto s=q.tryPop(0);CHECK(s&&s->sample.wireFrameIndex==i);CHECK(static_cast<bool>(s->configBefore)==(i==0));}}
TEST(queue_overflow_requires_idr){QueueLimits l;l.maxFrames=2;VideoIngress q(l);start(q);q.push(frame(1,false),0);CHECK(q.push(frame(2,false),0)==PushStatus::WaitingForIdr);CHECK(q.stats().depth==0);CHECK(q.stats().waitingForIdr);CHECK(q.push(frame(3,false),0)==PushStatus::WaitingForIdr);CHECK(q.push(frame(4,true),0)==PushStatus::Accepted);auto s=q.tryPop(0);CHECK(s&&s->resetDecoder&&s->configBefore);CHECK(s->epoch==2);}
TEST(queue_overflow_incoming_idr){QueueLimits l;l.maxFrames=1;VideoIngress q(l);start(q);CHECK(q.push(frame(1,true),0)==PushStatus::Accepted);auto s=q.tryPop(0);CHECK(s&&s->sample.wireFrameIndex==1&&s->resetDecoder);}
TEST(queue_age_recovery){VideoIngress q;start(q);CHECK(!q.tryPop(1000001));CHECK(q.stats().waitingForIdr&&q.stats().depth==0);}
TEST(queue_frame_stamped_after_reader_clock){
    // The reader samples its clock before it takes the lock. A frame stamped and queued in
    // between is new: it is handed out and the queue is not reset.
    VideoIngress q;q.begin(1);q.push(config(1,1000),1000);CHECK(q.push(frame(0,true,1000),1000)==PushStatus::Accepted);
    auto s=q.tryPop(999);CHECK(s&&s->sample.wireFrameIndex==0);CHECK(q.stats().recoveries==0&&!q.stats().waitingForIdr);
    // A frame that waited longer than the limit still resets the queue.
    CHECK(q.push(frame(1,false,2000),2000)==PushStatus::Accepted);CHECK(!q.tryPop(2000+1000001));CHECK(q.stats().recoveries==1&&q.stats().waitingForIdr);
}
TEST(queue_idr_coalescing){VideoIngress q;q.begin(1);q.push(config(),0);q.push(frame(0,false),0);CHECK(q.takeIdrRequest(0));q.push(frame(1,false),0);CHECK(!q.takeIdrRequest(100000));CHECK(q.takeIdrRequest(5000000));CHECK(!q.takeIdrRequest(10000000));}
TEST(queue_config_change_clears_history){VideoIngress q;start(q);auto c=config();c.width=1280;c.height=720;q.push(std::move(c),0);CHECK(q.stats().depth==0&&q.stats().epoch==2);auto f=frame(1,true);f.width=1280;f.height=720;CHECK(q.push(std::move(f),0)==PushStatus::Accepted);CHECK(q.tryPop(0)->configBefore->width==1280);}
TEST(queue_identical_config_no_reset){VideoIngress q;start(q);q.push(config(),0);CHECK(q.stats().depth==1&&q.stats().epoch==1);}
TEST(queue_wrong_generation){VideoIngress q;start(q);CHECK(q.push(frame(1,false,0,2),0)==PushStatus::WrongGeneration);CHECK(q.stats().depth==1);}
TEST(queue_reference_recovery_not_keyframe){VideoIngress q;start(q);q.tryPop(0);auto f=frame(1,false);f.referenceRecovered=true;CHECK(q.push(std::move(f),0)==PushStatus::WaitingForIdr);CHECK(q.stats().waitingForIdr);}
TEST(queue_frame_gap_resets){VideoIngress q;start(q);q.tryPop(0);CHECK(q.push(frame(3,false),0)==PushStatus::WaitingForIdr);CHECK(q.stats().epoch==2);}
TEST(queue_reject_injected_config_picture){VideoIngress q;q.begin(1);auto f=frame(0,true);f.kind=SampleKind::CodecConfig;CHECK(q.push(std::move(f),0)==PushStatus::Malformed);}
TEST(queue_close_unblocks){VideoIngress q;q.begin(1);std::atomic<bool> result{true};std::thread t([&]{result=q.waitForData(std::chrono::seconds(2));});q.close();t.join();CHECK(!result);CHECK(q.push(config(),0)==PushStatus::Closed);}
TEST(queue_bytes_bound){QueueLimits l;l.maxBytes=1024;l.maxConfigBytes=64;VideoIngress q(l);start(q);std::vector<std::uint8_t>b(1000,0x88);b[0]=0;b[1]=0;b[2]=1;b[3]=0x41;auto f=frame(1,false);f.payload=PaddedBytes::copy(b);q.push(std::move(f),0);CHECK(q.stats().bytes<=1024);CHECK(q.stats().highWaterBytes<=1024);}
TEST(queue_malformed_time){VideoIngress q;start(q);CHECK(q.push(frame(1,false,10),9)==PushStatus::Malformed);CHECK(q.stats().waitingForIdr);}
TEST(queue_concurrent_bound){VideoIngress q;q.begin(1);q.push(config(),0);std::atomic<bool> done=false,ok=true;std::thread producer([&]{for(std::uint16_t i=0;i<20000;++i){q.push(frame(i,i%10==0),0);auto s=q.stats();if(s.depth>4||s.bytes>8u*1024u*1024u)ok=false;}done=true;});std::thread consumer([&]{while(!done){(void)q.tryPop(0);std::this_thread::yield();}});producer.join();consumer.join();CHECK(ok);}
TEST(audio_sample_count_per_channel){auto b=audio();CHECK(b.valid());CHECK(b.frames()==480);CHECK(b.samples.size()*sizeof(std::int16_t)==1920);}
TEST(audio_overflow_discontinuous){AudioIngress q(20);q.begin(1);q.push(audio(0));q.push(audio(480));q.push(audio(960));CHECK(q.stats().dropped==1);CHECK(q.stats().bufferedMs==20);auto b=q.tryPop();CHECK(b&&b->firstSample==480&&b->discontinuity);}
TEST(audio_out_of_order){AudioIngress q;q.begin(1);CHECK(q.push(audio(480)));CHECK(!q.push(audio(0)));CHECK(q.stats().rejected==1);}
TEST(audio_reject_odd_channels){AudioIngress q;q.begin(1);auto b=audio();b.samples.pop_back();CHECK(!q.push(std::move(b)));CHECK(!q.push(audio(0,10000)));}
TEST(audio_format_change){AudioIngress q;q.begin(1);q.push(audio());auto b=audio();b.rate=24000;b.channels=1;b.samples.resize(240);CHECK(q.push(std::move(b)));auto a=q.tryPop();CHECK(a&&a->rate==24000&&a->channels==1&&a->discontinuity);}
TEST(audio_close_and_generation){AudioIngress q;q.begin(1);CHECK(!q.push(audio(0,480,2)));q.close();CHECK(!q.push(audio()));CHECK(!q.tryPop());}
TEST(mailbox_latest_owned){LatestMailbox<std::shared_ptr<int>> m;m.begin(1);auto a=std::make_shared<int>(1);std::weak_ptr<int>w=a;m.publish({1,1,a});a.reset();m.publish({1,2,std::make_shared<int>(2)});CHECK(w.expired());auto s=m.take();CHECK(s&&*s->value==2);CHECK(m.overwritten()==1);CHECK(!m.take());}
TEST(mailbox_generation_and_order){LatestMailbox<int> m;m.begin(1);CHECK(!m.publish({2,1,1}));CHECK(m.publish({1,2,2}));CHECK(!m.publish({1,1,1}));CHECK(!m.publish({1,2,22}));m.close();CHECK(!m.publish({1,3,3}));CHECK(!m.take());}
struct FakeCodec {std::vector<SendStatus> sends;std::vector<ReceiveStatus> receives;std::vector<int> seen;std::size_t si=0,ri=0;SendStatus send(const int& n){seen.push_back(n);CHECK(si<sends.size());return sends[si++];}ReceiveStatus receive(){CHECK(ri<receives.size());return receives[ri++];}};
TEST(pump_retries_same_packet){PacketPump<int> p;FakeCodec c{{SendStatus::NeedDrain,SendStatus::Accepted},{ReceiveStatus::Frame,ReceiveStatus::NeedInput,ReceiveStatus::Frame,ReceiveStatus::NeedInput},{},0,0};CHECK(p.submit(42));CHECK(p.step(c)==PumpStatus::Idle);CHECK((c.seen==std::vector<int>{42,42}));CHECK(p.stats().accepted==1&&p.stats().frames==2&&p.stats().eagain==1);}
TEST(pump_double_eagain_rejected){PacketPump<int> p;FakeCodec c{{SendStatus::NeedDrain},{ReceiveStatus::NeedInput},{},0,0};p.submit(7);CHECK(p.step(c)==PumpStatus::ProtocolViolation);CHECK(p.stats().accepted==0&&p.hasPending());}
TEST(pump_budget_retains_pending){PacketPump<int> p;FakeCodec c{{SendStatus::NeedDrain,SendStatus::Accepted},{ReceiveStatus::Frame,ReceiveStatus::NeedInput,ReceiveStatus::NeedInput},{},0,0};p.submit(9);CHECK(p.step(c,1)==PumpStatus::Yield&&p.hasPending());CHECK(!p.submit(10));CHECK(p.step(c)==PumpStatus::Idle);CHECK(c.seen.back()==9);}
TEST(pump_error_not_accepted){PacketPump<int> p;FakeCodec c{{SendStatus::Error},{},{},0,0};p.submit(1);CHECK(p.step(c)==PumpStatus::Error);CHECK(p.stats().accepted==0);CHECK(!p.submit(2));p.reset();CHECK(p.submit(2));}
TEST(pump_drain_all_frames){PacketPump<int> p;FakeCodec c{{SendStatus::Accepted},{ReceiveStatus::Frame,ReceiveStatus::Frame,ReceiveStatus::Frame,ReceiveStatus::NeedInput},{},0,0};p.submit(1);CHECK(p.step(c)==PumpStatus::Idle);CHECK(p.stats().frames==3&&p.drained());}
TEST(pump_end_not_success){PacketPump<int> p;FakeCodec c{{SendStatus::End},{},{},0,0};p.submit(1);CHECK(p.step(c)==PumpStatus::End&&p.stats().accepted==0);CHECK(!p.submit(2));}
TEST(input_focus_release){InputGate g;g.begin(1);ControllerState s;s.buttons=ControllerState::Cross;CHECK(!g.update(1,s,10));g.setFocused(true);CHECK(g.update(1,s,10));CHECK(g.sample(1,11)==s);g.setFocused(false);CHECK(g.sample(1,12)==ControllerState{});}
TEST(input_stale_and_new_session){InputGate g;g.begin(1);g.setFocused(true);ControllerState s;s.l2=255;g.update(1,s,10);CHECK(g.sample(1,1000011)==ControllerState{});CHECK(g.sample(2,11)==ControllerState{});g.begin(2);g.setFocused(true);CHECK(!g.update(1,s,20));CHECK(g.sample(2,20)==ControllerState{});}
TEST(inbox_connected_is_not_streaming){SessionInbox q;auto t=q.begin(monotonic100ns());CHECK(q.snapshot().state==SessionState::Connecting);t.connected();CHECK(q.snapshot().state==SessionState::WaitingFirstFrame);CHECK(q.decodedFrameReady(t.generation()));CHECK(q.snapshot().state==SessionState::Streaming);}
TEST(inbox_reject_stale_callbacks){SessionInbox q;auto a=q.begin(monotonic100ns());q.invalidate();q.finishStop();auto b=q.begin(monotonic100ns());a.connected();CHECK(q.snapshot().state==SessionState::Connecting);CHECK(!a.video(config(a.generation(),monotonic100ns())));b.connected();CHECK(q.snapshot().state==SessionState::WaitingFirstFrame);CHECK(q.snapshot().staleCallbacks>=2);}
TEST(inbox_weak_token_after_destruction){std::optional<SessionInbox::Token> t;{SessionInbox q;t=q.begin(monotonic100ns());}CHECK(!t->video(config(t->generation(),monotonic100ns())));t->connected();t->failed(99);}
TEST(inbox_failure_closes_media){SessionInbox q;auto t=q.begin(monotonic100ns());CHECK(t.video(config(t.generation(),monotonic100ns())));t.failed(32);CHECK(q.snapshot().state==SessionState::Failed);CHECK(!q.isCurrent(t.generation()));CHECK(!q.tryVideo(monotonic100ns()));CHECK(q.snapshot().errorCode==32);}
TEST(inbox_begin_and_stop_guards){SessionInbox q;THROWS(q.begin(-1));auto t=q.begin(monotonic100ns());THROWS(q.begin(monotonic100ns()));THROWS(q.finishStop());q.invalidate();THROWS(q.begin(monotonic100ns()));q.finishStop();CHECK(q.begin(monotonic100ns()).generation()>t.generation());}
TEST(inbox_callbacks_race_invalidate){SessionInbox q;auto t=q.begin(monotonic100ns());std::atomic<bool> finish=false;std::thread worker([&]{while(!finish){t.connected();(void)t.audio(audio(0,480,t.generation()));}});q.invalidate();finish=true;worker.join();CHECK(!q.isCurrent(t.generation()));CHECK(!q.tryAudio());q.finishStop();CHECK(q.snapshot().state==SessionState::Idle);}
TEST(inbox_failed_requires_backend_join){SessionInbox q;auto t=q.begin(monotonic100ns());t.failed(42);THROWS(q.begin(monotonic100ns()));q.invalidate();q.finishStop();CHECK(q.begin(monotonic100ns()).generation()>t.generation());}
TEST(input_and_mailbox_zero_generation){InputGate g;THROWS(g.begin(0));LatestMailbox<int> m;THROWS(m.begin(0));VideoIngress v;THROWS(v.begin(0));AudioIngress a;THROWS(a.begin(0));}
TEST(inbox_frame_stamped_after_reader_clock){
    // The same through the session boundary, in the product's order: the reader's clock
    // sample, then the callback stamps and queues a frame, then the reader takes the lock.
    SessionInbox q;auto t=q.begin(monotonic100ns());const auto readerNow=monotonic100ns()-1;
    CHECK(t.video(config(t.generation(),monotonic100ns())));CHECK(t.video(frame(0,true,monotonic100ns(),t.generation())));
    auto v=q.tryVideo(readerNow);CHECK(v&&v->sample.wireFrameIndex==0);CHECK(q.snapshot().video.recoveries==0&&!q.snapshot().video.waitingForIdr);
}
TEST(inbox_real_sample_vs_config_counts){SessionInbox q;auto t=q.begin(monotonic100ns());auto now=monotonic100ns();CHECK(t.video(config(t.generation(),now)));CHECK(q.snapshot().video.accessUnits==0);CHECK(t.video(frame(0,true,monotonic100ns(),t.generation())));CHECK(q.snapshot().video.accessUnits==1);auto v=q.tryVideo(monotonic100ns());CHECK(v&&v->sample.generation==t.generation());}
}
TEST(recovery_keyframes_before_reconnect){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(100);r.frame(200);
    CHECK(r.poll(1199,false,false)==A::Wait);CHECK(r.poll(1200,false,false)==A::Keyframe);
    CHECK(r.poll(3199,false,false)==A::Wait);CHECK(r.poll(3200,false,false)==A::Keyframe);
    CHECK(r.poll(6199,false,false)==A::Wait);CHECK(r.poll(6200,false,false)==A::Reconnect);CHECK(r.reconnects()==1);
}
TEST(recovery_initial_and_login_deadlines){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(100);
    CHECK(r.poll(30099,false,false)==A::Wait);CHECK(r.poll(30100,false,false)==A::Fail);CHECK(r.reconnects()==0);
    r.frame(50000);CHECK(r.poll(169999,true,true,false)==A::Wait);CHECK(r.poll(170000,true,true,false)==A::Fail);
    CHECK(r.reconnects()==0);
}
TEST(recovery_budget_survives_brief_recovery){
    StreamRecovery r;using A=StreamRecovery::Action;
    for(int i=0;i<3;++i){r.beginAttempt(i*10000);r.frame(i*10000+100);CHECK(r.poll(i*10000+6100,false,false)==A::Reconnect);CHECK(r.reconnects()==unsigned(i+1));}
    r.beginAttempt(30000);r.frame(30100);CHECK(r.poll(36100,false,false)==A::Fail);CHECK(r.reconnects()==3);
}
TEST(recovery_progress_and_terminal_quit){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);r.frame(100);
    CHECK(r.poll(1100,false,false)==A::Keyframe);r.frame(1500);CHECK(r.poll(2000,false,false)==A::Wait);
    CHECK(r.poll(2200,false,true,false)==A::Fail);CHECK(r.reconnects()==0);
    CHECK(r.poll(2200,false,true,true)==A::Reconnect);r.beginAttempt(5000);
    CHECK(r.poll(11000,false,false)==A::Wait);CHECK(r.poll(34999,false,false)==A::Wait);CHECK(r.poll(35000,false,false)==A::Reconnect);
}
TEST(recovery_retryable_busy_requires_prior_video){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);
    CHECK(r.poll(500,false,true,true)==A::Fail);CHECK(r.reconnects()==0);
    r.frame(1000);CHECK(r.poll(7000,false,false)==A::Reconnect);r.beginAttempt(8000);
    CHECK(r.poll(8500,false,true,true)==A::Reconnect);CHECK(r.reconnects()==2);
}
TEST(recovery_manual_busy_retry_is_bounded){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);
    for(int i=0;i<3;++i){CHECK(r.poll(i*2000+500,false,true,true,true)==A::Reconnect);r.beginAttempt((i+1)*2000);}
    CHECK(r.poll(6500,false,true,true,true)==A::Fail);CHECK(r.reconnects()==3);
    StreamRecovery denied;denied.beginAttempt(0);
    CHECK(denied.poll(500,false,true,false,true)==A::Fail);CHECK(denied.reconnects()==0);
}
TEST(recovery_busy_then_slow_handshake){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);
    CHECK(r.poll(500,false,true,true,true)==A::Reconnect);r.beginAttempt(2000);
    CHECK(r.poll(8000,false,false)==A::Wait);CHECK(r.poll(31999,false,false)==A::Wait);
    r.frame(31999);CHECK(r.poll(32999,false,false)==A::Keyframe);
    CHECK(r.poll(37999,false,false)==A::Reconnect);
}
TEST(recovery_stable_progress_renews_outage_budget_not_identity){
    StreamRecovery r;using A=StreamRecovery::Action;
    for(int i=0;i<3;++i){r.beginAttempt(i*10000);r.frame(i*10000+100);CHECK(r.poll(i*10000+6100,false,false)==A::Reconnect);}
    r.beginAttempt(30000);CHECK(!r.frame(30100));
    for(int64_t t=30200;t<60100;t+=100)CHECK(!r.frame(t));
    CHECK(r.episodeRetries()==3);CHECK(r.frame(60100));CHECK(r.episodeRetries()==0);CHECK(r.reconnects()==3);
    CHECK(!r.frame(60200));CHECK(r.poll(66200,false,false)==A::Reconnect);
    CHECK(r.episodeRetries()==1);CHECK(r.reconnects()==4);
}
TEST(recovery_intermittent_frames_do_not_renew_budget){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);r.frame(100);CHECK(r.poll(6100,false,false)==A::Reconnect);r.beginAttempt(7000);
    for(int64_t t=7100;t<150000;t+=1000)CHECK(!r.frame(t));
    CHECK(r.episodeRetries()==1);CHECK(r.reconnects()==1);
}
int main(int argc,char**argv){
    std::string filter;
    if(argc==2&&std::string_view(argv[1])=="--list"){for(auto [name,f]:tests()){(void)f;std::cout<<name<<'\n';}return 0;}
    if(argc==3&&std::string_view(argv[1])=="--case")filter=argv[2];
    else if(argc!=1){std::cerr<<"Usage: core_tests [--list | --case NAME]\n";return 2;}
    unsigned pass=0,fail=0;
    for(auto [name,f]:tests()){
        if(!filter.empty()&&filter!=name)continue;
        try{f();++pass;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++fail;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    if(pass+fail==0){std::cerr<<"No test matched\n";return 2;}
    std::cout<<"TOTAL "<<pass+fail<<" PASS "<<pass<<" FAIL "<<fail<<'\n';
    return fail?1:0;
}
