#include <windows.h>
#include "shared-memory-queue.h"
#include "tiny-nv12-scale.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);std::exit(2);}}while(false)
static void padded(){
    constexpr unsigned w=638,h=478,pitch=768;
    auto* writer=video_queue_create(w,h,166667);CHECK(writer);
    auto* first=video_queue_open();auto* second=video_queue_open();CHECK(first&&second);
    std::vector<uint8_t> y(pitch*h,0xcc),uv(pitch*h/2,0xcc),expected(size_t(w)*h*3/2);
    for(unsigned row=0;row<h;++row)for(unsigned x=0;x<w;++x)y[row*pitch+x]=uint8_t((x+row)%220+16);
    for(unsigned row=0;row<h/2;++row)for(unsigned x=0;x<w;++x)uv[row*pitch+x]=uint8_t((x+row)%160+40);
    for(unsigned row=0;row<h;++row)memcpy(expected.data()+size_t(row)*w,y.data()+size_t(row)*pitch,w);
    for(unsigned row=0;row<h/2;++row)memcpy(expected.data()+size_t(w)*h+size_t(row)*w,uv.data()+size_t(row)*pitch,w);
    uint8_t* planes[]={y.data(),uv.data()};uint32_t pitches[]={pitch,pitch};
    CHECK(!video_queue_write(writer,planes,nullptr,9));CHECK(video_queue_write(writer,planes,pitches,12345));
    nv12_scale_t scale;nv12_scale_init(&scale,TARGET_FORMAT_NV12,w,h,w,h);uint64_t timestamp=0;
    std::vector<uint8_t> got(expected.size());
    CHECK(video_queue_read(first,&scale,got.data(),&timestamp));CHECK(timestamp==12345&&got==expected);
    CHECK(video_queue_read(second,&scale,got.data(),&timestamp));CHECK(timestamp==12345&&got==expected);
    // A paused source remains a valid picture while the producer is alive.
    Sleep(180);video_queue_heartbeat(writer);
    CHECK(video_queue_read(first,&scale,got.data(),&timestamp));CHECK(got==expected);
    video_queue_close(writer);CHECK(video_queue_state(first)==SHARED_QUEUE_STATE_STOPPING);
    CHECK(!video_queue_read(first,&scale,got.data(),&timestamp));
    CHECK(!video_queue_create(1280,720,166667)); // No overwrite while old readers hold the mapping.
    video_queue_close(first);video_queue_close(second);
    writer=video_queue_create(1280,720,166667);CHECK(writer);video_queue_close(writer);
}
static void readers(){
    constexpr unsigned w=1280,h=720;
    auto* writer=video_queue_create(w,h,166667);CHECK(writer);
    std::vector<uint8_t> pixels(size_t(w)*h*3/2,128);memset(pixels.data(),35,size_t(w)*h);
    uint8_t* data[]={pixels.data(),pixels.data()+size_t(w)*h};uint32_t pitch[]={w,w};CHECK(video_queue_write(writer,data,pitch,1));
    std::atomic<bool> stop{false},torn{false};std::atomic<unsigned> ready{0},received{0};
    auto read=[&]{
        auto* reader=video_queue_open();if(!reader){torn=true;++ready;return;}
        nv12_scale_t scale;nv12_scale_init(&scale,TARGET_FORMAT_NV12,w,h,w,h);
        std::vector<uint8_t> frame(pixels.size());++ready;
        while(!stop){
            uint64_t timestamp=0;
            if(video_queue_read(reader,&scale,frame.data(),&timestamp)){
                const auto y=frame[0];
                if(!std::all_of(frame.begin(),frame.begin()+size_t(w)*h,[&](uint8_t v){return v==y;}))torn=true;
                if(!std::all_of(frame.begin()+size_t(w)*h,frame.end(),[](uint8_t v){return v==128;}))torn=true;
                ++received;
            }
            Sleep(1);
        }
        video_queue_close(reader);
    };
    std::thread a(read),b(read);while(ready<2)Sleep(1);
    unsigned published=0;
    for(unsigned n=0;n<400;++n){
        memset(pixels.data(),35+(n%170),size_t(w)*h);
        if(video_queue_write(writer,data,pitch,n+2))++published;
    }
    stop=true;a.join();b.join();CHECK(published>0);CHECK(received>0);CHECK(!torn);
    video_queue_close(writer);std::printf("multi-reader frames=%u copies=%u noTornFrames=1\n",published,received.load());
}
// Protocol 2 (2026-11-17): frames in write order with their grid stamps, a slot reused
// after six frames, and consumer rate announcements the producer can read.
static void ordered(){
    constexpr unsigned w=320,h=180;
    auto* writer=video_queue_create(w,h,166667);CHECK(writer);
    auto* reader=video_queue_open();CHECK(reader);
    std::vector<uint8_t> pixels(size_t(w)*h*3/2,128);uint8_t* data[]={pixels.data(),pixels.data()+size_t(w)*h};uint32_t pitch[]={w,w};
    uint64_t sequence=0,ts=0,written=0;
    CHECK(!video_queue_peek(reader,0,&sequence,&ts,&written));
    for(unsigned n=1;n<=3;++n){pixels[0]=uint8_t(n);CHECK(video_queue_write(writer,data,pitch,1000*n));}
    nv12_scale_t scale;nv12_scale_init(&scale,TARGET_FORMAT_NV12,w,h,w,h);std::vector<uint8_t> got(pixels.size());
    uint64_t after=0;
    for(unsigned n=1;n<=3;++n){
        CHECK(video_queue_peek(reader,after,&sequence,&ts,&written));CHECK(sequence==n&&ts==1000*n&&written>=ts);
        CHECK(video_queue_read_sequence(reader,&scale,got.data(),sequence));CHECK(got[0]==n);after=sequence;
    }
    CHECK(!video_queue_peek(reader,after,&sequence,&ts,&written));
    // Ten more: the ring holds the newest six; peeking from an old sequence starts at the oldest held.
    for(unsigned n=4;n<=13;++n){pixels[0]=uint8_t(n);CHECK(video_queue_write(writer,data,pitch,1000*n));}
    CHECK(video_queue_peek(reader,after,&sequence,&ts,&written));CHECK(sequence==8&&ts==8000);
    CHECK(!video_queue_read_sequence(reader,&scale,got.data(),3)); // reused
    CHECK(video_queue_read_sequence(reader,&scale,got.data(),13));CHECK(got[0]==13);
    uint64_t latest=0;CHECK(video_queue_read(reader,&scale,got.data(),&latest));CHECK(latest==13000&&got[0]==13);
    // Consumers: the producer sees the fastest live one; a closed reader leaves.
    CHECK(video_queue_consumer_interval(writer)==0);
    auto* other=video_queue_open();CHECK(other);
    video_queue_set_consumer(reader,166667);CHECK(video_queue_consumer_interval(writer)==166667);
    video_queue_set_consumer(other,83333);CHECK(video_queue_consumer_interval(writer)==83333);
    video_queue_close(other);CHECK(video_queue_consumer_interval(writer)==166667);
    video_queue_set_consumer(reader,333333);CHECK(video_queue_consumer_interval(writer)==333333);
    video_queue_close(reader);CHECK(video_queue_consumer_interval(writer)==0);
    video_queue_close(writer);
    std::printf("ordered ring=6 consumers=fastest-live\n");
}
static void crash(const wchar_t* exe){
    const auto token=std::to_wstring(GetCurrentProcessId());
    const auto readyName=L"Local\\VeyraCameraTestReady"+token,openedName=L"Local\\VeyraCameraTestOpened"+token;
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str()),opened=CreateEventW(nullptr,TRUE,FALSE,openedName.c_str());CHECK(ready&&opened);
    std::wstring command=L"\""+std::wstring(exe)+L"\" --crash \""+readyName+L"\" \""+openedName+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
    auto* reader=video_queue_open();CHECK(reader);SetEvent(opened);
    CHECK(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0);
    CHECK(video_queue_state(reader)==SHARED_QUEUE_STATE_STOPPING);
    video_queue_close(reader);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(ready);CloseHandle(opened);
    auto* next=video_queue_create(640,480,166667);CHECK(next);video_queue_close(next);
}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring(argv[1])==L"--crash"){
        auto* writer=video_queue_create(640,480,166667);if(!writer)return 2;
        std::vector<uint8_t> pixels(640*480*3/2,90);uint8_t* planes[]={pixels.data(),pixels.data()+640*480};uint32_t pitch[]={640,640};
        video_queue_write(writer,planes,pitch,42);
        HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[2]),opened=OpenEventW(SYNCHRONIZE,FALSE,argv[3]);
        if(!ready||!opened)return 2;SetEvent(ready);WaitForSingleObject(opened,5000);
        ExitProcess(0); // Deliberately skip video_queue_close: a real producer crash.
    }
    const auto token=L"transport-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    SetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_NAMESPACE",token.c_str());
    CHECK(!video_queue_create(0,0,166667));CHECK(!video_queue_create(3,4,166667));
    CHECK(!video_queue_create(0xffffffff,0xffffffff,166667));CHECK(!video_queue_create(8192,8192,166667));
    padded();readers();ordered();crash(argv[0]);std::printf("PASS checks=%u bits=%zu\n",checks,sizeof(void*)*8);return 0;
}
