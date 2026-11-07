// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/pipeline/VfgAmpereCompat.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>
using namespace veyra::pipeline;
template<class T> void set(std::vector<std::byte>& b,size_t p,T value){std::memcpy(b.data()+p,&value,sizeof(value));}
template<class T> T get(const std::byte* b,size_t p){T v{};std::memcpy(&v,b+p,sizeof(v));return v;}
std::vector<std::byte> fatbin(uint32_t sm=89,uint32_t flags=0x00590559){
    std::vector<std::byte> b(144);set<uint32_t>(b,0,0xBA55ED50);set<uint16_t>(b,4,1);set<uint16_t>(b,6,16);set<uint64_t>(b,8,128);
    set<uint16_t>(b,16,2);set<uint16_t>(b,18,0x101);set<uint32_t>(b,20,64);set<uint64_t>(b,24,64);set<uint32_t>(b,44,sm);
    set<uint32_t>(b,80,0x464c457f);set<uint8_t>(b,84,2);set<uint8_t>(b,85,1);set<uint16_t>(b,98,190);set<uint32_t>(b,128,flags);return b;
}
std::vector<std::byte> mappedPe(){
    std::vector<std::byte> b(4096);IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;set(b,0,dos);
    IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.SizeOfOptionalHeader=sizeof(nt.OptionalHeader);nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage=4096;nt.OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={0x200,40};set(b,0x80,nt);
    IMAGE_IMPORT_DESCRIPTOR d{};d.OriginalFirstThunk=0x400;d.FirstThunk=0x500;d.Name=0x300;set(b,0x200,d);
    std::memcpy(b.data()+0x300,"nvcuda.dll",11);
    set<uint64_t>(b,0x400,0x600);set<uint64_t>(b,0x408,0x640);
    std::memcpy(b.data()+0x602,"cuDeviceGetAttribute",21);std::memcpy(b.data()+0x642,"cuModuleLoadData",17);return b;
}
int wmain(int argc,wchar_t** argv){
    unsigned checks=0,failed=0;auto check=[&](bool okay,const char* label){++checks;if(!okay){++failed;std::printf("FAIL %s\n",label);}};
    check(vfgAmpereArchitecture(8,6)&&!vfgNativeArchitecture(8,6),"SM86 gets compatibility only");
    check(vfgNativeArchitecture(8,9)&&vfgNativeArchitecture(12,0)&&vfgNativeArchitecture(12,1),"40/50 native paths preserved");
    check(!vfgAmpereArchitecture(8,0)&&!vfgNativeArchitecture(8,0)&&!vfgAmpereArchitecture(7,5),"unsupported GPU does not gain VFG");
    check(vfgReportedAttribute(6,76,2,2,0)==9,"selected SM86 minor reports9 only inside VFG");
    check(vfgReportedAttribute(6,76,1,2,0)==6,"other device retains its real attribute");
    check(vfgReportedAttribute(8,75,2,2,0)==8&&vfgReportedAttribute(6,76,2,2,1)==6,"major and failed calls untouched");
    check(vfgReportedAttribute(0,76,2,2,0)==0&&vfgReportedAttribute(9,76,2,2,0)==9,"native device minor unchanged");
    check(vfgRetargetContext(2,2,0)&&!vfgRetargetContext(1,2,0)&&!vfgRetargetContext(2,2,3),"only selected GPU current context may retarget kernels");
    std::vector<std::byte> out;VfgFatbinChanges stats;std::string why;
    const auto input=fatbin(),original=input;
    check(retargetVfgFatbin(input,out,stats,why)&&stats.containers==1&&stats.elfFlags==1,"retarget valid SM89 CUDA ELF");
    check(input==original&&get<uint32_t>(out.data(),44)==86&&get<uint32_t>(out.data(),128)==0x00560556,"source immutable, copy retargeted");
    bool exact=true;for(size_t i=0;i<input.size();++i)if(!(i>=44&&i<48)&&!(i>=128&&i<132)&&input[i]!=out[i])exact=false;
    check(exact,"kernel instructions and all unrelated bytes unchanged");
    for(auto sm:{86u,120u}){const auto native=fatbin(sm,0x00560556);check(retargetVfgFatbin(native,out,stats,why)&&!stats.containers&&out==native,"non-SM89 entries stay exact");}
    for(size_t n=0;n<input.size();++n)check(!retargetVfgFatbin({input.data(),n},out,stats,why)&&out.empty(),"truncated container rejected without output");
    for(size_t offset:{size_t(0),size_t(4),size_t(6),size_t(8),size_t(16),size_t(18),size_t(20),size_t(24),size_t(80),size_t(84),size_t(85),size_t(98),size_t(128)}){
        auto broken=input;broken[offset]^=std::byte{0xFF};check(!retargetVfgFatbin(broken,out,stats,why)&&out.empty(),"unknown header/size/ELF rejected");}
    auto image=mappedPe();std::array<uint32_t,2> imports{};
    check(findVfgCudaImports(image,imports,why)&&imports==std::array<uint32_t,2>{0x500,0x508},"named import discovery independent of address");
    for(size_t n:{size_t(0),size_t(63),size_t(127),size_t(512),size_t(2048)})check(!findVfgCudaImports({image.data(),n},imports,why),"truncated mapped PE rejected");
    auto broken=image;set<uint64_t>(broken,0x408,0x600);check(!findVfgCudaImports(broken,imports,why),"duplicate import rejected");
    broken=image;set<uint64_t>(broken,0x408,UINT64_MAX);check(!findVfgCudaImports(broken,imports,why),"missing required named import rejected");
    broken=image;set<uint32_t>(broken,0x200+offsetof(IMAGE_IMPORT_DESCRIPTOR,FirstThunk),0x501);check(!findVfgCudaImports(broken,imports,why),"unaligned pointer rejected");
    auto kernel=GetModuleHandleW(L"kernel32.dll");VfgAmpereCompat unknown;
    check(!unknown.install(kernel,kernel,0,why)&&!unknown.installed(),"unrecognized runtime never patched");
    if(argc==2){
        const std::filesystem::path file=argv[1];
        auto module=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(module!=nullptr,"load existing audited VFG runtime");if(!module)return 2;
        auto* base=reinterpret_cast<const std::byte*>(module);
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        check(findVfgCudaImports({base,nt->OptionalHeader.SizeOfImage},imports,why),"discover real SDK CUDA imports");
        std::array<void*,2> saved{};std::array<DWORD,2> protections{};
        for(size_t i=0;i<2;++i){saved[i]=*reinterpret_cast<void*const*>(base+imports[i]);MEMORY_BASIC_INFORMATION page{};VirtualQuery(base+imports[i],&page,sizeof(page));protections[i]=page.Protect;}
        const auto* section=IMAGE_FIRST_SECTION(nt);unsigned bins=0,patches=0;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
            if(std::memcmp(section[i].Name,".data",5))continue;
            const size_t begin=section[i].VirtualAddress,end=begin+section[i].Misc.VirtualSize;
            for(size_t p=begin;p+16<end;++p)if(get<uint32_t>(base,p)==0xBA55ED50){
                const auto bytes=get<uint64_t>(base,p+8)+16;if(bytes>end-p)continue;
                std::vector<std::byte> snapshot(base+p,base+p+size_t(bytes));
                const bool okay=retargetVfgFatbin(snapshot,out,stats,why);
                check(okay&&stats.containers==1&&stats.elfFlags==1,"real kernel metadata copy validates");
                check(std::memcmp(base+p,snapshot.data(),snapshot.size())==0,"real mapped kernel original immutable");
                patches+=stats.containers;++bins;p+=size_t(bytes)-1;
            }
        }
        check(bins==62&&patches==62,"all62 existing VFG fatbins retarget in private copies");
        auto driver=GetModuleHandleW(L"nvcuda.dll");check(driver!=nullptr,"SDK imports system CUDA driver");
        for(unsigned round=0;round<3;++round){
            VfgAmpereCompat first,second,other;
            check(first.install(module,driver,0,why),"install exact module import lease (no SDK inference)");
            check(second.install(module,driver,0,why),"same device shares lease");
            check(!other.install(module,driver,1,why),"other device cannot replace active scope");
            check(*reinterpret_cast<void*const*>(base+imports[0])!=saved[0],"real IAT changed");
            check(first.reset(),"first lease releases without detaching shared hooks");
            check(*reinterpret_cast<void*const*>(base+imports[0])!=saved[0],"second lease retains hook");
            check(second.reset(),"final lease restores imports and protection");
            for(size_t i=0;i<2;++i){check(*reinterpret_cast<void*const*>(base+imports[i])==saved[i],"real import exactly restored");
                MEMORY_BASIC_INFORMATION page{};check(VirtualQuery(base+imports[i],&page,sizeof(page))&&page.Protect==protections[i],"real import page protection exactly restored");}
        }
        check(!VfgAmpereCompat::restoreUncertain(),"no uncertain restoration");FreeLibrary(module);
        std::printf("Real module metadata/IAT only, not Ampere GPU execution. fatbins=%u\n",bins);
    }
    std::printf("VFG Ampere: %u checks %u failures\n",checks,failed);return failed?1:0;
}
