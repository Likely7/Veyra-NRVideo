// SPDX-License-Identifier: GPL-3.0-only
// On-screen text source for live translation (ScreenTextCapture.h). The reading
// logic in ScreenText.cpp is ported from VI Translator (MIT License).
#include "veyra/engine/ScreenTextCapture.h"
#include "veyra/engine/ScreenText.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <wrl/client.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace veyra::engine {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kSlots=2;
constexpr uint32_t kMaxOcrWidth=1920; // wider crops (4K, SR output) are averaged down before OCR
constexpr float kWhiteNits=250.0f;    // HDR pictures: this brightness and above reads as white

int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string utf8(std::wstring_view text){
    if(text.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(std::max(0,n)),'\0');
    if(n>0)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);
    return out;
}

std::wstring lowerAscii(std::wstring text){
    for(auto& c:text)if(c>=L'A'&&c<=L'Z')c=wchar_t(c+32);
    return text;
}

std::wstring primaryTag(const std::wstring& tag){
    const auto low=lowerAscii(tag);
    return low.substr(0,low.find(L'-'));
}

uint32_t bytesPerPixel(DXGI_FORMAT format){
    switch(format){
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return 8;
    default:
        return 0;
    }
}

float halfToFloat(uint16_t h){
    const uint32_t sign=uint32_t(h>>15)<<31;
    uint32_t exponent=(h>>10)&0x1Fu,mantissa=h&0x3FFu,bits=0;
    if(exponent==0){
        if(mantissa==0)bits=sign;
        else{ // subnormal: normalise
            exponent=127-15+1;
            while(!(mantissa&0x400u)){mantissa<<=1;--exponent;}
            bits=sign|(exponent<<23)|((mantissa&0x3FFu)<<13);
        }
    }else if(exponent==31)bits=sign|0x7F800000u|(mantissa<<13);
    else bits=sign|((exponent+127-15)<<23)|(mantissa<<13);
    return std::bit_cast<float>(bits);
}

// Display values for OCR: linear light (as a fraction of kWhiteNits) and 10-bit PQ codes.
struct Tables {
    std::array<uint8_t,4096> gamma{};
    std::array<uint8_t,1024> pq{};
    Tables(){
        for(size_t i=0;i<gamma.size();++i)
            gamma[i]=uint8_t(std::lround(std::pow(double(i)/4095.0,1.0/2.2)*255.0));
        constexpr double m1=2610.0/16384.0,m2=2523.0/4096.0*128.0,c1=3424.0/4096.0,c2=2413.0/4096.0*32.0,c3=2392.0/4096.0*32.0;
        for(size_t i=0;i<pq.size();++i){
            const double p=std::pow(double(i)/1023.0,1.0/m2);
            const double nits=10000.0*std::pow(std::max(p-c1,0.0)/(c2-c3*p),1.0/m1);
            pq[i]=linear(float(nits));
        }
    }
    uint8_t linear(float nits)const{
        const float v=nits/kWhiteNits;
        if(!(v>0.0f))return 0;
        if(v>=1.0f)return 255;
        return gamma[size_t(v*4095.0f+0.5f)];
    }
};
const Tables& tables(){static const Tables t;return t;}

