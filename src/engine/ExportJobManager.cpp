#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
#include <chrono>
#include <type_traits>
#include <deque>
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
    // Version 3 carries the effect chain alongside the settings struct. The
    // chain is fixed-capacity and trivially copyable, so it can cross the
    // process boundary; a worker that sees another version refuses the job
    // instead of reading a mismatched layout.
    DWORD signature=magic,version=5,bytes=sizeof(Shared); // R5.3 independent colour settings
    EnhancementSettings settings;
    EffectChain chain;
    wchar_t input[32768]{},output[32768]{};
    unsigned hevc=0,maxFrames=0;
    int audioStreamIndex=-1;
    double trimStartSeconds=0.0,trimEndSeconds=0.0;
    uint32_t rateControl=uint32_t(sink::ExportRateControl::Cq);
    volatile LONG64 sourceFrames=0,generated=0,holds=0,encoded=0;
    volatile LONG messageLock=0;wchar_t message[1024]{};
    volatile LONG cancel=0,pause=0,watching=0,state=LONG(ExportState::Preparing),progress=0;
};
static_assert(std::is_trivially_copyable_v<Shared>);
void close(HANDLE& h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
const wchar_t* label(ExportState s){switch(s){case ExportState::Preparing:return L"正在准备独立导出任务";case ExportState::Running:return L"正在编码";case ExportState::Paused:return L"导出已暂停，观看继续";case ExportState::Finishing:return L"正在封装和保存输出";case ExportState::Succeeded:return L"导出完成";case ExportState::Failed:return L"导出失败；详见独立任务日志，partial已保留";case ExportState::Cancelled:return L"导出已取消；已写入的partial保留";default:return L"尚无导出任务";}}
}
struct ExportJobManager::Impl {
    HANDLE mapping=nullptr,process=nullptr,job=nullptr;Shared* shared=nullptr;
    ExportJobSnapshot snapshot;ULONGLONG cancelAt=0,startTick=0;
    struct Request { std::wstring input,output; EnhancementSettings settings; bool hevc=false; unsigned maxFrames=0; int audioStreamIndex=-1; double trimStart=0,trimEnd=0; sink::ExportRateControl rateControl=sink::ExportRateControl::Cq; };
    std::deque<Request> queue;
    uint64_t queueFailures=0;std::wstring lastQueueFailure;bool launchingQueued=false;
    // Starts queued items in order until one launches; each refusal is recorded.
    bool startNext(ExportJobManager& owner){
        while(!queue.empty()){
            auto next=std::move(queue.front());queue.pop_front();
            launchingQueued=true;
            const bool started=owner.start(next.input,next.output,next.settings,next.hevc,next.maxFrames,next.audioStreamIndex,next.trimStart,next.trimEnd,next.rateControl);
            launchingQueued=false;
            if(started)return true;
            ++queueFailures;
            lastQueueFailure=std::filesystem::path(next.output).filename().wstring()+L"："+snapshot.message;
            log::warn("export-queue",std::format("queued item refused output={} reason={} failures={}",utf8(next.output),utf8(snapshot.message),queueFailures));
        }
        return false;
    }
    void clear(){if(shared){UnmapViewOfFile(shared);shared=nullptr;}close(process);close(job);close(mapping);cancelAt=0;}
    ~Impl(){clear();}
};
ExportJobManager::ExportJobManager():p_(std::make_unique<Impl>()){}
// Shutdown is bounded: the job object is KILL_ON_CLOSE, so a worker still
// draining NVENC is terminated by clear() after a short grace instead of
// stalling process exit for up to five seconds.
ExportJobManager::~ExportJobManager(){cancel();clearQueue();if(p_->process)WaitForSingleObject(p_->process,1000);p_->clear();}
bool ExportJobManager::start(const std::wstring& input,const std::wstring& output,EnhancementSettings settings,bool hevc,unsigned maxFrames,int audioStreamIndex,double trimStartSeconds,double trimEndSeconds,sink::ExportRateControl rateControl){
    // Busy means a launched worker not yet collected. Not poll(): polling here
    // starts the next queued item, which made a queued conflict look "busy"
    // and dropped it without a failure.
    if(p_->shared)return false;p_->clear();
    if(!p_->launchingQueued){p_->queueFailures=0;p_->lastQueueFailure.clear();}
    const auto failures=p_->queueFailures;const auto lastFailure=p_->lastQueueFailure;
    p_->snapshot={};p_->snapshot.queueFailures=failures;p_->snapshot.lastQueueFailure=lastFailure;
    auto fail=[&](const wchar_t* message){const DWORD error=GetLastError();p_->clear();p_->snapshot.state=ExportState::Failed;p_->snapshot.message=message;log::error("export-worker",std::format("launch failed error={}",error));return false;};
    if(input.empty()||output.empty()||input.size()>=32768||output.size()>=32768||!settings.validate().empty()||input.starts_with(L"capture:")||input.starts_with(L"capture2:"))return fail(L"请选择本地视频与有效导出设置");
    if(rateControl!=sink::ExportRateControl::Cq&&settings.exportBitrateMbps==0)return fail(L"CBR/VBR 需要指定码率；CQ 不使用码率");
    if(std::filesystem::exists(output)||std::filesystem::exists(output+L".partial"))return fail(L"输出或partial文件已存在，请选择新文件名");
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    p_->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(Shared),nullptr);
    if(!p_->mapping)return fail(L"无法创建导出通信资源");
    p_->shared=static_cast<Shared*>(MapViewOfFile(p_->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!p_->shared)return fail(L"无法映射导出通信资源");
    if(!std::isfinite(trimStartSeconds)||!std::isfinite(trimEndSeconds)||trimStartSeconds<0||trimEndSeconds<0||(trimEndSeconds>0&&trimEndSeconds<=trimStartSeconds))return fail(L"导出剪辑范围无效");
    new(p_->shared) Shared{};auto& s=*p_->shared;s.settings=settings;s.settings.nrPolicy=pipeline::NrSizePolicy::Native;s.chain=toChain(s.settings);s.hevc=hevc;s.maxFrames=maxFrames;s.rateControl=uint32_t(rateControl);
    s.audioStreamIndex=audioStreamIndex;
    s.trimStartSeconds=trimStartSeconds;s.trimEndSeconds=trimEndSeconds;
    wcscpy_s(s.input,input.c_str());wcscpy_s(s.output,output.c_str());
    p_->job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!p_->job||!SetInformationJobObject(p_->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return fail(L"无法建立导出进程生命周期");
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<std::byte> attrs(bytes);
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;si.StartupInfo.wShowWindow=SW_HIDE;si.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    if(!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&bytes))return fail(L"无法初始化导出进程");
    const bool attr=UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&p_->mapping,sizeof(HANDLE),nullptr,nullptr)!=FALSE;
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto cmd=std::format(L"\"{}\" --export-worker {}",exe,reinterpret_cast<uintptr_t>(p_->mapping));PROCESS_INFORMATION pi{};
    const bool started=attr&&CreateProcessW(exe,cmd.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,nullptr,&si.StartupInfo,&pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);SetHandleInformation(p_->mapping,HANDLE_FLAG_INHERIT,0);
    if(!started)return fail(L"无法启动导出进程");p_->process=pi.hProcess;
    if(!AssignProcessToJobObject(p_->job,p_->process)){TerminateProcess(p_->process,1);CloseHandle(pi.hThread);return fail(L"导出进程隔离失败");}
    if(ResumeThread(pi.hThread)==DWORD(-1)){TerminateProcess(p_->process,1);CloseHandle(pi.hThread);return fail(L"无法运行导出进程");}CloseHandle(pi.hThread);
    p_->snapshot.state=ExportState::Preparing;p_->snapshot.output=output;p_->snapshot.jobId=(GetTickCount64()<<16)^pi.dwProcessId;p_->snapshot.frozenRevision=settings.revision;p_->snapshot.frozen=s.settings;p_->startTick=GetTickCount64();
    p_->snapshot.workerPid=pi.dwProcessId;
    p_->snapshot.workerLog=std::filesystem::absolute(runtime::logsDirectory()/std::format("export-worker-{}.log",pi.dwProcessId)).wstring();
    log::info("export-worker",std::format("started jobId={} pid={} frozenRevision={} nativeNR=true independentPlayback=true log={}",p_->snapshot.jobId,pi.dwProcessId,settings.revision,std::filesystem::path(p_->snapshot.workerLog).string()));return true;
}
bool ExportJobManager::enqueue(const std::wstring& input,const std::wstring& output,EnhancementSettings settings,bool hevc,unsigned maxFrames,int audioStreamIndex,double trimStartSeconds,double trimEndSeconds,sink::ExportRateControl rateControl){
    if(poll().active()||!p_->queue.empty()){p_->queue.push_back({input,output,settings,hevc,maxFrames,audioStreamIndex,trimStartSeconds,trimEndSeconds,rateControl});return true;}
    // A new batch from idle starts with a clean failure record.
    p_->queueFailures=0;p_->lastQueueFailure.clear();
    return start(input,output,settings,hevc,maxFrames,audioStreamIndex,trimStartSeconds,trimEndSeconds,rateControl);
}
void ExportJobManager::clearQueue(){p_->queue.clear();}
size_t ExportJobManager::queuedCount() const{return p_->queue.size();}
void ExportJobManager::cancel(){if(p_->shared&&p_->snapshot.active()){InterlockedExchange(&p_->shared->cancel,1);if(!p_->cancelAt)p_->cancelAt=GetTickCount64();}p_->queue.clear();}
void ExportJobManager::pause(bool v){if(p_->shared)InterlockedExchange(&p_->shared->pause,v);}
void ExportJobManager::watching(bool v){if(p_->shared)InterlockedExchange(&p_->shared->watching,v);}
ExportJobSnapshot ExportJobManager::poll(){
    if(!p_->shared&&!p_->queue.empty())p_->startNext(*this);
    p_->snapshot.queueFailures=p_->queueFailures;p_->snapshot.lastQueueFailure=p_->lastQueueFailure;
    if(!p_->shared){p_->snapshot.queuePosition=0;p_->snapshot.queued=p_->queue.size();return p_->snapshot;}
    auto& s=*p_->shared;p_->snapshot.sourceFrames=InterlockedCompareExchange64(&s.sourceFrames,0,0);p_->snapshot.generated=InterlockedCompareExchange64(&s.generated,0,0);p_->snapshot.holds=InterlockedCompareExchange64(&s.holds,0,0);p_->snapshot.encoded=InterlockedCompareExchange64(&s.encoded,0,0);p_->snapshot.state=static_cast<ExportState>(InterlockedCompareExchange(&s.state,0,0));p_->snapshot.progress=InterlockedCompareExchange(&s.progress,0,0)/10000.0;p_->snapshot.queued=p_->queue.size();p_->snapshot.queuePosition=p_->queue.empty()?0:1;
    if(p_->snapshot.progress>0.001&&p_->startTick){const double elapsed=double(GetTickCount64()-p_->startTick)/1000.0;p_->snapshot.etaSeconds=std::max(0.0,elapsed*(1.0-p_->snapshot.progress)/p_->snapshot.progress);}else p_->snapshot.etaSeconds=0;
    if(p_->cancelAt&&GetTickCount64()-p_->cancelAt>5000&&WaitForSingleObject(p_->process,0)==WAIT_TIMEOUT)TerminateJobObject(p_->job,3);
    if(WaitForSingleObject(p_->process,0)==WAIT_OBJECT_0){DWORD code=1;GetExitCodeProcess(p_->process,&code);p_->snapshot.state=code==0?ExportState::Succeeded:code==3||p_->cancelAt?ExportState::Cancelled:ExportState::Failed;if(code==0)p_->snapshot.progress=1;
        // Preserve the worker's actual outcome, including codec substitutions.
        const bool explained=(p_->snapshot.state==ExportState::Failed||p_->snapshot.state==ExportState::Succeeded)&&InterlockedCompareExchange(&s.state,0,0)==LONG(p_->snapshot.state)&&s.message[0];
        p_->snapshot.message=explained?s.message:label(p_->snapshot.state);
        const auto& final=p_->snapshot;
        log::info("export-worker",std::format("finished jobId={} pid={} exit={} state={} source={} generated={} holds={} encoded={} log={}",final.jobId,final.workerPid,code,int(final.state),final.sourceFrames,final.generated,final.holds,final.encoded,std::filesystem::path(final.workerLog).string()));
        log::info("export-worker",utf8(final.message));
        if(final.state==ExportState::Failed)p_->snapshot.message+=L"\n日志："+final.workerLog;
        p_->clear();
        if(!p_->queue.empty()){
            // The finished job's outcome is reported by this poll; the next one
            // is launched on the following poll so the UI sees both states.
            p_->snapshot.queued=p_->queue.size();p_->snapshot.queuePosition=0;
            return p_->snapshot;
        }
    }
    if(p_->shared&&InterlockedCompareExchange(&p_->shared->messageLock,1,0)==0){if(p_->shared->message[0])p_->snapshot.message=p_->shared->message;InterlockedExchange(&p_->shared->messageLock,0);}if(p_->snapshot.message.empty()||p_->snapshot.state==ExportState::Paused||p_->snapshot.state==ExportState::Cancelled)p_->snapshot.message=label(p_->snapshot.state);return p_->snapshot;
}
int runExportWorker(HANDLE mapping){
    auto s=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!s)return 1;
    if(s->signature!=magic||s->version!=5||s->bytes!=sizeof(Shared)||s->audioStreamIndex< -1||s->rateControl>uint32_t(sink::ExportRateControl::Cq)||!std::isfinite(s->trimStartSeconds)||!std::isfinite(s->trimEndSeconds)||s->trimStartSeconds<0||s->trimEndSeconds<0||(s->trimEndSeconds>0&&s->trimEndSeconds<=s->trimStartSeconds)||!s->settings.validate().empty()||s->input[32767]||s->output[32767]){UnmapViewOfFile(s);CloseHandle(mapping);return 1;}
    Logger::instance().openFile((runtime::logsDirectory()/std::format("export-worker-{}.log",GetCurrentProcessId())).wstring());
    // The chain is the description of record for this job; the settings struct
    // stays the runtime input until the executor consumes chains directly.
    // Re-deriving one from the other must agree, otherwise the job is refused.
    {auto fromChainSettings=s->settings;fromChain(s->chain,fromChainSettings);
     if(fromChainSettings!=s->settings){UnmapViewOfFile(s);CloseHandle(mapping);return 1;}}
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
    auto options=PlayerOptions::from(settings);options.audioStreamIndex=s->audioStreamIndex;options.exportStartSeconds=s->trimStartSeconds;options.exportEndSeconds=s->trimEndSeconds;options.exportRateControl=static_cast<sink::ExportRateControl>(s->rateControl);
    bool ok=false;try{ok=exportVideo(s->input,s->output,options,s->hevc!=0,cancel,[&](double p,const std::wstring& message){if(InterlockedCompareExchange(&s->messageLock,1,0)==0){wcsncpy_s(s->message,message.c_str(),_TRUNCATE);InterlockedExchange(&s->messageLock,0);}
InterlockedExchange(&s->progress,LONG(std::clamp(p,0.0,.999)*10000));if(p>=.99)InterlockedExchange(&s->state,LONG(ExportState::Finishing));},s->maxFrames,frameBoundary,[&](const ExportCounts& count){InterlockedExchange64(&s->sourceFrames,count.source);InterlockedExchange64(&s->generated,count.generated);InterlockedExchange64(&s->holds,count.holds);InterlockedExchange64(&s->encoded,count.encoded);});}catch(...){log::error("export-worker","unhandled job exception");}
    done=true;monitor.join();InterlockedExchange(&s->state,LONG(ok?ExportState::Succeeded:cancel?ExportState::Cancelled:ExportState::Failed));UnmapViewOfFile(s);CloseHandle(mapping);return ok?0:cancel?3:1;
}
}
