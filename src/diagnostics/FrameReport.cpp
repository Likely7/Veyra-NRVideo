// SPDX-License-Identifier: GPL-3.0-only
// 性能诊断 report files: frames.csv, SUMMARY.md (for people and for Claude/Codex),
// a single offline report.html, and the comparison outputs.
#include "FrameAnalysisInternal.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace veyra::diagnostics::analysis::detail {
using json::Array;
using json::Object;
using json::Value;
namespace fs = std::filesystem;

namespace {
std::string f1(double v, int digits = 1) {
    if (!std::isfinite(v)) return "—";
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", digits, v);
    return b;
}
std::string n(const Value& v, int digits = 1) { return v.isNumber() ? f1(v.number(), digits) : "—"; }
std::string text(const Value& v) {
    if (v.isString()) return v.string();
    if (v.isNumber()) return f1(v.number(), v.number() == std::floor(v.number()) ? 0 : 3);
    if (v.isBool()) return v.boolean() ? "是" : "否";
    if (v.isNull()) return "—";
    return json::dump(v, -1);
}
std::string mmss(double ms) {
    if (!std::isfinite(ms) || ms < 0) return "—";
    const int s = int(ms / 1000.0);
    char b[32];
    std::snprintf(b, sizeof(b), "%02d:%02d.%01d", s / 60, s % 60, int(std::fmod(ms, 1000.0) / 100.0));
    return b;
}
const char* severityZh(const std::string& s) {
    return s == "high" ? "严重" : s == "medium" ? "注意" : s == "pass" ? "通过" : "提示";
}
}

void writeFramesCsv(const Analysis& a, const fs::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "index,t_ms,interval_ms,kind,session,revision,source,subframe,steady,hitch,cause,stage,event,"
           "present_call_ms,arrival_to_process_ms,process_to_ready_ms,ready_to_present_ms,late_vs_deadline_ms,arrival_to_present_ms\n";
    char line[512];
    for (size_t i = 0; i < a.frames.size(); ++i) {
        const auto& f = a.frames[i];
        const auto opt = [](double v) { return v >= 0 ? f1(v, 3) : std::string(); };
        std::snprintf(line, sizeof(line), "%zu,%.3f,%.3f,%s,%llu,%llu,%llu,%u,%d,%d,%s,%s,%d,%s,%s,%s,%s,%s,%s\n", i, f.t, f.dt,
                      f.kind == 1 ? "real" : f.kind == 2 ? "generated" : "", static_cast<unsigned long long>(f.session),
                      static_cast<unsigned long long>(f.revision), static_cast<unsigned long long>(f.source), f.subframe, int(f.steady),
                      int(f.hitch), causeId(f.cause), f.stage.c_str(), f.event, opt(f.callMs).c_str(), opt(f.waitInMs).c_str(),
                      opt(f.gpuMs).c_str(), opt(f.pacingMs).c_str(), std::isfinite(f.lateMs) ? f1(f.lateMs, 3).c_str() : "",
                      opt(f.latencyMs).c_str());
        out << line;
    }
}

