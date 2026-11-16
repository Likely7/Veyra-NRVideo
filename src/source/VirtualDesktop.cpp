#include "veyra/source/VirtualDesktop.h"
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>

namespace veyra::source::vdesk {
using Microsoft::WRL::ComPtr;
namespace {
// Explorer keeps the desktop order here (Windows 10 and 11): VirtualDesktopIDs is the
// GUIDs of all desktops back to back, and Desktops\{GUID}\Name a user-given name.
constexpr wchar_t registryRoot[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
// CLSID_VirtualDesktopManager (shobjidl_core.h), spelled out so no import library
// has to define it.
constexpr CLSID managerClass{0xaa509086,0x5ca9,0x4c25,{0x8f,0x95,0x58,0x9d,0x3c,0x07,0xb4,0x8a}};

std::vector<uint8_t> binaryValue(const wchar_t* key,const wchar_t* value){
    DWORD size=0;
    if(RegGetValueW(HKEY_CURRENT_USER,key,value,RRF_RT_REG_BINARY,nullptr,nullptr,&size)!=ERROR_SUCCESS||!size)return {};
    std::vector<uint8_t> data(size);
    // A desktop created between the two calls makes the second fail: report none.
    if(RegGetValueW(HKEY_CURRENT_USER,key,value,RRF_RT_REG_BINARY,nullptr,data.data(),&size)!=ERROR_SUCCESS)return {};
    data.resize(size);return data;
}
std::wstring textValue(const std::wstring& key,const wchar_t* value){
    DWORD size=0;
    if(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),value,RRF_RT_REG_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS||size<sizeof(wchar_t))return {};
    std::wstring text(size/sizeof(wchar_t),L'\0');
    if(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),value,RRF_RT_REG_SZ,nullptr,text.data(),&size)!=ERROR_SUCCESS)return {};
    text.resize(wcsnlen(text.c_str(),text.size()));return text;
}
// COM for the lifetime of a Lookup. A thread that already runs COM gets S_FALSE (and
// is balanced) or RPC_E_CHANGED_MODE when it uses the other apartment model; both keep
// working, and only a successful call is undone.
struct ComScope {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}
};
}

std::vector<Desktop> desktops(){
    std::vector<Desktop> list;
    const auto ids=binaryValue(registryRoot,L"VirtualDesktopIDs");
    for(size_t at=0;at+sizeof(GUID)<=ids.size();at+=sizeof(GUID)){
        Desktop desktop;std::memcpy(&desktop.id,ids.data()+at,sizeof(GUID));
        wchar_t guid[40]{};
        if(StringFromGUID2(desktop.id,guid,40)>0)desktop.name=textValue(std::wstring(registryRoot)+L"\\Desktops\\"+guid,L"Name");
        list.push_back(std::move(desktop));
    }
    return list;
}

bool shellCloaked(void* window){
    DWORD cloaked=0;
    return window&&SUCCEEDED(DwmGetWindowAttribute(static_cast<HWND>(window),DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked==DWM_CLOAKED_SHELL;
}

// Member order matters: the manager is released before COM is uninitialized.
struct Lookup::Impl {
    ComScope com;
    ComPtr<IVirtualDesktopManager> manager;
    std::vector<Desktop> list;
};
Lookup::Lookup():p_(std::make_unique<Impl>()){
    if(FAILED(CoCreateInstance(managerClass,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&p_->manager))))p_->manager.Reset();
    else p_->list=desktops();
}
Lookup::~Lookup()=default;
bool Lookup::available()const{return p_->manager!=nullptr;}
bool Lookup::otherDesktop(void* handle,Placement& out)const{
    out={};
    const auto window=static_cast<HWND>(handle);
    if(!p_->manager||!shellCloaked(window))return false;
    BOOL current=TRUE;GUID id{};
    if(FAILED(p_->manager->IsWindowOnCurrentVirtualDesktop(window,&current))||current)return false;
    if(FAILED(p_->manager->GetWindowDesktopId(window,&id))||id==GUID{})return false;
    for(size_t i=0;i<p_->list.size();++i)
        if(p_->list[i].id==id){out.number=unsigned(i+1);out.name=p_->list[i].name;break;}
    // With the desktop list known, a GUID outside it is not a Task View desktop.
    // Without one the window still counts; its number just stays unknown.
    if(!out.number&&!p_->list.empty())return false;
    out.other=true;return true;
}
}
