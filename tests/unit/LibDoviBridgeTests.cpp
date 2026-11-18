#include "veyra/source/LibDoviBridge.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace fs=std::filesystem;
int wmain(int argc,wchar_t**argv){
    if(argc!=2)return 2;
    unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<"\n";}};
    for(const auto* name:{L"profile5.bin",L"profile8.bin",L"fel_orig.bin",L"fel_rpu.bin",L"mel_orig.bin",L"poly_coef_int_logic.bin"}){
        const auto path=fs::path(argv[1])/name;
        std::ifstream in(path,std::ios::binary);if(!in){std::cerr<<"missing fixture "<<path.filename().string()<<"\n";return 2;}
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),{});
        // These upstream fixtures contain one Annex-B RPU. Keep only the first NAL.
        size_t first=0;
        if(data.size()>4&&data[0]==0&&data[1]==0){first=data[2]==1?3:4;}
        size_t end=data.size();
        for(size_t n=first+3;n+3<data.size();++n)if(data[n]==0&&data[n+1]==0&&(data[n+2]==1||(data[n+2]==0&&data[n+3]==1))){end=n;break;}
        auto* parsed=dovi_parse_unspec62_nalu(data.data()+first,end-first);
        auto* head=parsed?dovi_rpu_get_header(parsed):nullptr;
        auto* map=parsed?dovi_rpu_get_data_mapping(parsed):nullptr;
        auto* metadata=veyra::source::allocDoviMetadata();
        check(head&&map&&metadata,"upstream fixture and allocation");
        if(!head||!map||!metadata)return 2;
        check(veyra::source::decodeRpuWithLibDovi(data.data()+first,end-first,metadata),"bridge accepts valid RPU");
        check(av_dovi_get_header(metadata)->coef_log2_denom==head->coefficient_log2_denom,"denominator ABI");
        check((reinterpret_cast<uintptr_t>(av_dovi_get_mapping(metadata))%alignof(AVDOVIDataMapping))==0,"FFmpeg mapping alignment");
        const int64_t scale=int64_t(1)<<head->coefficient_log2_denom;
        auto* dst=av_dovi_get_mapping(metadata);
        for(int c=0;c<3;++c){
            const auto& src=map->curves[c];const auto& curve=dst->curves[c];
            check(curve.num_pivots==src.num_pivots_minus2+2,"pivot count");
            unsigned sum=0;for(size_t i=0;i<src.pivots.len;++i){sum+=src.pivots.data[i];check(curve.pivots[i]==sum,"pivot deltas accumulate");}
            if(src.mmr){const auto& mmr=*src.mmr;
                for(size_t i=0;i<mmr.mmr_constant.len;++i){
                    check(curve.mmr_constant[i]==mmr.mmr_constant_int.data[i]*scale+int64_t(mmr.mmr_constant.data[i]),"MMR constant integer and fraction");
                    if(i<mmr.mmr_coef.len) {
                        const size_t j=i;
                        const auto& frac=*mmr.mmr_coef.list[j];const auto& whole=*mmr.mmr_coef_int.list[j];
                        for(size_t order=0;order<frac.len;++order)for(size_t k=0;k<frac.list[order]->len;++k)
                            check(curve.mmr_coef[j][order][k]==whole.list[order]->data[k]*scale+int64_t(frac.list[order]->data[k]),"MMR coefficients");
                    }
                }
            }
            if(src.polynomial){const auto& p=*src.polynomial;
                for(size_t i=0;i<p.poly_coef.len;++i)for(size_t k=0;k<p.poly_coef.list[i]->len;++k)
                    check(curve.poly_coef[i][k]==p.poly_coef_int.list[i]->data[k]*scale+int64_t(p.poly_coef.list[i]->data[k]),"polynomial integer and fraction");
            }
        }
        check(!veyra::source::decodeRpuWithLibDovi(data.data(),3,metadata),"truncated RPU rejected");
        check(!veyra::source::decodeRpuWithLibDovi(nullptr,9,metadata),"null RPU rejected");
        av_free(metadata);dovi_rpu_free_header(head);dovi_rpu_free_data_mapping(map);dovi_rpu_free(parsed);
        std::cout<<path.filename().string()<<" checked\n";
    }
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
