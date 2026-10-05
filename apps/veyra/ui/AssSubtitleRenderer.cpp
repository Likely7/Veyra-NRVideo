#include "AssSubtitleRenderer.h"

#include "veyra/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

extern "C" {
#include <ass/ass.h>
}

namespace veyra::ui {
namespace {
// libass is chatty at verbose levels; keep errors/warnings, rate limited.
void onMessage(int level,const char* format,va_list args,void*){
    if(level>2)return;
    static auto nextLog=std::chrono::steady_clock::time_point{};
    static int suppressed=0;
    const auto now=std::chrono::steady_clock::now();
    if(now<nextLog){++suppressed;return;}
    nextLog=now+std::chrono::milliseconds(500);
    char text[512]{};
    std::vsnprintf(text,sizeof(text),format,args);
    log::warn("libass",std::format("level={} {}{}",level,text,suppressed?std::format(" (+{} suppressed)",suppressed):std::string{}));
    suppressed=0;
}
} // namespace

struct AssSubtitleRenderer::Impl {
    ASS_Library* library=nullptr;
    ASS_Renderer* renderer=nullptr;
    ASS_Track* track=nullptr;
    std::shared_ptr<const engine::SubtitleAssData> data;
    std::wstring source;
    std::string header;
    size_t fedEvents=0,fontsAdded=0;
    bool fontsReady=false;
    ASS_Image* images=nullptr;
    int frameW=0,frameH=0,storageW=0,storageH=0;
    double fontScale=1.0;

