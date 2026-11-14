#include "veyra/ngx/AnrProvider.h"
#include "veyra/Log.h"
#include <windows.h>
#include <nvsdk_ngx_params.h>
#include <filesystem>
#include <format>
#include <unordered_map>
#include <variant>
#include <map>
#include <mutex>

namespace veyra::ngx {
namespace {
// Typed implementation of the published NGX parameter interface. Unknown
// values fail explicitly; numeric conversion follows the caller's Get type.
class Parameters final : public NVSDK_NGX_Parameter {
    using Value=std::variant<unsigned long long,double,void*>;
    std::unordered_map<std::string,Value> values_;
    template<class T> void number(const char* name,T value){values_[name]=double(value);}
    template<class T> NVSDK_NGX_Result getNumber(const char* name,T* out)const{
        const auto i=values_.find(name);if(!out||i==values_.end())return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if(const auto* p=std::get_if<double>(&i->second)){*out=T(*p);return NVSDK_NGX_Result_Success;}
        if(const auto* p=std::get_if<unsigned long long>(&i->second)){*out=T(*p);return NVSDK_NGX_Result_Success;}
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    template<class T> NVSDK_NGX_Result getPointer(const char* name,T** out)const{
        const auto i=values_.find(name);if(!out||i==values_.end())return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if(const auto* p=std::get_if<void*>(&i->second)){*out=static_cast<T*>(*p);return NVSDK_NGX_Result_Success;}
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
public:
    void Set(const char* n,unsigned long long v)override{values_[n]=v;}
    void Set(const char* n,float v)override{number(n,v);}
    void Set(const char* n,double v)override{number(n,v);}
    void Set(const char* n,unsigned v)override{number(n,v);}
    void Set(const char* n,int v)override{number(n,v);}
    void Set(const char* n,ID3D11Resource* v)override{values_[n]=static_cast<void*>(v);}
    void Set(const char* n,ID3D12Resource* v)override{values_[n]=static_cast<void*>(v);}
    void Set(const char* n,void* v)override{values_[n]=v;}
    NVSDK_NGX_Result Get(const char* n,unsigned long long* v)const override{return getNumber(n,v);}
    NVSDK_NGX_Result Get(const char* n,float* v)const override{return getNumber(n,v);}
    NVSDK_NGX_Result Get(const char* n,double* v)const override{return getNumber(n,v);}
    NVSDK_NGX_Result Get(const char* n,unsigned* v)const override{return getNumber(n,v);}
    NVSDK_NGX_Result Get(const char* n,int* v)const override{return getNumber(n,v);}
    NVSDK_NGX_Result Get(const char* n,ID3D11Resource** v)const override{return getPointer(n,v);}
    NVSDK_NGX_Result Get(const char* n,ID3D12Resource** v)const override{return getPointer(n,v);}
    NVSDK_NGX_Result Get(const char* n,void** v)const override{return getPointer(n,v);}
    void Reset()override{values_.clear();}
};
using InitFn=NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
using PopulateFn=NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
using CreateFn=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using EvaluateFn=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFn=NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using ShutdownFn=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
NVSDK_NGX_Result safeInit(InitFn init,PopulateFn populate,ID3D12Device* device,const wchar_t* path,NVSDK_NGX_Parameter* params,unsigned& seh){
    __try{auto result=init(1,path,device,NVSDK_NGX_Version_API,nullptr);if(result==NVSDK_NGX_Result_Success&&populate)result=populate(params);return result;}
    __except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
}
namespace {
// NGX initialization/shutdown is device-scoped, while parameter blocks and
// features are graph-scoped. A settings transaction can retain the old graph
// while constructing its replacement: the old graph must not shut the new one
// down. Weak cache entries never keep an unused module/device alive.
struct SharedAnrRuntime {
    HMODULE module=nullptr;ID3D12Device* device=nullptr;bool initialized=false;
    CreateFn create=nullptr;EvaluateFn evaluate=nullptr;ReleaseFn release=nullptr;
    PopulateFn populate=nullptr;ShutdownFn shutdown=nullptr;
    ~SharedAnrRuntime(){
        if(initialized&&shutdown){unsigned seh=0;
            auto call=[](ShutdownFn fn,ID3D12Device* d,unsigned* code)->NVSDK_NGX_Result{
                __try{return fn(d);}__except(EXCEPTION_EXECUTE_HANDLER){*code=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}};
            const auto result=call(shutdown,device,&seh);log::info("anr",std::format("Shutdown last device owner result=0x{:X} seh=0x{:X}",unsigned(result),seh));
        }
        if(module)FreeLibrary(module);
        if(device)device->Release();
    }
};
std::mutex runtimeMutex;
std::map<std::pair<ID3D12Device*,std::wstring>,std::weak_ptr<SharedAnrRuntime>> runtimes;
NVSDK_NGX_Result safePopulate(PopulateFn fn,NVSDK_NGX_Parameter* params,unsigned& seh){
    __try{return fn(params);}__except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
}
struct AnrProvider::Impl {
    std::shared_ptr<SharedAnrRuntime> runtime;
    Parameters params;
    ~Impl(){
        // Last-owner shutdown and a replacement Init must serialize under the
        // same cache lock. A weak cache alone leaves a destruction/open race.
        std::lock_guard lock(runtimeMutex);
        runtime.reset();
    }
};
AnrProvider::AnrProvider():impl_(std::make_unique<Impl>()){}
AnrProvider::~AnrProvider()=default;
std::wstring AnrProvider::directory(const std::wstring& root){return (std::filesystem::path(root)/L"anr").wstring();}
bool AnrProvider::available(const std::wstring& root){std::error_code ec;return std::filesystem::is_regular_file(std::filesystem::path(directory(root))/L"nvngx_dlssg.dll",ec);}
bool AnrProvider::open(ID3D12Device* device,const std::wstring& root,unsigned& maxGenerated){
    auto& s=*impl_;if(s.runtime||!device)return false;
    const auto path=std::filesystem::absolute(std::filesystem::path(directory(root))/L"nvngx_dlssg.dll").lexically_normal();
    unsigned seh=0;NVSDK_NGX_Result result=NVSDK_NGX_Result_Fail;
    {
        std::lock_guard lock(runtimeMutex);
        for(auto i=runtimes.begin();i!=runtimes.end();)if(i->second.expired())i=runtimes.erase(i);else ++i;
        const auto key=std::make_pair(device,path.wstring());
        s.runtime=runtimes[key].lock();
        if(!s.runtime){
            auto runtime=std::make_shared<SharedAnrRuntime>();
            runtime->module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!runtime->module){log::error("anr",std::format("LoadLibrary error={}",GetLastError()));return false;}
            const auto init=reinterpret_cast<InitFn>(GetProcAddress(runtime->module,"NVSDK_NGX_D3D12_Init_Ext"));
            runtime->populate=reinterpret_cast<PopulateFn>(GetProcAddress(runtime->module,"NVSDK_NGX_D3D12_PopulateParameters_Impl"));
            runtime->create=reinterpret_cast<CreateFn>(GetProcAddress(runtime->module,"NVSDK_NGX_D3D12_CreateFeature"));
            runtime->evaluate=reinterpret_cast<EvaluateFn>(GetProcAddress(runtime->module,"NVSDK_NGX_D3D12_EvaluateFeature"));
            runtime->release=reinterpret_cast<ReleaseFn>(GetProcAddress(runtime->module,"NVSDK_NGX_D3D12_ReleaseFeature"));
            runtime->shutdown=reinterpret_cast<ShutdownFn>(GetProcAddress(runtime->module,"NVSDK_NGX_D3D12_Shutdown1"));
            if(!init||!runtime->populate||!runtime->create||!runtime->evaluate||!runtime->release||!runtime->shutdown){log::error("anr","required direct-provider export missing");return false;}
            result=safeInit(init,nullptr,device,path.parent_path().c_str(),&s.params,seh);
            if(result!=NVSDK_NGX_Result_Success){log::error("anr",std::format("Init failed result=0x{:X} seh=0x{:X}",unsigned(result),seh));return false;}
            runtime->initialized=true;runtime->device=device;device->AddRef();
            s.runtime=runtime;runtimes[key]=runtime;
        }
        result=safePopulate(s.runtime->populate,&s.params,seh);
    }
    unsigned maximum=0;const auto cap=s.params.Get("DLSSG.MultiFrameCountMax",&maximum);
    // The graph validates actual provider capability before creation.
    maxGenerated=maximum?std::min(maximum,5u):0u;
    log::info("anr",std::format("Init result=0x{:X} seh=0x{:X} maxQuery=0x{:X} runtimeMax={} experimentMax={} (ANR FG V.B15; estimated video guidance)",unsigned(result),seh,unsigned(cap),maximum,maxGenerated));
    return result==NVSDK_NGX_Result_Success&&maxGenerated>0;
}
NVSDK_NGX_Parameter* AnrProvider::parameters(){return &impl_->params;}
NVSDK_NGX_Result AnrProvider::create(ID3D12GraphicsCommandList* l,NVSDK_NGX_Feature f,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h){return impl_->runtime->create(l,f,p,h);}
NVSDK_NGX_Result AnrProvider::evaluate(ID3D12GraphicsCommandList* l,const NVSDK_NGX_Handle* h,NVSDK_NGX_Parameter* p){return impl_->runtime->evaluate(l,h,p,nullptr);}
NVSDK_NGX_Result AnrProvider::release(NVSDK_NGX_Handle* h){return impl_->runtime->release(h);}
}