void writeSummary(const Analysis& a, const fs::path& path) {
    const Value& r = a.report;
    const Value& s = r["summary"];
    const Value& env = r["environment"];
    std::ostringstream o;
    o << "# Veyra 性能诊断摘要\n\n";
    o << "- 版本：" << text(env["veyraVersion"]) << "；记录 " << n(s["durationSeconds"], 0) << " 秒；来源 " << text(s["sourceKind"])
      << "（到达 " << n(s["sourceFps"], 2) << " fps）\n";
    o << "- " << text(s["headline"]) << "\n";
    o << "- 期望输出 " << n(s["expectedFps"], 2) << " fps，最长帧间隔 " << n(s["worstIntervalMs"], 1) << " ms，卡顿每分钟 "
      << n(s["hitchesPerMinute"], 2) << " 次，用户标记 " << n(s["marks"], 0) << " 次\n";
    if (r["trace"]["partial"].boolean() || r["trace"]["dropped"].number() > 0)
        o << "- 记录不完整：partial=" << text(r["trace"]["partial"]) << " dropped=" << n(r["trace"]["dropped"], 0) << "\n";
    o << "\n## 主要发现\n\n";
    int k = 0;
    for (const auto& f : r["findings"].array()) {
        o << ++k << ". **[" << f["id"].string() << " · " << severityZh(f["severity"].string()) << "] " << f["title"].string() << "**  \n";
        o << "   证据：" << f["evidence"].string() << "  \n";
        o << "   建议：" << f["advice"].string() << "\n";
        if (!f["developer"].string().empty()) o << "   开发：" << f["developer"].string() << "\n";
    }
    if (!k) o << "（无）\n";
    const Value& out = r["output"];
    o << "\n## 关键指标（稳定段）\n\n| 指标 | 值 |\n|---|---|\n";
    o << "| 送显帧数（真实/生成） | " << n(out["presents"], 0) << "（" << n(out["realPresents"], 0) << "/" << n(out["generatedPresents"], 0) << "） |\n";
    o << "| 帧间隔 P50/P95/P99/最大 ms | " << n(out["intervalMs"]["p50"], 2) << " / " << n(out["intervalMs"]["p95"], 2) << " / "
      << n(out["intervalMs"]["p99"], 2) << " / " << n(out["intervalMs"]["max"], 2) << " |\n";
    o << "| 平均 fps | " << n(out["averageFps"], 2) << " |\n";
    o << "| 1% low（P99 换算 / 最慢 1% 平均） | " << n(out["low1FpsP99"], 2) << " / " << n(out["low1FpsSlowestAverage"], 2) << " |\n";
    o << "| 0.1% low（P99.9 / 最慢 0.1% 平均） | " << n(out["low01FpsP999"], 2) << " / " << n(out["low01FpsSlowestAverage"], 2) << " |\n";
    o << "| 卡顿（>1.5×/2×/3× 期望间隔、>50/100 ms） | " << n(out["hitches"]["over1_5x"], 0) << " / " << n(out["hitches"]["over2x"], 0) << " / "
      << n(out["hitches"]["over3x"], 0) << " / " << n(out["hitches"]["over50ms"], 0) << " / " << n(out["hitches"]["over100ms"], 0) << " |\n";
    o << "| 节拍不均（偏离期望 >25%） | " << n(Value(out["irregularShare"].number() * 100), 1) << "% |\n";
    const Value& lat = r["latency"];
    o << "| 到达→送显 P50/P95 ms | " << n(lat["arrivalToPresentMs"]["p50"], 2) << " / " << n(lat["arrivalToPresentMs"]["p95"], 2) << " |\n";
    o << "| 到达→开始处理 P95 ms | " << n(lat["arrivalToProcessMs"]["p95"], 2) << " |\n";
    o << "| 开始处理→GPU 就绪 P95 ms | " << n(lat["processToReadyMs"]["p95"], 2) << " |\n";
    o << "| 送显调用 P95/最大 ms | " << n(lat["presentCallMs"]["p95"], 3) << " / " << n(lat["presentCallMs"]["max"], 2) << " |\n";
    o << "| 晚于期限比例（>0.5 ms） | " << n(Value(lat["lateShare"].number() * 100), 1) << "% |\n";
    const Value& disp = r["display"];
    o << "| 送显→上屏 P50（DXGI 统计） ms | " << n(disp["presentToScanoutP50Ms"]["p50"], 2) << " |\n";
    o << "| 刷新率 / 可变刷新生效秒数 | " << n(disp["refreshHz"], 2) << " Hz / " << n(disp["vrrActiveSeconds"], 0) << " |\n";
    if (r["byRevision"].array().size() > 1) {
        o << "\n## 按设置版本（记录期间改过设置）\n\n| 版本 | 时间段 | 帧数 | 平均 fps | 期望 fps | 最慢 1% 平均 | 倍数 | 卡顿 |\n|---|---|---|---|---|---|---|---|\n";
        for (const auto& v : r["byRevision"].array())
            o << "| " << n(v["revision"], 0) << " | " << mmss(v["fromMs"].number()) << "–" << mmss(v["toMs"].number()) << " | " << n(v["presents"], 0)
              << " | " << n(v["averageFps"], 1) << " | " << n(v["expectedFps"], 1) << " | " << n(v["low1FpsSlowestAverage"], 1) << " | "
              << n(v["observedMultiplier"], 2) << " | " << n(v["hitches"], 0) << " |\n";
    }
    o << "\n## 卡顿归因（额外时间占比）\n\n";
    for (const auto& [cause, share] : r["hitchCauseShare"].object().items) o << "- " << cause << "：" << f1(share.number() * 100, 0) << "%\n";
    o << "\n## 前 10 个卡顿事件\n\n| 时间 | 最长间隔 ms | 次数 | 归因 | 阶段 | 标记 |\n|---|---|---|---|---|---|\n";
    k = 0;
    for (const auto& e : r["events"].array()) {
        if (++k > 10) break;
        std::string marks;
        for (const auto& m : e["marks"].array()) marks += (marks.empty() ? "" : ",") + f1(m.number(), 0);
        o << "| " << mmss(e["startMs"].number()) << " | " << n(e["worstIntervalMs"], 1) << " | " << n(e["hitches"], 0) << " | " << e["causeText"].string()
          << " | " << text(e["stage"]) << " | " << marks << " |\n";
    }
    o << "\n## GPU 阶段（P50 / P95 / 最大 ms）\n\n";
    std::vector<std::pair<double, std::string>> gpu;
    for (const auto& [stage, d] : r["stages"]["gpuMs"].object().items)
        gpu.emplace_back(d["p95"].number(), "- " + stage + "：" + n(d["p50"], 3) + " / " + n(d["p95"], 3) + " / " + n(d["max"], 2) + "（" + n(d["n"], 0) + " 次）\n");
    std::sort(gpu.rbegin(), gpu.rend());
    for (size_t i = 0; i < std::min<size_t>(12, gpu.size()); ++i) o << gpu[i].second;
    o << "\n## CPU 阶段（P50 / P95 / 最大 ms）\n\n";
    std::string loop;
    for (const auto& [stage, d] : r["stages"]["cpuMs"].object().items) {
        const std::string line = "- " + stage + "：" + n(d["p50"], 3) + " / " + n(d["p95"], 3) + " / " + n(d["max"], 2) + "\n";
        // Lower-case names are the engine loop's own phases (controlAndSchedule includes idle waiting).
        if (!stage.empty() && std::islower(static_cast<unsigned char>(stage[0]))) loop += line;
        else o << line;
    }
    if (!loop.empty()) o << "\n引擎循环各段（每圈一次；controlAndSchedule 含等待）：\n\n" << loop;
    const Value& fg = r["frameGeneration"];
    o << "\n## 补帧\n\n- 观察到的倍数 " << n(fg["observedMultiplier"], 2) << "；生成帧丢弃 " << n(fg["discardedGenerated"], 0) << "（"
      << f1(fg["discardedGeneratedShare"].number() * 100, 1) << "%）；评估 " << n(fg["evaluatedPairs"], 0) << "，评估前跳过 "
      << n(fg["skippedBeforeEvaluation"], 0) << "（" << f1(fg["skippedShare"].number() * 100, 1) << "%）\n";
    for (const auto& [reason, count] : fg["discarded"].object().items) o << "- 丢弃原因 " << reason << "：" << n(count, 0) << "\n";
    const Value& lg = r["log"];
    o << "- 日志：准入 " << n(lg["fgAdmittedPairs"], 0) << " 对，拒绝 " << n(lg["fgRejectedPairs"], 0) << " 对，补帧 GPU P95 中位 "
      << n(lg["gpuFgBatchP95Ms"]["p50"], 2) << " ms\n";
    const Value& src = r["source"];
    o << "\n## 输入\n\n- 类型 " << text(src["kind"]) << "，接受 " << n(src["accepted"], 0) << " 帧，到达间隔 P50/SD/P99 "
      << n(src["arrivalIntervalMs"]["p50"], 2) << " / " << n(src["arrivalIntervalMs"]["sd"], 2) << " / " << n(src["arrivalIntervalMs"]["p99"], 2)
      << " ms，时钟漂移 " << n(src["clockDriftPpm"], 0) << " ppm\n";
    for (const auto& [reason, count] : src["skipped"].object().items) o << "- 跳过 " << reason << "：" << n(count, 0) << "\n";
    const Value& tel = r["telemetry"];
    o << "\n## 系统采样（每秒）\n\n";
    o << "- 系统 CPU 平均/P90 " << n(tel["cpu_total"]["mean"], 0) << "% / " << n(tel["cpu_total"]["p90"], 0) << "%，Veyra CPU 平均 "
      << n(tel["cpu_veyra"]["mean"], 1) << "%\n";
    o << "- GPU 最忙引擎平均 " << n(tel["gpu_busy_max"]["mean"], 0) << "%，Veyra " << n(tel["gpu_veyra_max"]["mean"], 0) << "%，其他进程 P90 "
      << n(tel["gpu_others_max"]["p90"], 0) << "%\n";
    for (const auto& p : tel["topOtherGpuProcesses"].array())
        o << "  - " << p["process"].string() << "：" << n(p["seconds"], 0) << " 秒，峰值 " << n(p["peakPercent"], 0) << "%\n";
    o << "- NVIDIA 频率平均 " << n(tel["nv_clock_mhz"]["mean"], 0) << " MHz，功耗平均 " << n(tel["nv_power_w"]["mean"], 0) << " W，温度最高 "
      << n(tel["nv_temp_c"]["max"], 0) << " °C\n";
    for (const auto& [reason, seconds] : tel["nvidiaClockReasonsSeconds"].object().items) o << "  - 降频原因 " << reason << "：" << n(seconds, 0) << " 秒\n";
    o << "- 界面线程最长间隔 " << n(tel["ui_gap_ms"]["max"], 0) << " ms\n";
    o << "\n## 环境\n\n";
    for (const auto& [key, value] : env.object().items) {
        if (value.isArray()) {
            o << "- " << key << "：";
            bool first = true;
            for (const auto& item : value.array()) { o << (first ? "" : "；") << text(item); first = false; }
            o << (first ? "无" : "") << "\n";
        } else o << "- " << key << "：" << text(value) << "\n";
    }
    if (!r["settingsChanges"].array().empty()) {
        o << "\n## 记录期间的设置变化\n\n";
        for (const auto& c : r["settingsChanges"].array())
            o << "- " << mmss(c["tMs"].number()) << " " << c["name"].string() << "：" << text(c["previous"]) << " → " << text(c["value"]) << "\n";
    }
    if (!r["marks"].array().empty()) {
        o << "\n## 用户标记\n\n";
        for (const auto& m : r["marks"].array())
            o << "- #" << n(m["number"], 0) << " " << mmss(m["tMs"].number()) << "：前 5 秒最长间隔 " << n(m["worstIntervalMs5s"], 1) << " ms，卡顿 "
              << n(m["hitches5s"], 0) << " 次" << (m["causeText"].isString() ? "，" + m["causeText"].string() : std::string()) << "\n";
    }
    if (!r["resets"].array().empty()) {
        o << "\n## 重建\n\n";
        for (const auto& e : r["resets"].array())
            o << "- " << mmss(e["tMs"].number()) << " " << e["reason"].string() << " " << n(e["totalMs"], 1) << " ms\n";
    }
    if (!lg["firstErrors"].array().empty()) {
        o << "\n## 日志错误（前几条）\n\n";
        for (const auto& e : lg["firstErrors"].array()) o << "- `" << e.string() << "`\n";
    }
    o << "\n---\n文件：report.json（全部指标）、frames.csv（逐帧）、report.html（图表）、raw/（原始记录、系统采样、日志片段、环境快照）。\n";
    writeText(path, o.str());
}