// The copied region as an RGBA8 crop, averaged down by an integer factor when wider than kMaxOcrWidth.
screentext::Crop toCrop(const uint8_t* data,uint32_t pitch,DXGI_FORMAT format,uint32_t hdrMode,
                        uint32_t width,uint32_t height,uint32_t& factor){
    factor=std::max(1u,(width+kMaxOcrWidth-1)/kMaxOcrWidth);
    screentext::Crop crop;
    crop.w=width/factor;crop.h=height/factor;
    if(!crop.w||!crop.h)return {};
    crop.rgba.resize(size_t(crop.w)*crop.h*4);
    const auto& t=tables();
    const float samples=float(factor*factor);
    for(uint32_t oy=0;oy<crop.h;++oy){
        for(uint32_t ox=0;ox<crop.w;++ox){
            float r=0,g=0,b=0;
            for(uint32_t dy=0;dy<factor;++dy){
                const uint8_t* row=data+size_t(oy*factor+dy)*pitch;
                for(uint32_t dx=0;dx<factor;++dx){
                    const uint32_t x=ox*factor+dx;
                    switch(format){
                    case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:{
                        const uint8_t* p=row+size_t(x)*4;r+=p[2];g+=p[1];b+=p[0];break;
                    }
                    case DXGI_FORMAT_R10G10B10A2_UNORM:{
                        uint32_t v;std::memcpy(&v,row+size_t(x)*4,4);
                        r+=float(v&0x3FFu);g+=float((v>>10)&0x3FFu);b+=float((v>>20)&0x3FFu);break;
                    }
                    case DXGI_FORMAT_R16G16B16A16_FLOAT:{
                        uint16_t v[4];std::memcpy(v,row+size_t(x)*8,8);
                        r+=halfToFloat(v[0]);g+=halfToFloat(v[1]);b+=halfToFloat(v[2]);break;
                    }
                    default:{
                        const uint8_t* p=row+size_t(x)*4;r+=p[0];g+=p[1];b+=p[2];break;
                    }
                    }
                }
            }
            r/=samples;g/=samples;b/=samples;
            uint8_t* d=&crop.rgba[(size_t(oy)*crop.w+ox)*4];
            switch(format){
            case DXGI_FORMAT_R10G10B10A2_UNORM:
                if(hdrMode==2){ // HDR10: PQ BT.2020 (the primaries do not matter for reading text)
                    d[0]=t.pq[size_t(std::clamp(r+0.5f,0.0f,1023.0f))];
                    d[1]=t.pq[size_t(std::clamp(g+0.5f,0.0f,1023.0f))];
                    d[2]=t.pq[size_t(std::clamp(b+0.5f,0.0f,1023.0f))];
                }else{
                    d[0]=uint8_t(std::clamp(r*255.0f/1023.0f+0.5f,0.0f,255.0f));
                    d[1]=uint8_t(std::clamp(g*255.0f/1023.0f+0.5f,0.0f,255.0f));
                    d[2]=uint8_t(std::clamp(b*255.0f/1023.0f+0.5f,0.0f,255.0f));
                }
                break;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                if(hdrMode==1){ // scRGB: linear BT.709, 1.0 = 80 nits
                    d[0]=t.linear(r*80.0f);d[1]=t.linear(g*80.0f);d[2]=t.linear(b*80.0f);
                }else{
                    d[0]=uint8_t(std::clamp(r,0.0f,1.0f)*255.0f+0.5f);
                    d[1]=uint8_t(std::clamp(g,0.0f,1.0f)*255.0f+0.5f);
                    d[2]=uint8_t(std::clamp(b,0.0f,1.0f)*255.0f+0.5f);
                }
                break;
            default:
                d[0]=uint8_t(r+0.5f);d[1]=uint8_t(g+0.5f);d[2]=uint8_t(b+0.5f);
                break;
            }
            d[3]=255;
        }
    }
    return crop;
}

bool debugEnabled(){
    static const bool on=[]{wchar_t value[8]{};return GetEnvironmentVariableW(L"VEYRA_SCREEN_TEXT_DEBUG",value,8)>0;}();
    return on;
}

std::string joined(const std::vector<screentext::Word>& words){
    std::wstring text;
    for(const auto& w:words){if(!text.empty())text+=L' ';text+=w.text;}
    return utf8(text);
}

// The read area as a 24-bit BMP (diagnostics only).
void saveBmp(const screentext::Crop& crop,const std::filesystem::path& path){
    const uint32_t row=(crop.w*3+3)&~3u,size=54+row*crop.h;
    std::vector<uint8_t> file(size,0);
    auto put32=[&](size_t at,uint32_t v){for(int i=0;i<4;++i)file[at+size_t(i)]=uint8_t(v>>(8*i));};
    file[0]='B';file[1]='M';put32(2,size);put32(10,54);put32(14,40);put32(18,crop.w);put32(22,crop.h);
    file[26]=1;file[28]=24;put32(34,row*crop.h);
    for(uint32_t y=0;y<crop.h;++y){
        uint8_t* out=&file[54+size_t(crop.h-1-y)*row];
        for(uint32_t x=0;x<crop.w;++x){
            const uint8_t* p=&crop.rgba[(size_t(y)*crop.w+x)*4];
            out[x*3]=p[2];out[x*3+1]=p[1];out[x*3+2]=p[0];
        }
    }
    std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(file.data()),std::streamsize(file.size()));
}

