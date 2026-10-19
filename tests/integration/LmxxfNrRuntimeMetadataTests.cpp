// Real upstream modules and real D3D12 objects. No PrepareFrame/HIP inference.
#include "veyra/pipeline/LmxxfNrBackend.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include <iostream>
#include <cstdlib>
#include <string_view>
using namespace veyra;
int wmain(int argc,wchar_t** argv){
    if(argc!=3&&argc!=4)return 2;
    const std::filesystem::path runtime=argv[1],scratch=argv[2];
    if(!runtime.is_absolute()||!scratch.is_absolute()||std::filesystem::exists(scratch))return 2;
    unsigned failures=0,checks=0;
    auto check=[&](bool ok,const char* text){++checks;failures+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;};
    gfx::D3D12DeviceContext ctx;Status status=Status::Ok;gfx::DeviceContextDesc desc{};
    if(!ctx.initialize(desc,status))return 2;
    pipeline::LmxxfNrBackend smallBackend,original;
    if(argc==4){
        if(std::wstring_view(argv[3])!=L"--reject-config")return 2;
        check(!smallBackend.open(runtime,ctx.device(),ctx.directQueue(),0x1002,true)&&
            smallBackend.error().find("Veyra-owned history")!=std::string::npos,
            "real runtime config is checked inside its own CRT after Create");
        check(!smallBackend.ready(),"rejected real runtime context is fully closed");
        return failures?1:0;
    }
    check(!smallBackend.open(runtime,ctx.device(),ctx.directQueue(),0x10de),"actual NVIDIA vendor remains blocked");
    _putenv_s("DLSS5_MULTI_PASS","3"); // Must not override either compiled model.
    check(smallBackend.open(runtime,ctx.device(),ctx.directQueue(),0x1002,true),"real Small Create/PrepareSession metadata");
    check(original.open(runtime,ctx.device(),ctx.directQueue(),0x1002,false),"real Original metadata coexists with Small");
    check(smallBackend.ready()&&original.ready()&&smallBackend.admits(1920,1080)&&!smallBackend.admits(3840,2160),"actual capability budget retained");
    original.close();check(smallBackend.ready(),"closing Original preserves Small session");smallBackend.close();
    _putenv_s("DLSS5_MULTI_PASS","");
    _putenv_s("DLSS5_TEMPORAL_MODE","1");
    check(!smallBackend.open(runtime,ctx.device(),ctx.directQueue(),0x1002,true),"hidden temporal mode cannot silently select Original geometry");
    _putenv_s("DLSS5_TEMPORAL_MODE","");
    std::filesystem::create_directories(scratch/L"assets");
    std::filesystem::copy_file(runtime/L"LmxxfNrRuntime.dll",scratch/L"LmxxfNrRuntime.dll");
    check(!smallBackend.open(scratch,ctx.device(),ctx.directQueue(),0x1002,true)&&smallBackend.error().find("Small model assets")!=std::string::npos,"real missing Small assets fail before Create; no silent fallback");
    std::filesystem::copy_file(runtime/L"LmxxfNrRuntime-Original.dll",scratch/L"LmxxfNrRuntime.dll",std::filesystem::copy_options::overwrite_existing);
    check(!smallBackend.open(scratch,ctx.device(),ctx.directQueue(),0x1002,true)&&smallBackend.error().find("model module mismatch")!=std::string::npos,"actual swapped module refuses wrong model");
    check(original.open(runtime,ctx.device(),ctx.directQueue(),0x1002,false),"real module reopens after metadata teardown");original.close();
    uint32_t removed=0;check(ctx.checkDeviceAlive(removed),"device alive after metadata lifecycle");
    std::cout<<"Real lmxxf metadata: "<<checks<<" checks, "<<failures<<" failures. NO AMD inference/quality/performance measured.\n";
    return failures?1:0;
}
