#pragma once
#include <cstdint>
#include <windows.h>
#include <dshow.h>
#include <string>
#include <format>

namespace veyra::source {
enum class CapturePacking { Unknown, Yuy2, Uyvy, Yvyu, Nv12, Nv21, I420, Yv12, P010, P016, Bgr32, Bgra32, Bgr24, Rgb555, Rgb565 };
constexpr DWORD captureFourcc(char a,char b,char c,char d){return DWORD(uint8_t(a))|(DWORD(uint8_t(b))<<8)|(DWORD(uint8_t(c))<<16)|(DWORD(uint8_t(d))<<24);}
inline bool captureIsFourcc(const GUID& id,DWORD code){
    const GUID expected{code,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};return id==expected;
}
inline CapturePacking capturePacking(const GUID& id){
    if(id==MEDIASUBTYPE_RGB32)return CapturePacking::Bgr32;
    if(id==MEDIASUBTYPE_ARGB32)return CapturePacking::Bgra32;
    if(id==MEDIASUBTYPE_RGB24)return CapturePacking::Bgr24;
    if(id==MEDIASUBTYPE_RGB555)return CapturePacking::Rgb555;
    if(id==MEDIASUBTYPE_RGB565)return CapturePacking::Rgb565;
    struct Entry{DWORD code;CapturePacking packing;};
    static constexpr Entry entries[]={
        {captureFourcc('Y','U','Y','2'),CapturePacking::Yuy2},{captureFourcc('U','Y','V','Y'),CapturePacking::Uyvy},
        {captureFourcc('H','D','Y','C'),CapturePacking::Uyvy},
        {captureFourcc('Y','V','Y','U'),CapturePacking::Yvyu},{captureFourcc('N','V','1','2'),CapturePacking::Nv12},
        {captureFourcc('N','V','2','1'),CapturePacking::Nv21},{captureFourcc('I','4','2','0'),CapturePacking::I420},
        {captureFourcc('I','Y','U','V'),CapturePacking::I420},{captureFourcc('Y','V','1','2'),CapturePacking::Yv12},
        {captureFourcc('P','0','1','0'),CapturePacking::P010},{captureFourcc('P','0','1','6'),CapturePacking::P016}};
    for(const auto& e:entries)if(captureIsFourcc(id,e.code))return e.packing;
    return CapturePacking::Unknown;
}
inline std::wstring capturePixelName(const GUID& id){
    if(id==MEDIASUBTYPE_RGB32)return L"RGB32";
    if(id==MEDIASUBTYPE_ARGB32)return L"ARGB32";
    if(id==MEDIASUBTYPE_RGB24)return L"RGB24";
    if(id==MEDIASUBTYPE_RGB555)return L"RGB555";
    if(id==MEDIASUBTYPE_RGB565)return L"RGB565";
    if(id==MEDIASUBTYPE_MJPG)return L"MJPEG";
    if(captureIsFourcc(id,id.Data1)){
        std::wstring name;for(unsigned i=0;i<4;++i){const unsigned ch=(id.Data1>>(8*i))&255;if(ch<32||ch>126){name.clear();break;}name+=wchar_t(ch);}if(!name.empty())return name;
    }
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);return std::format(L"未知格式 {}",guid);
}
}
