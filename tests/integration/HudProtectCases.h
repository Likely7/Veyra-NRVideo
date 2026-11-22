// Included by RepairFgTests.cpp. Input is 1280x720 RGBA. The smooth fixture
// has one mask row per frame:
// dx dy check_motion body_count pixel_count flat_pixel_indices...
// Body indices come first, followed by any transient hit-marker pixels.
// Offsets are integer current-to-previous work pixels at 1280x720.
static int hudProtectCases(int argc,wchar_t** argv) {
    using namespace veyra;
    using diagnostics::GpuStage;
    if(argc<6)return 2;
    const std::wstring mode=argv[1],provider=argv[4];
    const auto has=[&](std::wstring_view token){return mode.find(token)!=std::wstring::npos;};
    const bool enabled=!has(L"-off"),square=has(L"-square"),perf=has(L"-perf");
    const bool dynamic=has(L"-dynamic"),smooth=has(L"-smooth");
    if(argc!=(smooth?7:6))return 2;
    constexpr unsigned panelLeft=248,panelTop=164,panelRight=476,panelBottom=190;
    const unsigned mult=has(L"-4x")?4u:2u;
    constexpr unsigned srcWidth=1280,srcHeight=720;
    const unsigned workHeight=has(L"-2160")?2160u:has(L"-1440")?1440u:srcHeight;
    const unsigned workWidth=workHeight*16/9;
    const std::filesystem::path artifacts(argv[2]);
    std::filesystem::create_directories(artifacts);
    std::ifstream input(std::filesystem::path{argv[5]},std::ios::binary);
    if(!input)return 2;
    std::ifstream masks;
    if(smooth){
        masks.open(std::filesystem::path{argv[6]});
        if(!masks)return 2;
    }
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    gfx::D3D12DeviceContext ctx;
    gfx::CommandSlotRing ring;
    Status st;
    gfx::DeviceContextDesc dd;
    dd.enableDebugLayer=!perf;
    dd.commandSlotCount=6;
    if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 2;

    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=srcWidth;desc.sourceHeight=srcHeight;
    desc.workWidth=workWidth;desc.workHeight=workHeight;
    desc.enableNr=desc.enableSr=false;
    desc.enableFg=true;desc.fgMultiplier=mult;desc.runtimeAbsPath=argv[3];
    if(provider==L"vfg")desc.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;
    else if(provider==L"anr")desc.frameGenerationBackend=engine::FrameGenerationBackend::Anr;
    else if(provider==L"fsr")desc.frameGenerationBackend=engine::FrameGenerationBackend::Fsr;
    else if(provider==L"fsr4")desc.frameGenerationBackend=engine::FrameGenerationBackend::Fsr4;
    else if(provider==L"amf")desc.frameGenerationBackend=engine::FrameGenerationBackend::AmdFrc;
    else if(provider==L"xess")desc.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    else if(provider!=L"dlss")return 2;
    if(enabled&&dynamic){
        desc.exclusion.enabled=true;
        desc.exclusion.regions[0]={244.f/srcWidth,160.f/srcHeight,480.f/srcWidth,194.f/srcHeight,
                                   false,engine::ExclusionContent::Hud};
    }else if(enabled&&has(L"-region")){
        desc.exclusion.enabled=true;
        desc.exclusion.regions[2]={.475f,.455f,.525f,.545f,true,engine::ExclusionContent::Hud};
    }else if(enabled)desc.hudProtect=engine::HudProtectMode::Center;
    pipeline::EnhanceGraph graph(ctx,ring);
    if(!graph.initialize(desc)||!graph.createViews()){
        std::cout<<"HUD backend initialization failed\n";
        return 3;
    }
    graph.recordGpuTimings();
    AVFrame* frame=av_frame_alloc();
    if(!frame)return 2;
    frame->format=AV_PIX_FMT_RGBA;frame->width=srcWidth;frame->height=srcHeight;
    frame->color_range=AVCOL_RANGE_JPEG;
    bool ok=av_frame_get_buffer(frame,32)>=0;
    double ghostTotal=0;
    unsigned maxGhost=0,withGhost=0,count=0,changed=0,ownChanged=0,cleanChanges=0,realError=0,hudError=0;
    unsigned dynamicChecks=0,dynamicError=0;
    std::array<std::vector<double>,unsigned(GpuStage::Count)> timings;
    sink::RgbaImage previous;
    std::vector<unsigned> body,previousBody;
    std::vector<uint8_t> rawMotion;
    std::ofstream crops;
    if(smooth&&!perf)crops.open(artifacts/"generated-crops.rgba",std::ios::binary);
    unsigned motionSamples=0,trustedSamples=0,untrustedChanged=0,minimumBody=~0u,missedBody=0,missedHit=0;
    unsigned bodySize=0,previousBodySize=0,untrustedBodyCleaned=0;
    double maxMotionError=0;
    const unsigned frames=perf?44u:smooth?124u:18u;
    for(unsigned i=0;ok&&i<frames;++i){
        unsigned strictMotion=0;
        float trueDx=0,trueDy=0;
        if(smooth){
            unsigned count=0;
            if(!(masks>>trueDx>>trueDy>>strictMotion>>bodySize>>count)||bodySize==0||
               bodySize>count||count>srcWidth*srcHeight)return 2;
            body.resize(count);
            for(auto& index:body)
                if(!(masks>>index)||index>=srcWidth*srcHeight)return 2;
        }
        for(unsigned y=0;y<srcHeight;++y)
            input.read(reinterpret_cast<char*>(frame->data[0]+size_t(y)*frame->linesize[0]),srcWidth*4);
        if(!input){ok=false;break;}
        if(square){
            for(unsigned y=260;y<460;++y)for(unsigned x=540;x<740;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                p[0]=p[1]=p[2]=detailedPlane(x,y,0);p[3]=255;
            }
        }
        if(dynamic){
            const unsigned fill=panelLeft+8+(i*35)%164;
            for(unsigned y=panelTop;y<panelBottom;++y)for(unsigned x=panelLeft;x<panelRight;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                const bool rail=y<panelTop+2||y>=panelBottom-2||x<panelLeft+2||x>=panelRight-2;
                const bool bar=y>=174&&y<=180&&x>=panelLeft+8&&x<fill;
                const bool digits=x>=436&&x<466&&y>=174&&y<=180&&
                    ((x-436)/5==(i%6)||(x-436)%5==0&&(i%2==0));
                p[0]=rail?229:bar?235:digits?68:38;
                p[1]=rail?229:bar?132:digits?192:38;
                p[2]=rail?229:bar?60:digits?224:38;
                p[3]=255;
            }
        }
        pipeline::EnhanceGraph::FrameOutputs out;
        ok=graph.process(frame,i*1000.0/30,i==0,out)&&ring.waitIdle()&&graph.resolveGeneration(out);
        if(!ok||i<4)continue;
        for(const auto& timing:graph.takeGpuTimings()){
            if(timing.identity.sourceFrameId<6)continue;
            for(size_t stage=size_t(GpuStage::HudMask);stage<size_t(GpuStage::Count);++stage)
                if(timing.gpu[stage].milliseconds)timings[stage].push_back(*timing.gpu[stage].milliseconds);
        }
        if(perf){
            for(unsigned n=0;n<out.batch.count;++n)
                if(out.batch.frames[n].kind==pipeline::FrameKind::Generated&&
                   out.batch.frames[n].validity==pipeline::GenerationValidity::Valid)++count;
            continue;
        }
        sink::RgbaImage real,clean,video;
        ok=readImage(ctx,ring,graph.unmaskedVideo(out.videoSlot),real)&&
           readImage(ctx,ring,graph.fgInput(out.videoSlot),clean)&&
           readImage(ctx,ring,graph.videoFrameResource(out.videoSlot),video);
        if(!ok)break;
        bool trusted=false;
        if(smooth&&enabled){
            ok=readTexture(ctx,ring,graph.hudBackgroundMotion(out.videoSlot),
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,16,rawMotion);
            if(!ok)break;
            std::array<float,4> measured{};
            std::memcpy(measured.data(),rawMotion.data(),sizeof(measured));
            trusted=measured[2]>0.5f;
            if(strictMotion){
                ++motionSamples;
                trustedSamples+=trusted;
                if(trusted)maxMotionError=std::max(maxMotionError,std::hypot(double(measured[0]-trueDx),double(measured[1]-trueDy)));
            }
            std::cout<<"HUD_SHIFT frame="<<i<<" dx="<<measured[0]<<" dy="<<measured[1]<<" trusted="<<trusted
                     <<" truth="<<trueDx<<','<<trueDy<<" strict="<<strictMotion<<'\n';
        }
        if(smooth&&enabled&&!trusted){
            const auto hasInk=[](const uint8_t* p){return int(p[0])-p[1]>12&&int(p[2])-p[1]>12;};
            for(unsigned j=0;j<bodySize;++j){
                const auto offset=size_t(body[j])*4;
                untrustedBodyCleaned+=hasInk(real.pixels.data()+offset)&&!hasInk(clean.pixels.data()+offset);
            }
        }
        for(size_t a=0;a<real.pixels.size();a+=4){
            const unsigned x=unsigned((a/4)%workWidth),y=unsigned((a/4)/workWidth);
            const bool changedPixel=!std::equal(real.pixels.begin()+a,real.pixels.begin()+a+3,clean.pixels.begin()+a);
            if(changedPixel)++cleanChanges;
            if(changedPixel&&smooth&&!trusted)++untrustedChanged;
            if(changedPixel&&x>=621&&x<=658&&y>=341&&y<=378)++ownChanged;
            for(unsigned c=0;c<3;++c)realError=std::max(realError,unsigned(std::abs(int(real.pixels[a+c])-int(video.pixels[a+c]))));
        }
        if(i==4){
            previous=std::move(real);previousBody=body;previousBodySize=bodySize;
            continue;
        }
        if(i==8||(smooth&&i==32)){
            sink::saveImage((artifacts/"real.png").wstring(),real);
            sink::saveImage((artifacts/"previous.png").wstring(),previous);
            sink::saveImage((artifacts/"clean.png").wstring(),clean);
            if(desc.frameGenerationBackend==engine::FrameGenerationBackend::XeSS&&graph.exclusionUi(out.videoSlot)){
                sink::RgbaImage layer;
                if(readImage(ctx,ring,graph.exclusionUi(out.videoSlot),layer))
                    sink::saveImage((artifacts/"xess-ui.png").wstring(),layer);
            }
        }
        for(unsigned n=0;n<out.batch.count;++n){
            const auto& item=out.batch.frames[n];
            if(item.kind!=pipeline::FrameKind::Generated||item.validity!=pipeline::GenerationValidity::Valid)continue;
            sink::RgbaImage generated;
            if(!readImage(ctx,ring,item.lease->texture.Get(),generated)){ok=false;break;}
            const auto& held=item.subframe*2<=mult?previous:real;
            if(dynamic){
                for(unsigned y=174;y<=180;++y)for(unsigned x=panelLeft+8;x<panelRight-8;++x){
                    const auto p=(size_t(y)*workWidth+x)*4;
                    if(std::equal(real.pixels.begin()+p,real.pixels.begin()+p+3,previous.pixels.begin()+p))continue;
                    ++dynamicChecks;
                    for(unsigned c=0;c<3;++c){
                        const auto error=unsigned(std::abs(int(held.pixels[p+c])-int(generated.pixels[p+c])));
                        if(error<=dynamicError)continue;
                        dynamicError=error;
                        if(enabled&&error>2)std::cout<<"HUD_DYNAMIC_MISS frame="<<i<<" p="<<x<<','<<y
                            <<" error="<<error<<" A="<<unsigned(previous.pixels[p+c])<<" B="<<unsigned(real.pixels[p+c])
                            <<" clean="<<unsigned(clean.pixels[p+c])<<'\n';
                    }
                }
            }
            unsigned ghosts=0,preserved=0;
            if(smooth){
                constexpr unsigned crop=320,cropX=(srcWidth-crop)/2,cropY=(srcHeight-crop)/2;
                for(unsigned y=cropY;y<cropY+crop;++y)
                    crops.write(reinterpret_cast<const char*>(generated.pixels.data()+
                                (size_t(y)*srcWidth+cropX)*4),crop*4);
                const auto& expected=item.subframe*2<=mult?previousBody:body;
                const auto coreSize=item.subframe*2<=mult?previousBodySize:bodySize;
                for(size_t k=0;k<expected.size();++k){
                    const auto* p=generated.pixels.data()+size_t(expected[k])*4;
                    const bool painted=int(p[0])-p[1]>12&&int(p[2])-p[1]>12;
                    if(k<coreSize){
                        preserved+=painted;
                        if(enabled&&!painted){
                            const auto* a=held.pixels.data()+size_t(expected[k])*4;
                            const auto* b=real.pixels.data()+size_t(expected[k])*4;
                            std::cout<<"HUD_BODY_MISS frame="<<i<<" x="<<expected[k]%srcWidth<<" y="<<expected[k]/srcWidth
                                     <<" held="<<int(a[0])<<','<<int(a[1])<<','<<int(a[2])
                                     <<" other="<<int(b[0])<<','<<int(b[1])<<','<<int(b[2])
                                     <<" out="<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<'\n';
                        }
                    } else missedHit+=!painted;
                }
                minimumBody=std::min(minimumBody,preserved);
                missedBody+=coreSize-preserved;
            }
            for(unsigned y=260;y<460;++y)for(unsigned x=440;x<840;++x){
                const size_t a=(size_t(y)*workWidth+x)*4;
                const bool own=x>=621&&x<=658&&y>=341&&y<=378;
                if(smooth){
                    const auto* p=generated.pixels.data()+a;
                    const bool outside=std::abs(int(x)-640)>27||std::abs(int(y)-360)>27;
                    ghosts+=outside&&int(p[0])-p[1]>12&&int(p[2])-p[1]>12;
                    continue;
                }
                if(!own&&std::min({generated.pixels[a],generated.pixels[a+1],generated.pixels[a+2]})>=150)++ghosts;
                // Source drawbox geometry, independent of H.264 halo and background colours.
                const bool vertical=x>=639&&x<641&&((y>=344&&y<354)||(y>=359&&y<361)||(y>=366&&y<376));
                const bool horizontal=y>=359&&y<361&&((x>=624&&x<634)||(x>=646&&x<656));
                if(square||dynamic||(!vertical&&!horizontal))continue;
                unsigned error=0;
                for(unsigned c=0;c<3;++c)error=std::max(error,unsigned(std::abs(int(held.pixels[a+c])-int(generated.pixels[a+c]))));
                if(error>hudError){
                    hudError=error;
                    if(enabled&&error>2)std::cout<<"HUD_MISS frame="<<i<<" sub="<<item.subframe<<" pixel="<<x<<','<<y
                         <<" error="<<error<<" held="<<unsigned(held.pixels[a])<<" gen="<<unsigned(generated.pixels[a])
                         <<" clean="<<unsigned(clean.pixels[a])<<'\n';
                }
                ++preserved;
            }
            maxGhost=std::max(maxGhost,ghosts);
            withGhost+=ghosts>0;
            ghostTotal+=ghosts;
            ++count;
            changed+=preserved;
            if(i==8)sink::saveImage((artifacts/("generated-"+std::to_string(item.subframe)+".png")).wstring(),generated);
        }
        previous=std::move(real);
        previousBody=body;
        previousBodySize=bodySize;
    }
    av_frame_free(&frame);
    for(size_t stage=size_t(GpuStage::HudMask);stage<size_t(GpuStage::Count);++stage){
        auto& samples=timings[stage];
        if(samples.empty())continue;
        std::sort(samples.begin(),samples.end());
        std::cout<<"HUD_TIMING stage="<<stage<<" n="<<samples.size()<<" median_ms="<<samples[samples.size()/2]
                 <<" p95_ms="<<samples[(samples.size()-1)*95/100]<<'\n';
    }
    std::cout<<"HUD_RESULT generated="<<count<<" ghost_frames="<<withGhost
             <<" ghost_mean="<<(count?ghostTotal/count:0)<<" ghost_max="<<maxGhost
             <<" hud_pixels="<<changed<<" hud_max_error="<<hudError<<" real_max_error="<<realError
             <<" cleaned_pixels="<<cleanChanges<<" cleaned_crosshair_pixels="<<ownChanged
             <<" dynamic_pixels="<<dynamicChecks<<" dynamic_max_error="<<dynamicError<<'\n';
    // A baseline can legitimately have no ghosts on a particular provider.
    // The matched baseline/feature runs provide the effectiveness comparison.
    ok=ok&&realError==0&&(provider==L"xess"||count>0);
    if(smooth&&!perf){
        std::cout<<"HUD_MASK_RESULTS motion_samples="<<motionSamples<<" trusted="<<trustedSamples
                 <<" largest_shift_error="<<maxMotionError<<" untrusted_cleaned="<<untrustedChanged
                 <<" untrusted_body_cleaned="<<untrustedBodyCleaned<<" min_body="<<(count?minimumBody:0)<<" missing_body="<<missedBody<<" missing_hit="<<missedHit<<'\n';
        ok=ok&&crops.good()&&(!enabled||(motionSamples>0&&trustedSamples==motionSamples&&
               maxMotionError<=2&&untrustedBodyCleaned>0&&missedBody==0&&ownChanged>0));
    }else if(dynamic)ok=ok&&dynamicChecks>0&&(!enabled||dynamicError<=2);
    else {
        ok=ok&&(square?cleanChanges==0:!enabled||perf||ownChanged>0);
        if(enabled&&!square&&!perf&&provider!=L"xess")ok=ok&&hudError<=2;
    }
    ring.waitIdle();
    graph.shutdown();
    if(!perf){
        pipeline::ComPtr<ID3D12InfoQueue> queue;
        if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&queue))))ok=false;
        else for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){
            SIZE_T size=0;queue->GetMessage(i,nullptr,&size);
            std::vector<uint8_t> data(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
            queue->GetMessage(i,message,&size);
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                std::cerr<<message->pDescription<<'\n';
                ok=false;
            }
        }
    }
    ring.shutdown();ctx.shutdown();CoUninitialize();
    return ok?0:1;
}
