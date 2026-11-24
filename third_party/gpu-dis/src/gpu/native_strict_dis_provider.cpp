// Distribution note (2026-09-09): project-owned additions and modifications
// are licensed under Apache-2.0. Upstream portions retain their original
// licenses and attribution; see NOTICE, PROVENANCE.md and licenses/.
// OpenCV DIS/reference-derived portions retain Intel (2000-2008), Willow
// Garage (2009) and other upstream copyrights and BSD/Apache terms.
// See licenses/OPENCV_DIS_BSD_HEADER.txt and OPENCV_APACHE_LICENSE.txt.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "gpu_frame_contract.h"
#include "native_strict_dis_provider.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace xess_gpu { namespace {
constexpr auto kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto kWrite = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void checked(HRESULT hr, const char* where) {
    if (FAILED(hr)) { char text[128]; std::snprintf(text,sizeof(text),"%s: HRESULT 0x%08lX",where,(unsigned long)hr); throw std::runtime_error(text); }
}
struct Texture {
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = kWrite;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t w=0,h=0;
    void create(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT fmt) {
        w=width; h=height; format=fmt;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=w; d.Height=h; d.DepthOrArraySize=1; d.MipLevels=1; d.Format=fmt;
        d.SampleDesc.Count=1; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&resource)),"DIS texture");
    }
    void to(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES next) {
        if(state==next) return;
        D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=resource.Get(); b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore=state; b.Transition.StateAfter=next;
        list->ResourceBarrier(1,&b); state=next;
    }
};
struct Pipeline {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> state;
    uint32_t srvs=0,uavs=0,constants=0;
    void create(ID3D12Device* d,const std::string& file,uint32_t s,uint32_t u,uint32_t n) {
        srvs=s; uavs=u; constants=n;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors=s;
        ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors=u;
        D3D12_ROOT_PARAMETER params[3]{};
        for(uint32_t i=0;i<2;++i) { params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].DescriptorTable.NumDescriptorRanges=1; params[i].DescriptorTable.pDescriptorRanges=&ranges[i]; }
        params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants.Num32BitValues=n;
        D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters=3; desc.pParameters=params;
        ComPtr<ID3DBlob> blob,errors;
        checked(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"DIS root serialize");
        checked(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"DIS root");
        // Veyra 2026-11-24: `file` is UTF-8; open it as such, not through the ANSI code page.
        std::ifstream input(std::filesystem::path(std::u8string(file.begin(),file.end())),std::ios::binary|std::ios::ate);
        if(!input) throw std::runtime_error("Cannot open DIS shader: "+file);
        const auto length=input.tellg(); if(length<=0) throw std::runtime_error("Empty DIS shader: "+file);
        std::vector<char> bytes((size_t)length); input.seekg(0); input.read(bytes.data(),(std::streamsize)bytes.size());
        if(!input) throw std::runtime_error("Cannot read DIS shader: "+file);
        D3D12_COMPUTE_PIPELINE_STATE_DESC ps{}; ps.pRootSignature=root.Get(); ps.CS={bytes.data(),bytes.size()};
        checked(d->CreateComputePipelineState(&ps,IID_PPV_ARGS(&state)),"DIS pipeline");
    }
};
struct Binding {
    ComPtr<ID3D12DescriptorHeap> heap, staging;
    uint32_t increment=0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t offset) const {
        auto h=heap->GetCPUDescriptorHandleForHeapStart(); h.ptr+=SIZE_T(offset)*increment; return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(uint32_t offset) const {
        auto h=heap->GetGPUDescriptorHandleForHeapStart(); h.ptr+=UINT64(offset)*increment; return h;
    }
    void initialize(ID3D12Device* d, const Pipeline& p,
                    std::initializer_list<Texture*> inputs,std::initializer_list<Texture*> outputs) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=p.srvs+p.uavs; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        checked(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"DIS descriptors");
        hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        checked(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&staging)),"DIS staging descriptors");
        increment=d->GetDescriptorHandleIncrementSize(hd.Type);
        uint32_t i=0;
        for(auto* t:inputs) { set_input(d,i,t?t->resource.Get():nullptr,t?t->format:DXGI_FORMAT_R32_FLOAT); ++i; }
        i=p.srvs;
        for(auto* t:outputs) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC v{}; v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; v.Format=t->format;
            d->CreateUnorderedAccessView(t->resource.Get(),nullptr,&v,cpu(i++));
        }
    }
    void set_input(ID3D12Device* d,uint32_t index,ID3D12Resource* resource,DXGI_FORMAT format,uint32_t plane=0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; v.Format=format;
        v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels=1;
        v.Texture2D.PlaneSlice=plane;
        auto target=staging->GetCPUDescriptorHandleForHeapStart();target.ptr+=SIZE_T(index)*increment;
        d->CreateShaderResourceView(resource,&v,target);
        d->CopyDescriptorsSimple(1,cpu(index),target,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    void bind(ID3D12GraphicsCommandList* list,const Pipeline& p,const void* constants) const {
        ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(p.root.Get()); list->SetPipelineState(p.state.Get());
        list->SetComputeRootDescriptorTable(0,gpu(0)); list->SetComputeRootDescriptorTable(1,gpu(p.srvs));
        list->SetComputeRoot32BitConstants(2,p.constants,constants,0);
    }
};
struct ImageLevel {
    Texture image,gradient;
    std::array<Texture,5> tensors,aux;
    Binding area,grad,hor,ver;
};
struct WorkLevel {
    Texture sparse,dense,initial,work,du,dv;
    std::array<Texture,8> deriv;
    std::array<Texture,6> coeff;
    std::array<Binding,2> patch,dens,deriv1;
    Binding deriv2,system,sor0,sor1,update,zero,up;
};
struct Slot {
    std::array<std::vector<ImageLevel>,2> image;
    std::vector<WorkLevel> work;
    std::array<Texture,2> output;
    std::array<Binding,2> input,rgba_input,final;
    FencePoint completion;
    Texture unused_trace;
};
class StrictDis final : public MotionProvider {
    std::string shader_directory_;
    ComPtr<ID3D12Device> device_;
    ProviderConfig config_;
    StrictDisPreset preset_;
    StrictDisInput input_profile_;
    uint32_t stride_=4,gd_inner_=8;
    uint32_t finest_=2,coarsest_=0;
    Pipeline luma_,rgba_gray_,area_,gradient_,hor_,ver_,patch_,dense_,up_,v1_,v2_,system_,sor_,update_,zero_,final_;
    std::vector<Slot> slots_;
    uint64_t shader_dispatches_=0;
    void dispatch(ID3D12GraphicsCommandList* list,const Pipeline& p,const Binding& b,
                  const void* constants,uint32_t x,uint32_t y=1) {
        b.bind(list,p,constants); list->Dispatch(x,y,1); ++shader_dispatches_;
    }
    void allocate(Slot& s);
    void record_image(Slot& s,uint32_t index,const FrameLease& frame,ID3D12GraphicsCommandList* list);
    void record_direction(Slot& s,uint32_t direction,ID3D12GraphicsCommandList* list);
public:
    explicit StrictDis(std::string directory,StrictDisPreset preset,StrictDisInput input):shader_directory_(std::move(directory)),preset_(preset),input_profile_(input) {}
    const char* backend_name() const noexcept override { return preset_==StrictDisPreset::Medium?"gpu-dis-cpu-medium-strict-v1":"gpu-dis-cpu-fast-strict-v1"; }
    uint64_t shader_dispatches() const noexcept { return shader_dispatches_; }
    bool initialize(ID3D12Device* device,const ProviderConfig& config,std::string& error) override;
    bool record(const FrameLease* previous,const FrameLease& current,uint32_t slot,
                RecordContext& context,MotionPacket& packet,Counters& counters,std::string& error) override;
};

bool StrictDis::initialize(ID3D12Device* device,const ProviderConfig& config,std::string& error) {
    try {
        if(!device || config.source.width<32 || config.source.height<32 || !config.slots || config.slots>8)
            throw std::runtime_error("Strict DIS requires source >=32x32 and 1..8 slots");
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        checked(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&options,sizeof(options)),"DIS double feature");
        if(!options.DoublePrecisionFloatShaderOps) throw std::runtime_error("Strict DIS CPU rounding requires GPU double shader operations");
        device_=device; config_=config;
        finest_=preset_==StrictDisPreset::Medium?1:2;
        stride_=preset_==StrictDisPreset::Medium?3:4;
        gd_inner_=preset_==StrictDisPreset::Medium?12:8; // CPU floor(GD / 2): 25 -> 12 per sweep.
        auto w=config.source.width,h=config.source.height;
        int level=std::min((int)(std::log(std::max(w,h)/32.0)/std::log(2.0)+.5),
                           (int)(std::log(std::min(w,h)/8.0)/std::log(2.0)));
        if(level<(int)finest_) { level=std::max(0,(int)std::floor(std::log2(2.0f*w/40.0f))); finest_=(uint32_t)std::max(level-2,0); }
        coarsest_=(uint32_t)level;
        auto pipe=[&](Pipeline& p,const char* name,uint32_t s,uint32_t u,uint32_t n) {
            p.create(device,shader_directory_+"/"+name+".dxil",s,u,n);
        };
        if(input_profile_==StrictDisInput::Ffmpeg71RgbGrayLimited) {
            pipe(luma_,"dis_native_gray",2,1,6);
            pipe(rgba_gray_,"dis_native_rgba_gray",1,1,4);
        }
        else pipe(luma_,"dis_native_luma",1,1,4);
        pipe(area_,"dis_perfect_area",1,1,4);
        pipe(gradient_,"dis_d2_gradient",1,1,4);
        pipe(hor_,stride_==3?"dis_d3_structure_hor_stride3":"dis_d3_structure_hor",1,5,4);
        pipe(ver_,stride_==3?"dis_d3_structure_ver_stride3":"dis_d3_structure_ver",5,5,4);
        pipe(patch_,"dis_perfect_patch_parallel",9,2,14); pipe(dense_,"dis_perfect_densify_exact",3,1,6);
        pipe(up_,"dis_perfect_upsample_ipp",1,1,4); pipe(v1_,"dis_perfect_vr_deriv1_cpu",3,5,2);
        pipe(v2_,"dis_perfect_vr_deriv2",2,3,2); pipe(system_,"dis_perfect_vr_system_exact",11,6,6);
        pipe(sor_,"dis_perfect_vr_sor_exact",7,1,4); pipe(update_,"dis_perfect_vr_update",2,1,2);
        pipe(zero_,"dis_perfect_vr_zero",1,1,2); pipe(final_,"dis_perfect_final",1,1,5);
        slots_.clear(); slots_.resize(config.slots);
        for(auto& s:slots_) allocate(s);
        return true;
    } catch(const std::exception& ex) { error=ex.what(); return false; }
}

