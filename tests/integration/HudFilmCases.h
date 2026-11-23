// SPDX-License-Identifier: GPL-3.0-or-later
// The caller supplies RGBA frames and per-scene counts with an optional clean
// interval. With explicit dimensions the input is replayed without drawing a
// reticle. Crops and frame IDs permit direct comparison between providers.
static int hudFilmCases(int argc,wchar_t** argv) {
    using namespace veyra;
    using diagnostics::GpuStage;
    if(argc!=7&&argc!=9)return 2;
    const bool recorded=argc==9;
    const std::wstring mode=argv[1],provider=argv[4];
    const auto has=[&](std::wstring_view token){return mode.find(token)!=mode.npos;};
    const bool protect=has(L"-on"), reference=has(L"-reference"), tagged=has(L"-tag");
    const bool coloured=has(L"-color");
    const unsigned width=recorded?std::stoul(argv[7]):3840;
    const unsigned height=recorded?std::stoul(argv[8]):2160;
    constexpr unsigned cropWidth=640,cropHeight=512;
    if(width<cropWidth||height<cropHeight||width>8192||height>8192)return 2;
    const D3D12_BOX crop{(width-cropWidth)/2,(height-cropHeight)/2,0,(width+cropWidth)/2,(height+cropHeight)/2,1};
    const D3D12_BOX reticle{width/2-27,height/2-27,0,width/2+27,height/2+27,1};
    const std::filesystem::path artifacts(argv[2]);
    std::filesystem::create_directories(artifacts);
    std::ifstream input(std::filesystem::path{argv[5]},std::ios::binary);
    std::ifstream cases(std::filesystem::path{argv[6]});
    std::ofstream output(artifacts/"generated.rgba",std::ios::binary),inputCrops(artifacts/"inputs.rgba",std::ios::binary);
    std::ofstream manifest(artifacts/"frames.csv");
    if(!input||!cases||!output||!inputCrops||!manifest)return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    gfx::D3D12DeviceContext ctx;
    gfx::CommandSlotRing ring;
    Status status;
    gfx::DeviceContextDesc device;
    device.commandSlotCount=6;
    if(!ctx.initialize(device,status)||
       !ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=width;
    desc.sourceHeight=desc.workHeight=height;
    desc.enableNr=desc.enableSr=false;
    desc.enableFg=true;
    desc.runtimeAbsPath=argv[3];
    desc.fgMultiplier=has(L"-4x")?4:2;
    if(provider==L"vfg")desc.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;
    else if(provider!=L"dlss")return 2;
    if(has(L"-high"))desc.vfgQuality=2;
    if(protect&&tagged){
        desc.hudProtect=engine::HudProtectMode::Regions;
        desc.exclusion.enabled=true;
        const float radius=float(height)/72.f;
        desc.exclusion.regions[0]={.5f-radius/width,.5f-radius/height,.5f+radius/width,.5f+radius/height,
                                   true,engine::ExclusionContent::Hud};
    }else if(protect)desc.hudProtect=engine::HudProtectMode::Center;

    pipeline::EnhanceGraph graph(ctx,ring);
    if(!graph.initialize(desc)||!graph.createViews())return 3;
    graph.recordGpuTimings();
    AVFrame* frame=av_frame_alloc();
    if(!frame)return 2;
    frame->format=AV_PIX_FMT_RGBA;
    frame->width=width;frame->height=height;
    frame->color_range=AVCOL_RANGE_JPEG;
    bool ok=av_frame_get_buffer(frame,32)>=0;
    manifest<<"kind,scene,frame,sub,reticle,density,core,repaired,remembered,evidence,trusted,shape\n";
    std::vector<double> realCosts,composeCosts;
    std::array<std::vector<double>,3> realStageCosts;
    std::vector<uint8_t> pixels,statistics;
    const auto write=[&](std::ofstream& file){
        file.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
        return file.good();
    };
    const auto draw=[&](int x,int y,int w,int h,bool ink){
        for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i){
            auto* p=frame->data[0]+size_t(j)*frame->linesize[0]+i*4;
            p[0]=p[2]=ink?255:3;
            p[1]=ink?(coloured?24:255):3;
        }
    };
    unsigned scene=0,submitted=0,generated=0;
    int length=0,missing=0,resume=0;
    while(ok&&(cases>>length>>missing>>resume)){
        if(length<1||length>300||scene>=24)return 2;
        bool priorVisible=false;
        for(int i=0;i<length&&ok;++i){
            const bool visible=!reference&&(missing<0||i<missing||i>=resume);
            for(unsigned y=0;y<height;++y)
                input.read(reinterpret_cast<char*>(frame->data[0]+size_t(y)*frame->linesize[0]),width*4);
            if(!input){ok=false;break;}
            if(visible&&!recorded){
                draw(1900,1078,15,4,false);draw(1927,1078,15,4,false);draw(1918,1060,4,15,false);
                draw(1918,1087,4,15,false);draw(1918,1078,4,4,false);
                draw(1901,1079,13,2,true);draw(1928,1079,13,2,true);draw(1919,1061,2,13,true);
                draw(1919,1088,2,13,true);draw(1919,1079,2,2,true);
            }
            pipeline::EnhanceGraph::FrameOutputs out;
            ok=graph.process(frame,submitted++*1000.0/30,i==0,out)&&ring.waitIdle()&&graph.resolveGeneration(out);
            if(!ok)break;
            std::array<uint32_t,7> count{};
            if(protect){
                ok=readTexture(ctx,ring,graph.hudProtectionCounters(out.videoSlot),
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,4,statistics);
                if(!ok||statistics.size()!=5*sizeof(count)){ok=false;break;}
                std::memcpy(count.data(),statistics.data()+(tagged?sizeof(count):0),sizeof(count));
            }
            const auto row=[&](char kind,unsigned sub,bool show){
                manifest<<kind<<','<<scene<<','<<i<<','<<sub<<','<<show;
                for(const auto value:count)manifest<<','<<value;
                manifest<<'\n';
            };
            ok=readTexture(ctx,ring,graph.fgInput(out.videoSlot),D3D12_RESOURCE_STATE_COMMON,4,pixels,&reticle)&&write(inputCrops);
            row('I',0,visible);
            for(unsigned n=0;n<out.batch.count&&ok;++n){
                const auto& item=out.batch.frames[n];
                if(item.kind!=pipeline::FrameKind::Generated||item.validity!=pipeline::GenerationValidity::Valid)continue;
                ok=readTexture(ctx,ring,item.lease->texture.Get(),D3D12_RESOURCE_STATE_COMMON,4,pixels,&crop)&&write(output);
                row('F',item.subframe,item.subframe*2<=desc.fgMultiplier?priorVisible:visible);
                ++generated;
            }
            priorVisible=visible;
            for(const auto& timing:graph.takeGpuTimings()){
                const auto milliseconds=[&](GpuStage stage){return timing.gpu[unsigned(stage)].milliseconds.value_or(0);};
                const std::array<double,3> stages={milliseconds(GpuStage::HudMask),
                    milliseconds(GpuStage::HudRepair),milliseconds(GpuStage::HudMotion)};
                const double real=stages[0]+stages[1]+stages[2];
                if(i>5&&real>0){
                    realCosts.push_back(real);
                    for(unsigned at=0;at<stages.size();++at)realStageCosts[at].push_back(stages[at]);
                }
                for(unsigned sub=1;sub<desc.fgMultiplier;++sub){
                    const auto ms=milliseconds(GpuStage(unsigned(GpuStage::HudCompose1)+sub-1));
                    if(i>5&&ms>0)composeCosts.push_back(ms);
                }
            }
        }
        ++scene;
    }
    const auto quantile=[](std::vector<double>& values,unsigned percentile){
        if(values.empty())return 0.;
        std::sort(values.begin(),values.end());
        return values[(values.size()-1)*percentile/100];
    };
    std::cout<<"HUD_FILM scenes="<<scene<<" real="<<submitted<<" generated="<<generated
             <<" real_p50_ms="<<quantile(realCosts,50)<<" real_p95_ms="<<quantile(realCosts,95)
             <<" real_max_ms="<<quantile(realCosts,100)
             <<" real_samples="<<realCosts.size()
             <<" real_over_700us="<<std::count_if(realCosts.begin(),realCosts.end(),[](double ms){return ms>0.7;})
             <<" mask_p95_ms="<<quantile(realStageCosts[0],95)
             <<" repair_p95_ms="<<quantile(realStageCosts[1],95)
             <<" flow_p95_ms="<<quantile(realStageCosts[2],95)
             <<" compose_p50_ms="<<quantile(composeCosts,50)<<'\n';
    ring.waitIdle();
    graph.shutdown();
    ring.shutdown();ctx.shutdown();CoUninitialize();
    return ok&&scene&&generated&&manifest.good()?0:1;
}
