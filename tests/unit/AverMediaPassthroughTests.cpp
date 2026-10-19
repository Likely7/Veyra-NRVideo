#include "veyra/source/AverMediaAudioSwitch.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {unsigned checks=0;void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}}
int wmain(int argc,wchar_t** argv){try{
    require(argc==4,"usage: sdk.dll test-root complete|no-get|no-set");
    const auto root=std::filesystem::absolute(argv[2]);const std::wstring variant=argv[3];
    std::filesystem::create_directories(root/L"obs-plugins/64bit");std::filesystem::create_directories(root/L"data/obs-plugins/AVerMediaMultichannelAudio");
    const auto dll=root/L"obs-plugins/64bit/avt_device_opener.dll";
    std::filesystem::copy_file(argv[1],dll,std::filesystem::copy_options::overwrite_existing);
    for(auto name:{L"RTICE_SDK_x64.dll",L"RTK_IO_x64.dll"})std::ofstream(root/L"data/obs-plugins/AVerMediaMultichannelAudio"/name).put('\0');
    SetEnvironmentVariableW(L"VEYRA_AVERMEDIA_COMPONENT",root.c_str());
    const auto module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);require(module!=nullptr,"load own SDK fixture");
    auto config=reinterpret_cast<void(*)(int,int,int,int,int)>(GetProcAddress(module,"FakeConfigure"));
    auto count=reinterpret_cast<unsigned(*)(unsigned)>(GetProcAddress(module,"FakeCount"));require(config&&count,"fixture control exports");
    using veyra::source::AverMediaAudioSwitch;AverMediaAudioSwitch audio;
    const std::wstring name=L"Live Gamer Ultra 2.1-Audio",path=L"\\\\?\\usb#vid_07ca&pid_2553&mi_02#5&card&0&0002#{guid}";
    config(-236,0,0,0,1);require(audio.apply(name,path,false),"optional exports do not prevent old component load");
    require(!audio.active()&&count(4)==0,"graph setup defers the monitor while reading and arming");
    require(audio.sourceIsBitstream(),"negative sign-extended format low byte is non-PCM");
    audio.alignPassthroughOnce();
    if(variant==L"no-get"){
        require(!audio.status().passthroughKnown&&count(0)==0,"missing query must not force already-armed hardware");
        require(!audio.setPassthrough(true)&&count(0)==0,"explicit enable cannot bypass unknown query");
    }else if(variant==L"no-set"){
        require(audio.status().passthroughKnown&&!audio.passthroughEnabled(),"read-only component still reports state");
        require(!audio.setPassthrough(true)&&count(0)==0,"missing setter leaves mode unchanged");
    }else{
        require(variant==L"complete","known variant");
        require(count(0)==1&&audio.status().passthroughKnown&&audio.passthroughEnabled(),"off is armed once and read back");
        audio.startMonitoring();require(audio.active()&&count(4)==1,"monitor starts only after explicit graph setup completion");
        require(audio.apply(name,path,false)&&!audio.active(),"graph rebuild pauses the previous monitor on the same card");
        audio.alignPassthroughOnce();require(count(0)==1,"rebuild of armed card does not force enable");
        audio.startMonitoring();require(audio.active()&&count(4)==2,"monitor resumes after rebuild");
        require(audio.apply(name,path,false)&&!audio.active(),"same-card setup can defer monitoring again");
        require(audio.apply(name,path)&&audio.active()&&count(4)==3,"legacy/default same-card apply resumes the monitor");
        audio.alignPassthroughOnce();require(count(0)==1,"already armed is never forced again");
        require(!audio.setPassthrough(false)&&count(1)==0,"never disarm an active source");
        config(20,1,0,0,1);audio.alignPassthroughOnce();require(count(0)==0,"positive format and armed state preserved");
        config(2,0,0,0,1);require(!audio.sourceIsBitstream(),"PCM classification remains PCM");
        config(-236,1,1,0,1);audio.alignPassthroughOnce();require(!audio.status().passthroughKnown&&count(0)==0,"failed query clears stale state and never forces");
        config(-236,0,2,0,1);require(!audio.setPassthrough(true)&&count(0)==0,"unreachable chip cannot be forced");
        config(-236,0,3,0,1);audio.alignPassthroughOnce();require(!audio.status().passthroughKnown&&count(0)==0,"query SEH handled without forcing");
        config(-236,0,0,1,1);require(!audio.setPassthrough(true)&&!audio.passthroughEnabled(),"setter failure not marked enabled");
        config(-236,0,0,0,0);require(!audio.setPassthrough(true)&&!audio.passthroughEnabled(),"successful write needs actual readback");
        config(-236,0,0,0,1);require(audio.setPassthrough(true)&&audio.passthroughEnabled(),"query/write can recover after failure");
        const auto initialized=count(2);audio.stop();require(!audio.status().sdkReady&&!audio.status().passthroughKnown,"stop invalidates SDK/readback state");
        require(audio.apply(name,path)&&count(2)==initialized+1,"stopped object can initialize same card again");
    }
    audio.stop();SetEnvironmentVariableW(L"VEYRA_AVERMEDIA_COMPONENT",nullptr);FreeLibrary(module);
    std::cout<<"PASS "<<checks<<" passthrough ABI checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
