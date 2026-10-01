#include "veyra/engine/ColorLut.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
namespace veyra::engine {
namespace {
std::string trim(std::string s){
    const auto first=s.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    const auto last=s.find_last_not_of(" \t\r\n");
    return s.substr(first,last-first+1);
}
bool parseFloat(const std::string& text,float& value){
    if(text.empty())return false;
    char* end=nullptr;
    const float parsed=std::strtof(text.c_str(),&end);
    if(end==text.c_str()||*end||!std::isfinite(parsed))return false;
    value=parsed;
    return true;
}
bool parseInt(const std::string& text,int& value){
    if(text.empty())return false;
    char* end=nullptr;
    const long parsed=std::strtol(text.c_str(),&end,10);
    if(end==text.c_str()||*end||parsed<1||parsed>4096)return false;
    value=int(parsed);
    return true;
}
// Trilinear sample of a parsed grid at 0..1 coordinates (clamped).
void sampleGrid(const ColorLutData& lut,double r,double g,double b,float out[3]){
    const int n=lut.size;
    const double coord[3]={std::clamp(r,0.0,1.0)*(n-1),std::clamp(g,0.0,1.0)*(n-1),std::clamp(b,0.0,1.0)*(n-1)};
    const int base[3]={std::min(int(coord[0]),n-2<0?0:n-2),std::min(int(coord[1]),n-2<0?0:n-2),std::min(int(coord[2]),n-2<0?0:n-2)};
    const double f[3]={coord[0]-base[0],coord[1]-base[1],coord[2]-base[2]};
    for(int c=0;c<3;++c){
        double value=0;
        for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
            const int x=std::min(base[0]+dx,n-1),y=std::min(base[1]+dy,n-1),z=std::min(base[2]+dz,n-1);
            const double weight=(dx?f[0]:1-f[0])*(dy?f[1]:1-f[1])*(dz?f[2]:1-f[2]);
            const auto index=(std::size_t(z)*std::size_t(n)*std::size_t(n)+std::size_t(y)*std::size_t(n)+std::size_t(x))*3+std::size_t(c);
            value+=weight*lut.rgb[index];
        }
        out[c]=float(value);
    }
}
std::string sha256Hex(const std::vector<uint8_t>& bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(FAILED(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)))return {};
    std::vector<uint8_t> hash(32);
    const bool ok=SUCCEEDED(BCryptHash(algorithm,const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>("")),0,
        const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),hash.data(),ULONG(hash.size())));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok)return {};
    static const char* digits="0123456789abcdef";
    std::string text;
    for(uint8_t byte:hash){text.push_back(digits[byte>>4]);text.push_back(digits[byte&0xF]);}
    return text;
}
}
ColorLutData parseCube(std::string_view text){
    ColorLutData lut;
    int size1d=0,size3d=0;
    double domainMin[3]={0,0,0},domainMax[3]={1,1,1};
    std::vector<std::array<float,3>> rows;
    std::istringstream stream{std::string(text)};
    std::string line;
    while(std::getline(stream,line)){
        const auto comment=line.find('#');
        if(comment!=std::string::npos)line.resize(comment);
        line=trim(line);
        if(line.empty())continue;
        std::istringstream fields(line);
        std::string keyword;
        fields>>keyword;
        if(keyword=="TITLE"){
            std::string rest=trim(line.substr(5));
            if(rest.size()>=2&&rest.front()=='"'&&rest.back()=='"')rest=rest.substr(1,rest.size()-2);
            lut.title=rest;
        }else if(keyword=="LUT_1D_SIZE"){
            std::string value;fields>>value;
            if(!parseInt(value,size1d)||size1d<2||size1d>4096)return {};
        }else if(keyword=="LUT_3D_SIZE"){
            std::string value;fields>>value;
            if(!parseInt(value,size3d)||size3d<2||size3d>64)return {};
        }else if(keyword=="DOMAIN_MIN"||keyword=="DOMAIN_MAX"){
            double values[3]{};
            for(int i=0;i<3;++i){std::string value;fields>>value;float parsed=0;if(!parseFloat(value,parsed))return {};values[i]=parsed;}
            if(keyword=="DOMAIN_MIN")for(int i=0;i<3;++i)domainMin[i]=values[i];
            else for(int i=0;i<3;++i)domainMax[i]=values[i];
        }else{
            // A data row: three floats. The keyword extraction above already
            // consumed the first component.
            std::array<float,3> row{};
            float parsed[3]{};
            if(!parseFloat(keyword,parsed[0]))return {};
            for(int i=1;i<3;++i){std::string value;fields>>value;if(!parseFloat(value,parsed[i]))return {};}
            std::string extra;
            if(fields>>extra)return {};
            for(int i=0;i<3;++i)row[std::size_t(i)]=parsed[i];
            rows.push_back(row);
        }
    }
    for(int i=0;i<3;++i)if(!(domainMax[i]>domainMin[i]))return {};
    for(int i=0;i<3;++i){lut.domainMin[i]=domainMin[i];lut.domainMax[i]=domainMax[i];}
    if(size3d){
        const std::size_t expected=std::size_t(size3d)*std::size_t(size3d)*std::size_t(size3d);
        if(rows.size()!=expected)return {};
        lut.size=size3d;
        lut.rgb.resize(expected*3);
        for(std::size_t i=0;i<expected;++i)for(int c=0;c<3;++c)lut.rgb[i*3+std::size_t(c)]=rows[i][std::size_t(c)];
        return lut;
    }
    if(size1d){
        if(rows.size()!=std::size_t(size1d))return {};
        // Expand a 1D LUT to a 32^3 grid so the shader keeps one code path: the
        // three output ports come from the same ramp.
        ColorLutData ramp;ramp.size=size1d;ramp.rgb.resize(std::size_t(size1d)*3);
        for(std::size_t i=0;i<std::size_t(size1d);++i)for(int c=0;c<3;++c)ramp.rgb[i*3+std::size_t(c)]=rows[i][std::size_t(c)];
        const int expanded=32;
        lut.size=expanded;lut.title=std::move(ramp.title);
        lut.rgb.resize(std::size_t(expanded)*expanded*expanded*3);
        for(int r=0;r<expanded;++r)for(int g=0;g<expanded;++g)for(int b=0;b<expanded;++b){
            const std::size_t index=(std::size_t(b)*expanded*expanded+std::size_t(g)*expanded+std::size_t(r))*3;
            const double coord[3]={double(r)/(expanded-1),double(g)/(expanded-1),double(b)/(expanded-1)};
            for(int c=0;c<3;++c){
                const double x=coord[c]*(size1d-1);
                const int i0=std::min(int(x),size1d-2<0?0:size1d-2),i1=std::min(i0+1,size1d-1);
                const double f=x-i0;
                lut.rgb[index+std::size_t(c)]=float(ramp.rgb[std::size_t(i0)*3+std::size_t(c)]*(1-f)+ramp.rgb[std::size_t(i1)*3+std::size_t(c)]*f);
            }
        }
        return lut;
    }
    return {};
}
ColorLutData normalizeCubeDomain(const ColorLutData& lut){
    if(!lut.valid())return {};
    // The grid index i/(n-1) already corresponds to the declared DOMAIN span, so
    // the shader's 0..1 coordinate maps onto the domain without a resample: only
    // the bookkeeping fields are reset so callers see a normalised LUT.
    ColorLutData out=lut;
    out.domainMin[0]=out.domainMin[1]=out.domainMin[2]=0;
    out.domainMax[0]=out.domainMax[1]=out.domainMax[2]=1;
    return out;
}ColorLutStore::ColorLutStore(std::filesystem::path dataDirectory)
    :folder_(std::move(dataDirectory)/"luts"){}
