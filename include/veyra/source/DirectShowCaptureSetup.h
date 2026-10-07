#pragma once
// WDM crossbar discovery/connection adapted from OBS libdshowcapture:
// c13d4b7b0c66979396ba0a9060c9aafc15bb7b22, source/device.cpp
// (FindCrossbar, ConnectPins), source/dshow-base.cpp (GetPinMedium,
// GetFilterByMedium, DirectConnectFilters). Copyright (C) 2023 Lain Bailey.
// LGPL-2.1-or-later; licenses/capture/LIBDSHOWCAPTURE_LGPL.txt.
// Veyra: all valid mediums, bounds/direction checks, exact matching pins,
// no duplicate/self connections, HRESULT propagation; never Route.
#include <windows.h>
#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
// Windows gates IKsPin's public COM declaration behind the DirectShow
// base-class marker. Only expose the declarations; no base-class SDK/library.
#ifndef __STREAMS__
#define __STREAMS__
#define VEYRA_CAPTURE_UNDEF_STREAMS
#endif
#include <ksproxy.h>
#ifdef VEYRA_CAPTURE_UNDEF_STREAMS
#undef __STREAMS__
#undef VEYRA_CAPTURE_UNDEF_STREAMS
#endif
#include <ocidl.h>
#include <olectl.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>

