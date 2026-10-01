#include "veyra/pipeline/ColorGradeTables.h"
#include <algorithm>
namespace veyra::pipeline {
namespace {
constexpr float kMidGrey=0.18f;
constexpr float kLogStops=10.0f;   // -5..+5 EV around 18% grey
constexpr double kD65x=0.3127,kD65y=0.3290;
using Mat3=std::array<double,9>;

// Planckian locus, Kim et al. cubic approximation (valid 1667..25000 K):
// x is a cubic in 1/T, y is a cubic in x.
void planckianXy(double kelvin,double& x,double& y){
    const double t=std::clamp(kelvin,1667.0,25000.0);
    if(t<=4000.0){
        x=-0.2661239e9/(t*t*t)-0.2343589e6/(t*t)+0.8776956e3/t+0.179910;
        y=-0.9549476*x*x*x-1.37418593*x*x+2.09137015*x-0.0316772744;
    }else{
        x=-3.0258469e9/(t*t*t)+2.1070379e6/(t*t)+0.2226347e3/t+0.240390;
        y=3.0817580*x*x*x-5.87338670*x*x+3.75112997*x-0.37001483;
    }
}
Mat3 mul(const Mat3& a,const Mat3& b){
    Mat3 out{};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)for(int k=0;k<3;++k)out[std::size_t(r*3+c)]+=a[std::size_t(r*3+k)]*b[std::size_t(k*3+c)];
    return out;
}
// Bradford chromatic adaptation: source white -> target white, in XYZ.
Mat3 bradford(const std::array<double,3>& src,const std::array<double,3>& dst){
    constexpr Mat3 m{0.8951,0.2664,-0.1614, -0.7502,1.7135,0.0367, 0.0389,-0.0685,1.0296};
    constexpr Mat3 inv{0.9869929,-0.1470543,0.1599627, 0.4323053,0.5183603,0.0492912, -0.0085287,0.0400428,0.9684867};
    const auto response=[&](const std::array<double,3>& v){
        return std::array<double,3>{
            m[0]*v[0]+m[1]*v[1]+m[2]*v[2],
            m[3]*v[0]+m[4]*v[1]+m[5]*v[2],
            m[6]*v[0]+m[7]*v[1]+m[8]*v[2]};
    };
    const auto s=response(src),d=response(dst);
    const Mat3 scale{d[0]/s[0],0,0, 0,d[1]/s[1],0, 0,0,d[2]/s[2]};
    return mul(inv,mul(scale,m));
}
std::array<double,3> whiteXyz(double x,double y){return {x/y,1.0,(1.0-x-y)/y};}
constexpr Mat3 kRgbToXyz{0.4123908,0.3575843,0.1804808, 0.2126390,0.7151687,0.0721923, 0.0193308,0.1191948,0.9505322};
constexpr Mat3 kXyzToRgb{3.2409699,-1.5373832,-0.4986108, -0.9692436,1.8759675,0.0415551, 0.0556301,-0.2039770,1.0569715};

float smoothstepf(float edge0,float edge1,float x){
    const float t=std::clamp((x-edge0)/std::max(1e-6f,edge1-edge0),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
// Piecewise-linear point curve on the 0..1 tonal domain (P1 cut; the data model
// already stores up to eight points, so a monotone cubic can replace this later
// without touching the format).
// Monotone cubic Hermite (Fritsch-Carlson) tangents. A tone curve must pass
// through every control point, be C1-smooth, and never overshoot or invert -
// which is exactly what this construction guarantees. Piecewise-linear (the
// previous behaviour) produced visible corners; a plain Catmull-Rom can ring
// past the control points and invert a strong S-curve.
void curveTangents(const engine::ColorCurve& curve,std::array<float,engine::kColorCurvePoints>& slope){
    const int count=std::clamp(curve.count,0,int(engine::kColorCurvePoints));
    std::array<float,engine::kColorCurvePoints> secant{};
    for(int i=0;i<count-1;++i){
        const float dx=std::max(1e-6f,curve.points[std::size_t(i+1)].x-curve.points[std::size_t(i)].x);
        secant[std::size_t(i)]=(curve.points[std::size_t(i+1)].y-curve.points[std::size_t(i)].y)/dx;
    }
    if(count<2)return;
    slope[0]=secant[0];
    for(int i=1;i<count-1;++i){
        const float previous=secant[std::size_t(i-1)],next=secant[std::size_t(i)];
        // A sign change (or a flat neighbour) is a local extremum: flatten it so
        // the spline cannot bulge past the point the user placed.
        slope[std::size_t(i)]=previous*next<=0.0f?0.0f:(previous+next)*0.5f;
    }
    slope[std::size_t(count-1)]=secant[std::size_t(count-2)];
    for(int i=0;i<count-1;++i){
        if(secant[std::size_t(i)]==0.0f){slope[std::size_t(i)]=0.0f;slope[std::size_t(i+1)]=0.0f;continue;}
        const float a=slope[std::size_t(i)]/secant[std::size_t(i)];
        const float b=slope[std::size_t(i+1)]/secant[std::size_t(i)];
        const float magnitude=std::sqrt(a*a+b*b);
        if(magnitude>3.0f){
            const float scale=3.0f/magnitude;
            slope[std::size_t(i)]=scale*a*secant[std::size_t(i)];
            slope[std::size_t(i+1)]=scale*b*secant[std::size_t(i)];
        }
    }
}
float pointCurve(const engine::ColorCurve& curve,float x){
    const int count=std::clamp(curve.count,0,int(engine::kColorCurvePoints));
    if(count<2)return x;
    if(x<=curve.points[0].x)return curve.points[0].y;
    if(x>=curve.points[std::size_t(count-1)].x)return curve.points[std::size_t(count-1)].y;
    std::array<float,engine::kColorCurvePoints> slope{};
    curveTangents(curve,slope);
    for(int i=0;i<count-1;++i){
        const auto& a=curve.points[std::size_t(i)];
        const auto& b=curve.points[std::size_t(i+1)];
        if(x<=b.x){
            const float dx=std::max(1e-6f,b.x-a.x);
            const float t=std::clamp((x-a.x)/dx,0.0f,1.0f);
            const float t2=t*t,t3=t2*t;
            const float h00=2.0f*t3-3.0f*t2+1.0f,h10=t3-2.0f*t2+t;
            const float h01=-2.0f*t3+3.0f*t2,h11=t3-t2;
            return h00*a.y+h10*dx*slope[std::size_t(i)]+h01*b.y+h11*dx*slope[std::size_t(i+1)];
        }
    }
    return curve.points[std::size_t(count-1)].y;
}
// Parametric regions: the three splitters divide the tonal range and each region
// slider shifts its own band (Lightroom-style).
float parametricShift(const engine::ColorSettings& s,float t){
    const float sh=std::clamp(0.5f+0.35f*s.splitShadows/100.0f,0.05f,0.90f);
    const float sm=std::clamp(0.5f+0.35f*s.splitMidtones/100.0f,0.10f,0.95f);
    const float shi=std::clamp(0.5f+0.35f*s.splitHighlights/100.0f,0.10f,0.95f);
    const float lo=std::min(sh,sm),hi=std::max(sm,shi);
    const float wShadows=1.0f-smoothstepf(0.0f,sh,t);
    const float wDarks=smoothstepf(0.0f,sh,t)*(1.0f-smoothstepf(lo,hi,t));
    const float wLights=smoothstepf(lo,hi,t)*(1.0f-smoothstepf(sm,shi,t));
    const float wHighlights=smoothstepf(sm,shi,t);
    return (s.paramShadows*wShadows+s.paramDarks*wDarks+s.paramLights*wLights+s.paramHighlights*wHighlights)/100.0f*0.25f;
}
}

float ColorGradeTables::encodeLog(float linear){
    const float safe=std::max(linear,1e-5f);
    return std::clamp(std::log2(safe/kMidGrey)/kLogStops+0.5f,0.0f,1.0f);
}
float ColorGradeTables::decodeLog(float encoded){
    return kMidGrey*std::exp2((std::clamp(encoded,0.0f,1.0f)-0.5f)*kLogStops);
}

float ColorGradeTables::curveValue(const engine::ColorCurve& curve,float x){return pointCurve(curve,std::clamp(x,0.0f,1.0f));}
ColorGradeTables ColorGradeTables::bake(const engine::ColorSettings& s){
    // Per-section bypass ("group eye"): a bypassed section is baked as if its
    // parameters were neutral, so the user can A/B one group without losing the
    // numbers they dialled in. Cheap, exact and no shader branch is needed.
    const auto bypassed=[&](int section){return (s.groupBypassMask&(1u<<unsigned(section)))!=0;};
    if(s.groupBypassMask){
        auto masked=s;
        if(bypassed(0)){masked.exposure=masked.contrast=masked.highlights=masked.shadows=masked.whites=masked.blacks=0;}
        if(bypassed(1)){masked.temperature=masked.tint=masked.vibrance=masked.saturation=0;}
        if(bypassed(2)){
            masked.paramHighlights=masked.paramLights=masked.paramDarks=masked.paramShadows=0;
            masked.splitHighlights=masked.splitMidtones=masked.splitShadows=0;
            for(auto& curve:masked.curves)curve.reset();
        }
        if(bypassed(3)){
            masked.mixerHue.fill(0);masked.mixerSaturation.fill(0);masked.mixerLuminance.fill(0);
            masked.blackWhite=false;masked.blackWhiteMix.fill(0);
        }
        if(bypassed(4)){for(auto& wheel:masked.grading)wheel={};masked.gradingBlending=50;masked.gradingBalance=0;}
        if(bypassed(5)){masked.calibrationShadowTint=0;masked.calibrationHue.fill(0);masked.calibrationSaturation.fill(0);}
        if(bypassed(6)){masked.lutStrength=0;}
        masked.groupBypassMask=0;   // the recursion must not bypass again
        return bake(masked);
    }
    ColorGradeTables out;
    out.exposure=s.exposure;
    out.saturation=s.saturation;
    out.vibrance=s.vibrance;
    out.lutStrength=s.hasLut()?std::clamp(s.lutStrength/100.0f,0.0f,1.0f):0.0f;
    out.lutInputSpace=s.lutInputSpace;
    out.blackWhite=s.blackWhite;
    if(!s.enabled||s.neutral()){
        for(int i=0;i<kCurveEntries;++i){
            const float t=float(i)/float(kCurveEntries-1);
            const auto base=std::size_t(i)*4;
            out.curve[base]=out.curve[base+1]=out.curve[base+2]=t;
        }
        for(int i=0;i<kHueEntries;++i)out.hue[std::size_t(i)*4+1]=1.0f;
        for(int i=0;i<kLumEntries;++i){const auto base=std::size_t(i)*4;out.lum[base]=out.lum[base+1]=out.lum[base+2]=1.0f;}
        out.identity=true;
        return out;
    }
    out.identity=false;

    // --- white balance + calibration -> one linear 3x3 ---------------------
    double wx=kD65x,wy=kD65y;
    if(s.temperature!=0||s.tint!=0){
        // Temperature is relative: +100 assumes a much cooler illuminant, which
        // makes the image warmer (Lightroom's slider direction).
        planckianXy(6500.0*std::exp2(double(s.temperature)/100.0),wx,wy);
        wy=std::clamp(wy+double(s.tint)*0.00025,0.05,0.95);
    }
    // Adaption direction: the slider states which illuminant the image was shot
    // under, so the matrix maps that illuminant onto D65 (positive temperature =
    // assume a cooler source = the picture looks warmer, Lightroom's direction).
    Mat3 combined=mul(kXyzToRgb,mul(bradford(whiteXyz(wx,wy),whiteXyz(kD65x,kD65y)),kRgbToXyz));
    Mat3 calib{1,0,0, 0,1,0, 0,0,1};
    for(int i=0;i<3;++i){
        const double gain=1.0+double(s.calibrationSaturation[std::size_t(i)])/100.0;
        const double leak=double(s.calibrationHue[std::size_t(i)])/100.0*0.25;
        calib[std::size_t(i*3+i)]*=gain;
        calib[std::size_t(i*3+(i+1)%3)]+=leak*gain;
    }
    combined=mul(calib,combined);
    for(int i=0;i<9;++i)out.matrix[std::size_t(i)]=float(combined[std::size_t(i)]);

    // --- tone response (log domain) ---------------------------------------
    for(int i=0;i<kCurveEntries;++i){
        const float in=float(i)/float(kCurveEntries-1);
        float t=in;
        t+=s.highlights/100.0f*0.25f*smoothstepf(0.5f,1.0f,t);
        t+=s.shadows/100.0f*0.25f*(1.0f-smoothstepf(0.0f,0.5f,t));
        t+=s.whites/100.0f*0.20f*std::pow(std::clamp(t,0.0f,1.0f),3.0f);
        t+=s.blacks/100.0f*0.20f*std::pow(1.0f-std::clamp(t,0.0f,1.0f),3.0f);
        t=0.5f+(t-0.5f)*(1.0f+s.contrast/100.0f*0.75f);
        t+=parametricShift(s,t);
        t=std::clamp(t,0.0f,1.0f);
        const auto base=std::size_t(i)*4;
        // Per-channel point curves, then the RGB master curve on top.
        out.curve[base]=pointCurve(s.curves[0],pointCurve(s.curves[1],t));
        out.curve[base+1]=pointCurve(s.curves[0],pointCurve(s.curves[2],t));
        out.curve[base+2]=pointCurve(s.curves[0],pointCurve(s.curves[3],t));
    }

    // --- hue response (mixer) --------------------------------------------
    for(int i=0;i<kHueEntries;++i){
        const float hue=360.0f*float(i)/float(kHueEntries-1);
        float shift=0,sat=1,lum=1,bw=0;
        constexpr float centres[engine::kColorMixerBands]={0,30,60,120,180,240,280,320};
        for(int b=0;b<engine::kColorMixerBands;++b){
            // Use a continuous triangular mask between adjacent hue centres.
            // The old fixed 32-degree radius left large gaps (for example around
            // 90/150/210 degrees), so a slider could appear to do nothing for
            // perfectly valid colours. The ring topology also makes magenta ->
            // red wrap around without a seam.
            const int prev=(b+engine::kColorMixerBands-1)%engine::kColorMixerBands;
            const int next=(b+1)%engine::kColorMixerBands;
            const float left=centres[b]-centres[prev]+(b==0?360.0f:0.0f);
            const float right=centres[next]-centres[b]+(next==0?360.0f:0.0f);
            const float delta=std::remainder(hue-centres[b],360.0f);
            const float hueWeight=std::max(0.0f,1.0f-std::abs(delta)/(delta<0?left:right));
            // Preserve the established UI scale: +/-100 is approximately +/-30
            // degrees at the centre of a band, as in existing saved presets.
            shift+=hueWeight*s.mixerHue[std::size_t(b)]*0.3f;
            // Keep existing saturation, brightness and B&W preset responses.
            const float t=std::max(0.0f,1.0f-std::abs(delta)/32.0f);
            const float w=t*t*(3.0f-2.0f*t);
            sat*=1.0f+w*s.mixerSaturation[std::size_t(b)]/100.0f;
            lum*=1.0f+w*s.mixerLuminance[std::size_t(b)]/100.0f;
            // Black & white mixer: per-band lighten/darken of the monochrome
            // result. The weight rides in the hue table's unused alpha channel
            // so the shader keeps its existing table set (no extra SRV).
            bw+=w*s.blackWhiteMix[std::size_t(b)];
        }
        const auto base=std::size_t(i)*4;
        out.hue[base]=shift;out.hue[base+1]=std::max(0.0f,sat);out.hue[base+2]=std::max(0.0f,lum);
        out.hue[base+3]=std::clamp(bw,-100.0f,100.0f);
    }

    // --- luminance response (colour grading + shadow tint) ----------------
    const float tint=std::clamp(s.calibrationShadowTint/100.0f,-1.0f,1.0f);
    for(int i=0;i<kLumEntries;++i){
        const float t=float(i)/float(kLumEntries-1);
        float gains[3]={1,1,1};
        const float wS=1.0f-smoothstepf(0.15f,0.5f,t);
        const float wH=smoothstepf(0.5f,0.85f,t);
        const float wM=std::max(0.0f,1.0f-wS-wH);
        const float zones[3]={wS,wM,wH};
        for(int z=0;z<3;++z){
            const auto& wheel=s.grading[std::size_t(z)];
            const float sat=wheel.saturation/100.0f*zones[z];
            const float lum=1.0f+wheel.luminance/100.0f*zones[z];
            const float rad=wheel.hue*3.14159265f/180.0f;
            gains[0]*=std::max(0.0f,1.0f+sat*std::cos(rad))*lum;
            gains[1]*=std::max(0.0f,1.0f+sat*std::cos(rad-2.0943951f))*lum;
            gains[2]*=std::max(0.0f,1.0f+sat*std::cos(rad+2.0943951f))*lum;
        }
        const auto& global=s.grading[3];
        const float gSat=global.saturation/100.0f;
        const float gLum=1.0f+global.luminance/100.0f;
        const float gRad=global.hue*3.14159265f/180.0f;
        gains[0]*=std::max(0.0f,1.0f+gSat*std::cos(gRad))*gLum;
        gains[1]*=std::max(0.0f,1.0f+gSat*std::cos(gRad-2.0943951f))*gLum;
        gains[2]*=std::max(0.0f,1.0f+gSat*std::cos(gRad+2.0943951f))*gLum;
        // Shadow tint is a dark-end green/magenta balance, so it lives here.
        if(tint!=0.0f){
            const float w=wS*0.25f*tint;
            gains[0]*=1.0f-w*0.5f;
            gains[1]*=1.0f+w;
            gains[2]*=1.0f-w*0.5f;
        }
        const auto base=std::size_t(i)*4;
        out.lum[base]=std::max(0.0f,gains[0]);
        out.lum[base+1]=std::max(0.0f,gains[1]);
        out.lum[base+2]=std::max(0.0f,gains[2]);
    }
    return out;
}
} // namespace veyra::pipeline
