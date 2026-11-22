// On-screen subtitle reading (ScreenText.h).
// Ported from VI Translator's ocr.rs / main.rs (MIT License, Copyright (c) 2026 Fouad Almalki).
#include "veyra/engine/ScreenText.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cwctype>
#endif
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <unordered_set>

namespace veyra::engine::screentext {
namespace {
// Character classes (the Windows CT_CTYPE1 bits).
constexpr uint16_t kUpper=0x0001,kLower=0x0002,kSpace=0x0008,kAlpha=0x0100;

uint16_t ctype(wchar_t c){
#ifdef _WIN32
    WORD type=0;
    if(!GetStringTypeW(CT_CTYPE1,&c,1,&type))return 0;
    return uint16_t(type);
#else
    uint16_t type=0;
    if(std::iswalpha(wint_t(c)))type|=kAlpha;
    if(std::iswupper(wint_t(c)))type|=kUpper;
    if(std::iswlower(wint_t(c)))type|=kLower;
    if(std::iswspace(wint_t(c)))type|=kSpace;
    return type;
#endif
}
bool asciiUpper(wchar_t c){return c>=L'A'&&c<=L'Z';}
bool asciiLower(wchar_t c){return c>=L'a'&&c<=L'z';}
bool asciiAlpha(wchar_t c){return asciiUpper(c)||asciiLower(c);}
bool asciiDigit(wchar_t c){return c>=L'0'&&c<=L'9';}
bool asciiAlnum(wchar_t c){return asciiAlpha(c)||asciiDigit(c);}
bool isLetter(wchar_t c){return c<128?asciiAlpha(c):(ctype(c)&kAlpha)!=0;}
bool isAlnum(wchar_t c){return asciiDigit(c)||isLetter(c);}
bool isUpper(wchar_t c){return c<128?asciiUpper(c):(ctype(c)&kUpper)!=0;}
bool isLower(wchar_t c){return c<128?asciiLower(c):(ctype(c)&kLower)!=0;}
bool isSpace(wchar_t c){return c<128?(c==L' '||(c>=9&&c<=13)):(ctype(c)&kSpace)!=0;}
wchar_t toLower(wchar_t c){
    if(c<128)return asciiUpper(c)?wchar_t(c+32):c;
#ifdef _WIN32
    wchar_t out=c;
    if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,&c,1,&out,1,nullptr,nullptr,0)!=1)return c;
    return out;
#else
    return wchar_t(std::towlower(wint_t(c)));
#endif
}
wchar_t toUpper(wchar_t c){
    if(c<128)return asciiLower(c)?wchar_t(c-32):c;
#ifdef _WIN32
    wchar_t out=c;
    if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,&c,1,&out,1,nullptr,nullptr,0)!=1)return c;
    return out;
#else
    return wchar_t(std::towupper(wint_t(c)));
#endif
}
std::wstring lowered(std::wstring s){for(auto& c:s)c=toLower(c);return s;}
bool contains(std::wstring_view set,wchar_t c){return set.find(c)!=std::wstring_view::npos;}

std::vector<std::wstring> splitWhitespace(const std::wstring& s){
    std::vector<std::wstring> out;
    std::wstring cur;
    for(wchar_t c:s){
        if(isSpace(c)){if(!cur.empty())out.push_back(std::move(cur));cur.clear();}
        else cur.push_back(c);
    }
    if(!cur.empty())out.push_back(std::move(cur));
    return out;
}
std::wstring join(const std::vector<std::wstring>& parts,std::wstring_view sep=L" "){
    std::wstring out;
    for(size_t i=0;i<parts.size();++i){if(i)out+=sep;out+=parts[i];}
    return out;
}
template<class Pred> std::wstring trimMatches(const std::wstring& s,Pred pred){
    size_t a=0,b=s.size();
    while(a<b&&pred(s[a]))++a;
    while(b>a&&pred(s[b-1]))--b;
    return s.substr(a,b-a);
}
std::wstring trimSpace(const std::wstring& s){return trimMatches(s,isSpace);}
std::wstring textOf(const std::vector<Word>& words){
    std::wstring out;
    for(size_t i=0;i<words.size();++i){if(i)out+=L' ';out+=words[i].text;}
    return out;
}
uint32_t toU32(double v){
    if(!(v>0))return 0;
    if(v>=4294967295.0)return 4294967295u;
    return uint32_t(v);
}
uint32_t median(std::vector<uint32_t> v){
    std::sort(v.begin(),v.end());
    return v[v.size()/2];
}

