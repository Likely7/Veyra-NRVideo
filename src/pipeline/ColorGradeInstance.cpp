#include "veyra/pipeline/ColorGradeInstance.h"
#include <bit>
#include <cstring>

namespace veyra::pipeline {
namespace {
constexpr std::array<UINT,3> widths{ColorGradeTables::kCurveEntries,ColorGradeTables::kHueEntries,ColorGradeTables::kLumEntries};
ComPtr<ID3D12Resource> makeLut(ID3D12Device* device,unsigned size){
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    d.Width=size;d.Height=size;d.DepthOrArraySize=UINT16(size);d.MipLevels=1;
    d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> result;
    const HRESULT hr=device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&result));
    if(FAILED(hr))veyra::log::error("color-instance",std::format("LUT allocation size={} hr=0x{:08X}",size,unsigned(hr)));
    return result;
}
}
bool ColorGradeInstance::initialize(ID3D12Device* device,uint32_t commandSlots){
    if(!commandSlots)return false;
    uploads_.resize(commandSlots);
    for(size_t t=0;t<textures_.size();++t){
        textures_[t]=makeTexture(device,widths[t],1,DXGI_FORMAT_R32G32B32A32_FLOAT,false);
        if(!textures_[t])return false;
        for(auto& slot:uploads_){slot[t]=makeUploadBuffer(device,uint64_t(widths[t])*16);if(!slot[t])return false;}
    }
    lut_=makeLut(device,1); // descriptor placeholder; never sampled
    return bool(lut_);
}
bool ColorGradeInstance::createPass(ID3D12Device* device){
    std::vector<uint8_t> cs;
    return pass_.loadShader("ColorGradePass.dxil",cs)&&pass_.create(device,cs,12,1,1,4+kColorGradeConstantCount,4);
}
void ColorGradeInstance::refresh(const engine::ColorSettings& settings){
    settings_=settings;
    auto effective=settings;
    // An unresolved/rejected LUT must stay disabled on subsequent live edits.
    if(!lutSize_&&effective.hasLut()){
        effective.lutName={};effective.lutStrength=100;effective.lutInputSpace=engine::ColorSettings::kLutInputCineon;
    }
    tables_=ColorGradeTables::bake(effective);
    // The input is already FP16. Sending a pure gain through neutral log/HSV
    // transforms can put an exactly representable value just below itself,
    // losing an entire FP16 step on store. Test the effective (including group
    // bypass/LUT rejection) settings, not a near-identity matrix tolerance.
    auto withoutExposure=effective;withoutExposure.exposure=0;
    exposureOnly_=!tables_.identity&&ColorGradeTables::bake(withoutExposure).identity;
    tables_.exposureOnly=exposureOnly_;
    dirty_=true;
}
bool ColorGradeInstance::setLut(ID3D12Device* device,const float* rgb,unsigned size){
    if(!rgb||size<2||size>64)return false;
    const size_t values=size_t(size)*size*size*3;
    for(size_t i=0;i<values;++i)if(!std::isfinite(rgb[i]))return false;
    auto texture=makeLut(device,size);
    const unsigned pitch=(size*16+255)&~255u;
    auto upload=makeUploadBuffer(device,uint64_t(pitch)*size*size);
    if(!texture||!upload)return false;
    void* p=nullptr;const HRESULT hr=upload->Map(0,nullptr,&p);
    if(FAILED(hr)){veyra::log::error("color-instance",std::format("LUT map hr=0x{:08X}",unsigned(hr)));return false;}
    for(unsigned z=0;z<size;++z)for(unsigned y=0;y<size;++y){
        auto* row=reinterpret_cast<float*>(static_cast<uint8_t*>(p)+(size_t(z)*size+y)*pitch);
        for(unsigned x=0;x<size;++x){
            const size_t i=((size_t(z)*size+y)*size+x)*3;
            row[x*4]=rgb[i];row[x*4+1]=rgb[i+1];row[x*4+2]=rgb[i+2];row[x*4+3]=1;
        }
    }
    upload->Unmap(0,nullptr);
    lut_=std::move(texture);lutUpload_=std::move(upload);lutSize_=size;lutPitch_=pitch;lutPending_=true;
    refresh(settings_);
    return true;
}
void ColorGradeInstance::stageLut(DescriptorStager& stager){
    if(!pass_.heap)return;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture3D.MipLevels=1;
    stager.stageSrv(lut_.Get(),&srv,pass_.heap.Get(),11);
}
void ColorGradeInstance::createViews(ID3D12Device* device,DescriptorStager& stager,ID3D12Resource* input,ID3D12Resource* output){
    input_=input;output_=output;
    stager.stageSrv(input,nullptr,pass_.heap.Get(),0);
    makeUav(device,output,DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(pass_,1));
    for(UINT t=0;t<3;++t)stager.stageSrv(textures_[t].Get(),nullptr,pass_.heap.Get(),8+t);
    stageLut(stager);
}
bool ColorGradeInstance::dispatch(ID3D12GraphicsCommandList* list,StateTracker& tracker,uint32_t slot,uint32_t width,uint32_t height,bool hdrWorking){
    if(slot>=uploads_.size()||!input_||!output_||input_==output_)return false;
    auto copy=[&](ID3D12Resource* dst,ID3D12Resource* src,UINT w,UINT h,UINT d,UINT pitch){
        tracker.transition(list,dst,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION target{},source{};target.pResource=dst;target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.pResource=src;source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,w,h,d,pitch};
        list->CopyTextureRegion(&target,0,0,0,&source,nullptr);
        tracker.transition(list,dst,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    if(dirty_){
        const float* data[]={tables_.curve.data(),tables_.hue.data(),tables_.lum.data()};
        for(size_t t=0;t<textures_.size();++t){
            void* p=nullptr;const HRESULT hr=uploads_[slot][t]->Map(0,nullptr,&p);
            if(FAILED(hr)){veyra::log::error("color-instance",std::format("table map slot={} table={} hr=0x{:08X}",slot,t,unsigned(hr)));return false;}
            std::memcpy(p,data[t],size_t(widths[t])*16);uploads_[slot][t]->Unmap(0,nullptr);
            copy(textures_[t].Get(),uploads_[slot][t].Get(),widths[t],1,1,widths[t]*16);
        }
        dirty_=false;
    }
    if(lutPending_){copy(lut_.Get(),lutUpload_.Get(),lutSize_,lutSize_,lutSize_,lutPitch_);lutPending_=false;}
    tracker.transition(list,input_,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list,output_,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    float c[4+kColorGradeConstantCount]={std::bit_cast<float>(width),std::bit_cast<float>(height),hdrWorking?80.0f/203.0f:1.0f,exposureOnly_?1.0f:0.0f};
    packColorGradeConstants(tables_,c+4);
    pass_.bind(list,c,gpuHandleOf(pass_,0).ptr,gpuHandleOf(pass_,1).ptr,gpuHandleOf(pass_,8).ptr);
    list->Dispatch((width+15)/16,(height+15)/16,1);
    tracker.uavBarrier(list,output_);
    (void)detail_.run(list,tracker,output_,settings_,hdrWorking);
    tracker.transition(list,output_,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}
}