void StrictDis::allocate(Slot& s) {
    auto tex=[&](Texture& t,uint32_t w,uint32_t h,DXGI_FORMAT format) { t.create(device_.Get(),w,h,format); };
    for(auto& image:s.image) {
        image.resize(coarsest_+1);
        for(uint32_t l=0;l<=coarsest_;++l) {
            auto& p=image[l]; uint32_t w=config_.source.width>>l,h=config_.source.height>>l;
            tex(p.image,w,h,DXGI_FORMAT_R32_FLOAT);
            if(l<finest_) continue;
            tex(p.gradient,w,h,DXGI_FORMAT_R32G32_SINT);
            for(auto& t:p.tensors) tex(t,1+(w-8)/stride_,1+(h-8)/stride_,DXGI_FORMAT_R32_FLOAT);
            for(auto& t:p.aux) tex(t,w/stride_,h,DXGI_FORMAT_R32_FLOAT);
        }
    }
    s.work.resize(coarsest_+1);
    for(uint32_t l=finest_;l<=coarsest_;++l) {
        auto& p=s.work[l]; uint32_t w=config_.source.width>>l,h=config_.source.height>>l;
        tex(p.sparse,1+(w-8)/stride_,1+(h-8)/stride_,DXGI_FORMAT_R32G32_FLOAT);
        for(auto* t:{&p.dense,&p.initial,&p.work,&p.du,&p.dv}) tex(*t,w,h,DXGI_FORMAT_R32G32_FLOAT);
        for(auto& t:p.deriv) tex(t,w,h,DXGI_FORMAT_R32_FLOAT);
        for(auto& t:p.coeff) tex(t,w,h,DXGI_FORMAT_R32_FLOAT);
    }
    for(auto& t:s.output) tex(t,config_.source.width,config_.source.height,DXGI_FORMAT_R32G32_FLOAT);
    tex(s.unused_trace,1,1,DXGI_FORMAT_R32G32_FLOAT);
    auto bind=[&](Binding& b,const Pipeline& p,std::initializer_list<Texture*> a,std::initializer_list<Texture*> o) {
        b.initialize(device_.Get(),p,a,o);
    };
    for(uint32_t d=0;d<2;++d) {
        if(input_profile_==StrictDisInput::Ffmpeg71RgbGrayLimited) {
            bind(s.input[d],luma_,{nullptr,nullptr},{&s.image[d][0].image});
            bind(s.rgba_input[d],rgba_gray_,{nullptr},{&s.image[d][0].image});
        }
        else bind(s.input[d],luma_,{nullptr},{&s.image[d][0].image});
        bind(s.final[d],final_,{&s.work[finest_].work},{&s.output[d]});
        for(uint32_t l=finest_;l<=coarsest_;++l) {
            auto& i=s.image[d][l]; auto& j=s.image[1-d][l]; auto& w=s.work[l];
            auto& t=i.tensors; auto& a=i.aux;
            if(l) bind(i.area,area_,{&s.image[d][l==finest_?0:l-1].image},{&i.image});
            bind(i.grad,gradient_,{&i.image},{&i.gradient});
            bind(i.hor,hor_,{&i.gradient},{&a[0],&a[1],&a[2],&a[3],&a[4]});
            bind(i.ver,ver_,{&a[0],&a[1],&a[2],&a[3],&a[4]},{&t[0],&t[1],&t[2],&t[3],&t[4]});
            // u1 trace slot is a valid unused view, never written by strict patch.
            bind(w.patch[d],patch_,{&i.image,&j.image,&i.gradient,&t[0],&t[1],&t[2],&t[3],&t[4],&w.initial},{&w.sparse,&s.unused_trace});
            bind(w.dens[d],dense_,{&i.image,&j.image,&w.sparse},{&w.dense});
            bind(w.deriv1[d],v1_,{&i.image,&j.image,&w.work},{&w.deriv[0],&w.deriv[1],&w.deriv[2],&w.deriv[3],&w.deriv[4]});
        }
    }
    for(uint32_t l=finest_;l<=coarsest_;++l) {
        auto& w=s.work[l]; auto& d=w.deriv; auto& c=w.coeff;
        bind(w.deriv2,v2_,{&d[0],&d[1]},{&d[5],&d[6],&d[7]});
        bind(w.system,system_,{&d[0],&d[1],&d[2],&d[5],&d[6],&d[7],&d[3],&d[4],&w.du,&w.dense,&w.work},
             {&c[0],&c[1],&c[2],&c[3],&c[4],&c[5]});
        bind(w.sor0,sor_,{&c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&w.du},{&w.dv});
        bind(w.sor1,sor_,{&c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&w.dv},{&w.du});
        bind(w.update,update_,{&w.dense,&w.du},{&w.work});
        bind(w.zero,zero_,{nullptr},{&w.du});
        if(l>finest_) bind(w.up,up_,{&w.work},{&s.work[l-1].initial});
    }
}