uint8_t prepValue(Prep mode,int r,int g,int b){
    switch(mode){
    case Prep::WhiteOrColour:{
        const int mx=std::max({r,g,b}),mn=std::min({r,g,b});
        const int v=mx-mn>80?mx:mn; // coloured: its brightness; grey/white: its whiteness
        return uint8_t(std::min(255,(std::max(0,v-150)*255)/105));
    }
    case Prep::Brightest:
    case Prep::Outlined:
    default:
        return uint8_t(std::max({r,g,b}));
    }
}

// Clear the pieces of a mask (8-connected) smaller than minPx pixels or shorter than minH rows.
void dropSpecks(std::vector<bool>& m,size_t w,size_t h,size_t minH,size_t minPx){
    std::vector<bool> seen(w*h,false);
    std::vector<size_t> stack,piece;
    for(size_t start=0;start<w*h;++start){
        if(!m[start]||seen[start])continue;
        seen[start]=true;
        stack.push_back(start);
        piece.clear();
        size_t y0=start/w,y1=start/w;
        while(!stack.empty()){
            const size_t i=stack.back();stack.pop_back();
            piece.push_back(i);
            const size_t x=i%w,y=i/w;
            y0=std::min(y0,y);y1=std::max(y1,y);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                const long long nx=(long long)x+dx,ny=(long long)y+dy;
                if(nx<0||ny<0||size_t(nx)>=w||size_t(ny)>=h)continue;
                const size_t j=size_t(ny)*w+size_t(nx);
                if(m[j]&&!seen[j]){seen[j]=true;stack.push_back(j);}
            }
        }
        if(piece.size()<minPx||y1-y0+1<minH)for(size_t i:piece)m[i]=false;
    }
}

// Letters in the words of `words` that `other` (another reading of the same text) has too.
size_t agreed(const std::vector<Word>& words,const std::wstring& other){
    auto norm=[](const std::wstring& t){return lowered(trimMatches(t,[](wchar_t c){return !isAlnum(c);}));};
    std::unordered_set<std::wstring> seen;
    for(const auto& t:splitWhitespace(other)){auto n=norm(t);if(!n.empty())seen.insert(std::move(n));}
    size_t letters=0;
    for(const auto& w:words){const auto n=norm(w.text);if(seen.count(n))letters+=n.size();}
    return letters;
}

// Two words OCR glued with a dot it saw in the gap: "my.leg" -> "my leg" (both sides at
// least two lower-case letters, so "e.g." and "U.S." stay).
std::wstring splitGlued(const std::wstring& w){
    const auto dot=w.find(L'.');
    if(dot==std::wstring::npos)return w;
    const std::wstring a=w.substr(0,dot),b=w.substr(dot+1);
    size_t tail=0;
    while(tail<a.size()&&asciiLower(a[a.size()-1-tail]))++tail;
    size_t lead=0;
    while(lead<b.size()&&asciiLower(b[lead]))++lead;
    // the part before the dot is only those letters (after any leading quote or bracket)
    size_t start=0;
    while(start<a.size()&&!isAlnum(a[start]))++start;
    if(tail>=2&&tail==a.size()-start&&lead>=2&&b.find(L'.')==std::wstring::npos)return a+L" "+b;
    return w;
}
} // namespace

