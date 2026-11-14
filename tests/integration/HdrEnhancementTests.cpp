#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/RuntimePaths.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/source/MediaFileSource.h"
#include <filesystem>
#include <DirectXPackedVector.h>
#include <iostream>
#include <bit>
#include <cmath>
#include <array>
#include <wincodec.h>
using namespace veyra;
using namespace veyra::pipeline;
// Readback is confined to this test; production HDR remains GPU-resident.
static bool read(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,ID3D12Resource* tex,std::vector<float>& values){
    const auto d=tex->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
    ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))return false;
    Status status;unsigned slot;auto* list=ring.acquireNext(slot,status);if(!list)return false;
    StateTracker states;states.transition(list,tex,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=tex;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,tex,D3D12_RESOURCE_STATE_COMMON);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    void* p;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(buffer->Map(0,&range,&p)))return false;
    values.resize(size_t(d.Width)*d.Height*3);
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<3;++c){
        const auto* row=static_cast<const uint8_t*>(p)+fp.Offset+y*fp.Footprint.RowPitch;
        float value;
        if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)value=DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const uint16_t*>(row)[x*4+c]);
        else {value=float((reinterpret_cast<const uint32_t*>(row)[x]>>(10*c))&1023)/1023;const double t=pow(value,32.0/2523);value=float(125*pow(std::max(t-3424.0/4096,0.0)/(2413.0/128-2392.0/128*t),16384.0/2610));}
        values[(size_t(y)*d.Width+x)*3+c]=value;
    }
    D3D12_RANGE none{};buffer->Unmap(0,&none);return true;
}
// Independent comparison against GPU readback; encoder self-roundtrip alone
// would miss a half-width copy or a wrong pre-encode transfer conversion.
static bool verifyJxr(const std::filesystem::path& path,const std::vector<float>& expected,unsigned width,unsigned height,bool encoded2020){
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(decoder->GetFrame(0,&frame)))return false;
    WICPixelFormatGUID format;UINT w=0,h=0;
    if(FAILED(frame->GetPixelFormat(&format))||format!=GUID_WICPixelFormat64bppRGBAHalf||FAILED(frame->GetSize(&w,&h))||w!=width||h!=height)return false;
    std::vector<uint16_t> data(size_t(w)*h*4);if(FAILED(frame->CopyPixels(nullptr,w*8,UINT(data.size()*2),reinterpret_cast<BYTE*>(data.data()))))return false;
    double error=0;const double matrix[3][3]={{1.660491,-.587641,-.072850},{-.124550,1.132900,-.008349},{-.018151,-.100579,1.118730}};
    for(size_t i=0;i<size_t(w)*h;++i)for(unsigned c=0;c<3;++c){const double ref=encoded2020?matrix[c][0]*expected[i*3]+matrix[c][1]*expected[i*3+1]+matrix[c][2]*expected[i*3+2]:expected[i*3+c];
        error=std::max(error,std::abs(DirectX::PackedVector::XMConvertHalfToFloat(data[i*4+c])-ref));
    }
    std::cout<<"HDR_JXR_GPU_COMPARE pixels="<<size_t(w)*h<<" maxError="<<error<<" encoded2020="<<encoded2020<<std::endl;
    return error<=(encoded2020?.02:0);
}
static bool shaderIdentity(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring){
    auto original=makeTexture(ctx.device(),64,16,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto proxy=makeTexture(ctx.device(),64,16,DXGI_FORMAT_R8G8B8A8_UNORM,true);
    auto final=makeTexture(ctx.device(),64,16,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto upload=makeUploadBuffer(ctx.device(),64*16*8);void* data;if(!original||!proxy||!final||!upload||FAILED(upload->Map(0,nullptr,&data)))return false;
    constexpr float colors[][3]={{0,0,0},{.001f,.001f,.001f},{2.5375f,2.5375f,2.5375f},{12.5f,12.5f,12.5f},{50,50,50},{16.60491f,-1.2455f,-.18151f},{-5.87641f,11.329f,-1.00579f},{-.7285f,-.08349f,11.1873f}};
    for(unsigned i=0;i<64*16;++i)for(unsigned c=0;c<4;++c)static_cast<uint16_t*>(data)[i*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(c==3?1:colors[(i%64)/8][c]);
    upload->Unmap(0,nullptr);Status status;unsigned slot;auto* list=ring.acquireNext(slot,status);StateTracker states;
    states.transition(list,original.Get(),D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,64,16,1,512};dst.pResource=original.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    ComputePass enc,dec;std::vector<uint8_t> code;
    if(!enc.loadShader("ParityEncode.dxil",code)||!enc.create(ctx.device(),code,2,1,1)||!dec.loadShader("ParityDecode.dxil",code)||!dec.create(ctx.device(),code,4))return false;
    makeSrv(ctx.device(),original.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(enc,0));makeUav(ctx.device(),proxy.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,cpuHandleOf(enc,1));
    makeSrv(ctx.device(),original.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(dec,0));for(unsigned i=1;i<3;++i)makeSrv(ctx.device(),proxy.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,cpuHandleOf(dec,i));makeUav(ctx.device(),final.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(dec,3));
    float constants[8]={1,1,1,1,std::bit_cast<float>(64u),std::bit_cast<float>(16u),0,0};
    states.transition(list,original.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);states.transition(list,proxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);enc.bind(list,constants,gpuHandleOf(enc,0).ptr,gpuHandleOf(enc,1).ptr);list->Dispatch(4,1,1);states.uavBarrier(list,proxy.Get());states.transition(list,proxy.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);states.transition(list,final.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);dec.bind(list,constants,gpuHandleOf(dec,0).ptr,gpuHandleOf(dec,3).ptr);list->Dispatch(4,1,1);states.uavBarrier(list,final.Get());states.transition(list,final.Get(),D3D12_RESOURCE_STATE_COMMON);states.transition(list,original.Get(),D3D12_RESOURCE_STATE_COMMON);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    std::vector<float>a,b;bool ok=read(ctx,ring,original.Get(),a)&&read(ctx,ring,final.Get(),b)&&a==b;
    const auto imagePath=std::filesystem::path(L"out")/(L"hdr-identity-"+std::to_wstring(GetTickCount64())+L".jxr");
    ok=ok&&sink::saveHdrScreenshot(imagePath.wstring(),ctx,ring,original.Get())&&verifyJxr(imagePath,a,64,16,false);
    std::cout<<"HDR_SHADER_IDENTITY pixels=1024 black_nearblack_white_1000_4000_nits_widegamut="<<ok<<std::endl;return ok;
}
int wmain(int argc,wchar_t** argv){
    const unsigned mode=argc>1?unsigned(_wtoi(argv[1])):0;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc dd;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    if(!shaderIdentity(ctx,ring))return 3;if(!mode)return 0;
    const bool anr=mode>=7&&mode<=11;
    const bool fileInput=argc>3,hardware=argc>4&&_wtoi(argv[4])!=0;
    source::MediaFileSource source;
    if(fileInput){source::SourceOpenDesc od;od.path=argv[3];od.preferHardwareDecode=hardware;od.d3d12Device=ctx.device();od.d3d12Queue=ctx.directQueue();
        if(!source.open(od)||!source.info().color.isHdrPath()||source.info().width!=1920||source.info().height!=1080)return 9;
    }
    EnhanceGraph graph(ctx,ring);EnhanceGraphDesc gd;gd.sourceWidth=1920;gd.sourceHeight=1080;gd.workWidth=mode>1?2560:1920;gd.workHeight=mode>1?1440:1080;gd.nrWidth=1920;gd.nrHeight=1080;
    gd.hdrInput=gd.hdrOutput=true;gd.enableNr=true;gd.enableSr=mode>1;gd.enableFg=mode>1;gd.videoSrQuality=(mode==3||mode==4)?1:0;gd.nrBeforeSr=mode==4||mode==6;gd.enableNvofStandalone=true;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    if(anr){gd.frameGenerationBackend=engine::FrameGenerationBackend::Anr;gd.fgMultiplier=unsigned(mode-5);}
    if(argc>2)gd.nrRuntime=engine::NrRuntime(_wtoi(argv[2]));
    if(mode==5)gd.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    if(!graph.initialize(gd))return 4;
    HWND window=nullptr;engine::VideoPresenter presenter;
    if(mode==5){window=CreateWindowExW(0,L"STATIC",L"HDR XeSS test",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window||!presenter.open(ctx,window,graph)||!presenter.xessActive())return 5;}
    if(!graph.createViews())return 6;
    AVFrame* f=av_frame_alloc();f->width=1920;f->height=1080;f->format=AV_PIX_FMT_P010;f->color_range=AVCOL_RANGE_MPEG;f->colorspace=AVCOL_SPC_BT2020_NCL;f->color_primaries=AVCOL_PRI_BT2020;f->color_trc=AVCOL_TRC_SMPTE2084;
    if(av_frame_get_buffer(f,32)<0)return 7;
    for(int y=0;y<540;++y)std::fill_n(reinterpret_cast<uint16_t*>(f->data[1]+y*f->linesize[1]),1920,uint16_t(512<<6));
    unsigned generated=0;bool ok=true;double highlight=0;
    for(unsigned frame=0;frame<20&&ok;++frame){
        for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){const double v=x>1440?.7518270962:x>960?.5806888810:.15+.35*(.5+.5*sin((x+frame*4)*.05)*sin(y*.04));reinterpret_cast<uint16_t*>(f->data[0]+y*f->linesize[0])[x]=uint16_t(std::lround(64+876*v))<<6;}
        const AVFrame* input=f;FramePacket packet;
        if(fileInput){if(source.read(packet,&input)!=source::SourceReadStatus::Frame||(input->format==AV_PIX_FMT_D3D12)!=hardware){ok=false;break;}}
        EnhanceGraph::FrameOutputs out;ok=graph.process(input,frame*1000.0/60,frame==0||frame==10,out,frame+1,fileInput?&packet.colorInfo:nullptr);if(!ok)break;
        std::vector<float> pixels;ok=read(ctx,ring,graph.videoFrameResource(out.videoSlot),pixels);
        if(ok){highlight=pixels[(size_t(gd.workHeight/2)*gd.workWidth+gd.workWidth*7/8)*3];ok=highlight>11.5&&highlight<13.5&&std::all_of(pixels.begin(),pixels.end(),[](float v){return std::isfinite(v)&&std::abs(v)<150;});}
        if(frame==19&&ok){const auto target=std::filesystem::path(L"out")/(L"hdr-test-"+std::to_wstring(GetTickCount64())+L".jxr");ok=sink::saveHdrScreenshot(target.wstring(),ctx,ring,graph.videoFrameResource(out.videoSlot))&&verifyJxr(target,pixels,gd.workWidth,gd.workHeight,graph.hdr10Output());}
        if(mode==5&&ok){ok=presenter.present(ctx,ring,graph,out.videoSlot,false,true,0,false,.5f,out.batch.identity);Sleep(17);}
        if(mode>1&&mode!=5&&ok){ok=ring.waitIdle()&&graph.resolveGeneration(out);if(ok&&out.hasGenerated){for(unsigned j=0;j<out.batch.count&&ok;++j){const auto& item=out.batch.frames[j];if(item.kind!=FrameKind::Generated||item.validity!=GenerationValidity::Valid)continue;std::vector<float> middle;ok=read(ctx,ring,graph.generatedFrameResource(item.lease->slot),middle)&&std::all_of(middle.begin(),middle.end(),[](float v){return std::isfinite(v)&&std::abs(v)<150;});if(ok){const auto h=middle[(size_t(gd.workHeight/2)*gd.workWidth+gd.workWidth*7/8)*3];ok=h>11.5&&h<13.5;}++generated;}}}
    }
    if(mode==5)generated=unsigned(presenter.xessGeneratedCount());
    ok=ok&&graph.metrics().nrEvaluateCount==20&&(mode==1||generated>0);
    if(anr)ok=ok&&generated==18*(gd.fgMultiplier-1);
    std::cout<<"HDR_ENHANCEMENT file="<<fileInput<<" hardware="<<hardware<<" mode="<<mode<<" nr="<<graph.metrics().nrEvaluateCount<<" sr="<<graph.metrics().srEvaluateCount<<" generated="<<generated<<" highlight_nits="<<highlight*80<<" pass="<<ok<<std::endl;
    ring.drainQueue();presenter.close();if(window)DestroyWindow(window);graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:8;
}