void StrictDis::record_image(Slot& s,uint32_t index,const FrameLease& frame,ID3D12GraphicsCommandList* list) {
    auto& levels=s.image[index];
    const bool rgba=frame.color.resource->GetDesc().Format==DXGI_FORMAT_R8G8B8A8_UNORM;
    auto& input=rgba?s.rgba_input[index]:s.input[index];
    input.set_input(device_.Get(),0,frame.color.resource.Get(),rgba?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8_UNORM);
    if(!rgba && input_profile_==StrictDisInput::Ffmpeg71RgbGrayLimited)
        s.input[index].set_input(device_.Get(),1,frame.color.resource.Get(),DXGI_FORMAT_R8G8_UNORM,1);
    Texture borrowed; borrowed.resource=frame.color.resource; borrowed.state=frame.color.state;
    borrowed.to(list,kRead);
    levels[0].image.to(list,kWrite);
    const uint32_t base[6]={config_.source.width,config_.source.height,frame.color.valid.x,frame.color.valid.y,frame.metadata.matrix==Matrix::Bt709?1u:0u,0};
    dispatch(list,rgba?rgba_gray_:luma_,input,base,(base[0]+7)/8,(base[1]+7)/8);
    levels[0].image.to(list,kRead); borrowed.to(list,frame.color.state);
    for(uint32_t l=finest_;l<=coarsest_;++l) {
        auto& i=levels[l]; auto w=i.image.w,h=i.image.h;
        if(l) {
            const auto& from=levels[l==finest_?0:l-1].image;
            const uint32_t dims[4]={from.w,from.h,w,h};
            i.image.to(list,kWrite); dispatch(list,area_,i.area,dims,(w+7)/8,(h+7)/8); i.image.to(list,kRead);
        }
        const uint32_t dims[4]={w,h,w,h}, shape[4]={w,h,i.tensors[0].w,i.tensors[0].h};
        i.gradient.to(list,kWrite); dispatch(list,gradient_,i.grad,dims,(w+7)/8,(h+7)/8); i.gradient.to(list,kRead);
        for(auto& t:i.aux) t.to(list,kWrite);
        dispatch(list,hor_,i.hor,shape,(h+63)/64);
        for(auto& t:i.aux) t.to(list,kRead);
        for(auto& t:i.tensors) t.to(list,kWrite);
        dispatch(list,ver_,i.ver,shape,(shape[2]+63)/64);
        for(auto& t:i.tensors) t.to(list,kRead);
    }
}

