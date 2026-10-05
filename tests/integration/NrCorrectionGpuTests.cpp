// Production shader on the real D3D12 device, with an archived pre-change
// shader as the off-path oracle. Readback is confined to this diagnostic.
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>

using namespace veyra;
using namespace veyra::pipeline;
struct Pixel{float r,g,b,a;};
constexpr unsigned Width=128,Height=16,Pitch=Width*sizeof(Pixel),Count=Width*Height;
std::array<double,3> lab(Pixel c){
    // Independent double-precision Oklab coordinates, used to measure hue
    // rotation rather than reproducing the correction implementation.
    const double l=std::cbrt(.4122214708*c.r+.5363325363*c.g+.0514459929*c.b);
    const double m=std::cbrt(.2119034982*c.r+.6806995451*c.g+.1073969566*c.b);
    const double s=std::cbrt(.0883024619*c.r+.2817188376*c.g+.6299787005*c.b);
    return {.2104542553*l+.7936177850*m-.0040720468*s,
            1.9779984951*l-2.4285922050*m+.4505937099*s,
            .0259040371*l+.7827717662*m-.8086757660*s};
}
Pixel to709(Pixel c){return {float(1.660491*c.r-.587641*c.g-.072850*c.b),
    float(-.124550*c.r+1.132900*c.g-.008349*c.b),
    float(-.018151*c.r-.100579*c.g+1.118730*c.b),c.a};}
Pixel to2020(Pixel c){return {float(.627404*c.r+.329283*c.g+.043313*c.b),
    float(.069097*c.r+.919540*c.g+.011362*c.b),
    float(.016391*c.r+.088013*c.g+.895595*c.b),c.a};}
float distance(Pixel a,Pixel b){return std::max({std::abs(a.r-b.r),std::abs(a.g-b.g),std::abs(a.b-b.b),std::abs(a.a-b.a)});}
bool finite(Pixel p){return std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a);}
void quantize(std::vector<Pixel>& pixels){using namespace DirectX::PackedVector;
    for(auto& p:pixels)for(auto* c:{&p.r,&p.g,&p.b,&p.a})*c=XMConvertHalfToFloat(XMConvertFloatToHalf(*c));}

