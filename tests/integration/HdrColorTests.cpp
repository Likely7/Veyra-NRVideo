#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <DirectXPackedVector.h>
#include "veyra/engine/VideoPresenter.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/RuntimePaths.h"
#include "CaptureFormatGpuCases.h"
#include "SdrColorGpuCases.h"
#include "HdrNativeRoundTripCases.h"
#include "HdrToneMapGpuCases.h"
#include "ScreenScRgbCases.h"
#include <iostream>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
int wmain(int argc,wchar_t** argv){
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;
 if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 2;
 int failures=captureGpuCases(ctx,ring)+sdrColorGpuCases(ctx,ring)+hdrRoundTrip::run(ctx,ring)+hdrToneTests::run(ctx,ring)+screenScRgbCases(ctx,ring);
 if(argc>1){
  std::vector<uint8_t> software;const float expectedPeak=argc>2?float(_wtof(argv[2])):0;
  for(bool hardware:{false,true}){
   source::MediaFileSource source;source::SourceOpenDesc sd;sd.path=argv[1];sd.preferHardwareDecode=hardware;sd.d3d12Device=ctx.device();sd.d3d12Queue=ctx.directQueue();
   bool ok=source.open(sd);pipeline::FramePacket packet;const AVFrame* frame=nullptr;
   if(ok)ok=source.read(packet,&frame)==source::SourceReadStatus::Frame;
   pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=source.info().width;gd.sourceHeight=gd.workHeight=source.info().height;
   gd.hdrInput=true;gd.enableNr=gd.enableSr=gd.enableFg=false;gd.noFeatures=true;
   pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;float selected=0;
   if(ok){selected=pipeline::hdrToneMapPeak(packet.colorInfo).nits;ok=(frame->format==AV_PIX_FMT_D3D12)==hardware&&(!expectedPeak||selected==expectedPeak)&&graph.initialize(gd)&&graph.createViews()&&graph.process(frame,0,true,out,1,&packet.colorInfo)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);}
   unsigned maxDifference=0;if(ok){if(!hardware)software=image.pixels;else{ok=software.size()==image.pixels.size();if(ok)for(size_t i=0;i<software.size();++i)maxDifference=std::max(maxDifference,unsigned(std::abs(int(software[i])-int(image.pixels[i]))));ok&=maxDifference<=1;}}
   std::cout<<"HDR_TONE_FILE hardware="<<hardware<<" selectedPeak="<<selected<<" expectedPeak="<<expectedPeak<<" softHardwareMaxCodeDifference="<<maxDifference<<" pass="<<ok<<std::endl;
   failures+=!ok;out={};ring.drainQueue();graph.shutdown();source.close();
  }
 }
 for(bool hlg:{false,true})for(bool native:{false,true})for(bool planar:{false,true})for(bool full:{false,true}){
  pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
  gd.sourceWidth=gd.workWidth=64;gd.sourceHeight=gd.workHeight=32;gd.enableNr=gd.enableFg=gd.enableSr=false;gd.noFeatures=true;gd.hdrInput=true;gd.hdrOutput=native;
  gd.hdrOutputMode=engine::HdrOutputMode::ScRgb; // this case reads linear FP16; HDR10 is checked separately above
  if(!graph.initialize(gd)||!graph.createViews())return 3;
  AVFrame* f=av_frame_alloc();f->width=64;f->height=32;f->format=planar?AV_PIX_FMT_YUV420P10LE:AV_PIX_FMT_P010;f->color_range=full?AVCOL_RANGE_JPEG:AVCOL_RANGE_MPEG;f->colorspace=AVCOL_SPC_BT2020_NCL;f->color_trc=hlg?AVCOL_TRC_ARIB_STD_B67:AVCOL_TRC_SMPTE2084;f->color_primaries=AVCOL_PRI_BT2020;
  if(av_frame_get_buffer(f,32)<0)return 4;
  // Published ST2084 reference encodings: black, 100 cd/m2, 1000 cd/m2.
  const double encoded[]={0,hlg?.75:.5080784215,hlg?1.0:.7518270962};const double nits[]={0,hlg?203.152:100,1000};
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto code=uint16_t(std::lround((full?0:64)+encoded[std::min(x/22,2u)]*(full?1023:876)));reinterpret_cast<uint16_t*>(f->data[0]+y*f->linesize[0])[x]=planar?code:uint16_t(code<<6);}
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<(planar?32u:64u);++x)reinterpret_cast<uint16_t*>(f->data[1]+y*f->linesize[1])[x]=planar?512:32768;
  if(planar)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x)reinterpret_cast<uint16_t*>(f->data[2]+y*f->linesize[2])[x]=512;
  pipeline::ColorDescription color;color.pixelFormat=pipeline::SourcePixelFormat::P010;color.transfer=hlg?pipeline::TransferFunction::HLG:pipeline::TransferFunction::PQ;color.matrix=pipeline::YuvMatrix::BT2020NCL;
  pipeline::EnhanceGraph::FrameOutputs out;bool ok=graph.process(f,0,true,out,1,&color);double error=0;
  if(ok&&!native){sink::RgbaImage image;ok=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);if(ok){for(unsigned i=0;i<3;++i){double m=hdrToneTests::luminance(nits[i],1000);double expected=m<=.0031308?12.92*m:1.055*pow(m,1/2.4)-.055;double actual=image.pixels[(i*22+5)*4]/255.0;error=std::max(error,std::abs(expected-actual));}ok=error<.012;}}
  if(ok&&!native){
   // Plan §5.1/§5.2 on HDR content. (a) Exposure is a scene-linear multiply that
   // happens *before* tone mapping, so +1 EV must match the independent BT.2390
   // reference evaluated on doubled luminance. (b) A LUT whose declared input
   // space cannot mean anything on HDR content (sRGB display reference) must be
   // refused with a visible notice instead of being applied silently.
   pipeline::EnhanceGraph gradeGraph(ctx,ring);pipeline::EnhanceGraphDesc ggd=gd;ggd.color.enabled=true;ggd.color.exposure=1.0f;
   pipeline::EnhanceGraph::FrameOutputs gradeOut,lutOut;sink::RgbaImage gradeImage,lutImage;double gradeError=1;
   bool graded=gradeGraph.initialize(ggd)&&gradeGraph.createViews();
   pipeline::EnhanceGraph lutGraph(ctx,ring);pipeline::EnhanceGraphDesc lgd=ggd;
   lgd.color.lutStrength=100.0f;lgd.color.lutInputSpace=engine::ColorSettings::kLutInputSrgb;
   const bool named=lgd.color.setLutName(L"hdr-input-space-probe.cube");
   bool refused=named&&lutGraph.initialize(lgd)&&lutGraph.createViews();
   if(graded){
    graded=gradeGraph.process(f,0,true,gradeOut,1,&color)&&sink::readRgba8(ctx,ring,gradeGraph.videoFrameResource(gradeOut.videoSlot),gradeImage);
    if(graded){gradeError=0;for(unsigned i=0;i<3;++i){
     const double m=hdrToneTests::luminance(std::min(nits[i]*2,1000.0),1000);
     const double expected=m<=.0031308?12.92*m:1.055*pow(m,1/2.4)-.055;
     const double actual=gradeImage.pixels[(i*22+5)*4]/255.0;gradeError=std::max(gradeError,std::abs(expected-actual));}
     graded=gradeError<.014;}
   }
   if(refused)refused=!lutGraph.colorLutNotice().empty();
   if(refused)refused=lutGraph.process(f,0,true,lutOut,1,&color)&&sink::readRgba8(ctx,ring,lutGraph.videoFrameResource(lutOut.videoSlot),lutImage);
   if(refused)refused=graded&&lutImage.pixels==gradeImage.pixels;
   std::cout<<"HDR_TONE_GRADE hlg="<<hlg<<" full="<<full<<" linearError="<<gradeError<<" lutInputSpaceRefused="<<refused<<" pass="<<(graded&&refused)<<std::endl;
   failures+=!(graded&&refused);
   gradeOut={};lutOut={};ring.drainQueue();gradeGraph.shutdown();lutGraph.shutdown();
  }
  if(ok&&native){
   auto* texture=graph.videoFrameResource(out.videoSlot);auto desc=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;ctx.device()->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&size);
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=size;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   Microsoft::WRL::ComPtr<ID3D12Resource> read;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))))return 5;
   uint32_t slot;auto* list=ring.acquireNext(slot,st);pipeline::StateTracker states;states.transition(list,texture,D3D12_RESOURCE_STATE_COPY_SOURCE);
   D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=texture;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=read.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=footprint;list->CopyTextureRegion(&b,0,0,0,&a,nullptr);states.transition(list,texture,D3D12_RESOURCE_STATE_COMMON);
   if(!ring.submitAndSignal(slot)||!ring.waitIdle())return 6;void* ptr=nullptr;D3D12_RANGE range{0,size};if(FAILED(read->Map(0,&range,&ptr)))return 7;
   const auto* values=reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(ptr)+footprint.Offset);
   for(unsigned i=0;i<3;++i){auto actual=DirectX::PackedVector::XMConvertHalfToFloat(values[(i*22+5)*4]);error=std::max(error,std::abs(actual-nits[i]/80));}D3D12_RANGE written{};read->Unmap(0,&written);ok=error<.12;
  }
  if(ok){
   HWND window=CreateWindowExW(0,L"STATIC",L"HDR contract",WS_POPUP,0,0,64,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
   engine::VideoPresenter presenter;ok=window&&presenter.open(ctx,window,graph)&&presenter.present(ctx,ring,graph,out.videoSlot,false);
   ring.drainQueue();presenter.close();if(window)DestroyWindow(window);
  }
  std::cout<<"HDR_COLOR hlg="<<hlg<<" native="<<native<<" planar="<<planar<<" full="<<full<<" error="<<error<<" pass="<<ok<<std::endl;if(!ok)++failures;
  out={};ring.drainQueue();graph.shutdown();av_frame_free(&f);
 }
 if(argc>1)for(unsigned mode=0;mode<3;++mode)for(bool hardware:{false,true}){
  source::MediaFileSource source;source::SourceOpenDesc sd;sd.path=argv[1];sd.preferHardwareDecode=hardware;sd.d3d12Device=ctx.device();sd.d3d12Queue=ctx.directQueue();
  if(!source.open(sd))return 8;
  pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=source.info().width;gd.sourceHeight=gd.workHeight=source.info().height;
  gd.hdrInput=true;gd.enableNr=true;gd.enableSr=mode>0;gd.enableFg=mode>0;gd.videoSrQuality=mode?1:0;gd.nrBeforeSr=mode==2;
  if(mode){gd.workWidth=3840;gd.workHeight=2160;}
  if(mode==2){gd.nrWidth=gd.sourceWidth;gd.nrHeight=gd.sourceHeight;}gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
  if(!graph.initialize(gd)||!graph.createViews())return 9;unsigned count=0,generated=0;bool confirmed=false;
  while(count<12){pipeline::FramePacket packet;const AVFrame* f=nullptr;auto result=source.read(packet,&f);if(result==source::SourceReadStatus::Waiting)continue;if(result!=source::SourceReadStatus::Frame)break;
   confirmed|=f->format==AV_PIX_FMT_D3D12;
   pipeline::EnhanceGraph::FrameOutputs out;
   if(!graph.process(f,count*1000.0/30,count==0,out,count+1,&packet.colorInfo))return 10;
   sink::RgbaImage image;if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image))return 11;
   if(std::none_of(image.pixels.begin(),image.pixels.end(),[](uint8_t v){return v>0&&v<255;}))return 12;
   if(mode){if(!graph.resolveGeneration(out))return 13;if(out.hasGenerated){sink::RgbaImage middle;if(!sink::readRgba8(ctx,ring,graph.generatedFrameResource(out.genSlot),middle))return 14;++generated;}}
   out={};++count;
  }
  bool ok=count==12&&confirmed==hardware&&graph.metrics().nrEvaluateCount==12&&(!mode||(graph.metrics().srEvaluateCount==12&&generated>0));
  std::cout<<"HDR_MAIN10_NR mode="<<mode<<" sr="<<graph.metrics().srEvaluateCount<<" generated="<<generated<<" hardware="<<hardware<<" confirmed="<<confirmed<<" frames="<<count<<" evaluate="<<graph.metrics().nrEvaluateCount<<" pass="<<ok<<std::endl;if(!ok)++failures;
  ring.drainQueue();graph.shutdown();source.close();
 }
 ring.shutdown();ctx.shutdown();CoUninitialize();return failures?1:0;
}
