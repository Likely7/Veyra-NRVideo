#include "veyra/engine/PresetStore.h"
#include "veyra/ngx/NrArchitecturePolicy.h"
#include <iostream>
#include <fstream>
#include <sstream>
namespace {
bool retiredSrMode(const std::filesystem::path& path) {
 using namespace veyra::engine;
 for(unsigned mode:{5u,6u,7u}) {
  std::ostringstream fixture;
  fixture<<"VEYRA_PRESETS 20\n\"retired\" 1\n\"retired\" 1 1 1 -1 0 0 0 1 1 1 1 1 0 1 1 0 1 0";
  fixture<<" 0 0";
  for(int region=0;region<4;++region)fixture<<" 0 0 0 0";
  fixture<<' '<<mode<<" 0 1 0 0 1 137 0 1 0 0 0 60 0 0 ";
  writeColorSettings(fixture,{}, "");
  fixture<<" 1 100 100 50 1000\n";
  {std::ofstream file(path);file<<fixture.str();}
  PresetStore store(path);
  if(mode==7) {
   if(store.load()||store.put(L"must not overwrite",{}))return false;
   std::ifstream file(path);std::string unchanged((std::istreambuf_iterator<char>(file)),{});
   if(unchanged!=fixture.str())return false;
   continue;
  }
  if(!store.load())return false;
  const auto value=store.defaultSettings();
  if(value.videoSrQuality!=(mode==6?3u:mode)||!value.sr||value.nr||
     value.audioOffsetMs!=137||value.exportBitrateMbps!=60||
     !value.videoHdr.enabled||value.videoHdr.peakNits!=1000)return false;
  if(!store.save())return false;
  PresetStore reloaded(path);
  if(!reloaded.load()||reloaded.defaultSettings()!=value)return false;
 }
 std::cout<<"retired SR migration and unknown mode preservation=1\n";
 return true;
}
bool schemaVersions(const std::filesystem::path& path) {
 using namespace veyra::engine;
 for(int version=1;version<=21;++version) {
  std::ostringstream fixture;
  fixture<<"VEYRA_PRESETS "<<version<<"\n\"versioned\" 1\n\"versioned\" 1 1 1 -1 0 0 0 1 1 1 1 1 0 0 1 0 1 0";
  if(version>=2) {
   fixture<<" 0 0";
   for(int region=0;region<4;++region)fixture<<" 0 0 0 0";
  }
  if(version>=3)fixture<<" 2";
  if(version>=4)fixture<<" 0";
  if(version>=5)fixture<<" 1";
  if(version>=6)fixture<<" 0 0";
  if(version>=7)fixture<<" 1 137";
  if(version>=9)fixture<<" 0";
  if(version>=10)fixture<<" 1";
  if(version>=11)fixture<<" 0";
  if(version>=12)fixture<<" 0";
  if(version>=14)fixture<<" 0";
  if(version>=15)fixture<<" 60";
  if(version>=16)fixture<<" 1";
  if(version>=17)fixture<<" 1";
  if(version>=18) {
   std::ostringstream color;
   writeColorSettings(color,{},"");
   if(version==18) {
    std::istringstream fields(color.str());
    std::vector<std::string> tokens;
    for(std::string token;fields>>token;)tokens.push_back(token);
    if(tokens.size()<30||tokens[27]!="\"\""||tokens[28]!="0")return false;
    tokens.erase(tokens.begin()+28); // v19 added the bypass mask after the LUT path.
    for(const auto& token:tokens)fixture<<' '<<token;
   } else fixture<<' '<<color.str();
  }
  if(version>=20)fixture<<" 0 100 100 50 1000";
  if(version>=21)fixture<<" 1";
  fixture<<'\n';
  const auto original=fixture.str();
  {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<original;}
  PresetStore store(path);
  if(!store.load()) {std::cout<<"schema v"<<version<<" load failed\n";return false;}
  if(store.defaultSettings().audioOffsetMs!=(version>=7?137:0)||
     store.defaultSettings().exportBitrateMbps!=(version>=15?60u:0u)||
     store.defaultSettings().captureFlipVertical!=(version>=16)||
     store.defaultSettings().nrTemporal!=(version>=21)) {
   std::cout<<"schema v"<<version<<" fields mismatch\n";return false;
  }
  {
   std::ifstream before(path,std::ios::binary);
   if(std::string(std::istreambuf_iterator<char>(before),{})!=original) {
    std::cout<<"schema v"<<version<<" modified on load\n";return false;
   }
  }
  if(!store.save()) {std::cout<<"schema v"<<version<<" upgrade failed\n";return false;}
  PresetStore upgraded(path);
  if(!upgraded.load()||upgraded.defaultSettings()!=store.defaultSettings()) {
   std::cout<<"schema v"<<version<<" upgrade mismatch\n";return false;
  }
 }
 std::cout<<"v1-v21 preset read, unchanged source, and upgrade roundtrip=1\n";
 return true;
}
bool legacyBackends(const std::filesystem::path& path) {
 using namespace veyra::engine;
 unsigned checks=0;
 for(int version=4;version<=14;++version)for(int backend=0;backend<=2;++backend)for(int multiplier:{2,4}){
  std::ostringstream fixture;
  fixture<<"VEYRA_PRESETS "<<version<<"\n\"legacy\" 1\n\"legacy\" 1 1 1 -1 0 0 0 1 1 1 1 1 1 0 "<<multiplier<<" 0 1 0";
  fixture<<" 0 0";
  for(int region=0;region<4;++region)fixture<<" 0 0 0 0";
  fixture<<" 2 "<<backend;
  if(version>=5)fixture<<" 1";
  if(version>=6)fixture<<" 0 0";
  if(version>=7)fixture<<" 1 137";
  if(version>=9)fixture<<" 1";
  if(version>=10)fixture<<" 1";
  if(version>=11)fixture<<" 0";
  if(version>=12)fixture<<" 0";
  if(version>=14)fixture<<" 0";
  fixture<<'\n';
  {std::ofstream file(path);file<<fixture.str();}
  const bool xess=version<8?backend==2:backend==1;
  const bool fsr=version>=13&&backend==2;
  const bool supportedValue=backend<2||(version>=6&&version<=7)||fsr;
  // FSR accepts 2X only (the 3.1.x provider generates one frame per present).
  // XeSS presets up to 4X stay loadable: the audited unlock raises the ceiling
  // and the engine clamps to the runtime value. DLSS accepts both here.
  const bool expected=supportedValue&&(!fsr||multiplier==2);
  PresetStore store(path);
  if(store.load()!=expected)return false;
  if(expected){
   const auto value=store.defaultSettings();
   if(value.lowLatency||value.forceSdrPreview)return false;
   if(value.captureCompatible!=(version>=10))return false;
   if(value.nrRuntime!=(version>=9?NrRuntime::Ampere:NrRuntime::Original))return false;
   const auto expectedBackend=fsr?FrameGenerationBackend::Fsr:xess?FrameGenerationBackend::XeSS:FrameGenerationBackend::Dlss;
   if(value.frameGenerationBackend!=expectedBackend||value.multiplier!=multiplier||value.videoSrQuality!=2)return false;
   if(version>=7&&(value.audioSync!=AudioSyncMode::Manual||value.audioOffsetMs!=137))return false;
   if(!store.put(L"legacy",value,true))return false;
   std::ifstream file(path);std::string magic;int savedVersion=0;file>>magic>>savedVersion;
   if(magic!="VEYRA_PRESETS"||savedVersion!=21||value.videoHdr.enabled)return false;
   PresetStore reloaded(path);
   if(!reloaded.load()||reloaded.defaultSettings()!=value)return false;
  }else{
   if(store.put(L"must not overwrite",{}))return false;
   std::ifstream file(path);std::string unchanged((std::istreambuf_iterator<char>(file)),{});
   if(unchanged!=fixture.str())return false;
  }
  ++checks;
 }
 std::cout<<"legacy/current backend migration cases="<<checks<<'\n';
 return true;
}
}
int main(int argc,char** argv){if(argc!=2)return 2;using namespace veyra::engine;const std::filesystem::path p=argv[1];PresetStore a(p);bool ok=a.load();EnhancementSettings s;s.videoSrQuality=2;s.frameGenerationBackend=FrameGenerationBackend::Dlss;s.opticalFlowBackend=OpticalFlowBackend::AmdFidelityFx;s.amdFlowHalfResolution=true;s.protection.enabled=true;s.protection.featherPixels=3.5f;s.protection.regions[0]={.1f,.2f,.7f,.8f};s.protection.regions[3]={0,0,1,1};s.model.intensity=.375f;s.model.skin=1.5f;s.residual.darken=1.2f;s.multiplier=4;s.flow=FlowQuality::Quality;s.content=ContentRate::Fps50;s.nrPolicy=veyra::pipeline::NrSizePolicy::Native;
 for(uint32_t family:{0x160u,0x170u,0x171u,0x190u,0x1A0u,0x1B0u})for(bool selected:{false,true})for(int result:{0,-1}){
  auto value=family;const bool expected=selected&&result==0&&(family==0x170u||family==0x171u);
  ok=ok&&(veyra::ngx::rewriteNrAmpereArchitecture(value,selected,result)==expected)&&value==(expected?0x1B0u:family);
 }
 // NeuralScreen 310.8.0.0 keeps the rewrite; SF 310.8.2.0 and Lecram 310.8.3.0 do not.
 ok=ok&&veyra::ngx::nrRuntimeNeedsAmpereRewrite((310u<<16)|8u,0u)&&veyra::ngx::nrRuntimeNeedsAmpereRewrite((310u<<16)|8u,1u<<16)
     &&!veyra::ngx::nrRuntimeNeedsAmpereRewrite((310u<<16)|8u,2u<<16)&&!veyra::ngx::nrRuntimeNeedsAmpereRewrite((310u<<16)|8u,3u<<16)
     &&veyra::ngx::nrRuntimeNeedsAmpereRewrite((311u<<16)|0u,0u);
 s.srTarget=veyra::pipeline::SrTarget::Uhd8K;
 ok=ok&&veyra::ngx::preferSfNr(0x10DE,L"NVIDIA GeForce RTX 2080 Ti")&&veyra::ngx::preferSfNr(0x10DE,L"NVIDIA GeForce RTX 3090")
      &&veyra::ngx::preferSfNr(0x10DE,L"NVIDIA GeForce RTX 4070 SUPER")&&!veyra::ngx::preferSfNr(0x10DE,L"NVIDIA GeForce RTX 5070")
      &&!veyra::ngx::preferSfNr(0x1002,L"RTX 4070")&&!veyra::ngx::preferSfNr(0x10DE,L"Unknown")
      &&currentNrRuntime(NrRuntime::Community)==NrRuntime::Ampere&&currentNrRuntime(NrRuntime::Original)==NrRuntime::Original;
 s.audioSync=AudioSyncMode::Manual;s.audioOffsetMs=137;
s.nrRuntime=NrRuntime::Ampere;s.captureCompatible=true;s.lowLatency=true;s.forceSdrPreview=true;
s.videoHdr={true,110,80,45,800};
// Colour grade round-trips through schema v18 (plan P1: named colour presets).
s.color.temperature=-40;s.color.tint=7.5f;s.color.exposure=1.25f;s.color.contrast=-12.5f;s.color.highlights=18;s.color.shadows=-22;
s.color.whites=9;s.color.blacks=-4;s.color.vibrance=33;s.color.saturation=-8;
s.color.paramHighlights=20;s.color.paramShadows=-25;s.color.splitHighlights=-15;s.color.splitMidtones=12;s.color.splitShadows=-6;
s.color.curves[0].count=3;s.color.curves[0].points[1]={0.25f,0.35f};s.color.curves[0].points[2]={1,1};
s.color.curves[2].count=2;s.color.curves[2].points[0]={0,0.05f};s.color.curves[2].points[1]={0.95f,1};
s.color.mixerHue[3]=25.5f;s.color.mixerSaturation[0]=-60;s.color.mixerLuminance[5]=12;
s.color.blackWhite=true;s.color.blackWhiteMix[2]=-33.5f;
s.color.grading[1]={210.5f,45,-20};s.color.gradingBlending=62.5f;s.color.gradingBalance=-18;
s.color.calibrationShadowTint=9.5f;s.color.calibrationHue[1]=-22;s.color.calibrationSaturation[2]=17.5f;
ok=ok&&s.color.setLutName(L"film.cube");s.color.lutStrength=75;s.color.lutInputSpace=1;
ok=ok&&a.put(L"test",s)&&a.setDefault(0)&&!a.put(L"test",s);PresetStore b(p);ok=ok&&b.load()&&b.defaultSettings()==s&&b.rename(0,L"renamed")&&b.defaultSettings()==s;
// Colour validation guards the same ranges the panel exposes.
auto badGrading=s;badGrading.color.grading[2].hue=400;ok=ok&&!b.put(L"invalid grading hue",badGrading);
auto badCurve=s;badCurve.color.curves[1].count=3;badCurve.color.curves[1].points[1]={0.6f,0.5f};badCurve.color.curves[1].points[2]={0.2f,0.7f};ok=ok&&!b.put(L"unsorted curve",badCurve);
auto badLut=s;badLut.color.lutInputSpace=3;ok=ok&&!b.put(L"invalid lut space",badLut);
auto badExposure=s;badExposure.color.exposure=6;ok=ok&&!b.put(L"invalid exposure",badExposure);
auto badHdr=s;badHdr.videoHdr.peakNits=2001;ok=ok&&!b.put(L"invalid HDR peak",badHdr);
ok=ok&&b.entries().size()==1&&b.defaultSettings()==s;
 
 auto badTarget=s;badTarget.srTarget=static_cast<veyra::pipeline::SrTarget>(6);ok=ok&&!b.put(L"invalid target",badTarget);
 for(auto target:{veyra::pipeline::SrTarget::Uhd5K,veyra::pipeline::SrTarget::Uhd6K,veyra::pipeline::SrTarget::Uhd7K}){
     auto higher=s;higher.srTarget=target;ok=ok&&b.put(L"higher resolution",higher);
     PresetStore reload(p);ok=ok&&reload.load()&&reload.entries().back().settings==higher&&b.erase(1);
 }
 ok=ok&&unsigned(veyra::pipeline::SrTarget::Uhd8K)==2;
 auto rendering=s;rendering.hdrOutputMode=HdrOutputMode::ScRgb;rendering.fgMotion=MotionSource::Zero;rendering.srMotion=MotionSource::Zero;rendering.nrMotion=MotionSource::Zero;
 ok=ok&&b.put(L"rendering",rendering);PresetStore renderReload(p);ok=ok&&renderReload.load()&&renderReload.entries().back().settings==rendering&&b.erase(1);
 
 // XeSS presets may request up to 4X since the audited multi-frame unlock; the
 // engine clamps to the ceiling the provider reports at session start.
 auto xess=s;xess.frameGenerationBackend=FrameGenerationBackend::XeSS;ok=ok&&b.put(L"XeSS 4X",xess);
 PresetStore xessReload(p);ok=ok&&xessReload.load()&&xessReload.entries().back().settings==xess&&b.erase(1);
 xess.multiplier=5;ok=ok&&!b.put(L"XeSS 5X rejected",xess);
 xess.multiplier=2;ok=ok&&b.put(L"XeSS 2X",xess);PresetStore xess2Reload(p);ok=ok&&xess2Reload.load()&&xess2Reload.entries().back().settings==xess&&b.erase(1);
 
 // AMD FSR frame generation: the 3.1.x provider delivers one generated frame
 // per present, so 2X round-trips and 4X is refused by validation.
 auto fsr=s;fsr.frameGenerationBackend=FrameGenerationBackend::Fsr;ok=ok&&!b.put(L"FSR 4X rejected",fsr);
 
 fsr.multiplier=2;ok=ok&&b.put(L"FSR 2X",fsr);PresetStore fsrReload(p);ok=ok&&fsrReload.load()&&fsrReload.entries().back().settings==fsr&&b.erase(1);
 
 auto fsr4=fsr;fsr4.frameGenerationBackend=FrameGenerationBackend::Fsr4;
 ok=ok&&b.put(L"FSR 4 ML 2X",fsr4);PresetStore fsr4Reload(p);
 ok=ok&&fsr4Reload.load()&&fsr4Reload.entries().back().settings==fsr4&&b.erase(1);
 fsr4.multiplier=4;ok=ok&&!b.put(L"FSR 4 ML 4X rejected",fsr4);
 auto badBackend=s;badBackend.frameGenerationBackend=static_cast<FrameGenerationBackend>(4);ok=ok&&!b.put(L"invalid backend",badBackend);
 auto dis=s;dis.opticalFlowBackend=OpticalFlowBackend::GpuDis;ok=ok&&b.put(L"GPU DIS",dis)&&b.save();PresetStore disReload(p);ok=ok&&disReload.load()&&disReload.entries().back().settings==dis&&b.erase(1);
 
 auto badFlow=s;badFlow.opticalFlowBackend=static_cast<OpticalFlowBackend>(3);ok=ok&&!b.put(L"invalid flow backend",badFlow);
 auto ampere=s;ampere.nr=true;ampere.nrRuntime=NrRuntime::Ampere;ok=ok&&b.put(L"RTX30",ampere);PresetStore ampereReload(p);ok=ok&&ampereReload.load()&&ampereReload.entries().back().settings==ampere&&b.erase(1);
 auto original=s;original.nr=true;original.nrRuntime=NrRuntime::NvidiaOriginal;ok=ok&&b.put(L"NVIDIA original",original);PresetStore originalReload(p);ok=ok&&originalReload.load()&&originalReload.entries().back().settings==original&&b.erase(1);
 auto badNr=s;badNr.nrRuntime=static_cast<NrRuntime>(4);ok=ok&&!b.put(L"invalid NR runtime",badNr);
 auto half=s;half.content=ContentRate::Capture60To30;ok=ok&&b.put(L"capture half rate",half);PresetStore halfReload(p);ok=ok&&halfReload.load()&&halfReload.entries().back().settings.content==ContentRate::Capture60To30&&b.erase(1);
 // 5 is the AMD FSR upscaling slot now, so it must round-trip; 6 stays invalid.
 auto fsrSr=s;fsrSr.videoSrQuality=veyra::engine::kVideoSrFsr;ok=ok&&b.put(L"FSR SR",fsrSr);
 PresetStore fsrSrReload(p);ok=ok&&fsrSrReload.load()&&fsrSrReload.entries().back().settings.videoSrQuality==veyra::engine::kVideoSrFsr&&b.erase(1);
 auto badSr=s;badSr.videoSrQuality=veyra::engine::kVideoSrFsr+1;ok=ok&&!b.put(L"invalid video SR",badSr);
 // Export bitrate round-trips through the preset format (v15) and stays inside
 // the range the encoders accept.
 auto bitrate=s;bitrate.exportBitrateMbps=60;ok=ok&&b.put(L"bitrate 60",bitrate);
 PresetStore bitrateReload(p);ok=ok&&bitrateReload.load()&&bitrateReload.entries().back().settings.exportBitrateMbps==60&&b.erase(1);
 auto badBitrate=s;badBitrate.exportBitrateMbps=301;ok=ok&&!b.put(L"invalid bitrate",badBitrate);
 auto invalid=s;invalid.model.tone=9;ok=ok&&!b.put(L"bad",invalid)&&b.entries().size()==1&&b.erase(0)&&b.entries().empty();
 auto badRegion=s;badRegion.protection.regions[0].left=2;ok=ok&&!b.put(L"invalid region",badRegion);
 // Capture audio ingress: the manual Dolby/DTS selector round-trips and an
 // out-of-range value is refused.
 // The store is empty here (the invalid-settings block erased the last entry),
 // so the round-tripped preset lands at index 0.
 auto ingress=s;ingress.captureAudio=CaptureAudioIngress::BitstreamPreferred;ok=ok&&b.put(L"audio bitstream",ingress);
 PresetStore ingressReload(p);ok=ok&&ingressReload.load()&&ingressReload.entries().size()==1&&ingressReload.entries().back().settings.captureAudio==CaptureAudioIngress::BitstreamPreferred&&b.erase(0);
 auto badIngress=s;badIngress.captureAudio=static_cast<CaptureAudioIngress>(3);ok=ok&&!b.put(L"invalid ingress",badIngress);
 // Manual capture vertical flip (v16) round-trips with the rest of the preset.
 auto flipped=s;flipped.captureFlipVertical=true;ok=ok&&b.put(L"capture flip",flipped);
 PresetStore flipReload(p);ok=ok&&flipReload.load()&&flipReload.entries().size()==1&&flipReload.entries().back().settings.captureFlipVertical&&b.erase(0);
 // Capture video-pin buffer policy (v17) round-trips with the rest of the preset.
 auto buffers=s;buffers.captureBuffer=veyra::source::CaptureBufferMode::Minimum;ok=ok&&b.put(L"capture buffer",buffers);
 PresetStore bufferReload(p);ok=ok&&bufferReload.load()&&bufferReload.entries().size()==1&&bufferReload.entries().back().settings.captureBuffer==veyra::source::CaptureBufferMode::Minimum&&b.erase(0);
 auto badBuffer=s;badBuffer.captureBuffer=static_cast<veyra::source::CaptureBufferMode>(3);ok=ok&&!b.put(L"invalid buffer",badBuffer);
 auto multi=s;multi.additionalColorCount=5;
 for(unsigned i=0;i<5;++i){multi.additionalColors[i].enabled=i!=2;multi.additionalColors[i].exposure=float(i)*.25f;multi.additionalColors[i].setLutName(L"grade-"+std::to_wstring(i)+L".cube");}
 ok=ok&&b.put(L"six colors",multi);PresetStore multiReload(p);
 ok=ok&&multiReload.load()&&multiReload.entries().size()==1&&multiReload.entries()[0].settings==multi&&b.erase(0);
 // Six grades must not reduce the advertised 64-preset capacity. Exercise
 // full curves and long Unicode LUT names, not just mostly-zero defaults.
 {
  auto dense=multi;
  auto fill=[](ColorSettings& color){
   color.enabled=true;color.setLutName(std::wstring(220,L'色')+L".cube");
   for(auto& curve:color.curves){curve.count=kColorCurvePoints;
    for(int j=0;j<kColorCurvePoints;++j)curve.points[j]={float(j)/7.0f,float(j*j)/49.0f};}
  };
  fill(dense.color);for(auto& color:dense.additionalColors)fill(color);
  auto fullPath=p;fullPath+=L".capacity";PresetStore full(fullPath);bool capacity=full.load();
  for(unsigned i=0;i<64&&capacity;++i)capacity=full.put(L"six-grade-"+std::to_wstring(i),dense);
  PresetStore loaded(fullPath);capacity=capacity&&loaded.load()&&loaded.entries().size()==64;
  for(const auto& entry:loaded.entries())capacity=capacity&&entry.settings==dense;
  std::cout<<"64 six-grade presets full-curve/Unicode roundtrip="<<capacity<<'\n';ok=capacity&&ok;
  auto hugePath=p;hugePath+=L".oversized";const size_t bytes=2u*1024u*1024u+1;
  {std::ofstream f(hugePath,std::ios::binary);f<<std::string(bytes,'x');}
  PresetStore huge(hugePath);ok=!huge.load()&&!huge.put(L"do not overwrite",{})&&std::filesystem::file_size(hugePath)==bytes&&ok;
 }
 {
  auto stackPath=p;stackPath+=L".nr-stack-v23";PresetStore stack(stackPath);
  EnhancementSettings layers;layers.nr=true;layers.nrLayerCount=4;layers.additionalColorCount=1;
  layers.additionalColors[0].exposure=.75f;
  for(unsigned i=0;i<4;++i){auto& n=layers.nrLayers[i];n.enabled=i!=1;
   n.sizePolicy=static_cast<veyra::pipeline::NrSizePolicy>(i);
   n.model.skin=1.5f;n.model.intensity=.25f*float(i+1);n.residual.total=.3f*float(i+1);n.temporal=(i%2)!=0;}
  bool roundtrip=stack.load()&&stack.put(L"four-independent-NR",layers);
  PresetStore loaded(stackPath);roundtrip=roundtrip&&loaded.load()&&loaded.entries().back().settings==layers;
  std::cout<<"NR v23 four layers mixed sizes bypass/model/residual/color roundtrip="<<roundtrip<<'\n';ok=roundtrip&&ok;
  // v24: per-layer tiers and the stabiliser pair, stored beside a default entry
  // in the same file (every row must then carry the v24 fields consistently).
  auto tierPath=p;tierPath+=L".nr-stack-v24";PresetStore tiers(tierPath);
  auto tiered=layers;tiered.nrHoldStrength=.6f;tiered.nrHoldTolerance=.05f;
  const NrAntiFlicker kinds[]={NrAntiFlicker::Off,NrAntiFlicker::Static,NrAntiFlicker::FlowPlus,NrAntiFlicker::LowFrequency};
  for(unsigned i=0;i<4;++i)tiered.nrLayers[i].antiFlicker=kinds[i];
  bool v24=tiers.load()&&tiers.put(L"plain-stack",layers)&&tiers.put(L"tiered-stack",tiered);
  PresetStore tierLoaded(tierPath);v24=v24&&tierLoaded.load()&&tierLoaded.entries().size()==2;
  for(const auto& e:v24?tierLoaded.entries():std::vector<UserPreset>{})v24=v24&&e.settings==(e.name==L"tiered-stack"?tiered:layers);
  std::ifstream header(tierPath);std::string magic;int written=0;v24=v24&&(header>>magic>>written)&&written==24;
  std::cout<<"NR v24 per-layer tiers and stabiliser with a default entry roundtrip="<<v24<<'\n';ok=v24&&ok;
 }
 {std::ofstream legacy(p);legacy<<"VEYRA_PRESETS 1\n\"legacy\" 1\n\"legacy\" 1 1 1 -1 0 0 0 1 1 1 1 1 1 0 1 0 1 0\n";}
 PresetStore old(p);ok=ok&&old.load()&&!old.defaultSettings().protection.enabled&&old.defaultSettings().srTarget==veyra::pipeline::SrTarget::Uhd4K&&old.put(L"v2",s);
 PresetStore upgraded(p);ok=ok&&upgraded.load()&&upgraded.entries().size()==2&&upgraded.entries()[1].settings==s;
 
{std::ofstream f(p);f<<"VEYRA_PRESETS 99\ncorrupt mediaPath executable must reject";}PresetStore c(p);const bool corruptLoaded=c.load();const bool corruptPut=c.put(L"override",{});std::ifstream f(p);std::string data((std::istreambuf_iterator<char>(f)),{});
 const bool corruptPreserved=data=="VEYRA_PRESETS 99\ncorrupt mediaPath executable must reject";
 ok=ok&&!corruptLoaded&&!corruptPut&&corruptPreserved;
 f.close();ok=legacyBackends(p)&&ok;ok=retiredSrMode(p)&&ok;ok=schemaVersions(p)&&ok;
 
 std::cout<<"preset roundtrip, all fields, duplicate, rename-default, delete, validation, unknown schema, corrupt-preservation="<<ok<<'\n';return ok?0:1;}