struct Fixture{
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status=Status::Ok;
    StateTracker states;ComPtr<ID3D12Resource> base,neural,output,upload,readback;
    ComputePass pass,legacy;
    bool half=false;DXGI_FORMAT format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    bool initialize(const std::filesystem::path& archived,bool fp16){
        half=fp16;format=half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        gfx::DeviceContextDesc desc;desc.enableDebugLayer=true;
        if(!ctx.initialize(desc,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return false;
        base=makeTexture(ctx.device(),Width,Height,format,true);
        neural=makeTexture(ctx.device(),Width,Height,format,true);
        output=makeTexture(ctx.device(),Width,Height,format,true);
        upload=makeUploadBuffer(ctx.device(),Pitch*Height);
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=Pitch*Height;
        bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
        std::vector<uint8_t> shader;
        if(!pass.loadShader("NrResidualComposite.dxil",shader)||!pass.create(ctx.device(),shader,4,3,1,40))return false;
        std::ifstream f(archived,std::ios::binary);std::vector<uint8_t> old((std::istreambuf_iterator<char>(f)),{});
        if(!f||old.empty()||!legacy.create(ctx.device(),old,4,3,1,40))return false;
        for(auto* p:{&pass,&legacy}){
            makeSrv(ctx.device(),base.Get(),format,cpuHandleOf(*p,0));
            makeSrv(ctx.device(),base.Get(),format,cpuHandleOf(*p,1));
            makeSrv(ctx.device(),neural.Get(),format,cpuHandleOf(*p,2));
            makeUav(ctx.device(),output.Get(),format,cpuHandleOf(*p,3));
        }
        return base&&neural&&output&&upload;
    }
    bool fill(ID3D12Resource* tex,const std::vector<Pixel>& pixels){
        void* data=nullptr;if(FAILED(upload->Map(0,nullptr,&data)))return false;
        if(half){using namespace DirectX::PackedVector;auto* d=static_cast<HALF*>(data);
            for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){const auto p=pixels[y*Width+x];unsigned at=y*Pitch/2+x*4;
                d[at]=XMConvertFloatToHalf(p.r);d[at+1]=XMConvertFloatToHalf(p.g);d[at+2]=XMConvertFloatToHalf(p.b);d[at+3]=XMConvertFloatToHalf(p.a);}
        }else std::copy(pixels.begin(),pixels.end(),static_cast<Pixel*>(data));
        upload->Unmap(0,nullptr);
        unsigned slot;auto* list=ring.acquireNext(slot,status);if(!list)return false;
        states.transition(list,tex,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=tex;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={format,Width,Height,1,Pitch};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        return ring.submitAndSignal(slot)&&ring.waitIdle();
    }
    std::vector<Pixel> render(const std::array<float,40>& c,bool old=false){
        unsigned slot;auto* list=ring.acquireNext(slot,status);if(!list)return {};
        for(auto* tex:{base.Get(),neural.Get()})states.transition(list,tex,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list,output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto& p=old?legacy:pass;p.bind(list,c.data(),gpuHandleOf(p,0).ptr,gpuHandleOf(p,3).ptr);
        list->Dispatch((Width+15)/16,(Height+15)/16,1);
        states.uavBarrier(list,output.Get());states.transition(list,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};src.pResource=output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint={format,Width,Height,1,Pitch};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        if(!ring.submitAndSignal(slot)||!ring.waitIdle())return {};
        void* data=nullptr;D3D12_RANGE range{0,Pitch*Height};if(FAILED(readback->Map(0,&range,&data)))return {};
        std::vector<Pixel> result(Count);
        if(half){using namespace DirectX::PackedVector;auto* d=static_cast<HALF*>(data);
            for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){unsigned at=y*Pitch/2+x*4;
                result[y*Width+x]={XMConvertHalfToFloat(d[at]),XMConvertHalfToFloat(d[at+1]),XMConvertHalfToFloat(d[at+2]),XMConvertHalfToFloat(d[at+3])};}
        }else std::copy_n(static_cast<Pixel*>(data),Count,result.begin());
        D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);return result;
    }
};
int wmain(int argc,wchar_t** argv){
    if(argc!=3&&argc!=4)return 2;
    const bool fp16=argc==4&&std::wstring_view(argv[3])==L"--fp16";
    std::filesystem::create_directories(argv[2]);std::ofstream csv(std::filesystem::path(argv[2])/L"shader-observations.csv");
    csv<<"mode,index,baseR,baseG,baseB,neuralR,neuralG,neuralB,resultR,resultG,resultB\n";
    Fixture f;if(!f.initialize(argv[1],fp16))return 2;
    bool ok=true;unsigned checks=0;
    auto check=[&](const char* label,bool success){++checks;std::cout<<(success?"PASS ":"FAIL ")<<label<<std::endl;ok&=success;};
    std::vector<Pixel> base(Count),nr(Count);std::mt19937 random(37181);std::uniform_real_distribution<float> unit(0,1);
    for(unsigned i=0;i<Count;++i){base[i]={unit(random),unit(random),unit(random),.15f+.8f*unit(random)};nr[i]={unit(random),unit(random),unit(random),1};}
    base[0]={.2f,.3f,.4f,.7f};nr[0]={.2002f,.3003f,.4004f,1}; // safe detail at full 5
    base[1]={.985f,.985f,.985f,.8f};nr[1]={.992f,.992f,.992f,1};
    base[2]={.9f,.05f,.02f,.9f};nr[2]={.98f,.12f,.6f,1};
    base[3]={.002f,.004f,.006f,.6f};nr[3]={.001f,.002f,.003f,1};
    for(unsigned i=32;i<128;++i){const float e=(float(i)-80)*1e-7f;base[i]={.2f+e,.2f,.2f-e,.7f};nr[i]={.9f,.05f,.3f,1};}
    if(fp16){quantize(base);quantize(nr);}
    if(!f.fill(f.base.Get(),base)||!f.fill(f.neural.Get(),nr))return 2;
    std::array<float,40> c{};c[0]=c[1]=c[2]=c[3]=c[4]=1;
    for(float gain:{0.f,1.f,2.f,5.f}){
        c[0]=gain;auto old=f.render(c,true),now=f.render(c);
        if(old.size()!=Count||now.size()!=Count)return 2;
        float maxError=0;bool same=true;
        for(unsigned i=0;i<Count;++i){maxError=std::max(maxError,distance(old[i],now[i]));same&=distance(old[i],now[i])==0;}
        std::cout<<"legacy comparison gain="<<gain<<" maxError="<<maxError<<std::endl;
        check("control off agrees with archived shader",same);
    }
    engine::NrCorrectionSettings control;control.enabled=true;
    auto controls=control.constants();std::copy(controls.begin(),controls.end(),c.begin()+24);
    for(float gain:{0.f,1.f,2.f,5.f}){
        c[0]=gain;auto result=f.render(c);if(result.size()!=Count)return 2;
        bool gamut=true,alpha=true;double maxHue=0;
        for(unsigned i=0;i<Count;++i){const auto p=result[i];
            gamut&=finite(p)&&std::min({p.r,p.g,p.b})>=-1e-6f&&std::max({p.r,p.g,p.b})<=1.000001f;
            alpha&=p.a==base[i].a;
            const auto a=lab(base[i]),b=lab(p);const double ca=std::hypot(a[1],a[2]),cb=std::hypot(b[1],b[2]);
            if(ca>.02001&&cb>.001){const double sine=std::abs(a[1]*b[2]-a[2]*b[1])/(ca*cb);maxHue=std::max(maxHue,sine);}
            if(gain==5)csv<<"auto,"<<i<<','<<base[i].r<<','<<base[i].g<<','<<base[i].b<<','<<nr[i].r<<','<<nr[i].g<<','<<nr[i].b<<','<<p.r<<','<<p.g<<','<<p.b<<'\n';
        }
        check("auto output finite and within SDR gamut",gamut);check("source alpha preserved",alpha);
        std::cout<<"hue sine error gain="<<gain<<" maximum="<<maxHue<<std::endl;
        check("full hue protection retains source hue",maxHue<(fp16?.015:.003));
        if(gain==5){
            const auto raw=f.render(c,true);
            check("safe proportional detail keeps requested gain five",distance(result[0],raw[0])<(fp16?.0005f:5e-6f));
            check("white highlight shoulder avoids flat clipping",result[1].r>.985f&&result[1].r<1&&std::abs(result[1].r-result[1].g)<1e-5f);
            check("dark extrapolation retains positive shadow detail",std::min({result[3].r,result[3].g,result[3].b})>0);
            float jump=0;for(unsigned i=33;i<128;++i)jump=std::max(jump,distance(result[i-1],result[i]));
            std::cout<<"gray-axis maximum step="<<jump<<std::endl;check("gray-axis continuity",jump<(fp16?.0005f:1e-4f));
        }
    }
    c[0]=5;auto autoFive=f.render(c);c[0]=4.99999f;auto nearby=f.render(c);float jump=0;
    for(unsigned i=0;i<Count;++i)jump=std::max(jump,distance(autoFive[i],nearby[i]));
    check("gain five boundary is continuous",jump<(fp16?.001f:1e-4f));
    // Each manual spatial control must have an observable effect through the
    // actual cbuffer while keeping a valid output; zero controls are checked
    // separately against the archived raw path below.
    c[0]=5;
    for(unsigned field=25;field<=28;++field){auto start=c;start[field]=0;auto finish=c;finish[field]=1;
        auto a=f.render(start),b=f.render(finish);float response=0;bool valid=true;
        for(unsigned i=0;i<Count;++i){response=std::max(response,distance(a[i],b[i]));valid&=finite(a[i])&&finite(b[i]);}
        std::cout<<"manual field="<<field<<" response="<<response<<std::endl;
        check("manual spatial control is wired and responsive",response>.0001f&&valid);
    }
    c[0]=5;std::fill(c.begin()+24,c.end(),0);c[24]=1;auto zero=f.render(c),off=f.render(c,true);
    bool identical=true;for(unsigned i=0;i<Count;++i)identical&=distance(zero[i],off[i])==0;
    check("all manual protections zero retains raw gain five",identical);
    // Style-specific auto and each extra manual control through production
    // constants, measured independently in double precision.
    auto chromaError=[&](const std::vector<Pixel>& values){
        double error=0;for(unsigned i=0;i<Count;++i){auto a=lab(base[i]),b=lab(values[i]);
            error+=std::hypot(b[1]-a[1]*b[0]/std::max(a[0],1e-6),b[2]-a[2]*b[0]/std::max(a[0],1e-6));}
        return error/Count;
    };
    std::copy(controls.begin(),controls.end(),c.begin()+24);auto style0=f.render(c);double originalError=chromaError(style0);
    for(int style:{1,2}){
        auto profile=control.constants(false,style);std::copy(profile.begin(),profile.end(),c.begin()+24);
        auto result=f.render(c);double error=chromaError(result);bool legal=true;
        for(auto p:result)legal&=finite(p)&&std::min({p.r,p.g,p.b})>=0&&std::max({p.r,p.g,p.b})<=1;
        std::cout<<"auto style="<<style<<" source-relative color error="<<error<<" style0="<<originalError<<std::endl;
        check("styles 1/2 preserve more source color without disabling NR",legal&&error<originalError*.4&&distance(result[0],base[0])>0);
    }
    for(unsigned field=32;field<=35;++field){auto raw=c;std::fill(raw.begin()+24,raw.end(),0);raw[24]=1;
        auto isolated=raw;isolated[field]=1;auto before=f.render(raw),after=f.render(isolated);float response=0;
        for(unsigned i=0;i<Count;++i)response=std::max(response,distance(before[i],after[i]));
        check("extra manual spatial control has independent response",response>.0001f);
        if(field==32){auto o=lab(base[80]),p=lab(after[80]);
            check("neutral protection repairs gray tint without hue direction",std::hypot(p[1],p[2])<(fp16?.001:.00001)&&p[0]>0);}
        if(field==33)check("full color retention preserves source RGB ratios",chromaError(after)<(fp16?.0003:.000003));
        if(field==34){double error=0;for(unsigned i=0;i<Count;++i)error=std::max(error,std::abs(lab(after[i])[0]-lab(base[i])[0]));
            check("full lightness retention keeps source lightness independently",error<(fp16?.0008:.00001));}
        if(field==35)check("shadow control restores dark detail",lab(after[3])[0]>lab(before[3])[0]);
    }
    control.autoAmount=0;auto none=control.constants();std::copy(none.begin(),none.end(),c.begin()+24);
    auto amountZero=f.render(c);identical=true;for(unsigned i=0;i<Count;++i)identical&=distance(amountZero[i],off[i])==0;
    check("automatic amount zero retains exact original gain five",identical);control.autoAmount=1;
    // A full user exclusion bypasses every colour control.
    std::copy(controls.begin(),controls.end(),c.begin()+24);c[5]=1;c[6]=0;c[8]=0;c[9]=0;c[10]=1;c[11]=1;
    auto protectedFrame=f.render(c);identical=true;
    for(unsigned i=0;i<Count;++i)identical&=distance(protectedFrame[i],base[i])==0;
    check("protection rectangle returns exact source",identical);c[5]=0;
    f.fill(f.neural.Get(),base);auto identity=f.render(c);identical=true;
    for(unsigned i=0;i<Count;++i)identical&=distance(identity[i],base[i])==0;
    check("zero NR residual is exact identity",identical);
    nr[100].r=std::numeric_limits<float>::quiet_NaN();f.fill(f.neural.Get(),nr);
    auto repaired=f.render(c);check("nonfinite NR falls back to finite original",distance(repaired[100],base[100])==0);
    // HDR palette: valid wide-gamut BT.2020 maps to signed BT.709, with peaks
    // above SDR white. No comparison against a display or an HDR-trained model.
    for(unsigned i=0;i<Count;++i){
        Pixel wide={.01f+40*unit(random),.01f+40*unit(random),.01f+40*unit(random),base[i].a};
        base[i]=to709(wide);wide.r*=.96f;wide.g*=1.02f;wide.b*=1.03f;nr[i]=to709(wide);
    }
    base[0]=to709({8,.02f,.02f,.7f});nr[0]=to709({8.2f,.025f,.025f,.7f});
    if(fp16){quantize(base);quantize(nr);}
    f.fill(f.base.Get(),base);f.fill(f.neural.Get(),nr);c[7]=1;auto hdr=f.render(c);
    bool legal=true,alpha=true;
    for(unsigned i=0;i<Count;++i){Pixel p=to2020(hdr[i]),o=to2020(base[i]);float peak=std::min(125.f,std::max(203.f/80.f,std::max({o.r,o.g,o.b})));
        const float tolerance=fp16?.002f*std::max(1.f,peak):.001f;
        legal&=finite(p)&&std::min({p.r,p.g,p.b})>=-tolerance&&std::max({p.r,p.g,p.b})<=peak+tolerance;alpha&=hdr[i].a==base[i].a;
        csv<<"hdr,"<<i<<','<<base[i].r<<','<<base[i].g<<','<<base[i].b<<','<<nr[i].r<<','<<nr[i].g<<','<<nr[i].b<<','<<hdr[i].r<<','<<hdr[i].g<<','<<hdr[i].b<<'\n';
    }
    check("HDR BT2020 gamut and source peak envelope",legal);check("HDR alpha preserved",alpha);
    check("HDR retains signed wide gamut and above-white values",hdr[0].r>1&&std::min(hdr[0].g,hdr[0].b)<0);
    f.fill(f.neural.Get(),base);identity=f.render(c);identical=true;
    for(unsigned i=0;i<Count;++i)identical&=distance(identity[i],base[i])==0;
    check("HDR zero residual retains exact signed source",identical);
    f.ring.drainQueue();ComPtr<ID3D12InfoQueue> info;
    if(FAILED(f.ctx.device()->QueryInterface(IID_PPV_ARGS(&info))))return 2;
    unsigned errors=0,warnings=0;
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){
        SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(FAILED(info->GetMessage(i,message,&size)))return 2;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<std::endl;}
        if(message->Severity==D3D12_MESSAGE_SEVERITY_WARNING){++warnings;std::cerr<<message->pDescription<<std::endl;}
    }
    check("D3D12 debug layer has no errors or warnings",errors==0&&warnings==0);
    std::ofstream report(std::filesystem::path(argv[2])/L"shader-result.json");
    report<<"{\"passed\":"<<(ok?"true":"false")<<",\"fp16\":"<<(fp16?"true":"false")<<",\"checks\":"<<checks<<",\"palettePixels\":"<<Count<<",\"debugErrors\":"<<errors<<",\"debugWarnings\":"<<warnings<<"}\n";
    return ok?0:1;
}
