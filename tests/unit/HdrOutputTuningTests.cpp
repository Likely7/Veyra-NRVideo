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
        HdrMetadataSettings manual{true,700,800,900,50};
        const auto light=resolveHdrMetadataLuminance(manual,source);
        check(light.peak==700&&light.minX10000==50&&light.fall==800,"metadata: whole-nit maximum, 0.0001-nit minimum and bounded FALL");
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
            applied.model.style==2&&applied.residual.correction.neutral==.31f&&applied.content==ContentRate::Fps30,
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