// Windows.Media.Ocr for one language. Worker thread only (an MTA thread: RecognizeAsync().get() blocks).
class OcrReader {
public:
    bool open(const std::wstring& tag,std::wstring& error){
        using winrt::Windows::Media::Ocr::OcrEngine;
        close();
        const std::wstring want=lowerAscii(tag.empty()?std::wstring(L"en"):tag);
        const std::wstring primary=primaryTag(want);
        // Chinese recognizers are per script: zh-Hans-CN, zh-Hant-TW.
        std::wstring script;
        if(primary==L"zh")
            script=want.find(L"tw")!=std::wstring::npos||want.find(L"hk")!=std::wstring::npos||
                   want.find(L"mo")!=std::wstring::npos||want.find(L"hant")!=std::wstring::npos?L"hant":L"hans";
        try{
            winrt::Windows::Globalization::Language pick{nullptr};
            int best=-1;
            for(const auto& language:OcrEngine::AvailableRecognizerLanguages()){
                const auto name=language.LanguageTag();
                const std::wstring t=lowerAscii(std::wstring(name.c_str(),name.size()));
                if(primaryTag(t)!=primary)continue;
                int score=1;
                if(t==want)score=4;
                else if(!script.empty()&&t.find(script)!=std::wstring::npos)score=3;
                else if(primary==L"en"&&t==L"en-us")score=2;
                if(score>best){best=score;pick=language;}
            }
            if(!pick){
                error=std::format(L"Windows 未安装“{}”的文字识别（OCR）语言，请在 设置 → 时间和语言 → 语言和区域 中添加该语言",
                                  tag.empty()?std::wstring(L"en"):tag);
                return false;
            }
            engine_=OcrEngine::TryCreateFromLanguage(pick);
            if(!engine_){error=L"无法启动 Windows 文字识别（OCR）";return false;}
            const auto name=pick.LanguageTag();
            language_.assign(name.c_str(),name.size());
            maxDim_=std::max(64u,OcrEngine::MaxImageDimension());
            return true;
        }catch(const winrt::hresult_error& e){
            log::warn("screen-text",std::format("OCR engine hr=0x{:08X} {}",uint32_t(e.code()),winrt::to_string(e.message())));
        }catch(const std::exception& e){
            log::warn("screen-text",std::string("OCR engine ")+e.what());
        }
        engine_=nullptr;
        error=L"无法启动 Windows 文字识别（OCR）";
        return false;
    }
    void close(){engine_=nullptr;language_.clear();}
    bool ready()const{return static_cast<bool>(engine_);}
    const std::wstring& language()const{return language_;}
    uint32_t maxDim()const{return maxDim_;}
    std::vector<screentext::RawWord> read(const uint8_t* bgra,uint32_t w,uint32_t h)const{
        using namespace winrt::Windows::Graphics::Imaging;
        using winrt::Windows::Storage::Streams::DataWriter;
        std::vector<screentext::RawWord> out;
        if(!engine_||!w||!h)return out;
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(bgra,bgra+size_t(w)*h*4));
        const auto bitmap=SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),BitmapPixelFormat::Bgra8,int32_t(w),int32_t(h));
        const auto result=engine_.RecognizeAsync(bitmap).get();
        uint32_t line=0;
        for(const auto& ocrLine:result.Lines()){
            for(const auto& word:ocrLine.Words()){
                const auto box=word.BoundingRect();
                const auto text=word.Text();
                screentext::RawWord raw;
                raw.line=line;
                raw.text.assign(text.c_str(),text.size());
                raw.x=box.X;raw.y=box.Y;raw.w=box.Width;raw.h=box.Height;
                out.push_back(std::move(raw));
            }
            ++line;
        }
        return out;
    }
private:
    winrt::Windows::Media::Ocr::OcrEngine engine_{nullptr};
    std::wstring language_;
    uint32_t maxDim_=2600;
};
} // namespace