std::vector<std::wstring> ColorLutStore::list()const{
    std::vector<std::wstring> names;
    std::error_code ec;
    if(!std::filesystem::exists(folder_,ec))return names;
    for(const auto& entry:std::filesystem::directory_iterator(folder_,ec)){
        if(ec)break;
        if(!entry.is_regular_file())continue;
        if(entry.path().extension()!=L".cube")continue;
        names.push_back(entry.path().filename().wstring());
    }
    std::sort(names.begin(),names.end());
    return names;
}
bool ColorLutStore::validName(std::wstring_view name)const{
    if(name.empty()||name.size()>120)return false;
    if(name.find_first_of(L"\\/:*?\"<>|")!=std::wstring_view::npos)return false;
    return std::filesystem::path(name).extension()==L".cube";
}
bool ColorLutStore::importFile(const std::filesystem::path& source,std::wstring& nameOut,std::string& error)const{
    error.clear();
    std::error_code ec;
    if(!std::filesystem::exists(source,ec)){error="Source file does not exist";return false;}
    const auto bytes=std::filesystem::file_size(source,ec);
    if(ec||bytes==0||bytes>8u*1024u*1024u){error="File is empty or exceeds 8MiB";return false;}
    std::ifstream file(source,std::ios::binary);
    if(!file){error="Could not read source file";return false;}
    std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto lut=parseCube(text);
    if(!lut.valid()){error="Not a valid .cube (size/line count/value validation failed)";return false;}
    auto name=source.filename().wstring();
    if(!validName(name)){error="File name is not accepted (must be .cube and must not contain path separators)";return false;}
    std::filesystem::create_directories(folder_,ec);
    if(ec){error="Could not create luts directory";return false;}
    const auto target=folder_/name;
    std::filesystem::copy_file(source,target,std::filesystem::copy_options::overwrite_existing,ec);
    if(ec){error="Copying to the luts directory failed";return false;}
    // Manifest keeps the identity of every imported file next to the data; the
    // export worker resolves the same folder, so names are enough at runtime.
    std::ofstream manifest(folder_/"manifest.v1",std::ios::app);
    if(manifest){
        const std::vector<uint8_t> raw(text.begin(),text.end());
        const auto hash=sha256Hex(raw);
        manifest<<std::filesystem::path(name).string()<<' '<<lut.size<<' '<<bytes<<' '<<(hash.empty()?"nohash":hash)<<'\n';
    }
    nameOut=name;
    return true;
}
bool ColorLutStore::resolve(const std::wstring& name,ColorLutData& out)const{
    if(!validName(name))return false;
    std::ifstream file(folder_/name,std::ios::binary);
    if(!file)return false;
    std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    out=normalizeCubeDomain(parseCube(text));
    return out.valid();
}
} // namespace veyra::engine
