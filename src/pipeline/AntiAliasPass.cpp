#include "veyra/pipeline/AntiAliasPass.h"
#include "veyra/pipeline/SmaaLookupTables.h"
#include <bit>
#include <cstring>
#include <format>

namespace veyra::pipeline {
namespace {
using engine::AntiAliasMethod;
using engine::AntiAliasTemporal;
constexpr const char* kFxaaShaders[]={"AaFxaa3.dxil","AaFxaa4.dxil","AaFxaa5.dxil"};
constexpr const char* kSmaaWeightShaders[]={"AaSmaaWeightsLow.dxil","AaSmaaWeightsMedium.dxil","AaSmaaWeightsHigh.dxil","AaSmaaWeightsUltra.dxil"};
// SMAA_THRESHOLD of the low / medium / high / ultra presets (iryoku/smaa).
constexpr float kSmaaThresholds[]={0.15f,0.1f,0.1f,0.05f};
constexpr const char* kFxaaTiers[]={"medium","high","ultra"};
constexpr const char* kSmaaTiers[]={"low","medium","high","ultra"};
// Official SMAA T2x / 4x subsample-index sets; captured frames are not jittered.
constexpr float kT2xIndices[2][4]={{1,1,1,0},{2,2,2,0}};
constexpr float kX4Indices[4][4]={{5,3,1,3},{4,6,2,3},{3,5,1,4},{6,4,2,4}};
constexpr DXGI_FORMAT kColorFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr UINT alignedPitch(UINT bytes){
    return (bytes+D3D12_TEXTURE_DATA_PITCH_ALIGNMENT-1)/D3D12_TEXTURE_DATA_PITCH_ALIGNMENT*D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
}
}

std::string AntiAliasPass::describe() const {
    switch(settings_.method){
    case AntiAliasMethod::Fxaa:
        return std::format("FXAA {}",settings_.quality<3?kFxaaTiers[settings_.quality]:"?");
    case AntiAliasMethod::Smaa:
        return std::format("SMAA {}{}",settings_.quality<4?kSmaaTiers[settings_.quality]:"?",
            !settings_.usesTemporal()?"":settings_.temporal==AntiAliasTemporal::T2x?" temporal-T2x":" temporal-4x");
    case AntiAliasMethod::Mlaa:
        return std::format("MLAA threshold={:.2f} strength={:.2f}",settings_.mlaaThreshold,settings_.mlaaStrength);
    }
    return "unknown";
}

bool AntiAliasPass::createStage(ID3D12Device* device,Stage& stage,const char* shader,UINT srvCount){
    std::vector<uint8_t> cs;
    stage.srvCount=srvCount;
    return stage.pass.loadShader(shader,cs)&&stage.pass.create(device,cs,srvCount+1,srvCount,1,kConstantCount,0,true);
}

bool AntiAliasPass::initialize(ID3D12Device* device,const engine::AntiAliasSettings& settings,uint32_t width,uint32_t height){
    if(!settings.enabled||!settings.validate().empty()||!width||!height)return false;
    settings_=settings;width_=width;height_=height;frame_=0;historyValid_=false;
    lumaTexture_=makeTexture(device,width,height,DXGI_FORMAT_R16_FLOAT,true);
    if(!lumaTexture_||!createStage(device,luma_,"AaLuma.dxil",1))return false;
    switch(settings.method){
    case AntiAliasMethod::Fxaa:
        return createStage(device,fxaa_,kFxaaShaders[settings.quality],2);
    case AntiAliasMethod::Smaa:{
        // As in Magpie: 8-bit edges / weights for low and medium, FP16 for high and ultra.
        const bool wide=settings.quality>=2;
        edgesFormat_=wide?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R8G8_UNORM;
        weightsFormat_=wide?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
        edges_=makeTexture(device,width,height,edgesFormat_,true);
        weights_=makeTexture(device,width,height,weightsFormat_,true);
        if(!edges_||!weights_||!prepareLookups(device)||
           !createStage(device,smaaEdges_,"AaSmaaEdges.dxil",1)||
           !createStage(device,smaaWeights_,kSmaaWeightShaders[settings.quality],3)||
           !createStage(device,smaaBlend_,"AaSmaaBlend.dxil",2))return false;
        if(!settings.usesTemporal())return true;
        current_=makeTexture(device,width,height,kColorFormat,true);
        history_=makeTexture(device,width,height,kColorFormat,false);
        return current_&&history_&&createStage(device,temporal_,"AaTemporalResolve.dxil",2);
    }
    case AntiAliasMethod::Mlaa:
        edgesFormat_=DXGI_FORMAT_R8G8_UNORM;
        edges_=makeTexture(device,width,height,edgesFormat_,true);
        counts_=makeTexture(device,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        return edges_&&counts_&&createStage(device,mlaaEdges_,"AaMlaaEdges.dxil",1)&&
            createStage(device,mlaaCounts_,"AaMlaaCounts.dxil",1)&&createStage(device,mlaaBlend_,"AaMlaaBlend.dxil",3);
    }
    return false;
}

bool AntiAliasPass::prepareLookups(ID3D12Device* device){
    area_=makeTexture(device,smaa::kAreaTexWidth,smaa::kAreaTexHeight,DXGI_FORMAT_R8G8_UNORM,false);
    search_=makeTexture(device,smaa::kSearchTexWidth,smaa::kSearchTexHeight,DXGI_FORMAT_R8_UNORM,false);
    areaPitch_=alignedPitch(smaa::kAreaTexPitch);searchPitch_=alignedPitch(smaa::kSearchTexPitch);
    areaUpload_=makeUploadBuffer(device,uint64_t(areaPitch_)*smaa::kAreaTexHeight);
    searchUpload_=makeUploadBuffer(device,uint64_t(searchPitch_)*smaa::kSearchTexHeight);
    if(!area_||!search_||!areaUpload_||!searchUpload_)return false;
    const auto fill=[](ID3D12Resource* upload,const uint8_t* rows,UINT rowBytes,UINT pitch,UINT height){
        void* p=nullptr;
        if(FAILED(upload->Map(0,nullptr,&p)))return false;
        for(UINT y=0;y<height;++y)std::memcpy(static_cast<uint8_t*>(p)+size_t(y)*pitch,rows+size_t(y)*rowBytes,rowBytes);
        upload->Unmap(0,nullptr);
        return true;
    };
    lookupsPending_=fill(areaUpload_.Get(),smaa::kAreaTex,smaa::kAreaTexPitch,areaPitch_,smaa::kAreaTexHeight)&&
        fill(searchUpload_.Get(),smaa::kSearchTex,smaa::kSearchTexPitch,searchPitch_,smaa::kSearchTexHeight);
    if(!lookupsPending_)veyra::log::error("anti-alias","SMAA lookup upload map failed");
    return lookupsPending_;
}

// The upload buffers live as long as the pass; the owner drains before release.
void AntiAliasPass::uploadLookups(ID3D12GraphicsCommandList* list,StateTracker& tracker){
    const auto copy=[&](ID3D12Resource* texture,ID3D12Resource* upload,DXGI_FORMAT format,UINT width,UINT height,UINT pitch){
        tracker.transition(list,texture,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION target{},source{};
        target.pResource=texture;target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.pResource=upload;source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint={format,width,height,1,pitch};
        list->CopyTextureRegion(&target,0,0,0,&source,nullptr);
        tracker.transition(list,texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    copy(area_.Get(),areaUpload_.Get(),DXGI_FORMAT_R8G8_UNORM,smaa::kAreaTexWidth,smaa::kAreaTexHeight,areaPitch_);
    copy(search_.Get(),searchUpload_.Get(),DXGI_FORMAT_R8_UNORM,smaa::kSearchTexWidth,smaa::kSearchTexHeight,searchPitch_);
    lookupsPending_=false;
}

void AntiAliasPass::refresh(const engine::AntiAliasSettings& settings){
    // Only the uniform values change in place; a structural change rebuilds the graph.
    if(settings_.sameStructure(settings))settings_=settings;
}

float AntiAliasPass::edgeThreshold() const {
    if(settings_.method==AntiAliasMethod::Smaa&&settings_.quality<4)return kSmaaThresholds[settings_.quality];
    if(settings_.method==AntiAliasMethod::Mlaa)return settings_.mlaaThreshold;
    return 0.0f;
}

void AntiAliasPass::createViews(ID3D12Device* device,DescriptorStager& stager,ID3D12Resource* input,ID3D12Resource* output){
    input_=input;output_=output;
    const auto srv=[&](Stage& stage,UINT slot,ID3D12Resource* resource){stager.stageSrv(resource,nullptr,stage.pass.heap.Get(),slot);};
    const auto uav=[&](Stage& stage,ID3D12Resource* resource,DXGI_FORMAT format){makeUav(device,resource,format,cpuHandleOf(stage.pass,stage.srvCount));};
    srv(luma_,0,input);uav(luma_,lumaTexture_.Get(),DXGI_FORMAT_R16_FLOAT);
    switch(settings_.method){
    case AntiAliasMethod::Fxaa:
        srv(fxaa_,0,input);srv(fxaa_,1,lumaTexture_.Get());uav(fxaa_,output,kColorFormat);
        break;
    case AntiAliasMethod::Smaa:
        srv(smaaEdges_,0,lumaTexture_.Get());uav(smaaEdges_,edges_.Get(),edgesFormat_);
        srv(smaaWeights_,0,edges_.Get());srv(smaaWeights_,1,area_.Get());srv(smaaWeights_,2,search_.Get());
        uav(smaaWeights_,weights_.Get(),weightsFormat_);
        srv(smaaBlend_,0,input);srv(smaaBlend_,1,weights_.Get());uav(smaaBlend_,current_?current_.Get():output,kColorFormat);
        if(current_){srv(temporal_,0,current_.Get());srv(temporal_,1,history_.Get());uav(temporal_,output,kColorFormat);}
        break;
    case AntiAliasMethod::Mlaa:
        srv(mlaaEdges_,0,lumaTexture_.Get());uav(mlaaEdges_,edges_.Get(),edgesFormat_);
        srv(mlaaCounts_,0,edges_.Get());uav(mlaaCounts_,counts_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
        srv(mlaaBlend_,0,input);srv(mlaaBlend_,1,lumaTexture_.Get());srv(mlaaBlend_,2,counts_.Get());uav(mlaaBlend_,output,kColorFormat);
        break;
    }
}

bool AntiAliasPass::dispatch(ID3D12GraphicsCommandList* list,StateTracker& tracker,bool hdrWorking,bool reset){
    if(!input_||!output_||input_==output_)return false;
    if(lookupsPending_)uploadLookups(list,tracker);
    if(reset){historyValid_=false;frame_=0;}
    float c[kConstantCount]{};
    c[0]=std::bit_cast<float>(width_);c[1]=std::bit_cast<float>(height_);
    c[2]=1.0f/float(width_);c[3]=1.0f/float(height_);
    // "1.0 = white": SDR white, or the 203-nit reference white of scRGB HDR working.
    c[4]=hdrWorking?80.0f/203.0f:1.0f;
    c[5]=edgeThreshold();c[6]=settings_.mlaaStrength;c[7]=settings_.historyWeight;
    if(settings_.usesTemporal())
        std::memcpy(c+8,settings_.temporal==AntiAliasTemporal::T2x?kT2xIndices[frame_&1]:kX4Indices[frame_&3],sizeof(float)*4);
    c[12]=settings_.historyRejection;c[13]=historyValid_?1.0f:0.0f;
    const UINT groupsX=(width_+7)/8,groupsY=(height_+7)/8;
    const auto run=[&](Stage& stage,ID3D12Resource* target,ID3D12Resource* a,ID3D12Resource* b=nullptr,ID3D12Resource* d=nullptr){
        ID3D12Resource* reads[]={a,b,d};
        for(auto* read:reads)if(read)tracker.transition(list,read,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list,target,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        stage.pass.bind(list,c,gpuHandleOf(stage.pass,0).ptr,gpuHandleOf(stage.pass,stage.srvCount).ptr);
        list->Dispatch(groupsX,groupsY,1);
        tracker.uavBarrier(list,target);
    };
    run(luma_,lumaTexture_.Get(),input_);
    switch(settings_.method){
    case AntiAliasMethod::Fxaa:
        run(fxaa_,output_,input_,lumaTexture_.Get());
        break;
    case AntiAliasMethod::Smaa:
        run(smaaEdges_,edges_.Get(),lumaTexture_.Get());
        run(smaaWeights_,weights_.Get(),edges_.Get(),area_.Get(),search_.Get());
        if(!current_){run(smaaBlend_,output_,input_,weights_.Get());break;}
        run(smaaBlend_,current_.Get(),input_,weights_.Get());
        run(temporal_,output_,current_.Get(),history_.Get());
        tracker.transition(list,output_,D3D12_RESOURCE_STATE_COPY_SOURCE);
        tracker.transition(list,history_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(history_.Get(),output_);
        tracker.transition(list,history_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        historyValid_=true;
        break;
    case AntiAliasMethod::Mlaa:
        run(mlaaEdges_,edges_.Get(),lumaTexture_.Get());
        run(mlaaCounts_,counts_.Get(),edges_.Get());
        run(mlaaBlend_,output_,input_,lumaTexture_.Get(),counts_.Get());
        break;
    }
    tracker.transition(list,output_,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ++frame_;
    return true;
}
}
