#include "NrExperimentalFixture.h"
#include "veyra/ngx/DlssSrBackend.h"
#include "veyra/FileIdentity.h"
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>
#include <iostream>

// Compatibility first: an invalid recorded list is discarded before execution.
// Author-created fixed pixels, actual product NR/SR adapter and NGX contracts.
int wmain(int argc,wchar_t** argv){
    using namespace nrperf;if(argc!=5)return 2;
    const bool compute=std::wstring_view(argv[3])==L"compute";
    const bool sr=std::wstring_view(argv[4])==L"sr";
    if((!compute&&std::wstring_view(argv[3])!=L"direct")||(!sr&&std::wstring_view(argv[4])!=L"nr"))return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    Session session;if(!session.init(argv[1]))return 2;Status status;
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=compute?D3D12_COMMAND_LIST_TYPE_COMPUTE:D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(!hrOK(session.device.device()->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Queue.Create")||
       !hrOK(session.device.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence.Create"))return 2;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 2;
    gfx::CommandSlotRing ring;UINT64 frequency=0;
    if(!ring.initialize(session.device.device(),queue.Get(),fence.Get(),event,4,status)||
       !hrOK(queue->GetTimestampFrequency(&frequency),"Queue.Frequency"))return 2;
    bool created=false,evaluated=false,validOutput=false;
    std::ofstream frames(out/L"frames.csv");frames<<"frame,width,height,sha256\n";
    {
        Feature feature(session);feature.timestampFrequencyOverride=frequency;
        ngx::DlssSrBackend srFeature;
        if(sr){
            uint32_t slot;auto* list=ring.acquireNext(slot,status);
            if(list){ngx::DlssSrBackend::CreateDesc desc{1920,1080,2560,1440,1,false};
                created=srFeature.create(session.core,list,feature.params,desc,status);
                created=created?(ring.submitAndSignal(slot)&&ring.waitIdle()):(ring.discardRecording()&&false);}
        }else created=feature.create(ring,1920,1080);
        if(created&&feature.textures(ring,1920,1080,1920,1080)){
            if(sr){
                feature.color=pipeline::makeTexture(session.device.device(),1920,1080,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
                feature.output=pipeline::makeTexture(session.device.device(),2560,1440,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
                std::vector<uint8_t> pixels(size_t(1920)*1080*8);auto* half=reinterpret_cast<uint16_t*>(pixels.data());
                for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x)for(unsigned c=0;c<4;++c)
                    half[(size_t(y)*1920+x)*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(c==3?1.f:.15f+float((x*3+y*7+c*11)%251)/400.f);
                if(!feature.color||!feature.output||!feature.upload(ring,feature.color.Get(),pixels))return 2;
            }
            evaluated=bool(feature.output);
            for(unsigned frame=0;frame<30&&evaluated;++frame){
                if(sr){uint32_t slot;auto* list=ring.acquireNext(slot,status);if(!list){evaluated=false;break;}
                    feature.states.transition(list,feature.output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    ngx::DlssSrBackend::EvalDesc desc{feature.color.Get(),feature.output.Get(),feature.depth.Get(),feature.motion.Get(),frame==0,0,0,0};
                    evaluated=srFeature.evaluate(list,feature.params,desc,status);
                    if(evaluated){feature.states.uavBarrier(list,feature.output.Get());feature.states.transition(list,feature.output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        evaluated=ring.submitAndSignal(slot)&&ring.waitIdle();}
                    else ring.discardRecording();
                }else evaluated=feature.evaluate(ring,1920,1080,frame==0);
                if(evaluated){const auto image=feature.read(ring);validOutput=image.size()==size_t(sr?2560:1920)*(sr?1440:1080)*(sr?8:4);
                    evaluated=validOutput;if(validOutput){frames<<frame<<','<<(sr?2560:1920)<<','<<(sr?1440:1080)<<','<<sha256Hex(image.data(),image.size())<<'\n';frames.flush();
                        if(frame<2||frame==29){std::ofstream file(out/(L"frame-"+std::to_wstring(frame)+(sr?L".rgba16f":L".rgba")),std::ios::binary);
                            file.write(reinterpret_cast<const char*>(image.data()),std::streamsize(image.size()));}}}
            }
        }
        ring.drainQueue();if(sr&&!srFeature.release())evaluated=false;
    }
    ring.drainQueue();ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(!hrOK(session.device.device()->QueryInterface(IID_PPV_ARGS(&debug)),"Debug.Query"))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);
        std::vector<unsigned char> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,m,&n)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<m->pDescription<<std::endl;}}
    const auto removed=session.device.device()->GetDeviceRemovedReason();const bool compatible=created&&evaluated&&validOutput&&errors==0&&SUCCEEDED(removed);
    std::cout<<"COMPUTE_RESULT created="<<created<<" evaluated="<<evaluated<<" compatible="<<compatible<<" debugErrors="<<errors
        <<" deviceRemoved="<<unsigned(removed)<<" queueType="<<unsigned(qd.Type)<<" frequency="<<frequency<<std::endl;
    debug.Reset();ring.shutdown();CloseHandle(event);return compatible?0:1;
}
