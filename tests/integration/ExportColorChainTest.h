#pragma once
// Test-only end-to-end coverage: the production D3D12/NVENC exporter writes
// files, then the existing software decoder compares their active pixels.
// No GPU video readback is added to the player or exporter.
#include "veyra/engine/ColorLut.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/RuntimePaths.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

namespace {
constexpr unsigned kExportColorFrames=12;
std::string exportColorPathText(const std::filesystem::path& path){
    const auto text=path.u8string();return {text.begin(),text.end()};
}
struct ExportColorFrame {
    int width=0,height=0,format=0;
    veyra::pipeline::Rational pts;
    std::vector<uint8_t> pixels;
};
using ExportColorFrames=std::vector<ExportColorFrame>;

bool decodeExportColorFrames(const std::filesystem::path& path,ExportColorFrames& frames){
    using namespace veyra;
    source::SourceOpenDesc desc;desc.path=path.wstring();desc.preferHardwareDecode=false;
    source::MediaFileSource source;if(!source.open(desc))return false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline){
        pipeline::FramePacket packet;const AVFrame* frame=nullptr;
        const auto status=source.read(packet,&frame);
        if(status==source::SourceReadStatus::Eos)return frames.size()==kExportColorFrames;
        if(status==source::SourceReadStatus::Waiting){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        if(status!=source::SourceReadStatus::Frame||!frame||frames.size()>=kExportColorFrames)return false;
        // This fixture is SDR H.264; comparing bytes of a 10-bit frame would
        // make the predeclared code-value difference threshold meaningless.
        if(frame->format!=AV_PIX_FMT_YUV420P||frame->width<=0||frame->height<=0||
           frame->width>3840||frame->height>2160||packet.pts.isUnknown())return false;
        ExportColorFrame result{frame->width,frame->height,frame->format,packet.pts,{}};
        const auto format=static_cast<AVPixelFormat>(frame->format);
        const int size=av_image_get_buffer_size(format,frame->width,frame->height,1);
        if(size<=0)return false;
        result.pixels.resize(size);
        if(av_image_copy_to_buffer(result.pixels.data(),size,frame->data,frame->linesize,
                                  format,frame->width,frame->height,1)!=size)return false;
        frames.push_back(std::move(result));
    }
    return false;
}

struct ExportColorDifference {
    bool comparable=true;
    unsigned maximum=0;
    uint64_t changed=0,total=0;
};
ExportColorDifference exportColorDifference(const ExportColorFrames& a,const ExportColorFrames& b){
    ExportColorDifference result;
    if(a.size()!=kExportColorFrames||b.size()!=a.size()){result.comparable=false;return result;}
    for(size_t f=0;f<a.size();++f){
        if(a[f].width!=b[f].width||a[f].height!=b[f].height||a[f].format!=b[f].format||
           !a[f].pts.equals(b[f].pts)||a[f].pixels.size()!=b[f].pixels.size()){result.comparable=false;return result;}
        result.total+=a[f].pixels.size();
        for(size_t p=0;p<a[f].pixels.size();++p){
            const unsigned delta=unsigned(std::abs(int(a[f].pixels[p])-int(b[f].pixels[p])));
            result.maximum=std::max(result.maximum,delta);result.changed+=delta!=0;
        }
    }
    return result;
}
bool exportColorChanged(const ExportColorDifference& difference){
    // Registered before running: >=2 code values and >=1% changed samples.
    return difference.comparable&&difference.maximum>=2&&difference.changed*100>=difference.total;
}

bool runExportColorChainTest(const std::filesystem::path& input,const std::filesystem::path& output){
    using namespace veyra;using namespace veyra::engine;
    // Explicitly acknowledge the staging data root, so accidental execution
    // beside the user's normal application cannot create test LUTs there.
    wchar_t expectedRoot[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"VEYRA_TEST_COLOR_CHAIN_DATA_ROOT",expectedRoot,32768);
    if(!length||length>=32768||!std::filesystem::equivalent(expectedRoot,runtime::localDataDirectory())||
       !std::filesystem::is_regular_file(input)||std::filesystem::exists(output))return false;
    if(!std::filesystem::create_directories(output))return false;
    const ColorLutStore store(runtime::localDataDirectory());std::filesystem::create_directories(store.folder());
    std::cout<<"COLOR-WORKER dataRoot="<<exportColorPathText(runtime::localDataDirectory())<<" output="<<exportColorPathText(output)<<'\n';
    EnhancementSettings plain;plain.nr=plain.sr=false;plain.multiplier=1;plain.nrPolicy=pipeline::NrSizePolicy::Native;
    auto six=plain;six.additionalColorCount=5;
    const unsigned sizes[]={2,3,5,17,33,4};
    for(unsigned i=0;i<kMaxColorInstances;++i){
        auto& color=i?six.additionalColors[i-1]:six.color;
        color.enabled=true;color.lutInputSpace=ColorSettings::kLutInputSrgb;
        color.setLutName(L"worker-color-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(i)+L".cube");
        const auto path=store.folder()/color.lutNameString();
        if(std::filesystem::exists(path))return false;
        std::ofstream file(path);file.precision(9);file<<"LUT_3D_SIZE "<<sizes[i]<<'\n';
        for(unsigned z=0;z<sizes[i];++z)for(unsigned y=0;y<sizes[i];++y)for(unsigned x=0;x<sizes[i];++x){
            const float d=float(sizes[i]-1);
            file<<(float(x)/d*(.90f+.01f*i)+.008f*(i+1))<<' '
                <<(float(y)/d*(.93f-.01f*i)+.005f*(6-i))<<' '
                <<(float(z)/d*(.88f+.015f*i)+.01f*(i%3))<<'\n';
        }
        file.close();if(!file)return false;
        ColorLutData lut;if(!store.resolve(color.lutNameString(),lut)||lut.size!=int(sizes[i]))return false;
        std::filesystem::copy_file(path,output/path.filename());
    }
    struct Case {std::wstring name;EnhancementSettings settings;};
    auto one=plain;one.color=six.color;
    auto disabled=six;disabled.color.enabled=false;disabled.additionalColors[2].enabled=false;
    auto reversed=six;std::array<ColorSettings,kMaxColorInstances> colors;
    colors[0]=six.color;for(unsigned i=1;i<kMaxColorInstances;++i)colors[i]=six.additionalColors[i-1];
    reversed.color=colors.back();for(unsigned i=1;i<kMaxColorInstances;++i)reversed.additionalColors[i-1]=colors[kMaxColorInstances-1-i];
    std::vector<Case> cases{{L"plain",plain},{L"one",one},{L"six",six},{L"disabled-first-middle",disabled},{L"reverse",reversed}};
    for(unsigned i=0;i<kMaxColorInstances;++i){
        auto without=six;(i?without.additionalColors[i-1]:without.color).enabled=false;
        cases.push_back({L"without-"+std::to_wstring(i),without});
    }
    ExportColorFrames plainFrames,oneFrames,sixFrames;bool all=true;unsigned passed=0;
    for(size_t index=0;index<cases.size();++index){
        auto& test=cases[index];test.settings.revision=5300+index;
        const auto directPath=output/(test.name+L"-direct.mp4");
        const auto workerPath=output/(test.name+L"-worker.mp4");
        std::atomic<bool> cancel=false;ExportCounts counts;bool directNvenc=false;
        const bool direct=exportVideo(input.wstring(),directPath.wstring(),PlayerOptions::from(test.settings),false,cancel,
            [&](double progress,const std::wstring& message){if(progress>=1)directNvenc=message.find(L"NVENC")!=std::wstring::npos;},
            kExportColorFrames,{},[&](const ExportCounts& value){counts=value;});
        ExportJobSnapshot snapshot;bool launched=false;
        {
            ExportJobManager manager;launched=manager.start(input.wstring(),workerPath.wstring(),test.settings,false,kExportColorFrames);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            do{snapshot=manager.poll();if(!snapshot.active())break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            while(std::chrono::steady_clock::now()<deadline);
            if(snapshot.active())manager.cancel();
        }
        if(!snapshot.workerLog.empty()&&std::filesystem::is_regular_file(snapshot.workerLog))
            std::filesystem::copy_file(snapshot.workerLog,output/(test.name+L"-worker.log"));
        bool workerNvenc=false,workerD3D12=false;
        std::ifstream workerLog(output/(test.name+L"-worker.log"));
        for(std::string line;std::getline(workerLog,line);){
            workerNvenc=workerNvenc||line.find("encoder selected=NVIDIA-NVENC")!=std::string::npos;
            workerD3D12=workerD3D12||line.find("OpenD3D12Session status=0")!=std::string::npos;
        }
        ExportColorFrames directFrames,workerFrames;
        const bool decoded=direct&&snapshot.state==ExportState::Succeeded&&
            decodeExportColorFrames(directPath,directFrames)&&decodeExportColorFrames(workerPath,workerFrames);
        const auto difference=exportColorDifference(directFrames,workerFrames);
        bool ok=launched&&direct&&decoded&&directNvenc&&workerNvenc&&workerD3D12&&counts.source==kExportColorFrames&&counts.encoded==kExportColorFrames&&
            counts.generated==0&&counts.holds==0&&snapshot.sourceFrames==kExportColorFrames&&snapshot.encoded==kExportColorFrames&&
            snapshot.generated==0&&snapshot.holds==0&&snapshot.frozen==test.settings&&snapshot.frozenRevision==test.settings.revision&&
            snapshot.workerPid!=0&&snapshot.workerPid!=GetCurrentProcessId()&&
            std::filesystem::is_regular_file(output/(test.name+L"-worker.log"))&&difference.comparable&&difference.maximum==0;
        const auto changed=exportColorDifference(workerFrames,index>2?sixFrames:plainFrames);
        if(index>0)ok=ok&&exportColorChanged(changed);
        if(index==2)ok=ok&&exportColorChanged(exportColorDifference(workerFrames,oneFrames));
        std::cout<<"COLOR-WORKER case="<<exportColorPathText(test.name)<<" direct="<<direct<<" state="<<int(snapshot.state)
            <<" directNvenc="<<directNvenc<<" workerNvenc="<<workerNvenc<<" workerD3D12="<<workerD3D12
            <<" directSource="<<counts.source<<" directEncoded="<<counts.encoded<<" workerSource="<<snapshot.sourceFrames
            <<" workerEncoded="<<snapshot.encoded<<" workerGenerated="<<snapshot.generated<<" workerHolds="<<snapshot.holds
            <<" frozenEqual="<<(snapshot.frozen==test.settings)<<" workerPid="<<snapshot.workerPid<<" decodedFrames="<<workerFrames.size()
            <<" equalPixels="<<(difference.comparable&&difference.maximum==0)<<" maxError8="<<difference.maximum
            <<" changedCodes="<<difference.changed<<" effectMaxError8="<<changed.maximum
            <<" effectChangedCodes="<<changed.changed<<" effectTotalCodes="<<changed.total<<" passed="<<ok<<'\n';
        if(index==0)plainFrames=std::move(workerFrames);
        if(index==1)oneFrames=std::move(workerFrames);
        if(index==2)sixFrames=std::move(workerFrames);
        all=all&&ok;passed+=ok;
    }
    std::cout<<"COLOR-WORKER SUMMARY passed="<<passed<<" cases="<<cases.size()<<" gpuReadback=false\n";
    return all;
}
} // namespace
