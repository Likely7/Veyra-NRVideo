#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include <iostream>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext_d3d12va.h>
}
using namespace veyra;
#include "HardwareImportFixtures.h"
#include "D3D11FrameLeaseFixture.h"
// Explicit diagnostic readback. Compare the entire GPU-converted image, not
// merely successful decode/evaluate return codes or one non-black pixel.
int wmain(int argc,wchar_t** argv){
 const bool lease=argc==4&&std::wstring_view(argv[3])==L"--d3d11-lease";
 if(argc!=3&&!lease)return 2;
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;
 if(!ctx.initialize({},st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 3;
 if(lease){
  const bool passed=d3d11FrameLeaseFixture(argv[1],argv[2],ctx,ring);
  ring.shutdown();ctx.shutdown();CoUninitialize();
  return passed?0:12;
 }
 std::vector<sink::RgbaImage> reference;double worst=0;
 for(bool hardware:{false,true}){
  source::MediaFileSource source;source::SourceOpenDesc sd;sd.path=argv[1];sd.preferHardwareDecode=hardware;sd.d3d12Device=ctx.device();sd.d3d12Queue=ctx.directQueue();sd.d3d12AdapterLuid=ctx.adapter().luid;
  if(!source.open(sd))return 4;
  pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
  gd.sourceWidth=gd.workWidth=source.info().width;gd.sourceHeight=gd.workHeight=source.info().height;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
  if(!graph.initialize(gd)||!graph.createViews())return 5;
  std::cout<<"DECODE_PATH hardware="<<hardware<<" path="<<source.info().videoDecodePath<<" visible="<<source.info().width<<"x"<<source.info().height<<std::endl;
  for(unsigned i=0;i<4;++i){
   pipeline::FramePacket packet;const AVFrame* frame=nullptr;source::SourceReadStatus result;
   do{result=source.read(packet,&frame);}while(result==source::SourceReadStatus::Waiting);
   // Hardware decode is D3D12VA for most codecs and D3D11VA (NT-handle shared)
   // for HEVC; both must hand the ingress an importable NV12/P010 surface.
   const bool hwFrame=frame->format==AV_PIX_FMT_D3D12||frame->format==AV_PIX_FMT_D3D11;
   if(result!=source::SourceReadStatus::Frame||hwFrame!=hardware)return 6;
   if(hardware&&frame->format==AV_PIX_FMT_D3D12){auto* native=reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);auto d=native->texture->GetDesc();std::cout<<"GPU_IMPORT d3d12va frame="<<i<<" visible="<<frame->width<<"x"<<frame->height<<" resource="<<d.Width<<"x"<<d.Height<<" array="<<d.DepthOrArraySize<<" slice="<<native->subresource_index<<" format="<<d.Format<<std::endl;}
   else if(hardware){if(!packet.hardwareSurface.present())return 6;std::cout<<"GPU_IMPORT d3d11va frame="<<i<<" visible="<<frame->width<<"x"<<frame->height<<" resource="<<packet.hardwareSurface.textureWidth<<"x"<<packet.hardwareSurface.textureHeight<<" slice="<<packet.hardwareSurface.subresourceIndex<<" fence="<<(packet.hardwareSurface.waitFence!=nullptr)<<" value="<<packet.hardwareSurface.waitValue<<std::endl;}
   pipeline::EnhanceGraph::FrameOutputs out;
   if(!graph.process(frame,i*1000.0/30,i==0,out,i+1,&packet.colorInfo,&packet.hardwareSurface))return 7;
   sink::RgbaImage image;if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image))return 8;
   if(i==0&&!sink::saveImage(std::wstring(argv[2])+(hardware?L"-hardware.png":L"-software.png"),image))return 9;
   if(!hardware)reference.push_back(std::move(image));
   else{
    auto& ref=reference[i];if(ref.width!=image.width||ref.height!=image.height)return 10;
    double sum=0;for(size_t p=0;p<ref.pixels.size();++p)if(p%4!=3)sum+=std::abs(int(ref.pixels[p])-int(image.pixels[p]));
    double error=sum/(image.width*image.height*3.0);worst=std::max(worst,error);
    std::cout<<"PIXEL_COMPARE frame="<<i<<" meanAbsoluteError="<<error<<std::endl;
   }
   out={};
  }
  ring.drainQueue();graph.shutdown();source.close();
 }
 const bool fixtures=hardwareImportFixtures(ctx,ring);
 ring.shutdown();ctx.shutdown();CoUninitialize();
 std::cout<<"FULL_IMAGE_PASS="<<(worst<2.0)<<" worst="<<worst<<std::endl;
 return worst<2&&fixtures?0:11;
}
