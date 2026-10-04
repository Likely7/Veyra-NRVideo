#include "veyra/gfx/PresentSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>
#include <vector>

// Real window close / failed reopen / second close / valid reopen. No NGX,
// allocation pressure, device fault injection or artificial GPU workload.
int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc!=2)return 2;
    const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    Logger::instance().openFile((output/L"engine.log").wstring());
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;
    wc.lpszClassName=L"VeyraPresentSinkLifecycleFixture";
    if(!RegisterClassW(&wc))return 2;
    gfx::D3D12DeviceContext ctx;gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    Status status=Status::Ok;if(!ctx.initialize(device,status))return 2;
    gfx::ComPtr<ID3D12InfoQueue> info;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info))))return 2;
    info->ClearStoredMessages();
    auto makeWindow=[&]{return CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Veyra lifecycle",
        WS_POPUP,40,40,320,180,nullptr,nullptr,wc.hInstance,nullptr);};
    auto makeQueue=[&](gfx::ComPtr<ID3D12CommandQueue>& queue){
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        return SUCCEEDED(ctx.device()->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
    };
    bool pass=true;
    for(unsigned round=0;round<3&&pass;++round){
        gfx::PresentSink sink;gfx::ComPtr<ID3D12CommandQueue> first,second;
        HWND window=makeWindow();if(!window||!makeQueue(first))return 2;
        gfx::PresentSink::Desc desc;desc.targetWindow=window;desc.width=320;desc.height=180;
        pass=sink.initialize(ctx.device(),first.Get(),desc,status);
        sink.shutdown();pass=pass&&sink.hwnd()==nullptr;
        first.Reset();const HWND closed=window;DestroyWindow(window);
        if(!makeQueue(second))return 2;
        desc.targetWindow=closed;
        pass=pass&&!sink.initialize(ctx.device(),second.Get(),desc,status)&&status==Status::WindowFailure;
        sink.shutdown();sink.shutdown();pass=pass&&sink.hwnd()==nullptr;
        window=makeWindow();if(!window)return 2;desc.targetWindow=window;
        pass=pass&&sink.initialize(ctx.device(),second.Get(),desc,status);
        sink.shutdown();sink.shutdown();pass=pass&&sink.hwnd()==nullptr;
        second.Reset();DestroyWindow(window);
        std::cout<<"LIFECYCLE round="<<round<<" passed="<<pass<<std::endl;
    }
    unsigned errors=0;
    for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T size=0;if(FAILED(info->GetMessage(i,nullptr,&size)))return 2;
        std::vector<char> storage(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if(FAILED(info->GetMessage(i,message,&size)))return 2;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
            ++errors;std::cerr<<"DEBUG_ERROR "<<message->pDescription<<std::endl;
        }
    }
    pass=pass&&errors==0&&SUCCEEDED(ctx.device()->GetDeviceRemovedReason());
    info.Reset();ctx.shutdown();UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::cout<<"LIFECYCLE_RESULT passed="<<pass<<" debugErrors="<<errors<<std::endl;
    return pass?0:1;
}
