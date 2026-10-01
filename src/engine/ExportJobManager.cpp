#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
#include <chrono>
#include <type_traits>
namespace veyra::engine {
namespace {
std::string utf8(std::wstring_view value) {
    if(value.empty())return {};
    const int bytes=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(bytes,'\0');
    if(bytes)WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),result.data(),bytes,nullptr,nullptr);
    return result;
}
constexpr DWORD magic=0x56585931;
struct Shared {
    DWORD signature=magic,version=2,bytes=sizeof(Shared);
    EnhancementSettings settings;
    wchar_t input[32768]{},output[32768]{};
    unsigned hevc=0,maxFrames=0;
    int audioStreamIndex=-1;
    volatile LONG64 sourceFrames=0,generated=0,holds=0,encoded=0;
    volatile LONG messageLock=0;wchar_t message[1024]{};
    volatile LONG cancel=0,pause=0,watching=0,state=LONG(ExportState::Preparing),progress=0;
};
static_assert(std::is_trivially_copyable_v<Shared>);
void close(HANDLE& h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
const wchar_t* label(ExportState s){switch(s){case ExportState::Preparing:return L"Preparing standalone export task";case ExportState::Running:return L"Encoding";case ExportState::Paused:return L"Export paused; playback continues";case ExportState::Finishing:return L"Muxing and saving output";case ExportState::Succeeded:return L"Export complete";case ExportState::Failed:return L"Export failed; see the standalone task log, partial kept";case ExportState::Cancelled:return L"Export cancelled; the partial written so far is kept";default:return L"No export task yet";}}
}
struct ExportJobManager::Impl {
    HANDLE mapping=nullptr,process=nullptr,job=nullptr;Shared* shared=nullptr;
    ExportJobSnapshot snapshot;ULONGLONG cancelAt=0;
    void clear(){if(shared){UnmapViewOfFile(shared);shared=nullptr;}close(process);close(job);close(mapping);cancelAt=0;}
    ~Impl(){clear();}
};
ExportJobManager::ExportJobManager():p_(std::make_unique<Impl>()){}
// Shutdown is bounded: the job object is KILL_ON_CLOSE, so a worker still
// draining NVENC is terminated by clear() after a short grace instead of
// stalling process exit for up to five seconds.
ExportJobManager::~ExportJobManager(){cancel();if(p_->process)WaitForSingleObject(p_->process,1000);p_->clear();}
bool ExportJobManager::start(const std::wstring& input,const std::wstring& output,EnhancementSettings settings,bool hevc,unsigned maxFrames,int audioStreamIndex){
    if(poll().active())return false;p_->clear();p_->snapshot={};
    auto fail=[&](const wchar_t* message){const DWORD error=GetLastError();p_->clear();p_->snapshot.state=ExportState::Failed;p_->snapshot.message=message;log::error("export-worker",std::format("launch failed error={}",error));return false;};
    if(input.empty()||output.empty()||input.size()>=32768||output.size()>=32768||!settings.validate().empty()||input.starts_with(L"capture:")||input.starts_with(L"capture2:"))return fail(L"Please select a local video and valid export settings");
    if(std::filesystem::exists(output)||std::filesystem::exists(output+L".partial"))return fail(L"The output or partial file already exists; please choose a new file name");
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    p_->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(Shared),nullptr);
    if(!p_->mapping)return fail(L"Could not create export communication resource");
    p_->shared=static_cast<Shared*>(MapViewOfFile(p_->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!p_->shared)return fail(L"Could not map export communication resource");
    new(p_->shared) Shared{};auto& s=*p_->shared;s.settings=settings;s.settings.nrPolicy=pipeline::NrSizePolicy::Native;s.hevc=hevc;s.maxFrames=maxFrames;
    s.audioStreamIndex=audioStreamIndex;
    wcscpy_s(s.input,input.c_str());wcscpy_s(s.output,output.c_str());
    p_->job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!p_->job||!SetInformationJobObject(p_->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return fail(L"Could not establish export process lifetime");
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<std::byte> attrs(bytes);
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;si.StartupInfo.wShowWindow=SW_HIDE;si.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    if(!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&bytes))return fail(L"Could not initialize export process");
    const bool attr=UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&p_->mapping,sizeof(HANDLE),nullptr,nullptr)!=FALSE;
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto cmd=std::format(L"\"{}\" --export-worker {}",exe,reinterpret_cast<uintptr_t>(p_->mapping));PROCESS_INFORMATION pi{};
    const bool started=attr&&CreateProcessW(exe,cmd.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,nullptr,&si.StartupInfo,&pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);SetHandleInformation(p_->mapping,HANDLE_FLAG_INHERIT,0);
    if(!started)return fail(L"Could not start export process");p_->process=pi.hProcess;
    if(!AssignProcessToJobObject(p_->job,p_->process)){TerminateProcess(p_->process,1);CloseHandle(pi.hThread);return fail(L"Export process isolation failed");}
    if(ResumeThread(pi.hThread)==DWORD(-1)){TerminateProcess(p_->process,1);CloseHandle(pi.hThread);return fail(L"Could not run export process");}CloseHandle(pi.hThread);
    p_->snapshot.state=ExportState::Preparing;p_->snapshot.output=output;p_->snapshot.jobId=(GetTickCount64()<<16)^pi.dwProcessId;p_->snapshot.frozenRevision=settings.revision;p_->snapshot.frozen=s.settings;
    p_->snapshot.workerPid=pi.dwProcessId;
    p_->snapshot.workerLog=std::filesystem::absolute(runtime::logsDirectory()/std::format("export-worker-{}.log",pi.dwProcessId)).wstring();
    log::info("export-worker",std::format("started jobId={} pid={} frozenRevision={} nativeNR=true independentPlayback=true log={}",p_->snapshot.jobId,pi.dwProcessId,settings.revision,std::filesystem::path(p_->snapshot.workerLog).string()));return true;
}
void ExportJobManager::cancel(){if(p_->shared&&p_->snapshot.active()){InterlockedExchange(&p_->shared->cancel,1);if(!p_->cancelAt)p_->cancelAt=GetTickCount64();}}
void ExportJobManager::pause(bool v){if(p_->shared)InterlockedExchange(&p_->shared->pause,v);}
void ExportJobManager::watching(bool v){if(p_->shared)InterlockedExchange(&p_->shared->watching,v);}
ExportJobSnapshot ExportJobManager::poll(){
    if(!p_->shared)return p_->snapshot;
    auto& s=*p_->shared;p_->snapshot.sourceFrames=InterlockedCompareExchange64(&s.sourceFrames,0,0);p_->snapshot.generated=InterlockedCompareExchange64(&s.generated,0,0);p_->snapshot.holds=InterlockedCompareExchange64(&s.holds,0,0);p_->snapshot.encoded=InterlockedCompareExchange64(&s.encoded,0,0);p_->snapshot.state=static_cast<ExportState>(InterlockedCompareExchange(&s.state,0,0));p_->snapshot.progress=InterlockedCompareExchange(&s.progress,0,0)/10000.0;
    if(p_->cancelAt&&GetTickCount64()-p_->cancelAt>5000&&WaitForSingleObject(p_->process,0)==WAIT_TIMEOUT)TerminateJobObject(p_->job,3);
    if(WaitForSingleObject(p_->process,0)==WAIT_OBJECT_0){DWORD code=1;GetExitCodeProcess(p_->process,&code);p_->snapshot.state=code==0?ExportState::Succeeded:code==3||p_->cancelAt?ExportState::Cancelled:ExportState::Failed;if(code==0)p_->snapshot.progress=1;
        // Preserve the worker's actual outcome, including codec substitutions.
        const bool explained=(p_->snapshot.state==ExportState::Failed||p_->snapshot.state==ExportState::Succeeded)&&InterlockedCompareExchange(&s.state,0,0)==LONG(p_->snapshot.state)&&s.message[0];
        p_->snapshot.message=explained?s.message:label(p_->snapshot.state);
        const auto& final=p_->snapshot;
        log::info("export-worker",std::format("finished jobId={} pid={} exit={} state={} source={} generated={} holds={} encoded={} log={}",final.jobId,final.workerPid,code,int(final.state),final.sourceFrames,final.generated,final.holds,final.encoded,std::filesystem::path(final.workerLog).string()));
        log::info("export-worker",utf8(final.message));
        if(final.state==ExportState::Failed)p_->snapshot.message+=L"\nLog: "+final.workerLog;
        p_->clear();}
    if(p_->shared&&InterlockedCompareExchange(&p_->shared->messageLock,1,0)==0){if(p_->shared->message[0])p_->snapshot.message=p_->shared->message;InterlockedExchange(&p_->shared->messageLock,0);}if(p_->snapshot.message.empty()||p_->snapshot.state==ExportState::Paused||p_->snapshot.state==ExportState::Cancelled)p_->snapshot.message=label(p_->snapshot.state);return p_->snapshot;
}
int runExportWorker(HANDLE mapping){
    auto s=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!s)return 1;
    if(s->signature!=magic||s->version!=2||s->bytes!=sizeof(Shared)||s->audioStreamIndex< -1||!s->settings.validate().empty()||s->input[32767]||s->output[32767]){UnmapViewOfFile(s);CloseHandle(mapping);return 1;}
    Logger::instance().openFile((runtime::logsDirectory()/std::format("export-worker-{}.log",GetCurrentProcessId())).wstring());
    const auto settings=s->settings;log::info("export-frozen",std::format("revision={} nr={} sr={} multiplier={} intensity={} tone={} structure={} skin={} style={} autoMask={} ui={} total={} darken={} brighten={} color={} luminance={} flow={} content={} nativeNR=true",settings.revision,settings.nr,settings.sr,settings.multiplier,settings.model.intensity,settings.model.tone,settings.model.structure,settings.model.skin,settings.model.style,settings.model.autoMask,settings.model.uiCorrection,settings.residual.total,settings.residual.darken,settings.residual.brighten,settings.residual.color,settings.residual.luminance,int(settings.flow),int(settings.content)));std::atomic<bool> cancel=false,done=false;
    std::thread monitor([&]{while(!done){if(InterlockedCompareExchange(&s->cancel,0,0))cancel=true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}});
    auto frameBoundary=[&]{
        while(!cancel&&InterlockedCompareExchange(&s->pause,0,0)){InterlockedExchange(&s->state,LONG(ExportState::Paused));std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        InterlockedExchange(&s->state,LONG(ExportState::Running));
        // Yield only between complete source frames. No source-frame drops or
        // quality changes. Both contexts still share the physical GPU budget.
        if(InterlockedCompareExchange(&s->watching,0,0))for(int i=0;i<5&&!cancel;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return !cancel;
    };
    auto options=PlayerOptions::from(settings);options.audioStreamIndex=s->audioStreamIndex;
    bool ok=false;try{ok=exportVideo(s->input,s->output,options,s->hevc!=0,cancel,[&](double p,const std::wstring& message){if(InterlockedCompareExchange(&s->messageLock,1,0)==0){wcsncpy_s(s->message,message.c_str(),_TRUNCATE);InterlockedExchange(&s->messageLock,0);}
InterlockedExchange(&s->progress,LONG(std::clamp(p,0.0,.999)*10000));if(p>=.99)InterlockedExchange(&s->state,LONG(ExportState::Finishing));},s->maxFrames,frameBoundary,[&](const ExportCounts& count){InterlockedExchange64(&s->sourceFrames,count.source);InterlockedExchange64(&s->generated,count.generated);InterlockedExchange64(&s->holds,count.holds);InterlockedExchange64(&s->encoded,count.encoded);});}catch(...){log::error("export-worker","unhandled job exception");}
    done=true;monitor.join();InterlockedExchange(&s->state,LONG(ok?ExportState::Succeeded:cancel?ExportState::Cancelled:ExportState::Failed));UnmapViewOfFile(s);CloseHandle(mapping);return ok?0:cancel?3:1;
}
}
