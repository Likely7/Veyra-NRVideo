#pragma once
#include "veyra/Log.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <bit>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace veyra::engine {
// The present list uses one scissored triangle and packed glyph constants.
// No extra window, font texture, CPU readback or queue submission.
class FullscreenFrameStats {
public:
    bool needsUpdate() const {
        return std::chrono::steady_clock::now()>=nextUpdate_;
    }
    void update(std::optional<double> fps,std::optional<double> low) {
        const auto now=std::chrono::steady_clock::now();
        if(now<nextUpdate_)return;
        nextUpdate_=now+std::chrono::milliseconds(250);
        const auto rate=[](std::optional<double> value) {
            return value&&std::isfinite(*value)&&*value>0
                ? std::format("{:.1f}",std::min(*value,9999.9)) : std::string("--");
        };
        const auto fpsText="FPS "+rate(fps),lowText="1% LOW "+rate(low);
        if(rows_[0]!=fpsText||rows_[1]!=lowText){rows_={fpsText,lowText};layoutDirty_=true;}
    }
    void resetPass(){pass_={};passFailed_=false;}
    bool prepare(ID3D12Device* device,DXGI_FORMAT format) {
        if(pass_.pso)return true;
        if(passFailed_)return false;
        std::vector<uint8_t> vs,ps;
        if(!pipeline::loadShaderBytes("FullscreenFrameStats_vs.dxil",vs)||
           !pipeline::loadShaderBytes("FullscreenFrameStats_ps.dxil",ps)||
           !pass_.create(device,vs,ps,1,format,unsigned(constants_.size()))) {
            pass_={};passFailed_=true;
            log::warn("frame-stats","fullscreen stats shader is unavailable");
            return false;
        }
        pipeline::makeSrv(device,nullptr,DXGI_FORMAT_R8G8B8A8_UNORM,pass_.heap->GetCPUDescriptorHandleForHeapStart());
        constants_[3]=std::bit_cast<uint32_t>(format==DXGI_FORMAT_R10G10B10A2_UNORM?0.58f:
                      format==DXGI_FORMAT_R16G16B16A16_FLOAT?2.5f:0.96f);
        return true;
    }
    void draw(ID3D12GraphicsCommandList* list,D3D12_CPU_DESCRIPTOR_HANDLE target,
              unsigned width,unsigned height,int corner) {
        if(corner<0||width<96||height<40||!pass_.pso)return;
        if(layoutDirty_||width!=width_||height!=height_||corner!=corner_)
            layout(width,height,corner);
        list->OMSetRenderTargets(1,&target,FALSE,nullptr);
        list->RSSetScissorRects(1,&panel_);
        ID3D12DescriptorHeap* heaps[]={pass_.heap.Get()};
        list->SetDescriptorHeaps(1,heaps);
        list->SetGraphicsRootSignature(pass_.rootSig.Get());
        list->SetPipelineState(pass_.pso.Get());
        list->SetGraphicsRoot32BitConstants(0,unsigned(constants_.size()),constants_.data(),0);
        list->SetGraphicsRootDescriptorTable(1,pass_.heap->GetGPUDescriptorHandleForHeapStart());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(3,1,0,0);
    }
private:
    using Glyph=std::array<unsigned,5>;
    static Glyph glyph(char ch) {
        switch(ch){
        case '0': return {7,5,5,5,7};
        case '1': return {2,6,2,2,7};
        case '2': return {7,1,7,4,7};
        case '3': return {7,1,7,1,7};
        case '4': return {5,5,7,1,1};
        case '5': return {7,4,7,1,7};
        case '6': return {7,4,7,5,7};
        case '7': return {7,1,1,1,1};
        case '8': return {7,5,7,5,7};
        case '9': return {7,5,7,1,7};
        case 'F': return {7,4,6,4,4};
        case 'P': return {7,5,7,4,4};
        case 'S': return {7,4,7,1,7};
        case 'L': return {4,4,4,4,7};
        case 'O': return {7,5,5,5,7};
        case 'W': return {5,5,7,7,5};
        case '%': return {5,1,2,4,5};
        case '.': return {0,0,0,0,2};
        case '-': return {0,0,7,0,0};
        default:  return {};
        }
    }
    void layout(unsigned width,unsigned height,int corner) {
        const LONG scale=std::clamp<LONG>(LONG(height)/540,1,std::min<LONG>(6,LONG(width)/80));
        const LONG padding=3*scale,margin=7*scale;
        const LONG w=LONG(4*std::max(rows_[0].size(),rows_[1].size())-1)*scale+2*padding;
        const LONG h=12*scale+2*padding;
        const LONG left=(corner&1)?LONG(width)-margin-w:margin;
        const LONG top=(corner&2)?LONG(height)-margin-h:margin;
        panel_={left,top,left+w,top+h};
        constants_[0]=unsigned(left);constants_[1]=unsigned(top);constants_[2]=unsigned(scale);
        std::fill(constants_.begin()+4,constants_.end(),0u);
        for(unsigned line=0;line<rows_.size();++line) {
            const auto& text=rows_[line];
            for(size_t character=0;character<text.size();++character) {
                const auto bits=glyph(text[character]);
                uint32_t packed=0;
                for(unsigned row=0;row<bits.size();++row)packed|=bits[row]<<(row*3);
                const size_t index=line*10+character;
                constants_[4+index/2]|=packed<<((index%2)*16);
            }
        }
        width_=width;height_=height;corner_=corner;layoutDirty_=false;
    }
    std::array<std::string,2> rows_{"FPS --","1% LOW --"};
    std::array<uint32_t,16> constants_{};
    D3D12_RECT panel_{};
    pipeline::GraphicsPass pass_;
    bool passFailed_=false;
    unsigned width_=0,height_=0;
    int corner_=-1;
    bool layoutDirty_=true;
    std::chrono::steady_clock::time_point nextUpdate_{};
};
}
