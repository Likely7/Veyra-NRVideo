// GPU acceptance for the v4 colour chain. The grade rides inside the ingest
// dispatch, so these cases drive a real graph and read the presented frame:
//   * master switch off  -> byte-identical to the ungraded baseline
//   * enabled + neutral  -> byte-identical to the ungraded baseline (identity)
//   * +1 EV              -> clearly brighter coded value
//   * point curve        -> the control point lands where the model says
//   * saturation -100    -> a saturated input collapses to grey
// The optional .cube LUT is exercised once its importer lands (T5).
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/ColorSettings.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/ColorLut.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <memory>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <cstdlib>
#include <d3d12sdklayers.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace {
using namespace veyra;
int failures=0;
void check(bool ok,const std::string& label){
    std::printf("%s %s\n",ok?"PASS":"FAIL",label.c_str());
    if(!ok)++failures;
}
constexpr int kSize=64;

// One frame of a solid colour, sRGB-coded RGB32 (the still-image / RGB capture
// ingress path), so the linear value is exactly the sRGB decode of the code.
struct Frame {
    AVFrame* frame=nullptr;
    ~Frame(){if(frame)av_frame_free(&frame);}
    bool makeHdr(bool hlg){
        frame=av_frame_alloc();if(!frame)return false;
        frame->format=AV_PIX_FMT_P010;frame->width=frame->height=kSize;
        frame->color_range=AVCOL_RANGE_MPEG;frame->colorspace=AVCOL_SPC_BT2020_NCL;
        frame->color_primaries=AVCOL_PRI_BT2020;frame->color_trc=hlg?AVCOL_TRC_ARIB_STD_B67:AVCOL_TRC_SMPTE2084;
        if(av_frame_get_buffer(frame,32)<0)return false;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x)
            reinterpret_cast<uint16_t*>(frame->data[0]+y*frame->linesize[0])[x]=uint16_t((100+650*x/(kSize-1))<<6);
        for(int y=0;y<kSize/2;++y)for(int x=0;x<kSize;++x)
            reinterpret_cast<uint16_t*>(frame->data[1]+y*frame->linesize[1])[x]=32768;
        return true;
    }
    bool make(uint8_t r,uint8_t g,uint8_t b){
        frame=av_frame_alloc();
        frame->format=AV_PIX_FMT_BGR0;frame->width=frame->height=kSize;
        frame->color_range=AVCOL_RANGE_UNSPECIFIED;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
        if(av_frame_get_buffer(frame,32)<0)return false;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x){
            auto* p=frame->data[0]+std::size_t(y)*frame->linesize[0]+std::size_t(x)*4;
            p[0]=b;p[1]=g;p[2]=r;p[3]=255;
        }
        return true;
    }
    // Horizontal ramp used by the banding check: the grade compresses it so the
    // ideal 8-bit output advances by a fraction of a code per pixel.
    bool makeRamp(uint8_t from,uint8_t to,int size=kSize){
        frame=av_frame_alloc();
        frame->format=AV_PIX_FMT_BGR0;frame->width=frame->height=size;
        frame->color_range=AVCOL_RANGE_UNSPECIFIED;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
        if(av_frame_get_buffer(frame,32)<0)return false;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){
            const int value=from+int(std::lround(double(to-from)*double(x)/double(size-1)));
            auto* p=frame->data[0]+std::size_t(y)*frame->linesize[0]+std::size_t(x)*4;
            p[0]=uint8_t(value);p[1]=uint8_t(value);p[2]=uint8_t(value);p[3]=255;
        }
        return true;
    }
};
struct Graph {
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;
    // A graph owns six large baked-table instances. Several local Graphs in
    // one test function must not exhaust the default Windows 1 MiB stack.
    std::unique_ptr<pipeline::EnhanceGraph> graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring);
    pipeline::EnhanceGraph& g=*graph;
    bool up=false;
    bool startColors(const engine::EnhancementSettings& settings,bool hdrInput=false,bool hdrOutput=false,int size=kSize){
        if(!up){
            gfx::DeviceContextDesc device;Status st;
            device.enableDebugLayer=std::getenv("VEYRA_COLOR_GPU_DEBUG")!=nullptr;
            if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
            up=true;
        }else {ring.drainQueue();g.shutdown();}
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=size;
        gd.rgbInput=!hdrInput;gd.hdrInput=hdrInput;gd.hdrOutput=hdrOutput;
        gd.stillImage=true;gd.noFeatures=true;gd.noNgx=true;
        gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;gd.outputDitherStep=0;
        engine::StageRequest request;request.width=request.height=size;request.stillImage=true;
        engine::describeStages(request,settings,gd);
        return g.initialize(gd)&&g.createViews();
    }
    bool start(bool colorEnabled,bool rgb=true){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=rgb;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        gd.color.enabled=colorEnabled;
        return g.initialize(gd)&&g.createViews();
    }
    bool startWithLut(const std::wstring& lutName,int inputSpace=engine::ColorSettings::kLutInputSrgb){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=true;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        gd.color.enabled=true;
        if(!gd.color.setLutName(lutName))return false;
        gd.color.lutStrength=100.0f;
        gd.color.lutInputSpace=inputSpace;
        return g.initialize(gd)&&g.createViews();
    }
    bool startWithDither(float ditherStep){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=true;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        // A compressing grade: the ramp must be flattened by the tone curve so
        // plateaus are long enough to measure the dither's effect.
        gd.color.enabled=true;gd.color.contrast=-100.0f;gd.outputDitherStep=ditherStep;
        return g.initialize(gd)&&g.createViews();
    }
    bool apply(const engine::EnhancementSettings& settings){return g.applySettings(settings);}
    bool render(Frame& frame,sink::RgbaImage& out){
        pipeline::EnhanceGraph::FrameOutputs outputs;
        return g.process(frame.frame,0,true,outputs,1)&&
               sink::readRgba8(ctx,ring,g.videoFrameResource(outputs.videoSlot),out);
    }
    ~Graph(){if(up){
        ring.drainQueue();g.shutdown();ring.shutdown();
        if(std::getenv("VEYRA_COLOR_GPU_DEBUG")){
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
            const bool available=SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)));
            check(available,"D3D12 debug info queue is actually available");
            unsigned errors=0;
            if(available)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
                SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<uint8_t> storage(size);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if(SUCCEEDED(info->GetMessage(i,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                    std::printf("D3D12_ERROR id=%u %s\n",unsigned(message->ID),message->pDescription);++errors;
                }
            }
            check(available&&errors==0,"D3D12 resource lifetime and dispatch errors=0");
        }
        ctx.shutdown();
    }}
};
struct Pixel { int r=0,g=0,b=0; };
// Test-only raw FP16 readback preserves exact comparison-reference values.
bool readReference(Graph& graph,ID3D12Resource* texture,std::vector<uint8_t>& pixels,
                   D3D12_RESOURCE_STATES initialState=D3D12_RESOURCE_STATE_COMMON){
    if(!texture)return false;
    const auto desc=texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    graph.ctx.device()->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if(FAILED(graph.ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
    Status status;uint32_t slot;auto* list=graph.ring.acquireNext(slot,status);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,initialState,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!graph.ring.submitAndSignal(slot)||!graph.ring.waitIdle())return false;
    void* data=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};if(FAILED(readback->Map(0,&range,&data)))return false;
    pixels.resize(size_t(desc.Width)*desc.Height*8);
    for(unsigned y=0;y<desc.Height;++y)memcpy(pixels.data()+size_t(y)*desc.Width*8,static_cast<uint8_t*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(desc.Width)*8);
    D3D12_RANGE empty{};readback->Unmap(0,&empty);return true;
}
// Keep this separate from the legacy large wmain stack frame.
__declspec(noinline) void exactExposureTests(){
    // Multiplication by two and its inverse are exact on this normal FP16
    // range. Neutral log/HSV round trips must not lose a half-float ULP.
    auto plainStorage=std::make_unique<engine::EnhancementSettings>();
    auto settingsStorage=std::make_unique<engine::EnhancementSettings>();
    auto& plain=*plainStorage;auto& settings=*settingsStorage;settings.additionalColorCount=2;
    settings.additionalColors[0].enabled=settings.additionalColors[1].enabled=true;
    settings.additionalColors[0].exposure=1;settings.additionalColors[1].exposure=-1;
    auto baseline=std::make_unique<Graph>();auto graded=std::make_unique<Graph>();Frame frame;
    pipeline::EnhanceGraph::FrameOutputs output;std::vector<uint8_t> reference,roundTrip;
    const auto linear=[&](Graph& graph,std::vector<uint8_t>& data){
        return graph.g.process(frame.frame,0,true,output,1)&&
            readReference(graph,graph.g.diagnosticLinearInput(),data,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
    const bool ok=frame.makeRamp(0,255)&&baseline->startColors(plain)&&graded->startColors(settings)&&
        linear(*baseline,reference)&&linear(*graded,roundTrip);
    check(ok&&!reference.empty()&&reference==roundTrip,"standalone +1/-1 EV round trip preserves every FP16 bit");
    settings.additionalColors[0].temperature=30;
    std::vector<uint8_t> warm;
    check(ok&&graded->apply(settings)&&linear(*graded,warm)&&warm!=reference,"non-neutral live edits leave the exact-exposure path");
    settings.additionalColors[0].groupBypassMask=1u<<1; // bypass white balance
    std::vector<uint8_t> bypassed;
    check(ok&&graded->apply(settings)&&linear(*graded,bypassed)&&bypassed==reference,"bypassed white balance restores exact exposure without rebuilding graph");
}
Pixel center(const sink::RgbaImage& image){
    const auto index=(std::size_t(kSize/2)*std::size_t(image.width)+std::size_t(kSize/2))*4;
    return {image.pixels[index],image.pixels[index+1],image.pixels[index+2]};
}
// Generate the reference in an isolated stage with the original v1.4.4 RGB
// ingress shader, then compare these deterministic intermediate/output bytes.
// This tests the shader contract, not the complete v1.4.4 executable.
bool fusedLegacyReference(const std::filesystem::path& path,bool write){
    std::vector<uint8_t> actual;
    constexpr std::array<const char*,7> names={
        "off","disabled-exposure","neutral","plus-one","minus-one","quarter","mixed"};
    for(size_t i=0;i<names.size();++i){
        auto settings=std::make_unique<engine::EnhancementSettings>();
        settings->color.enabled=i>=2;
        if(i==1||i==3)settings->color.exposure=1;
        if(i==4)settings->color.exposure=-1;
        if(i==5)settings->color.exposure=0.25f;
        if(i==6){settings->color.exposure=0.25f;settings->color.temperature=20;settings->color.saturation=-15;}
        Graph graph;Frame frame;sink::RgbaImage image;std::vector<uint8_t> linear;
        if(!frame.makeRamp(0,255))return false;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x){
            auto* p=frame.frame->data[0]+size_t(y)*frame.frame->linesize[0]+size_t(x)*4;
            p[0]=uint8_t(x*255/(kSize-1));p[1]=uint8_t(y*255/(kSize-1));
            p[2]=uint8_t((x+y)*255/(2*(kSize-1)));
        }
        if(!graph.startColors(*settings)||!graph.render(frame,image)||
           !readReference(graph,graph.g.diagnosticLinearInput(),linear,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||
           linear.size()!=size_t(kSize)*kSize*8||image.pixels.size()!=size_t(kSize)*kSize*4)return false;
        actual.insert(actual.end(),linear.begin(),linear.end());
        actual.insert(actual.end(),image.pixels.begin(),image.pixels.end());
        std::printf("FUSED-LEGACY case=%s linearBytes=%zu outputBytes=%zu%c",names[i],linear.size(),image.pixels.size(),10);
    }
    if(write){
        if(std::filesystem::exists(path)||!std::filesystem::is_directory(path.parent_path()))return false;
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char*>(actual.data()),std::streamsize(actual.size()));
        return bool(file);
    }
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=std::streamoff(actual.size()))return false;
    std::vector<uint8_t> expected(actual.size());file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(expected.data()),std::streamsize(expected.size())))return false;
    size_t differences=0;for(size_t i=0;i<actual.size();++i)differences+=actual[i]!=expected[i];
    std::printf("FUSED-LEGACY comparedBytes=%zu differingBytes=%zu%c",actual.size(),differences,10);
    return differences==0;
}
}
void colorLifecycleTests(){
    auto settings=std::make_unique<engine::EnhancementSettings>();
    settings->color.enabled=true;settings->color.exposure=0.25f;settings->additionalColorCount=5;
    for(unsigned i=0;i<5;++i){settings->additionalColors[i].enabled=true;settings->additionalColors[i].saturation=float(i)*4-8;}
    Graph graph;Frame frame;sink::RgbaImage first;
    const bool started=frame.makeRamp(0,255)&&graph.startColors(*settings)&&graph.render(frame,first);
    check(started,"lifecycle: open six-instance graph and produce a complete frame");
    if(!started)return;
    const std::array<double,4> pts{16.0,0.0,10000.0,10016.0};
    for(size_t i=0;i<pts.size();++i){
        pipeline::EnhanceGraph::FrameOutputs output;sink::RgbaImage image;
        const bool ok=graph.g.process(frame.frame,pts[i],i==1||i==2,output,i+2)&&
            sink::readRgba8(graph.ctx,graph.ring,graph.g.videoFrameResource(output.videoSlot),image);
        check(ok&&image.pixels==first.pixels,"lifecycle: frame/reset/backward-seek/discontinuity preserves stateless color");
    }
    {
        Frame larger;Graph reference;sink::RgbaImage resized,expected;
        const bool ok=larger.makeRamp(0,255,96)&&graph.startColors(*settings,false,false,96)&&graph.render(larger,resized)&&
            reference.startColors(*settings,false,false,96)&&reference.render(larger,expected);
        check(ok&&resized.width==96&&resized.height==96&&resized.pixels==expected.pixels,
              "lifecycle: resize/recreate six instances matches a fresh graph");
    }
    {
        Frame other;Graph reference;sink::RgbaImage changed,expected;
        const bool ok=other.make(10,80,200)&&graph.startColors(*settings)&&graph.render(other,changed)&&
            reference.startColors(*settings)&&reference.render(other,expected);
        check(ok&&changed.pixels==expected.pixels&&changed.pixels!=first.pixels,
              "lifecycle: source switch does not retain preceding source pixels");
    }
    for(unsigned count:std::array<unsigned,2>{1,0}){
        settings->additionalColorCount=count;settings->color.enabled=count!=0;
        const bool requiresRebuild=!graph.apply(*settings);
        Graph reference;sink::RgbaImage changed,expected;
        const bool ok=graph.startColors(*settings)&&graph.render(frame,changed)&&
            reference.startColors(*settings)&&reference.render(frame,expected);
        check(requiresRebuild&&ok&&changed.pixels==expected.pixels,
              "lifecycle: six-to-two-to-zero topology rebuild matches a fresh graph");
    }
    graph.g.shutdown();graph.g.shutdown();
    check(!graph.g.initialized()&&!graph.g.colorGradeActive(),"lifecycle: repeated close releases all color state");
}
void colorDeviceRemovalTest(){
    auto settings=std::make_unique<engine::EnhancementSettings>();
    settings->color.enabled=true;settings->color.exposure=0.25f;settings->additionalColorCount=5;
    for(auto& c:settings->additionalColors){c.enabled=true;c.saturation=-4;}
    Frame frame;sink::RgbaImage before,after;
    if(!frame.makeRamp(0,255)){check(false,"device-removal fixture");return;}
    {
        Graph graph;Microsoft::WRL::ComPtr<ID3D12Device5> device;
        const bool ready=graph.startColors(*settings)&&graph.render(frame,before)&&graph.ring.drainQueue()&&
            SUCCEEDED(graph.ctx.device()->QueryInterface(IID_PPV_ARGS(&device)));
        check(ready,"device-removal: isolated graph ready and submitted work drained");
        if(!ready)return;
        // Explicitly remove this test process's device, not a system TDR.
        // Expected removal diagnostics are separate from unexpected debug errors.
        device->RemoveDevice();
        const HRESULT reason=graph.ctx.device()->GetDeviceRemovedReason();
        std::printf("EXPECTED_DEVICE_REMOVAL reason=0x%08X%c",unsigned(reason),10);
        check(FAILED(reason),"device-removal: failure was actually injected");
        pipeline::EnhanceGraph::FrameOutputs output;
        // process() records/submits asynchronous GPU work; true is not proof of
        // execution or presentation. VideoPresenter::present rejects a removed
        // device separately. This test only certifies color-resource recovery.
        const bool submissionAccepted=graph.g.process(frame.frame,16,true,output,2);
        uint32_t reportedReason=0;
        std::printf("EXPECTED_DEVICE_REMOVAL submissionAccepted=%d%c",int(submissionAccepted),10);
        check(!graph.ctx.checkDeviceAlive(reportedReason)&&reportedReason==uint32_t(reason),
              "device-removal: device health remains failed regardless of submission result");
        graph.g.shutdown();graph.g.shutdown();
        check(!graph.g.initialized()&&!graph.g.colorGradeActive(),"device-removal: graph teardown is idempotent");
        graph.ring.shutdown();graph.ctx.shutdown();graph.up=false;device.Reset();
    }
    Graph recovered;
    check(recovered.startColors(*settings)&&recovered.render(frame,after)&&before.pixels==after.pixels,
          "device-removal: a fresh device and graph reproduce the complete prior image");
}
int wmain(int argc,wchar_t** argv){
    if(argc>1){
        if(argc==2&&std::wstring_view(argv[1])==L"--lifecycle"){colorLifecycleTests();return failures?1:0;}
        if(argc==2&&std::wstring_view(argv[1])==L"--device-removal"){colorDeviceRemovalTest();return failures?1:0;}
        if(argc!=3)return 2;
        const std::wstring_view mode=argv[1];
        if(mode!=L"--write-fused-reference"&&mode!=L"--compare-fused-reference")return 2;
        check(fusedLegacyReference(argv[2],mode==L"--write-fused-reference"),
              "legacy fused full-image intermediate/output byte contract");
        return failures?1:0;
    }
    exactExposureTests();
    // R5.3: compare complete images, not just hashes or a centre pixel. The
    // independent path rounds ingress to FP16 before grading (fused does not).
    const auto maxDifference=[](const sink::RgbaImage& a,const sink::RgbaImage& b){
        if(a.pixels.empty()||a.pixels.size()!=b.pixels.size())return 999;
        int maxDiff=0;for(size_t i=0;i<a.pixels.size();++i)if(i%4!=3)maxDiff=std::max(maxDiff,std::abs(int(a.pixels[i])-int(b.pixels[i])));
        return maxDiff;
    };
    {
        engine::EnhancementSettings settings,plain;settings.color.enabled=true;settings.additionalColorCount=5;
        for(auto& color:settings.additionalColors)color.enabled=true;
        Graph a,b;Frame frame;sink::RgbaImage first,second;
        const bool ok=frame.makeRamp(0,255)&&a.startColors(settings)&&b.startColors(plain)&&a.render(frame,first)&&b.render(frame,second);
        check(ok&&first.pixels==second.pixels,"six enabled-neutral grades are byte-identical to no grading");
    }
    {
        engine::EnhancementSettings missing,reference;missing.color.enabled=true;missing.additionalColorCount=1;missing.additionalColors[0].enabled=true;
        missing.color.setLutName(L"r53-unavailable-primary.cube");missing.additionalColors[0].setLutName(L"r53-unavailable-secondary.cube");
        reference=missing;reference.color.lutName={};reference.additionalColors[0].lutName={};
        Graph a,b;Frame frame;sink::RgbaImage first,second;
        bool ok=frame.makeRamp(30,190)&&a.startColors(missing)&&b.startColors(reference);
        missing.color.exposure=reference.color.exposure=.25f;missing.additionalColors[0].contrast=reference.additionalColors[0].contrast=10;
        ok=ok&&a.apply(missing)&&b.apply(reference)&&a.render(frame,first)&&b.render(frame,second);
        check(ok&&first.pixels==second.pixels,"missing primary and secondary LUTs stay disabled after live edits");
    }
    // Additional grades run after ingress tone mapping. Their LUT contract is
    // the working texture's domain, not the source video's original transfer.
    {
        const engine::ColorLutStore store(runtime::localDataDirectory());
        std::filesystem::create_directories(store.folder());
        const std::wstring name=L"gpu-post-tonemap-halve.cube";
        {
            std::ofstream file(store.folder()/name);file<<"LUT_3D_SIZE 2\n";
            for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r)
                file<<r*.5f<<' '<<g*.5f<<' '<<b*.5f<<'\n';
        }
        for(bool hlg:{false,true}){
            engine::EnhancementSettings plain,srgb,pq;
            srgb.additionalColorCount=1;auto& grade=srgb.additionalColors[0];
            grade.enabled=true;grade.setLutName(name);grade.lutInputSpace=engine::ColorSettings::kLutInputSrgb;
            pq=srgb;pq.additionalColors[0].lutInputSpace=engine::ColorSettings::kLutInputPq;
            auto baseline=std::make_unique<Graph>(),accepted=std::make_unique<Graph>(),rejected=std::make_unique<Graph>();
            Frame frame;sink::RgbaImage reference,withSrgb,withPq;
            bool ok=frame.makeHdr(hlg)&&baseline->startColors(plain,true,false)&&accepted->startColors(srgb,true,false)&&
                rejected->startColors(pq,true,false)&&baseline->render(frame,reference)&&accepted->render(frame,withSrgb)&&rejected->render(frame,withPq);
            int maxError=999;
            if(ok&&!reference.pixels.empty()&&reference.pixels.size()==withSrgb.pixels.size()){
                maxError=0;
                for(size_t i=0;i<reference.pixels.size();++i)if(i%4!=3)
                    maxError=std::max(maxError,std::abs(int(withSrgb.pixels[i])-int(std::lround(reference.pixels[i]*.5f))));
            }
            check(ok&&accepted->g.colorLutNotice().empty()&&maxError<=2&&maxDifference(reference,withSrgb)>20,
                  std::format("{} to SDR standalone sRGB LUT halves coded ramp (maxError={})",hlg?"HLG":"PQ",maxError));
            check(ok&&!rejected->g.colorLutNotice().empty()&&reference.pixels==withPq.pixels,
                  std::format("{} to SDR rejects standalone PQ LUT without changing pixels",hlg?"HLG":"PQ"));
            engine::EnhancementSettings noLut=pq;noLut.additionalColors[0].lutName={};
            noLut.additionalColors[0].exposure=pq.additionalColors[0].exposure=.25f;
            auto editedReference=std::make_unique<Graph>();sink::RgbaImage edited,expected;
            ok=ok&&rejected->apply(pq)&&editedReference->startColors(noLut,true,false)&&
                rejected->render(frame,edited)&&editedReference->render(frame,expected);
            check(ok&&edited.pixels==expected.pixels,
                  std::format("{} to SDR rejected PQ LUT stays disabled after live edits",hlg?"HLG":"PQ"));
        }
        std::error_code ec;std::filesystem::remove(store.folder()/name,ec);
    }
    for(bool hlg:{false,true}){
        engine::EnhancementSettings fused,standalone;
        fused.color.enabled=true;fused.color.exposure=.75f;
        standalone.additionalColorCount=1;standalone.additionalColors[0]=fused.color;
        Graph a,b;Frame frame;pipeline::EnhanceGraph::FrameOutputs oa,ob;
        std::vector<uint8_t> first,second;
        const bool ok=frame.makeHdr(hlg)&&a.startColors(fused,true,true)&&b.startColors(standalone,true,true)&&
            a.g.process(frame.frame,0,true,oa,1)&&b.g.process(frame.frame,0,true,ob,1)&&
            readReference(a,a.g.videoFrameResource(oa.videoSlot),first)&&readReference(b,b.g.videoFrameResource(ob.videoSlot),second);
        const auto half=[](uint16_t h){
            const unsigned exponent=(h>>10)&31,mantissa=h&1023;
            const float value=exponent?std::ldexp(1.f+float(mantissa)/1024,int(exponent)-15):std::ldexp(float(mantissa),-24);
            return h&0x8000?-value:value;
        };
        float maxError=0,maxValue=0;
        if(ok&&first.size()==second.size())for(size_t i=0;i<first.size();i+=2){
            const float x=half(uint16_t(first[i])|(uint16_t(first[i+1])<<8));
            const float y=half(uint16_t(second[i])|(uint16_t(second[i+1])<<8));
            maxError=std::max(maxError,std::abs(x-y));maxValue=std::max(maxValue,y);
        }
        check(ok&&!first.empty()&&first.size()==second.size()&&maxError<=.04f&&maxValue>1,
              std::format("native {} fused/standalone FP16 full-ramp maxError={} maxValue={}",hlg?"HLG":"PQ",maxError,maxValue));
    }
    {
        engine::EnhancementSettings settings;settings.color.enabled=true;settings.additionalColorCount=5;
        for(auto& color:settings.additionalColors)color.enabled=true;
        Graph streamed,reference;Frame frame;pipeline::EnhanceGraph::FrameOutputs outputs;
        bool ok=frame.makeRamp(30,160)&&streamed.startColors(settings);
        for(unsigned i=0;ok&&i<24;++i){
            settings.color.exposure=float(i%4)*.1f;
            settings.color.curves[0].points[1].y=.85f+float(i%4)*.04f;
            for(unsigned c=0;c<5;++c)settings.additionalColors[c].saturation=float((i+c)%6)*2;
            ok=streamed.apply(settings)&&streamed.g.process(frame.frame,i*16.667,i==0,outputs,i+1);
        }
        sink::RgbaImage first,second;
        ok=ok&&sink::readRgba8(streamed.ctx,streamed.ring,streamed.g.videoFrameResource(outputs.videoSlot),first)&&
            reference.startColors(settings)&&reference.render(frame,second);
        check(ok&&maxDifference(first,second)<=1,"24 six-grade live updates without per-frame readback match a fresh graph");
    }
    {
        engine::EnhancementSettings fused,standalone;
        fused.color.enabled=true;fused.color.exposure=0.55f;fused.color.contrast=12;fused.color.temperature=8;
        standalone.additionalColorCount=1;standalone.additionalColors[0]=fused.color;
        Graph a,b;Frame ramp;sink::RgbaImage first,second;
        const bool ok=ramp.makeRamp(28,210)&&a.startColors(fused)&&b.startColors(standalone)&&a.render(ramp,first)&&b.render(ramp,second);
        check(ok&&maxDifference(first,second)<=2,"standalone and fused SDR grades agree within 2 codes over a full ramp");
        standalone.additionalColors[0].exposure=-0.5f;
        sink::RgbaImage edited;
        check(ok&&b.apply(standalone)&&b.render(ramp,edited)&&maxDifference(second,edited)>20,"independent color live parameters update the actual GPU output");
    }
    {
        engine::EnhancementSettings ab,ba;ab.color.enabled=true;ab.color.exposure=1;
        ab.additionalColorCount=1;ab.additionalColors[0].enabled=true;ab.additionalColors[0].contrast=65;
        ba=ab;std::swap(ba.color,ba.additionalColors[0]);
        Graph a,b;Frame ramp;sink::RgbaImage first,second;
        const bool ok=ramp.makeRamp(35,180)&&a.startColors(ab)&&b.startColors(ba)&&a.render(ramp,first)&&b.render(ramp,second);
        check(ok&&maxDifference(first,second)>8,"two GPU color instances preserve noncommutative execution order");
    }
    {
        const engine::ColorLutStore store(runtime::localDataDirectory());
        std::filesystem::create_directories(store.folder());
        const unsigned sizes[]={2,3,5,17,33,4}; // exercise 256-byte padded rows
        engine::EnhancementSettings settings;settings.additionalColorCount=5;
        for(unsigned i=0;i<6;++i){
            auto& color=i?settings.additionalColors[i-1]:settings.color;
            color.enabled=true;color.lutInputSpace=engine::ColorSettings::kLutInputSrgb;
            color.setLutName(L"gpu-multi-"+std::to_wstring(i)+L".cube");
            std::ofstream file(store.folder()/color.lutNameString());
            file<<"LUT_3D_SIZE "<<sizes[i]<<'\n';
            for(unsigned z=0;z<sizes[i];++z)for(unsigned y=0;y<sizes[i];++y)for(unsigned x=0;x<sizes[i];++x){
                const float d=float(sizes[i]-1);
                file<<(float(x)/d*(.90f+.01f*i)+.008f*(i+1))<<' '
                    <<(float(y)/d*(.93f-.01f*i)+.005f*(6-i))<<' '
                    <<(float(z)/d*(.88f+.015f*i)+.01f*(i%3))<<'\n';
            }
        }
        Graph graph;Frame ramp;bool ok=ramp.makeRamp(32,196);
        for(unsigned cycle=0;cycle<3;++cycle){
            sink::RgbaImage result;ok=ok&&graph.startColors(settings)&&graph.render(ramp,result);
            int maxError=0,minValue=255,maxValue=0;
            if(ok){
                for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x){
                    float r=ramp.frame->data[0][y*ramp.frame->linesize[0]+x*4]/255.0f,g=r,b=r;
                    for(unsigned i=0;i<6;++i){r=r*(.90f+.01f*i)+.008f*(i+1);g=g*(.93f-.01f*i)+.005f*(6-i);b=b*(.88f+.015f*i)+.01f*(i%3);}
                    const float expected[]={r,g,b};
                    for(unsigned c=0;c<3;++c){const int value=result.pixels[(size_t(y)*kSize+x)*4+c];
                        maxError=std::max(maxError,std::abs(value-int(std::lround(expected[c]*255))));minValue=std::min(minValue,value);maxValue=std::max(maxValue,value);}
                }
            }
            check(ok&&maxError<=3&&minValue>0&&maxValue-minValue>30,std::format("six independent LUTs cycle={} maxError={} min={} max={} (nonblack/nonconstant)",cycle,maxError,minValue,maxValue));
            if(ok){
                check(!graph.g.setColorLut(nullptr,2,1)&&!graph.g.setColorLut(nullptr,2,6),"invalid LUT payload/index rejected without replacing resources");
                graph.g.shutdown();graph.g.shutdown();
                check(!graph.g.colorGradeActive(),"shutdown releases and resets all color instances (idempotent)");
            }
        }
        for(unsigned i=0;i<6;++i){std::error_code ec;std::filesystem::remove(store.folder()/(L"gpu-multi-"+std::to_wstring(i)+L".cube"),ec);}
    }
    {
        Graph graph;Frame red;
        bool ready=graph.start(true)&&red.make(200,0,0);
        for(float amount:{-100.0f,100.0f}){
            engine::EnhancementSettings settings;settings.color.enabled=true;
            settings.color.mixerHue[0]=amount;
            sink::RgbaImage image;
            const bool rendered=ready&&graph.apply(settings)&&graph.render(red,image)&&!image.pixels.empty();
            const auto p=rendered?center(image):Pixel{};
            check(rendered&&p.r>150&&(amount<0?(p.b>60&&p.g<5):(p.g>60&&p.b<5)),
                  amount<0?"negative red hue wraps towards magenta":"positive red hue moves towards orange");
        }
    }
    for(bool rgb:{true,false}){
        Graph graph;Frame frame;
        bool ok=graph.start(true,rgb);
        if(rgb)ok=ok&&frame.make(100,120,140);
        else{
            frame.frame=av_frame_alloc();frame.frame->format=AV_PIX_FMT_NV12;frame.frame->width=frame.frame->height=kSize;
            frame.frame->color_range=AVCOL_RANGE_MPEG;frame.frame->colorspace=AVCOL_SPC_BT709;frame.frame->color_trc=AVCOL_TRC_BT709;
            ok=ok&&av_frame_get_buffer(frame.frame,32)>=0;
            if(ok){for(int y=0;y<kSize;++y)memset(frame.frame->data[0]+y*frame.frame->linesize[0],100,kSize);
                for(int y=0;y<kSize/2;++y)memset(frame.frame->data[1]+y*frame.frame->linesize[1],128,kSize);}
        }
        pipeline::EnhanceGraph::FrameOutputs out;
        std::vector<uint8_t> baseline,original,base;
        sink::RgbaImage graded,released;
        ok=ok&&graph.g.process(frame.frame,0,true,out,1)&&readReference(graph,graph.g.sourceReference(out.videoSlot),baseline);
        engine::EnhancementSettings settings;settings.color.enabled=true;settings.color.exposure=1.5f;
        ok=ok&&graph.apply(settings);
        for(uint64_t id=2;ok&&id<=5;++id){
            out={};
            ok=graph.g.process(frame.frame,double(id)*33,false,out,id)&&
                readReference(graph,graph.g.sourceReference(out.videoSlot),original)&&
                readReference(graph,graph.g.baseReference(out.videoSlot),base)&&
                sink::readRgba8(graph.ctx,graph.ring,graph.g.videoFrameResource(out.videoSlot),graded);
            ok=ok&&original==baseline&&base!=original;
        }
        out={};
        ok=ok&&graph.g.process(frame.frame,200,false,out,6,nullptr,nullptr,false)&&
            sink::readRgba8(graph.ctx,graph.ring,graph.g.videoFrameResource(out.videoSlot),released)&&released.pixels==graded.pixels;
        check(ok,rgb?"RGB original excludes grade across both slots; base and released output retain grade":"NV12 original excludes grade across both slots; base and released output retain grade");
    }
    // 1. Master switch off and enabled-neutral are both identity.
    {
        Graph off,on;
        if(!off.start(false)||!on.start(true)){std::printf("FAIL graph init\n");return 1;}
        Frame f1,f2;
        sink::RgbaImage baseline,neutral;
        const bool ready=f1.make(188,96,64)&&f2.make(188,96,64)&&off.render(f1,baseline)&&on.render(f2,neutral);
        check(ready,"colour graphs initialise and render");
        if(ready)check(baseline.pixels==neutral.pixels,"master off and enabled-neutral are byte-identical to the ungraded path");
    }
    // 2. Exposure, curve, saturation and hue behaviour through the real graph.
    {
        Graph graph;
        if(!graph.start(true)){std::printf("FAIL graph init\n");return 1;}
        Frame neutral,exposed,curved,grass,desaturated;
        sink::RgbaImage a,b,c,d,e;
        if(!(neutral.make(188,188,188)&&exposed.make(188,188,188)&&curved.make(188,188,188)&&
             grass.make(64,160,64)&&desaturated.make(64,160,64)&&graph.render(neutral,a))){
            std::printf("FAIL baseline render\n");return 1;
        }
        const auto neutralPixel=center(a);
        Pixel neutralGrass{};
        {
            if(!graph.render(grass,d)){std::printf("FAIL colour render\n");return 1;}
            const auto coloured=center(d);neutralGrass=coloured;
            check(coloured.g>coloured.r&&coloured.g>coloured.b,"a green input stays green through the neutral grade");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;s.color.exposure=1.0f;
            if(!graph.apply(s)||!graph.render(exposed,b)){std::printf("FAIL exposure render\n");return 1;}
            check(center(b).r>neutralPixel.r+20,"+1 EV brightens mid grey by a visible step");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;
            s.color.curves[0].count=3;s.color.curves[0].points[1]={0.5f,0.75f};s.color.curves[0].points[2]={1,1};
            if(!graph.apply(s)||!graph.render(curved,c)){std::printf("FAIL curve render\n");return 1;}
            check(center(c).r>neutralPixel.r+10,"point curve at 0.5 raises the coded value");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;s.color.saturation=-100.0f;
            if(!graph.apply(s)||!graph.render(desaturated,e)){std::printf("FAIL saturation render\n");return 1;}
            const auto pixel=center(e);
            check(std::abs(pixel.r-pixel.g)<=2&&std::abs(pixel.g-pixel.b)<=2,"saturation -100 collapses the frame to grey");
        }
        {
            // T4 verbs: the hue table (mixer) and the luminance table (grading).
            engine::EnhancementSettings s;s.color.enabled=true;s.color.mixerSaturation[3]=100.0f;
            sink::RgbaImage mixed;
            const bool ok=graph.apply(s)&&graph.render(grass,mixed);
            const auto pixel=center(mixed);
            check(ok&&(pixel.g-std::max(pixel.r,pixel.b))>(neutralGrass.g-std::max(neutralGrass.r,neutralGrass.b)),
                "green mixer saturation raises the green separation");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;
            s.color.grading[1]={0,100,0};
            sink::RgbaImage graded;
            const bool ok=graph.apply(s)&&graph.render(neutral,graded);
            const auto pixel=center(graded);
            check(ok&&pixel.r>pixel.b+10,"mid-tone grading wheel tints mid grey towards red");
        }
        {
            // T4 black & white mixer: the switch must actually produce a
            // monochrome frame, and the per-band row must move that band's grey
            // instead of being an inert slider.
            engine::EnhancementSettings s;s.color.enabled=true;s.color.blackWhite=true;
            sink::RgbaImage monoGreen,monoGreenLifted;
            const bool monochrome=graph.apply(s)&&graph.render(grass,monoGreen)&&!monoGreen.pixels.empty();
            check(monochrome,"the black and white mixer renders through the real graph");
            if(monochrome){
                const auto grey=center(monoGreen);
                check(grey.r==grey.g&&grey.g==grey.b,"the black and white mixer collapses the frame to grey");
                s.color.blackWhiteMix[3]=60.0f;
                const bool lifted=graph.apply(s)&&graph.render(grass,monoGreenLifted)&&!monoGreenLifted.pixels.empty();
                const auto liftedGrey=lifted?center(monoGreenLifted):Pixel{};
                check(lifted&&liftedGrey.r==liftedGrey.g&&liftedGrey.g==liftedGrey.b&&liftedGrey.r>grey.r+8,
                    std::format("the green band row lightens the green area's grey (neutral {} vs lifted {})",grey.r,liftedGrey.r));
                // Same non-colour payload as the accepted call above, so only the
                // colour block changes (a fresh default settings object carries a
                // different multiplier/backend combination the graph rejects).
                auto off=s;off.color=engine::ColorSettings{};off.color.enabled=true;
                sink::RgbaImage colour;
                const bool applied=graph.apply(off);
                const bool rendered=applied&&graph.render(grass,colour)&&!colour.pixels.empty();
                const auto back=rendered?center(colour):Pixel{};
                check(rendered&&back.g>back.r+8,
                    std::format("turning the black and white mixer off restores the colour image (applied={} r={} g={} b={})",applied,back.r,back.g,back.b));
            }
        }
        {
            // Section bypass ("分组眼睛"): stopping the 亮 group must render
            // exactly like the ungraded reference while the numbers stay stored.
            engine::EnhancementSettings lit;lit.color.enabled=true;lit.color.exposure=1.0f;lit.color.contrast=-40.0f;
            sink::RgbaImage graded,bypassed;
            // A fresh default settings object differs from this graph in a
            // non-colour field, so the reference keeps the accepted payload and
            // only resets the colour block (enabled but neutral = identity).
            auto reference=lit;reference.color=engine::ColorSettings{};reference.color.enabled=true;
            const bool referenced=graph.apply(reference)&&graph.render(neutral,graded);
            auto off=lit;off.color.groupBypassMask=1u<<0;
            const bool ignored=graph.apply(off)&&graph.render(neutral,bypassed);
            check(referenced&&ignored&&!graded.pixels.empty()&&graded.pixels==bypassed.pixels,
                "bypassing the 亮 group renders exactly like the ungraded reference");
            check(off.color.exposure==1.0f,"the bypassed group keeps its stored numbers");
        }
    }
    // 3. Output dither (plan section 4.1): a smooth ramp pushed through a
    // compressing grade must not turn into long flat plateaus at the 8-bit
    // write. The same graph is rendered with the dither off and at one LSB.
    {
        Graph plain,ditheredGraph;
        const bool started=plain.startWithDither(0.0f)&&ditheredGraph.startWithDither(1.0f/255.0f);
        check(started,"the dither probe graphs initialise");
        if(started){
            Frame rampA,rampB;
            sink::RgbaImage noDither,dithered;
            const bool rendered=rampA.makeRamp(112,128)&&rampB.makeRamp(112,128)&&
                plain.render(rampA,noDither)&&ditheredGraph.render(rampB,dithered);
            check(rendered,"the compressing ramp renders through both graphs");
            if(rendered&&!noDither.pixels.empty()&&!dithered.pixels.empty()){
                const auto longestRun=[&](const sink::RgbaImage& image){
                    int longest=0,current=0,previous=-1;
                    for(int x=8;x<kSize-8;++x){
                        const int value=image.pixels[(std::size_t(kSize/2)*image.width+std::size_t(x))*4];
                        if(value==previous)++current;else current=1;
                        previous=value;longest=std::max(longest,current);
                    }
                    return longest;
                };
                const int without=longestRun(noDither),with=longestRun(dithered);
                check(with<without,std::format("the output dither breaks the banding plateaus apart ({} -> {} px)",without,with));
                check(with<=6,std::format("dither keeps the longest identical 8-bit run short ({} px, without dither {} px)",with,without));
            }
        }
    }
    // 4. A real .cube file, imported into runtime_local/luts and resolved by name
    // when the graph is built - the same path the UI importer uses.
    {
        const engine::ColorLutStore store(veyra::runtime::localDataDirectory());
        std::error_code ec;std::filesystem::create_directories(store.folder(),ec);
        const auto source=store.folder()/L"gpu-probe-halve.cube";
        {
            std::ofstream file(source,std::ios::binary);
            file<<"TITLE \"halve\"\nLUT_3D_SIZE 2\n";
            for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r)
                file<<(r*0.5f)<<" "<<(g*0.5f)<<" "<<(b*0.5f)<<"\n";
        }
        Frame frame,reference;
        sink::RgbaImage baseline,graded;
        bool ok=frame.make(188,188,188)&&reference.make(188,188,188);
        if(ok){
            Graph plain,withLut;
            ok=plain.start(true)&&plain.render(reference,baseline);
            if(ok)ok=withLut.startWithLut(L"gpu-probe-halve.cube")&&withLut.render(frame,graded);
        }
        check(ok,"a .cube imported into runtime_local/luts resolves when the graph is built");
        if(ok)check(center(graded).r<center(baseline).r-20,"the imported halving LUT darkens the frame through the 3D sampler");
        // A PQ input space means nothing on SDR content: it must be refused with
        // a visible notice, and the frame must come out exactly as without a LUT.
        if(getenv("VEYRA_SKIP_LUT_SPACE_CASE")==nullptr){
            Graph mismatched;
            Frame third;
            sink::RgbaImage ungraded;
            bool refused=third.make(188,188,188)&&mismatched.startWithLut(L"gpu-probe-halve.cube",engine::ColorSettings::kLutInputPq)&&mismatched.render(third,ungraded);
            check(refused&&!mismatched.g.colorLutNotice().empty(),
                "a PQ LUT on SDR content is refused with a notice instead of applied silently");
            if(refused)check(std::abs(center(ungraded).r-center(baseline).r)<=1,
                "the refused LUT leaves the frame identical to the no-LUT render");
        }
        std::filesystem::remove(source,ec);
    }
    if(failures){std::printf("FAIL: colour grade GPU contract (%d checks)\n",failures);return 1;}
    std::printf("PASS: colour grade GPU contract (off/neutral identity, exposure, curve, saturation, hue)\n");
    return 0;
}
