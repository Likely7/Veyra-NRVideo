// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>
namespace veyra::pipeline::rdna3 {
constexpr uint32_t kMagic=0x33524e56, kVersion=1;
enum class Op:uint32_t { Init=1, Evaluate=2, Stop=3, Reply=4 };
struct Header {uint32_t magic=kMagic,version=kVersion,bytes=0;Op op=Op::Init;uint64_t sequence=0;};
struct Init {
    Header h{ kMagic,kVersion,sizeof(Init),Op::Init,0 };
    uint64_t adapterLuid=0;
    uint32_t width=0,height=0,preset=2,reserved=0;
    // Display-proxy input/output RGBA16F (the upstream CUDA network contract),
    // zero depth R32F, zero motion RG16F, producer/done fences.
    // Last handle: GPU-written validity buffer; cleared by each producer.
    std::array<uint64_t,7> handles{};
    std::array<wchar_t,1024> runtime{},data{},log{};
};
struct Evaluate {Header h{kMagic,kVersion,sizeof(Evaluate),Op::Evaluate,0};float intensity=1; /* host transfer strength, not feature recreation */ uint32_t reset=1;};
struct Reply {Header h{kMagic,kVersion,sizeof(Reply),Op::Reply,0};int32_t result=0;std::array<char,768> message{};};
constexpr bool validHeader(const Header& h,Op op,uint32_t bytes){return h.magic==kMagic&&h.version==kVersion&&h.op==op&&h.bytes==bytes;}
constexpr bool validExtent(uint32_t w,uint32_t h){return w&&h&&w<=2560&&h<=1080&&uint64_t(w)*h<=1920ull*1080;}
static_assert(std::is_trivially_copyable_v<Init> && sizeof(Init)<16384);
}
