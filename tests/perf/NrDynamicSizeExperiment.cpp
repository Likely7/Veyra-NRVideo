#include "NrExperimentalFixture.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>

// E1: maximum-created feature vs independently-created exact-size feature.
// CLI: runtime-dir output-dir subrect|dimensions full|small
int wmain(int argc,wchar_t** argv) {
    using namespace nrperf;
    if(argc!=5)return 2;
    const bool dimensions=std::wstring_view(argv[3])==L"dimensions";
    const bool full=std::wstring_view(argv[4])==L"full";
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    Session s;if(!s.init(argv[1]))return 2;
    bool pass=true;std::ofstream csv(out/L"observations.csv");
    csv<<"step,w,h,evaluateOK,changedActivePixels,unchangedSentinelPixels,outsideWrites,referenceDifferentBytes,referenceMeanAbsoluteError,dynamicGpuMs,referenceGpuMs\n";
    {
        Feature dynamic(s);if(!dynamic.create(s.ring,1920,1080))return 2;
        const uint32_t widths[]={1920,1632,1344,1056,768,1920};
        for(unsigned step=0;step<std::size(widths);++step){
            const uint32_t w=widths[step],h=(w*9/16)&~1u,tw=full?1920:w,th=full?1080:h;
            const bool setup=dynamic.textures(s.ring,tw,th,w,h);
            bool evaluated=setup;
            for(unsigned frame=0;frame<5&&evaluated;++frame)evaluated=dynamic.evaluate(s.ring,w,h,frame==0,dimensions);
            const auto actual=setup?dynamic.read(s.ring):std::vector<uint8_t>{};
            Feature reference(s);bool refOK=reference.create(s.ring,w,h)&&reference.textures(s.ring,w,h,w,h);
            for(unsigned frame=0;frame<5&&refOK;++frame)refOK=reference.evaluate(s.ring,w,h,frame==0);
            const auto expected=refOK?reference.read(s.ring):std::vector<uint8_t>{};
            uint64_t changed=0,sentinel=0,outside=0,different=0,totalError=0;
            if(actual.size()==size_t(tw)*th*4&&expected.size()==size_t(w)*h*4){
                for(uint32_t y=0;y<th;++y)for(uint32_t x=0;x<tw;++x){const auto i=(size_t(y)*tw+x)*4;
                    const bool untouched=actual[i]==17&&actual[i+1]==23&&actual[i+2]==29&&actual[i+3]==255;
                    if(x<w&&y<h){changed+=!untouched;sentinel+=untouched;const auto j=(size_t(y)*w+x)*4;
                        for(unsigned c=0;c<4;++c){different+=actual[i+c]!=expected[j+c];totalError+=unsigned(std::abs(int(actual[i+c])-int(expected[j+c])));}}
                    else outside+=!untouched;
                }
            }
            const double error=double(totalError)/(double(w)*h*4);
            csv<<step<<','<<w<<','<<h<<','<<evaluated<<','<<changed<<','<<sentinel<<','<<outside<<','<<different<<','<<error<<','<<dynamic.lastGpuEvaluateMs<<','<<reference.lastGpuEvaluateMs<<'\n';csv.flush();
            auto dump=[&](const std::wstring& name,const auto& data){std::ofstream f(out/name,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));};
            dump(L"dynamic-"+std::to_wstring(step)+L".rgba",actual);dump(L"reference-"+std::to_wstring(step)+L".rgba",expected);
            std::printf("STEP step=%u extent=%ux%u textures=%ux%u evaluated=%d reference=%d changed=%llu sentinel=%llu outside=%llu differentBytes=%llu mae=%.6f\n",
                step,w,h,tw,th,evaluated,refOK,changed,sentinel,outside,different,error);
            pass=pass&&evaluated&&refOK&&changed>size_t(w)*h/2&&sentinel==0&&outside==0&&different==0;
            if(!evaluated)break;
        }
    }
    s.ring.drainQueue();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(!hrOK(s.device.device()->QueryInterface(IID_PPV_ARGS(&debug)),"Debug.Query"))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);
        std::vector<uint8_t> data(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(!hrOK(debug->GetMessage(i,m,&n),"Debug.Read"))return 2;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::printf("DEBUG_ERROR %s\n",m->pDescription);}}
    std::printf("RESULT pass=%d debugErrors=%u deviceRemoved=0x%08X\n",pass,errors,unsigned(s.device.device()->GetDeviceRemovedReason()));
    return pass&&errors==0?0:1;
}
