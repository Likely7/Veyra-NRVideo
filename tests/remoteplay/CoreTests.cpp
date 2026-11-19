// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Types.h"
#include "veyra/remoteplay/Timeline.h"
#include "veyra/remoteplay/VideoIngress.h"
#include "veyra/remoteplay/LatestMailbox.h"
#include "veyra/remoteplay/PacketPump.h"
#include "veyra/remoteplay/InputGate.h"
#include "veyra/remoteplay/SessionInbox.h"
#include "veyra/remoteplay/StreamRecovery.h"
#include "veyra/remoteplay/DualSenseHapticsReport.h"
#include "veyra/remoteplay/HapticsBlockPacer.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
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
// --- DualSense haptics over Bluetooth: report bytes and pacing. No device is opened. ---
namespace {
struct HapticsRandom {
    std::uint64_t state=0x9E3779B97F4A7C15ull;
    std::int64_t range(std::int64_t lo,std::int64_t hi){state=state*6364136223846793005ull+1442695040888963407ull;return lo+static_cast<std::int64_t>((state>>33)%static_cast<std::uint64_t>(hi-lo+1));}
};
// A sender without reserve control, for comparison: starts 20 ms after the first chunk and
// always takes 32 frames.
struct FixedBlockPacer {
    bool flowing_=false;
    void reset(){flowing_=false;}
    HapticsBlockPacer::Step next(unsigned queued,unsigned sinceLastPushMs,unsigned waitedMs){
        HapticsBlockPacer::Step step;
        if(!flowing_){if(!queued||(queued<96&&waitedMs<20))return step;flowing_=true;}
        if(queued<32){if(sinceLastPushMs<40){step.starved=true;return step;}if(!queued){flowing_=false;return step;}}
        step.send=true;step.take=std::min(queued,32u);return step;
    }
};
struct HapticsScenario {
    double seconds=120;
    double driftPpm=0;            // console clock against the PC clock
    double stallEverySeconds=0;   // UI stalls of 25..45 ms about this often (0 = never)
    double onMs=0,offMs=0;        // effect pattern; 0/0 = continuous
    bool silenceBetween=false;    // between effects the console sends silence (true) or nothing
};
struct HapticsOutcome {
    std::uint64_t reports=0;      // every report written, with audio or with silence
    std::uint64_t audioReports=0; // reports that carried queued audio
    std::uint64_t late=0;         // audio reports written more than 1.5 ms behind their schedule
    std::uint64_t waits=0;        // times a report had to wait for audio (the sender counts the same)
    std::uint64_t loudProduced=0,loudSent=0,loudDropped=0,loudDiscarded=0; // audible frames
    std::uint64_t stretched=0,squeezed=0,stalls=0,effects=0;
    unsigned levelMax=0,takeMax=0,loudRemaining=0;
};
// The sender loop of DualSenseBtHaptics (ControllerInput.cpp) with the audio delivered as in
// the product: 10 ms chunks from the console with network jitter, handed over in one batch
// per tick by a UI timer that ticks every 8 ms and now and then stalls, and a report due
// every 10.667 ms. Time in microseconds, 0.1 ms steps.
template<class Pacer> HapticsOutcome simulateHaptics(const HapticsScenario& s,std::uint64_t seed){
    struct Run{unsigned frames;bool loud;};   // queued audio, oldest first
    struct Chunk{std::int64_t at;bool loud;}; // on the way from the console
    HapticsRandom random;random.state^=seed*0x2545F4914F6CDD1Dull;
    HapticsOutcome r;Pacer pacer;
    const std::int64_t period=10667,end=static_cast<std::int64_t>(s.seconds*1e6),cycle=static_cast<std::int64_t>((s.onMs+s.offMs)*1000);
    const double chunkUs=10000.0*(1.0-s.driftPpm*1e-6);
    std::deque<Chunk> onTheWay;std::deque<Run> fifo;
    double nextChunkAt=0;
    std::int64_t effectClock=0,nextTick=0,lastPush=-1000000,lastLoudPush=-1000000,fifoSince=0,senderAt=0,next=0,lastLoud=0;
    unsigned queued=0;bool streaming=false,wasStarved=false,wasOn=false;
    const auto sinceMs=[](std::int64_t from,std::int64_t to){return static_cast<unsigned>(std::min<std::int64_t>((to-from)/1000,100000));};
    const auto loudQueued=[&]{unsigned frames=0;for(const auto& run:fifo)if(run.loud)frames+=run.frames;return frames;};
    for(std::int64_t now=0;now<end;now+=100){
        while(nextChunkAt<=static_cast<double>(now)){
            const bool on=cycle<=0||static_cast<double>(effectClock%cycle)<s.onMs*1000;
            if(on&&!wasOn)++r.effects;
            wasOn=on;
            if(on||s.silenceBetween){onTheWay.push_back({static_cast<std::int64_t>(nextChunkAt)+random.range(0,2000),on});if(on)r.loudProduced+=30;}
            nextChunkAt+=chunkUs;effectClock+=10000;
        }
        if(now>=nextTick){
            // push(): one batch per tick. Silence is not queued while nothing plays.
            std::size_t chunks=0;bool loud=false;
            while(chunks<onTheWay.size()&&onTheWay[chunks].at<=now){loud=loud||onTheWay[chunks].loud;++chunks;}
            if(chunks&&(loud||streaming||queued)){
                if(!queued)fifoSince=now;
                for(std::size_t i=0;i<chunks;++i){fifo.push_back({30u,onTheWay[i].loud});queued+=30;}
                lastPush=now;if(loud)lastLoudPush=now;
                while(queued>384){ // the sender fell far behind: the oldest audio goes
                    auto& oldest=fifo.front();const unsigned drop=std::min(queued-384,oldest.frames);
                    if(oldest.loud)r.loudDropped+=drop;
                    oldest.frames-=drop;queued-=drop;if(!oldest.frames)fifo.pop_front();
                }
            }
            onTheWay.erase(onTheWay.begin(),onTheWay.begin()+static_cast<std::ptrdiff_t>(chunks));
            nextTick=now+8000+random.range(-500,500);
            if(s.stallEverySeconds>0&&random.range(0,static_cast<std::int64_t>(s.stallEverySeconds*125)-1)==0){nextTick+=random.range(25000,45000);++r.stalls;}
        }
        if(now<senderAt)continue;
        // run(): one pass of the sender loop.
        const auto step=pacer.next(queued,sinceMs(lastPush,now),sinceMs(fifoSince,now));
        r.takeMax=std::max(r.takeMax,step.take);
        if(lastLoudPush>lastLoud)lastLoud=lastLoudPush;
        if(!streaming){
            if(!step.send){senderAt=now+(queued?4000:1000);continue;}
            streaming=true;wasStarved=false;next=now;if(lastLoud<now)lastLoud=now;
        }
        if(step.starved){
            if(!wasStarved){wasStarved=true;++r.waits;}
            if(now-next>60000){next=now;wasStarved=false;}
            senderAt=now+2000;continue;
        }
        wasStarved=false;
        bool loudBlock=false;
        if(step.send){
            r.levelMax=std::max(r.levelMax,queued);
            for(unsigned left=std::min(step.take,queued);left;){
                auto& run=fifo.front();const unsigned frames=std::min(left,run.frames);
                if(run.loud){loudBlock=true;r.loudSent+=frames;}
                run.frames-=frames;left-=frames;queued-=frames;if(!run.frames)fifo.pop_front();
            }
            ++r.audioReports;
            if(step.take==31)++r.stretched;else if(step.take==33)++r.squeezed;
            if(now-next>1500)++r.late;
        }
        ++r.reports; // without audio the report carries silence and keeps the cadence
        if(loudBlock)lastLoud=now;
        if(now-lastLoud>250000){
            // 250 ms after the last audible audio the stream stops and the queue is cleared.
            if(lastLoudPush>lastLoud)lastLoud=lastLoudPush;
            else{streaming=false;pacer.reset();r.loudDiscarded+=loudQueued();fifo.clear();queued=0;senderAt=now+1000;continue;}
        }
        next+=period;
        if(now-next>period*3)next=now;
        senderAt=std::max(now+100,next);
    }
    r.loudRemaining=loudQueued();
    for(const auto& chunk:onTheWay)if(chunk.loud)r.loudRemaining+=30;
    return r;
}
// Every audible frame the console produced was sent or is still on its way.
bool hapticsNothingLost(const HapticsOutcome& r){return r.loudDropped==0&&r.loudDiscarded==0&&r.loudSent+r.loudRemaining==r.loudProduced&&r.takeMax<=HapticsBlockPacer::kBlock+1;}
}
TEST(haptics_report_layout_and_crc){
    const std::uint8_t seed=0xa2;
    CHECK(~dualSenseCrc32(0xffffffffu,&seed,1)==0xEADA2D49u);
    // Left = 1000*i - 16000, right = -left; sequence 3, counter 7, buffer depth 0x20. The
    // expected bytes were computed apart from this code (Python, zlib.crc32).
    std::array<std::int16_t,kDualSenseHapticsFrames*2> pcm{};
    for(std::size_t i=0;i<kDualSenseHapticsFrames;++i){pcm[i*2]=static_cast<std::int16_t>(1000*static_cast<int>(i)-16000);pcm[i*2+1]=static_cast<std::int16_t>(16000-1000*static_cast<int>(i));}
    std::array<std::uint8_t,kDualSenseHapticsReportSize> report{};
    CHECK(buildDualSenseHapticsReport(report,pcm.data(),3,7,0x20));
    const std::array<std::uint8_t,16> head{0x32,0x30,0x91,0x07,0xfe,0x00,0x00,0x00,0x00,0x20,0x07,0x92,0x40,0xc1,0x3f,0xc5};
    CHECK(std::equal(head.begin(),head.end(),report.begin()));
    const std::array<std::uint8_t,4> crc{0x6f,0x94,0x5c,0x01};
    CHECK(std::equal(crc.begin(),crc.end(),report.begin()+138));
    for(std::size_t i=77;i<138;++i)CHECK(report[i]==0);
    CHECK(buildDualSenseHapticsReport(report,pcm.data(),0,0));CHECK(report[9]==kDualSenseHapticsBufferDepth);
}
TEST(haptics_report_rounding_and_silence){
    // Values one step from zero are not "loud": the console sends such noise between effects.
    std::array<std::int16_t,kDualSenseHapticsFrames*2> quiet{};quiet[5]=300;quiet[9]=-300;quiet[11]=127;quiet[12]=128;
    std::array<std::uint8_t,kDualSenseHapticsReportSize> report{};
    CHECK(!buildDualSenseHapticsReport(report,quiet.data(),0,0));
    CHECK(report[13+5]==0x01&&report[13+9]==0xff&&report[13+11]==0x00&&report[13+12]==0x01);
    CHECK(dualSenseNarrowSample(32767)==127&&dualSenseNarrowSample(-32768)==-128&&dualSenseNarrowSample(-128)==-1&&dualSenseNarrowSample(-127)==0);
    CHECK(!dualSenseAudible(383)&&dualSenseAudible(384)&&!dualSenseAudible(-383)&&dualSenseAudible(-384));
    quiet[20]=600;CHECK(buildDualSenseHapticsReport(report,quiet.data(),0,0));
}
TEST(haptics_fit_block){
    // A ramp of 310 per frame on the left, its mirror on the right.
    std::array<std::int16_t,33*2> ramp{};
    for(std::size_t i=0;i<33;++i){ramp[i*2]=static_cast<std::int16_t>(static_cast<int>(i)*310);ramp[i*2+1]=static_cast<std::int16_t>(-static_cast<int>(i)*310);}
    std::array<std::int16_t,32*2> out{};
    hapticsFitBlock(ramp.data(),32,out.data());CHECK(std::equal(out.begin(),out.end(),ramp.begin()));
    for(const int count:{31,33}){
        hapticsFitBlock(ramp.data(),static_cast<unsigned>(count),out.data());
        // Linear interpolation of a ramp is the ramp itself over the new length: output frame
        // i sits at source position i*(count-1)/31. First and last frame are kept, so
        // consecutive blocks join without a step.
        for(int i=0;i<32;++i){
            const int expected=(310*i*(count-1)+15)/31;
            CHECK(out[static_cast<std::size_t>(i)*2]==expected&&out[static_cast<std::size_t>(i)*2+1]==-expected);
        }
        CHECK(out[62]==ramp[static_cast<std::size_t>(count-1)*2]);
    }
    hapticsFitBlock(ramp.data(),10,out.data());
    CHECK(std::equal(out.begin(),out.begin()+20,ramp.begin()));for(std::size_t i=20;i<64;++i)CHECK(out[i]==0);
}
TEST(haptics_pacer_continuous_stream){
    for(std::uint64_t seed=1;seed<=2;++seed){
        const auto r=simulateHaptics<HapticsBlockPacer>({120,0,0,0,0,false},seed);
        // Nothing waits and the queue stays near its starting reserve.
        CHECK(hapticsNothingLost(r));CHECK(r.late==0&&r.waits==0&&r.levelMax<=200);
    }
}
TEST(haptics_pacer_follows_clock_drift){
    for(const double ppm:{300.0,-300.0}){
        const auto r=simulateHaptics<HapticsBlockPacer>({240,ppm,0,0,0,false},1);
        CHECK(hapticsNothingLost(r));CHECK(r.late==0&&r.waits==0&&r.levelMax<=330);CHECK(ppm>0?r.squeezed>0:r.stretched>0);
    }
    // Without reserve control a console clock that runs slow drains the queue to its edge,
    // and from then on most reports wait for audio; a fast one overflows the queue (after
    // about six minutes at 300 ppm). This is the failure the simulation has to be able to show.
    const auto slow=simulateHaptics<FixedBlockPacer>({240,-300,0,0,0,false},1),fast=simulateHaptics<FixedBlockPacer>({480,300,0,0,0,false},1);
    CHECK(slow.late*2>slow.audioReports);CHECK(fast.loudDropped>0);
}
TEST(haptics_pacer_ui_stalls){
    for(std::uint64_t seed=1;seed<=2;++seed){
        const auto r=simulateHaptics<HapticsBlockPacer>({120,0,3,0,0,false},seed);
        // A stall costs a wait once. After it the queue holds enough to ride out the next
        // ones, and it stays bounded.
        CHECK(hapticsNothingLost(r));CHECK(r.stalls>10&&r.waits*5<=r.stalls);CHECK(r.levelMax<=330);
    }
}
TEST(haptics_pacer_effects_and_clicks){
    for(std::uint64_t seed=1;seed<=2;++seed){
        // 700 ms effects with 400 ms pauses. When nothing arrives between them, at most the
        // last partial block of each effect waits; with silence in between nothing waits.
        const auto effects=simulateHaptics<HapticsBlockPacer>({120,0,0,700,400,false},seed);
        CHECK(hapticsNothingLost(effects));CHECK(effects.waits<=effects.effects+4);
        const auto joined=simulateHaptics<HapticsBlockPacer>({120,0,0,700,400,true},seed);
        CHECK(hapticsNothingLost(joined));CHECK(joined.waits==0&&joined.late==0);
        // 20 ms clicks: sent whole and without waiting, whether the console goes quiet or
        // keeps sending silence after them.
        for(const bool silence:{false,true}){
            const auto clicks=simulateHaptics<HapticsBlockPacer>({60,0,0,20,480,silence},seed);
            CHECK(hapticsNothingLost(clicks));CHECK(clicks.waits==0&&clicks.late==0);
        }
    }
}
TEST(haptics_idle_pad_gets_no_reports){
    // Silence only: nothing is queued and no report is written.
    const auto silent=simulateHaptics<HapticsBlockPacer>({30,0,0,0,1000,true},1);
    CHECK(silent.reports==0&&silent.loudProduced==0);
    // A 20 ms click every two seconds with silence in between: each click costs its own
    // reports plus 250 ms of silence, then the link rests. Without the stop the same minute
    // would take about 5600 reports.
    const auto clicks=simulateHaptics<HapticsBlockPacer>({60,0,0,20,1980,true},1);
    CHECK(hapticsNothingLost(clicks));CHECK(clicks.effects==30);
    CHECK(clicks.reports>=clicks.effects*23&&clicks.reports<=clicks.effects*34);
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
