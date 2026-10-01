#include "veyra/engine/PresetStore.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>
namespace veyra::engine {
namespace {
std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);if(n)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);return r;}
bool nameOk(const std::wstring& s){return !s.empty()&&s.size()<=48&&s.front()!=L' '&&s.back()!=L' '&&std::none_of(s.begin(),s.end(),[](wchar_t c){return c<32;});}
std::wstring trim(std::wstring s){auto a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return {};auto b=s.find_last_not_of(L" \t\r\n");return s.substr(a,b-a+1);}
}
std::string PresetStore::serialize()const{
    std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(std::numeric_limits<float>::max_digits10)<<"VEYRA_PRESETS 21\n"<<std::quoted(utf8(default_))<<' '<<entries_.size()<<'\n';
    for(auto& p:entries_){const auto& s=p.settings;const auto& m=s.model;const auto& r=s.residual;o<<std::quoted(utf8(p.name))<<' '<<m.intensity<<' '<<m.tone<<' '<<m.structure<<' '<<m.skin<<' '<<m.style<<' '<<m.autoMask<<' '<<m.uiCorrection<<' '<<r.total<<' '<<r.darken<<' '<<r.brighten<<' '<<r.color<<' '<<r.luminance<<' '<<s.nr<<' '<<s.sr<<' '<<s.multiplier<<' '<<int(s.nrPolicy)<<' '<<int(s.flow)<<' '<<int(s.content)<<' '<<s.protection.enabled<<' '<<s.protection.featherPixels;for(auto q:s.protection.regions)o<<' '<<q.left<<' '<<q.top<<' '<<q.right<<' '<<q.bottom;o<<' '<<s.videoSrQuality<<' '<<int(s.frameGenerationBackend)<<' '<<int(s.srTarget)<<' '<<int(s.opticalFlowBackend)<<' '<<s.amdFlowHalfResolution<<' '<<int(s.audioSync)<<' '<<s.audioOffsetMs<<' '<<int(s.nrRuntime)<<' '<<s.captureCompatible<<' '<<s.lowLatency<<' '<<s.forceSdrPreview<<' '<<int(s.captureAudio)<<' '<<s.exportBitrateMbps<<' '<<s.captureFlipVertical<<' '<<int(s.captureBuffer)<<' ';writeColorSettings(o,s.color,utf8(s.color.lutNameString()));o<<' '<<s.videoHdr.enabled<<' '<<s.videoHdr.contrast<<' '<<s.videoHdr.saturation<<' '<<s.videoHdr.middleGray<<' '<<s.videoHdr.peakNits<<' '<<s.nrTemporal<<'\n';}return o.str();
}
bool PresetStore::parse(const std::string& data,std::vector<UserPreset>& out,std::wstring& def){
    if(data.size()>65536)return false;std::istringstream in(data);in.imbue(std::locale::classic());std::string magic,d;int version=0;size_t count=0;
    if(!(in>>magic>>version)||magic!="VEYRA_PRESETS"||(version<1||version>21)||!(in>>std::quoted(d)>>count)||count>64)return false;def=wide(d);if(!d.empty()&&def.empty())return false;
    for(size_t i=0;i<count;++i){UserPreset p;std::string n;int policy,flow,content,nr,sr;auto& s=p.settings;auto& m=s.model;auto& r=s.residual;
        if(!(in>>std::quoted(n)>>m.intensity>>m.tone>>m.structure>>m.skin>>m.style>>m.autoMask>>m.uiCorrection>>r.total>>r.darken>>r.brighten>>r.color>>r.luminance>>nr>>sr>>s.multiplier>>policy>>flow>>content))return false;
        if(version>=2){int enabled;if(!(in>>enabled>>s.protection.featherPixels)||enabled<0||enabled>1)return false;s.protection.enabled=enabled!=0;
            for(auto& q:s.protection.regions)if(!(in>>q.left>>q.top>>q.right>>q.bottom))return false;}
        if(version>=3&&!(in>>s.videoSrQuality))return false;
        // Retired local FSR4 mode: keep the preset, use RTX Video SR high.
        if(version==20&&s.videoSrQuality==6)s.videoSrQuality=3;
        if(version>=4){int backend;if(!(in>>backend)||backend<0||backend>(version>=13?2:(version>=6&&version<=7?2:1)))return false;
            // Before v8, 1 meant removed FRUC and 2 meant XeSS. New writes use v10.
            s.frameGenerationBackend=version<8
                ?(backend==2?FrameGenerationBackend::XeSS:FrameGenerationBackend::Dlss)
                :static_cast<FrameGenerationBackend>(backend);}
        if(version>=5){uint32_t target;if(!(in>>target))return false;s.srTarget=static_cast<pipeline::SrTarget>(target);}
        if(version>=6){int backend,half;if(!(in>>backend>>half)||half<0||half>1)return false;s.opticalFlowBackend=static_cast<OpticalFlowBackend>(backend);s.amdFlowHalfResolution=half!=0;}
        if(version>=7){int sync;if(!(in>>sync>>s.audioOffsetMs))return false;s.audioSync=static_cast<AudioSyncMode>(sync);}
        if(version>=9){int runtime;if(!(in>>runtime))return false;s.nrRuntime=static_cast<NrRuntime>(runtime);}
        if(version>=10){int capture;if(!(in>>capture)||capture<0||capture>1)return false;s.captureCompatible=capture!=0;}
        if(version>=11){int low;if(!(in>>low)||low<0||low>1)return false;s.lowLatency=low!=0;}
        if(version>=12){int sdr;if(!(in>>sdr)||sdr<0||sdr>1)return false;s.forceSdrPreview=sdr!=0;}
        if(version>=14){int ingress;if(!(in>>ingress)||ingress<0||ingress>2)return false;s.captureAudio=static_cast<CaptureAudioIngress>(ingress);}
        // v15 appends the export bitrate to the line, so it must be read last.
        if(version>=15){if(!(in>>s.exportBitrateMbps))return false;}
        // v16 appends the manual capture vertical flip after the bitrate.
        if(version>=16){int flip;if(!(in>>flip)||flip<0||flip>1)return false;s.captureFlipVertical=flip!=0;}
        // v17 appends the capture video-pin buffer policy.
        if(version>=17){int buffer;if(!(in>>buffer)||buffer<0||buffer>2)return false;s.captureBuffer=static_cast<veyra::source::CaptureBufferMode>(buffer);}
        // v18 appends the full colour grade block (see ColorSettings.h).
        if(version>=18){std::string lut;if(!readColorSettings(in,s.color,lut,version))return false;if(!lut.empty()&&!s.color.setLutName(wide(lut)))return false;}
        if(version>=20){int enabled;if(!(in>>enabled>>s.videoHdr.contrast>>s.videoHdr.saturation>>s.videoHdr.middleGray>>s.videoHdr.peakNits)||enabled<0||enabled>1)return false;s.videoHdr.enabled=enabled!=0;} if(version>=21){int temporal=0;if(!(in>>temporal)||temporal<0||temporal>1)return false;s.nrTemporal=temporal!=0;}
        p.name=wide(n);if(!nameOk(p.name)||std::any_of(out.begin(),out.end(),[&](auto& a){return a.name==p.name;})||nr<0||nr>1||sr<0||sr>1)return false;
        s.nr=nr;s.sr=sr;s.nrPolicy=static_cast<pipeline::NrSizePolicy>(policy);s.flow=static_cast<FlowQuality>(flow);s.content=static_cast<ContentRate>(content);
        if(!s.validate().empty())return false;out.push_back(std::move(p));
    }
    in>>std::ws;if(!in.eof())return false;
    return def.empty()||std::any_of(out.begin(),out.end(),[&](auto& a){return a.name==def;});
}
bool PresetStore::load(){
    error_.clear();if(!std::filesystem::exists(path_))return true;if(std::filesystem::file_size(path_)>65536){corrupt_=true;error_=L"Preset file exceeds 64KiB; original file kept";return false;}std::ifstream f(path_,std::ios::binary);std::string data((std::istreambuf_iterator<char>(f)),{});std::vector<UserPreset> loaded;std::wstring def;
    if(!f||!parse(data,loaded,def)){corrupt_=true;error_=L"Preset file is corrupted or its version is unsupported; original file kept, overwriting disallowed. Currently using built-in settings.";return false;}
    entries_=std::move(loaded);default_=std::move(def);corrupt_=false;return true;
}
bool PresetStore::save(){
    if(corrupt_){error_=L"The corrupted original file is protected; no settings were written. Please back it up and move it away first";return false;}
    const auto data=serialize();std::vector<UserPreset> check;std::wstring def;if(!parse(data,check,def)){error_=L"Preset validation failed";return false;}
    std::error_code ec;std::filesystem::create_directories(path_.parent_path(),ec);if(ec){error_=L"Could not create preset directory";return false;}
    auto tmp=path_;tmp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){error_=L"Could not create preset temporary file";return false;}
    DWORD written=0;bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(h);CloseHandle(h);
    std::ifstream verify(tmp,std::ios::binary);std::string back((std::istreambuf_iterator<char>(verify)),{});verify.close();check.clear();ok=ok&&back==data&&parse(back,check,def);
    if(ok)ok=MoveFileExW(tmp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){error_=L"Atomic save of presets failed; original file was not replaced; temporary file kept";return false;}error_.clear();return true;
}
bool PresetStore::put(std::wstring name,EnhancementSettings s,bool replace){name=trim(name);if(!nameOk(name)||!s.validate().empty()){error_=L"Preset name or parameters are invalid (name is at most 48 characters)";return false;}auto old=entries_;auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& p){return p.name==name;});if(i!=entries_.end()){if(!replace){error_=L"Preset name already exists";return false;}i->settings=s;}else{if(entries_.size()>=64){error_=L"At most 64 presets can be saved";return false;}entries_.push_back({name,s});}if(save())return true;entries_=old;return false;}
bool PresetStore::rename(size_t i,std::wstring name){name=trim(name);if(i>=entries_.size()||!nameOk(name)||std::any_of(entries_.begin(),entries_.end(),[&](auto& p){return p.name==name;})){error_=L"Name is invalid or duplicated";return false;}auto old=entries_;auto d=default_;if(default_==entries_[i].name)default_=name;entries_[i].name=name;if(save())return true;entries_=old;default_=d;return false;}
bool PresetStore::erase(size_t i){if(i>=entries_.size())return false;auto old=entries_;auto d=default_;if(default_==entries_[i].name)default_.clear();entries_.erase(entries_.begin()+i);if(save())return true;entries_=old;default_=d;return false;}
bool PresetStore::setDefault(size_t i){if(i>=entries_.size())return false;auto old=default_;default_=entries_[i].name;if(save())return true;default_=old;return false;}
EnhancementSettings PresetStore::defaultSettings()const{for(auto& p:entries_)if(p.name==default_)return p.settings;return {};}
}
