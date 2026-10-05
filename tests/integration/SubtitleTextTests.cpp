#include "veyra/engine/Subtitles.h"
#include <cmath>
#include <filesystem>
#include <iostream>

// Golden text is independent of parsing implementation. The MKV contains the
// same authored cues as the external files, decoded by the actual FFmpeg build.
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    const std::filesystem::path root=argv[1];
    const std::vector<std::wstring> expected={
        L"It is over, Sauron.", L"Ka-Zar, I warn you!",
        L"Your reign of terror over\nthe Savage Land ends now!",
        L"I am Zaladane,\nHigh Priestess of\nthe Sun God, Garokk.",
        L"中文，第一行\nEnglish, second line\n第三行, 原样保留",
        L"A, B, C, D, E, F, G, H, I, J, K.",
    };
    bool all=true;
    auto check=[&](const veyra::engine::SubtitleTrack& t){
        bool ok=t.cues.size()==expected.size();
        for(size_t k=0;k<std::min(t.cues.size(),expected.size());++k){
            const auto& c=t.cues[k];
            const bool match=c.text==expected[k]&&std::abs(c.begin-k*3.)<.03&&std::abs(c.end-(k*3.+2.5))<.03;
            if(!match)std::cout<<"MISMATCH cue="<<k<<" textChars="<<c.text.size()<<" expected="<<expected[k].size()<<" times="<<c.begin<<".."<<c.end<<'\n';
            ok&=match;
        }
        std::cout<<(ok?"PASS":"FAIL")<<" embedded="<<t.embedded<<" cues="<<t.cues.size()<<'\n';all&=ok;
    };
    for(auto name:{L"golden.srt",L"golden.ass",L"golden.vtt"})check(veyra::engine::loadSubtitleFile((root/name).wstring()));
    auto tracks=veyra::engine::loadEmbeddedSubtitleTracks((root/L"golden.mkv").wstring());
    if(tracks.size()!=2)all=false;
    for(const auto& t:tracks)check(t);
    return all?0:1;
}
