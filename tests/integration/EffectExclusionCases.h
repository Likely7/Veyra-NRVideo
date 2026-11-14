// Included by RepairFgTests.cpp after its diagnostic readImage helper.
// Compare final pixels with a separate unprocessed graph. Generated subframes
// must restore the previous raw picture, not an enhanced or interpolated one.
static int effectExclusionCases(int argc,wchar_t** argv) {
    using namespace veyra;
    const std::wstring provider=argc>4?argv[4]:L"off";
    const bool hdr=std::wstring(argv[1]).find(L"hdr")!=std::wstring::npos;
    const bool feather=std::wstring(argv[1]).find(L"feather")!=std::wstring::npos;
    const bool upscale=std::wstring(argv[1]).find(L"sr")!=std::wstring::npos;
    const bool resize=upscale||std::wstring(argv[1]).find(L"scale")!=std::wstring::npos;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;device.commandSlotCount=6;
    bool ok=ctx.initialize(device,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status);
    pipeline::EnhanceGraph raw(ctx,ring),graph(ctx,ring);
    pipeline::EnhanceGraphDesc control;
    control.sourceWidth=640;control.sourceHeight=360;
    control.workWidth=resize?1280:640;control.workHeight=resize?720:360;
    control.enableNr=control.enableSr=control.enableFg=false;
    control.hdrInput=control.hdrOutput=hdr;
    if(argc>3)control.runtimeAbsPath=argv[3];
    auto desc=control;
    desc.color.enabled=true;desc.color.exposure=1;
    desc.enableNr=std::wstring(argv[1]).find(L"nr")!=std::wstring::npos;
    desc.enableSr=upscale;desc.videoSrQuality=2;
    desc.exclusion.enabled=true;desc.exclusion.featherPixels=feather?16:0;
    desc.exclusion.regions[0]={.1f,.1f,.45f,.45f};
    desc.exclusion.regions[2]={.6f,.6f,.9f,.9f,true};
    desc.enableFg=provider!=L"off";
    desc.fgMultiplier=std::wstring(argv[1]).find(L"6x")!=std::wstring::npos?6:provider==L"vfg"?3:2;
    if(provider==L"vfg")desc.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;
    else if(provider==L"anr")desc.frameGenerationBackend=engine::FrameGenerationBackend::Anr;
    else if(provider==L"fsr")desc.frameGenerationBackend=engine::FrameGenerationBackend::Fsr;
    else if(provider==L"xess")desc.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    if(ok)ok=raw.initialize(control)&&raw.createViews()&&graph.initialize(desc)&&graph.createViews();
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_RGBA;frame->width=640;frame->height=360;
    frame->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(frame,32)>=0;
    AVFrame* hdrFrame=nullptr;
    if(hdr){
        hdrFrame=av_frame_alloc();hdrFrame->format=AV_PIX_FMT_P010;hdrFrame->width=640;hdrFrame->height=360;
        hdrFrame->color_range=AVCOL_RANGE_MPEG;hdrFrame->colorspace=AVCOL_SPC_BT2020_NCL;hdrFrame->color_primaries=AVCOL_PRI_BT2020;hdrFrame->color_trc=AVCOL_TRC_SMPTE2084;
        ok=ok&&av_frame_get_buffer(hdrFrame,32)>=0;
        if(ok)for(unsigned y=0;y<180;++y)std::fill_n(reinterpret_cast<uint16_t*>(hdrFrame->data[1]+y*hdrFrame->linesize[1]),640,uint16_t(512<<6));
    }
    const std::filesystem::path artifacts=argc>2?argv[2]:L"";
    if(!artifacts.empty())std::filesystem::create_directories(artifacts);
    unsigned realChecked=0,generatedChecked=0;double maximumRawError=0,maximumOutsideError=0,maximumFeatherError=0,minimumChanged=255,minimumRawProgress=255;
    sink::RgbaImage previous;
    for(unsigned i=0;ok&&i<6;++i){
        for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
            auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
            p[0]=p[1]=p[2]=detailedPlane(x,y,i);p[3]=255;
        }
        if(hdrFrame)for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
            const double srgb=double(frame->data[0][size_t(y)*frame->linesize[0]+x*4])/255;
            const double linear=srgb<=.04045?srgb/12.92:std::pow((srgb+.055)/1.055,2.4);
            const double v=std::pow(linear*.008,.1593017578125);
            const double pq=std::pow((.8359375+18.8515625*v)/(1+18.6875*v),78.84375);
            reinterpret_cast<uint16_t*>(hdrFrame->data[0]+y*hdrFrame->linesize[0])[x]=uint16_t(std::lround(64+876*pq))<<6;
        }
        auto* input=hdrFrame?hdrFrame:frame;
        pipeline::EnhanceGraph::FrameOutputs reference,output;
        sink::RgbaImage original,enhanced,masked;
        ok=raw.process(input,i*33.333333,i==0,reference)&&ring.waitIdle()&&
            readImage(ctx,ring,raw.videoFrameResource(reference.videoSlot),original)&&
            graph.process(input,i*33.333333,i==0,output)&&ring.waitIdle()&&graph.resolveGeneration(output)&&
            readImage(ctx,ring,graph.unmaskedVideo(output.videoSlot),enhanced)&&
            readImage(ctx,ring,graph.videoFrameResource(output.videoSlot),masked);
        if(!ok)break;
        const auto patchError=[](const sink::RgbaImage& a,const sink::RgbaImage& b,double cx,double cy){
            double error=0;unsigned count=0;
            for(int y=int(a.height*cy)-6;y<int(a.height*cy)+6;++y)
                for(int x=int(a.width*cx)-6;x<int(a.width*cx)+6;++x)
                    for(unsigned c=0;c<3;++c){const auto at=(size_t(y)*a.width+x)*4+c;error+=std::abs(int(a.pixels[at])-int(b.pixels[at]));++count;}
            return error/count;
        };
        const double restored=std::max(patchError(masked,original,.25,.25),patchError(masked,original,.75,.75));
        const double unchanged=std::max(patchError(masked,enhanced,.75,.25),patchError(masked,enhanced,.61,.61));
        const double changed=patchError(masked,enhanced,.25,.25);
        maximumRawError=std::max(maximumRawError,restored);maximumOutsideError=std::max(maximumOutsideError,unchanged);minimumChanged=std::min(minimumChanged,changed);
        ok=restored<=2&&unchanged<=1&&changed>8;
        if(feather&&!hdr){
            const unsigned x=unsigned(masked.width*.1)+8,y=unsigned(masked.height*.25);
            const size_t at=(size_t(y)*masked.width+x)*4;
            const double edge=x+.5-masked.width*.1,t=std::clamp(edge/16.,0.,1.),weight=t*t*(3-2*t);
            const auto decode=[](double s){s/=255;return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
            const double linear=decode(enhanced.pixels[at])*(1-weight)+decode(original.pixels[at])*weight;
            const double expected=255*(linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055);
            const double error=std::abs(masked.pixels[at]-expected);maximumFeatherError=std::max(maximumFeatherError,error);
            ok=ok&&error<=3;
        }
        ++realChecked;
        if(i&&ok)for(unsigned n=0;n<output.batch.count;++n){
            const auto& item=output.batch.frames[n];if(item.kind!=pipeline::FrameKind::Generated)continue;
            sink::RgbaImage generated;
            if(item.validity!=pipeline::GenerationValidity::Valid||!readImage(ctx,ring,item.lease->texture.Get(),generated)){ok=false;break;}
            const double held=std::max(patchError(generated,previous,.25,.25),patchError(generated,previous,.75,.75));
            const double progress=patchError(original,previous,.25,.25);
            std::cout<<"EXCLUSION_FRAME frame="<<i<<" sub="<<item.subframe<<" heldError="<<held<<" currentError="<<patchError(generated,original,.25,.25)<<" rawProgress="<<progress<<std::endl;
            maximumRawError=std::max(maximumRawError,held);minimumRawProgress=std::min(minimumRawProgress,progress);
            ok=held<=2&&progress>1;
            ++generatedChecked;
            if((i==3||!ok)&&!artifacts.empty()){
                sink::saveImage((artifacts/("generated-"+std::to_string(item.subframe)+".png")).wstring(),generated);
                sink::saveImage((artifacts/"previous-raw.png").wstring(),previous);
            }
            if(!ok)break;
        }
        if((i==3||!ok)&&!artifacts.empty()){
            sink::saveImage((artifacts/"raw.png").wstring(),original);
            sink::saveImage((artifacts/"enhanced.png").wstring(),enhanced);
            sink::saveImage((artifacts/"masked.png").wstring(),masked);
        }
        previous=std::move(original);
    }
    const unsigned expectedGenerated=desc.enableFg&&!graph.xessEnabled()?5*(desc.fgMultiplier-1):0;
    ok=ok&&realChecked==6&&generatedChecked==expectedGenerated;
    ring.drainQueue();graph.shutdown();raw.shutdown();av_frame_free(&frame);av_frame_free(&hdrFrame);
    pipeline::ComPtr<ID3D12InfoQueue> queue;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&queue))))ok=false;
    else for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){
        SIZE_T size=0;queue->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());queue->GetMessage(i,message,&size);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<std::endl;}
    }
    std::cout<<"EXCLUSION real="<<realChecked<<" generated="<<generatedChecked<<" expectedGenerated="<<expectedGenerated
        <<" rawError="<<maximumRawError<<" outsideError="<<maximumOutsideError<<" restoredDifference="<<minimumChanged
        <<" rawProgress="<<minimumRawProgress<<" featherError="<<maximumFeatherError<<" hdr="<<hdr<<" feather="<<feather<<" scale="<<resize<<" debugErrors="<<errors<<" pass="<<(ok&&!errors)<<std::endl;
    ring.shutdown();ctx.shutdown();CoUninitialize();return ok&&!errors?0:1;
}