void StrictDis::record_direction(Slot& s,uint32_t direction,ID3D12GraphicsCommandList* list) {
    for(int level=(int)coarsest_;level>=(int)finest_;--level) {
        const auto l=(uint32_t)level; auto& w=s.work[l];
        auto width=w.dense.w,height=w.dense.h,sw=w.sparse.w,sh=w.sparse.h;
        const uint32_t wh[2]={width,height};
        w.sparse.to(list,kWrite); w.initial.to(list,kRead);
        const uint32_t stripe_h=(sh+7)/8,diagonals=sw+stripe_h-1;
        for(uint32_t d=0;d<2;++d) for(uint32_t diagonal=0;diagonal<diagonals;++diagonal) {
            uint32_t first=diagonal+1>sw?diagonal+1-sw:0,last=std::min(diagonal,stripe_h-1);
            uint32_t count=last>=first?last-first+1:0;
            const uint32_t args[14]={width,height,sw,sh,8,stride_,gd_inner_,d,diagonal,l<coarsest_?1u:0u,0,0,0,0};
            dispatch(list,patch_,w.patch[direction],args,count*8);
            D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource=w.sparse.resource.Get();
            list->ResourceBarrier(1,&b);
        }
        w.sparse.to(list,kRead); w.dense.to(list,kWrite);
        const uint32_t den[6]={width,height,sw,sh,8,stride_};
        dispatch(list,dense_,w.dens[direction],den,(width+7)/8,(height+7)/8); w.dense.to(list,kRead);
        w.du.to(list,kWrite); dispatch(list,zero_,w.zero,wh,(width+7)/8,(height+7)/8); w.du.to(list,kRead);
        w.work.to(list,kWrite); dispatch(list,update_,w.update,wh,(width+7)/8,(height+7)/8); w.work.to(list,kRead);
        for(auto& t:w.deriv) t.to(list,kWrite);
        dispatch(list,v1_,w.deriv1[direction],wh,(width+7)/8,(height+7)/8);
        for(uint32_t k=0;k<5;++k) w.deriv[k].to(list,kRead);
        dispatch(list,v2_,w.deriv2,wh,(width+7)/8,(height+7)/8);
        for(uint32_t k=5;k<8;++k) w.deriv[k].to(list,kRead);
        struct VR { uint32_t w,h; float a,d,g,e; } vr{width,height,20,5,10,.01f};
        for(uint32_t fixed=0;fixed<5;++fixed) {
            for(auto& t:w.coeff) t.to(list,kWrite);
            dispatch(list,system_,w.system,&vr,(width+7)/8,(height+7)/8);
            for(auto& t:w.coeff) t.to(list,kRead);
            for(uint32_t sor=0;sor<5;++sor) for(uint32_t parity=0;parity<2;++parity) {
                auto& output=parity==0?w.dv:w.du; output.to(list,kWrite);
                const uint32_t args[4]={width,height,0x3FCCCCCDu,parity};
                dispatch(list,sor_,parity==0?w.sor0:w.sor1,args,(width+7)/8,(height+7)/8);
                output.to(list,kRead);
            }
            w.work.to(list,kWrite); dispatch(list,update_,w.update,wh,(width+7)/8,(height+7)/8); w.work.to(list,kRead);
        }
        if(l>finest_) {
            auto& next=s.work[l-1].initial; next.to(list,kWrite);
            const uint32_t args[4]={width,height,next.w,next.h};
            dispatch(list,up_,w.up,args,(next.w+7)/8,(next.h+7)/8); next.to(list,kRead);
        }
    }
    auto& output=s.output[direction]; output.to(list,kWrite);
    const auto& work=s.work[finest_].work;
    const uint32_t final[5]={work.w,work.h,output.w,output.h,1u<<finest_};
    dispatch(list,final_,s.final[direction],final,(output.w+7)/8,(output.h+7)/8); output.to(list,kRead);
}