struct ScreenTextCapture::State {
    struct Region {float x=0,y=0,w=1,h=1;};
    struct Slot {
        // free(0) -> recording(1, engine thread) -> submitted(2) -> free. The worker frees a
        // submitted slot only once its copy has completed on the GPU (or the device is gone).
        std::atomic<int> state{0};
        ComPtr<ID3D12Resource> buffer;
        ComPtr<ID3D12Device> device;
        uint64_t capacity=0;
        ComPtr<ID3D12Fence> fence; // the last copy's fence point, kept until the buffer goes
        uint64_t value=0,order=0,epoch=0;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        uint32_t width=0,height=0,pitch=0,hdrMode=0,frameWidth=0,left=0;
    };
    std::atomic<bool> armed{false},shutdown{false},busy{false};
    std::atomic<uint64_t> epoch{1};
    std::atomic<uint32_t> intervalMs{250};
    std::array<Slot,kSlots> slots;
    HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HANDLE gpuDone=CreateEventW(nullptr,FALSE,FALSE,nullptr); // set by the GPU when a copy lands
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    mutable std::mutex mutex; // options, region, published, worker
    ScreenTextOptions options;
    Region region;
    ScreenTextStatus published;
    std::thread worker;
    // Engine thread only.
    int pending=-1;
    int64_t lastRecordMs=INT64_MIN/2;
    uint64_t nextOrder=0;
    bool formatLogged=false,allocationLogged=false;

    ~State(){
        for(HANDLE h:{wake,gpuDone,done})if(h)CloseHandle(h);
    }
    void run();
};