Prepared preprocess(const Crop& crop,Prep mode,uint32_t maxDim){
    Prepared out;
    if(crop.w==0||crop.h==0||crop.rgba.size()<size_t(crop.w)*crop.h*4)return out;
    const uint32_t scale=crop.h<60?2:1;
    const uint32_t w=crop.w*scale,h=crop.h*scale;
    std::vector<bool> inside;
    if(mode==Prep::Outlined)inside=outlinedLetters(crop);
    out.bgra.resize(size_t(w)*h*4);
    for(uint32_t y=0;y<h;++y){
        for(uint32_t x=0;x<w;++x){
            const size_t src=size_t(y/scale)*crop.w+x/scale;
            uint8_t v;
            if(!inside.empty())v=inside[src]?0:255;
            else{
                const uint8_t* p=&crop.rgba[src*4];
                v=uint8_t(255-prepValue(mode,p[0],p[1],p[2]));
            }
            uint8_t* d=&out.bgra[(size_t(y)*w+x)*4];
            d[0]=d[1]=d[2]=v;d[3]=255;
        }
    }
    out.w=w;out.h=h;out.back=1.0/double(scale);
    if(maxDim>=16&&(w>maxDim||h>maxDim)){
        // Very large crops: halve until they fit the engine (rare: a whole 4K frame).
        while(out.w>maxDim||out.h>maxDim){
            const uint32_t nw=std::max(1u,out.w/2),nh=std::max(1u,out.h/2);
            std::vector<uint8_t> next(size_t(nw)*nh*4);
            for(uint32_t y=0;y<nh;++y)for(uint32_t x=0;x<nw;++x){
                for(int c=0;c<4;++c){
                    unsigned sum=0;
                    for(uint32_t dy=0;dy<2;++dy)for(uint32_t dx=0;dx<2;++dx){
                        const uint32_t sx=std::min(out.w-1,x*2+dx),sy=std::min(out.h-1,y*2+dy);
                        sum+=out.bgra[(size_t(sy)*out.w+sx)*4+c];
                    }
                    next[(size_t(y)*nw+x)*4+c]=uint8_t((sum+2)/4);
                }
            }
            out.bgra=std::move(next);out.w=nw;out.h=nh;
        }
        out.back=double(crop.w)/double(out.w);
    }
    return out;
}

std::vector<Word> recognizeWith(const Crop& crop,Prep mode,const Engine& engine,uint32_t maxDim){
    std::vector<Word> words;
    const auto prepared=preprocess(crop,mode,maxDim);
    if(prepared.bgra.empty()||!engine)return words;
    const double back=prepared.back;
    for(auto& r:engine(prepared.bgra.data(),prepared.w,prepared.h)){
        Word w;
        w.text=std::move(r.text);
        w.line=r.line;
        w.rect={toU32(r.x*back),toU32(r.y*back),toU32(std::ceil(r.w*back))+1,toU32(std::ceil(r.h*back))+1};
        words.push_back(std::move(w));
    }
    return words;
}

std::pair<int,int> quality(const std::vector<Word>& words,bool english){
    int good=0,bad=0;
    for(const auto& w:words){
        const auto t=trimMatches(w.text,[](wchar_t c){return contains(L".,!?;:\"'()",c);});
        const int n=int(t.size());
        if(n==0)continue;
        if(n==1&&!asciiAlnum(t[0]))continue; // "@", "©": an icon, judged elsewhere
        const bool lettersOk=std::all_of(t.begin(),t.end(),[&](wchar_t c){
            return c==L'\''||(english?asciiAlpha(c):isLetter(c));
        });
        std::wstring rest;
        for(size_t i=1;i<t.size();++i)if(asciiAlpha(t[i]))rest.push_back(t[i]);
        const bool caseOk=std::all_of(rest.begin(),rest.end(),asciiLower)||std::none_of(t.begin(),t.end(),asciiLower);
        const bool number=std::all_of(t.begin(),t.end(),[](wchar_t c){return asciiDigit(c)||c==L','||c==L'$';});
        if((lettersOk&&caseOk)||number)good+=n;else bad+=n;
    }
    return {good,bad};
}

std::vector<Word> dropJunk(std::vector<Word> words){
    std::vector<Word> out;
    out.reserve(words.size());
    for(auto& w:words){
        const auto t=trimSpace(w.text);
        const size_t letters=size_t(std::count_if(t.begin(),t.end(),isLetter));
        const bool junky=std::any_of(t.begin(),t.end(),[](wchar_t c){return contains(L"\"“”•|=~_^`",c);})
            ||((!t.empty()&&(t.front()==L'\''||t.back()==L'\''))&&letters<=2);
        if(!(junky&&letters<=2))out.push_back(std::move(w));
    }
    return out;
}

