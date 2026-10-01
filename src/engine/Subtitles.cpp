#include "veyra/engine/Subtitles.h"
#include "veyra/Log.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace veyra::engine {
namespace {
constexpr size_t kMaxSubtitleFileBytes=16u*1024u*1024u;
constexpr size_t kMaxCues=120000;

std::wstring wide(const std::string& s){
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(n<=0)return {};
    std::wstring r(size_t(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);
    return r;
}
std::string utf8(const std::wstring& s){
    if(s.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string r(size_t(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);
    return r;
}

bool readTextFile(const std::wstring& path,std::wstring& out){
    std::ifstream in(std::filesystem::path(path),std::ios::binary);
    if(!in)return false;
    in.seekg(0,std::ios::end);
    const auto size=in.tellg();
    if(size<=0||size>std::streamoff(kMaxSubtitleFileBytes))return false;
    in.seekg(0);
    std::string data(size_t(size),'\0');
    in.read(data.data(),std::streamsize(data.size()));
    if(!in)return false;
    if(data.size()>2&&uint8_t(data[0])==0xFF&&uint8_t(data[1])==0xFE){
        out.clear();out.reserve(data.size()/2);
        for(size_t i=2;i+1<data.size();i+=2)out.push_back(wchar_t(uint8_t(data[i])|uint8_t(data[i+1])<<8));
        return true;
    }
    const char* start=data.data();size_t length=data.size();
    if(length>=3&&uint8_t(start[0])==0xEF&&uint8_t(start[1])==0xBB&&uint8_t(start[2])==0xBF){start+=3;length-=3;}
    out=wide(std::string(start,length));
    return !out.empty();
}

std::wstring trim(std::wstring s){
    const auto first=s.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos)return {};
    const auto last=s.find_last_not_of(L" \t\r\n");
    return s.substr(first,last-first+1);
}
std::wstring lower(std::wstring s){for(auto& c:s)c=wchar_t(towlower(c));return s;}

// "00:01:02,345" / "0:00:01.23" / "00:01:02.345" -> seconds
bool parseClock(const std::wstring& text,double& seconds){
    int h=0,m=0;double s=0;
    wchar_t tail=0;
    if(swscanf_s(text.c_str(),L"%d:%d:%lf%c",&h,&m,&s,&tail,1)==3){seconds=h*3600.0+m*60.0+s;return true;}
    return false;
}

// ASS colours are &HAABBGGRR with 00 = opaque.
uint32_t assColor(const std::wstring& field,uint32_t fallback){
    std::wstring value=trim(field);
    if(value.empty())return fallback;
    if(value[0]==L'&')value=value.substr(1);
    if(!value.empty()&&(value[0]==L'H'||value[0]==L'h'))value=value.substr(1);
    if(!value.empty()&&value.back()==L'&')value.pop_back();
    if(value.empty())return fallback;
    wchar_t* end=nullptr;
    const unsigned long raw=wcstoul(value.c_str(),&end,16);
    if(end==value.c_str())return fallback;
    const uint32_t b=raw&0xFF,r=(raw>>16)&0xFF,g=(raw>>8)&0xFF,a=(raw>>24)&0xFF;
    const uint32_t alpha=(raw>>24)?(255u-(a&0xFF)):255u; // 00 = fully opaque
    return (alpha<<24)|(r<<16)|(g<<8)|b;
}

// Removes ASS override blocks and turns the escaped line breaks into '\n'.
std::wstring cleanAssText(const std::wstring& raw,int& alignOverride,double& posX,double& posY){
    std::wstring out;out.reserve(raw.size());
    bool inBlock=false;
    for(size_t i=0;i<raw.size();++i){
        const wchar_t c=raw[i];
        if(!inBlock&&c==L'{'){inBlock=true;
            const auto end=raw.find(L'}',i);
            const std::wstring block=end==std::wstring::npos?raw.substr(i):raw.substr(i,end-i+1);
            const auto an=block.find(L"\\an");
            if(an!=std::wstring::npos&&an+3<block.size()&&block[an+3]>=L'1'&&block[an+3]<=L'9')alignOverride=block[an+3]-L'0';
            const auto pos=block.find(L"\\pos(");
            if(pos!=std::wstring::npos){
                const auto close=block.find(L')',pos);
                if(close!=std::wstring::npos){
                    const auto numbers=block.substr(pos+5,close-pos-5);
                    const auto comma=numbers.find(L',');
                    if(comma!=std::wstring::npos){
                        posX=wcstod(numbers.substr(0,comma).c_str(),nullptr);
                        posY=wcstod(numbers.substr(comma+1).c_str(),nullptr);
                    }
                }
            }
            i=end==std::wstring::npos?raw.size()-1:end;
            inBlock=false;
            continue;
        }
        if(c==L'\\'&&i+1<raw.size()){
            const wchar_t next=raw[++i];
            if(next==L'N'||next==L'n')out.push_back(L'\n');
            else if(next==L'h')out.push_back(L' ');
            else out.push_back(next);
            continue;
        }
        out.push_back(c);
    }
    return trim(out);
}

SubtitleTrack parseSrt(const std::wstring& data,const std::wstring& name){
    SubtitleTrack track;track.name=name;track.codec=L"SubRip";
    track.styles.push_back(SubtitleStyle{});
    std::wistringstream lines(data);std::wstring line;
    while(std::getline(lines,line)&&track.cues.size()<kMaxCues){
        int h=0,m=0,s=0,ms=0,h2=0,m2=0,s2=0,ms2=0;
        if(swscanf_s(line.c_str(),L"%d:%d:%d,%d --> %d:%d:%d,%d",&h,&m,&s,&ms,&h2,&m2,&s2,&ms2)!=8)continue;
        SubtitleCue cue;
        cue.begin=h*3600.0+m*60.0+s+ms/1000.0;
        cue.end=h2*3600.0+m2*60.0+s2+ms2/1000.0;
        while(std::getline(lines,line)){
            if(!line.empty()&&line.back()==L'\r')line.pop_back();
            if(trim(line).empty())break;
            if(!cue.text.empty())cue.text+=L'\n';
            cue.text+=line;
        }
        cue.text=trim(cue.text);
        if(cue.end>cue.begin&&!cue.text.empty())track.cues.push_back(std::move(cue));
    }
    return track;
}

SubtitleTrack parseAss(const std::wstring& data,const std::wstring& name,bool ssa){
    SubtitleTrack track;track.name=name;track.codec=ssa?L"SSA":L"ASS";
    track.styles.push_back(SubtitleStyle{});
    std::map<std::wstring,int> styleIndex;
    std::wistringstream lines(data);std::wstring line;
    enum class Section {None,Script,Styles,Events} section=Section::None;
    std::vector<std::wstring> styleFields,eventFields;
    // Format lines list every field; value lines keep commas inside the last
    // field (ASS dialogue text is allowed to contain commas).
    auto split=[&](const std::wstring& source,const std::vector<std::wstring>& fields,std::vector<std::wstring>& out){
        out.clear();
        size_t start=0;
        const size_t limit=fields.empty()?source.size():fields.size();
        for(size_t i=0;i<source.size()&&out.size()+1<limit;++i){
            if(source[i]!=L',')continue;
            out.push_back(trim(source.substr(start,i-start)));
            start=i+1;
        }
        out.push_back(trim(source.substr(start)));
        return !out.empty();
    };
    while(std::getline(lines,line)){
        line=trim(line);
        if(line.empty())continue;
        if(line.front()==L'['){
            const auto marker=lower(line);
            section=marker.find(L"[script info]")!=std::wstring::npos?Section::Script
                   :marker.find(L"[v4+ styles]")!=std::wstring::npos?Section::Styles
                   :marker.find(L"[v4 styles]")!=std::wstring::npos?Section::Styles
                   :marker.find(L"[events]")!=std::wstring::npos?Section::Events:Section::None;
            continue;
        }
        if(section==Section::Script){
            const auto colon=line.find(L':');
            if(colon==std::wstring::npos)continue;
            const auto key=trim(line.substr(0,colon));
            const auto value=trim(line.substr(colon+1));
            if(key==L"PlayResX")track.scriptWidth=std::max(1.0,wcstod(value.c_str(),nullptr));
            else if(key==L"PlayResY")track.scriptHeight=std::max(1.0,wcstod(value.c_str(),nullptr));
            continue;
        }
        if(section==Section::Styles){
            const auto colon=line.find(L':');
            if(colon==std::wstring::npos)continue;
            const auto kind=trim(line.substr(0,colon));
            const auto rest=line.substr(colon+1);
            if(kind==L"Format"){split(rest,{},styleFields);continue;}
            if(kind!=L"Style")continue;
            std::vector<std::wstring> values;
            if(styleFields.size()<4||!split(rest,styleFields,values))continue;
            SubtitleStyle style;style.font=L"Microsoft YaHei UI";
            auto field=[&](const wchar_t* key)->std::wstring{
                for(size_t i=0;i<styleFields.size()&&i<values.size();++i)if(styleFields[i]==key)return values[i];
                return {};
            };
            const auto styleName=field(L"Name");
            if(auto font=field(L"Fontname");!font.empty())style.font=font;
            if(auto size=field(L"Fontsize");!size.empty())style.size=wcstod(size.c_str(),nullptr);
            style.primary=assColor(field(L"PrimaryColour"),style.primary);
            style.outline=assColor(field(L"OutlineColour"),style.outline);
            style.back=assColor(field(L"BackColour"),style.back);
            if(auto width=field(L"Outline");!width.empty())style.outlineWidth=wcstod(width.c_str(),nullptr);
            if(auto shadow=field(L"Shadow");!shadow.empty())style.shadow=wcstod(shadow.c_str(),nullptr);
            if(auto align=field(L"Alignment");!align.empty()){const int value=_wtoi(align.c_str());style.alignment=value>0?value:2;}
            if(auto bold=field(L"Bold");!bold.empty())style.bold=_wtoi(bold.c_str())!=0;
            if(auto italic=field(L"Italic");!italic.empty())style.italic=_wtoi(italic.c_str())!=0;
            if(auto margin=field(L"MarginL");!margin.empty())style.marginL=wcstod(margin.c_str(),nullptr);
            if(auto margin=field(L"MarginR");!margin.empty())style.marginR=wcstod(margin.c_str(),nullptr);
            if(auto margin=field(L"MarginV");!margin.empty())style.marginV=wcstod(margin.c_str(),nullptr);
            styleIndex[styleName]=int(track.styles.size());
            track.styles.push_back(std::move(style));
            continue;
        }
        if(section==Section::Events){
            const auto colon=line.find(L':');
            if(colon==std::wstring::npos)continue;
            const auto kind=trim(line.substr(0,colon));
            const auto rest=line.substr(colon+1);
            if(kind==L"Format"){split(rest,{},eventFields);continue;}
            if(kind!=L"Dialogue"&&kind!=L"Comment")continue;
            if(kind==L"Comment")continue;
            std::vector<std::wstring> values;
            if(eventFields.size()<4||!split(rest,eventFields,values))continue;
            auto field=[&](const wchar_t* key)->std::wstring{
                for(size_t i=0;i<eventFields.size()&&i<values.size();++i)if(eventFields[i]==key)return values[i];
                return {};
            };
            SubtitleCue cue;
            if(!parseClock(trim(field(L"Start")),cue.begin)||!parseClock(trim(field(L"End")),cue.end))continue;
            const auto styleName=field(L"Style");
            const auto found=styleIndex.find(styleName);
            cue.style=found!=styleIndex.end()?found->second:0;
            const auto text=field(L"Text");
            cue.text=cleanAssText(text,cue.alignOverride,cue.posX,cue.posY);
            if(cue.end>cue.begin&&!cue.text.empty()&&track.cues.size()<kMaxCues)track.cues.push_back(std::move(cue));
        }
    }
    return track;
}

// Matroska/MP4 sometimes hand us a whole ASS dialogue line (Layer,Start,End,
// Style,...) instead of just the text; keep the text field only.
std::wstring stripDialoguePrefix(const std::wstring& raw){
    // Two shapes exist: the FFmpeg AVSubtitle ASS rectangle
    // (Layer,Start,End,Style,Name,ML,MR,MV,Effect,Text) and the Matroska stored
    // form (Layer,0,Style,Name,ML,MR,MV,Effect,Text - the times live in the
    // block, so Start/End collapse into one zero). Both end with an empty
    // Effect field, which is what makes the split unambiguous: subtitle text
    // itself may contain commas.
    std::vector<std::wstring> fields;
    size_t start=0;
    for(size_t i=0;i<raw.size()&&fields.size()<10;++i){
        if(raw[i]!=L',')continue;
        fields.push_back(trim(raw.substr(start,i-start)));
        start=i+1;
    }
    if(fields.size()<8)return raw;
    auto numeric=[](const std::wstring& value){if(value.empty())return false;for(wchar_t c:value)if(c<L'0'||c>L'9')return false;return true;};
    const bool matroskaShape=fields.size()>=8&&numeric(fields[0])&&numeric(fields[1])&&numeric(fields[4])&&numeric(fields[5])&&numeric(fields[6])&&fields[7].empty();
    const bool dialogueShape=fields.size()>=9&&numeric(fields[0])&&fields[7].empty()&&fields[8].empty();
    if(!matroskaShape&&!dialogueShape)return raw;
    return raw.substr(start);
}

SubtitleTrack parseVtt(const std::wstring& data,const std::wstring& name){
    SubtitleTrack track;track.name=name;track.codec=L"WebVTT";
    track.styles.push_back(SubtitleStyle{});
    std::wistringstream lines(data);std::wstring line;
    while(std::getline(lines,line)&&track.cues.size()<kMaxCues){
        if(line.find(L"-->")==std::wstring::npos)continue;
        const auto arrow=line.find(L"-->");
        const auto begin=trim(line.substr(0,arrow));
        auto rest=trim(line.substr(arrow+3));
        const auto space=rest.find(L' ');
        const auto end=space==std::wstring::npos?rest:rest.substr(0,space);
        SubtitleCue cue;
        if(!parseClock(begin,cue.begin)||!parseClock(end,cue.end))continue;
        while(std::getline(lines,line)){
            if(line.find(L"-->")!=std::wstring::npos){std::wistringstream back(line);break;}
            if(trim(line).empty())break;
            if(!cue.text.empty())cue.text+=L'\n';
            cue.text+=line;
        }
        cue.text=trim(cue.text);
        if(cue.end>cue.begin&&!cue.text.empty())track.cues.push_back(std::move(cue));
    }
    return track;
}

double rms(const float* samples,size_t count){
    if(count==0)return 0;
    double sum=0;
    for(size_t i=0;i<count;++i)sum+=double(samples[i])*samples[i];
    return std::sqrt(sum/double(count));
}
} // namespace

void SubtitleTrack::rebuildIndex(){
    std::stable_sort(cues.begin(),cues.end(),[](const SubtitleCue& a,const SubtitleCue& b){return a.begin<b.begin;});
    starts.clear();starts.reserve(cues.size());
    for(const auto& cue:cues)starts.push_back(cue.begin);
}

bool isTextSubtitleCodec(const std::wstring& codec){
    const auto name=lower(codec);
    return name==L"subrip"||name==L"srt"||name==L"ass"||name==L"ssa"||name==L"mov_text"
        ||name==L"webvtt"||name==L"text"||name==L"subviewer"||name==L"subviewer1"||name==L"microdvd";
}

SubtitleTrack loadSubtitleFile(const std::wstring& path){
    std::wstring data;
    if(!readTextFile(path,data)){
        log::warn("subtitle",std::format("cannot read subtitle file (missing, empty, >{}MiB or not UTF-8/UTF-16LE): {}",kMaxSubtitleFileBytes>>20,utf8(std::filesystem::path(path).filename().wstring())));
        return {};
    }
    std::wstring extension=lower(std::filesystem::path(path).extension().wstring());
    const std::wstring name=std::filesystem::path(path).filename().wstring();
    const auto head=data.substr(0,std::min<size_t>(data.size(),512));
    const auto headLower=lower(head);
    SubtitleTrack track;
    if(extension==L".ass"||extension==L".ssa")track=parseAss(data,name,extension==L".ssa");
    else if(extension==L".vtt"||headLower.find(L"webvtt")!=std::wstring::npos)track=parseVtt(data,name);
    else if(headLower.find(L"[script info]")!=std::wstring::npos||headLower.find(L"dialogue:")!=std::wstring::npos)track=parseAss(data,name,false);
    else track=parseSrt(data,name);
    track.rebuildIndex();   // cue lookup is binary searched, so it needs the index
    return track;
}

std::vector<const SubtitleCue*> cuesAt(const SubtitleTrack& track,double seconds,size_t limit){
    std::vector<const SubtitleCue*> found;
    if(track.starts.empty())return found;
    auto it=std::upper_bound(track.starts.begin(),track.starts.end(),seconds);
    if(it==track.starts.begin())return found;
    size_t index=size_t(it-track.starts.begin())-1;
    const size_t budget=std::min<size_t>(index+1,256);
    for(size_t scanned=0;scanned<budget;++scanned){
        const auto& cue=track.cues[index];
        if(cue.begin<=seconds&&seconds<cue.end){
            found.push_back(&cue);
            if(found.size()>=limit)break;
        }
        if(index==0)break;
        if(seconds-track.cues[index].begin>3600.0)break;
        --index;
    }
    std::reverse(found.begin(),found.end());
    return found;
}

std::wstring textAt(const SubtitleTrack& track,double seconds){
    const auto cues=cuesAt(track,seconds,3);
    std::wstring text;
    for(const auto* cue:cues){
        if(cue->text.empty())continue;
        if(!text.empty())text+=L'\n';
        text+=cue->text;
    }
    return text;
}

std::vector<SubtitleCue> loadSrt(const std::wstring& path){
    auto track=loadSubtitleFile(path);
    track.rebuildIndex();
    return track.cues;
}

std::wstring subtitleAt(const std::vector<SubtitleCue>& cues,double seconds){
    for(const auto& cue:cues)if(cue.begin<=seconds&&seconds<cue.end)return cue.text;
    return {};
}

std::vector<uint32_t> SubtitleBitmap::pixels()const{
    if(width<=0||height<=0||width>8192||height>8192)return {};
    const size_t count=size_t(width)*height;
    std::vector<uint32_t> result;result.reserve(count);
    for(const auto run:runs){const auto n=run>>8;if(!n||n>count-result.size())return {};result.insert(result.end(),n,palette[run&255]);}
    if(result.size()!=count)return {};
    return result;
}

std::vector<SubtitleTrack> loadEmbeddedSubtitleTracks(const std::wstring& path,std::stop_token stop,
    const std::function<void(const std::vector<SubtitleTrack>&)>& metadata){
    std::vector<SubtitleTrack> tracks;
    if(stop.stop_requested())return tracks;
    const auto started=std::chrono::steady_clock::now();
    AVFormatContext* input=avformat_alloc_context();
    if(!input)return tracks;
    input->interrupt_callback={[](void* opaque){return static_cast<std::stop_token*>(opaque)->stop_requested()?1:0;},&stop};
    struct InputCleanup {AVFormatContext*& input;~InputCleanup(){avformat_close_input(&input);}} cleanup{input};
    const auto utf8Path=utf8(path);
    if(avformat_open_input(&input,utf8Path.c_str(),nullptr,nullptr)<0||!input){
        log::warn("subtitle","cannot open media for embedded subtitle listing");
        return tracks;
    }
    if(avformat_find_stream_info(input,nullptr)<0){
        return tracks;
    }
    struct DecoderCleanup {void operator()(AVCodecContext* context)const{avcodec_free_context(&context);}};
    std::vector<std::unique_ptr<AVCodecContext,DecoderCleanup>> decoders;
    std::vector<int> streamToTrack(input->nb_streams,-1);
    for(unsigned streamIndex=0;streamIndex<input->nb_streams;++streamIndex){
        if(stop.stop_requested())return {};
        AVStream* stream=input->streams[streamIndex];
        if(stream->codecpar->codec_type!=AVMEDIA_TYPE_SUBTITLE)continue;
        SubtitleTrack track;
        track.embedded=true;
        track.streamIndex=int(streamIndex);
        const char* codecName=avcodec_get_name(stream->codecpar->codec_id);
        track.codec=wide(codecName?codecName:"");
        if(stream->metadata){
            if(const AVDictionaryEntry* language=av_dict_get(stream->metadata,"language",nullptr,0))track.language=wide(language->value);
            if(const AVDictionaryEntry* title=av_dict_get(stream->metadata,"title",nullptr,0))track.name=wide(title->value);
        }
        if(track.name.empty())track.name=track.language.empty()?std::format(L"Subtitle track {}",tracks.size()+1):track.language;
        track.styles.push_back(SubtitleStyle{});
        streamToTrack[streamIndex]=int(tracks.size());
        decoders.emplace_back();
        const auto codec=stream->codecpar->codec_id;
        if(!isTextSubtitleCodec(track.codec)&&codec!=AV_CODEC_ID_HDMV_PGS_SUBTITLE&&codec!=AV_CODEC_ID_DVD_SUBTITLE&&codec!=AV_CODEC_ID_DVB_SUBTITLE){
            track.note=L"This subtitle format is not supported yet";
            log::info("subtitle",std::format("embedded track {} codec={} listed as unsupported",streamIndex,codecName?codecName:""));
            tracks.push_back(std::move(track));
            continue;
        }
        const AVCodec* decoder=avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext* context=decoder?avcodec_alloc_context3(decoder):nullptr;
        if(context){context->pkt_timebase=stream->time_base;}
        if(!decoder||!context||avcodec_parameters_to_context(context,stream->codecpar)<0||avcodec_open2(context,decoder,nullptr)<0){
            track.note=L"Subtitle decoder initialization failed";
            if(context)avcodec_free_context(&context);
            tracks.push_back(std::move(track));
            continue;
        }
        decoders.back().reset(context);
        tracks.push_back(std::move(track));
    }
    if(metadata&&!stop.stop_requested())metadata(tracks);
    if(std::none_of(decoders.begin(),decoders.end(),[](const auto& decoder){return bool(decoder);}))return tracks;
    AVPacket* packet=av_packet_alloc();
    struct PacketCleanup {AVPacket*& packet;~PacketCleanup(){av_packet_free(&packet);}} packetCleanup{packet};
    constexpr size_t maxCacheBytes=64u*1024u*1024u;
    size_t cacheBytes=0,totalCues=0;uint64_t packets=0;
    int videoWidth=1920,videoHeight=1080;
    for(unsigned i=0;i<input->nb_streams;++i)if(input->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
        videoWidth=std::max(1,input->streams[i]->codecpar->width);videoHeight=std::max(1,input->streams[i]->codecpar->height);break;
    }
    auto nextPublish=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
    while(packet&&!stop.stop_requested()&&av_read_frame(input,packet)>=0){
            ++packets;
            const int index=packet->stream_index>=0&&size_t(packet->stream_index)<streamToTrack.size()?streamToTrack[packet->stream_index]:-1;
            if(index<0||!decoders[index]){av_packet_unref(packet);continue;}
            auto& track=tracks[index];auto* context=decoders[index].get();
            const double timeBase=av_q2d(input->streams[packet->stream_index]->time_base);
            AVSubtitle subtitle{};
            int got=0;
            if(avcodec_decode_subtitle2(context,&subtitle,&got,packet)<0||!got){avsubtitle_free(&subtitle);av_packet_unref(packet);continue;}
            const double packetSeconds=(subtitle.pts!=AV_NOPTS_VALUE?double(subtitle.pts)/AV_TIME_BASE:
                packet->pts==AV_NOPTS_VALUE?0:double(packet->pts)*timeBase);
            const bool bitmapCodec=context->codec_id==AV_CODEC_ID_HDMV_PGS_SUBTITLE||context->codec_id==AV_CODEC_ID_DVD_SUBTITLE||context->codec_id==AV_CODEC_ID_DVB_SUBTITLE;
            if(bitmapCodec){
                const double begin=packetSeconds+double(subtitle.start_display_time)/1000.0;
                // A new display set (including an empty clear set) replaces the old one.
                if(!track.cues.empty()&&begin>=track.cues.back().begin){
                    if(begin==track.cues.back().begin)track.cues.pop_back();
                    else track.cues.back().end=std::min(track.cues.back().end,begin);
                }
                auto frame=std::make_shared<SubtitleBitmapFrame>();
                frame->width=context->width>0?context->width:videoWidth;
                frame->height=context->height>0?context->height:videoHeight;
                size_t bytes=sizeof(SubtitleCue)+sizeof(SubtitleBitmapFrame);
                bool valid=bytes<=maxCacheBytes-cacheBytes&&subtitle.num_rects<=256;
                for(unsigned r=0;r<subtitle.num_rects&&valid;++r){
                    const auto* rect=subtitle.rects[r];
                    if(!rect||rect->type!=SUBTITLE_BITMAP)continue;
                    if(rect->w<=0||rect->h<=0||rect->w>8192||rect->h>8192||!rect->data[0]||!rect->data[1]||rect->linesize[0]<rect->w||rect->nb_colors<1||rect->nb_colors>256){valid=false;break;}
                    if(sizeof(SubtitleBitmap)>maxCacheBytes-cacheBytes-bytes){valid=false;break;}
                    const size_t runBudget=(maxCacheBytes-cacheBytes-bytes-sizeof(SubtitleBitmap))/sizeof(uint32_t);
                    SubtitleBitmap image;image.x=rect->x;image.y=rect->y;image.width=rect->w;image.height=rect->h;
                    for(int p=0;p<rect->nb_colors;++p){
                        uint32_t c=0;std::memcpy(&c,rect->data[1]+p*4,4);const uint32_t a=c>>24;
                        image.palette[p]=(a<<24)|((((c>>16)&255)*a/255)<<16)|((((c>>8)&255)*a/255)<<8)|((c&255)*a/255);
                    }
                    for(int y=0;y<rect->h&&valid;++y)for(int x=0;x<rect->w;++x){
                        const uint8_t pixel=rect->data[0][size_t(y)*rect->linesize[0]+x];
                        if(pixel>=rect->nb_colors){valid=false;break;}
                        if(!image.runs.empty()&&(image.runs.back()&255)==pixel&&(image.runs.back()>>8)<0xFFFFFF)image.runs.back()+=256;
                        else {if(image.runs.size()>=runBudget){valid=false;break;}image.runs.push_back(256u|pixel);}
                    }
                    if(!valid)break;
                    bytes+=sizeof(SubtitleBitmap)+image.runs.size()*sizeof(uint32_t);
                    frame->images.push_back(std::move(image));
                }
                if(valid&&!frame->images.empty()){
                    SubtitleCue cue;cue.begin=begin;
                    cue.end=subtitle.end_display_time>subtitle.start_display_time&&subtitle.end_display_time!=UINT32_MAX?
                        packetSeconds+double(subtitle.end_display_time)/1000.0:std::numeric_limits<double>::infinity();
                    cue.bitmap=std::move(frame);
                    if(totalCues<kMaxCues&&bytes<=maxCacheBytes-cacheBytes){cacheBytes+=bytes;++totalCues;track.cues.push_back(std::move(cue));}
                    else track.note=L"Subtitle cache reached its limit; later subtitles were not loaded";
                }else if(!valid){track.note=L"Graphic subtitle data is invalid or exceeds the cache limit";log::warn("subtitle","bitmap display rejected: invalid rectangle or cache budget");}
            }
            for(unsigned rect=0;rect<subtitle.num_rects;++rect){
                const auto* entry=subtitle.rects[rect];
                if(!entry)continue;
                std::wstring text;
                int alignOverride=0;double posX=-1,posY=-1;
                const std::wstring raw=entry->ass&&*entry->ass?wide(entry->ass):(entry->text&&*entry->text?wide(entry->text):std::wstring{});
                if(!raw.empty())text=cleanAssText(stripDialoguePrefix(raw),alignOverride,posX,posY);
                if(text.empty())continue;
                SubtitleCue cue;
                cue.begin=packetSeconds+double(subtitle.start_display_time)/1000.0;
                cue.end=subtitle.end_display_time>subtitle.start_display_time?packetSeconds+double(subtitle.end_display_time)/1000.0:cue.begin+3.0;
                cue.text=std::move(text);
                cue.alignOverride=alignOverride;cue.posX=posX;cue.posY=posY;
                const size_t bytes=sizeof(SubtitleCue)+sizeof(double)+cue.text.size()*sizeof(wchar_t);
                if(cue.end>cue.begin){
                    if(totalCues>=kMaxCues||bytes>maxCacheBytes-cacheBytes){track.note=L"Subtitle cache reached its limit; later subtitles were not loaded";continue;}
                    cacheBytes+=bytes;++totalCues;track.cues.push_back(std::move(cue));
                }
            }
            avsubtitle_free(&subtitle);
            av_packet_unref(packet);
            if(metadata&&std::chrono::steady_clock::now()>=nextPublish){
                for(auto& t:tracks)t.rebuildIndex();
                metadata(tracks);
                nextPublish=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            }
    }
    if(stop.stop_requested())return {};
    for(auto& track:tracks){
        track.rebuildIndex();
        log::info("subtitle",std::format("embedded track {} codec={} cues={}",track.streamIndex,utf8(track.codec),track.cues.size()));
    }
    log::info("subtitle",std::format("single-pass complete tracks={} packets={} cues={} cacheBytes={} elapsedMs={:.3f}",tracks.size(),packets,totalCues,cacheBytes,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()));
    return tracks;
}

struct SubtitleLoader::Impl {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::wstring pending;
    uint64_t generation=0;
    bool requested=false;
    std::stop_source cancel;
    std::optional<Result> ready;
    std::jthread worker;
    Impl():worker([this](std::stop_token shutdown){
        while(!shutdown.stop_requested()){
            std::unique_lock lock(mutex);
            if(!wake.wait(lock,shutdown,[&]{return requested;}))break;
            const auto path=std::move(pending);const auto id=generation;const auto stop=cancel.get_token();requested=false;
            lock.unlock();
            auto publish=[&](std::vector<SubtitleTrack> tracks,bool complete){
                std::lock_guard guard(mutex);
                if(id==generation&&!stop.stop_requested())ready=Result{id,complete,std::move(tracks)};
            };
            try{
                std::vector<SubtitleTrack> external;
                if(path.empty()||path.starts_with(L"capture:")||path.starts_with(L"capture2:")||path.starts_with(L"remoteplay:")||path.starts_with(L"screen:")){publish({},true);continue;}
                for(const wchar_t* extension:{L".srt",L".ass",L".ssa",L".vtt"}){
                    if(stop.stop_requested())break;
                    auto candidate=std::filesystem::path(path);candidate.replace_extension(extension);
                    std::error_code ec;if(!std::filesystem::exists(candidate,ec))continue;
                    auto track=loadSubtitleFile(candidate.wstring());if(!track.usable())continue;
                    track.name=std::format(L"External · {}",candidate.filename().wstring());external.push_back(std::move(track));break;
                }
                auto embedded=loadEmbeddedSubtitleTracks(path,stop,[&](const auto& tracks){auto listing=external;listing.insert(listing.end(),tracks.begin(),tracks.end());publish(std::move(listing),false);});
                for(auto& track:embedded)external.push_back(std::move(track));
                publish(std::move(external),true);
            }catch(const std::exception& e){log::error("subtitle",std::format("loader failed: {}",e.what()));publish({},true);}
        }
    }){}
    ~Impl(){cancel.request_stop();worker.request_stop();wake.notify_all();worker.join();}
};
SubtitleLoader::SubtitleLoader():p_(std::make_unique<Impl>()){}
SubtitleLoader::~SubtitleLoader()=default;
uint64_t SubtitleLoader::request(std::wstring path){
    std::lock_guard lock(p_->mutex);p_->cancel.request_stop();p_->cancel=std::stop_source{};
    p_->pending=std::move(path);p_->requested=true;p_->ready.reset();++p_->generation;p_->wake.notify_all();return p_->generation;
}
std::optional<SubtitleLoader::Result> SubtitleLoader::poll(){std::lock_guard lock(p_->mutex);auto result=std::move(p_->ready);p_->ready.reset();return result;}

SubtitleAlignResult alignSubtitleToAudio(const std::wstring& mediaPath,const SubtitleTrack& track,int maxShiftSeconds,std::stop_token stop){
    SubtitleAlignResult result;
    if(track.cues.empty()){result.detail=L"No usable subtitle content";return result;}
    AVFormatContext* input=nullptr;
    const auto utf8Path=utf8(mediaPath);
    if(avformat_open_input(&input,utf8Path.c_str(),nullptr,nullptr)<0||!input){result.detail=L"Could not open media";return result;}
    if(avformat_find_stream_info(input,nullptr)<0){avformat_close_input(&input);result.detail=L"Could not read media information";return result;}
    const int audioIndex=av_find_best_stream(input,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
    if(audioIndex<0){avformat_close_input(&input);result.detail=L"This file has no audio track; automatic alignment is not possible";return result;}
    AVStream* stream=input->streams[audioIndex];
    const AVCodec* decoder=avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* context=decoder?avcodec_alloc_context3(decoder):nullptr;
    if(!decoder||!context||avcodec_parameters_to_context(context,stream->codecpar)<0||avcodec_open2(context,decoder,nullptr)<0){
        if(context)avcodec_free_context(&context);
        avformat_close_input(&input);
        result.detail=L"Audio track decoder is unavailable";
        log::info("subtitle",std::format("auto align skipped: {}",utf8(result.detail)));
        return result;
    }
    constexpr int kSampleRate=8000;
    constexpr double kStepSeconds=0.02; // 20ms analysis grid
    AVChannelLayout mono=AV_CHANNEL_LAYOUT_MONO;
    SwrContext* resampler=nullptr;
    AVChannelLayout inputLayout{};
    if(av_channel_layout_copy(&inputLayout,&context->ch_layout)<0)av_channel_layout_default(&inputLayout,context->ch_layout.nb_channels>0?context->ch_layout.nb_channels:2);
    if(swr_alloc_set_opts2(&resampler,&mono,AV_SAMPLE_FMT_FLT,kSampleRate,&inputLayout,context->sample_fmt,context->sample_rate,0,nullptr)<0||!resampler||swr_init(resampler)<0){
        if(resampler)swr_free(&resampler);
        av_channel_layout_uninit(&inputLayout);
        avcodec_free_context(&context);
        avformat_close_input(&input);
        result.detail=L"Audio resampler initialization failed";
        log::info("subtitle",std::format("auto align skipped: {}",utf8(result.detail)));
        return result;
    }
    std::vector<float> envelope;
    std::vector<float> converted;
    std::vector<float> carry;
    AVPacket* packet=av_packet_alloc();
    AVFrame* frame=av_frame_alloc();
    const double limitSeconds=1800.0;
    while(packet&&frame&&!stop.stop_requested()&&av_read_frame(input,packet)>=0){
        if(packet->stream_index!=audioIndex){av_packet_unref(packet);continue;}
        if(avcodec_send_packet(context,packet)<0){av_packet_unref(packet);continue;}
        av_packet_unref(packet);
        while(avcodec_receive_frame(context,frame)>=0){
            const int maxOut=int(swr_get_out_samples(resampler,frame->nb_samples));
            if(maxOut<=0)continue;
            converted.resize(size_t(maxOut));
            uint8_t* output=reinterpret_cast<uint8_t*>(converted.data());
            const int produced=swr_convert(resampler,&output,maxOut,const_cast<const uint8_t**>(frame->extended_data),frame->nb_samples);
            if(produced<=0)continue;
            const size_t samplesPerStep=size_t(kSampleRate*kStepSeconds);
            carry.insert(carry.end(),converted.begin(),converted.begin()+produced);
            while(carry.size()>=samplesPerStep){
                envelope.push_back(float(rms(carry.data(),samplesPerStep)));
                carry.erase(carry.begin(),carry.begin()+std::ptrdiff_t(samplesPerStep));
            }
            if(envelope.size()*kStepSeconds>limitSeconds)break;
        }
        if(envelope.size()*kStepSeconds>limitSeconds)break;
    }
    if(packet)av_packet_free(&packet);
    if(frame)av_frame_free(&frame);
    swr_free(&resampler);
    av_channel_layout_uninit(&inputLayout);
    avcodec_free_context(&context);
    avformat_close_input(&input);
    if(envelope.size()<int(5.0/kStepSeconds)){
        result.detail=L"Audio track is too short for automatic alignment";
        log::info("subtitle",std::format("auto align skipped: {}",utf8(result.detail)));
        return result;
    }
    std::vector<float> sorted=envelope;
    std::sort(sorted.begin(),sorted.end());
    const float loud=sorted[size_t(double(sorted.size())*0.9)];
    const float threshold=std::max(1e-5f,loud*0.2f);   // anything clearly above silence counts as speech
    std::vector<uint8_t> audioActive(envelope.size());
    for(size_t i=0;i<envelope.size();++i)audioActive[i]=envelope[i]>threshold?1:0;
    const double mediaSeconds=double(envelope.size())*kStepSeconds;
    std::vector<uint8_t> subtitleActive(envelope.size());
    for(const auto& cue:track.cues){
        const double begin=cue.begin+double(track.offsetMs)/1000.0;
        const double end=cue.end+double(track.offsetMs)/1000.0;
        if(end<=0||begin>=mediaSeconds)continue;
        const size_t first=size_t(std::max(0.0,begin)/kStepSeconds);
        const size_t last=std::min<size_t>(subtitleActive.size(),size_t(std::max(0.0,end)/kStepSeconds)+1);
        for(size_t i=first;i<last;++i)subtitleActive[i]=1;
    }
    size_t subtitleCount=0;
    for(auto value:subtitleActive)subtitleCount+=value;
    size_t audioCount=0;
    for(auto value:audioActive)audioCount+=value;
    if(subtitleCount<20){
        result.detail=L"Subtitle active duration is too short for reliable alignment";
        log::info("subtitle",std::format("auto align skipped: {}",utf8(result.detail)));
        return result;
    }
    const int maxShiftSteps=int(double(maxShiftSeconds)/kStepSeconds);
    double bestScore=-1;int bestShift=0;
    for(int shift=-maxShiftSteps;shift<=maxShiftSteps;++shift){
        size_t matches=0,considered=0;
        for(size_t i=0;i<subtitleActive.size();++i){
            if(!subtitleActive[i])continue;
            const long target=long(i)+shift;
            if(target<0||target>=long(audioActive.size()))continue;
            ++considered;
            if(audioActive[size_t(target)])++matches;
        }
        // Overlap gate plus a global F1 denominator: a shift that pushes most
        // cues out of the analysed span cannot win on a handful of matches.
        if(considered<double(subtitleCount)*0.5)continue;
        const double score=2.0*double(matches)/double(std::max<size_t>(1,subtitleCount+audioCount));
        const double penalty=double(std::abs(shift))/double(maxShiftSteps+1)*0.02; // prefer small shifts on ties
        if(score-penalty>bestScore){bestScore=score-penalty;bestShift=shift;}
    }
    result.ok=bestScore>0.25;
    result.offsetMs=int(std::lround(bestShift*kStepSeconds*1000.0));
    result.score=bestScore;
    result.detail=result.ok?std::format(L"Correlation {:.2f}, suggested offset {} ms",bestScore,result.offsetMs)
                          :std::format(L"Correlation is only {:.2f}; no reliable offset found (subtitles may not match the video)",bestScore);
    log::info("subtitle",std::format("auto align media={} cues={} shift={}ms score={:.3f} ok={}",std::filesystem::path(mediaPath).filename().string(),track.cues.size(),result.offsetMs,result.score,result.ok));
    return result;
}
} // namespace veyra::engine
