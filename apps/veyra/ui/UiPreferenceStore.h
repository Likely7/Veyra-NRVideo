#pragma once
#include "UiSessionState.h"
#include "veyra/engine/PresetStore.h"
#include "veyra/engine/PresentationSettings.h"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace veyra::ui {
struct UiPreferences {
    engine::PresentationSettings presentation;
    float volume=1;bool muted=false,subtitles=true;
    // The colour page is a professional panel now (colour wheels, tone curve,
    // mixer strip): start wider so those controls are usable, still draggable
    // between 296 and 420.
    int inspectorWidth=392;
    int width=1280,height=800,x=0,y=0,inspector=0,subtitleSize=22;bool positioned=false;
    // v3: subtitle look (outline/background bar/font index/extra bottom margin)
    // and the dual-language switch.
    bool subtitleOutline=true,subtitleBackground=false,subtitleSecondLanguage=false;
    int subtitleMargin=0,subtitleFont=0;
    int subtitleLines=2;
    bool subtitleFitToLines=false;
    // v4: colour-page accordion fold mask (one bit per section).
    uint32_t colourFoldMask=0;
    // -1 migrates v1-v4 by deriving the master switch from the saved effects.
    int enhancementEnabled=-1;
};
class UiPreferenceStore {
    std::filesystem::path folder_;
    bool corrupt_=false;
public:
    explicit UiPreferenceStore(std::filesystem::path folder):folder_(std::move(folder)){}
    UiPreferences load(){UiPreferences result;auto path=folder_/"ui-preferences.v1";if(!std::filesystem::exists(path))return result;
        if(std::filesystem::file_size(path)>2048){corrupt_=true;return result;}std::ifstream file(path);std::string magic;int version=0,mute,sub,pos;UiPreferences read;
        // inspector index 4 is the audio page (selectInspector(4)), so the valid
        // range is 0..4. A narrower bound here silently discarded every saved
        // window size, volume and subtitle style for users who last sat on it.
        if(!(file>>magic>>version>>read.volume>>mute>>sub>>read.width>>read.height>>read.x>>read.y>>pos>>read.inspector>>read.subtitleSize)||magic!="VEYRA_UI"||(version<1||version>10)||!std::isfinite(read.volume)||read.volume<0||read.volume>1||mute<0||mute>1||sub<0||sub>1||pos<0||pos>1||read.width<720||read.width>10000||read.height<540||read.height>10000||abs(int64_t(read.x))>100000||abs(int64_t(read.y))>100000||read.inspector<0||read.inspector>4||read.subtitleSize<16||read.subtitleSize>56){corrupt_=true;return result;}
        if(version==2&&(!(file>>read.inspectorWidth)||read.inspectorWidth<296||read.inspectorWidth>480)){corrupt_=true;return result;}
        if(version>=3){
            int outline=0,background=0,second=0;
            // Field order must match save(): inspectorWidth precedes the three
            // switches. It used to be read only for version 2, so every version
            // 3 file was rejected as corrupt and all UI preferences were lost.
            if(!(file>>read.inspectorWidth>>outline>>background>>second>>read.subtitleMargin>>read.subtitleFont)||read.inspectorWidth<296||read.inspectorWidth>480||outline<0||outline>1||background<0||background>1||second<0||second>1||read.subtitleMargin<0||read.subtitleMargin>240||read.subtitleFont<0||read.subtitleFont>5){corrupt_=true;return result;}
            read.subtitleOutline=outline!=0;read.subtitleBackground=background!=0;read.subtitleSecondLanguage=second!=0;
        }
        // v4 appends the colour-page fold mask *after* the v3 subtitle block, so
        // it must be read here - reading it earlier shifted every following field.
        if(version>=4&&!(file>>read.colourFoldMask)){corrupt_=true;return result;}
        if(version>=5&&(!(file>>read.enhancementEnabled)||read.enhancementEnabled< -1||read.enhancementEnabled>1)){corrupt_=true;return result;}
        if(version>=6){int enabled=0,mode=0,display=0;if(!(file>>enabled>>mode>>display)||enabled<0||enabled>1||mode<0||mode>2||display<0||display>2){corrupt_=true;return result;}read.presentation={enabled!=0,engine::PacingMode(mode),engine::DisplaySync(display)};read.presentation.outputRate=engine::OutputRateMode::Off;}
        if(version>=7&&(!(file>>read.subtitleLines)||read.subtitleLines<0||read.subtitleLines>8)){corrupt_=true;return result;}
        if(version>=8){int fit=0;if(!(file>>fit)||fit<0||fit>1){corrupt_=true;return result;}read.subtitleFitToLines=fit!=0;} if(version>=9){int rate=0;if(!(file>>rate>>read.presentation.customFps)||rate<0||rate>2||!std::isfinite(read.presentation.customFps)){corrupt_=true;return result;}read.presentation.outputRate=static_cast<engine::OutputRateMode>(rate);if(version==9&&read.presentation.outputRate==engine::OutputRateMode::FollowDisplay)read.presentation.outputRate=engine::OutputRateMode::Off;}
        file>>std::ws;if(!file.eof()){corrupt_=true;return result;}read.muted=mute;read.subtitles=sub;read.positioned=pos;return read;
    }
    engine::EnhancementSettings startup(engine::EnhancementSettings fallback){engine::PresetStore last(folder_/"last-applied.v1");if(!last.load()||last.entries().empty())return fallback;return last.entries().front().settings;}
    bool save(const UiPreferences& p,const engine::EnhancementSettings* applied){
        bool ok=true;std::filesystem::create_directories(folder_);if(applied){engine::PresetStore last(folder_/"last-applied.v1");ok=last.load()&&last.put(L"Last applied",*applied,true);}
        if(corrupt_)return false;std::ostringstream out;out<<"VEYRA_UI 10\n"<<p.volume<<' '<<p.muted<<' '<<p.subtitles<<' '<<p.width<<' '<<p.height<<' '<<p.x<<' '<<p.y<<' '<<p.positioned<<' '<<p.inspector<<' '<<p.subtitleSize<<' '<<p.inspectorWidth<<' '<<p.subtitleOutline<<' '<<p.subtitleBackground<<' '<<p.subtitleSecondLanguage<<' '<<p.subtitleMargin<<' '<<p.subtitleFont<<' '<<p.colourFoldMask<<' '<<p.enhancementEnabled<<' '<<p.presentation.enabled<<' '<<unsigned(p.presentation.mode)<<' '<<unsigned(p.presentation.display)<<' '<<p.subtitleLines<<' '<<p.subtitleFitToLines<<' '<<unsigned(p.presentation.outputRate)<<' '<<p.presentation.customFps<<'\n';
        auto target=folder_/"ui-preferences.v1",tmp=folder_/(L"ui-preferences.tmp-"+std::to_wstring(GetCurrentProcessId()));auto data=out.str();HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;bool saved=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);CloseHandle(file);if(saved)saved=MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;return ok&&saved;
    }
};
}
