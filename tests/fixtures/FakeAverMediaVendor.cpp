// Test double for the fixed public x64 ABI; no vendor implementation/binary.
#include "veyra/source/AverMediaVendorProtocol.h"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace veyra::source::avermedia;
namespace {
std::wstring mode;std::atomic<unsigned> starts=0,stops=0,callbacks=0,live=0;std::atomic<bool> sta=true;
using Logger=void(*)(const char*,char*);Logger logger=nullptr;
struct Graph {AudioDealer* dealer=nullptr;std::atomic<bool> stop=false;std::thread thread;};
Graph*& graph(void* self){return *reinterpret_cast<Graph**>(self);}
void trace(const char* format,...){va_list args;va_start(args,format);if(logger)logger(format,args);va_end(args);}
void apartment(){APTTYPE type;APTTYPEQUALIFIER qualifier;sta=sta&&SUCCEEDED(CoGetApartmentType(&type,&qualifier))&&(type==APTTYPE_STA||type==APTTYPE_MAINSTA);}
}
extern "C" {
__declspec(dllexport) void FakeMode(const wchar_t* value){mode=value;starts=stops=callbacks=live=0;sta=true;}
__declspec(dllexport) unsigned FakeCount(unsigned which){return which==0?starts.load():which==1?stops.load():which==2?callbacks.load():which==3?live.load():sta?1:0;}
void* FakeCtor(void* self){apartment();graph(self)=new Graph;++live;return self;}
bool FakeStop(void* self){++stops;auto* g=graph(self);if(!g)return true;if(mode==L"stop-fail")return false;g->stop=true;if(g->thread.joinable())g->thread.join();return true;}
void FakeDtor(void* self){FakeStop(self);delete graph(self);graph(self)=nullptr;--live;}
bool FakeReset(void*){apartment();if(mode==L"seh")RaiseException(0xC0000005,0,0,nullptr);if(mode==L"exception")throw std::runtime_error("test vendor exception");return mode!=L"reset-fail";}
void FakeDevices(std::vector<DeviceInfo>& out){
    out={{L"Live Gamer Ultra 2.1-Audio",L"\\\\?\\usb#vid_07ca&pid_2553&mi_02#5&first&0&0002#{guid}"},
         {L"Live Gamer Ultra 2.1-Audio",L"\\\\?\\usb#vid_07ca&pid_2553&mi_02#5&second&0&0002#{guid}"}};
}
bool FakeUpdate(void*,const std::wstring&,const std::wstring& path){return path.find(L"first")!=path.npos;}
bool FakeConnect(void*){return mode!=L"connect-fail";}
bool FakeCallback(void* self,AudioDealer* dealer){graph(self)->dealer=dealer;return true;}
bool FakeStart(void* self){
    ++starts;apartment();trace("fake public log channel=%d rate=%s",2,"48000");
    if(mode==L"delayed"||mode==L"late-fail")std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if(mode==L"start-fail"||mode==L"late-fail")return false;
    if(mode==L"stall")return true;
    auto* g=graph(self);g->stop=false;
    g->thread=std::thread([g]{
        std::vector<unsigned char> pcm(1920,0);
        for(size_t i=0;i<pcm.size();i+=2){pcm[i]=0;pcm[i+1]=0x20;}
        while(!g->stop){++callbacks;if(g->dealer){const SampleInfo info=mode==L"bad-format"?SampleInfo{2,12,48000}:SampleInfo{2,16,48000};g->dealer->onAudioData(info,pcm.data(),long(pcm.size()));}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    });return true;
}
void FakeLogger(Logger log){logger=log;}
}
