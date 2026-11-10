#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <map>
#include "veyra/source/CaptureFrameRate.h"
#include "veyra/source/CaptureColorOverride.h"
#include "veyra/source/CaptureCardSource.h"

namespace veyra::ui {
struct CapturePreferences {
    std::wstring videoPath,formatKey,audioPath;
    int audioMode=-1;
    unsigned colorOverride=0;
    // N3: the one-time "this format is high cost" hint was already shown.
    bool formatHintDismissed=false;
    double requestedFps=0;
    std::map<std::wstring,unsigned> deviceColors;
    unsigned colorForDevice(const std::wstring& device)const {
        const auto found=deviceColors.find(device);
        return found!=deviceColors.end()?found->second:device==videoPath?colorOverride:0;
    }
};
class CapturePreferenceStore {
    std::filesystem::path path_;
public:
    explicit CapturePreferenceStore(std::filesystem::path folder):path_(std::move(folder)/"capture-preferences.v1"){}
    CapturePreferences load()const {
        std::ifstream file(path_,std::ios::binary|std::ios::ate);
        if(!file)return {};
        const std::streamoff size=file.tellg();
        if(size<=0||size>65536||size%sizeof(wchar_t))return {};
        std::wstring data(size_t(size)/sizeof(wchar_t),0);file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(data.data()),size))return {};
        std::wistringstream in(data);std::wstring magic;int version=0;CapturePreferences result;
        if(!(in>>magic>>version>>std::quoted(result.videoPath)>>std::quoted(result.formatKey)>>result.audioMode>>std::quoted(result.audioPath)>>result.colorOverride)||
           magic!=L"VEYRA_CAPTURE"||(version<1||version>4)||(!source::captureAudioIsWasapi(result.audioMode)&&(result.audioMode< -2||result.audioMode>0))||!source::validCaptureColorOverride(result.colorOverride))return {};
        if(version>=2){int dismissed=0;if(!(in>>dismissed)||(dismissed!=0&&dismissed!=1))return {};result.formatHintDismissed=dismissed!=0;}
        if(version>=3&&(!(in>>result.requestedFps)||!source::validCaptureFrameRate(result.requestedFps)))return {};
        if(version>=4){unsigned count=0;if(!(in>>count)||count>128)return {};for(unsigned i=0;i<count;++i){std::wstring device;unsigned color=0;if(!(in>>std::quoted(device)>>color)||device.empty()||!source::validCaptureColorOverride(color)||!result.deviceColors.emplace(device,color).second)return {};}}
        in>>std::ws;return in.eof()?result:CapturePreferences{};
    }
    bool save(const CapturePreferences& p)const {
        if((!source::captureAudioIsWasapi(p.audioMode)&&(p.audioMode< -2||p.audioMode>0))||!source::validCaptureColorOverride(p.colorOverride)||!source::validCaptureFrameRate(p.requestedFps)||p.deviceColors.size()>128)return false;
        std::error_code ec;std::filesystem::create_directories(path_.parent_path(),ec);if(ec)return false;
        std::wostringstream out;out<<L"VEYRA_CAPTURE 4\n"<<std::quoted(p.videoPath)<<L' '<<std::quoted(p.formatKey)<<L' '<<p.audioMode<<L' '<<std::quoted(p.audioPath)<<L' '<<p.colorOverride<<L' '<<(p.formatHintDismissed?1:0)<<L' '<<std::setprecision(12)<<p.requestedFps<<L'\n';
        out<<p.deviceColors.size()<<L'\n';
        for(const auto& [device,color]:p.deviceColors){if(device.empty()||!source::validCaptureColorOverride(color))return false;out<<std::quoted(device)<<L' '<<color<<L'\n';}
        const auto data=out.str();if(data.size()>65536/sizeof(wchar_t))return false;const DWORD bytes=DWORD(data.size()*sizeof(wchar_t));
        auto tmp=path_;tmp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
        HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
        DWORD written=0;bool ok=WriteFile(file,data.data(),bytes,&written,nullptr)&&written==bytes&&FlushFileBuffers(file);CloseHandle(file);
        ok=ok&&MoveFileExW(tmp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok)DeleteFileW(tmp.c_str());
        return ok;
    }
};
}