namespace {
// The page reads window.R (report) and window.F (frames, thinned) and draws
// SVG charts; no external files, works offline.
const char* kPage = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Veyra 性能诊断报告</title>
<style>
:root{--bg:#101218;--card:#181b23;--line:#2a2f3b;--t1:#eef0f5;--t2:#a8afbf;--t3:#6f7787;--acc:#ff8a3d;--red:#ff5d6c;--yel:#f6c453;--grn:#4fd18b;--blu:#5aa9ff;--pur:#b88cff}
@media (prefers-color-scheme:light){:root{--bg:#f5f6f8;--card:#fff;--line:#dfe2e8;--t1:#14161b;--t2:#4a5161;--t3:#8a91a0}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--t1);font:14px/1.5 system-ui,"Segoe UI","Microsoft YaHei",sans-serif}
main{max-width:1180px;margin:0 auto;padding:20px 16px 60px}h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:28px 0 10px;color:var(--t2)}
.sub{color:var(--t2)}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
.k{color:var(--t3);font-size:12px}.v{font-size:20px;font-weight:600}.find{border-left:4px solid var(--t3)}
.find.high{border-left-color:var(--red)}.find.medium{border-left-color:var(--yel)}.find.pass{border-left-color:var(--grn)}.find.info{border-left-color:var(--blu)}
.find b{font-size:15px}.find p{margin:4px 0 0;color:var(--t2)}table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--t3);font-weight:500}
svg{width:100%;height:auto;display:block}.legend span{margin-right:14px;color:var(--t2);font-size:12px}.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;vertical-align:-1px}
.scroll{overflow-x:auto}code{font-size:12px;color:var(--t2)}
</style></head><body><main id="app"></main>
<script>window.R=__REPORT__;window.F=__FRAMES__;</script>
<script>
)HTML" R"HTML((function(){const R=window.R,F=window.F,$=document.getElementById('app');
const esc=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const num=(v,d=1)=>typeof v==='number'&&isFinite(v)?v.toFixed(d):'—';
const mmss=ms=>{if(!(ms>=0))return'—';const s=Math.floor(ms/1000);return String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0')};
const sev={high:'严重',medium:'注意',info:'提示',pass:'通过'};
const S=R.summary||{},O=R.output||{},L=R.latency||{},D=R.display||{},T=R.telemetry||{},E=R.environment||{};
let h='<h1>Veyra 性能诊断报告</h1><div class="sub">'+esc(E.veyraVersion||'')+' · 记录 '+num(S.durationSeconds,0)+' 秒 · 来源 '+esc(S.sourceKind||'—')+' · '+esc(S.headline||'')+'</div>';
h+='<h2>主要发现</h2><div class="grid" style="grid-template-columns:1fr">';
(R.findings||[]).forEach(f=>{h+='<div class="card find '+esc(f.severity)+'"><b>['+esc(f.id)+' · '+(sev[f.severity]||'')+'] '+esc(f.title)+'</b><p>证据：'+esc(f.evidence)+'</p><p>建议：'+esc(f.advice)+'</p></div>'});
if(!(R.findings||[]).length)h+='<div class="card">没有发现。</div>';
)HTML" R"HTML(h+='</div><h2>关键指标</h2><div class="grid">';
const tile=(k,v,u)=>{h+='<div class="card"><div class="k">'+k+'</div><div class="v">'+v+(u?' <span class="k">'+u+'</span>':'')+'</div></div>'};
tile('平均帧率',num(O.averageFps,1),'fps');tile('最慢 1% 平均',num(O.low1FpsSlowestAverage,1),'fps');tile('1% low（P99）',num(O.low1FpsP99,1),'fps');
tile('期望帧率',num(O.expectedFps,1),'fps');tile('卡顿/分钟',num((O.hitches||{}).perMinute,2),'');tile('最长帧间隔',num(S.worstIntervalMs,1),'ms');
tile('到达→送显 P50',num((L.arrivalToPresentMs||{}).p50,1),'ms');tile('到达→送显 P95',num((L.arrivalToPresentMs||{}).p95,1),'ms');
tile('送显调用 P95',num((L.presentCallMs||{}).p95,2),'ms');tile('送显→上屏 P50',num((D.presentToScanoutP50Ms||{}).p50,2),'ms');
tile('系统 CPU 平均',num((T.cpu_total||{}).mean,0),'%');tile('其他进程 GPU P90',num((T.gpu_others_max||{}).p90,0),'%');
h+='</div>';
// --- chart helpers
function chart(w,ht,series,opt){opt=opt||{};const pad={l:44,r:10,t:10,b:24};let xmax=opt.xmax||1,ymax=opt.ymax||1;
 let s='<svg viewBox="0 0 '+w+' '+ht+'"><rect x="0" y="0" width="'+w+'" height="'+ht+'" fill="none"/>';
 const X=x=>pad.l+(w-pad.l-pad.r)*x/xmax,Y=y=>ht-pad.b-(ht-pad.t-pad.b)*Math.min(y,ymax)/ymax;
 for(let i=0;i<=4;i++){const y=ymax*i/4;s+='<line x1="'+pad.l+'" x2="'+(w-pad.r)+'" y1="'+Y(y)+'" y2="'+Y(y)+'" stroke="var(--line)"/><text x="'+(pad.l-6)+'" y="'+(Y(y)+4)+'" fill="var(--t3)" font-size="10" text-anchor="end">'+(ymax>=10?Math.round(y):y.toFixed(1))+'</text>'}
 const step=Math.max(1,Math.ceil(xmax/60/10))*60;for(let x=0;x<=xmax;x+=step){s+='<text x="'+X(x)+'" y="'+(ht-6)+'" fill="var(--t3)" font-size="10" text-anchor="middle">'+mmss(x*1000)+'</text>'}
 (opt.vlines||[]).forEach(v=>{s+='<line x1="'+X(v.x)+'" x2="'+X(v.x)+'" y1="'+pad.t+'" y2="'+(ht-pad.b)+'" stroke="'+v.c+'" stroke-dasharray="3 3"><title>'+esc(v.t)+'</title></line>'});
 series.forEach(se=>{if(se.dots){se.pts.forEach(p=>{s+='<circle cx="'+X(p[0])+'" cy="'+Y(p[1])+'" r="'+(se.r||1.4)+'" fill="'+(p[2]||se.c)+'"/>'})}
  else{let d='';se.pts.forEach((p,i)=>{if(p[1]==null)return;d+=(d?'L':'M')+X(p[0]).toFixed(1)+' '+Y(p[1]).toFixed(1)});s+='<path d="'+d+'" fill="none" stroke="'+se.c+'" stroke-width="'+(se.w||1.5)+'"/>'}});
 return s+'</svg>'}
const P=R.perSecond||[],secs=P.length;const marks=(R.marks||[]).map(m=>({x:m.tMs/1000,c:'var(--blu)',t:'标记 #'+m.number}));
const resets=(R.resets||[]).map(r=>({x:r.tMs/1000,c:'var(--t3)',t:'重建 '+r.reason}));const sets=(R.settingsChanges||[]).map(c=>({x:c.tMs/1000,c:'var(--pur)',t:c.name+' → '+c.value}));
if(secs){let ymax=0;P.forEach(p=>{ymax=Math.max(ymax,p.presents||0)});ymax=Math.ceil(ymax*1.15/10)*10||10;
 h+='<h2>每秒送显帧数与 1% low</h2><div class="card"><div class="legend"><span><i style="background:var(--acc)"></i>每秒帧数</span><span><i style="background:var(--grn)"></i>1% low（P99，10 秒）</span><span><i style="background:var(--yel)"></i>最慢 1% 平均（10 秒）</span><span><i style="background:var(--red)"></i>卡顿</span><span><i style="background:var(--blu)"></i>标记</span><span><i style="background:var(--pur)"></i>设置变化</span><span><i style="background:var(--t3)"></i>重建</span></div>';
 h+=chart(1100,260,[{c:'var(--acc)',pts:P.map(p=>[p.s,p.presents])},{c:'var(--grn)',pts:P.map(p=>[p.s,p.low1P99Fps])},{c:'var(--yel)',pts:P.map(p=>[p.s,p.low1AvgFps])},
  {dots:1,r:3,c:'var(--red)',pts:P.filter(p=>p.hitches>0).map(p=>[p.s,Math.min(p.presents||0,ymax)])}],{xmax:secs,ymax:ymax,vlines:marks.concat(resets,sets)})+'</div>'}
if(F&&F.t&&F.t.length){const tgt=O.expectedIntervalMs||16.7;const ymax=Math.max(tgt*4,10);const pts=[];
 for(let i=0;i<F.t.length;i++){const k=F.k[i];pts.push([F.t[i]/1000,F.d[i],k&4?'var(--red)':k&2?'var(--pur)':'var(--acc)'])}
 h+='<h2>逐帧间隔（ms）</h2><div class="card"><div class="legend"><span><i style="background:var(--acc)"></i>真实帧</span><span><i style="background:var(--pur)"></i>生成帧</span><span><i style="background:var(--red)"></i>卡顿</span><span>期望 '+num(tgt,2)+' ms</span>'+(F.thinned?'<span>（已抽样显示 '+F.t.length+' 帧）</span>':'')+'</div>';
 h+=chart(1100,300,[{dots:1,pts:pts}],{xmax:Math.max(1,secs||F.t[F.t.length-1]/1000),ymax:ymax,vlines:marks})+'</div>'}
if(secs&&P.some(p=>p.cpu_total!=null||p.gpu_busy_max!=null)){
 h+='<h2>系统采样（%）</h2><div class="card"><div class="legend"><span><i style="background:var(--acc)"></i>系统 CPU</span><span><i style="background:var(--grn)"></i>Veyra GPU</span><span><i style="background:var(--red)"></i>其他进程 GPU</span><span><i style="background:var(--blu)"></i>GPU 最忙引擎</span></div>';
 h+=chart(1100,200,[{c:'var(--acc)',pts:P.map(p=>[p.s,p.cpu_total])},{c:'var(--grn)',pts:P.map(p=>[p.s,p.gpu_veyra_max])},{c:'var(--red)',pts:P.map(p=>[p.s,p.gpu_others_max])},{c:'var(--blu)',pts:P.map(p=>[p.s,p.gpu_busy_max]),w:1}],{xmax:secs,ymax:100,vlines:marks})+'</div>'}
h+='<h2>卡顿事件（按严重程度）</h2><div class="card scroll"><table><tr><th>时间</th><th>最长间隔</th><th>次数</th><th>额外时间</th><th>归因</th><th>阶段</th><th>标记</th></tr>';
(R.events||[]).slice(0,40).forEach(e=>{h+='<tr><td>'+mmss(e.startMs)+'</td><td>'+num(e.worstIntervalMs,1)+' ms</td><td>'+e.hitches+'</td><td>'+num(e.extraMs,1)+' ms</td><td>'+esc(e.causeText)+'</td><td>'+esc(e.stage||'')+'</td><td>'+esc((e.marks||[]).join(','))+'</td></tr>'});
h+='</table></div>';
)HTML" R"HTML(const distTable=(title,obj)=>{const keys=Object.keys(obj||{});if(!keys.length)return;keys.sort((a,b)=>((obj[b]||{}).p95||0)-((obj[a]||{}).p95||0));
 h+='<h2>'+title+'</h2><div class="card scroll"><table><tr><th>阶段</th><th>次数</th><th>P50</th><th>P95</th><th>P99</th><th>最大</th></tr>';
 keys.forEach(k=>{const d=obj[k];h+='<tr><td>'+esc(k)+'</td><td>'+d.n+'</td><td>'+num(d.p50,3)+'</td><td>'+num(d.p95,3)+'</td><td>'+num(d.p99,3)+'</td><td>'+num(d.max,2)+'</td></tr>'});h+='</table></div>'};
distTable('GPU 阶段耗时（ms）',(R.stages||{}).gpuMs);distTable('CPU 阶段耗时（ms）',(R.stages||{}).cpuMs);
const kv=(title,obj)=>{h+='<h2>'+title+'</h2><div class="card scroll"><table>';Object.keys(obj||{}).forEach(k=>{let v=obj[k];if(Array.isArray(v))v=v.join('；');else if(v&&typeof v==='object')v=JSON.stringify(v);h+='<tr><th style="width:240px">'+esc(k)+'</th><td>'+esc(v)+'</td></tr>'});h+='</table></div>'};
kv('环境',E);
if((R.settingsChanges||[]).length){h+='<h2>记录期间的设置变化</h2><div class="card scroll"><table><tr><th>时间</th><th>设置</th><th>之前</th><th>之后</th></tr>';R.settingsChanges.forEach(c=>{h+='<tr><td>'+mmss(c.tMs)+'</td><td>'+esc(c.name)+'</td><td>'+esc(c.previous)+'</td><td>'+esc(c.value)+'</td></tr>'});h+='</table></div>'}
kv('补帧',R.frameGeneration);kv('输入',R.source);kv('显示端（DXGI 统计）',R.display);
h+='<h2>说明</h2><div class="card sub">帧间隔取每次 Present 调用的时刻（与 RTSS/PresentMon 相同的软件送显口径），不是屏幕实际显示时刻；“送显→上屏”来自 DXGI 帧统计，是近似值。卡顿：帧间隔超过期望间隔 1.5 倍且多 4 ms 以上；归因比较这一帧在“到达→处理→GPU 就绪→送显”各段比平时多花的时间。全部数据见 report.json 与 frames.csv。</div>';
$.innerHTML=h;})();
</script></body></html>
)HTML";
}

