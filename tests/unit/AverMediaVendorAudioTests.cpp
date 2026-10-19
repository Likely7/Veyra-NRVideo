#include "veyra/source/AverMediaVendorAudio.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace veyra::source;
namespace {
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
template<class F> bool wait(F f,unsigned ms=2000){for(unsigned n=0;n<ms/10;++n){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return f();}
}
int wmain(int argc,wchar_t** argv){try{
    require(argc==3||argc==4,"usage: vendor tests fake.dll owned-test-root [--missing-export]");
    const auto root=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(root/L"obs-plugins/64bit");
    std::filesystem::create_directories(root/L"data/obs-plugins/AVerMediaMultichannelAudio");
    const auto dll=root/L"obs-plugins/64bit/avt_dshow_core.dll";
    std::filesystem::copy_file(argv[1],dll,std::filesystem::copy_options::overwrite_existing);
    // Component discovery checks these paths; these inert test placeholders
    // are never loaded. Product staging never contains this fake component.
    for(const auto& name:{L"obs-plugins/64bit/avt_device_opener.dll",L"data/obs-plugins/AVerMediaMultichannelAudio/RTICE_SDK_x64.dll",L"data/obs-plugins/AVerMediaMultichannelAudio/RTK_IO_x64.dll"})std::ofstream(root/name).put('\0');
    SetEnvironmentVariableW(L"VEYRA_AVERMEDIA_COMPONENT",root.c_str());
    const auto module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(module!=nullptr,"load own ABI fixture");
    auto mode=reinterpret_cast<void(*)(const wchar_t*)>(GetProcAddress(module,"FakeMode"));
    auto count=reinterpret_cast<unsigned(*)(unsigned)>(GetProcAddress(module,"FakeCount"));require(mode&&count,"fixture exports");
    const std::wstring name=L"Live Gamer Ultra 2.1-Video",path=L"\\\\?\\usb#vid_07ca&pid_2553&mi_00#5&first&0&0000#{guid}";
    if(argc==4){
        require(std::wstring_view(argv[3])==L"--missing-export","known test scenario");mode(L"normal");
        {AverMediaVendorAudio audio;audio.start([](const VendorPcmBlock&){return true;},name,path);
         require(wait([&]{return audio.state().failed;}),"missing export fails asynchronously without crash");
         require(audio.state().error.find(L"missing vendor export ?Start@AudioDevice")!=std::wstring::npos,"missing ABI export diagnostic");audio.stop();}
        require(count(3)==0&&count(0)==0,"no graph constructed on incomplete ABI");FreeLibrary(module);
        std::cout<<"PASS "<<checks<<" missing-export checks\n";return 0;
    }
    std::atomic<unsigned> received=0;std::atomic<bool> valid=true;
    auto callback=[&](const VendorPcmBlock& block){valid=valid&&block.channels==2&&block.rate==48000&&block.mask==3&&block.samples[0]==0.25f;++received;return true;};
    {
        mode(L"normal");AverMediaVendorAudio audio(100,2);
        require(!audio.start(callback,L"Microphone",L"opaque"),"never open unrelated microphone");require(count(0)==0,"no vendor calls for unrelated input");
        require(audio.start(callback,name,path),"start selected card");require(wait([&]{return received>3;}),"public callback delivers PCM");
        require(valid,"sample metadata and callback ABI");std::this_thread::sleep_for(std::chrono::milliseconds(180));
        require(audio.state().restarts==0,"continuous PCM is healthy without IEC bursts");require(count(4)==1,"all graph control on STA");
        audio.stop();const auto callbacks=count(2);std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(count(2)==callbacks&&count(3)==0,"stop joins callbacks and destroys graph");audio.stop();
    }
    {
        mode(L"delayed");AverMediaVendorAudio audio;
        const auto start=std::chrono::steady_clock::now();require(audio.start(callback,name,path),"delayed start accepted as pending");
        require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(350),"capture startup does not wait for vendor preroll");
        require(audio.state().starting&&!audio.state().running,"pending startup not reported running");require(wait([&]{return audio.running();}),"delayed completion visible");audio.stop();
    }
    for(const auto* scenario:{L"reset-fail",L"connect-fail",L"start-fail",L"late-fail",L"seh",L"exception"}){
        mode(scenario);AverMediaVendorAudio audio;audio.start(callback,name,path);
        require(wait([&]{return audio.state().failed;}),"vendor failure surfaced without crashing");
        require(!audio.state().error.empty()&&!audio.running(),"terminal diagnostic available");audio.stop();require(count(3)==0,"failure cleanup destroys graph");
    }
    {
        mode(L"normal");AverMediaVendorAudio audio;
        audio.start(callback,name,L"\\\\?\\usb#vid_07ca&pid_2553&mi_00#5&absent&0&0000#{guid}");
        require(wait([&]{return audio.state().failed;}),"absent selected serial refused");require(count(0)==0,"same-name other card never started");audio.stop();
    }
    for(const auto* scenario:{L"stall",L"bad-format"}){
        mode(scenario);AverMediaVendorAudio audio(80,2);audio.start(callback,name,path);
        require(wait([&]{return audio.state().failed;},3000),"no callbacks / invalid data exhaust bounded recovery");
        require(audio.state().restarts==2&&count(0)==3,"only configured number of rebuilds");audio.stop();require(count(3)==0,"rebuild cleanup");
    }
    {
        mode(L"normal");AverMediaVendorAudio audio;
        for(unsigned i=0;i<10;++i){require(audio.start(callback,name,path),"repeat startup");audio.stop();require(count(3)==0,"repeat teardown");}
    }
    {
        mode(L"stop-fail");AverMediaVendorAudio audio;
        const auto previous=received.load();require(audio.start(callback,name,path),"start faulty Stop fixture");
        require(wait([&]{return received>previous;}),"fixture callback is active before Stop");audio.stop();
        const auto stopped=received.load();std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(received==stopped,"failed vendor Stop cannot call released PCM session");
        require(count(3)==1&&!audio.start(callback,name,path),"one failed external graph is quarantined without reuse/unload");
    }
    FreeLibrary(module);SetEnvironmentVariableW(L"VEYRA_AVERMEDIA_COMPONENT",nullptr);
    std::cout<<"PASS "<<checks<<" checks: public ABI, STA, delayed startup, device identity, callback shutdown, SEH/C++ faults, bounded watchdog\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
