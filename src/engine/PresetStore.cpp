#include "veyra/engine/PresetStore.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>
namespace veyra::engine {
namespace {
// 64 presets may each contain six full-precision grades and UTF-8 LUT names.
// This is metadata only; retain a finite bound before allocating/parsing it.
constexpr size_t kMaxPresetBytes=2u*1024u*1024u;
std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);if(n)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);return r;}
bool nameOk(const std::wstring& s){return !s.empty()&&s.size()<=48&&s.front()!=L' '&&s.back()!=L' '&&std::none_of(s.begin(),s.end(),[](wchar_t c){return c<32;});}
std::wstring trim(std::wstring s){auto a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return {};auto b=s.find_last_not_of(L" \t\r\n");return s.substr(a,b-a+1);}
}
std::string PresetStore::serialize()const{
    // Leave single-instance files readable by 1.4.4. A multi-grade preset
    // explicitly opts into v22; older readers reject rather than lose nodes.
    const bool stack=std::any_of(entries_.begin(),entries_.end(),[](const auto& p){return p.settings.nrLayerCount!=0;});
    const bool multi=stack||std::any_of(entries_.begin(),entries_.end(),[](const auto& p){return p.settings.additionalColorCount!=0;});
    // v24 appends the stage-5 output-stabiliser pair and the stage-6 per-layer
    // anti-flicker tier, and follows the same rule as v22/v23: write the LOWEST
    // version that still describes the store, so a store that never touched
    // either stays readable by older builds. The tier alone does not need v24:
    // its default (Flow) is exactly what `temporal=true` meant before the tiers
    // existed, so only a non-default tier forces the version up.
    const bool hold=std::any_of(entries_.begin(),entries_.end(),[](const auto& p){
        if(p.settings.nrHoldStrength!=0.0f)return true;
        for(uint32_t i=0;i<p.settings.nrLayerCount&&i<kMaxNrInstances;++i)
            if(p.settings.nrLayer(i).antiFlicker!=NrAntiFlicker::Flow)return true;
        return p.settings.nrAntiFlicker!=NrAntiFlicker::Flow;});
    const bool fsr4=std::any_of(entries_.begin(),entries_.end(),[](const auto& p){return p.settings.frameGenerationBackend==FrameGenerationBackend::Fsr4;});
    // Custom v28: the RTX Video HDR HDR->SDR tone-map tuning. Store-level like the
    // other appended flags: once any entry needs it, every entry writes it, so the
    // rows stay aligned.
    const bool hdrMap=std::any_of(entries_.begin(),entries_.end(),[](const auto& p){const auto& v=p.settings.videoHdr;
        return v.sourcePeakNits!=0||v.sdrWhiteNits!=203||v.exposureEv100!=0||v.shoulderPercent!=100;});
    const bool higher=std::any_of(entries_.begin(),entries_.end(),[](const auto& p){return p.settings.srTarget>pipeline::SrTarget::Uhd8K;});
    const bool rendering=std::any_of(entries_.begin(),entries_.end(),[](const auto& p){const auto& s=p.settings;return s.hdrOutputMode!=HdrOutputMode::Hdr10||s.fgMotion!=MotionSource::Automatic||s.srMotion!=MotionSource::OpticalFlow||s.nrMotion!=MotionSource::OpticalFlow;});
    const bool fullSchema=fsr4||higher||rendering;
    // v26 introduces appended resolution IDs; old readers reject this schema.
    std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(std::numeric_limits<float>::max_digits10)<<"VEYRA_PRESETS "<<(hdrMap?28:rendering?27:higher?26:fsr4?25:hold?24:stack?23:multi?22:21)<<'\n'<<std::quoted(utf8(default_))<<' '<<entries_.size()<<'\n';
    for(auto& p:entries_){const auto& s=p.settings;const auto& m=s.model;const auto& r=s.residual;o<<std::quoted(utf8(p.name))<<' '<<m.intensity<<' '<<m.tone<<' '<<m.structure<<' '<<m.skin<<' '<<m.style<<' '<<m.autoMask<<' '<<m.uiCorrection<<' '<<r.total<<' '<<r.darken<<' '<<r.brighten<<' '<<r.color<<' '<<r.luminance<<' '<<s.nr<<' '<<s.sr<<' '<<s.multiplier<<' '<<int(s.nrPolicy)<<' '<<int(s.flow)<<' '<<int(s.content)<<' '<<s.protection.enabled<<' '<<s.protection.featherPixels;for(auto q:s.protection.regions)o<<' '<<q.left<<' '<<q.top<<' '<<q.right<<' '<<q.bottom;o<<' '<<s.videoSrQuality<<' '<<int(s.frameGenerationBackend)<<' '<<int(s.srTarget)<<' '<<int(s.opticalFlowBackend)<<' '<<s.amdFlowHalfResolution<<' '<<int(s.audioSync)<<' '<<s.audioOffsetMs<<' '<<int(s.nrRuntime)<<' '<<s.captureCompatible<<' '<<s.lowLatency<<' '<<s.forceSdrPreview<<' '<<int(s.captureAudio)<<' '<<s.exportBitrateMbps<<' '<<s.captureFlipVertical<<' '<<int(s.captureBuffer)<<' ';writeColorSettings(o,s.color,utf8(s.color.lutNameString()));o<<' '<<s.videoHdr.enabled<<' '<<s.videoHdr.contrast<<' '<<s.videoHdr.saturation<<' '<<s.videoHdr.middleGray<<' '<<s.videoHdr.peakNits<<' '<<s.nrTemporal;
        if(multi||fullSchema){o<<' '<<s.additionalColorCount;for(unsigned i=0;i<s.additionalColorCount;++i){o<<' ';writeColorSettings(o,s.additionalColors[i],utf8(s.additionalColors[i].lutNameString()));}}
        if(stack||fullSchema){
            o<<' '<<s.nrLayerCount;
            for(unsigned i=0;i<s.nrLayerCount;++i){const auto& n=s.nrLayers[i];
                o<<' '<<n.model.intensity<<' '<<n.model.tone<<' '<<n.model.structure<<' '<<n.model.skin
                 <<' '<<n.model.style<<' '<<n.model.autoMask<<' '<<n.model.uiCorrection
                 <<' '<<n.residual.total<<' '<<n.residual.darken<<' '<<n.residual.brighten<<' '<<n.residual.color<<' '<<n.residual.luminance
                 <<' '<<int(n.runtime)<<' '<<n.temporal<<' '<<n.lowLatencyPairing<<' '<<n.enabled<<' '<<int(n.sizePolicy);
                // The tier is a v24 field: the reader only takes it at version>=24,
                // so writing it on a v23 line would misalign the rest of the row.
                if(hold||fullSchema)o<<' '<<int(n.antiFlicker);
            }
        }
        // v24: the hold flag is store-level, so every entry writes the pair once
        // any entry needs it. Otherwise entries in the same file would parse
        // differently.
        if(hold||fullSchema)o<<' '<<s.nrHoldStrength<<' '<<s.nrHoldTolerance;
        if(rendering)o<<' '<<int(s.hdrOutputMode)<<' '<<int(s.fgMotion)<<' '<<int(s.srMotion)<<' '<<int(s.nrMotion);
        // v28: appended last, only when this store asks for it (the reader takes
        // them at version>=28 only). The leading field used to be the HDR-source
        // route switch and is written as 0 to keep the row layout, and every store
        // written while it existed, readable.
        if(hdrMap)o<<' '<<0<<' '<<s.videoHdr.sourcePeakNits<<' '<<s.videoHdr.sdrWhiteNits<<' '<<s.videoHdr.exposureEv100<<' '<<s.videoHdr.shoulderPercent;
        o<<'\n';
    }return o.str();
}
bool PresetStore::parse(const std::string& data,std::vector<UserPreset>& out,std::wstring& def){
    if(data.size()>kMaxPresetBytes)return false;std::istringstream in(data);in.imbue(std::locale::classic());std::string magic,d;int version=0;size_t count=0;
    if(!(in>>magic>>version)||magic!="VEYRA_PRESETS"||(version<1||version>28)||!(in>>std::quoted(d)>>count)||count>64)return false;def=wide(d);if(!d.empty()&&def.empty())return false;
    for(size_t i=0;i<count;++i){UserPreset p;std::string n;int policy,flow,content,nr,sr;auto& s=p.settings;auto& m=s.model;auto& r=s.residual;
        if(!(in>>std::quoted(n)>>m.intensity>>m.tone>>m.structure>>m.skin>>m.style>>m.autoMask>>m.uiCorrection>>r.total>>r.darken>>r.brighten>>r.color>>r.luminance>>nr>>sr>>s.multiplier>>policy>>flow>>content))return false;
        if(version>=2){int enabled;if(!(in>>enabled>>s.protection.featherPixels)||enabled<0||enabled>1)return false;s.protection.enabled=enabled!=0;
            for(auto& q:s.protection.regions)if(!(in>>q.left>>q.top>>q.right>>q.bottom))return false;}
        if(version>=3&&!(in>>s.videoSrQuality))return false;
        // Retired local FSR4 mode: keep the preset, use RTX Video SR high.
        if(version==20&&s.videoSrQuality==6)s.videoSrQuality=3;
        if(version>=4){int backend;if(!(in>>backend)||backend<0||backend>(version>=25?3:version>=13?2:(version>=6&&version<=7?2:1)))return false;
            // Before v8, 1 meant removed FRUC and 2 meant XeSS. New writes use v10.
            s.frameGenerationBackend=version<8
                ?(backend==2?FrameGenerationBackend::XeSS:FrameGenerationBackend::Dlss)
                :static_cast<FrameGenerationBackend>(backend);}
        if(version>=5){uint32_t target;if(!(in>>target))return false;s.srTarget=static_cast<pipeline::SrTarget>(target);}
        if(version>=6){int backend,half;if(!(in>>backend>>half)||half<0||half>1)return false;s.opticalFlowBackend=static_cast<OpticalFlowBackend>(backend);s.amdFlowHalfResolution=half!=0;}
        if(version>=7){int sync;if(!(in>>sync>>s.audioOffsetMs))return false;s.audioSync=static_cast<AudioSyncMode>(sync);}
        if(version>=9){int runtime;if(!(in>>runtime))return false;s.nrRuntime=currentNrRuntime(static_cast<NrRuntime>(runtime));}
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
        if(version>=22){
            if(!(in>>s.additionalColorCount)||s.additionalColorCount>=kMaxColorInstances)return false;
            for(unsigned c=0;c<s.additionalColorCount;++c){std::string lut;
                if(!readColorSettings(in,s.additionalColors[c],lut,version))return false;
                if(!lut.empty()&&!s.additionalColors[c].setLutName(wide(lut)))return false;
            }
        }
        if(version>=23){
            if(!(in>>s.nrLayerCount)||s.nrLayerCount>kMaxNrInstances)return false;
            for(unsigned j=0;j<s.nrLayerCount;++j){auto& layer=s.nrLayers[j];int runtime,temporal,low,enabled,size;
                auto& lm=layer.model;auto& lr=layer.residual;
                if(!(in>>lm.intensity>>lm.tone>>lm.structure>>lm.skin>>lm.style>>lm.autoMask>>lm.uiCorrection
                     >>lr.total>>lr.darken>>lr.brighten>>lr.color>>lr.luminance>>runtime>>temporal>>low>>enabled>>size)
                   ||temporal<0||temporal>1||low<0||low>1||enabled<0||enabled>1)return false;
                layer.runtime=currentNrRuntime(static_cast<NrRuntime>(runtime));layer.temporal=temporal!=0;
                layer.lowLatencyPairing=low!=0;layer.enabled=enabled!=0;
                layer.sizePolicy=static_cast<pipeline::NrSizePolicy>(size);
                // v24 appends the tier after the size policy. A v23 store has no
                // tier, and Flow is what its `temporal=true` meant, which is
                // exactly the field default.
                if(version>=24){int tier;if(!(in>>tier)||!validNrAntiFlicker(NrAntiFlicker(tier)))return false;layer.antiFlicker=static_cast<NrAntiFlicker>(tier);}
            }
        }
        // v24: the output-stabiliser pair. Bounds are the ones the pass accepts:
        // 0 disables, 1 is full hold, and the tolerance is a relative delta.
        if(version>=24){
            if(!(in>>s.nrHoldStrength>>s.nrHoldTolerance))return false;
            if(!std::isfinite(s.nrHoldStrength)||s.nrHoldStrength<0.0f||s.nrHoldStrength>1.0f)return false;
            if(!std::isfinite(s.nrHoldTolerance)||s.nrHoldTolerance<0.0f||s.nrHoldTolerance>1.0f)return false;
        }
        if(version>=27){int hdr,fg,sr,nr;if(!(in>>hdr>>fg>>sr>>nr))return false;s.hdrOutputMode=HdrOutputMode(hdr);s.fgMotion=MotionSource(fg);s.srMotion=MotionSource(sr);s.nrMotion=MotionSource(nr);}
        // Custom v28: RTX Video HDR's HDR->SDR tone map tuning. Absent in older
        // stores, where the defaults (the previously hard-coded tone-map numbers)
        // are exactly the old behaviour. The leading field is the retired
        // HDR-source route switch: it is validated and dropped so old stores keep
        // loading and the row layout does not move.
        if(version>=28){
            int retired,srcPeak,whiteNits,ev,shoulder;
            if(!(in>>retired>>srcPeak>>whiteNits>>ev>>shoulder))return false;
            if(retired<0||retired>1||srcPeak<0||srcPeak>4000||whiteNits<80||whiteNits>400||ev<-200||ev>200||shoulder<50||shoulder>150)return false;
            s.videoHdr.sourcePeakNits=unsigned(srcPeak);
            s.videoHdr.sdrWhiteNits=unsigned(whiteNits);
            s.videoHdr.exposureEv100=ev;
            s.videoHdr.shoulderPercent=unsigned(shoulder);
        }
        p.name=wide(n);if(!nameOk(p.name)||std::any_of(out.begin(),out.end(),[&](auto& a){return a.name==p.name;})||nr<0||nr>1||sr<0||sr>1)return false;
        s.nr=nr;s.sr=sr;s.nrPolicy=static_cast<pipeline::NrSizePolicy>(policy);s.flow=static_cast<FlowQuality>(flow);s.content=static_cast<ContentRate>(content);
        if(!s.validate().empty())return false;out.push_back(std::move(p));
    }
    in>>std::ws;if(!in.eof())return false;
    return def.empty()||std::any_of(out.begin(),out.end(),[&](auto& a){return a.name==def;});
}
bool PresetStore::load(){
    error_.clear();if(!std::filesystem::exists(path_))return true;if(std::filesystem::file_size(path_)>kMaxPresetBytes){corrupt_=true;error_=L"预设文件超过2MiB，原文件保留";return false;}std::ifstream f(path_,std::ios::binary);std::string data((std::istreambuf_iterator<char>(f)),{});std::vector<UserPreset> loaded;std::wstring def;
    if(!f||!parse(data,loaded,def)){corrupt_=true;error_=L"预设文件损坏或版本不支持；原文件已保留，禁止覆盖。当前使用内建设置。";return false;}
    entries_=std::move(loaded);default_=std::move(def);corrupt_=false;return true;
}
bool PresetStore::save(){
    if(corrupt_){error_=L"损坏原文件受保护，未写入任何设置；请先备份并移走该文件";return false;}
    const auto data=serialize();std::vector<UserPreset> check;std::wstring def;if(!parse(data,check,def)){error_=L"预设校验失败";return false;}
    std::error_code ec;std::filesystem::create_directories(path_.parent_path(),ec);if(ec){error_=L"无法创建预设目录";return false;}
    auto tmp=path_;tmp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){error_=L"无法创建预设临时文件";return false;}
    DWORD written=0;bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(h);CloseHandle(h);
    std::ifstream verify(tmp,std::ios::binary);std::string back((std::istreambuf_iterator<char>(verify)),{});verify.close();check.clear();ok=ok&&back==data&&parse(back,check,def);
    if(ok)ok=MoveFileExW(tmp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){error_=L"预设原子保存失败，原文件未替换；临时文件保留";return false;}error_.clear();return true;
}
bool PresetStore::put(std::wstring name,EnhancementSettings s,bool replace){name=trim(name);if(!nameOk(name)||!s.validate().empty()){error_=L"预设名称或参数无效（名称最多48字）";return false;}auto old=entries_;auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& p){return p.name==name;});if(i!=entries_.end()){if(!replace){error_=L"预设名称已存在";return false;}i->settings=s;}else{if(entries_.size()>=64){error_=L"最多保存64套预设";return false;}entries_.push_back({name,s});}if(save())return true;entries_=old;return false;}
bool PresetStore::rename(size_t i,std::wstring name){name=trim(name);if(i>=entries_.size()||!nameOk(name)||std::any_of(entries_.begin(),entries_.end(),[&](auto& p){return p.name==name;})){error_=L"名称无效或重复";return false;}auto old=entries_;auto d=default_;if(default_==entries_[i].name)default_=name;entries_[i].name=name;if(save())return true;entries_=old;default_=d;return false;}
bool PresetStore::erase(size_t i){if(i>=entries_.size())return false;auto old=entries_;auto d=default_;if(default_==entries_[i].name)default_.clear();entries_.erase(entries_.begin()+i);if(save())return true;entries_=old;default_=d;return false;}
bool PresetStore::setDefault(size_t i){if(i>=entries_.size())return false;auto old=default_;default_=entries_[i].name;if(save())return true;default_=old;return false;}
EnhancementSettings PresetStore::defaultSettings()const{for(auto& p:entries_)if(p.name==default_)return p.settings;return {};}
}
