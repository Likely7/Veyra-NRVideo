// WGC lifecycle adapted from robmikh/Win32CaptureSample SimpleCapture.cpp,
// commit 49fefe79fd9b11025f0b5eb91783a98888516070, MIT, (c) 2019 Robert Mikhayelyan.
// Veyra changes: polling latest frames, leased shared textures/fences, crop,
// timestamp contract, DXGI compatibility ingress and shared enhancement graph.
#include "veyra/source/ScreenCaptureSource.h"
#include "veyra/Log.h"
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <format>
#include <sstream>
#include <mutex>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
}

namespace veyra::source {
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
namespace {
int64_t host100ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;}
int64_t qpc100ns(){LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return int64_t(static_cast<long double>(q.QuadPart)*10000000/f.QuadPart);}
void check(HRESULT hr,const char* op){if(FAILED(hr)){log::error("screen",std::format("{} hr=0x{:08X}",op,uint32_t(hr)));winrt::check_hresult(hr);}}
struct NativeHandle {HANDLE value=nullptr;~NativeHandle(){if(value)CloseHandle(value);}};
std::mutex exclusionMutex;
unsigned exclusionUsers=0;
std::vector<std::pair<HWND,DWORD>> excludedWindows;
void excludeOwnWindows(){
    std::lock_guard lock(exclusionMutex);
    if(exclusionUsers++!=0)return;
    EnumWindows([](HWND w,LPARAM)->BOOL{
        DWORD pid=0;GetWindowThreadProcessId(w,&pid);
        if(pid!=GetCurrentProcessId()||!IsWindowVisible(w))return TRUE;
        DWORD previous=0;
        if(GetWindowDisplayAffinity(w,&previous)&&SetWindowDisplayAffinity(w,WDA_EXCLUDEFROMCAPTURE))excludedWindows.emplace_back(w,previous);
        else log::warn("screen",std::format("window exclusion failed hwnd={} error={}",uint64_t(w),GetLastError()));
        return TRUE;
    },0);
}
void restoreOwnWindows(){
    std::lock_guard lock(exclusionMutex);
    if(!exclusionUsers||--exclusionUsers)return;
    for(auto [window,affinity]:excludedWindows)if(IsWindow(window)&&!SetWindowDisplayAffinity(window,affinity))log::warn("screen","window exclusion restore failed");
    excludedWindows.clear();
}
struct Surface {
    ComPtr<ID3D11Texture2D> producer;
    ComPtr<ID3D12Resource> consumer;
    ComPtr<ID3D12Fence> fence;
    unsigned width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
};
DXGI_RATIONAL monitorRate(HMONITOR monitor){
    MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);
    if(!GetMonitorInfoW(monitor,&mi))return {};
    // DisplayConfig retains fractional modes such as 60000/1001 Hz.
    for(unsigned attempt=0;attempt<3;++attempt){
        UINT32 pathCount=0,modeCount=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS)break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const auto result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER)continue;
        if(result!=ERROR_SUCCESS)break;
        for(UINT32 i=0;i<pathCount;++i){
            const auto& path=paths[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);
            name.header.adapterId=path.sourceInfo.adapterId;name.header.id=path.sourceInfo.id;
            if(DisplayConfigGetDeviceInfo(&name.header)!=ERROR_SUCCESS||wcscmp(name.viewGdiDeviceName,mi.szDevice))continue;
            const auto r=path.targetInfo.refreshRate;
            if(r.Denominator&&r.Numerator<=INT_MAX&&r.Denominator<=INT_MAX&&double(r.Numerator)/r.Denominator>1&&double(r.Numerator)/r.Denominator<=1000)
                return {r.Numerator,r.Denominator};
        }
        break;
    }
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(EnumDisplaySettingsW(mi.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1&&mode.dmDisplayFrequency<=1000)return {mode.dmDisplayFrequency,1};
    return {};
}
bool hdrMonitor(HMONITOR monitor){
    ComPtr<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
    for(UINT a=0;;++a){ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        for(UINT o=0;;++o){ComPtr<IDXGIOutput> output;if(adapter->EnumOutputs(o,&output)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_OUTPUT_DESC d{};if(FAILED(output->GetDesc(&d))||d.Monitor!=monitor)continue;
            ComPtr<IDXGIOutput6> six;DXGI_OUTPUT_DESC1 one{};
            return SUCCEEDED(output.As(&six))&&SUCCEEDED(six->GetDesc1(&one))&&one.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
    }return false;
}
}
std::wstring ScreenCaptureOptions::uri()const{
    return std::format(L"screen:{}:{}:{}:{}:{}:{}:{}:{}:{}",unsigned(kind),target,unsigned(method),fps,unsigned(cursor),left,top,right,bottom);
}
bool ScreenCaptureOptions::parse(const std::wstring& uri,ScreenCaptureOptions& out){
    if(!uri.starts_with(L"screen:"))return false;
    auto body=uri.substr(7);std::replace(body.begin(),body.end(),L':',L' ');
    std::wistringstream in(body);unsigned kind=0,method=0,cursor=0;ScreenCaptureOptions v;
    if(!(in>>kind>>v.target>>method>>v.fps>>cursor>>v.left>>v.top>>v.right>>v.bottom))return false;
    in>>std::ws;if(!in.eof()||kind>1||method>1||cursor>1||!v.target||v.fps>240||
        v.left>16384||v.top>16384||v.right>16384||v.bottom>16384||(!kind&&method))return false;
    v.kind=ScreenTargetKind(kind);v.method=ScreenCaptureMethod(method);v.cursor=cursor!=0;out=v;return true;
}
std::vector<ScreenCaptureTarget> ScreenCaptureSource::targets(ScreenTargetKind kind){
    std::vector<ScreenCaptureTarget> result;
    if(kind==ScreenTargetKind::Window){
        EnumWindows([](HWND window,LPARAM param)->BOOL{
            DWORD pid=0;GetWindowThreadProcessId(window,&pid);
            if(pid==GetCurrentProcessId()||!IsWindowVisible(window)||GetWindow(window,GW_OWNER)||GetWindowTextLengthW(window)==0)return TRUE;
            DWORD cloaked=0;DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
            wchar_t title[1024]{};GetWindowTextW(window,title,1024);RECT r{};GetClientRect(window,&r);
            if(r.right<=0||r.bottom<=0)return TRUE;
            reinterpret_cast<std::vector<ScreenCaptureTarget>*>(param)->push_back({ScreenTargetKind::Window,uint64_t(window),pid,title,unsigned(r.right),unsigned(r.bottom),0});return TRUE;
        },reinterpret_cast<LPARAM>(&result));
    }else{
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM param)->BOOL{
            MONITORINFOEXW info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(monitor,&info))return TRUE;
            DEVMODEW mode{};mode.dmSize=sizeof(mode);EnumDisplaySettingsW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode);
            auto& list=*reinterpret_cast<std::vector<ScreenCaptureTarget>*>(param);
            list.push_back({ScreenTargetKind::Monitor,uint64_t(monitor),0,std::format(L"{}  {} x {}  {} Hz",info.szDevice,mode.dmPelsWidth,mode.dmPelsHeight,mode.dmDisplayFrequency),mode.dmPelsWidth,mode.dmPelsHeight,mode.dmDisplayFrequency});return TRUE;
        },reinterpret_cast<LPARAM>(&result));
    }return result;
}
struct ScreenCaptureSource::Impl {
    ScreenCaptureOptions options;SourceInfo info;ScreenCaptureMetrics metrics;
    ComPtr<ID3D11Device5> device;ComPtr<ID3D11DeviceContext4> context;
    ComPtr<ID3D12Device> consumer;ComPtr<ID3D11Fence> producerFence;ComPtr<ID3D12Fence> fence;
    ComPtr<IDXGIOutputDuplication> duplication;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice wrapped{nullptr};
    GraphicsCaptureItem item{nullptr};Direct3D11CaptureFramePool pool{nullptr};GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    std::array<std::shared_ptr<Surface>,6> slots;
    AVFrame* frame=nullptr;
    uint64_t signal=0,sequence=0;int64_t lastPts=0,nextPts=0,clockOffset=0,lastColorCheck=0;
    DWORD targetPid=0;bool hdr=false,wasWaiting=false,exclusion=false;
    std::wstring message;DXGI_FORMAT format=DXGI_FORMAT_B8G8R8A8_UNORM;
    ~Impl(){av_frame_free(&frame);}
    int64_t interval()const{return 10000000LL*info.nominalRateDen/info.nominalRateNum;}
    void updateRate(HMONITOR monitor){
        auto rate=options.fps?DXGI_RATIONAL{options.fps,1}:monitorRate(monitor);
        if(!rate.Numerator||!rate.Denominator){
            if(info.nominalRateNum)return; // Retain the last known rate on a transient query failure.
            rate={60,1};log::warn("screen","display refresh query failed; using 60 FPS limit");
        }
        if(info.nominalRateNum==int(rate.Numerator)&&info.nominalRateDen==int(rate.Denominator))return;
        info.nominalRateNum=int(rate.Numerator);info.nominalRateDen=int(rate.Denominator);
        info.averageFps=double(rate.Numerator)/rate.Denominator;nextPts=0;
        if(sequence)wasWaiting=true;
        log::info("screen-rate",std::format("followDisplay={} limit={}/{} ({:.3f} Hz) monitor={}",options.fps==0,rate.Numerator,rate.Denominator,info.averageFps,uint64_t(monitor)));
    }
    void color(){
        auto& c=info.color;c={};c.pixelFormat=hdr?pipeline::SourcePixelFormat::Rgba16F:pipeline::SourcePixelFormat::Bgra8;
        c.range=pipeline::ColorRange::Full;c.matrix=pipeline::YuvMatrix::BT709;
        c.transfer=hdr?pipeline::TransferFunction::Linear:pipeline::TransferFunction::SRGB;
        c.primaries=pipeline::ColorPrimaries::BT709;c.scRgb=hdr;
    }
    std::shared_ptr<Surface> acquire(unsigned width,unsigned height){
        for(auto& slot:slots){if(slot&&slot.use_count()!=1)continue;
            if(!slot||slot->width!=width||slot->height!=height||slot->format!=format){
                auto next=std::make_shared<Surface>();next->width=width;next->height=height;next->format=format;next->fence=fence;
                D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.Format=format;td.SampleDesc.Count=1;
                td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
                check(device->CreateTexture2D(&td,nullptr,&next->producer),"Create shared texture");
                ComPtr<IDXGIResource1> resource;check(next->producer.As(&resource),"shared IDXGIResource1");NativeHandle handle;
                check(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value),"texture NT handle");
                check(consumer->OpenSharedHandle(handle.value,IID_PPV_ARGS(&next->consumer)),"Open texture D3D12");slot=std::move(next);
            }return slot;
        }return {};
    }
    void startDuplication(){
        ComPtr<IDXGIDevice> dxgi;check(device.As(&dxgi),"DXGI device");ComPtr<IDXGIAdapter> adapter;check(dxgi->GetAdapter(&adapter),"adapter");
        for(UINT i=0;;++i){ComPtr<IDXGIOutput> output;const auto hr=adapter->EnumOutputs(i,&output);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"EnumOutputs");
            DXGI_OUTPUT_DESC d{};check(output->GetDesc(&d),"output description");if(uint64_t(d.Monitor)!=options.target)continue;
            if(d.Rotation!=DXGI_MODE_ROTATION_IDENTITY)throw winrt::hresult_error(E_NOTIMPL,L"For a rotated display, please use the Windows capture method");
            ComPtr<IDXGIOutput5> five;check(output.As(&five),"IDXGIOutput5");
            const DXGI_FORMAT formats[]={format};check(five->DuplicateOutput1(device.Get(),0,1,formats,&duplication),"DuplicateOutput1");return;
        }throw winrt::hresult_error(DXGI_ERROR_NOT_FOUND,L"The compatible method requires the display to be connected to the processing GPU; please select Windows capture");
    }
};
ScreenCaptureSource::ScreenCaptureSource():p_(std::make_unique<Impl>()){}
ScreenCaptureSource::~ScreenCaptureSource(){close();}
bool ScreenCaptureSource::open(const SourceOpenDesc& desc){
    close();p_=std::make_unique<Impl>();auto& p=*p_;
    try{
        if(!ScreenCaptureOptions::parse(desc.path,p.options)||!desc.d3d12Device)throw winrt::hresult_error(E_INVALIDARG,L"Screen capture parameters are invalid");
        const auto& o=p.options;HWND window=o.kind==ScreenTargetKind::Window?reinterpret_cast<HWND>(o.target):nullptr;
        if(window){GetWindowThreadProcessId(window,&p.targetPid);if(!IsWindow(window)||p.targetPid==GetCurrentProcessId())throw winrt::hresult_error(E_INVALIDARG,L"The window is closed or belongs to Veyra");}
        HMONITOR monitor=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST):reinterpret_cast<HMONITOR>(o.target);
        p.hdr=hdrMonitor(monitor);p.format=p.hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
        p.consumer=static_cast<ID3D12Device*>(desc.d3d12Device);
        ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");ComPtr<IDXGIAdapter> adapter;
        check(factory->EnumAdapterByLuid(p.consumer->GetAdapterLuid(),IID_PPV_ARGS(&adapter)),"processing adapter");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11CreateDevice");
        check(device.As(&p.device),"D3D11 device5");check(context.As(&p.context),"D3D11 context4");
        check(p.device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&p.producerFence)),"producer fence");NativeHandle handle;
        check(p.producerFence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&handle.value),"fence NT handle");
        check(p.consumer->OpenSharedHandle(handle.value,IID_PPV_ARGS(&p.fence)),"Open fence D3D12");
        if(o.kind==ScreenTargetKind::Monitor){excludeOwnWindows();p.exclusion=true;}
        if(o.method==ScreenCaptureMethod::Duplication){
            if(o.cursor)throw winrt::hresult_error(E_NOTIMPL,L"For the compatible method, turn off the mouse cursor, or switch to Windows capture");
            p.startDuplication();DXGI_OUTDUPL_DESC d{};p.duplication->GetDesc(&d);p.poolSize={int(d.ModeDesc.Width),int(d.ModeDesc.Height)};
        }else{
            if(!GraphicsCaptureSession::IsSupported())throw winrt::hresult_error(E_NOTIMPL,L"The system does not support Windows Graphics Capture");
            auto interop=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
            if(window)check(interop->CreateForWindow(window,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(p.item)),"CreateForWindow");
            else check(interop->CreateForMonitor(monitor,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(p.item)),"CreateForMonitor");
            ComPtr<IDXGIDevice> dxgi;check(device.As(&dxgi),"DXGI device");winrt::com_ptr<IInspectable> inspectable;
            check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspectable.put()),"WinRT device");p.wrapped=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
            p.poolSize=p.item.Size();p.pool=Direct3D11CaptureFramePool::CreateFreeThreaded(p.wrapped,DirectXPixelFormat(p.format),2,p.poolSize);
            p.session=p.pool.CreateCaptureSession(p.item);p.session.IsCursorCaptureEnabled(o.cursor);p.session.StartCapture();
        }
        if(p.poolSize.Width<=int(o.left+o.right)||p.poolSize.Height<=int(o.top+o.bottom))throw winrt::hresult_error(E_INVALIDARG,L"The crop range exceeds the frame");
        p.info.opened=true;p.info.kind=pipeline::SourceKind::ScreenCapture;p.info.width=p.poolSize.Width-o.left-o.right;p.info.height=p.poolSize.Height-o.top-o.bottom;
        p.updateRate(monitor);p.info.hardwareDecodeActive=true;
        p.info.videoDecodePath=o.method==ScreenCaptureMethod::Wgc?"WGC shared GPU":"DXGI duplication shared GPU";
        p.info.containerName="screen";p.info.videoCodecName="GPU RGB";p.info.videoPixelFormatName=p.hdr?"scRGB FP16":"BGRA8 sRGB";p.color();
        p.clockOffset=host100ns()-qpc100ns();p.frame=av_frame_alloc();if(!p.frame)throw std::bad_alloc();
        log::info("screen",std::format("opened method={} target={} size={}x{} hdr={} fpsLimit={:.3f} followDisplay={} cursor={} pool=6 cpuReadback=0 audioCapture=0",unsigned(o.method),o.target,p.info.width,p.info.height,p.hdr,p.info.averageFps,o.fps==0,o.cursor));
        return true;
    }catch(const winrt::hresult_error& e){p.message=std::format(L"Screen capture failed 0x{:08X}: {}",uint32_t(e.code()),e.message().c_str());log::error("screen",winrt::to_string(p.message));}
    catch(const std::exception& e){p.message=L"Screen capture resource allocation failed";log::error("screen",e.what());}
    auto error=p.message;close();p.message=error;return false;
}
SourceReadStatus ScreenCaptureSource::read(pipeline::FramePacket& packet,const AVFrame** output){
    auto& p=*p_;*output=nullptr;if(!p.info.opened)return SourceReadStatus::Error;
    try{
        HWND window=p.options.kind==ScreenTargetKind::Window?reinterpret_cast<HWND>(p.options.target):nullptr;
        if(window){DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(!IsWindow(window)||pid!=p.targetPid){p.message=L"The capture window is closed; please select a window again";return SourceReadStatus::Error;}
            if(IsIconic(window)){p.message=L"The window is minimized; waiting for it to restore";p.wasWaiting=true;return SourceReadStatus::Waiting;}}
        const auto now=host100ns();
        if(now-p.lastColorCheck>10000000){
            p.lastColorCheck=now;
            const auto monitor=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST):reinterpret_cast<HMONITOR>(p.options.target);
            MONITORINFO mi{sizeof(mi)};
            if(!GetMonitorInfoW(monitor,&mi)){p.message=L"The display was disconnected; please select a target again";return SourceReadStatus::Error;}
            if(!p.options.fps)p.updateRate(monitor);
            const bool hdr=hdrMonitor(monitor);
            if(hdr!=p.hdr){
                p.hdr=hdr;p.format=hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;p.color();p.wasWaiting=true;
                p.info.videoPixelFormatName=hdr?"scRGB FP16":"BGRA8 sRGB";
                if(p.duplication){p.duplication.Reset();p.startDuplication();}
                else p.pool.Recreate(p.wrapped,DirectXPixelFormat(p.format),2,p.poolSize);
                log::info("screen",std::format("source color changed scRgb={}",hdr));return SourceReadStatus::Waiting;
            }
        }
        ComPtr<ID3D11Texture2D> texture;Direct3D11CaptureFrame captured{nullptr};int64_t pts=0;
        struct FrameLease {Direct3D11CaptureFrame& frame;~FrameLease(){if(frame)try{frame.Close();}catch(...){}}} frameLease{captured};
        struct DupLease {IDXGIOutputDuplication* value=nullptr;~DupLease(){if(value)value->ReleaseFrame();}} lease;
        if(p.duplication){DXGI_OUTDUPL_FRAME_INFO fi{};ComPtr<IDXGIResource> resource;const auto hr=p.duplication->AcquireNextFrame(0,&fi,&resource);
            if(hr==DXGI_ERROR_WAIT_TIMEOUT)return SourceReadStatus::Waiting;
            if(hr==DXGI_ERROR_ACCESS_LOST){p.duplication.Reset();p.startDuplication();p.wasWaiting=true;return SourceReadStatus::Waiting;}
            check(hr,"AcquireNextFrame");lease.value=p.duplication.Get();check(resource.As(&texture),"duplication texture");
            if(!fi.LastPresentTime.QuadPart)return SourceReadStatus::Waiting;
            LARGE_INTEGER freq{};QueryPerformanceFrequency(&freq);pts=int64_t(static_cast<long double>(fi.LastPresentTime.QuadPart)*10000000/freq.QuadPart)+p.clockOffset;++p.metrics.received;
        }else{
            for(unsigned i=0;i<2;++i){auto next=p.pool.TryGetNextFrame();if(!next)break;if(captured){captured.Close();++p.metrics.dropped;}captured=std::move(next);++p.metrics.received;}
            if(!captured)return SourceReadStatus::Waiting;
            const auto size=captured.ContentSize();
            if(size.Width!=p.poolSize.Width||size.Height!=p.poolSize.Height){captured.Close();captured=nullptr;if(size.Width>0&&size.Height>0){p.poolSize=size;p.pool.Recreate(p.wrapped,DirectXPixelFormat(p.format),2,size);}p.wasWaiting=true;return SourceReadStatus::Waiting;}
            auto access=captured.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();check(access->GetInterface(IID_PPV_ARGS(&texture)),"capture surface");
            pts=captured.SystemRelativeTime().count()+p.clockOffset;
        }
        // Accumulate deadlines: comparing each jittery interval to an exact
        // 1/60 would accidentally discard nearly half of a 60 Hz stream.
        if(pts<=p.lastPts||pts<p.nextPts-5000){++p.metrics.dropped;return SourceReadStatus::Waiting;}
        D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
        if(td.Format!=p.format)throw winrt::hresult_error(E_UNEXPECTED,L"The capture color format changed; please reconnect");
        const auto& o=p.options;unsigned w=std::min(td.Width,unsigned(p.poolSize.Width)),h=std::min(td.Height,unsigned(p.poolSize.Height));
        if(w<=o.left+o.right||h<=o.top+o.bottom){p.message=L"The window is smaller than the crop range; waiting for the size to recover";p.wasWaiting=true;return SourceReadStatus::Waiting;}
        w-=o.left+o.right;h-=o.top+o.bottom;
        av_frame_unref(p.frame);auto slot=p.acquire(w,h);if(!slot){++p.metrics.dropped;return SourceReadStatus::Waiting;}
        D3D11_BOX box{o.left,o.top,0,o.left+w,o.top+h,1};p.context->CopySubresourceRegion(slot->producer.Get(),0,0,0,0,texture.Get(),0,&box);
        check(p.context->Signal(p.producerFence.Get(),++p.signal),"producer Signal");p.context->Flush();
        auto* owner=new std::shared_ptr<Surface>(slot);
        p.frame->buf[0]=av_buffer_create(reinterpret_cast<uint8_t*>(owner),sizeof(*owner),[](void*,uint8_t* bytes){delete reinterpret_cast<std::shared_ptr<Surface>*>(bytes);},nullptr,0);
        if(!p.frame->buf[0]){delete owner;throw std::bad_alloc();}
        p.frame->data[0]=reinterpret_cast<uint8_t*>(slot->producer.Get());p.frame->format=AV_PIX_FMT_D3D11;p.frame->width=int(w);p.frame->height=int(h);p.frame->pts=pts;
        packet={};packet.sequence=++p.sequence;packet.pts={pts,10000000};packet.duration=p.lastPts?pipeline::Rational{pts-p.lastPts,10000000}:pipeline::Rational{p.info.nominalRateDen,p.info.nominalRateNum};
        packet.sourceKind=pipeline::SourceKind::ScreenCapture;packet.colorInfo=p.info.color;
        packet.arrivalHost100ns=host100ns();packet.decodedHost100ns=packet.arrivalHost100ns;
        if(p.sequence==1)packet.flags|=uint32_t(pipeline::FrameFlagBits::Open);
        if(w!=p.info.width||h!=p.info.height){packet.flags|=uint32_t(pipeline::FrameFlagBits::Resize);p.info.width=w;p.info.height=h;}
        if(p.wasWaiting||p.lastPts&&pts-p.lastPts>std::max<int64_t>(p.interval()*2,1000000))packet.flags|=uint32_t(pipeline::FrameFlagBits::Discontinuity);
        packet.hardwareSurface={slot->consumer.Get(),0,p.fence.Get(),p.signal,w,h};
        p.nextPts=std::max(p.nextPts+p.interval(),pts+p.interval()/2);
        p.lastPts=pts;p.wasWaiting=false;p.message.clear();++p.metrics.delivered;
        // WGC's compositor timestamp can be ahead of the CPU callback. Keep
        // that signed delta in diagnostics, not in the ingress CPU duration.
        p.metrics.ageMs=double(packet.arrivalHost100ns-now)/10000;
        if(p.metrics.delivered==1||p.metrics.delivered%300==0)log::info("screen-timing",std::format("readCpuMs={:.3f} compositorToReadMs={:.3f} delivered={} received={} dropped={}",p.metrics.ageMs,double(packet.arrivalHost100ns-pts)/10000,p.metrics.delivered,p.metrics.received,p.metrics.dropped));
        *output=p.frame;return SourceReadStatus::Frame;
    }catch(const winrt::hresult_error& e){p.message=std::format(L"Screen capture error 0x{:08X}: {}",uint32_t(e.code()),e.message().c_str());log::error("screen",winrt::to_string(p.message));return SourceReadStatus::Error;}
    catch(const std::exception& e){p.message=L"Screen capture resource allocation failed";log::error("screen",e.what());return SourceReadStatus::Error;}
}
void ScreenCaptureSource::close()noexcept{
    if(!p_)return;
    try{if(p_->session)p_->session.Close();}catch(...){}
    try{if(p_->pool)p_->pool.Close();}catch(...){}
    p_->session=nullptr;p_->pool=nullptr;p_->item=nullptr;p_->wrapped=nullptr;p_->duplication.Reset();
    if(p_->exclusion){restoreOwnWindows();p_->exclusion=false;}
    av_frame_free(&p_->frame);p_->slots={};p_->fence.Reset();p_->producerFence.Reset();p_->context.Reset();p_->device.Reset();p_->consumer.Reset();p_->info.opened=false;
}
const SourceInfo& ScreenCaptureSource::info()const{return p_->info;}
std::wstring ScreenCaptureSource::status()const{return p_->message;}
ScreenCaptureMetrics ScreenCaptureSource::metrics()const{return p_->metrics;}
}