namespace veyra::source::dshow {
using Microsoft::WRL::ComPtr;
inline bool sameMedium(const REGPINMEDIUM& a,const REGPINMEDIUM& b){
    return a.clsMedium==b.clsMedium&&a.dw1==b.dw1&&a.dw2==b.dw2;
}
inline std::vector<REGPINMEDIUM> captureMediums(const KSMULTIPLE_ITEM* items){
    std::vector<REGPINMEDIUM> out;
    if(!items||items->Size<sizeof(KSMULTIPLE_ITEM)||items->Count>256||
       items->Count>(items->Size-sizeof(KSMULTIPLE_ITEM))/sizeof(REGPINMEDIUM))return out;
    const auto* media=reinterpret_cast<const REGPINMEDIUM*>(items+1);
    for(ULONG i=0;i<items->Count;++i)if(media[i].clsMedium!=GUID_NULL&&media[i].clsMedium!=KSMEDIUMSETID_Standard)
        out.push_back(media[i]);
    return out;
}
inline std::vector<REGPINMEDIUM> pinMediums(IPin* pin){
    ComPtr<IKsPin> ks;KSMULTIPLE_ITEM* items=nullptr;
    if(!pin||FAILED(pin->QueryInterface(IID_PPV_ARGS(&ks))))return {};
    const HRESULT hr=ks->KsQueryMediums(&items);
    auto out=SUCCEEDED(hr)?captureMediums(items):std::vector<REGPINMEDIUM>{};
    CoTaskMemFree(items);return out;
}
inline std::vector<ComPtr<IPin>> pins(IBaseFilter* filter,PIN_DIRECTION wanted){
    std::vector<ComPtr<IPin>> out;ComPtr<IEnumPins> en;
    if(!filter||FAILED(filter->EnumPins(&en)))return out;
    for(;;){ComPtr<IPin> pin;PIN_DIRECTION direction{};
        if(en->Next(1,&pin,nullptr)!=S_OK)break;
        if(SUCCEEDED(pin->QueryDirection(&direction))&&direction==wanted)out.push_back(pin);
    }return out;
}
inline bool connected(IPin* pin){ComPtr<IPin> peer;return pin&&SUCCEEDED(pin->ConnectedTo(&peer))&&peer;}
inline bool sameObject(IUnknown* a,IUnknown* b){
    ComPtr<IUnknown> x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x.Get()==y.Get();
}
// Connect the graph edge, not IAMCrossbar::Route: the user's physical input
// selection belongs to the driver and is retained.
inline HRESULT connectUpstream(IFilterGraph* graph,IBaseFilter* upstream,IBaseFilter* device){
    if(!graph||!upstream||!device)return E_POINTER;
    if(sameObject(upstream,device))return S_FALSE;
    HRESULT last=S_FALSE;
    for(auto& input:pins(device,PINDIR_INPUT)){
        if(connected(input.Get()))continue;
        last=VFW_E_CANNOT_CONNECT;
        const auto wanted=pinMediums(input.Get());
        for(auto& output:pins(upstream,PINDIR_OUTPUT)){
            if(connected(output.Get()))continue;
            const auto offered=pinMediums(output.Get());
            if(!wanted.empty()&&!offered.empty()&&!std::any_of(wanted.begin(),wanted.end(),[&](const auto& a){
                return std::any_of(offered.begin(),offered.end(),[&](const auto& b){return sameMedium(a,b);});}))continue;
            last=graph->ConnectDirect(output.Get(),input.Get(),nullptr);
            if(SUCCEEDED(last))return S_OK;
        }
    }return last;
}
struct CrossbarSetup {HRESULT hr=S_FALSE;ComPtr<IBaseFilter> filter;unsigned inputMediums=0;bool existing=false;};
inline CrossbarSetup connectCaptureCrossbar(IGraphBuilder* graph,ICaptureGraphBuilder2* builder,IBaseFilter* device){
    CrossbarSetup result;if(!graph||!builder||!device){result.hr=E_POINTER;return result;}
    std::vector<REGPINMEDIUM> wanted;
    for(auto& input:pins(device,PINDIR_INPUT)){
        ComPtr<IPin> peer;
        if(SUCCEEDED(input->ConnectedTo(&peer))&&peer){
            PIN_INFO info{};
            if(SUCCEEDED(peer->QueryPinInfo(&info))&&info.pFilter){
                ComPtr<IBaseFilter> filter;filter.Attach(info.pFilter);ComPtr<IAMCrossbar> crossbar;
                if(SUCCEEDED(filter.As(&crossbar))){result.filter=filter;result.existing=true;result.hr=S_OK;return result;}
            }continue;
        }
        auto media=pinMediums(input.Get());wanted.insert(wanted.end(),media.begin(),media.end());
    }
    result.inputMediums=unsigned(wanted.size());
    // No input medium means an ordinary source such as UVC. Avoid opening
    // unrelated hardware just to search for a crossbar.
    if(wanted.empty())return result;
    ComPtr<IAMCrossbar> crossbar;ComPtr<IBaseFilter> upstream;
    if(SUCCEEDED(builder->FindInterface(nullptr,nullptr,device,IID_PPV_ARGS(&crossbar)))&&
       SUCCEEDED(crossbar.As(&upstream))){
        result.filter=upstream;
        if(!sameObject(upstream.Get(),device)){
            FILTER_INFO info{};result.hr=upstream->QueryFilterInfo(&info);
            if(FAILED(result.hr))return result;
            ComPtr<IFilterGraph> owner;owner.Attach(info.pGraph);
            if(owner&&!sameObject(owner.Get(),graph)){result.hr=VFW_E_NOT_IN_GRAPH;return result;}
            if(!owner){result.hr=graph->AddFilter(upstream.Get(),L"Capture input selector");if(FAILED(result.hr))return result;}
        }
        result.hr=connectUpstream(graph,upstream.Get(),device);return result;
    }
    ComPtr<ICreateDevEnum> devices;ComPtr<IEnumMoniker> en;
    result.hr=CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&devices));
    if(FAILED(result.hr))return result;
    const HRESULT enumHr=devices->CreateClassEnumerator(AM_KSCATEGORY_CROSSBAR,&en,0);
    if(enumHr!=S_OK){result.hr=FAILED(enumHr)?enumHr:S_FALSE;return result;}
    for(;;){ComPtr<IMoniker> moniker;if(en->Next(1,&moniker,nullptr)!=S_OK)break;
        ComPtr<IBaseFilter> candidate;if(FAILED(moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&candidate))))continue;
        bool match=false;
        for(auto& pin:pins(candidate.Get(),PINDIR_OUTPUT))for(const auto& medium:pinMediums(pin.Get()))
            match|=std::any_of(wanted.begin(),wanted.end(),[&](const auto& m){return sameMedium(m,medium);});
        if(!match)continue;
        result.filter=candidate;result.hr=graph->AddFilter(candidate.Get(),L"Capture input selector");
        if(SUCCEEDED(result.hr))result.hr=connectUpstream(graph,candidate.Get(),device);
        return result;
    }
    result.hr=S_FALSE;return result;
}
inline HRESULT showPropertyPages(IUnknown* object,HWND owner,const wchar_t* title){
    if(!object)return E_POINTER;
    ComPtr<ISpecifyPropertyPages> pages;HRESULT hr=object->QueryInterface(IID_PPV_ARGS(&pages));
    if(FAILED(hr))return hr;
    CAUUID ids{};hr=pages->GetPages(&ids);
    if(SUCCEEDED(hr))hr=ids.cElems&&ids.pElems?OleCreatePropertyFrame(owner,0,0,title,1,&object,ids.cElems,ids.pElems,0,0,nullptr):E_NOINTERFACE;
    CoTaskMemFree(ids.pElems);return hr;
}
}
