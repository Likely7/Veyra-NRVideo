// Unit tests for the static HDR output tuning: preset table legality, parameter
// range validation, the preset resolver (display peak + strength), the match
// semantics used by the UI, PQ conversion, and the whole-title analyzer.
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

#include "veyra/engine/EffectChain.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/HdrOutputTuning.h"
#include "veyra/engine/PresetLibrary.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}


}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace veyra::engine;

    // ---- 1) the preset table itself ----
    check(kHdrTuningPresetCount >= 7, "presets: at least seven entries");
    for (std::size_t i = 0; i < kHdrTuningPresetCount; ++i) {
        const auto& p = kHdrTuningPresets[i];
        check(std::strlen(p.id) > 0, std::string("presets: entry ") + std::to_string(i) + " has an id");
        check(p.curve.valid(), std::string("presets: ") + p.id + " curve is within range");
        check(p.metadata.valid(), std::string("presets: ") + p.id + " metadata is within range");
        check(p.curve.strengthPercent == 100,
              std::string("presets: ") + p.id + " ships at 100% strength");
        for (std::size_t j = i + 1; j < kHdrTuningPresetCount; ++j)
            check(std::strcmp(p.id, kHdrTuningPresets[j].id) != 0,
                  std::string("presets: id ") + p.id + " is unique");
        check(findHdrTuningPreset(p.id) == &p, std::string("presets: lookup finds ") + p.id);
    }
    check(findHdrTuningPreset("no-such-preset") == nullptr, "presets: lookup rejects an unknown id");
    {
        const HdrTuningPreset* dark = findHdrTuningPreset("darkLift");
        const HdrTuningPreset* bright = findHdrTuningPreset("highlightGuard");
        check(dark && dark->curve.enabled && dark->curve.shadowLiftEv100 > 0,
              "presets: darkLift actually lifts the shadows");
        check(bright && bright->curve.enabled && bright->curve.highlightStartNits > 0,
              "presets: highlightGuard has a highlight roll-off");
    }

    // ---- 2) parameter range validation ----
    {
        HdrCurveSettings c;
        check(c.valid(), "range: default curve settings are valid");
        check(!c.enabled, "range: the curve is off by default (identity transform)");
        check(c.strengthPercent == 100, "range: strength defaults to 100%");
        check(c.displayPeakNits == 0, "range: display peak defaults to follow the system");
        c.midGrayNits = 99;
        check(!c.valid(), "range: mid-gray below 100 is rejected");
        c.midGrayNits = 501;
        check(!c.valid(), "range: mid-gray above 500 is rejected");
        c.midGrayNits = 203;
        c.shadowLiftEv100 = 201;
        check(!c.valid(), "range: shadow lift above 200 is rejected");
        c.shadowLiftEv100 = -201;
        check(!c.valid(), "range: shadow lift below -200 is rejected");
        c.shadowLiftEv100 = 0;
        c.highlightStartNits = 50;
        check(!c.valid(), "range: highlight start must be 0 or at least 100");
        c.highlightStartNits = 0;
        c.peakCapNits = 50;
        check(!c.valid(), "range: peak cap must be 0 or at least 100");
        c.peakCapNits = 0;
        c.rollOffPercent = 9;
        check(!c.valid(), "range: roll-off below 10% is rejected");
        c.rollOffPercent = 100;
        c.strengthPercent = 201;
        check(!c.valid(), "range: strength above 200 is rejected");
        c.strengthPercent = 100;
        c.displayPeakNits = 50;
        check(!c.valid(), "range: display peak must be 0 or at least 100");
        c.displayPeakNits = 10001;
        check(!c.valid(), "range: display peak above 10000 is rejected");
    }
    {
        HdrMetadataSettings m;
        check(m.valid(), "range: default metadata settings are valid");
        check(!m.enabled, "range: metadata hints are opt-in; existing HDR display behavior is preserved");
        m.masteringPeakNits = 50;
        check(!m.valid(), "range: mastering peak must be 0 or at least 100");
        m.masteringPeakNits = 0;
        m.maxFallNits = 20000;
        check(!m.valid(), "range: MaxFALL above 10000 is rejected");
    }

    // ---- 3) preset resolver: display peak then strength ----
    {
        const auto* dark = findHdrTuningPreset("darkLift");
        const auto* guard = findHdrTuningPreset("highlightGuard");
        HdrCurveSettings c;
        HdrMetadataSettings m;

        resolveHdrTuning(*dark, 100, 0, c, m);
        check(c == dark->curve, "resolve: baseline (no peak, 100%) equals the preset curve");
        check(m == dark->metadata, "resolve: baseline metadata equals the preset metadata");

        // A 600 nit display pulls the highlight work in, and must NOT touch the
        // shadow or mid-gray numbers (those describe content and environment).
        resolveHdrTuning(*guard, 100, 600, c, m);
        check(c.highlightStartNits == 270, "resolve: 600 nit display rolls off at 450*0.6");
        check(c.peakCapNits == 600, "resolve: 600 nit display caps at its own peak");
        check(c.midGrayNits == guard->curve.midGrayNits, "resolve: display peak does not move the mid-gray");
        check(c.shadowLiftEv100 == guard->curve.shadowLiftEv100 &&
              c.shadowRangeNits == guard->curve.shadowRangeNits,
              "resolve: display peak does not touch the shadow lift");
        check(m.masteringPeakNits == 600, "resolve: metadata peak follows the display");
        check(c.displayPeakNits == 600, "resolve: the resolved value records the display peak");

        resolveHdrTuning(*guard, 100, 2000, c, m);
        check(c.highlightStartNits == 900 && c.peakCapNits == 2000,
              "resolve: a 2000 nit display gets the whole range back");

        resolveHdrTuning(*guard, 0, 0, c, m);
        check(!c.enabled, "resolve: 0% strength fades the curve out entirely");
        resolveHdrTuning(*dark, 50, 0, c, m);
        check(c.shadowLiftEv100 == 25, "resolve: 50% halves the shadow lift");
        check(c.midGrayNits == 214, "resolve: 50% halves the mid-gray shift (203 + 22/2)");
        check(c.shadowRangeNits == 18, "resolve: 50% narrows the lift range");
        resolveHdrTuning(*guard, 50, 0, c, m);
        check(c.highlightStartNits == 900, "resolve: 50% starts the roll-off earlier (450/0.5)");
        check(c.peakCapNits == 1000, "resolve: the hard cap does not follow the strength dial");
        resolveHdrTuning(*dark, 200, 0, c, m);
        check(c.shadowLiftEv100 == 100, "resolve: 200% doubles the shadow lift");
        resolveHdrTuning(*guard, 200, 0, c, m);
        check(c.highlightStartNits == 225, "resolve: 200% compresses from further down (450/2)");

        bool allValid = true;
        for (std::size_t i = 0; i < kHdrTuningPresetCount; ++i) {
            for (unsigned s : {0u, 25u, 50u, 100u, 150u, 200u}) {
                for (unsigned p : {0u, 400u, 600u, 1000u, 2000u, 4000u, 10000u}) {
                    resolveHdrTuning(kHdrTuningPresets[i], s, p, c, m);
                    if (!c.valid() || !m.valid()) {
                        allValid = false;
                        std::printf("FAIL resolve: out of range for %s strength=%u peak=%u\n",
                                    kHdrTuningPresets[i].id, s, p);
                    }
                }
            }
        }
        check(allValid, "resolve: every preset x strength x display peak stays within range");
    }

    // ---- 4) match semantics used by the UI ----
    {
        const auto* dark = findHdrTuningPreset("darkLift");
        // The stored value is the baseline (what the preset ships); the dial and the
        // display peak are stored next to it and must not affect the match.
        HdrCurveSettings c = dark->curve;
        HdrMetadataSettings m = dark->metadata;
        check(hdrPresetMatches(c, m, *dark), "match: the raw preset matches itself");
        c.strengthPercent = 60;
        c.displayPeakNits = 800;
        check(hdrPresetMatches(c, m, *dark), "match: turning the dial keeps you on the same preset");
        c.displayPeakNits = 1600;
        check(hdrPresetMatches(c, m, *dark), "match: changing the display peak keeps the preset selected");
        c.shadowLiftEv100 += 1;
        check(!hdrPresetMatches(c, m, *dark), "match: editing a slider moves to custom");

        HdrCurveSettings resolved;
        HdrMetadataSettings resolvedMeta;
        resolveHdrTuning(*dark, 60, 800, resolved, resolvedMeta);
        check(resolved != dark->curve,
              "resolve: the applied curve differs from the baseline once scaled");
        check(resolvedMeta.masteringPeakNits == 560,
              "resolve: metadata peak scales with the display (700 * 0.8)");
    }

    // ---- 5) the two capture paths the session is written from ----
    {
        EnhancementSettings s;
        s.hdrCurve.enabled = true;
        s.hdrCurve.shadowLiftEv100 = 50;
        s.hdrCurve.shadowRangeNits = 35;
        s.hdrCurve.midGrayNits = 260;
        s.hdrCurve.strengthPercent = 140;
        s.hdrCurve.displayPeakNits = 1600;
        s.hdrMetadata.enabled = true;
        s.hdrMetadata.masteringPeakNits = 700;

        const auto globals = ChainGlobalSettings::capture(s);
        check(globals.hdrCurve == s.hdrCurve, "capture: globals carry the curve");
        check(globals.hdrCurve.strengthPercent == 140, "capture: globals carry the strength");
        check(globals.hdrCurve.displayPeakNits == 1600, "capture: globals carry the display peak");
        check(globals.hdrMetadata == s.hdrMetadata, "capture: globals carry the metadata");

        EnhancementSettings back;
        globals.apply(back);
        check(back.hdrCurve == s.hdrCurve, "capture: globals restore the curve");
        check(back.hdrMetadata == s.hdrMetadata, "capture: globals restore the metadata");

        EffectChain chain;
        const auto configuration = ChainConfiguration::capture(chain, s, 0, 0);
        check(configuration.hdrCurve == s.hdrCurve,
              "capture: a chain configuration carries the curve (this is what the session stores)");
        check(configuration.hdrMetadata == s.hdrMetadata,
              "capture: a chain configuration carries the metadata");
    }

    // Optional metadata must not leak between clips and must obey DXGI units.
    {
        HdrSourceMetadata source;
        source.observe(10,true,4000,2500,900);
        const auto first=resolveHdrMetadataLuminance({},source);
        source.observe(10,true,0,std::numeric_limits<float>::quiet_NaN(),0);
        check(resolveHdrMetadataLuminance({},source)==first,"metadata: omitted/NaN values retain the same clip metadata");
        source.observe(11,true,0,0,0);
        check(resolveHdrMetadataLuminance({},source)==HdrMetadataLuminance{},"metadata: a new epoch clears the previous clip");
        source.observe(11,true,1000,800,1200);
        check(resolveHdrMetadataLuminance({},source).fall==800,"metadata: MaxFALL never exceeds MaxCLL");
        source.observe(11,false,4000,2000,800);
        check(resolveHdrMetadataLuminance({},source)==HdrMetadataLuminance{},"metadata: SDR clears HDR source values");
        // ③: an SDR clip must not be described by the generic HDR10 fallback, and the
        // clamp must leave values alone while the curve is inactive.
        const auto fallback=resolveHdrMetadataLuminance({},HdrSourceMetadata{});
        {
            HdrCurveSettings offCurve;
            check(fallback.peak==1000&&fallback.cll==1000,
                  "metadata: an SDR clip resolves to the generic 1000-nit fallback (hence 3 gates it out)");
            check(clampMetadataToCurve(fallback,offCurve,{})==fallback,
                  "metadata: an inactive curve leaves the reported values untouched");
        }
        // ①: with a cap the reported content light levels must not exceed what the
        // present shader really outputs, and MaxFALL must stay under MaxCLL.
        {
            HdrCurveSettings capped;capped.enabled=true;capped.peakCapNits=300;capped.strengthPercent=100;
            const auto bounded=clampMetadataToCurve(fallback,capped,{});
            check(bounded.cll==300,"metadata: a 300-nit cap bounds MaxCLL");
            check(bounded.fall<=bounded.cll,"metadata: MaxFALL stays within MaxCLL after clamping");
            check(bounded.peak==fallback.peak,"metadata: the mastering peak keeps describing the master");
            HdrCurveSettings zeroStrength=capped;zeroStrength.strengthPercent=0;
            check(clampMetadataToCurve(fallback,zeroStrength,{})==fallback,
                  "metadata: a curve at zero strength does not rewrite the hint");
        }
        HdrMetadataSettings manual{true,700,800,900,50};
        const auto light=resolveHdrMetadataLuminance(manual,source);
        check(light.peak==700&&light.minX10000==50&&light.fall==800,"metadata: whole-nit maximum, 0.0001-nit minimum and bounded FALL");
    // Custom: the one-click recommendation is a pure function, so the table the UI
    // shows and the tests below cannot drift apart.
    {
        const auto dark=recommendHdrTuning(1000,1000,150,1000);
        check(dark.available&&std::string(dark.presetId)=="darkLift"&&std::string(dark.reasonKey)=="dark-content",
              "recommend: a dark film with few bright frames maps to darkLift");
        const auto narrow=recommendHdrTuning(400,600,320,1000);
        check(std::string(narrow.presetId)=="punch"&&std::string(narrow.reasonKey)=="narrow-range",
              "recommend: a low mastering ceiling maps to punch before highlight protection");
        const auto bright=recommendHdrTuning(4000,9978,4800,1000);
        check(std::string(bright.presetId)=="highlightGuard"&&std::string(bright.reasonKey)=="beyond-display",
              "recommend: an extremely bright film on a 1000-nit display is named as beyond it");
        const auto extremeBright=recommendHdrTuning(4000,4000,2400,10000);
        check(std::string(extremeBright.presetId)=="highlightGuard"&&std::string(extremeBright.reasonKey)=="bright-content",
              "recommend: extreme content on a display that can show it keeps the bright-content reason");
        const auto balanced=recommendHdrTuning(1000,1000,380,1000);
        check(std::string(balanced.presetId)=="standard"&&std::string(balanced.reasonKey)=="balanced",
              "recommend: a balanced film keeps the standard curve");
        // The display's ceiling must take part in the decision: content far beyond the
        // panel needs the highlight ceiling even when its average is ordinary, and a dark
        // film on a dim display needs both ends at once. displayPeak 0 means "unknown".
        {
            const auto dimDark=recommendHdrTuning(1000,1000,150,300);
            check(std::string(dimDark.presetId)=="darkAndBright"&&std::string(dimDark.reasonKey)=="dark-and-bright",
                  "recommend: a dark film on a dim display gets both ends");
            const auto dimBright=recommendHdrTuning(4000,2000,800,300);
            check(std::string(dimBright.presetId)=="highlightGuard"&&std::string(dimBright.reasonKey)=="beyond-display",
                  "recommend: content beyond the panel maps to highlight protection, named as such");
            const auto unknownDisplay=recommendHdrTuning(1000,1000,150,0);
            check(std::string(unknownDisplay.presetId)=="darkLift",
                  "recommend: an unknown display peak skips the beyond-display branch");
            const auto brightDisplay=recommendHdrTuning(1000,1000,150,2000);
            check(std::string(brightDisplay.presetId)=="darkLift"&&std::string(brightDisplay.reasonKey)=="dark-content",
                  "recommend: a display above the content peak needs no extra protection");
        }
        const auto missing=recommendHdrTuning(0,0,0,1000);
        check(!missing.available&&std::string(missing.reasonKey)=="no-metadata",
              "recommend: a source without metadata is reported instead of guessed");
        check(recommendHdrTuning(1000,3000,150,1000).fallNits==150&&recommendHdrTuning(1000,3000,150,1000).cllNits==3000,
              "recommend: MaxCLL and MaxFALL are carried through for the hint text");
        // The renamed environment ids state the action; the old ones must keep working.
        {
            const auto plain=hdrAxesCurve("standard","plain");
            const auto compensate=hdrAxesCurve("standard","compensate");
            check(plain.midGrayNits==203&&plain.shadowLiftEv100==0,
                  "environment: plain leaves the neutral content untouched (203 nit, no lift)");
            check(compensate.midGrayNits==203+kHdrEnvironmentBrightShift&&compensate.shadowLiftEv100>0,
                  "environment: compensate raises the mid-gray and the shadows");
            const auto legacyDark=hdrAxesCurve("standard","dark");
            const auto legacyBright=hdrAxesCurve("standard","bright");
            check(legacyDark.midGrayNits==plain.midGrayNits&&legacyDark.shadowLiftEv100==plain.shadowLiftEv100,
                  "environment: the legacy dark id still means plain");
            check(legacyBright.midGrayNits==compensate.midGrayNits,
                  "environment: the legacy bright id still means compensate");
        }
        // The two preset systems must agree on what a finding means.
        check(std::string(hdrAxesContentForPreset("darkLift"))=="dark"&&
              std::string(hdrAxesContentForPreset("highlightGuard"))=="highlight"&&
              std::string(hdrAxesContentForPreset("punch"))=="punch"&&
              std::string(hdrAxesContentForPreset("standard"))=="standard",
              "mapping: every recommended legacy preset names a content");
        check(std::string(hdrAxesContentForPreset("brightRoom"))=="standard"&&
              std::string(hdrAxesContentForPreset(nullptr))=="standard",
              "mapping: environment-only presets and null fall back to the neutral content");
        // The effective cap follows the display rescale and the strength dial: it is
        // what the highlight warning compares the source peak against.
        HdrCurveSettings curve;HdrMetadataSettings metadata;
        curve.enabled=true;curve.peakCapNits=1000;curve.displayPeakNits=0;curve.strengthPercent=100;
        check(hdrEffectivePeakCapNits(curve,metadata)==1000,"cap: 100% strength keeps the authored ceiling");
        curve.displayPeakNits=500;
        check(hdrEffectivePeakCapNits(curve,metadata)==500,"cap: a 500 nit display halves the authored ceiling");
        curve.displayPeakNits=0;curve.enabled=false;
        check(hdrEffectivePeakCapNits(curve,metadata)==0,"cap: a disabled curve caps nothing");
        // The warning must survive a source with no metadata at all: an enabled
        // curve with a cap and a peak of 0 divided by zero and crashed the UI thread.
        HdrCurveSettings warnCurve;HdrMetadataSettings warnMetadata;
        warnCurve.enabled=true;warnCurve.peakCapNits=1000;warnCurve.displayPeakNits=0;warnCurve.strengthPercent=100;
        const auto noSource=hdrPeakWarning(0,false,true,warnCurve,warnMetadata);
        check(!noSource.valid&&noSource.keptPercent==100&&!noSource.clipped&&noSource.capNits==1000,
              "warning: a source without metadata is inert instead of dividing by zero");
        // The graph is a port of the shader, so the defaults must be exact no-ops
        // and the headline behaviours must match the HLSL branch for branch.
        {
            const HdrCurveSettings identity;   // lift 0, mid 203, start 0, cap 0
            check(std::abs(hdrCurveLuminance(identity,100.0)-100.0)<1e-6&&
                  std::abs(hdrCurveLuminance(identity,0.25)-0.25)<1e-6,
                  "graph: the default curve is an exact identity");
        // The live meter's numbers must be defined by tests, not by the render loop.
        {
            HdrCurveSettings curve;curve.enabled=true;curve.peakCapNits=1000;
            curve.highlightStartNits=450;curve.shadowRangeNits=35;curve.shadowLiftEv100=50;
            const double frame[]={10.0,20.0,30.0,100.0,500.0,900.0,2000.0};
            const auto coverage=hdrCurveCoverage(frame,7,curve);
            check(coverage.valid&&coverage.samples==7,
                  "coverage: a valid curve counts every finite sample");
        // The probe carries PQ code values; the decode must land on the published
        // reference points, otherwise every threshold comparison is meaningless.
        {
            check(pqCodeToNits(0.0)==0.0,"pq: code zero is zero nits");
            check(std::abs(pqCodeToNits(0.5081)-100.0)<2.0,
                  "pq: the published 100-nit code lands on 100 nits");
            check(std::abs(pqCodeToNits(0.7518)-1000.0)<20.0,
                  "pq: the published 1000-nit code lands on 1000 nits");
            check(std::abs(pqCodeToNits(1.0)-10000.0)<1.0,"pq: full scale is 10000 nits");
            double previous=-1.0;bool rising=true;
            for(unsigned i=0;i<=100;++i){
                const double value=pqCodeToNits(double(i)/100.0);
                if(value<previous)rising=false;
                previous=value;
            }
            check(rising,"pq: the decode is monotonic");
        }
            check(coverage.aboveCeiling==0&&coverage.aboveCeilingPercent()==0,
                  "coverage: the soft knee prevents false hard-clipping reports");
            check(coverage.inRollOff==3,
                  "coverage: every value entering the highlight knee is counted");
            check(coverage.inLift==3,
                  "coverage: samples below the lift range are counted as being raised");
            const auto disabled=hdrCurveCoverage(frame,7,HdrCurveSettings{});
            check(!disabled.valid&&disabled.samples==0&&disabled.aboveCeilingPercent()==0.0,
                  "coverage: a disabled curve reports nothing instead of zeroes that look real");
            check(hdrMetadataNits(INFINITY)==0&&hdrMetadataNits(NAN)==0&&hdrMetadataNits(-1)==0&&hdrMetadataNits(10001)==10000,
                  "source metadata: non-finite values are absent and finite values bounded");
            const auto empty=hdrCurveCoverage(nullptr,0,curve);
            check(!empty.valid,"coverage: no samples means no reading");
            // 20 nit sits inside the 35-nit lift range; 50 would not.
            const double garbage[]={0.0,-5.0,std::nan(""),20.0};
            const auto filtered=hdrCurveCoverage(garbage,4,curve);
            check(filtered.samples==4&&filtered.aboveCeiling==0&&filtered.inRollOff==0&&filtered.inLift==1,
                  "coverage: zero, negative and non-finite samples are ignored by the buckets");
        }
            HdrCurveSettings lift;lift.shadowLiftEv100=50;lift.shadowRangeNits=35;
            check(hdrCurveLuminance(lift,5.0)>5.0&&hdrCurveLuminance(lift,200.0)==200.0,
                  "graph: the shadow lift raises dark values and leaves bright ones alone");
            HdrCurveSettings capped;capped.peakCapNits=1000;capped.highlightStartNits=450;
            capped.rollOffPercent=120;
            check(hdrCurveLuminance(capped,8000.0)==1000.0,
                  "graph: the hard cap is a hard cap");
            check(hdrCurveLuminance(capped,600.0)<600.0,
                  "graph: the roll-off already compresses above the highlight start");
            HdrCurveSettings mid;mid.midGrayNits=250;
            // By construction the mapping is the identity at the peak (1000 nit with no cap);
            // a raised mid-gray lifts below it and pulls above it down.
            check(hdrCurveLuminance(mid,250.0)>250.0&&hdrCurveLuminance(mid,1000.0)==1000.0&&
                  hdrCurveLuminance(mid,2000.0)<2000.0,
                  "graph: a raised mid-gray lifts the middle, pivots at the peak and pulls the top down");
            double previous=0.0;bool monotonic=true;
            HdrCurveSettings all;all.enabled=true;all.shadowLiftEv100=40;all.shadowRangeNits=35;
            all.midGrayNits=225;all.highlightStartNits=450;all.rollOffPercent=120;all.peakCapNits=1000;
            for (unsigned i=1;i<kHdrCurveGraphPoints;++i) {
                const double value=hdrCurveLuminance(all,hdrCurveGraphInputNits(i));
                if (value+1e-9<previous) monotonic=false;
                previous=value;
            }
            check(monotonic,"graph: every sample of a full curve stays monotonic");
            check(hdrCurveGraphInputNits(0)==kHdrCurveGraphMinNits&&
                  std::abs(hdrCurveGraphInputNits(kHdrCurveGraphPoints-1)-kHdrCurveGraphMaxNits)<1e-6,
                  "graph: the sweep spans the advertised input range");
        }
        const auto clipped=hdrPeakWarning(4000,true,true,warnCurve,warnMetadata);
        check(clipped.valid&&clipped.clipped&&clipped.keptPercent==25,
              "warning: a 4000 nit source under a 1000 nit ceiling keeps a quarter of its peak");
        const auto covered=hdrPeakWarning(800,true,true,warnCurve,warnMetadata);
        check(covered.valid&&!covered.clipped&&covered.keptPercent==100,
              "warning: a source below the ceiling is reported as safe");
        const auto noCap=hdrPeakWarning(4000,true,true,HdrCurveSettings{},warnMetadata);
        check(!noCap.clipped&&noCap.capNits==0,
              "warning: a curve without a ceiling never reports clipping");
        // Custom: the content x environment axes and the three plain dials. Both are
        // pure mappings, so the UI can show readable controls without a second
        // interpretation of the same numbers living in QML.
        {
            const auto darkNight=hdrAxesCurve("dark","dark");
            const auto darkDay=hdrAxesCurve("dark","bright");
            check(darkNight.shadowLiftEv100==50&&darkNight.highlightStartNits==0,
                  "axes: the dark content keeps its lift and adds no highlight ceiling");
            check(darkDay.midGrayNits==225+kHdrEnvironmentBrightShift&&darkNight.midGrayNits==225-kHdrEnvironmentDarkShift,
                  "axes: the environment axis moves only the mid-gray reference");
            check(darkNight.highlightStartNits==darkDay.highlightStartNits&&
                  darkNight.peakCapNits==darkDay.peakCapNits&&darkNight.rollOffPercent==darkDay.rollOffPercent,
                  "axes: the environment axis never touches the highlight behaviour");
            check(darkDay.shadowLiftEv100==darkNight.shadowLiftEv100+kHdrEnvironmentBrightLiftEv100,
                  "axes: daylight lifts the shadows by the documented amount");
            const auto standardDay=hdrAxesCurve("standard","bright");
            check(standardDay.shadowLiftEv100>0&&standardDay.shadowRangeNits>=kHdrEnvironmentBrightLiftRangeNits,
                  "axes: even the neutral content gets a usable daylight shadow lift");
            const auto highlight=hdrAxesCurve("highlight","bright");
            check(highlight.peakCapNits==1000&&highlight.rollOffPercent==120&&highlight.highlightStartNits==450,
                  "axes: the highlight content carries its ceiling through both environments");
            const auto unknown=hdrAxesCurve("does-not-exist","dark");
            const auto standard=hdrAxesCurve("standard","dark");
            check(unknown.shadowLiftEv100==standard.shadowLiftEv100&&unknown.midGrayNits==standard.midGrayNits,
                  "axes: an unknown content id falls back to the neutral baseline");
            for (const auto& preset : kHdrContentPresets) {
                const auto day=hdrAxesCurve(preset.id,"bright");
                const auto night=hdrAxesCurve(preset.id,"dark");
                check(day.midGrayNits>=100&&day.midGrayNits<=500&&night.midGrayNits>=100&&night.midGrayNits<=500,
                      "axes: every combination stays inside the valid mid-gray range");
            }
            HdrCurveSettings curve;
            for (unsigned level=0;level<=10;++level) {
                setHdrShadowLiftLevel(curve,level);
                check(hdrShadowLiftLevel(curve)==level,"dials: the shadow-lift dial round-trips at every step");
            }
            for (unsigned level=0;level<=10;++level) {
                setHdrBrightnessLevel(curve,level);
                check(hdrBrightnessLevel(curve)==level,"dials: the brightness dial round-trips at every step");
            }
            for (unsigned level=0;level<kHdrHighlightStepCount;++level) {
                setHdrHighlightLevel(curve,level);
                check(hdrHighlightLevel(curve)==level,"dials: the highlight dial round-trips at every step");
            }
            check(hdrBrightnessLevel(hdrAxesCurve("standard","bright"))>=
                  hdrBrightnessLevel(hdrAxesCurve("standard","dark")),
                  "dials: the daylight curve reads brighter than the night one");
            HdrCurveSettings edited;edited.shadowLiftEv100=31;edited.shadowRangeNits=29;
            check(hdrShadowLiftLevel(edited)==3,"dials: an off-grid lift picks the nearest step");
        }
    }

    }

    // Real public persistence paths: old v10 Flow data is preserved and the new
    // library/session carry HDR, NR strength five and per-style controls together.
    {
        const auto dir=std::filesystem::temp_directory_path()/L"pr19-hdr-persistence";
        std::filesystem::create_directories(dir);
        const auto read=[](const auto& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});};
        const auto path=dir/L"library";
        PresetLibrary library(path);library.setIncludeBuiltins(false);
        EnhancementSettings s;s.nr=true;s.nrRuntime=NrRuntime::NvidiaOriginal;s.residual.total=5;
        s.model.style=2;s.residual.correction.enabled=true;
        s.residual.correction.neutral=.31f;
        s.content=ContentRate::Fps30;s.opticalFlowBackend=OpticalFlowBackend::AmdFidelityFx;
        PresetEntry entry;entry.name=L"HDR+NR+Flow";entry.chain=toChain(s);
        entry.contents=presetContentMask(PresetContent::Chain)|presetContentMask(PresetContent::Flow);
        entry.globals=ChainGlobalSettings::capture(s);entry.flow=PresetFlowSettings::capture(s);
        check(library.load()&&library.put(entry),"persist: save existing v10 Flow entry");
        const auto oldPath=std::filesystem::path(path).concat(L".flow");const auto before=read(oldPath);
        check(before.starts_with("VEYRA_PRESET_LIBRARY 10"),"persist: existing Flow uses v10 without an HDR block");
        const auto* preset=findHdrTuningPreset("darkAndBright");
        s.hdrCurve=preset->curve;s.hdrMetadata=preset->metadata;
        s.hdrCurve.strengthPercent=140;s.hdrCurve.displayPeakNits=1600;
        entry.globals=ChainGlobalSettings::capture(s);
        check(library.put(entry,true),"persist: save v11 HDR plus Flow and NR controls");
        check(read(oldPath)==before,"persist: retain exact old v10 file as a rollback point");
        PresetLibrary restored(path);restored.setIncludeBuiltins(false);
        check(restored.load()&&restored.entries().size()==1&&restored.entries()[0]==entry,"persist: reopen new HDR sibling with all entry fields");
        EnhancementSettings applied;
        if(restored.entries().size()==1)PresetLibrary::apply(restored.entries()[0],applied);
        check(applied.hdrCurve==s.hdrCurve&&applied.hdrMetadata==s.hdrMetadata&&applied.residual.total==5&&
            applied.model.style==2&&applied.residual.correction.neutral==.31f&&applied.content==ContentRate::Auto,
            "persist: HDR preset applies while retaining NR five, style controls and Flow");
        auto session=std::make_unique<ChainSession>(ChainSession::initial(s));
        check(session->select(ChainMode::Node),"persist: initialize separate node settings");
        session->configurations[1].hdrCurve.strengthPercent=75;
        const auto sessionPath=dir/L"session";ChainSessionStore writer(sessionPath);
        auto loaded=std::make_unique<ChainSession>(ChainSession::initial({}));ChainSessionStore reader(sessionPath);
        check(writer.save(*session)&&reader.load(*loaded)&&*loaded==*session,"persist: session v6 restores independent list/node HDR and NR settings");
        const auto encoded=read(std::filesystem::path(sessionPath).concat(L".hdr-output"));
        check(encoded.starts_with("VEYRA_CHAIN_SESSION 6"),"persist: HDR has its own session version, old v1-v5 retain their layout");
    }

    std::printf(failures == 0 ? "hdr output tuning: all checks passed\n"
                              : "hdr output tuning: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
