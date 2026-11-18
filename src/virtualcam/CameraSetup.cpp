// Standalone, static-CRT setup for Veyra's own DirectShow servers. Never loads
// a payload DLL into this elevated process or changes any other camera/driver.
#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <dbt.h>
#include <dshow.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <stdexcept>
#include <vector>
#include "CameraRegistration.h"
#include "virtualcam-guid.h"
#include "CameraPayloadHashes.h"
namespace fs=std::filesystem;
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Local{PSECURITY_DESCRIPTOR value=nullptr;~Local(){if(value)LocalFree(value);}};
static void require(bool ok,DWORD code=ERROR_INVALID_DATA){if(!ok)throw code;}
static void win(bool ok){if(!ok)throw GetLastError();}
static bool elevated(){Handle token;win(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value));TOKEN_ELEVATION e{};DWORD n=0;win(GetTokenInformation(token.value,TokenElevation,&e,sizeof(e),&n));return e.TokenIsElevated!=0;}
static fs::path root(){PWSTR p=nullptr;const auto hr=SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&p);require(SUCCEEDED(hr),DWORD(hr));fs::path target=fs::path(p)/L"VeyraVirtualCamera";CoTaskMemFree(p);return target;}
static fs::path beside(){wchar_t p[32768]{};const auto n=GetModuleFileNameW(nullptr,p,32768);require(n&&n<32768);return fs::path(p).parent_path();}
static std::wstring hash(HANDLE f){
    LARGE_INTEGER zero{};win(SetFilePointerEx(f,zero,nullptr,FILE_BEGIN));BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE h=nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    DWORD bytes=0,size=0;BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&bytes,0);std::vector<BYTE> object(size);std::array<BYTE,32> output{};
    const auto created=BCryptCreateHash(algorithm,&h,object.data(),size,nullptr,0,0);if(created<0){BCryptCloseAlgorithmProvider(algorithm,0);throw DWORD(created);}
    std::array<BYTE,65536> block{};DWORD got=0,error=0;for(;;){if(!ReadFile(f,block.data(),DWORD(block.size()),&got,nullptr)){error=GetLastError();break;}if(!got)break;if(BCryptHashData(h,block.data(),got,0)<0){error=ERROR_INVALID_DATA;break;}}
    if(!error&&BCryptFinishHash(h,output.data(),DWORD(output.size()),0)<0)error=ERROR_INVALID_DATA;BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(algorithm,0);if(error)throw error;
    std::wstring result;for(auto b:output){result+=L"0123456789abcdef"[b>>4];result+=L"0123456789abcdef"[b&15];}return result;
}
static std::wstring hash(const fs::path& p){Handle h;h.value=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);win(h.value!=INVALID_HANDLE_VALUE);return hash(h.value);}
static void ordinaryPath(const fs::path& p){const auto attr=GetFileAttributesW(p.c_str());win(attr!=INVALID_FILE_ATTRIBUTES);require(!(attr&FILE_ATTRIBUTE_REPARSE_POINT),ERROR_REPARSE_TAG_INVALID);}
static void secure(const fs::path& p){
    ordinaryPath(p);PSID owner=nullptr;PACL acl=nullptr;PSECURITY_DESCRIPTOR sd=nullptr;
    const DWORD result=GetNamedSecurityInfoW(p.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&sd);require(result==ERROR_SUCCESS,result);Local memory;memory.value=sd;
    BYTE admin[SECURITY_MAX_SID_SIZE],system[SECURITY_MAX_SID_SIZE],users[SECURITY_MAX_SID_SIZE];DWORD a=sizeof(admin),s=sizeof(system),u=sizeof(users);
    win(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admin,&a));win(CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&s));win(CreateWellKnownSid(WinBuiltinUsersSid,nullptr,users,&u));
    require(owner&&(EqualSid(owner,admin)||EqualSid(owner,system))&&acl,ERROR_ACCESS_DENIED);
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;win(GetSecurityDescriptorControl(sd,&control,&revision));require((control&SE_DACL_PROTECTED)!=0,ERROR_ACCESS_DENIED);
    for(DWORD i=0;i<acl->AceCount;++i){void* data=nullptr;win(GetAce(acl,i,&data));auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(data);require(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE,ERROR_ACCESS_DENIED);auto* sid=&ace->SidStart;
        if(EqualSid(sid,admin)||EqualSid(sid,system))continue;
        require(EqualSid(sid,users),ERROR_ACCESS_DENIED);
        constexpr DWORD write=GENERIC_WRITE|GENERIC_ALL|DELETE|WRITE_DAC|WRITE_OWNER|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD;
        require(!(ace->Mask&write),ERROR_ACCESS_DENIED);
    }
}
static SECURITY_ATTRIBUTES attributes(Local& sd,bool directory){
    const auto text=directory?L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)":L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)";
    win(ConvertStringSecurityDescriptorToSecurityDescriptorW(text,SDDL_REVISION_1,&sd.value,nullptr));return {sizeof(SECURITY_ATTRIBUTES),sd.value,FALSE};
}
static void directory(const fs::path& p){Local sd;auto sa=attributes(sd,true);if(!CreateDirectoryW(p.c_str(),&sa)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw GetLastError();secure(p);}
static void copySecure(const fs::path& source,const fs::path& dest,std::wstring_view expected={}){
    ordinaryPath(source);Handle input;input.value=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);win(input.value!=INVALID_HANDLE_VALUE);
    if(!expected.empty())require(hash(input.value)==expected,ERROR_CRC);
    if(fs::exists(dest)){secure(dest);if(hash(dest)==hash(input.value))return;}
    LARGE_INTEGER zero{};win(SetFilePointerEx(input.value,zero,nullptr,FILE_BEGIN));const auto temp=dest.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L".new";
    Local sd;auto sa=attributes(sd,false);Handle output;output.value=CreateFileW(temp.c_str(),GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);win(output.value!=INVALID_HANDLE_VALUE);
    try{std::array<BYTE,65536> buffer{};DWORD got=0,written=0;for(;;){win(ReadFile(input.value,buffer.data(),DWORD(buffer.size()),&got,nullptr));if(!got)break;win(WriteFile(output.value,buffer.data(),got,&written,nullptr));require(written==got,ERROR_WRITE_FAULT);}win(FlushFileBuffers(output.value));CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;
        secure(temp);win(MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));secure(dest);
    }catch(...){if(output.value!=INVALID_HANDLE_VALUE){CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;}DeleteFileW(temp.c_str());throw;}
}
static HRESULT verifyDevice(){
    using Microsoft::WRL::ComPtr;
    ComPtr<ICreateDevEnum> devices;HRESULT hr=CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&devices));if(FAILED(hr))return hr;
    ComPtr<IEnumMoniker> entries;hr=devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&entries,0);if(hr!=S_OK)return FAILED(hr)?hr:HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for(;;){ComPtr<IMoniker> entry;if(entries->Next(1,&entry,nullptr)!=S_OK)break;ComPtr<IPropertyBag> bag;if(FAILED(entry->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&bag))))continue;
        VARIANT value;VariantInit(&value);const bool own=SUCCEEDED(bag->Read(L"CLSID",&value,nullptr))&&value.vt==VT_BSTR&&CompareStringOrdinal(value.bstrVal,-1,VeyraCameraClsid,-1,TRUE)==CSTR_EQUAL;VariantClear(&value);
        if(own){ComPtr<IBaseFilter> filter;return entry->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter));}
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
static void resultFile(const fs::path& folder){
    const auto path=folder/L"install-result.json";if(fs::exists(path))secure(path);Local sd;auto sa=attributes(sd,false);Handle file;
    file.value=CreateFileW(path.c_str(),GENERIC_WRITE,0,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);win(file.value!=INVALID_HANDLE_VALUE);
    constexpr char text[]="{\"elevated\":true,\"deviceEnumerated\":true,\"classBound\":true,\"architecture\":64}\n";DWORD written=0;win(WriteFile(file.value,text,DWORD(sizeof(text)-1),&written,nullptr));require(written==sizeof(text)-1,ERROR_WRITE_FAULT);win(FlushFileBuffers(file.value));
}
static void install(){
    const auto source=beside();require(hash(source/L"veyra-virtualcam64.dll")==CameraHash64&&hash(source/L"veyra-virtualcam32.dll")==CameraHash32,ERROR_CRC);
    const auto base=root();ordinaryPath(base.parent_path());directory(base);
    const auto version=base/(std::wstring(L"v1-")+std::wstring(CameraHash64,12)+std::wstring(CameraHash32,12));directory(version);
    copySecure(source/L"veyra-virtualcam64.dll",version/L"veyra-virtualcam64.dll",CameraHash64);copySecure(source/L"veyra-virtualcam32.dll",version/L"veyra-virtualcam32.dll",CameraHash32);
    for(const auto* name:{L"COPYING.GPL-2.0.txt",L"COPYING.LGPL-2.1.txt",L"ISC-Lain-Bailey.txt"})copySecure(source.parent_path()/L"licenses"/L"virtual-camera"/name,version/name);
    const auto old64=veyra::virtualcam::registeredModule(false,true),old32=veyra::virtualcam::registeredModule(true,true);
    const auto oldUser64=veyra::virtualcam::registeredModule(false),oldUser32=veyra::virtualcam::registeredModule(true);
    const auto a=veyra::virtualcam::registerModule((version/L"veyra-virtualcam64.dll").c_str(),false,true);
    auto b=SUCCEEDED(a)?veyra::virtualcam::registerModule((version/L"veyra-virtualcam32.dll").c_str(),true,true):a;
    if(SUCCEEDED(b)){
        veyra::virtualcam::unregisterModule(false);veyra::virtualcam::unregisterModule(true);
        b=verifyDevice();
    }
    if(FAILED(b)){
        if(old64.empty())veyra::virtualcam::unregisterModule(false,true);else veyra::virtualcam::registerModule(old64.c_str(),false,true);
        if(old32.empty())veyra::virtualcam::unregisterModule(true,true);else veyra::virtualcam::registerModule(old32.c_str(),true,true);
        if(!oldUser64.empty())veyra::virtualcam::registerModule(oldUser64.c_str(),false);
        if(!oldUser32.empty())veyra::virtualcam::registerModule(oldUser32.c_str(),true);
        throw DWORD(b);
    }
    // The normal parent also clears its own HKCU overrides after success (the
    // consent prompt may have used a different administrator account).
    veyra::virtualcam::unregisterModule(false);veyra::virtualcam::unregisterModule(true);
    resultFile(version);
}
static bool versionName(const std::wstring& name){return name.size()==27&&name.starts_with(L"v1-")&&std::all_of(name.begin()+3,name.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});}
static void uninstall(){
    const auto a=veyra::virtualcam::unregisterModule(false,true),b=veyra::virtualcam::unregisterModule(true,true);require(SUCCEEDED(a)&&SUCCEEDED(b),DWORD(FAILED(a)?a:b));
    veyra::virtualcam::unregisterModule(false);veyra::virtualcam::unregisterModule(true);
    const auto base=root();if(!fs::exists(base))return;secure(base);
    // Only our fixed filenames in our hash-named, non-reparse directories.
    // A DLL retained by a consumer is left in place until a later cleanup.
    for(const auto& entry:fs::directory_iterator(base)){
        if(!versionName(entry.path().filename().wstring())||!entry.is_directory())continue;secure(entry.path());
        for(const auto* name:{L"veyra-virtualcam64.dll",L"veyra-virtualcam32.dll",L"COPYING.GPL-2.0.txt",L"COPYING.LGPL-2.1.txt",L"ISC-Lain-Bailey.txt",L"install-result.json"}){
            const auto file=entry.path()/name;if(!fs::exists(file))continue;secure(file);if(!DeleteFileW(file.c_str()))std::fwprintf(stderr,L"retained locked component: %ls (%lu)\n",file.c_str(),GetLastError());
        }
        RemoveDirectoryW(entry.path().c_str());
    }
    RemoveDirectoryW(base.c_str());
}
int wmain(int argc,wchar_t** argv){
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try{
        require(argc==2,ERROR_INVALID_PARAMETER);const std::wstring mode=argv[1];
        if(mode==L"--verify"){
            require(hash(beside()/L"veyra-virtualcam64.dll")==CameraHash64&&hash(beside()/L"veyra-virtualcam32.dll")==CameraHash32,ERROR_CRC);
            std::wprintf(L"Verified Veyra camera payload. Target: %ls\n",root().c_str());return 0;
        }
        require(mode==L"--install"||mode==L"--uninstall",ERROR_INVALID_PARAMETER);require(elevated(),ERROR_ELEVATION_REQUIRED);
        if(mode==L"--install")install();else uninstall();
        SendNotifyMessageW(HWND_BROADCAST,WM_DEVICECHANGE,DBT_DEVNODES_CHANGED,0);return 0;
    }catch(DWORD error){std::fprintf(stderr,"Veyra camera setup failed: 0x%08lX\n",error);return int(error?error:ERROR_GEN_FAILURE);}
    catch(const std::exception& e){std::fprintf(stderr,"Veyra camera setup failed: %s\n",e.what());return ERROR_GEN_FAILURE;}
}
