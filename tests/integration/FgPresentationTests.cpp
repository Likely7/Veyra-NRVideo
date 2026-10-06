#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/sink/ImageExportSink.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <thread>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if((argc<2||argc>4)||FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    const bool displaySync=argc==4&&std::wstring_view(argv[3])==L"display-sync";
    const bool temporal=argc==4&&!displaySync;
    const bool exact=temporal&&std::wstring_view(argv[3])==L"temporal-exact";
    if(temporal&&!exact&&std::wstring_view(argv[3])!=L"temporal")return 2;
    const auto runtimePath=argc>=3?std::filesystem::absolute(argv[2]):std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia";
    std::filesystem::create_directories(argv[1]);
    HWND window=CreateWindowExW(0,L"STATIC",L"FG presentation regression",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status=Status::Ok;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    bool ok=window&&ctx.initialize(device,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status);
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    if(ok)ok=SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)));
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    AVFrame* frame=av_frame_alloc();
    if(frame){frame->format=AV_PIX_FMT_RGBA;frame->width=640;frame->height=360;frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;}
    ok=ok&&frame&&av_frame_get_buffer(frame,32)>=0;
    unsigned cycle=0;
    if(displaySync){
        // Real windowed and borderless-fullscreen swapchains, including both
        // latency-queue settings. The runner checks actual Present flags in
        // addition to the pure settings contract; panel scanout is not measured.
        MONITORINFO monitor{sizeof(MONITORINFO)};
        ok=ok&&GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
        for(bool full:{false,true})for(bool lowQueue:{false,true})for(auto mode:{engine::DisplaySync::Automatic,engine::DisplaySync::Vsync,engine::DisplaySync::Tearing}){
            if(!ok)break;
            const int w=full?monitor.rcMonitor.right-monitor.rcMonitor.left:640;
            const int h=full?monitor.rcMonitor.bottom-monitor.rcMonitor.top:360;
            SetWindowPos(window,nullptr,full?monitor.rcMonitor.left:0,full?monitor.rcMonitor.top:0,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(window,SW_SHOWNOACTIVATE);
            pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;desc.sourceHeight=desc.workHeight=360;
            desc.enableNr=desc.enableSr=desc.enableFg=false;desc.rgbInput=true;desc.runtimeAbsPath=runtimePath.wstring();
            ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
            engine::PresentationSettings requested;requested.fullscreen=full;requested.enabled=lowQueue;requested.display=mode;
            std::wstring text;const auto applied=presenter.configurePresentation(ctx,requested,false,text);
            ok=ok&&applied.display==mode&&applied.enabled==lowQueue;
            const auto beforeSubmitted=presenter.submittedCount();
            std::cout<<"DISPLAY_SYNC_BEGIN full="<<full<<" lowQueue="<<lowQueue<<" mode="<<unsigned(mode)<<std::endl;
            for(unsigned f=0;ok&&f<6;++f){
                for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;p[0]=60+f*10;p[1]=110;p[2]=180;p[3]=255;}
                pipeline::EnhanceGraph::FrameOutputs out;
                ok=graph.process(frame,f*33.333,f==0||f==3,out,f+1)&&presenter.present(ctx,ring,graph,out.videoSlot,false,false);
                if(f==1&&ok){
                    sink::RgbaImage actual,expected;
                    ok=presenter.readPresentedFrameForTest(ctx,ring,actual)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),expected);
                    ok=ok&&actual.width==unsigned(w)&&actual.height==unsigned(h)&&!expected.pixels.empty();
                    // The fullscreen monitor may have a different aspect
                    // ratio; inspect the image centre, excluding letterboxing.
                    if(ok)for(unsigned y=actual.height*3/8;y<actual.height*5/8;++y)for(unsigned x=actual.width*3/8;x<actual.width*5/8;++x)
                        for(unsigned c=0;c<3;++c)if(std::abs(int(actual.pixels[(size_t(y)*actual.width+x)*4+c])-int(expected.pixels[c]))>2)ok=false;
                }
            }
            ok=ok&&presenter.submittedCount()==beforeSubmitted+6;
            // Native presentation uses the producer ring. Match the engine's
            // drain-before-rebuild contract before releasing presenter passes.
            ok=ring.drainQueue()&&ok;graph.shutdown();presenter.close();
            std::cout<<"DISPLAY_SYNC_END full="<<full<<" lowQueue="<<lowQueue<<" mode="<<unsigned(mode)<<" pass="<<ok<<std::endl;
        }
    }
    for(unsigned multiplier:{4u,6u,4u}){
        if(temporal||displaySync)break;
        if(!ok)break;
        ++cycle;
        pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;desc.sourceHeight=desc.workHeight=360;
        desc.enableNr=desc.enableSr=false;desc.enableFg=desc.rgbInput=true;desc.fgMultiplier=multiplier;
        desc.runtimeAbsPath=runtimePath.wstring();
        ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
        if(ok){engine::PresentationSettings requested;std::wstring text;presenter.configurePresentation(ctx,requested,true,text);ShowWindow(window,SW_SHOWNOACTIVATE);}
        if(ok&&cycle==1){
            // A producer allocator stall must not be reported as private
            // presentation-ring service. Release the GPU wait from the CPU.
            Microsoft::WRL::ComPtr<ID3D12Fence> blocker;
            ok=SUCCEEDED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&blocker)));
            unsigned slot=0;
            if(ok)ok=ring.acquireNext(slot,status)!=nullptr;
            if(ok)ok=SUCCEEDED(ctx.directQueue()->Wait(blocker.Get(),1));
            if(ok){
                const auto sharedBefore=ring.cpuWaitMilliseconds(),presentBefore=presenter.cpuWaitMilliseconds(ring);
                HRESULT released=E_FAIL;
                std::jthread release([&]{Sleep(40);released=blocker->Signal(1);});
                ok=ring.submitAndSignal(slot)&&ring.acquire(slot,status)!=nullptr;
                release.join();ok=SUCCEEDED(released)&&ring.discardRecording()&&ok;
                const auto sharedWait=ring.cpuWaitMilliseconds()-sharedBefore;
                const auto presentWait=presenter.cpuWaitMilliseconds(ring)-presentBefore;
                ok=ok&&sharedWait>=20&&presentWait==0;
                std::cout<<"WAIT_ACCOUNTING sharedMs="<<sharedWait<<" privateMs="<<presentWait<<" pass="<<ok<<std::endl;
            }
        }
        Microsoft::WRL::ComPtr<ID3D12Fence> consumer;
        if(ok)ok=SUCCEEDED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumer)));
        if(ok){
            graph.presentationSubmitted(0,consumer.Get(),1);
            ok=!graph.nextFrameSlotAvailable();
            ok=SUCCEEDED(consumer->Signal(1))&&ok&&graph.nextFrameSlotAvailable();
        }
        unsigned generated=0;int maxError=0;
        for(unsigned i=0;ok&&i<40;++i){
            if(i==8||i==24){
                SetWindowPos(window,nullptr,0,0,i==8?320:640,i==8?180:360,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                Sleep(110);
            }
            for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                p[0]=p[1]=p[2]=uint8_t(24+(((x+i*8)/32+y/32)%2)*180);p[3]=255;
            }
            pipeline::EnhanceGraph::FrameOutputs out;
            ok=graph.process(frame,i*20.0,i==0||i==4,out,i+1)&&ring.waitIdle()&&graph.resolveGeneration(out);
            for(unsigned j=0;ok&&j<out.batch.count;++j){
                const auto& item=out.batch.frames[j];const bool interpolated=item.kind==pipeline::FrameKind::Generated;
                if(interpolated&&item.validity!=pipeline::GenerationValidity::Valid){ok=false;break;}
                ok=presenter.present(ctx,ring,graph,item.lease->slot,interpolated,false,0,false,.5f,item.identity);
                sink::RgbaImage shown;
                // First verify exact pixels; then exercise queued GPU reads,
                // resize and parity reuse without a readback draining each blit.
                if(i<8){
                    sink::RgbaImage expected;
                    ok=ok&&presenter.readPresentedFrameForTest(ctx,ring,shown)&&sink::readRgba8(ctx,ring,item.lease->texture.Get(),expected);
                    ok=ok&&shown.width==expected.width&&shown.height==expected.height&&shown.pixels.size()==expected.pixels.size();
                    if(ok){
                        for(size_t p=0;p<shown.pixels.size();++p)if(p%4!=3)maxError=std::max(maxError,std::abs(int(shown.pixels[p])-int(expected.pixels[p])));
                        ok=maxError<=2;
                    }
                }
                if(interpolated){
                    ++generated;
                    if(generated==1&&ok)ok=sink::saveImage((std::filesystem::path(argv[1])/(std::to_wstring(cycle)+L"-"+std::to_wstring(multiplier)+L"x-generated.png")).wstring(),shown);
                }
            }
        }
        ok=ok&&generated==38*(multiplier-1);
        unsigned pendingOriginals=0;
        for(unsigned i=40;ok&&i<56;++i){
            // Expired/suppressed generated frames can be bypassed before their
            // fences complete. The original must still wait for ALL FG reads
            // on the producer queue before its presentation transition.
            for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                p[0]=uint8_t(24+i*3);p[1]=uint8_t(32+x/4);p[2]=uint8_t(48+y/3);p[3]=255;
            }
            pipeline::EnhanceGraph::FrameOutputs out;
            ok=graph.process(frame,i*20.0,i==48,out,i+1);
            if(!ok)break;
            const auto& original=out.batch.frames[out.batch.count-1];
            if(!out.gpuComplete())++pendingOriginals;
            ok=original.kind==pipeline::FrameKind::Real&&
                presenter.present(ctx,ring,graph,original.lease->slot,false,false,0,false,.5f,original.identity);
            sink::RgbaImage shown,expected;
            ok=ok&&presenter.readPresentedFrameForTest(ctx,ring,shown)&&
                sink::readRgba8(ctx,ring,original.lease->texture.Get(),expected);
            ok=ok&&shown.width==expected.width&&shown.height==expected.height&&shown.pixels.size()==expected.pixels.size();
            if(ok){
                for(size_t p=0;p<shown.pixels.size();++p)if(p%4!=3)
                    maxError=std::max(maxError,std::abs(int(shown.pixels[p])-int(expected.pixels[p])));
                ok=maxError<=2;
            }
            ok=ok&&graph.resolveGeneration(out);
        }
        ok=ok&&pendingOriginals>0;
        std::cout<<"EARLY_ORIGINAL multiplier="<<multiplier<<" pending="<<pendingOriginals
                 <<" maxPixelError="<<maxError<<" pass="<<ok<<std::endl;
        // Also exercise producer teardown first: it must cover consumer GPU reads.
        graph.shutdown();presenter.close();
        std::cout<<"PRESENTATION multiplier="<<multiplier<<" generated="<<generated<<" maxPixelError="<<maxError<<" pass="<<ok<<std::endl;
    }
    // Verify interpolation time against an independent moving-object oracle,
    // including the first complete group after an explicit input discontinuity.
    unsigned temporalFailures=0;
    for(unsigned multiplier:{2u,4u,6u}){
        if(!temporal)break;
        if(!ok)break;
        pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;desc.sourceHeight=desc.workHeight=360;
        desc.enableNr=desc.enableSr=false;desc.enableFg=desc.rgbInput=true;desc.fgMultiplier=multiplier;
        desc.runtimeAbsPath=runtimePath.wstring();
        pipeline::ComPtr<ID3D12Resource> motion,upload;
        if(exact){
            motion=pipeline::makeTexture(ctx.device(),640,360,DXGI_FORMAT_R16G16_FLOAT,false);
            upload=pipeline::makeUploadBuffer(ctx.device(),640ull*360*4);
            ok=motion&&upload;desc.fgMotionProbe=motion.Get();
        }
        if(!ok)break;
        ok=graph.initialize(desc)&&graph.createViews();
        unsigned checked=0,violations=0;double worstPosition=0,worstGray=0;
        for(unsigned i=0;ok&&i<20;++i){
            const unsigned source=i+(i>=10?3:0);
            const unsigned left=80+source*12;
            if(exact){
                void* data=nullptr;
                if(FAILED(upload->Map(0,nullptr,&data))){ok=false;break;}
                for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
                    auto* p=static_cast<uint16_t*>(data)+(size_t(y)*640+x)*2;
                    p[0]=i&&x>=left&&x<left+80&&y>=100&&y<260?0xca00:0; // binary16 -12
                    p[1]=0;
                }
                upload->Unmap(0,nullptr);unsigned slot=0;auto* list=ring.acquireNext(slot,status);
                if(!list){ok=false;break;}
                D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition={motion.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};
                list->ResourceBarrier(1,&barrier);
                D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=motion.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16_FLOAT,640,360,1,640*4};
                list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
                barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                list->ResourceBarrier(1,&barrier);
                if(!ring.submitAndSignal(slot)||!ring.waitIdle()){ok=false;break;}
            }
            for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
                auto* pixel=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                const bool object=x>=left&&x<left+80&&y>=100&&y<260;
                pixel[0]=pixel[1]=pixel[2]=uint8_t(y<48?96:object?224:24);pixel[3]=255;
            }
            pipeline::EnhanceGraph::FrameOutputs out;
            ok=graph.process(frame,source*(1000.0/60),i==0||i==10,out,source+1)&&ring.waitIdle()&&graph.resolveGeneration(out);
            double previousPosition=-1;
            for(unsigned j=0;ok&&j<out.batch.count;++j){
                const auto& item=out.batch.frames[j];
                const bool generated=item.kind==pipeline::FrameKind::Generated;
                if(generated&&item.validity!=pipeline::GenerationValidity::Valid){ok=false;break;}
                sink::RgbaImage pixels;
                if(!sink::readRgba8(ctx,ring,item.lease->texture.Get(),pixels)){ok=false;break;}
                double sum=0,weighted=0,gray=0;
                for(unsigned y=120;y<240;++y)for(unsigned x=20;x<620;++x){
                    const double weight=std::max(0.,double(pixels.pixels[(size_t(y)*640+x)*4])-120.);
                    sum+=weight;weighted+=weight*x;
                }
                for(unsigned y=8;y<40;++y)for(unsigned x=16;x<624;++x)
                    gray+=pixels.pixels[(size_t(y)*640+x)*4];
                gray/=32*608;
                const double position=sum>0?weighted/sum:-1000;
                const double expected=80+(generated?source-1.+double(item.subframe)/multiplier:source)*12+39.5;
                worstPosition=std::max(worstPosition,std::abs(position-expected));
                worstGray=std::max(worstGray,std::abs(gray-96));
                const bool valid=std::abs(position-expected)<=2.5&&std::abs(gray-96)<=2&&position>previousPosition;
                std::cout<<"TEMPORAL multiplier="<<multiplier<<" source="<<source<<" sub="<<(generated?item.subframe:multiplier)
                         <<" position="<<position<<" expected="<<expected<<" gray="<<gray<<" pass="<<valid<<std::endl;
                if(!valid)++violations;
                previousPosition=position;if(generated)++checked;
                if(generated&&i==2&&!sink::saveImage((std::filesystem::path(argv[1])/(std::to_wstring(multiplier)+L"x-sub"+std::to_wstring(item.subframe)+L".png")).wstring(),pixels))ok=false;
            }
            if((i==0||i==10)&&out.batch.count!=1)ok=false;
        }
        ok=ok&&checked==18*(multiplier-1);
        temporalFailures+=violations;
        std::cout<<"TEMPORAL_SUMMARY multiplier="<<multiplier<<" exactMotion="<<exact<<" generated="<<checked<<" maxPositionError="<<worstPosition
                 <<" maxGrayError="<<worstGray<<" violations="<<violations<<" pass="<<(ok&&!violations)<<std::endl;
        graph.shutdown();
    }
    graph.shutdown();presenter.close();ring.drainQueue();
    unsigned errors=0;
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> data(bytes);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(SUCCEEDED(info->GetMessage(i,message,&bytes))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<'\n';}
    }
    std::cout<<"D3D12 errors="<<errors<<std::endl;
    av_frame_free(&frame);ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return !ok||errors?1:temporalFailures?3:0;
}