std::vector<Word> dropTallWords(std::vector<Word> words){
    if(words.size()<3)return words;
    std::vector<uint32_t> hs;
    for(const auto& w:words)hs.push_back(w.rect.h);
    const uint32_t limit=median(hs)*2;
    std::erase_if(words,[&](const Word& w){return w.rect.h>limit;});
    return words;
}

bool similar(const std::wstring& a0,const std::wstring& b0){
    if(a0.empty()||b0.empty())return false;
    const auto a=lowered(a0),b=lowered(b0);
    const size_t la=a.size(),lb=b.size(),longest=std::max(la,lb);
    // the edit distance is at least the difference in length: too different without working it out
    if((la>lb?la-lb:lb-la)*20>=longest*3)return false;
    std::vector<size_t> prev(lb+1),cur(lb+1);
    std::iota(prev.begin(),prev.end(),size_t(0));
    for(size_t i=1;i<=la;++i){
        cur[0]=i;
        for(size_t j=1;j<=lb;++j){
            const size_t cost=a[i-1]!=b[j-1]?1:0;
            cur[j]=std::min({prev[j]+1,cur[j-1]+1,prev[j-1]+cost});
        }
        std::swap(prev,cur);
    }
    return 1.0-double(prev[lb])/double(longest)>0.85;
}

bool agree(const std::wstring& a,const std::wstring& b){
    const bool ea=trimSpace(a).empty(),eb=trimSpace(b).empty();
    if(ea&&eb)return true;
    if(ea||eb)return false;
    if(similar(a,b))return true;
    const auto& shorter=a.size()<=b.size()?a:b;
    const auto& longer=a.size()<=b.size()?b:a;
    return longer.find(shorter)!=std::wstring::npos;
}

bool wordsOutOfOrder(const std::vector<Word>& words){
    for(size_t i=1;i<words.size();++i){
        const auto& p=words[i-1];const auto& q=words[i];
        const long long dy=(long long)p.rect.y-(long long)q.rect.y;
        const bool sameRow=(dy<0?-dy:dy)<(long long)std::max(p.rect.h,q.rect.h)/2;
        if((p.line==q.line||sameRow)&&q.rect.x+q.rect.w<p.rect.x)return true;
    }
    return false;
}

std::vector<bool> outlinedLetters(const Crop& c){
    const size_t w=c.w,h=c.h;
    std::vector<int> lum(w*h);
    for(size_t i=0;i<w*h;++i){const uint8_t* p=&c.rgba[i*4];lum[i]=std::max({p[0],p[1],p[2]});}
    // the outline is dark grey, lighter where video compression smeared it
    auto dark=[&](size_t i){return lum[i]<130;};
    // a stroke is a small part of the text's height; the area holds about one line
    const size_t reach=std::clamp<size_t>(h/9,3,12);
    std::vector<bool> m(w*h,false);
    for(size_t y=0;y<h;++y){
        for(size_t x=0;x<w;++x){
            const size_t i=y*w+x;
            if(lum[i]<170)continue;
            auto side=[&](int dx,int dy){
                for(size_t k=1;k<=reach;++k){
                    const long long nx=(long long)x+dx*(long long)k,ny=(long long)y+dy*(long long)k;
                    if(nx>=0&&ny>=0&&size_t(nx)<w&&size_t(ny)<h&&dark(size_t(ny)*w+size_t(nx)))return true;
                }
                return false;
            };
            m[i]=(side(-1,0)&&side(1,0))||(side(0,-1)&&side(0,1));
        }
    }
    // specks that pass too (gravel, glints) make the OCR lose whole words: only letter-sized pieces stay
    dropSpecks(m,w,h,std::max<size_t>(h/16,5),12);
    // one pixel thicker: the strokes come out thin, and the OCR reads solid letters best
    auto thick=m;
    for(size_t y=1;y+1<h;++y){
        for(size_t x=1;x+1<w;++x){
            const size_t i=y*w+x;
            if(!m[i]&&(m[i-1]||m[i+1]||m[i-w]||m[i+w])&&lum[i]>=150)thick[i]=true;
        }
    }
    return thick;
}