void writeHtml(const Analysis& a, const fs::path& path) {
    // Thin the scatter to at most ~40k points; every hitch is kept.
    const size_t limit = 40000;
    const size_t stride = a.frames.size() > limit ? (a.frames.size() + limit - 1) / limit : 1;
    std::string t, d, k;
    t.reserve(a.frames.size() * 8 / stride + 64);
    size_t kept = 0;
    char b[48];
    for (size_t i = 0; i < a.frames.size(); ++i) {
        const auto& f = a.frames[i];
        if (f.dt <= 0) continue;
        if (!f.hitch && i % stride) continue;
        if (kept++) { t += ','; d += ','; k += ','; }
        std::snprintf(b, sizeof(b), "%.1f", f.t); t += b;
        std::snprintf(b, sizeof(b), "%.2f", f.dt); d += b;
        std::snprintf(b, sizeof(b), "%d", (f.kind == 2 ? 2 : 0) | (f.hitch ? 4 : 0)); k += b;
    }
    const std::string frames = "{\"thinned\":" + std::string(stride > 1 ? "true" : "false") + ",\"t\":[" + t + "],\"d\":[" + d + "],\"k\":[" + k + "]}";
    // Keep "</script>" out of the embedded JSON.
    auto report = json::dump(a.report, -1);
    for (size_t at = report.find("</"); at != std::string::npos; at = report.find("</", at + 3)) report.replace(at, 2, "<\\/");
    std::string page(kPage);
    page.replace(page.find("__REPORT__"), 10, report);
    page.replace(page.find("__FRAMES__"), 10, frames);
    writeText(path, page);
}