void ScreenTextCapture::State::run(){
    try{winrt::init_apartment(winrt::apartment_type::multi_threaded);}catch(...){}
    OcrReader reader;
    screentext::LineTracker tracker;
    uint64_t trackerEpoch=0;
    std::wstring openedLanguage,lastOpenError;
    bool opened=false;
    int64_t retryAt=0,summaryAt=nowMs();
    uint64_t summaryReads=0;double summaryMs=0;
    bool readFailureLogged=false;
    int64_t debugImageAt=0;uint32_t debugImages=0;
    std::filesystem::path debugDir;
    if(debugEnabled()){
        std::error_code ec;
        debugDir=runtime::logsDirectory()/L"screen-text";
        std::filesystem::create_directories(debugDir,ec);
        log::info("screen-text",std::format("diagnostics on: words of every read and area images in {}",debugDir.string()));
    }
    while(!shutdown.load()){
        HANDLE events[2]={wake,gpuDone};
        WaitForMultipleObjects(2,events,FALSE,100);
        if(shutdown.load())break;
        const uint64_t generation=epoch.load();
        const bool on=armed.load();
        ScreenTextOptions o;
        {std::lock_guard lock(mutex);o=options;}
        // Landed copies: the newest one of this generation is read, the others are let go.
        int take=-1;
        for(unsigned i=0;i<kSlots;++i){
            auto& slot=slots[i];
            if(slot.state.load(std::memory_order_acquire)!=2)continue;
            const uint64_t completed=slot.fence?slot.fence->GetCompletedValue():UINT64_MAX;
            if(completed!=UINT64_MAX&&completed<slot.value)continue; // still on the GPU
            if(completed==UINT64_MAX||!on||slot.epoch!=generation){slot.state.store(0,std::memory_order_release);continue;}
            if(take>=0&&slots[size_t(take)].order>slot.order){slot.state.store(0,std::memory_order_release);continue;}
            if(take>=0)slots[size_t(take)].state.store(0,std::memory_order_release);
            take=int(i);
        }
        screentext::Crop crop;
        uint32_t factor=1,left=0,frameWidth=0;
        if(take>=0){
            auto& slot=slots[size_t(take)];
            void* mapped=nullptr;
            const D3D12_RANGE readRange{0,size_t(slot.pitch)*slot.height};
            const HRESULT hr=slot.buffer->Map(0,&readRange,&mapped);
            if(SUCCEEDED(hr)&&mapped){
                crop=toCrop(static_cast<const uint8_t*>(mapped),slot.pitch,slot.format,slot.hdrMode,slot.width,slot.height,factor);
                const D3D12_RANGE none{0,0};
                slot.buffer->Unmap(0,&none);
            }else log::warn("screen-text",std::format("readback Map hr=0x{:08X}",unsigned(hr)));
            left=slot.left;frameWidth=slot.frameWidth;
            slot.state.store(0,std::memory_order_release);
        }
        if(!on){tracker.reset();continue;}
        // The recognizer for the language on screen; a missing language is retried now and then
        // (the user may be installing it in Windows Settings meanwhile).
        if(!opened||o.language!=openedLanguage||(!reader.ready()&&nowMs()>=retryAt)){
            std::wstring error;
            const bool ok=reader.open(o.language,error);
            opened=true;openedLanguage=o.language;retryAt=nowMs()+5000;
            {
                std::lock_guard lock(mutex);
                published.ready=ok;published.failed=!ok;published.error=ok?std::wstring{}:error;
                published.language=reader.language();
            }
            if(ok)log::info("screen-text",std::format("OCR ready language={} maxDim={}",utf8(reader.language()),reader.maxDim()));
            else if(error!=lastOpenError)log::warn("screen-text",std::format("OCR unavailable for language={}",utf8(o.language)));
            lastOpenError=ok?std::wstring{}:error;
        }
        if(crop.rgba.empty()||!reader.ready())continue;
        busy.store(true);
        const bool english=primaryTag(o.language)==L"en";
        const auto begin=std::chrono::steady_clock::now();
        screentext::Reading reading;
        try{
            reading=screentext::recognize(crop,[&](const uint8_t* bgra,uint32_t w,uint32_t h){return reader.read(bgra,w,h);},
                                          reader.maxDim(),english);
        }catch(const winrt::hresult_error& e){
            if(!readFailureLogged){readFailureLogged=true;log::warn("screen-text",std::format("OCR read hr=0x{:08X}",uint32_t(e.code())));}
            reading={};
        }catch(const std::exception& e){
            if(!readFailureLogged){readFailureLogged=true;log::warn("screen-text",std::string("OCR read ")+e.what());}
            reading={};
        }
        const uint32_t rawWords=uint32_t(reading.words.size());
        const std::string rawText=debugDir.empty()?std::string{}:joined(reading.words);
        auto words=screentext::dropTallWords(std::move(reading.words));
        if(o.centeredOnly&&!words.empty()&&frameWidth){
            auto kept=screentext::centeredWords(words,double(left)/factor,double(frameWidth)/factor);
            if(kept.size()!=words.size())words=std::move(kept);
        }
        auto text=screentext::clean(words,english);
        if(english&&!screentext::looksLikeWords(text))text.clear();
        if(trackerEpoch!=generation){tracker.reset();trackerEpoch=generation;}
        const bool changed=tracker.feed(text,reading.sure,nowMs());
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(!debugDir.empty()){
            log::info("screen-text-debug",std::format("words={} sure={} ms={:.1f} raw=\"{}\" kept=\"{}\" text=\"{}\" shown=\"{}\"",
                rawWords,reading.sure,ms,rawText,joined(words),utf8(text),utf8(tracker.shown())));
            if(debugImages<40&&nowMs()-debugImageAt>=2000){
                debugImageAt=nowMs();
                saveBmp(crop,debugDir/std::format("area-{:02}.bmp",debugImages++));
            }
        }
        {
            std::lock_guard lock(mutex);
            ++published.reads;published.readMs=ms;published.reading=text;published.words=rawWords;
            // stop() or a new source in the meantime: that line is not this picture's
            if(changed&&epoch.load()==generation){published.text=tracker.shown();++published.sequence;}
        }
        busy.store(false);
        ++summaryReads;summaryMs+=ms;
        if(nowMs()-summaryAt>=60000){
            // Diagnostics carry timing only, never the text read from the picture.
            log::info("screen-text",std::format("reads={} avgMs={:.1f} crop={}x{} factor={}",summaryReads,
                summaryReads?summaryMs/double(summaryReads):0.0,crop.w,crop.h,factor));
            summaryAt=nowMs();summaryReads=0;summaryMs=0;
        }
    }
    reader.close();
    SetEvent(done);
}

ScreenTextCapture::ScreenTextCapture():state_(std::make_shared<State>()){}

