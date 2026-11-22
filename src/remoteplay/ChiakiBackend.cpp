// SPDX-License-Identifier: GPL-3.0-only
// Uses chiaki-ng's AGPL-3.0-only + OpenSSL-exception APIs; retain both licenses.
// Windows native initialization and source regression evidence: see execution log.
#include "veyra/remoteplay/ChiakiBackend.h"
#include "veyra/remoteplay/PsnDiagnostics.h"
#include <chiaki/session.h>
#include <chiaki/remote/holepunch.h>
#if !defined(CHIAKI_VEYRA_PSN_CANCEL_API) || CHIAKI_VEYRA_PSN_CANCEL_API != 1
#error The reviewed Chiaki PSN cancellation patch is required
#endif
#if !defined(CHIAKI_VEYRA_VIDEO_METADATA_API) || CHIAKI_VEYRA_VIDEO_METADATA_API != 1
#error The reviewed Chiaki video metadata patch is required
#endif
#include <chiaki/opusdecoder.h>
#include <chiaki/controller.h>
#include <chiaki/orientation.h>
#include <cmath>
#include <chiaki/log.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <sstream>
namespace veyra::remoteplay {
static_assert(CHIAKI_PSN_ACCOUNT_ID_SIZE == 8);
static_assert(CHIAKI_SESSION_AUTH_SIZE == 16);
namespace {
void wipe(void* ptr,std::size_t n) noexcept {
    auto* p=static_cast<volatile unsigned char*>(ptr);
    while(n--) *p++=0;
}
BackendResult result(ChiakiErrorCode code,const char* operation) {
    return {code==CHIAKI_ERR_SUCCESS,static_cast<int>(code),operation};
}
}
SecretString::SecretString(SecretString&& other) noexcept { bytes_.swap(other.bytes_); }
SecretString& SecretString::operator=(SecretString&& other) noexcept {
    if(this!=&other){clear();bytes_.swap(other.bytes_);}return *this;
}
SecretString::~SecretString(){clear();}
void SecretString::clear() noexcept {wipe(bytes_.data(),bytes_.size());bytes_.clear();}
void SecretString::assign(std::string_view value){
    // Allocate before wiping: assign(view()) is safe, and allocation failure preserves the old value.
    std::vector<char> replacement(value.size()+1,'\0');
    std::copy(value.begin(),value.end(),replacement.begin());
    clear();bytes_.swap(replacement);
}
std::string_view SecretString::view() const noexcept {return bytes_.empty()?std::string_view{}:std::string_view(bytes_.data(),bytes_.size()-1);}
const char* SecretString::c_str() const noexcept {return bytes_.empty()?"":bytes_.data();}
std::optional<std::string> NativeConnectRequest::validate() const {
    if(auto error=video.validate())return error;
    if(!psn){
        if(!ps5)return "Local PS4 pairing is not supported";
        if(!validHost(host))return "Invalid console host";
        if(!psnToken.empty()||!psnDuid.empty())return "PSN credentials require PSN transport";
        return {};
    }
    if(psnDuid.size()!=64||!std::all_of(psnDuid.begin(),psnDuid.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}))return "Invalid PSN console identifier";
    if(!ps5&&(video.codec!=Codec::H264||psnDuid!=std::string(64,'0')))return "PS4 requires H264 and the main-console identifier";
    if(ps5&&psnDuid==std::string(64,'0'))return "Select a PS5 console";
    const auto token=psnToken.view();
    if(token.empty()||token.size()>8192||!std::all_of(token.begin(),token.end(),[](unsigned char c){return c>32&&c<127;}))return "Invalid PSN access token";
    if(std::all_of(credentials.accountId.begin(),credentials.accountId.end(),[](auto v){return v==0;}))return "Missing PSN account identifier";
    return {};
}
PairingCredentials::PairingCredentials(PairingCredentials&& other) noexcept { *this=std::move(other); }
PairingCredentials& PairingCredentials::operator=(PairingCredentials&& other) noexcept {
    if(this==&other) return *this;
    registrationKey=other.registrationKey;sessionKey=other.sessionKey;accountId=other.accountId;
    wipe(other.registrationKey.data(),other.registrationKey.size());
    wipe(other.sessionKey.data(),other.sessionKey.size());wipe(other.accountId.data(),other.accountId.size());
    return *this;
}
PairingCredentials::~PairingCredentials(){wipe(registrationKey.data(),registrationKey.size());wipe(sessionKey.data(),sessionKey.size());wipe(accountId.data(),accountId.size());}
BackendResult initializeChiaki(){
    static std::once_flag flag;
    static ChiakiErrorCode code=CHIAKI_ERR_UNKNOWN;
    std::call_once(flag,[]{code=chiaki_lib_init();});
    return result(code,"chiaki_lib_init");
}
struct ChiakiBackend::Impl {
    explicit Impl(SessionInbox::Token t):token(std::move(t)),owner(std::this_thread::get_id()){}
    SessionInbox::Token token;
    std::thread::id owner;
    ChiakiSession session{};
    ChiakiOpusDecoder opus{};
    ChiakiLog log{};
    VideoProfile profile;
    std::string host;
    bool initialized=false,opusInitialized=false;
    ChiakiHolepunchSession holepunch=nullptr; // Owned by session after chiaki_session_init.
    std::atomic<bool> active=false,started=false,connected=false;
    std::atomic<std::uint64_t> warnings=0,errors=0,videoCallbacks=0,audioCallbacks=0;
    std::atomic<int> quitReason=0,apiError=0;
    std::atomic<unsigned> psnEvents=0,psnState=0,psnHttp=0;
    std::atomic<unsigned> psnWsError=0; // operation and CURL code in one snapshot
    std::array<int64_t,4> psnPhaseMs{-1,-1,-1,-1};
    bool psnDiagnostics=false;
    bool standbyRequested=false; // Owner thread only; one control command per session.
    std::atomic<int> standbyResult=-1;
    std::atomic<uint64_t> callbackRejected=0,transportErrors=0,assemblyErrors=0;
    std::atomic<int64_t> serverTargetBitrate=-1;
    std::atomic<uint64_t> qualityReports=0;
    // PS5 手柄控制: set before the session starts, read only by the callbacks.
    bool discardMedia=false;
    // Only the upstream audio/Opus callback thread reads/writes these fields.
    std::uint32_t channels=0,rate=0;
    std::uint64_t sampleIndex=0;
    bool audioDiscontinuity=true;
    ChiakiOrientationTracker orientation{};ChiakiAccelNewZero accelZero{};
    uint64_t lastMotion=0;
    std::mutex feedbackMutex;ControllerFeedback feedback;

