#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <vector>
#include "veyra/pipeline/ResolutionPlan.h"

namespace veyra::pipeline {
// Smart SR (EnhancementSettings::smartSr): a source that upscaled its own render
// (a console's 1080p game sent as 4K) is measured for that render extent,
// reduced back to it by inverting the upscale ("descale"), and the single RTX
// Video SR pass enlarges it again in place of the console's scaler. Native and
// reconstructed frames keep the ordinary SR path.
// The coded domain the source's own scaler enlarged in (shaders/SmartSrCoding.hlsli):
// sRGB codes for SDR, PQ codes for an HDR10 source kept as HDR (Switch 2 TV mode:
// 51.1-51.7 dB fit in PQ, no fit in linear light). HLG, linear float (scRGB)
// sources and HDR tone-mapped to SDR at ingest are not descaled.
enum class SmartSrCoding : uint32_t { Srgb = 0, Pq = 1, None = 2 };
constexpr SmartSrCoding smartSrCoding(bool hdrInput,bool hdrWorking,bool pq,bool scRgb){
    if(scRgb)return SmartSrCoding::None;
    if(!hdrInput)return SmartSrCoding::Srgb;
    return hdrWorking&&pq?SmartSrCoding::Pq:SmartSrCoding::None;
}
// Render extent of this source at the given height, same aspect and even
// sizes, or an empty extent when that is not a reduction.
inline Extent smartSrRenderExtent(Extent source,uint32_t height){
    height&=~1u;
    if(!height||!source.valid()||height>=source.height)return {};
    const uint32_t width=std::max(2u,uint32_t((uint64_t(source.width)*height+source.height/2)/source.height)&~1u);
    return {width,height};
}

// How the source was enlarged.
//   Bilinear:  pixel-centre bilinear, any ratio. Switch 2 TV mode: re-upscaling the
//              least-squares inverse reproduces the capture at 45.85 dB.
//   Aligned2x: exactly twice. Even samples sit on the render samples, odd ones half
//              way, both through one symmetric 8-tap filter. Measured on Switch 2 HOME
//              captured as 4K HDR10 (GC573) while a game that switches the output mode
//              stays open (2026-11-26): odd samples follow from the even ones within
//              0.33 ten-bit codes on both axes, so the fit is at the rounding noise.
//              Bilinear finds no height there (knee x2.5). Whether the console or the
//              capture card scales is not known.
enum class SmartSrScaler : uint32_t { Bilinear = 0, Aligned2x = 1 };
inline constexpr std::array<double,4> kSmartSrAlignedEven{0.7856,0.1121,-0.0126,0.0077};  // 0, 1, 2, 3 render samples away
inline constexpr std::array<double,4> kSmartSrAlignedOdd{0.5362,-0.0599,0.0214,0.0023};   // 0.5, 1.5, 2.5, 3.5
// Weight of render sample i in source sample j of the aligned scaler.
inline double smartSrAlignedWeight(uint32_t j,int i){
    const int k=int(j/2);
    if(!(j&1)){const int d=std::abs(i-k);return d<4?kSmartSrAlignedEven[size_t(d)]:0.0;}
    const int d=i<=k?k-i:i-k-1;
    return d<4?kSmartSrAlignedOdd[size_t(d)]:0.0;
}

// ---- reduction back to the render extent ----------------------------------
// A plain box/bilinear reduction blurs the render again; the least-squares
// inverse of the upscale recovers it. Per axis, render sample i is a fixed
// filter over source samples centre(i)-14 .. centre(i)+13; the filter repeats
// every q render samples (p source samples), one row per phase i % q.
inline constexpr uint32_t kSmartSrTaps=28;
inline constexpr int kSmartSrTapOrigin=14;     // taps start at centre - 14
inline constexpr uint32_t kSmartSrConstants=40; // SmartSrReduce.hlsl: 9 uints, 3 pad, 28 taps
inline constexpr uint32_t kSmartSrMaxPhases=16; // one dispatch per phase; a larger q is not descaled
struct SmartSrAxis {
    uint32_t highLength=0,lowLength=0,p=0,q=0;  // p source samples per q render samples
    SmartSrScaler scaler=SmartSrScaler::Bilinear;
    std::vector<std::array<float,kSmartSrTaps>> taps;
};
// Source sample holding the centre of render sample i.
inline uint32_t smartSrCentre(uint32_t i,uint32_t p,uint32_t q){return uint32_t((uint64_t(2*i+1)*p)/(2*q));}
// The two render samples (and weights) source sample j reads in the upscale.
inline void smartSrBilinear(uint32_t j,uint32_t high,uint32_t low,int& i0,double& w0,int& i1,double& w1){
    const double x=(j+0.5)*double(low)/double(high)-0.5;
    const double f=std::floor(x);
    w1=x-f;w0=1.0-w1;
    i0=std::clamp(int(f),0,int(low)-1);i1=std::clamp(int(f)+1,0,int(low)-1);
}
// Inverse of the aligned 2x scaler: the 28 taps that give render sample i back from its
// own source samples whatever the neighbours are (minimum norm, with a ridge far below
// the source's own rounding noise).
inline SmartSrAxis smartSrAlignedInverseAxis(uint32_t high){
    SmartSrAxis axis;
    if(high<2*kSmartSrTaps||(high&1))return axis;
    axis.highLength=high;axis.lowLength=high/2;axis.p=2;axis.q=1;axis.scaler=SmartSrScaler::Aligned2x;
    constexpr size_t n=kSmartSrTaps,reach=kSmartSrTaps;  // render samples i-reach .. i+reach
    // Around render sample `reach` (source sample 2*reach): rows of the upscale under the taps.
    std::array<std::array<double,2*reach+1>,n> up{};
    const uint32_t base=uint32_t(2*reach+1)-uint32_t(kSmartSrTapOrigin);
    for(size_t t=0;t<n;++t)for(size_t i=0;i<up[t].size();++i)up[t][i]=smartSrAlignedWeight(base+uint32_t(t),int(i));
    // (U U^T + ridge) taps = U e: symmetric positive definite.
    std::array<std::array<double,n+1>,n> m{};
    for(size_t a=0;a<n;++a){
        for(size_t b=0;b<n;++b)for(size_t i=0;i<up[a].size();++i)m[a][b]+=up[a][i]*up[b][i];
        m[a][a]+=1e-5;m[a][n]=up[a][reach];
    }
    for(size_t c=0;c<n;++c)for(size_t r=c+1;r<n;++r){
        const double f=m[r][c]/m[c][c];
        for(size_t k=c;k<=n;++k)m[r][k]-=f*m[c][k];
    }
    std::array<double,n> x{};double sum=0.0;
    for(size_t r=n;r-->0;){
        double v=m[r][n];
        for(size_t k=r+1;k<n;++k)v-=m[r][k]*x[k];
        x[r]=v/m[r][r];sum+=x[r];
    }
    std::array<float,kSmartSrTaps> taps{};
    for(size_t t=0;t<n;++t)taps[t]=float(x[t]+(1.0-sum)/double(n));  // sums to exactly one, as below
    axis.taps.push_back(taps);
    return axis;
}
inline SmartSrAxis smartSrInverseAxis(uint32_t high,uint32_t low,SmartSrScaler scaler=SmartSrScaler::Bilinear){
    SmartSrAxis axis;
    if(!low||low>=high)return axis;
    if(scaler==SmartSrScaler::Aligned2x)return high==2*low?smartSrAlignedInverseAxis(high):axis;
    const uint32_t g=std::gcd(high,low);
    axis.highLength=high;axis.lowLength=low;axis.p=high/g;axis.q=low/g;
    // U^T U of the bilinear upscale is tridiagonal: diag[i], off[i] couples i and i+1.
    std::vector<double> diag(low,0.0),off(low,0.0);
    for(uint32_t j=0;j<high;++j){
        int i0,i1;double w0,w1;smartSrBilinear(j,high,low,i0,w0,i1,w1);
        if(i0==i1){diag[i0]+=(w0+w1)*(w0+w1);continue;}
        diag[i0]+=w0*w0;diag[i1]+=w1*w1;off[std::min(i0,i1)]+=w0*w1;
    }
    std::vector<double> c(low),d(low),z(low);
    for(uint32_t phase=0;phase<axis.q;++phase){
        // An interior render sample of this phase, away from edge effects.
        const uint32_t row=(low/2/axis.q)*axis.q+phase;
        // Row of the pseudo-inverse: solve (U^T U) z = e_row (Thomas), then U z.
        for(uint32_t i=0;i<low;++i){
            const double b=diag[i]-(i?off[i-1]*c[i-1]:0.0);
            const double rhs=(i==row?1.0:0.0)-(i?off[i-1]*d[i-1]:0.0);
            c[i]=i+1<low?off[i]/b:0.0;d[i]=rhs/b;
        }
        for(uint32_t i=low;i-->0;)z[i]=d[i]-(i+1<low?c[i]*z[i+1]:0.0);
        std::array<double,kSmartSrTaps> row28{};
        double sum=0.0;uint32_t inside=0;
        const int base=int(smartSrCentre(row,axis.p,axis.q))-kSmartSrTapOrigin;
        for(uint32_t t=0;t<kSmartSrTaps;++t){
            const int j=base+int(t);
            if(j<0||j>=int(high))continue;
            int i0,i1;double w0,w1;smartSrBilinear(uint32_t(j),high,low,i0,w0,i1,w1);
            row28[t]=w0*z[i0]+w1*z[i1];sum+=row28[t];++inside;
        }
        // Cutting the inverse at 28 taps loses a little of its sum, a different amount per
        // scale ratio (7e-3 at 576, 5e-4 at 1080, 3e-5 at 1152). On an almost flat picture that
        // gain error was all the probes measured and looked like a render height (live
        // 2026-11-24: 1152, knee x992.6, notch x1.1). Every row sums to exactly one.
        std::array<float,kSmartSrTaps> taps{};
        for(uint32_t t=0;t<kSmartSrTaps;++t){
            const int j=base+int(t);
            if(j>=0&&j<int(high))taps[t]=float(row28[t]+(1.0-sum)/double(inside));
        }
        axis.taps.push_back(taps);
    }
    return axis;
}
// ---- render detection ------------------------------------------------------
// Every kSmartSrProbeInterval frames the graph re-upscales kSmartSrProbeRows
// source rows from each candidate width (inverse taps, then bilinear) and reads
// back the residuals (SmartSrProbeReduce/Residual.hlsl). Heights have exact
// 16:9 widths; their scales are ratios p/q with p <= 15. A frame upscaled from
// height h fits h almost exactly and the next height up worse ("notch"), and
// every lower height much worse ("knee"); native and reconstructed (DLSS/TAA)
// frames lose error smoothly. Rules as res-probe/resprobe.py, validated on rows
// with res-probe/m2_validate.py (synthetic 9/9, Switch 2 captures 13/13).
inline constexpr std::array<uint32_t,22> kSmartSrCandidates{540,576,648,720,810,864,900,1008,1080,1152,1260,
    1296,1350,1440,1512,1584,1620,1728,1800,1872,1944,2016};
// One more probe behind the candidates: half the source height through the aligned scaler.
inline constexpr size_t kSmartSrAlignedProbe=kSmartSrCandidates.size();
inline constexpr size_t kSmartSrProbes=kSmartSrAlignedProbe+1;
inline constexpr uint32_t kSmartSrProbeRows=96;
inline constexpr uint32_t kSmartSrProbeInterval=30;  // frames between probes
inline constexpr uint32_t kSmartSrProbeStreak=3;     // equal verdicts before the render extent changes
inline constexpr uint32_t kSmartSrOffStreak=6;       // verdicts without one before a render extent is dropped
inline constexpr uint32_t kSmartSrReportProbes=40;   // one summary line per this many probes
// Probed rows that vary less than this (standard deviation of the coded luma: two
// 8-bit steps) hold only quantisation and rounding noise: no evidence either way.
inline constexpr double kSmartSrMinDeviation=0.008;
// fit: where the error curve rises most, with or without the start rules below;
// height: the same once the rise is a knee.
struct SmartSrVerdict { uint32_t height=0; double knee=1.0,notch=1.0; bool spatial=false; uint32_t fit=0;
                        SmartSrScaler scaler=SmartSrScaler::Bilinear; };
inline SmartSrVerdict smartSrDecideBilinear(const std::array<double,kSmartSrCandidates.size()>& errors){
    SmartSrVerdict v;constexpr size_t n=kSmartSrCandidates.size();
    double best=-1e30;size_t at=n;
    for(size_t i=0;i+1<n;++i){
        if(!(errors[i+1]<=0.02)||!std::isfinite(errors[i]))continue;  // the fit at the render height stays under 2%
        const double rise=std::log10(std::max(errors[i],1e-7))-std::log10(std::max(errors[i+1],1e-7));
        if(rise>best){best=rise;at=i;}
    }
    if(at==n)return v;
    v.knee=std::pow(10.0,best);v.fit=kSmartSrCandidates[at+1];
    const bool next=at+2<n&&std::isfinite(errors[at+2]);
    v.notch=next?std::max(errors[at+2],1e-7)/std::max(errors[at+1],1e-7):1.0;
    if(v.knee<3.0)return v;
    v.height=v.fit;
    // An upscale fits its own height better than the next one up; a smooth picture fits
    // everything above some height alike, however large the knee. Every true detection so
    // far has notch x2.1 or more. Only where no height above was measured the knee decides.
    v.spatial=next?v.notch>=2.0:v.knee>=20.0;
    return v;
}
// errors[c]: residual of candidate c over the probed rows' energy; NaN where c
// was not measured (not a reduction of this source). aligned: the same for half the
// source height through the aligned scaler, NaN when not measured. Its knee is the gain
// over the bilinear fit of the same height, its notch the gain over the next height up.
// Measured: x61 and more on that picture, at most x2.3 on every other one (native,
// bilinear from all candidates, HDR10 and SDR captures, almost flat pictures).
inline constexpr double kSmartSrAlignedKnee=3.0,kSmartSrAlignedWeakKnee=2.5;
inline SmartSrVerdict smartSrDecide(const std::array<double,kSmartSrCandidates.size()>& errors,
                                    double aligned=std::numeric_limits<double>::quiet_NaN(),uint32_t alignedHeight=0){
    const auto v=smartSrDecideBilinear(errors);
    const auto same=std::find(kSmartSrCandidates.begin(),kSmartSrCandidates.end(),alignedHeight);
    if(same==kSmartSrCandidates.end()||!(aligned<=0.02))return v;
    const size_t at=size_t(same-kSmartSrCandidates.begin());
    if(!std::isfinite(errors[at]))return v;
    const double fit=std::max(aligned,1e-7);
    SmartSrVerdict a;a.scaler=SmartSrScaler::Aligned2x;a.fit=alignedHeight;
    a.knee=errors[at]/fit;
    a.notch=at+1<errors.size()&&std::isfinite(errors[at+1])?errors[at+1]/fit:a.knee;
    const double least=std::min(a.knee,a.notch);
    if(least>=kSmartSrAlignedKnee){a.height=alignedHeight;a.spatial=true;return a;}
    return !v.spatial&&least>=kSmartSrAlignedWeakKnee?a:v;
}
// The render height and scaler a probe votes for, given the ones in use (0: none). Live
// Switch 2 HDR (2026-11-22): a dimmed, almost black picture has a weak knee
// (x2.8-3.1) and flipped smart SR on and off every few seconds, so a fit at the
// height in use counts for it below the start rules. A weak fit anywhere else is
// no support for it: kept as such it held a wrong 1152 for minutes (2026-11-24).
// Native frames are smooth (knee about x1.6) and turn it off.
inline constexpr double kSmartSrKeepKnee=2.0;
struct SmartSrTarget {
    uint32_t height=0;
    SmartSrScaler scaler=SmartSrScaler::Bilinear;
    bool operator==(const SmartSrTarget&) const=default;
};
inline SmartSrTarget smartSrVote(const SmartSrVerdict& v,SmartSrTarget current){
    if(current.height&&v.fit==current.height&&v.scaler==current.scaler&&v.knee>=kSmartSrKeepKnee)return current;
    return v.spatial?SmartSrTarget{v.height,v.scaler}:SmartSrTarget{};
}
// Equal votes needed to act: dropping the height in use for none takes longer
// than finding one, so a few unclear frames do not switch it off.
inline uint32_t smartSrVotesNeeded(SmartSrTarget vote,SmartSrTarget current){
    return !vote.height&&current.height?kSmartSrOffStreak:kSmartSrProbeStreak;
}
// Source row probed for row r (both probe shaders use the same formula).
inline uint32_t smartSrProbeRow(uint32_t r,uint32_t height){
    return height/10+uint32_t(uint64_t(r)*(height*8/10)/kSmartSrProbeRows);
}

// Largest error recovering an interior render signal from its own upscale
// with these taps.
inline double smartSrSelfCheck(const SmartSrAxis& axis){
    if(axis.taps.empty())return 1.0;
    std::vector<double> a(axis.lowLength),y(axis.highLength);
    for(uint32_t i=0;i<axis.lowLength;++i)a[i]=0.5+0.35*std::sin(i*0.73)+0.15*std::cos(i*2.11);
    for(uint32_t j=0;j<axis.highLength;++j){
        if(axis.scaler==SmartSrScaler::Aligned2x){
            for(int i=int(j/2)-3;i<=int(j/2)+4;++i)
                y[j]+=smartSrAlignedWeight(j,i)*a[size_t(std::clamp(i,0,int(axis.lowLength)-1))];
            continue;
        }
        int i0,i1;double w0,w1;smartSrBilinear(j,axis.highLength,axis.lowLength,i0,w0,i1,w1);
        y[j]=w0*a[i0]+w1*a[i1];
    }
    double worst=0.0;
    for(uint32_t i=16;i+16<axis.lowLength;++i){
        const auto& taps=axis.taps[i%axis.q];
        const int base=int(smartSrCentre(i,axis.p,axis.q))-kSmartSrTapOrigin;
        double v=0.0;
        for(uint32_t t=0;t<kSmartSrTaps;++t)v+=taps[t]*y[std::clamp(base+int(t),0,int(axis.highLength)-1)];
        worst=std::max(worst,std::abs(v-a[i]));
    }
    return worst;
}
}