bool StrictDis::record(const FrameLease* previous,const FrameLease& current,uint32_t slot,
                       RecordContext& context,MotionPacket& packet,Counters& counters,std::string& error) {
    try {
        if(!context.list || context.device!=device_.Get() || slot>=slots_.size())
            throw std::runtime_error("Strict DIS record context or slot mismatch");
        if(!context.completion.fence || !context.completion.value)
            throw std::runtime_error("Strict DIS requires caller completion fence/value");
        auto& s=slots_[slot];
        if(!s.completion.complete()) throw std::runtime_error("Strict DIS output slot still in flight; host must retire it");
        auto validate=[&](const FrameLease& f) {
            if(!f.color.resource || !same_adapter(context.adapter_luid,f.metadata.adapter_luid))
                throw std::runtime_error("Strict DIS source texture/adapter mismatch");
            auto desc=f.color.resource->GetDesc(); auto crop=f.color.valid;
            if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize!=1 || desc.SampleDesc.Count!=1 ||
               (desc.Format!=DXGI_FORMAT_NV12 && desc.Format!=DXGI_FORMAT_R8_UNORM &&
                !(input_profile_==StrictDisInput::Ffmpeg71RgbGrayLimited && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM)))
                throw std::runtime_error("Strict DIS input requires NV12, R8, or limited RGB-gray RGBA8");
            if(crop.width!=config_.source.width || crop.height!=config_.source.height ||
               uint64_t(crop.x)+crop.width>desc.Width || uint64_t(crop.y)+crop.height>desc.Height)
                throw std::runtime_error("Strict DIS source crop/geometry mismatch");
            if(input_profile_==StrictDisInput::Ffmpeg71RgbGrayLimited &&
               ((desc.Format!=DXGI_FORMAT_NV12 && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) || f.metadata.range!=Range::Limited ||
                (f.metadata.matrix!=Matrix::Bt601 && f.metadata.matrix!=Matrix::Bt709)))
                throw std::runtime_error("Strict DIS FFmpeg RGB-gray requires limited BT.601/709 NV12 or compatibility RGBA8");
            ComPtr<ID3D12Device> owner; checked(f.color.resource->GetDevice(IID_PPV_ARGS(&owner)),"DIS input device");
            if(owner.Get()!=device_.Get()) throw std::runtime_error("Strict DIS source belongs to another D3D12 device");
        };
        validate(current); if(previous) validate(*previous);
        if(previous && current.metadata.previous_source_frame_id!=previous->metadata.source_frame_id)
            throw std::runtime_error("Strict DIS adjacent source-frame ID mismatch");
        // All validation precedes recording; the host supplies producer GPU waits.
        record_image(s,0,current,context.list);
        record_image(s,1,previous?*previous:current,context.list);
        record_direction(s,0,context.list);
        if(previous && config_.bidirectional) record_direction(s,1,context.list);
        packet={}; packet.current_source_frame_id=current.metadata.source_frame_id;
        packet.previous_source_frame_id=current.metadata.previous_source_frame_id;
        packet.current_metadata=current.metadata; packet.first_frame_self=previous==nullptr;
        auto view=[&](Texture& t) { TextureView v; v.resource=t.resource; v.format=t.format;
            v.allocation={t.w,t.h}; v.valid={0,0,t.w,t.h}; v.state=kRead; return v; };
        packet.current_to_previous=view(s.output[0]); packet.has_reverse=config_.bidirectional;
        if(config_.bidirectional) packet.previous_to_current=view(s.output[previous?1:0]);
        packet.confidence_definition=ConfidenceDefinition::Unavailable;
        packet.confidence_semantics="DIS has no native confidence; host derives photometric/roundtrip reliability from raw source flows";
        packet.produced=context.completion; s.completion=context.completion;
        if(previous) { ++counters.source_pair_analysis_count; counters.directional_dispatch_count+=config_.bidirectional?2:1; }
        else { ++counters.first_frame_self_analysis_count; ++counters.directional_dispatch_count; }
        return true;
    } catch(const std::exception& ex) { error=ex.what(); return false; }
}
} // namespace
std::unique_ptr<MotionProvider> make_strict_dis_provider(const std::string& shader_directory,StrictDisPreset preset,StrictDisInput input) {
    return std::make_unique<StrictDis>(shader_directory,preset,input);
}
uint64_t strict_dis_shader_dispatches(const MotionProvider& provider) noexcept {
    auto* strict=dynamic_cast<const StrictDis*>(&provider);
    return strict?strict->shader_dispatches():0;
}
} // namespace xess_gpu