std::vector<Word> fillGaps(const std::vector<Word>& best,const std::vector<const std::vector<Word>*>& others,bool english){
    std::vector<Word> out=best;
    if(best.empty())return out;
    auto overlaps=[](uint32_t a0,uint32_t a1,uint32_t b0,uint32_t b1){return a0<b1&&b0<a1;};
    for(const auto* other:others){
        if(!other)continue;
        for(const auto& w:*other){
            const auto [g,b]=quality({w},english);
            if(b>0||g<2)continue;
            const uint32_t x0=w.rect.x,x1=w.rect.x+w.rect.w,y0=w.rect.y,y1=w.rect.y+w.rect.h;
            // the line it belongs to: a word taken whose height overlaps most of this one's
            const auto host=std::find_if(out.begin(),out.end(),[&](const Word& o){
                const uint32_t oy0=o.rect.y,oy1=o.rect.y+o.rect.h;
                return std::max(oy0,y0)+(y1-y0)/2<=std::min(oy1,y1);
            });
            if(host==out.end())continue;
            const uint32_t line=host->line;
            if(std::any_of(out.begin(),out.end(),[&](const Word& o){return overlaps(o.rect.x,o.rect.x+o.rect.w,x0,x1);}))continue;
            out.push_back({w.text,w.rect,line});
        }
    }
    std::stable_sort(out.begin(),out.end(),[](const Word& a,const Word& b){
        return a.line!=b.line?a.line<b.line:a.rect.x<b.rect.x;
    });
    return out;
}

Reading recognize(const Crop& crop,const Engine& engine,uint32_t maxDim,bool english){
    auto score=[&](const std::vector<Word>& w){const auto [g,b]=quality(w,english);return g-2*b;};
    auto first=dropJunk(recognizeWith(crop,Prep::Brightest,engine,maxDim));
    auto second=dropJunk(recognizeWith(crop,Prep::WhiteOrColour,engine,maxDim));
    const auto t1=textOf(first),t2=textOf(second);
    // the two readings disagree, or the one taken looks wrong
    const bool unsure=!agree(t1,t2);
    // the second reading must clearly win: a 1-character edge is usually a misread
    const bool tookSecond=!second.empty()&&score(second)>=score(first)+3;
    const auto& best=tookSecond?second:first;
    if(unsure||best.empty()||quality(best,english).second>0||wordsOutOfOrder(best)){
        // Outlined letters over a bright floor: read once more with only the letters inside their outline.
        auto third=dropJunk(recognizeWith(crop,Prep::Outlined,engine,maxDim));
        const auto t3=textOf(third);
        const auto bestOthers=(tookSecond?t1:t2)+L" "+t3;
        const bool thirdOk=agreed(third,t1+L" "+t2)>=agreed(best,bestOthers);
        if(!third.empty()&&score(third)>=score(best)+2&&!wordsOutOfOrder(third)&&thirdOk)return {std::move(third),false};
        // Half the line over sunlit ground, half over shade: each pass read its own half.
        const std::vector<Word> none;
        auto filled=fillGaps(best,{&first,&third,tookSecond?&none:&second},english);
        if(filled.size()>best.size())return {std::move(filled),false};
    }
    return {tookSecond?std::move(second):std::move(first),!unsure};
}

