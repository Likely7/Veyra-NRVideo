#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

// ---- reduction back to the render extent ----------------------------------
// Consoles enlarge with a pixel-centre bilinear scaler (Switch 2 TV mode:
// re-upscaling the least-squares inverse reproduces the capture at 45.85 dB).
// A plain box/bilinear reduction blurs that render again; the least-squares
// inverse of the upscale recovers it. Per axis, render sample i is a fixed
// filter over source samples centre(i)-14 .. centre(i)+13; the filter repeats
// every q render samples (p source samples), one row per phase i % q.
inline constexpr uint32_t kSmartSrTaps=28;
inline constexpr int kSmartSrTapOrigin=14;     // taps start at centre - 14
inline constexpr uint32_t kSmartSrConstants=40; // SmartSrReduce.hlsl: 9 uints, 3 pad, 28 taps
inline constexpr uint32_t kSmartSrMaxPhases=16; // one dispatch per phase; a larger q is not descaled
struct SmartSrAxis {
    uint32_t highLength=0,lowLength=0,p=0,q=0;  // p source samples per q render samples
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
inline SmartSrAxis smartSrInverseAxis(uint32_t high,uint32_t low){
    SmartSrAxis axis;
    if(!low||low>=high)return axis;
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
struct SmartSrVerdict { uint32_t height=0; double knee=1.0,notch=1.0; bool spatial=false; uint32_t fit=0; };
// errors[c]: residual of candidate c over the probed rows' energy; NaN where c
// was not measured (not a reduction of this source).
inline SmartSrVerdict smartSrDecide(const std::array<double,kSmartSrCandidates.size()>& errors){
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
// The render height a probe votes for, given the height in use (0: none). Live
// Switch 2 HDR (2026-11-22): a dimmed, almost black picture has a weak knee
// (x2.8-3.1) and flipped smart SR on and off every few seconds, so a fit at the
// height in use counts for it below the start rules. A weak fit anywhere else is
// no support for it: kept as such it held a wrong 1152 for minutes (2026-11-24).
// Native frames are smooth (knee about x1.6) and turn it off.
inline constexpr double kSmartSrKeepKnee=2.0;
inline uint32_t smartSrVote(const SmartSrVerdict& v,uint32_t current){
    if(current&&v.fit==current&&v.knee>=kSmartSrKeepKnee)return current;
    return v.spatial?v.height:0;
}
// Equal votes needed to act: dropping the height in use for none takes longer
// than finding one, so a few unclear frames do not switch it off.
inline uint32_t smartSrVotesNeeded(uint32_t vote,uint32_t current){
    return !vote&&current?kSmartSrOffStreak:kSmartSrProbeStreak;
}
// Source row probed for row r (both probe shaders use the same formula).
inline uint32_t smartSrProbeRow(uint32_t r,uint32_t height){
    return height/10+uint32_t(uint64_t(r)*(height*8/10)/kSmartSrProbeRows);
}

// Largest error recovering an interior render signal from its bilinear
// upscale with these taps (logged at graph creation as evidence).
inline double smartSrSelfCheck(const SmartSrAxis& axis){
    if(axis.taps.empty())return 1.0;
    std::vector<double> a(axis.lowLength),y(axis.highLength);
    for(uint32_t i=0;i<axis.lowLength;++i)a[i]=0.5+0.35*std::sin(i*0.73)+0.15*std::cos(i*2.11);
    for(uint32_t j=0;j<axis.highLength;++j){
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