void writeCompare(const Value& c, const fs::path& outDir) {
    std::ostringstream o;
    o << "# Veyra 性能诊断对比\n\n";
    o << "- A：" << text(c["a"]["folder"]) << "（" << text(c["a"]["version"]) << "）  " << text(c["a"]["summary"]["headline"]) << "\n";
    o << "- B：" << text(c["b"]["folder"]) << "（" << text(c["b"]["version"]) << "）  " << text(c["b"]["summary"]["headline"]) << "\n\n";
    o << "## 指标（B 相对 A）\n\n| 指标 | A | B | 变化 | 结论 |\n|---|---|---|---|---|\n";
    for (const auto& m : c["metrics"].array()) {
        const std::string verdict = m["verdict"].string();
        o << "| " << m["label"].string() << " | " << text(m["a"]) << " | " << text(m["b"]) << " | "
          << (m["deltaPercent"].isNumber() ? f1(m["deltaPercent"].number(), 1) + "%" : std::string("—")) << " | "
          << (verdict == "b-better" ? "B 更好" : verdict == "b-worse" ? "B 更差" : verdict == "same" ? "相同" : "—") << " |\n";
    }
    const auto list = [&](const char* title, const Value& rows) {
        o << "\n## " << title << "\n\n";
        if (rows.array().empty()) { o << "（无差异）\n"; return; }
        o << "| 名称 | A | B |\n|---|---|---|\n";
        for (const auto& r : rows.array()) o << "| " << r["name"].string() << " | " << text(r["a"]) << " | " << text(r["b"]) << " |\n";
    };
    list("界面设置差异", c["interfaceDifferences"]);
    list("偏好设置差异", c["preferenceDifferences"]);
    list("环境差异", c["environmentDifferences"]);
    o << "\n## 主要发现\n\nA：\n";
    for (const auto& f : c["findingsA"].array()) o << "- " << f.string() << "\n";
    o << "\nB：\n";
    for (const auto& f : c["findingsB"].array()) o << "- " << f.string() << "\n";
    writeText(outDir / "COMPARE.md", o.str());
    // A plain page with the same tables.
    std::string md = o.str(), html = "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><title>Veyra 诊断对比</title>"
        "<style>body{font:14px/1.5 system-ui,'Microsoft YaHei',sans-serif;max-width:1100px;margin:20px auto;padding:0 16px;background:#101218;color:#eef0f5}"
        "@media (prefers-color-scheme:light){body{background:#f5f6f8;color:#14161b}}table{border-collapse:collapse;width:100%}td,th{border-bottom:1px solid #2a2f3b;padding:5px 8px;text-align:left}</style>"
        "</head><body><pre style=\"white-space:pre-wrap;font:inherit\">";
    for (const char ch : md) {
        switch (ch) { case '<': html += "&lt;"; break; case '>': html += "&gt;"; break; case '&': html += "&amp;"; break; default: html += ch; }
    }
    html += "</pre></body></html>";
    writeText(outDir / "compare.html", html);
}

}