    static void logCallback(ChiakiLogLevel level,const char* message,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(message&&level==CHIAKI_LOG_INFO&&self.active.load()){
            const std::string_view text=message;
            if(text=="VEYRA_STANDBY_SENT")self.standbyResult.store(CHIAKI_ERR_SUCCESS);
            else if(text=="VEYRA_STANDBY_FAILED")self.standbyResult.store(CHIAKI_ERR_NETWORK);
            if(text=="Session received entered Login PIN, forwarding to Ctrl")self.token.loginPinNativeProgress(3);
            else if(text=="Ctrl received entered Login PIN, sending to console")self.token.loginPinNativeProgress(4);
            else if(text=="Ctrl received Login message: success")self.token.loginPinAccepted();
        }
        if(self.psnDiagnostics&&message){
            if(const auto event=parsePsnDiagnostic(message)){
                self.psnEvents.fetch_or(1u<<(event->event-1));
                // Progress flags are monotonic; concurrent callbacks can arrive
                // in a different order from their native snapshots.
                self.psnState.fetch_or(event->state);
                if(event->http)self.psnHttp.store(event->http);
            }
            if(const auto error=parsePsnWebsocketError(message))
                self.psnWsError.store((error->operation<<10)|error->curl);
            // This upstream error is a fixed literal, not server content.
            if(std::string_view(message)=="websocket_thread_func: Did not receive PONG in time.")self.psnEvents.fetch_or(1u<<10);
        }
        if(message&&level==CHIAKI_LOG_VERBOSE){
            int target=-1;
            if(std::sscanf(message,"StreamConnection received connection quality: target_bitrate=%d,",&target)==1&&target>=0){self.serverTargetBitrate=target;++self.qualityReports;}
        }
        // Raw upstream log strings/hexdumps may contain keys. Do not forward them.
        if(level==CHIAKI_LOG_ERROR)++self.errors;
        if(level==CHIAKI_LOG_WARNING)++self.warnings;
        // Classify in memory; no upstream strings or hexdumps leave this callback.
        if(message&&(level==CHIAKI_LOG_ERROR||level==CHIAKI_LOG_WARNING)){
            const std::string_view text=message;
            if(text.find("Takion")!=text.npos||text.find("timeout")!=text.npos)++self.transportErrors;
            if(text.find("Video")!=text.npos||text.find("video")!=text.npos||text.find("frame")!=text.npos||text.find("FEC")!=text.npos)++self.assemblyErrors;
        }
    }
    static void eventCallback(ChiakiEvent* event,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(!event||!self.active.load())return;
        switch(event->type){
        case CHIAKI_EVENT_CONNECTED:self.connected=true;self.token.connected();break;
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST:self.token.loginPinRequired(event->login_pin_request.pin_incorrect);break;
        case CHIAKI_EVENT_HOLEPUNCH:self.token.dataChannelConnecting(!event->data_holepunch.finished);break;
        case CHIAKI_EVENT_QUIT:
            self.connected=false;self.quitReason=static_cast<int>(event->quit.reason);
            // A remote termination is not a successful decoded EOF.
            self.token.failed(2000+static_cast<int>(event->quit.reason));break;
        case CHIAKI_EVENT_RUMBLE:{std::lock_guard lock(self.feedbackMutex);self.feedback.rumble=true;self.feedback.left=event->rumble.left;self.feedback.right=event->rumble.right;break;}
        case CHIAKI_EVENT_TRIGGER_EFFECTS:{std::lock_guard lock(self.feedbackMutex);auto& f=self.feedback;f.triggers=true;f.leftTrigger[0]=event->trigger_effects.type_left;f.rightTrigger[0]=event->trigger_effects.type_right;std::copy_n(event->trigger_effects.left,10,f.leftTrigger.begin()+1);std::copy_n(event->trigger_effects.right,10,f.rightTrigger.begin()+1);break;}
        case CHIAKI_EVENT_MOTION_RESET:{std::lock_guard lock(self.feedbackMutex);self.feedback.motionReset=true;break;}
        // Light bar colour and player indicator, as chiaki-ng's StreamSession forwards them.
        case CHIAKI_EVENT_LED_COLOR:{std::lock_guard lock(self.feedbackMutex);auto& f=self.feedback;f.led=true;std::copy_n(event->led_state,3,f.ledColor.begin());break;}
        case CHIAKI_EVENT_PLAYER_INDEX:{std::lock_guard lock(self.feedbackMutex);self.feedback.playerIndex=true;self.feedback.player=event->player_index;break;}
        default:break; // No mic, standby or keyboard side effects.

        }
    }
    static void hapticsCallback(uint8_t* data,size_t size,void* user)noexcept{
        auto& self=*static_cast<Impl*>(user);
        if(!self.active||!data||!size||size%4||size>1200)return;
        try{std::lock_guard lock(self.feedbackMutex);auto& pcm=self.feedback.haptics;
            if(pcm.size()+size/2>600)pcm.clear();
            const auto at=pcm.size();pcm.resize(at+size/2);std::memcpy(pcm.data()+at,data,size);
        }catch(...){++self.errors;}
    }
    static bool videoCallback(std::uint8_t* data,std::size_t size,
        const ChiakiVeyraVideoSampleInfo* info,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(!self.active.load()||!data||!size||!info)return false;
        // Counted and accepted, never rejected: a false return makes Chiaki drop
        // the reference frame, leave the last complete frame behind and send a
        // corrupt-frame report to the console for every following sample.
        if(self.discardMedia){++self.videoCallbacks;return true;}
        try{
            const auto nal=inspectAnnexB({data,size},self.profile.codec);
            if(!nal.valid || (!nal.hasConfig&&!nal.hasPicture)){++self.callbackRejected;return false;}
            VideoSample sample;
            sample.generation=self.token.generation();sample.arrival100ns=monotonic100ns();
            sample.codec=self.profile.codec;
            sample.kind=info->kind==CHIAKI_VEYRA_SAMPLE_FRAME?SampleKind::AccessUnit:SampleKind::CodecConfig;
            if((sample.kind==SampleKind::AccessUnit && !nal.hasPicture) ||
                (sample.kind==SampleKind::CodecConfig && (!nal.hasConfig || nal.hasPicture))){++self.callbackRejected;return false;}
            if(info->frame_index_valid)sample.wireFrameIndex=info->frame_index;
            sample.width=info->width;sample.height=info->height;
            sample.framesLost=info->frames_lost;sample.referenceRecovered=info->reference_recovered;
            sample.payload=PaddedBytes::copy({data,size});++self.videoCallbacks;
            const bool accepted=self.token.video(std::move(sample));if(!accepted)++self.callbackRejected;return accepted;
        }catch(...){self.apiError=-1001;self.token.failed(-1001);return false;}
    }
    // PS5 手柄控制 plays the capture card's audio. A sink is still attached: with
    // no frame callback Chiaki's audio receiver returns before freeing each
    // buffered frame. Nothing is decoded.
    static void discardAudio(std::uint8_t*,std::size_t,void* user) noexcept {++static_cast<Impl*>(user)->audioCallbacks;}
    static void opusSettings(std::uint32_t channels,std::uint32_t rate,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        // Sample position stays monotonic across repeated Opus headers within
        // one session; restarting at zero makes AudioIngress reject new PCM.
        self.channels=channels;self.rate=rate;self.audioDiscontinuity=true;
        if((channels!=1&&channels!=2)||rate!=48000){self.channels=0;self.token.failed(-1003);}
    }
    static void opusFrame(std::int16_t* data,std::size_t count,void* user) noexcept {
        auto& self=*static_cast<Impl*>(user);
        if(!self.active.load()||!self.channels||!data)return;
        if(count==0||count>self.rate/5u||count>(std::numeric_limits<std::uint64_t>::max)()-self.sampleIndex){self.token.failed(-1004);return;}
        try{
            PcmBlock block;block.generation=self.token.generation();block.channels=self.channels;block.rate=self.rate;
            block.firstSample=self.sampleIndex;block.arrival100ns=monotonic100ns();block.discontinuity=self.audioDiscontinuity;
            // Opus count is samples PER CHANNEL, not bytes or interleaved elements.
            block.samples.assign(data,data+count*self.channels);self.sampleIndex+=count;++self.audioCallbacks;
            if(self.token.audio(std::move(block))) self.audioDiscontinuity=false;
        }catch(...){self.apiError=-1002;self.token.failed(-1002);}
    }
    BackendResult record(ChiakiErrorCode code,const char* name,bool includePsn=false){
        apiError=static_cast<int>(code);auto out=result(code,name);
        if(psnDiagnostics&&includePsn){
            const auto events=psnEvents.load(),state=psnState.load();
            const auto wsError=psnWsError.load();
            std::ostringstream text;
            text<<"create_ms="<<psnPhaseMs[0]<<" offer_ms="<<psnPhaseMs[1]<<" start_ms="<<psnPhaseMs[2]<<" ctrl_ms="<<psnPhaseMs[3]
                <<" events="<<events<<" state="<<state<<" last_http="<<psnHttp.load()
                <<" console_joined="<<bool(state&(1u<<6))<<" custom_data="<<bool(state&(1u<<7))
                <<" ws_finished="<<bool(events&(1u<<9))<<" ws_pong_timeout="<<bool(events&(1u<<10))
                <<" ws_op="<<(wsError>>10)<<" ws_curl="<<(wsError&1023u);
            out.diagnostics=text.str();
        }
        return out;
    }
    bool onOwner()const noexcept{return owner==std::this_thread::get_id();}
};
ChiakiBackend::ChiakiBackend()=default;
ChiakiBackend::~ChiakiBackend(){
    if(!p_)return;
    try{
        const auto stopped=stop();
        if(!stopped.ok && p_){
            // A failed join cannot be repaired by freeing memory still used by callbacks.
            // Quarantine/leak this one session rather than create a use-after-free.
            // The owner must treat stop failure as fatal and not reconnect this object.
            p_->active=false;(void)p_.release();
        }
    }catch(...){if(p_){p_->active=false;(void)p_.release();}}
}
BackendResult ChiakiBackend::start(const NativeConnectRequest& request,SessionInbox::Token token,bool discardMedia,std::stop_token cancel){
    if(p_)return {false,-1100,"already_initialized"};
    if(request.validate()||token.generation()==0)return {false,-1101,"validate_connect"};
    if(cancel.stop_requested())return result(CHIAKI_ERR_CANCELED,"connect_canceled");
    auto init=initializeChiaki();if(!init.ok)return init;
    p_=std::make_unique<Impl>(std::move(token));auto& s=*p_;s.host=request.host;s.profile=request.video;
    s.discardMedia=discardMedia;
    s.psnDiagnostics=request.psn;
    // Verbose formatting has per-packet overhead: enable only for explicit diagnostics.
    // Callback above whitelists numeric quality fields and never forwards raw text.
    const bool qualityTrace=std::getenv("VEYRA_TEST_PS5_QUALITY_TRACE")!=nullptr;
    // INFO carries fixed PIN acknowledgements for LAN as well as PSN. Raw log
    // text remains filtered by logCallback; no payload/hexdump is forwarded.
    chiaki_log_init(&s.log,CHIAKI_LOG_WARNING|CHIAKI_LOG_ERROR|CHIAKI_LOG_INFO|(qualityTrace?CHIAKI_LOG_VERBOSE:0),Impl::logCallback,&s);
    if(request.psn){
        // STUN/candidate negotiation works without changing router mappings.
        // UPnP discovery is optional and is deliberately not needed for PSN login.
        s.holepunch=chiaki_holepunch_session_init(request.psnToken.c_str(),&s.log);
        if(!s.holepunch){p_.reset();return result(CHIAKI_ERR_MEMORY,"psn_session_init");}
        ChiakiErrorCode code=CHIAKI_ERR_SUCCESS;const char* operation="psn_session_create";
        {
            // The callback is destroyed before session_init takes ownership (and may free it).
            std::stop_callback interrupt(cancel,[&s]{chiaki_holepunch_main_thread_cancel(s.holepunch,true);});
            std::array<uint8_t,32> duid{};
            const auto hex=[](char c)->unsigned{return c<='9'?c-'0':(c|32)-'a'+10;};
            for(size_t i=0;i<duid.size();++i)duid[i]=uint8_t(hex(request.psnDuid[i*2])*16+hex(request.psnDuid[i*2+1]));
            const auto timed=[&](size_t phase,auto call){
                const auto begin=std::chrono::steady_clock::now();const auto value=call();
                s.psnPhaseMs[phase]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();return value;
            };
            if(!cancel.stop_requested())code=timed(0,[&]{return chiaki_holepunch_session_create(s.holepunch);});else code=CHIAKI_ERR_CANCELED;
            if(code==CHIAKI_ERR_SUCCESS&&!cancel.stop_requested()){operation="psn_create_offer";code=timed(1,[&]{return holepunch_session_create_offer(s.holepunch);});}
            if(code==CHIAKI_ERR_SUCCESS&&!cancel.stop_requested()){operation="psn_session_start";code=timed(2,[&]{return chiaki_holepunch_session_start(s.holepunch,duid.data(),request.ps5?CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5:CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4);});}
            if(code==CHIAKI_ERR_SUCCESS&&!cancel.stop_requested()){operation="psn_punch_control";code=timed(3,[&]{return chiaki_holepunch_session_punch_hole(s.holepunch,CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL);});}
        }
        if(cancel.stop_requested())code=CHIAKI_ERR_CANCELED;
        if(code!=CHIAKI_ERR_SUCCESS){
            auto out=s.record(code,operation,true);s.token.failed(out.code);
            chiaki_holepunch_main_thread_cancel(s.holepunch,true);chiaki_holepunch_session_fini(s.holepunch);s.holepunch=nullptr;
            p_.reset();return out;
        }
    }
    ChiakiConnectInfo info{};info.ps5=request.ps5;info.host=s.host.c_str();info.holepunch_session=s.holepunch;
    std::memcpy(info.regist_key,request.credentials.registrationKey.data(),request.credentials.registrationKey.size());
    std::memcpy(info.morning,request.credentials.sessionKey.data(),request.credentials.sessionKey.size());
    std::memcpy(info.psn_account_id,request.credentials.accountId.data(),request.credentials.accountId.size());
    chiaki_connect_video_profile_preset(&info.video_profile,
        request.video.height==720?CHIAKI_VIDEO_RESOLUTION_PRESET_720p:CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
        request.video.fps==30?CHIAKI_VIDEO_FPS_PRESET_30:CHIAKI_VIDEO_FPS_PRESET_60);
    info.video_profile.codec=request.video.codec==Codec::H264?CHIAKI_CODEC_H264:request.video.codec==Codec::H265Hdr?CHIAKI_CODEC_H265_HDR:CHIAKI_CODEC_H265;
    info.video_profile.bitrate=request.video.bitrateKbps;
    // Base PS4 only offers 720p; the upstream console-type response selects that
    // profile while PS4 Pro can retain 1080p. Decoded dimensions remain authoritative.
    info.video_profile_auto_downgrade=!request.ps5;info.enable_keyboard=false;info.enable_dualsense=request.ps5&&(!request.viewOnly||discardMedia);
    info.auto_regist=false;info.packet_loss_max=0.05;info.enable_idr_on_fec_failure=true;
    const auto code=chiaki_session_init(&s.session,&info,&s.log);
    wipe(info.regist_key,sizeof(info.regist_key));wipe(info.morning,sizeof(info.morning));wipe(info.psn_account_id,sizeof(info.psn_account_id));
    if(code!=CHIAKI_ERR_SUCCESS){auto out=s.record(code,"chiaki_session_init",true);s.token.failed(out.code);p_.reset();return out;}
    s.initialized=true;
    if(cancel.stop_requested()){(void)stop();return result(CHIAKI_ERR_CANCELED,"connect_canceled");}
    if(discardMedia){ChiakiAudioSink sink{};sink.user=&s;sink.frame_cb=Impl::discardAudio;chiaki_session_set_audio_sink(&s.session,&sink);}
    else{
        chiaki_opus_decoder_init(&s.opus,&s.log);s.opusInitialized=true;
        chiaki_opus_decoder_set_cb(&s.opus,Impl::opusSettings,Impl::opusFrame,&s);
        ChiakiAudioSink sink{};chiaki_opus_decoder_get_sink(&s.opus,&sink);chiaki_session_set_audio_sink(&s.session,&sink);
    }
    ChiakiAudioSink hapticSink{};hapticSink.user=&s;hapticSink.frame_cb=Impl::hapticsCallback;
    chiaki_session_set_haptics_sink(&s.session,&hapticSink);
    chiaki_session_set_event_cb(&s.session,Impl::eventCallback,&s);
    chiaki_session_set_veyra_video_sample_cb(&s.session,Impl::videoCallback,&s);
    s.active=true;
    const auto startCode=chiaki_session_start(&s.session);
    if(startCode!=CHIAKI_ERR_SUCCESS){auto out=s.record(startCode,"chiaki_session_start",true);s.active=false;s.token.failed(out.code);(void)stop();return out;}
    s.started=true;return s.record(startCode,"chiaki_session_start",true);
}
BackendResult ChiakiBackend::stop(){
    if(!p_)return {true,0,"already_stopped"};
    auto& s=*p_;
    if(!s.onOwner())return {false,-1102,"wrong_owner_thread"};
    s.active=false;
    if(s.holepunch)chiaki_holepunch_main_thread_cancel(s.holepunch,true);
    ChiakiErrorCode stopResult=CHIAKI_ERR_SUCCESS;
    if(s.started){
        ChiakiControllerState idle;chiaki_controller_state_set_idle(&idle);
        (void)s.record(chiaki_session_set_controller_state(&s.session,&idle),"release_controller");
        const auto stopCode=chiaki_session_stop(&s.session);
        stopResult=stopCode;
        // Join even if stop reported an error. Never fini while a callback is alive.
        const auto joinCode=chiaki_session_join(&s.session);
        if(joinCode!=CHIAKI_ERR_SUCCESS)return s.record(joinCode,"chiaki_session_join");
        s.started=false;s.connected=false;
        if(stopCode!=CHIAKI_ERR_SUCCESS)s.apiError=static_cast<int>(stopCode);
    }
    if(s.opusInitialized){chiaki_opus_decoder_fini(&s.opus);s.opusInitialized=false;}
    if(s.initialized){chiaki_session_fini(&s.session);s.initialized=false;}
    else if(s.holepunch)chiaki_holepunch_session_fini(s.holepunch);
    s.holepunch=nullptr;
    wipe(&s.session,sizeof(s.session));
    p_.reset();return result(stopResult,"stop_join_fini");
}
BackendResult ChiakiBackend::requestIdr(){
    if(!p_||!p_->onOwner()||!p_->started)return {false,-1103,"idr_without_session"};
    return p_->record(chiaki_session_request_idr(&p_->session),"chiaki_session_request_idr");
}
BackendResult ChiakiBackend::submitLoginPin(std::string_view pin){
    if(!p_||!p_->onOwner()||!p_->started)return {false,-1104,"pin_without_session"};
    if(pin.empty()||pin.size()>8||!std::all_of(pin.begin(),pin.end(),[](char c){return c>='0'&&c<='9';}))return {false,-1105,"invalid_login_pin"};
    if(!p_->token.loginPinSubmitting())return {false,-1107,"pin_not_requested"};
    const auto code=chiaki_session_set_login_pin(&p_->session,reinterpret_cast<const std::uint8_t*>(pin.data()),pin.size());
    p_->token.loginPinSubmissionResult(static_cast<int>(code));
    return p_->record(code,"chiaki_session_set_login_pin");
}
BackendResult ChiakiBackend::requestStandby(std::stop_token stop){
    if(!p_||!p_->onOwner()||!p_->started||!p_->connected)return {false,-1110,"standby_without_connected_session"};
    if(stop.stop_requested())return result(CHIAKI_ERR_CANCELED,"standby_canceled");
    auto& s=*p_;
    if(!s.standbyRequested){
        s.standbyResult.store(-1);
        s.standbyRequested=true;
        const auto code=chiaki_session_goto_bed(&s.session);
        if(code!=CHIAKI_ERR_SUCCESS){s.standbyRequested=false;return s.record(code,"standby_queue");}
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(s.standbyResult.load()<0){
        if(stop.stop_requested())return result(CHIAKI_ERR_CANCELED,"standby_canceled");
        if(std::chrono::steady_clock::now()>=deadline)return result(CHIAKI_ERR_TIMEOUT,"standby_send_timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto code=static_cast<ChiakiErrorCode>(s.standbyResult.load());
    if(code!=CHIAKI_ERR_SUCCESS)s.standbyRequested=false; // The completed failure is safe to retry; an unconfirmed timeout is not.
    return s.record(code,"standby_send");
}
BackendResult ChiakiBackend::submitController(const ControllerState& state){
    if(!p_||!p_->onOwner()||!p_->started)return {false,-1106,"controller_without_session"};
    ChiakiControllerState c;chiaki_controller_state_set_idle(&c);
    struct Mapping{std::uint32_t semantic;ChiakiControllerButton target;};
    static constexpr Mapping buttons[]={
        {ControllerState::Cross,CHIAKI_CONTROLLER_BUTTON_CROSS},{ControllerState::Circle,CHIAKI_CONTROLLER_BUTTON_MOON},
        {ControllerState::Square,CHIAKI_CONTROLLER_BUTTON_BOX},{ControllerState::Triangle,CHIAKI_CONTROLLER_BUTTON_PYRAMID},
        {ControllerState::Left,CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT},{ControllerState::Right,CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT},
        {ControllerState::Up,CHIAKI_CONTROLLER_BUTTON_DPAD_UP},{ControllerState::Down,CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN},
        {ControllerState::L1,CHIAKI_CONTROLLER_BUTTON_L1},{ControllerState::R1,CHIAKI_CONTROLLER_BUTTON_R1},
        {ControllerState::L3,CHIAKI_CONTROLLER_BUTTON_L3},{ControllerState::R3,CHIAKI_CONTROLLER_BUTTON_R3},
        {ControllerState::Options,CHIAKI_CONTROLLER_BUTTON_OPTIONS},{ControllerState::Share,CHIAKI_CONTROLLER_BUTTON_SHARE},
        {ControllerState::Touchpad,CHIAKI_CONTROLLER_BUTTON_TOUCHPAD},{ControllerState::PS,CHIAKI_CONTROLLER_BUTTON_PS}};
    for(auto b:buttons)if(state.buttons&b.semantic)c.buttons|=static_cast<std::uint32_t>(b.target);
    for(size_t i=0;i<state.touches.size();++i){c.touches[i].id=state.touches[i].id;c.touches[i].x=state.touches[i].x;c.touches[i].y=state.touches[i].y;}
    const auto applyMotion=[&](const std::array<float,3>& gyro,const std::array<float,3>& accel,uint64_t timestamp){
        if(!timestamp||!std::all_of(gyro.begin(),gyro.end(),[](float v){return std::isfinite(v);})||!std::all_of(accel.begin(),accel.end(),[](float v){return std::isfinite(v);}))return;
        // Repeated state snapshots contain the same batch; never integrate twice.
        if(p_->lastMotion&&timestamp<=p_->lastMotion)return;
        if(!p_->lastMotion||timestamp-p_->lastMotion>100000){chiaki_orientation_tracker_init(&p_->orientation);chiaki_accel_new_zero_set_inactive(&p_->accelZero,false);}
        chiaki_orientation_tracker_update(&p_->orientation,gyro[0],gyro[1],gyro[2],accel[0],accel[1],accel[2],&p_->accelZero,false,uint32_t(timestamp));p_->lastMotion=timestamp;
    };
    if(state.motionValid){
        for(size_t i=0;i<std::min(size_t(state.motionSampleCount),state.motionSamples.size());++i){const auto& sample=state.motionSamples[i];applyMotion(sample.gyro,sample.accel,sample.timestampUs);}
        applyMotion(state.gyro,state.accel,state.motionTimestampUs);
        chiaki_orientation_tracker_apply_to_controller_state(&p_->orientation,&c);
    }else p_->lastMotion=0;
    c.l2_state=state.l2;c.r2_state=state.r2;c.left_x=state.leftX;c.left_y=state.leftY;c.right_x=state.rightX;c.right_y=state.rightY;
    return p_->record(chiaki_session_set_controller_state(&p_->session,&c),"chiaki_session_set_controller_state");
}
ControllerFeedback ChiakiBackend::takeFeedback(){
    if(!p_||!p_->onOwner())return {};
    std::lock_guard lock(p_->feedbackMutex);auto result=std::move(p_->feedback);p_->feedback={};
    if(result.motionReset)p_->lastMotion=0;
    return result;
}
NativeSnapshot ChiakiBackend::snapshot()const{
    // Like start/stop, pointer ownership requires the owner; the atomic counters
    // themselves may be updated by callbacks. Do not call concurrently with reset.
    if(!p_)return {};
    auto& s=*p_;
    NativeSnapshot out{s.started.load(),s.connected.load(),s.warnings.load(),s.errors.load(),s.videoCallbacks.load(),s.audioCallbacks.load(),s.quitReason.load(),s.apiError.load()};
    out.callbackRejected=s.callbackRejected;out.transportErrors=s.transportErrors;out.assemblyErrors=s.assemblyErrors;
    out.serverTargetBitrate=s.serverTargetBitrate;out.qualityReports=s.qualityReports;
    // Upstream packet window may reset itself; do not label this cumulative UDP.
    if(s.initialized)chiaki_packet_stats_get(&s.session.stream_connection.packet_stats,false,&out.packetReceived,&out.packetLost);
    // After a stopped session the PS5 may briefly still report RP_IN_USE.
    // Retrying this request cannot evict it; the console remains authoritative.
    // A manual reconnect may race the console releasing our previous session.
    // Retry RP_IN_USE within the same bounded budget; never force eviction.
    out.automaticRetryAllowed=out.lastQuitReason==CHIAKI_QUIT_REASON_NONE||out.lastQuitReason==CHIAKI_QUIT_REASON_CTRL_UNKNOWN||out.lastQuitReason==CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED||out.lastQuitReason==CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN||out.lastQuitReason==CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
    out.startupRetryAllowed=out.lastQuitReason==CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
    return out;
}
} // namespace veyra::remoteplay