ScreenTextCapture::~ScreenTextCapture(){
    auto& s=*state_;
    s.armed=false;s.shutdown=true;
    if(s.wake)SetEvent(s.wake);
    if(s.worker.joinable()){
        if(s.done&&WaitForSingleObject(s.done,8000)==WAIT_OBJECT_0)s.worker.join();
        else{
            // A read still running in Windows OCR: the worker keeps the state alive until it returns.
            s.worker.detach();
            log::error("screen-text","OCR still busy at shutdown; worker detached");
            return;
        }
    }
    // Buffers go only once their last copy is done on the GPU.
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    for(auto& slot:s.slots){
        if(!slot.fence||!slot.buffer)continue;
        const uint64_t completed=slot.fence->GetCompletedValue();
        if(completed==UINT64_MAX||completed>=slot.value)continue;
        if(event&&SUCCEEDED(slot.fence->SetEventOnCompletion(slot.value,event))&&WaitForSingleObject(event,2000)==WAIT_OBJECT_0)continue;
        slot.buffer.Detach(); // never released under a copy still in flight
        log::error("screen-text","readback still pending at shutdown; buffer retained");
    }
    if(event)CloseHandle(event);
}

void ScreenTextCapture::start(const ScreenTextOptions& requested){
    auto& s=*state_;
    ScreenTextOptions o=requested;
    // The region stays inside the picture and large enough to hold a line of text.
    o.x=std::clamp(std::isfinite(o.x)?o.x:0.0f,0.0f,0.95f);
    o.y=std::clamp(std::isfinite(o.y)?o.y:0.0f,0.0f,0.95f);
    o.w=std::clamp(std::isfinite(o.w)?o.w:1.0f,0.05f,1.0f-o.x);
    o.h=std::clamp(std::isfinite(o.h)?o.h:1.0f,0.05f,1.0f-o.y);
    o.intervalMs=std::clamp(o.intervalMs,100u,2000u);
    if(o.language.size()>35||o.language.empty()||!std::all_of(o.language.begin(),o.language.end(),[](wchar_t c){
        return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||c==L'-';}))o.language=L"en";
    std::lock_guard lock(s.mutex);
    if(!s.worker.joinable()){
        if(!s.wake||!s.gpuDone||!s.done){s.published.failed=true;s.published.error=L"无法启动画面文字识别";return;}
        try{s.worker=std::thread([state=state_]{state->run();});}
        catch(const std::exception&){s.published.failed=true;s.published.error=L"无法启动画面文字识别";return;}
    }
    const bool changed=!(o==s.options);
    // (a dragged custom region changes on every slider step: only language/filter changes are logged)
    const bool noted=o.language!=s.options.language||o.centeredOnly!=s.options.centeredOnly||o.intervalMs!=s.options.intervalMs;
    s.options=o;
    s.region={o.x,o.y,o.w,o.h};
    s.intervalMs=o.intervalMs;
    const bool wasArmed=s.armed.exchange(true);
    if(!wasArmed||changed){
        ++s.epoch;
        if(!s.published.text.empty()){s.published.text.clear();++s.published.sequence;}
    }
    if(!wasArmed){
        s.published.reads=0;
        log::info("screen-text",std::format("enabled region={:.3f},{:.3f},{:.3f},{:.3f} centred={} language={} intervalMs={} slots={}",
            o.x,o.y,o.w,o.h,o.centeredOnly,utf8(o.language),o.intervalMs,kSlots));
    }else if(noted){
        log::info("screen-text",std::format("options region={:.3f},{:.3f},{:.3f},{:.3f} centred={} language={}",
            o.x,o.y,o.w,o.h,o.centeredOnly,utf8(o.language)));
    }
    s.published.active=true;
    SetEvent(s.wake);
}

void ScreenTextCapture::stop(){
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    if(!s.armed.exchange(false))return;
    ++s.epoch;
    s.published.active=false;
    if(!s.published.text.empty()){s.published.text.clear();++s.published.sequence;}
    log::info("screen-text",std::format("disabled reads={}",s.published.reads));
    if(s.wake)SetEvent(s.wake);
}

void ScreenTextCapture::sessionEnded(){
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    ++s.epoch;
    if(!s.published.text.empty()){s.published.text.clear();++s.published.sequence;}
    if(s.wake)SetEvent(s.wake);
}

ScreenTextStatus ScreenTextCapture::status()const{
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    ScreenTextStatus out=s.published;
    out.active=s.armed.load();
    return out;
}

