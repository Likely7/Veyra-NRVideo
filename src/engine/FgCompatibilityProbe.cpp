#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/ngx/FgCompatibilitySession.h"
#include "veyra/FileIdentity.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <algorithm>
#include <cstdlib>
#include <format>
#include <mutex>
#include <set>
#include <type_traits>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
namespace {
std::wstring executable;
std::mutex cacheMutex;
std::set<std::wstring> passed;
constexpr DWORD signature=0x56464732;
struct Shared {
    DWORD magic=signature,bytes=sizeof(Shared);
    uint64_t luid=0;
    uint32_t width=0,height=0,multiplier=0,hdr=0;
    wchar_t runtime[32768]{};
    char hash[65]{};
    volatile LONG stage=0,generated=0;
};
static_assert(std::is_trivially_copyable_v<Shared>);
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct View {
    Shared* value=nullptr;
    ~View(){if(value)UnmapViewOfFile(value);}
};
std::filesystem::path logPath(DWORD pid){return runtime::logsDirectory()/std::format("fg-probe-{}.log",pid);}
[[noreturn]] void failProbe(Shared& shared, const char* operation) {
    // This disposable process owns all probe resources. A failed provider may
    // hold an internal lock, so neither ReleaseFeature nor DLL detach is safe.
    InterlockedExchange(&shared.stage,-1);
    log::error("fg-probe",std::format("{} failed; terminating isolated probe before provider teardown",operation));
    Logger::instance().flush();
    TerminateProcess(GetCurrentProcess(),1);
    std::abort();
}
}
void setFgCompatibilityProbeExecutable(std::wstring path){executable=std::move(path);}
bool checkFgCompatibility(const pipeline::EnhanceGraphDesc& desc,const gfx::D3D12DeviceContext& ctx,const std::atomic<bool>& cancel){
    if(!desc.enableFg||desc.noFeatures||desc.noNgx||desc.frameGenerationBackend!=FrameGenerationBackend::Dlss||
       !ngx::FgCompatibilitySession::requested(ctx.adapter().vendorId,ctx.adapter().deviceId))return true;
    if(executable.empty()){
        log::warn("fg-probe","diagnostic host has no child entry; external test timeout required");return true;
    }
    FileIdentity identity;IdentityError error;
    if(!computeFileIdentity((std::filesystem::path(desc.runtimeAbsPath)/L"nvngx_dlssg.dll").wstring(),identity,error))return false;
    const auto key=std::format(L"{}:{}:{}:{}:{}:{}:{}:{}",ctx.adapter().luid,ctx.adapter().deviceId,ctx.adapter().driverVersion,
        std::wstring(identity.sha256Upper.begin(),identity.sha256Upper.end()),desc.workWidth,desc.workHeight,desc.fgMultiplier,desc.hdrOutput);
    {std::lock_guard lock(cacheMutex);if(passed.contains(key))return !cancel;}
    if(cancel||desc.runtimeAbsPath.size()>=32768)return false;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle mapping{CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(Shared),nullptr)};
    if(!mapping.value)return false;
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)))};
    if(!view.value)return false;
    auto& s=*new(view.value) Shared{};
    s.luid=ctx.adapter().luid;s.width=desc.workWidth;s.height=desc.workHeight;s.multiplier=desc.fgMultiplier;s.hdr=desc.hdrOutput;
    wcscpy_s(s.runtime,desc.runtimeAbsPath.c_str());strcpy_s(s.hash,identity.sha256Upper.c_str());
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<std::byte> attrs(bytes);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
    startup.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes))return false;
    const bool inherited=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&mapping.value,sizeof(HANDLE),nullptr,nullptr)!=FALSE;
    auto command=std::format(L"\"{}\" --fg-compat-probe {}",executable,reinterpret_cast<uintptr_t>(mapping.value));
    PROCESS_INFORMATION info{};
    const bool started=inherited&&CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup.StartupInfo,&info);
    DeleteProcThreadAttributeList(startup.lpAttributeList);SetHandleInformation(mapping.value,HANDLE_FLAG_INHERIT,0);
    if(!started){log::error("fg-probe",std::format("launch failed win32={}",GetLastError()));return false;}
    Handle process{info.hProcess},thread{info.hThread};
    if(!AssignProcessToJobObject(job.value,process.value)||ResumeThread(thread.value)==DWORD(-1)){
        TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);return false;
    }
    log::info("fg-probe",std::format("started pid={} multiplier={} extent={}x{} log={}",info.dwProcessId,s.multiplier,s.width,s.height,veyra::runtime::pathUtf8(logPath(info.dwProcessId))));
    const auto start=GetTickCount64();DWORD wait=WAIT_TIMEOUT;
    const ULONGLONG timeout=GetEnvironmentVariableW(L"VEYRA_TEST_FG_PROBE_HANG",nullptr,0)?2000:60000;
    while(!cancel&&GetTickCount64()-start<timeout&&(wait=WaitForSingleObject(process.value,100))==WAIT_TIMEOUT){}
    if(wait!=WAIT_OBJECT_0){
        TerminateJobObject(job.value,124);WaitForSingleObject(process.value,5000);
        log::error("fg-probe",std::format("cancelled={} timeoutOrWaitFailure=true stage={} log={}",cancel.load(),InterlockedCompareExchange(&s.stage,0,0),veyra::runtime::pathUtf8(logPath(info.dwProcessId))));return false;
    }
    DWORD code=1;GetExitCodeProcess(process.value,&code);
    const LONG stage=InterlockedCompareExchange(&s.stage,0,0),generated=InterlockedCompareExchange(&s.generated,0,0);
    const bool ok=code==0&&stage==4&&generated==LONG(2*(s.multiplier-1));
    log::info("fg-probe",std::format("finished pid={} exit={} stage={} generated={} passed={} log={}",info.dwProcessId,code,stage,generated,ok,veyra::runtime::pathUtf8(logPath(info.dwProcessId))));
    if(ok){std::lock_guard lock(cacheMutex);passed.insert(key);}return ok&&!cancel;
}
int runFgCompatibilityProbe(HANDLE inherited){
    Handle mapping{inherited};View view{static_cast<Shared*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)))};
    if(!view.value)return 1;
    auto& s=*view.value;
    if(s.magic!=signature||s.bytes!=sizeof(Shared)||s.runtime[32767]||s.hash[64]||s.width<64||s.height<64||s.width>8192||s.height>8192||s.multiplier<2||s.multiplier>6)return 1;
    Logger::instance().openFile(logPath(GetCurrentProcessId()).wstring());
    // Keep provider-internal errors in the disposable child's diagnostic log.
    SetEnvironmentVariableW(L"VEYRA_TEST_NGX_VERBOSE", L"1");
    if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_PROBE_HANG",nullptr,0))Sleep(INFINITE);
    if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_PROBE_FAIL",nullptr,0))return 1;
    FileIdentity identity;IdentityError error;
    if(!computeFileIdentity((std::filesystem::path(s.runtime)/L"nvngx_dlssg.dll").wstring(),identity,error)||identity.sha256Upper!=s.hash)return 1;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool ok=false;
    {
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);Status status;
        gfx::DeviceContextDesc device;device.requiredVendorId=0x10de;device.requiredLuid=s.luid;device.commandSlotCount=6;
        InterlockedExchange(&s.stage,1);
        if(ctx.initialize(device,status)&&ctx.adapter().luid==s.luid&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status)){
            pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=s.width;desc.sourceHeight=desc.workHeight=s.height;
            desc.enableNr=false;desc.enableSr=false;desc.enableFg=true;desc.fgMultiplier=s.multiplier;desc.runtimeAbsPath=s.runtime;
            desc.hdrInput=desc.hdrOutput=s.hdr!=0;desc.rgbInput=!desc.hdrInput;
            InterlockedExchange(&s.stage,2);
            ok=graph.initialize(desc)&&graph.createViews();
            if(!ok)failProbe(s,"initialization");
            AVFrame* frame=av_frame_alloc();if(!frame)ok=false;
            if(frame){
                frame->format=s.hdr?AV_PIX_FMT_P010LE:AV_PIX_FMT_RGBA;
                frame->width=int(s.width);frame->height=int(s.height);
                frame->color_range=s.hdr?AVCOL_RANGE_MPEG:AVCOL_RANGE_JPEG;
                if(s.hdr){
                    frame->colorspace=AVCOL_SPC_BT2020_NCL;
                    frame->color_primaries=AVCOL_PRI_BT2020;
                    frame->color_trc=AVCOL_TRC_SMPTE2084;
                    frame->chroma_location=AVCHROMA_LOC_LEFT;
                }
                ok=ok&&av_frame_get_buffer(frame,32)>=0;
            }
            if(ok)InterlockedExchange(&s.stage,3);
            for(unsigned i=0;ok&&i<3;++i){
                if(s.hdr){
                    // Match the product's explicit PQ YUV ingress before testing RGB10 FG.
                    for(unsigned y=0;y<s.height;++y){
                        auto* row=reinterpret_cast<uint16_t*>(frame->data[0]+size_t(y)*frame->linesize[0]);
                        for(unsigned x=0;x<s.width;++x)row[x]=uint16_t((128+((x+i*8)/32+y/32)%2*480)<<6);
                    }
                    for(unsigned y=0;y<(s.height+1)/2;++y){
                        auto* row=reinterpret_cast<uint16_t*>(frame->data[1]+size_t(y)*frame->linesize[1]);
                        std::fill_n(row,2*((s.width+1)/2),uint16_t(512<<6));
                    }
                }else{
                    for(unsigned y=0;y<s.height;++y)for(unsigned x=0;x<s.width;++x){auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;const unsigned value=32+((x+i*8)/32+y/32)%2*180;p[0]=p[1]=p[2]=uint8_t(value);p[3]=255;}
                }
                pipeline::EnhanceGraph::FrameOutputs output;
                ok=graph.process(frame,i*(1000.0/60),i==0,output)&&ring.waitIdle()&&graph.resolveGeneration(output);
                if(!ok)failProbe(s,"Create/Evaluate or GPU completion");
                unsigned valid=0;
                for(unsigned j=0;ok&&j<output.batch.count;++j)if(output.batch.frames[j].kind==pipeline::FrameKind::Generated&&output.batch.frames[j].validity==pipeline::GenerationValidity::Valid)++valid;
                if(i&&valid!=s.multiplier-1)ok=false;
                if(!ok)failProbe(s,"generated output validation");
                InterlockedExchangeAdd(&s.generated,LONG(valid));
            }
            av_frame_free(&frame);ok=ring.drainQueue()&&ok;
        }
        graph.shutdown();ring.shutdown();ctx.shutdown();
    }
    ok=ok&&ngx::FgCompatibilitySession::processHealthy();
    if(ok)InterlockedExchange(&s.stage,4);
    log::info("fg-probe",std::format("Create/Evaluate probe passed={} generated={}; image quality requires separate content validation",ok,InterlockedCompareExchange(&s.generated,0,0)));
    Logger::instance().flush();CoUninitialize();return ok?0:1;
}
}
