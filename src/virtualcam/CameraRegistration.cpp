// Veyra-owned current-user DirectShow registration. No driver or OBS keys are touched.
#include "CameraRegistration.h"
#include "virtualcam-guid.h"
#include <dshow.h>
#include <wrl/client.h>
#include <string>

namespace veyra::virtualcam {
namespace {
constexpr wchar_t classes[]=L"Software\\Classes\\CLSID\\";
constexpr wchar_t category[]=L"{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\Instance\\";
struct Key { HKEY value=nullptr; ~Key(){if(value)RegCloseKey(value);} };
LSTATUS put(HKEY parent,const wchar_t* name,const wchar_t* text){
    return RegSetValueExW(parent,name,0,REG_SZ,reinterpret_cast<const BYTE*>(text),DWORD((wcslen(text)+1)*sizeof(wchar_t)));
}
HRESULT remove(bool x86,bool machine){
    const REGSAM view=x86?KEY_WOW64_32KEY:KEY_WOW64_64KEY;
    Key root; const auto opened=RegOpenKeyExW(machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,L"Software\\Classes\\CLSID",0,KEY_ALL_ACCESS|view,&root.value);
    if(opened==ERROR_FILE_NOT_FOUND)return S_OK;
    if(opened!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(opened);
    const auto instance=std::wstring(category)+VeyraCameraClsid;
    const auto a=RegDeleteTreeW(root.value,instance.c_str());
    const auto b=RegDeleteTreeW(root.value,VeyraCameraClsid);
    if(a!=ERROR_SUCCESS&&a!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(a);
    if(b!=ERROR_SUCCESS&&b!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(b);
    return S_OK;
}
}
HRESULT registerModule(const wchar_t* dll,bool x86,bool machine){
    if(!dll||wcslen(dll)<4||dll[1]!=L':'||GetFileAttributesW(dll)==INVALID_FILE_ATTRIBUTES)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    // Explicit device selection uses the System Device Enumerator. Do not register
    // this source with Intelligent Connect or write through merged HKCR/HKLM.
    const REGSAM view=x86?KEY_WOW64_32KEY:KEY_WOW64_64KEY;
    const HKEY root=machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER;
    Key cls,inproc,instance;
    LSTATUS status=RegCreateKeyExW(root,(std::wstring(classes)+VeyraCameraClsid).c_str(),0,nullptr,0,KEY_WRITE|view,nullptr,&cls.value,nullptr);
    if(status==ERROR_SUCCESS)status=put(cls.value,nullptr,VeyraCameraName);
    if(status==ERROR_SUCCESS)status=RegCreateKeyExW(cls.value,L"InprocServer32",0,nullptr,0,KEY_WRITE|view,nullptr,&inproc.value,nullptr);
    if(status==ERROR_SUCCESS)status=put(inproc.value,nullptr,dll);
    if(status==ERROR_SUCCESS)status=put(inproc.value,L"ThreadingModel",L"Both");
    if(status==ERROR_SUCCESS)status=RegCreateKeyExW(root,(std::wstring(classes)+category+VeyraCameraClsid).c_str(),0,nullptr,0,KEY_WRITE|view,nullptr,&instance.value,nullptr);
    if(status==ERROR_SUCCESS)status=put(instance.value,L"CLSID",VeyraCameraClsid);
    if(status==ERROR_SUCCESS)status=put(instance.value,L"FriendlyName",VeyraCameraName);
    return status==ERROR_SUCCESS?S_OK:HRESULT_FROM_WIN32(status);
}
HRESULT unregisterModule(bool x86,bool machine){return remove(x86,machine);}
std::wstring registeredModule(bool x86,bool machine){
    Key key;const auto path=std::wstring(classes)+VeyraCameraClsid+L"\\InprocServer32";
    if(RegOpenKeyExW(machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,path.c_str(),0,KEY_READ|(x86?KEY_WOW64_32KEY:KEY_WOW64_64KEY),&key.value)!=ERROR_SUCCESS)return {};
    wchar_t value[32768]{};DWORD bytes=sizeof(value),type=0;
    if(RegQueryValueExW(key.value,nullptr,nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes)!=ERROR_SUCCESS||type!=REG_SZ||bytes<sizeof(wchar_t)||bytes>sizeof(value))return {};
    value[std::size(value)-1]=0;return value;
}
}