bool ScreenTextCapture::record(ID3D12GraphicsCommandList* list,const Frame& frame){
    auto& s=*state_;
    // Off, a generated (interpolated) frame, a read still in flight or not yet due: nothing.
    if(!s.armed.load(std::memory_order_relaxed)||!list||!frame.texture||frame.generated||s.pending>=0||s.busy.load())return false;
    const int64_t now=nowMs();
    if(now-s.lastRecordMs<int64_t(s.intervalMs.load()))return false;
    const uint32_t bpp=bytesPerPixel(frame.format);
    if(!bpp){
        if(!s.formatLogged){s.formatLogged=true;log::warn("screen-text",std::format("unsupported output format {}",unsigned(frame.format)));}
        return false;
    }
    if(frame.width<32||frame.height<32)return false;
    State::Region r;
    {
        std::unique_lock lock(s.mutex,std::try_to_lock);
        if(!lock)return false; // never wait on the UI or the worker here
        r=s.region;
    }
    const uint64_t generation=s.epoch.load();
    auto edge=[](float f,uint32_t size){return uint32_t(std::lround(std::clamp(f,0.0f,1.0f)*float(size)));};
    const uint32_t x0=edge(r.x,frame.width),y0=edge(r.y,frame.height);
    const uint32_t x1=std::min(frame.width,edge(r.x+r.w,frame.width)),y1=std::min(frame.height,edge(r.y+r.h,frame.height));
    if(x1<x0+16||y1<y0+8)return false;
    const uint32_t width=x1-x0,height=y1-y0;
    const uint32_t pitch=(width*bpp+D3D12_TEXTURE_DATA_PITCH_ALIGNMENT-1)&~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT-1);
    const uint64_t bytes=uint64_t(pitch)*height;
    unsigned index=kSlots;
    for(unsigned i=0;i<kSlots;++i){
        int expected=0;
        if(s.slots[i].state.compare_exchange_strong(expected,1,std::memory_order_acquire)){index=i;break;}
    }
    if(index==kSlots)return false;
    auto& slot=s.slots[index];
    auto release=[&]{slot.state.store(0,std::memory_order_release);return false;};
    ComPtr<ID3D12Device> device;
    if(FAILED(frame.texture->GetDevice(IID_PPV_ARGS(&device))))return release();
    if(!slot.buffer||slot.device.Get()!=device.Get()||slot.capacity<bytes){
        // the old buffer may only go once its last copy is done
        if(slot.buffer&&slot.fence&&slot.fence->GetCompletedValue()<slot.value)return release();
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=(bytes+65535)&~uint64_t(65535);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> buffer;
        const HRESULT hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer));
        if(FAILED(hr)){
            if(!s.allocationLogged){s.allocationLogged=true;log::error("screen-text",std::format("readback {}x{} hr=0x{:08X}",width,height,unsigned(hr)));}
            return release();
        }
        slot.buffer=std::move(buffer);slot.device=device;slot.capacity=desc.Width;slot.fence.Reset();slot.value=0;
    }
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=frame.texture;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION target{},source{};
    target.pResource=slot.buffer.Get();target.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    target.PlacedFootprint.Offset=0;target.PlacedFootprint.Footprint={frame.format,width,height,1,pitch};
    source.pResource=frame.texture;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;source.SubresourceIndex=0;
    const D3D12_BOX box{x0,y0,0,x1,y1,1};
    list->CopyTextureRegion(&target,0,0,0,&source,&box);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
    list->ResourceBarrier(1,&barrier); // back in COMMON, as the presenter hands it over
    slot.format=frame.format;slot.width=width;slot.height=height;slot.pitch=pitch;slot.hdrMode=frame.hdrMode;
    slot.frameWidth=frame.width;slot.left=x0;slot.epoch=generation;
    s.pending=int(index);s.lastRecordMs=now;
    return true;
}

void ScreenTextCapture::submitted(ID3D12Fence* fence,uint64_t value){
    auto& s=*state_;
    if(s.pending<0)return;
    auto& slot=s.slots[size_t(s.pending)];
    s.pending=-1;
    if(!fence||!value){slot.state.store(0,std::memory_order_release);return;}
    slot.fence=fence;slot.value=value;slot.order=++s.nextOrder;
    slot.state.store(2,std::memory_order_release);
    if(s.gpuDone)fence->SetEventOnCompletion(value,s.gpuDone); // wakes the worker when the copy lands
    if(s.wake)SetEvent(s.wake);
}

void ScreenTextCapture::discarded(){
    auto& s=*state_;
    if(s.pending<0)return;
    s.slots[size_t(s.pending)].state.store(0,std::memory_order_release);
    s.pending=-1;
}

} // namespace veyra::engine
