// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string_view>
namespace veyra::pipeline {
enum class AmdNrProvider { Unsupported, LmxxfRdna4, RedDukeRdna3 };
// Hardware routing only. Runtime/driver initialization is checked separately.
constexpr AmdNrProvider amdNrProvider(uint32_t vendor,std::wstring_view name){
    if(vendor!=0x1002)return AmdNrProvider::Unsupported;
    for(size_t i=0;i+2<name.size();++i){
        if((name[i]!=L'R'&&name[i]!=L'r')||(name[i+1]!=L'X'&&name[i+1]!=L'x'))continue;
        size_t n=i+2;while(n<name.size()&&(name[n]==L' '||name[n]==L'\t'))++n;
        if(n<name.size()&&name[n]==L'7')return AmdNrProvider::RedDukeRdna3;
        if(n+1<name.size()&&name[n]==L'9'&&name[n+1]==L'0')return AmdNrProvider::LmxxfRdna4;
    }
    return AmdNrProvider::Unsupported;
}
}
