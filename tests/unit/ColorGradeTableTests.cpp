// CPU bake of the colour grade (T2b). No GPU: verifies the tables the ingest
// shader will read, plus the "enabled but neutral must be a visual no-op"
// contract the master switch relies on.
#include "veyra/pipeline/ColorGradeTables.h"
#include "veyra/engine/ColorSettings.h"
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
namespace {
int failures=0;
void check(bool ok,const std::string& label){
    std::printf("%s %s\n",ok?"PASS":"FAIL",label.c_str());
    if(!ok)++failures;
}
constexpr int kN=veyra::pipeline::ColorGradeTables::kCurveEntries;
constexpr int kH=veyra::pipeline::ColorGradeTables::kHueEntries;
constexpr int kL=veyra::pipeline::ColorGradeTables::kLumEntries;
float curveAt(const veyra::pipeline::ColorGradeTables& t,int index,int channel){
    return t.curve[std::size_t(index)*4+std::size_t(channel)];
}
float hueAt(const veyra::pipeline::ColorGradeTables& t,int index,int channel){
    return t.hue[std::size_t(index)*4+std::size_t(channel)];
}
float lumAt(const veyra::pipeline::ColorGradeTables& t,int index,int channel){
    return t.lum[std::size_t(index)*4+std::size_t(channel)];
}
std::array<float,3> applyMatrix(const veyra::pipeline::ColorGradeTables& t,std::array<float,3> v){
    std::array<float,3> out{};
    for(int r=0;r<3;++r)out[std::size_t(r)]=t.matrix[std::size_t(r*3)]*v[0]+t.matrix[std::size_t(r*3+1)]*v[1]+t.matrix[std::size_t(r*3+2)]*v[2];
    return out;
}
}
int main(){
    using veyra::engine::ColorSettings;
    using veyra::engine::ColorCurve;
    using veyra::pipeline::ColorGradeTables;

    // 1. Off, and on-but-neutral, are both visual no-ops.
    {
        const auto off=ColorGradeTables::bake(ColorSettings{});
        ColorSettings onNeutral;onNeutral.enabled=true;
        const auto on=ColorGradeTables::bake(onNeutral);
        bool ramp=true,mat=true,hueLum=true;
        for(int i=0;i<kN;++i)for(int c=0;c<3;++c)if(std::abs(curveAt(on,i,c)-float(i)/float(kN-1))>1e-6f)ramp=false;
        for(int i=0;i<9;++i)if(std::abs(on.matrix[std::size_t(i)]-(i%4==0?1.0f:0.0f))>1e-6f)mat=false;
        for(int i=0;i<kH;++i)if(std::abs(on.hue[std::size_t(i)*4+1]-1.0f)>1e-6f)hueLum=false;
        for(int i=0;i<kL;++i)for(int c=0;c<3;++c)if(std::abs(on.lum[std::size_t(i)*4+std::size_t(c)]-1.0f)>1e-6f)hueLum=false;
        check(off.identity&&on.identity,"master off and enabled-neutral both bake as identity");
        check(ramp&&mat&&hueLum,"identity tables are a linear ramp, unit matrix, unit gains");
    }
    // 2. Any real parameter clears the identity flag.
    {
        ColorSettings s;s.enabled=true;s.exposure=0.5f;
        check(!ColorGradeTables::bake(s).identity,"a real parameter clears the identity flag");
    }
    // 3. Log domain round trip (the curve table's indexing domain).
    {
        bool ok=true;
        // The table window is -5..+5 stops around 18% grey; values outside it are
        // passed through untouched by the shader (checked in the GPU test).
        for(float v:{0.01f,0.02f,0.18f,0.5f,1.0f,4.0f})ok=ok&&std::abs(ColorGradeTables::decodeLog(ColorGradeTables::encodeLog(v))-v)<v*1e-3f;
        check(ok,"log encode/decode round trips inside the 10-stop window");
    }
    // 4. White balance direction and neutral identity.
    {
        ColorSettings neutral;neutral.enabled=true;
        const auto mid=applyMatrix(ColorGradeTables::bake(neutral),{0.18f,0.18f,0.18f});
        check(std::abs(mid[0]-0.18f)<1e-4f&&std::abs(mid[2]-0.18f)<1e-4f,"neutral white balance leaves mid grey untouched");
        ColorSettings warm;warm.enabled=true;warm.temperature=80;
        ColorSettings cool;cool.enabled=true;cool.temperature=-80;
        const auto w=applyMatrix(ColorGradeTables::bake(warm),{0.18f,0.18f,0.18f});
        const auto c=applyMatrix(ColorGradeTables::bake(cool),{0.18f,0.18f,0.18f});
        check(w[0]>w[2]*1.01f,"positive temperature warms the image (red above blue)");
        check(c[0]<c[2]*0.99f,"negative temperature cools the image (blue above red)");
        // The cool end must be continuous, bounded and as strong as the warm end
        // (field report 2026-11-07: -100 multiplied blue by ~23 and blew the picture out).
        double previous=0;bool smooth=true,monotone=true;
        for(int v=0;v>=-100;--v){
            ColorSettings c;c.enabled=true;c.temperature=float(v);
            const auto m=applyMatrix(ColorGradeTables::bake(c),{0.18f,0.18f,0.18f});
            const double blueOverRed=double(m[2])/double(m[0]);
            if(v<0){smooth=smooth&&blueOverRed<previous*1.06;monotone=monotone&&blueOverRed>previous;}
            previous=blueOverRed;
        }
        check(smooth&&monotone,"cool temperature grows smoothly, one step at a time, from 0 to -100");
        ColorSettings warmest;warmest.enabled=true;warmest.temperature=100;
        ColorSettings coolest;coolest.enabled=true;coolest.temperature=-100;
        const auto hot=applyMatrix(ColorGradeTables::bake(warmest),{0.18f,0.18f,0.18f});
        const auto cold=applyMatrix(ColorGradeTables::bake(coolest),{0.18f,0.18f,0.18f});
        const double warmShift=std::log(double(hot[0])/double(hot[2])),coolShift=std::log(double(cold[2])/double(cold[0]));
        check(std::abs(coolShift/warmShift-1.0)<0.3,"-100 and +100 shift the white balance about equally");
        check(cold[0]>0.18f*0.5f&&cold[2]<0.18f*2.0f&&cold[1]<0.18f*1.2f,"-100 keeps mid grey within a 2x channel range");
        ColorSettings green;green.enabled=true;green.tint=80;
        const auto g=applyMatrix(ColorGradeTables::bake(green),{0.18f,0.18f,0.18f});
        check(g[1]<g[0]&&g[1]<g[2],"positive tint shifts mid grey towards magenta");
    }
    // 5. Tone controls move the right end of the curve and stay monotone.
    {
        const auto neutral=ColorGradeTables::bake(ColorSettings{});
        ColorSettings blacks;blacks.enabled=true;blacks.blacks=-50;
        ColorSettings whites;whites.enabled=true;whites.whites=50;
        const auto b=ColorGradeTables::bake(blacks);
        const auto w=ColorGradeTables::bake(whites);
        check(curveAt(b,8,0)<curveAt(neutral,8,0)-1e-3f,"negative blacks darkens the shadow end");
        check(curveAt(w,kN-9,0)>curveAt(neutral,kN-9,0)+1e-3f,"positive whites brightens the highlight end");
        bool monotone=true;
        for(int i=1;i<kN;++i)if(curveAt(b,i,0)<curveAt(b,i-1,0)-1e-6f)monotone=false;
        check(monotone,"baked curve stays monotone");
    }
    // 6. A point curve lands where the data model says it does.
    {
        ColorSettings s;s.enabled=true;
        s.curves[0].count=3;s.curves[0].points[1]={0.25f,0.35f};s.curves[0].points[2]={1,1};
        const auto t=ColorGradeTables::bake(s);
        const int index=int(0.25f*float(kN-1)+0.5f);
        check(std::abs(curveAt(t,index,0)-0.35f)<2e-3f,"RGB point curve is honoured at its control point");
        check(std::abs(curveAt(t,index,1)-0.35f)<2e-3f,"RGB master curve applies to every channel");
    }
    // 7. Per-channel curve only moves its own channel.
    {
        ColorSettings s;s.enabled=true;
        s.curves[2].count=3;s.curves[2].points[1]={0.5f,0.6f};s.curves[2].points[2]={1,1};
        const auto t=ColorGradeTables::bake(s);
        check(curveAt(t,kN/2,1)>curveAt(t,kN/2,0)+1e-3f,"green point curve only lifts green");
    }
    // 8. Mixer bands, grading zones and the shadow tint hit their own regions.
    {
        ColorSettings mixer;mixer.enabled=true;mixer.mixerSaturation[3]=100;   // green band
        const auto m=ColorGradeTables::bake(mixer);
        const int greenIndex=int(120.0f/360.0f*float(kH-1)+0.5f);
        check(hueAt(m,greenIndex,1)>1.5f,"green mixer band raises saturation at the green hue");
        check(std::abs(hueAt(m,kH/2,1)-1.0f)<0.35f,"a hue far from the band stays close to neutral");
        ColorSettings grade;grade.enabled=true;grade.grading[1].hue=0;grade.grading[1].saturation=100;
        const auto g=ColorGradeTables::bake(grade);
        check(lumAt(g,kL/2,0)>lumAt(g,kL/2,1)+0.05f&&lumAt(g,kL/2,0)>lumAt(g,kL/2,2)+0.05f,"mid-tone grading wheel pushes mid tones towards red");
        check(std::abs(lumAt(g,kL-1,0)-1.0f)<0.05f&&std::abs(lumAt(g,kL-1,2)-1.0f)<0.05f,"highlight end is untouched by the mid-tone wheel");
        ColorSettings tint;tint.enabled=true;tint.calibrationShadowTint=100;
        const auto st=ColorGradeTables::bake(tint);
        check(lumAt(st,0,1)>lumAt(st,0,0)&&lumAt(st,0,1)>lumAt(st,0,2),"shadow tint lifts green in the dark end");
        check(std::abs(lumAt(st,kL-1,1)-1.0f)<0.02f,"shadow tint leaves the bright end alone");
    }
    // 9. Constants pass through, and a LUT without a name is inert.
    {
        ColorSettings s;s.enabled=true;s.saturation=-100;s.vibrance=42;s.lutStrength=30;
        const auto t=ColorGradeTables::bake(s);
        check(t.saturation==-100&&t.vibrance==42,"saturation and vibrance pass through as constants");
        check(t.lutStrength==0.0f,"lut strength stays inert while no lut is selected");
        s.setLutName(L"film.cube");s.lutStrength=75;
        const auto withLut=ColorGradeTables::bake(s);
        check(std::abs(withLut.lutStrength-0.75f)<1e-6f,"lut strength normalises to 0..1 when a lut is selected");
    }
    // 10. Black & white mixer: the eight 黑白 rows must not be inert sliders.
    // The per-band weight is baked into the hue table's alpha channel and the
    // mode itself into the packed flags, so the shader needs no extra table.
    {
        ColorSettings s;s.enabled=true;s.blackWhite=true;s.blackWhiteMix[3]=60.0f;
        const auto t=ColorGradeTables::bake(s);
        const int greenIndex=int(120.0f/360.0f*float(kH-1)+0.5f);
        const int redIndex=int(0.0f/360.0f*float(kH-1)+0.5f);
        check(!t.identity,"enabling the black and white mixer clears the identity flag");
        check(t.blackWhite,"the bake reports the black and white mode");
        check(hueAt(t,greenIndex,3)>30.0f,"the green band lightens its grey in the B&W mix table");
        check(std::abs(hueAt(t,redIndex,3))<20.0f,"a band far from the edited colour stays near its neutral grey");
        float packed[20]{};
        packColorGradeConstants(t,packed);
        check(packed[18]>0.5f,"the packed flags tell the shader to run the monochrome path");
        ColorSettings off=s;off.blackWhite=false;
        const auto offTables=ColorGradeTables::bake(off);
        float offPacked[20]{};
        packColorGradeConstants(offTables,offPacked);
        check(offPacked[18]<0.5f,"with the mixer off the monochrome path stays disabled even though weights are baked");
        check(hueAt(offTables,greenIndex,3)>30.0f,"baked weights are kept so toggling the mode back on is instant");
        ColorSettings plain;s.enabled=true;
        check(ColorGradeTables::bake(plain).hue[std::size_t(greenIndex)*4+3]==0.0f,"identity bake leaves the B&W column at zero");
    }
    // 11. Per-section bypass ("分组眼睛"): bypassing a group must bake as if that
    // group were neutral while leaving the other groups alone.
    {
        ColorSettings s;s.enabled=true;s.exposure=1.0f;s.saturation=-100.0f;
        const auto full=ColorGradeTables::bake(s);
        check(!full.identity,"a real grade clears the identity flag");
        auto lightOnly=s;lightOnly.groupBypassMask=1u<<0;      // 亮 bypassed
        const auto withoutLight=ColorGradeTables::bake(lightOnly);
        check(withoutLight.exposure==0.0f,"bypassing 亮 removes its exposure from the bake");
        check(withoutLight.saturation==-100.0f,"bypassing 亮 leaves the 颜色 section alone");
        check(!withoutLight.identity,"a bypassed section still leaves the rest of the grade active");
        auto colOnly=s;colOnly.groupBypassMask=(1u<<0)|(1u<<1);
        check(ColorGradeTables::bake(colOnly).identity,"bypassing every used section bakes back to identity");
        auto curve=s;curve.curves[1].count=3;curve.curves[1].points[1]={0.5f,0.8f};
        auto curvesOff=curve;curvesOff.groupBypassMask=1u<<2;
        auto curvesReset=curve;curvesReset.curves[1].reset();
        check(ColorGradeTables::bake(curvesOff).curve==ColorGradeTables::bake(curvesReset).curve,
            "bypassing 曲线 bakes exactly the same table as resetting those curves");
        check(ColorGradeTables::bake(curve).curve!=ColorGradeTables::bake(curvesReset).curve,
            "the point curve still changes the table when it is not bypassed");
    }
    // 12. Point curves are curves, not polylines: the bake uses a monotone cubic
    // (Fritsch-Carlson) spline, so it is smooth, monotone and never overshoots.
    {
        ColorCurve sCurve;
        sCurve.count=3;sCurve.points[0]={0,0};sCurve.points[1]={0.5f,0.25f};sCurve.points[2]={1,1};
        const auto spline=veyra::pipeline::ColorGradeTables::curveValue(sCurve,0.25f);
        const float linear=0.125f;
        check(std::abs(spline-linear)>0.004f,"the point curve interpolates smoothly instead of straight segments");
        bool monotone=true,bounded=true;float previous=-1;
        for(int i=0;i<=200;++i){
            const float x=float(i)/200.0f;
            const float y=veyra::pipeline::ColorGradeTables::curveValue(sCurve,x);
            monotone&=y+1e-5f>=previous;previous=y;
            bounded&=y>=-1e-5f&&y<=1.0f+1e-5f;
        }
        check(monotone&&bounded,"the spline stays monotone and inside the unit square (no ringing)");
        ColorCurve identity;   // two points must stay exactly the ramp
        bool straight=true;
        for(int i=0;i<=100;++i){const float x=float(i)/100.0f;straight&=std::abs(veyra::pipeline::ColorGradeTables::curveValue(identity,x)-x)<2e-3f;}
        check(straight,"a two-point curve is still the identity ramp");
        ColorCurve strong;strong.count=4;strong.points[0]={0,0};strong.points[1]={0.3f,0.1f};
        strong.points[2]={0.7f,0.4f};strong.points[3]={1,1};
        const auto baked=ColorGradeTables::bake([&]{ColorSettings c;c.enabled=true;c.curves[0]=strong;return c;}());
        const int mid=kN/2;
        const float tableMid=curveAt(baked,mid,0);
        const float tableX=float(mid)/float(kN-1);
        check(std::abs(tableMid-veyra::pipeline::ColorGradeTables::curveValue(strong,tableX))<1e-5f,
            "the baked table samples the same spline the UI draws");
    }
    // 13. Mixer band separation: the shader matches these bands against the
    // display-referred hue, so a skin tone (~30 degrees, orange) must be driven
    // by the orange band - not dragged along by the red one.
    {
        ColorSettings s;s.enabled=true;
        s.mixerHue[0]=100.0f;    // red band shifted hard
        const auto redOnly=ColorGradeTables::bake(s);
        const int skinIndex=int(30.0f/360.0f*kH);          // ~orange
        const float redAtSkin=hueAt(redOnly,skinIndex,0);
        check(std::abs(redAtSkin)<6.0f,"the red band only nudges skin tones (narrow smooth falloff)");
        check(std::abs(hueAt(redOnly,0,0))>15.0f,"the red band still moves an actual red strongly");
        const int deepRed=0;
        check(std::abs(hueAt(redOnly,deepRed,0))>15.0f,"the red band still moves an actual red strongly");

        // Adjacent bands must cover the gaps between their centres, while a
        // single band remains isolated at the neighbouring centre.
        ColorSettings yellow;yellow.enabled=true;yellow.mixerHue[2]=100.0f;
        const auto yellowOnly=ColorGradeTables::bake(yellow);
        check(std::abs(hueAt(yellowOnly,int(90.0f/360.0f*kH),0))>10.0f,
              "the yellow band has continuous coverage through the yellow-green gap");
        check(std::abs(hueAt(yellowOnly,int(120.0f/360.0f*kH),0))<1.0f,
              "the yellow band fades at the green centre");
        ColorSettings orange;orange.enabled=true;orange.mixerHue[1]=100.0f;
        check(hueAt(ColorGradeTables::bake(orange),skinIndex,0)>27.0f,
              "orange controls skin hue; the test must enable its own settings");
        ColorSettings all;all.enabled=true;all.mixerHue.fill(100.0f);
        const auto uniform=ColorGradeTables::bake(all);
        bool covered=true;
        for(int i=0;i<kH;++i)covered&=std::abs(hueAt(uniform,i,0)-30.0f)<1e-4f;
        check(covered,"equal hue adjustments cover the entire ring without gaps or overlaps");
        check(std::abs(hueAt(redOnly,0,0)-hueAt(redOnly,kH-1,0))<1e-5f,
              "red hue response joins continuously at the ring seam");
    }
    if(failures){std::printf("FAIL: colour grade bake (%d checks)\n",failures);return 1;}
    std::printf("PASS: colour grade bake tables (identity, white balance, tone, curves, mixer, grading, lut)\n");
    return 0;
}
