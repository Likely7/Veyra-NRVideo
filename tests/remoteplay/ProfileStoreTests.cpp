#include "veyra/remoteplay/ProfileStore.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace veyra::remoteplay;
int main(){
    wchar_t temp[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,temp))return 2;
    auto path=std::filesystem::path(temp)/(L"veyra-profile-test-"+std::to_wstring(GetCurrentProcessId())+L".dat");
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}} cleanup{path};
    auto require=[](bool ok){if(!ok)throw std::runtime_error("DPAPI profile test failed");};
    try{
        const auto isolated=path.parent_path()/L"isolated-remoteplay";
        require(SetEnvironmentVariableW(L"VEYRA_REMOTEPLAY_PROFILE_DIR",isolated.c_str())!=0);
        require(profileDirectory()==isolated);
        require(SetEnvironmentVariableW(L"VEYRA_REMOTEPLAY_PROFILE_DIR",L"relative-profile")!=0);
        bool rejectedRelative=false;try{(void)profileDirectory();}catch(const std::runtime_error&){rejectedRelative=true;}
        require(rejectedRelative);
        require(SetEnvironmentVariableW(L"VEYRA_REMOTEPLAY_PROFILE_DIR",isolated.c_str())!=0);
        static_assert(!std::is_copy_constructible_v<SecretString>);
        static_assert(std::is_nothrow_move_constructible_v<SecretString>);
        SecretString secret;secret.assign("synthetic-token");secret.assign(secret.view());
        SecretString moved(std::move(secret));require(secret.empty()&&moved.view()=="synthetic-token");
        secret.assign("replacement");secret=std::move(moved);require(moved.empty()&&secret.view()=="synthetic-token");
        secret.clear();require(secret.empty()&&secret.c_str()[0]=='\0');
        NativeConnectRequest request;request.host="127.0.0.1";request.video.codec=Codec::H265;
        require(request.ps5&&!request.psn&&!request.validate());
        request.consoleId="012345abcdef";request.viewOnly=true;
        request.credentials.accountId.fill(7);request.credentials.registrationKey.fill('1');request.credentials.sessionKey.fill(42);
        require(saveProfile(path,request));auto saved=loadProfile(path);require(saved.has_value());
        require(saved->consoleId==request.consoleId&&saved->viewOnly);
        require(profileDirectory().is_absolute());
        require(saved->host==request.host&&saved->video.codec==Codec::H265&&saved->credentials.accountId==request.credentials.accountId&&saved->credentials.sessionKey==request.credentials.sessionKey&&saved->credentials.registrationKey==request.credentials.registrationKey);
        request.video.fps=30;require(saveProfile(path,request));require(loadProfile(path)->video.fps==30);
        NativeConnectRequest remote;remote.psn=true;remote.psnDuid=std::string(64,'1');
        remote.psnToken.assign("synthetic-token");remote.credentials.accountId.fill(7);
        require(remote.host.empty()&&!remote.validate());
        require(!saveProfile(path,remote)&&loadProfile(path)->host==request.host);
        remote.psnDuid="bad";require(remote.validate().has_value());remote.psnDuid=std::string(64,'0');
        require(remote.validate().has_value());remote.ps5=false;remote.video.codec=Codec::H264;require(!remote.validate());
        remote.video.codec=Codec::H265;require(remote.validate().has_value());remote.video.codec=Codec::H264;
        remote.psnToken.assign("bad\r\nheader");require(remote.validate().has_value());
        remote.psnToken.assign("synthetic-token");remote.credentials.accountId.fill(0);require(remote.validate().has_value());
        remote.credentials.accountId.fill(7);
        SessionInbox inbox;auto token=inbox.begin(monotonic100ns());
        std::stop_source canceled;canceled.request_stop();ChiakiBackend backend;
        const auto stopped=backend.start(remote,token,false,canceled.get_token());
        require(!stopped.ok&&stopped.operation=="connect_canceled"&&!backend.snapshot().started&&backend.stop().ok);
        const auto root=path.parent_path()/(path.stem().wstring()+L"-migration");
        struct TreeCleanup {std::filesystem::path p;~TreeCleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}} tree{root};
        const auto legacy=root/L"legacy",dest=root/L"new";
        const auto name=L"0123456789abcdef0123456789abcdef.dat";
        require(saveProfile(legacy/L"remoteplay"/name,request));
        auto migrated=migrateProfilesTo(legacy,dest);require(migrated.imported==1&&migrated.failed==0);
        require(loadProfile(dest/name).has_value());
        require(migrateProfilesTo(legacy,dest).imported==0);
        std::filesystem::remove(dest/name);
        require(migrateProfilesTo(legacy,dest).imported==0&&!std::filesystem::exists(dest/name));
        require(std::filesystem::exists(legacy/L"remoteplay"/name));
        {std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);char byte=0;file.read(&byte,1);byte^=0x40;file.seekp(0);file.write(&byte,1);}
        require(!loadProfile(path));request.host="http://invalid";require(!saveProfile(path,request));
        std::cout<<"DPAPI_PROFILE_PASS roundtrip=1 atomic_replace=1 corrupted_rejected=1 invalid_host_rejected=1 psn_not_persisted=1 psn_validation=1 secret_move=1 precanceled_no_network=1 synthetic_credentials_only=1\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