std::vector<Word> centeredWords(const std::vector<Word>& words,double cropX,double frameW){
    if(words.empty()||frameW<=0)return {};
    std::vector<uint32_t> hs;
    for(const auto& w:words)hs.push_back(w.rect.h);
    const double gap=std::max(double(median(hs))*3.0,24.0);
    // columns: runs of words (by x) with no gap wider than `gap` between them
    std::vector<size_t> order(words.size());
    std::iota(order.begin(),order.end(),size_t(0));
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return words[a].rect.x<words[b].rect.x;});
    struct Column{double x0,x1;std::vector<size_t> members;};
    std::vector<Column> cols;
    for(size_t i:order){
        const double x0=words[i].rect.x,x1=double(words[i].rect.x)+words[i].rect.w;
        if(!cols.empty()&&x0<=cols.back().x1+gap){
            cols.back().x1=std::max(cols.back().x1,x1);
            cols.back().members.push_back(i);
        }else cols.push_back({x0,x1,{i}});
    }
    // side panels first: with more than one column, a column whose middle is far from
    // the picture's (a quarter of its width) is not part of a subtitle
    if(cols.size()>1){
        std::erase_if(cols,[&](const Column& c){return std::abs((cropX+(c.x0+c.x1)/2.0)/frameW-0.5)>0.25;});
        if(cols.empty())return {};
    }
    for(;;){
        const double x0=cols.front().x0;
        double x1=0;
        for(const auto& c:cols)x1=std::max(x1,c.x1);
        const double cx=(cropX+(x0+x1)/2.0)/frameW;
        // Veyra: a block across the middle of the picture is a subtitle too when it is a
        // little off-centre (a few words the OCR missed at one end, a game that does not
        // centre its lines exactly). VI Translator's 4% suits GTA VI's exactly centred lines.
        const bool across=(cropX+x0)/frameW<0.47&&(cropX+x1)/frameW>0.53;
        if(std::abs(cx-0.5)<=0.04||(across&&std::abs(cx-0.5)<=0.15)){
            std::vector<bool> keep(words.size(),false);
            for(const auto& c:cols)for(size_t i:c.members)keep[i]=true;
            std::vector<Word> out;
            for(size_t i=0;i<words.size();++i)if(keep[i])out.push_back(words[i]);
            return out;
        }
        if(cols.size()<=1)return {};
        if(cx>0.5)cols.pop_back();else cols.erase(cols.begin());
    }
}

std::wstring fixContractions(const std::wstring& text){
    static const std::pair<const wchar_t*,const wchar_t*> fixes[]={
        {L"cmon",L"c'mon"},{L"im",L"i'm"},{L"ive",L"i've"},{L"youre",L"you're"},{L"youve",L"you've"},
        {L"youll",L"you'll"},{L"theyre",L"they're"},{L"theyve",L"they've"},{L"theyll",L"they'll"},
        {L"weve",L"we've"},{L"dont",L"don't"},{L"doesnt",L"doesn't"},{L"didnt",L"didn't"},{L"cant",L"can't"},
        {L"wont",L"won't"},{L"isnt",L"isn't"},{L"arent",L"aren't"},{L"wasnt",L"wasn't"},{L"werent",L"weren't"},
        {L"wouldnt",L"wouldn't"},{L"couldnt",L"couldn't"},{L"shouldnt",L"shouldn't"},{L"havent",L"haven't"},
        {L"hasnt",L"hasn't"},{L"aint",L"ain't"},{L"thats",L"that's"},{L"whats",L"what's"},{L"hes",L"he's"},
        {L"shes",L"she's"},{L"theres",L"there's"},{L"heres",L"here's"},{L"wheres",L"where's"},{L"whos",L"who's"},
        {L"yall",L"y'all"},
    };
    std::vector<std::wstring> words;
    {
        size_t at=0;
        for(;;){
            const size_t next=text.find(L' ',at);
            words.push_back(text.substr(at,next==std::wstring::npos?std::wstring::npos:next-at));
            if(next==std::wstring::npos)break;
            at=next+1;
        }
    }
    std::vector<std::wstring> out;
    out.reserve(words.size());
    for(size_t i=0;i<words.size();++i){
        const auto& w=words[i];
        size_t start=0;
        while(start<w.size()&&!isAlnum(w[start]))++start;
        size_t end=w.size();
        while(end>start&&!isAlnum(w[end-1]))--end;
        const std::wstring pre=w.substr(0,start),core=w.substr(start,end-start),post=w.substr(end);
        const std::wstring lower=lowered(core);
        std::wstring fixed;
        for(const auto& [from,to]:fixes)if(lower==from){fixed=to;break;}
        if(fixed.empty()&&lower==L"were"){
            bool nextIng=false;
            if(i+1<words.size()){
                const auto n=lowered(trimMatches(words[i+1],[](wchar_t c){return !isLetter(c);}));
                nextIng=n.size()>=3&&n.compare(n.size()-3,3,L"ing")==0;
            }
            // a clause starts here: the line's start, or after , . ! ? ; :
            const bool clauseStart=i==0||(!words[i-1].empty()&&contains(L",.!?;:",words[i-1].back()));
            if(nextIng&&clauseStart)fixed=L"we're";
        }
        if(fixed.empty()){out.push_back(w);continue;}
        // keep the capital ("Cmon" -> "C'mon"); "I'm" is always capital
        if((!core.empty()&&isUpper(core[0]))||fixed.rfind(L"i'",0)==0)fixed[0]=toUpper(fixed[0]);
        out.push_back(pre+fixed+post);
    }
    return join(out);
}

