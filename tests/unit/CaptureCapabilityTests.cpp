#include "veyra/source/CaptureCapabilityRange.h"
#include "veyra/source/CaptureFormatSelection.h"
#include <chrono>
#include <iostream>
#include <set>

using namespace veyra::source;
int main(){
    unsigned checks=0,failures=0;
    const auto check=[&](bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}};
    CaptureCapabilityRange range{640,1920,480,1080,16,2,166666,333667};
    check(range.acceptsSize(1280,720)&&range.acceptsSize(1920,1080),"range accepts aligned intermediate and endpoint dimensions");
    check(!range.acceptsSize(1279,720)&&!range.acceptsSize(1280,721)&&!range.acceptsSize(3840,2160),"step and bounds reject unsupported dimensions");
    const CaptureModeHint hint[]{captureResolutionHint(L"1440x900")};
    const auto sizes=captureCandidateSizes(range,0,0,hint);
    check(std::find(sizes.begin(),sizes.end(),std::pair{1440u,900u})!=sizes.end(),"custom dimensions work with an invalid template size and valid range");
    check(std::find(sizes.begin(),sizes.end(),std::pair{1920u,1080u})!=sizes.end(),"maximum capability is not lost behind template dimensions");
    auto unaligned=range;unaligned.maxWidth=1919;unaligned.maxHeight=1079;
    const auto alignedSizes=captureCandidateSizes(unaligned,0,0);
    check(std::find(alignedSizes.begin(),alignedSizes.end(),std::pair{1904u,1078u})!=alignedSizes.end(),"highest aligned size is retained when the maximum endpoint is off-step");
    check(!captureResolutionHint(L"1920x1080garbage").width&&!captureResolutionHint(L"99999999999x1080").width,"malformed and overflowing sizes rejected");
    const auto restored=captureKeyHint(L"1440:-900:333667:{subtype}:{format}:16:1:scan=5");
    check(restored.width==1440&&restored.height==900&&restored.interval==333667,"stable signed-height media key restores a range hint");
    check(!captureKeyHint(L"-1:1080:333667:bad").width&&!captureKeyHint(L"1920:1080:-1:bad").width,"invalid persisted keys cannot construct media types");
    const std::vector<int64_t> discrete{166666,166667,333667};
    const auto intervals=captureCandidateIntervals(333667,discrete,range);
    check(std::find(intervals.begin(),intervals.end(),166833)!=intervals.end(),"59.94 can be offered from an explicit driver interval range");
    check(std::find(intervals.begin(),intervals.end(),166666)!=intervals.end()&&std::find(intervals.begin(),intervals.end(),166667)!=intervals.end(),"native integer intervals are preserved exactly");
    check(std::find(intervals.begin(),intervals.end(),666667)==intervals.end(),"range does not invent 15fps outside its bounds");
    const auto fallback=captureCandidateIntervals(0,{},{});
    check(fallback==std::vector<int64_t>{0},"fallback with unknown rate stays unknown instead of fabricating 59.94");
    CaptureFormat nv12;nv12.index=0;nv12.width=1920;nv12.height=1080;nv12.interval100ns=166667;nv12.fps=1e7/166667;nv12.pixel=L"NV12";nv12.key=L"nv12";
    auto yuy2=nv12;yuy2.index=1;yuy2.interval100ns=166666;yuy2.fps=1e7/166666;yuy2.pixel=L"YUY2";yuy2.key=L"yuy2";
    std::vector<CaptureFormat> formats{nv12,yuy2};
    const auto all=captureFrameRateOptions(formats,&formats.front());
    check(all.size()==1,"rounding variants form one display group");
    check(captureFrameRateId(nv12)!=captureFrameRateId(yuy2),"display grouping never changes exact interval identity");
    for(bool showAll:{false,true}){
        const auto rows=captureVisibleFrameRates(all,all.front().id,showAll);
        check(rows.size()==1,"common and complete lists retain the same valid 60fps group");
        const auto* sample=captureRateSample(formats,L"1920x1080",rows.front().id);
        const auto pixels=capturePixelOptions(formats,sample);
        check(pixels.size()==2,"both NV12 and YUY2 remain reachable through the grouped menu");
        check(selectCapturePixelFormat(formats,L"1920x1080",rows.front().id,yuy2.key)==&formats[1],"pixel selection resolves the exact original YUY2 record");
    }
    auto ntsc=nv12;ntsc.key=L"ntsc";ntsc.interval100ns=166833;ntsc.fps=1e7/166833;formats.push_back(ntsc);
    auto interlaced=nv12;interlaced.key=L"interlaced";interlaced.interlaced=true;formats.push_back(interlaced);
    check(captureFrameRateOptions(formats,&formats.front()).size()==3,"60, 59.94 and interlaced groups remain separate");
    check(!selectCapturePixelFormat(formats,L"1920x1080",all.front().id,L"ntsc"),"60fps selection cannot silently open 59.94");
    check(!selectCapturePixelFormat(formats,L"1920x1080",all.front().id,L"interlaced"),"progressive selection cannot open interlaced media");
    const auto start=std::chrono::steady_clock::now();
    formats.clear();
    for(int i=0;i<16000;++i){auto f=nv12;f.index=i;f.key=std::format(L"mode{}",i);formats.push_back(std::move(f));}
    check(captureResolutionOptions(formats).size()==1&&captureFrameRateOptions(formats,&formats.front()).size()==1,"large capability sets keep compact linked menus");
    check(capturePixelOptions(formats,&formats.front()).size()==16000,"large exact-mode lists remain available without quadratic duplicate counting");
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Capture capability tests: "<<checks<<" checks, "<<failures<<" failures; 16000-mode case "<<elapsed<<" s\n";
    return failures?1:0;
}