    Impl(){
        library=ass_library_init();
        if(!library){log::error("libass","ass_library_init failed");return;}
        ass_set_message_cb(library,onMessage,nullptr);
        ass_set_extract_fonts(library,1); // [Fonts] sections embedded in the script
        renderer=ass_renderer_init(library);
        if(!renderer)log::error("libass","ass_renderer_init failed");
    }
    ~Impl(){
        images=nullptr;
        if(track)ass_free_track(track);
        if(renderer)ass_renderer_done(renderer);
        if(library)ass_library_done(library);
    }
    void addFonts(const engine::SubtitleAssData& script){
        bool added=false;
        for(;fontsAdded<script.fonts.size();++fontsAdded){
            const auto& font=script.fonts[fontsAdded];
            if(font.data.empty()||font.data.size()>size_t(INT32_MAX))continue;
            ass_add_font(library,font.name.c_str(),font.data.data(),int(font.data.size()));
            added=true;
        }
        // Memory fonts reach a renderer at its next ass_set_fonts call. The
        // system provider (DirectWrite, GDI fallback) resolves everything else.
        if(added||!fontsReady){
            const auto start=std::chrono::steady_clock::now();
            ass_set_fonts(renderer,nullptr,"Microsoft YaHei",ASS_FONTPROVIDER_AUTODETECT,nullptr,1);
            log::info("libass",std::format("fonts configured container={} ms={:.1f}",fontsAdded,
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));
            fontsReady=true;
        }
    }
    void feed(const engine::SubtitleAssData& script){
        for(;fedEvents<script.events.size();++fedEvents){
            const auto& event=script.events[fedEvents];
            if(event.chunk.empty()||event.chunk.size()>size_t(INT32_MAX))continue;
            ass_process_chunk(track,event.chunk.data(),int(event.chunk.size()),event.startMs,event.durationMs);
        }
    }
    void rebuild(const engine::SubtitleAssData& script){
        images=nullptr;
        if(track){ass_free_track(track);track=nullptr;}
        fedEvents=0;fontsAdded=0;
        addFonts(script);
        if(!script.script.empty()){
            std::vector<char> copy(script.script.begin(),script.script.end()); // ass_read_memory parses in place
            track=ass_read_memory(library,copy.data(),copy.size(),nullptr);
        }else{
            track=ass_new_track(library);
            if(track&&!script.header.empty())ass_process_codec_private(track,script.header.data(),int(script.header.size()));
        }
        if(!track){log::error("libass",std::format("track creation failed source={}",std::string(script.source.begin(),script.source.end())));return;}
        source=script.source;header=script.header;
        if(script.script.empty())feed(script);
        log::info("libass",std::format("track ready events={} styles={} playRes={}x{} fonts={}",
            track->n_events,track->n_styles,track->PlayResX,track->PlayResY,fontsAdded));
    }
};

AssSubtitleRenderer::AssSubtitleRenderer():p_(std::make_unique<Impl>()){}
AssSubtitleRenderer::~AssSubtitleRenderer()=default;

bool AssSubtitleRenderer::prepare(const std::shared_ptr<const engine::SubtitleAssData>& data){
    auto& p=*p_;
    if(!p.library||!p.renderer||!data)return false;
    if(data==p.data)return p.track!=nullptr;
    const bool grownEmbedded=p.track&&data->script.empty()&&data->source==p.source&&data->header==p.header&&
        data->events.size()>=p.fedEvents&&data->fonts.size()>=p.fontsAdded;
    if(grownEmbedded){p.addFonts(*data);p.feed(*data);}
    else p.rebuild(*data);
    p.data=data;
    return p.track!=nullptr;
}

int AssSubtitleRenderer::render(int64_t timeMs,int frameW,int frameH,int storageW,int storageH,double fontScale){
    auto& p=*p_;
    p.images=nullptr;
    if(!p.track||frameW<1||frameH<1)return -1;
    frameW=std::min(frameW,16384);frameH=std::min(frameH,16384);
    bool resized=false;
    if(frameW!=p.frameW||frameH!=p.frameH){ass_set_frame_size(p.renderer,frameW,frameH);p.frameW=frameW;p.frameH=frameH;resized=true;}
    storageW=std::max(storageW,1);storageH=std::max(storageH,1);
    if(storageW!=p.storageW||storageH!=p.storageH){ass_set_storage_size(p.renderer,storageW,storageH);p.storageW=storageW;p.storageH=storageH;resized=true;}
    if(fontScale!=p.fontScale){ass_set_font_scale(p.renderer,fontScale);p.fontScale=fontScale;resized=true;}
    int change=0;
    p.images=ass_render_frame(p.renderer,p.track,timeMs,&change);
    return resized?2:change;
}

bool AssSubtitleRenderer::hasImages()const{return p_->images!=nullptr;}

void AssSubtitleRenderer::composite(uint32_t* canvas,int canvasW,int canvasH,int offsetX,int offsetY)const{
    if(!canvas)return;
    for(const ASS_Image* image=p_->images;image;image=image->next){
        if(image->w<=0||image->h<=0)continue;
        const uint32_t color=image->color;
        const uint32_t r=(color>>24)&0xFF,g=(color>>16)&0xFF,b=(color>>8)&0xFF,opacity=255-(color&0xFF);
        if(!opacity)continue;
        const int x0=std::max(0,offsetX+image->dst_x),y0=std::max(0,offsetY+image->dst_y);
        const int x1=std::min(canvasW,offsetX+image->dst_x+image->w),y1=std::min(canvasH,offsetY+image->dst_y+image->h);
        for(int y=y0;y<y1;++y){
            const unsigned char* mask=image->bitmap+size_t(y-offsetY-image->dst_y)*image->stride;
            uint32_t* row=canvas+size_t(y)*canvasW;
            for(int x=x0;x<x1;++x){
                const uint32_t coverage=mask[x-offsetX-image->dst_x];
                if(!coverage)continue;
                const uint32_t k=(coverage*opacity+127)/255,inverse=255-k;
                const uint32_t d=row[x];
                const uint32_t da=d>>24,dr=(d>>16)&0xFF,dg=(d>>8)&0xFF,db=d&0xFF;
                // Premultiplied "over": source colour scaled by its coverage.
                const uint32_t oa=k+(da*inverse+127)/255;
                const uint32_t orr=(r*k+dr*inverse+127)/255,og=(g*k+dg*inverse+127)/255,ob=(b*k+db*inverse+127)/255;
                row[x]=(oa<<24)|(orr<<16)|(og<<8)|ob;
            }
        }
    }
}
} // namespace veyra::ui