bool looksLikeWords(const std::wstring& text){
    static const wchar_t* const noVowelOk[]={L"mr",L"mrs",L"ms",L"dr",L"st",L"jr",L"hmm",L"hm",L"mm",L"shh",L"sh",L"psst",L"pst",L"brr",L"grr",L"pfft"};
    auto plausible=[](const std::wstring& w){
        const auto lower=lowered(w);
        for(const auto* ok:noVowelOk)if(lower==ok)return true;
        if(w.size()<=5&&std::all_of(w.begin(),w.end(),isUpper))return true; // "LSPD"
        // accented letters count as vowels
        auto vowel=[](wchar_t c){return contains(L"aeiouy",c)||c>=128;};
        const bool hasVowel=std::any_of(lower.begin(),lower.end(),vowel);
        int run=0;bool longRun=false;
        for(wchar_t c:lower){run=vowel(c)?0:run+1;if(run>=6)longRun=true;}
        // "ulnJiJl": lowercase then a capital inside the word
        bool oddCaps=false;
        for(size_t i=2;i<w.size();++i)if(isLower(w[i-1])&&isUpper(w[i]))oddCaps=true;
        return hasVowel&&!longRun&&!oddCaps;
    };
    std::vector<std::wstring> words;
    std::wstring cur;
    auto flush=[&]{
        const auto t=trimMatches(cur,[](wchar_t c){return c==L'\'';});
        if(!t.empty()&&std::all_of(t.begin(),t.end(),[](wchar_t c){return isLetter(c)||c==L'\'';}))words.push_back(t);
        cur.clear();
    };
    for(wchar_t c:text){
        if(isSpace(c)||contains(L".,!?;:\"()[]…-–—",c))flush();
        else cur.push_back(c);
    }
    flush();
    return words.empty()||std::any_of(words.begin(),words.end(),plausible);
}

std::wstring clean(const std::vector<Word>& words,bool english){
    std::vector<std::wstring> parts;
    for(const auto& piece:splitWhitespace(textOf(words)))parts.push_back(english?splitGlued(piece):piece);
    const auto text=join(parts);
    const size_t letters=size_t(std::count_if(text.begin(),text.end(),isLetter));
    const size_t total=std::max<size_t>(text.size(),1);
    const size_t minimum=english?4:2;
    if(letters>=minimum&&double(letters)/double(total)>0.5)return english?fixContractions(text):text;
    return {};
}

bool LineTracker::feed(const std::wstring& text,bool sure,int64_t now){
    if(!text.empty()){
        lastSeen_=now;
        bool isNew=false;
        if(!similar(text,candidate_)){candidate_=text;candidateSince_=now;isNew=true;}
        const int64_t wait=sure?stableMs:stableMs*3;
        const bool settled=!isNew&&candidateSince_>=0&&now-candidateSince_>=wait;
        if(settled&&!similar(candidate_,current_)){current_=candidate_;return true;}
        return false;
    }
    if(lastSeen_<0||now-lastSeen_<=clearAfterMs)return false;
    if(!current_.empty()){
        // the text is gone, not just one blink of the OCR
        current_.clear();candidate_.clear();candidateSince_=-1;
        return true;
    }
    // text that came and went before it was ever shown: forget it
    candidate_.clear();candidateSince_=-1;
    return false;
}

void LineTracker::reset(){candidate_.clear();current_.clear();candidateSince_=lastSeen_=-1;}

} // namespace veyra::engine::screentext
