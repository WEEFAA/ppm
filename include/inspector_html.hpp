// ===========================================================================
//  inspector_html.hpp -- the frame inspector, embedded.
//
//  A single self-contained HTML file written into the output directory. Embedded
//  in the binary rather than read from the repo so the CLIs keep working from any
//  working directory and from an installed location.
//
//  Layout, fixed so that the eye does not have to re-find anything when the
//  content changes:
//
//    +----------+---------------------------------+----------+
//    |  BENCH   |          SHOW PLANE             |  TOOLS   |
//    |  240px   |   frame + projected landmarks   |  320px   |
//    |  fixed   |                                 | collaps. |
//    +----------+---------------------------------+----------+
//    |            NAVIGATION  (scrub, 68px fixed)            |
//    +-------------------------------------------------------+
//
//  No dependencies, no build step, no network. Opens over file:// -- with one
//  caveat handled in the page itself: browsers block fetch() on file:// URLs, so
//  the loader falls back to asking the user to serve the directory, and says how.
// ===========================================================================
#ifndef PPM_INSPECTOR_HTML_HPP
#define PPM_INSPECTOR_HTML_HPP

#include <cstdio>
#include <cstring>
#include <string>

namespace ppm {

inline const char *inspector_html() {
    return R"INSPECTOR(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Frame Inspector</title>
<style>
:root{
  --bench-w:240px; --tools-w:320px; --nav-h:68px;
  --bg:#0e0f12; --panel:#16181d; --line:#262a32; --text:#e6e8ec;
  --dim:#8b929f; --accent:#5ac8fa; --warn:#ffb340; --bad:#ff6b6b; --ok:#4ade80;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  --sans:system-ui,-apple-system,"Segoe UI",sans-serif;
}
@media (prefers-color-scheme:light){
  :root{--bg:#f5f6f8;--panel:#fff;--line:#dfe3ea;--text:#1a1d23;--dim:#5c6472;}
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{background:var(--bg);color:var(--text);font:13px/1.45 var(--sans);overflow:hidden}

.app{display:grid;height:100vh;
  grid-template-columns:var(--bench-w) 1fr var(--tools-w);
  grid-template-rows:1fr var(--nav-h);
  grid-template-areas:"bench plane tools" "nav nav nav";}
.app.collapsed{grid-template-columns:var(--bench-w) 1fr 0}

/* ---------- bench ---------- */
.bench{grid-area:bench;background:var(--panel);border-right:1px solid var(--line);
  display:flex;flex-direction:column;overflow:hidden}
.bench h1{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--dim);
  padding:14px 14px 8px}
.summary{padding:0 14px 12px;border-bottom:1px solid var(--line)}
.kv{display:flex;justify-content:space-between;gap:8px;padding:2px 0;font-size:12px}
.kv span:first-child{color:var(--dim)}
.kv span:last-child{font-family:var(--mono)}
.shots{overflow-y:auto;flex:1;padding:8px 0}
.shot{padding:6px 14px;cursor:pointer;border-left:3px solid transparent;font-size:12px}
.shot:hover{background:rgba(127,127,127,.08)}
.shot.active{border-left-color:var(--accent);background:rgba(90,200,250,.10)}
.shot b{font-weight:600}
.shot i{color:var(--dim);font-style:normal;font-family:var(--mono);font-size:11px}

/* ---------- show plane ---------- */
.plane{grid-area:plane;position:relative;display:flex;align-items:center;
  justify-content:center;overflow:hidden;background:
  repeating-conic-gradient(#00000010 0% 25%,transparent 0% 50%) 50%/22px 22px}
.stage{position:relative;line-height:0;max-width:100%;max-height:100%}
.stage img{display:block;max-width:100%;max-height:calc(100vh - var(--nav-h) - 32px);
  image-rendering:auto}
.stage canvas{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
.hit{position:absolute;inset:0}
.badge{position:absolute;top:12px;left:12px;display:flex;gap:6px;flex-wrap:wrap;z-index:3}
.tag{font:11px/1 var(--mono);padding:4px 7px;border-radius:4px;
  background:#000000aa;color:#fff;backdrop-filter:blur(4px)}
.tag.warn{background:#7a4a00cc} .tag.bad{background:#7a1f1fcc} .tag.ok{background:#14532dcc}
.tip{position:absolute;z-index:5;max-width:250px;padding:7px 9px;border-radius:6px;
  background:#000000e8;color:#fff;font-size:11.5px;pointer-events:none;opacity:0;
  transition:opacity .12s;box-shadow:0 4px 18px #0008}
.tip.on{opacity:1}
.tip b{color:var(--accent)}
.empty{color:var(--dim);text-align:center;padding:40px;max-width:520px;font-size:13px}
.empty code{font-family:var(--mono);background:#7f7f7f22;padding:2px 5px;border-radius:3px}

/* ---------- tools ---------- */
.tools{grid-area:tools;background:var(--panel);border-left:1px solid var(--line);
  overflow-y:auto;overflow-x:hidden}
.app.collapsed .tools{display:none}
.grp{border-bottom:1px solid var(--line)}
.grp>summary{padding:10px 14px;cursor:pointer;font-size:11px;letter-spacing:.08em;
  text-transform:uppercase;color:var(--dim);list-style:none}
.grp>summary::-webkit-details-marker{display:none}
.grp>summary::before{content:"▸ ";color:var(--dim)}
.grp[open]>summary::before{content:"▾ "}
.grp .body{padding:0 14px 12px}
.sw{display:flex;gap:4px;flex-wrap:wrap;margin:4px 0}
.sw div{width:26px;height:26px;border-radius:4px;border:1px solid var(--line)}
.bar{height:5px;background:#7f7f7f26;border-radius:3px;overflow:hidden;margin:3px 0 7px}
.bar i{display:block;height:100%;background:var(--accent)}
.note{font-size:11.5px;color:var(--warn);padding:4px 0 0}
.chips{display:flex;gap:4px;flex-wrap:wrap;padding:5px 0 2px}
.chips i{font:10.5px var(--mono);font-style:normal;padding:2px 5px;border-radius:3px;
  background:#7f7f7f26;color:var(--dim)}
.src{font-size:10.5px;letter-spacing:.03em;text-transform:uppercase;padding:5px 0 0}
.src.ok{color:var(--ok)} .src.warn{color:var(--warn)}
.err{font-family:var(--mono)} .err.ok{color:var(--ok)} .err.warn{color:var(--warn)}
.toggles{display:flex;flex-direction:column;gap:5px}
label.tg{display:flex;align-items:center;gap:7px;font-size:12px;cursor:pointer}
.hist{display:flex;align-items:flex-end;gap:1px;height:44px;margin:4px 0}
.hist i{flex:1;background:var(--dim);min-height:1px}

/* ---------- navigation ---------- */
.nav{grid-area:nav;background:var(--panel);border-top:1px solid var(--line);
  display:flex;align-items:center;gap:12px;padding:0 14px}
.nav button{background:#7f7f7f1f;color:var(--text);border:1px solid var(--line);
  border-radius:6px;height:30px;min-width:34px;cursor:pointer;font:12px var(--mono)}
.nav button:hover{background:#7f7f7f33}
.nav input[type=range]{flex:1;accent-color:var(--accent)}
.tc{font:12px var(--mono);color:var(--dim);min-width:150px;text-align:right}
.marks{position:relative;flex:1;height:30px;display:flex;align-items:center}
.marks input{position:absolute;inset:0;width:100%;margin:0}
.marks .m{position:absolute;top:0;width:2px;height:7px;background:var(--warn);border-radius:1px}
.marks .k{position:absolute;bottom:0;width:2px;height:5px;background:var(--ok);opacity:.7}
</style>
</head>
<body>
<div class="app" id="app">
  <aside class="bench">
    <h1>Bench</h1>
    <div class="summary" id="summary"></div>
    <div class="shots" id="shots"></div>
  </aside>

  <main class="plane" id="plane">
    <div class="stage" id="stage" hidden>
      <img id="frame" alt="frame">
      <canvas id="ov"></canvas>
      <div class="hit" id="hit"></div>
      <div class="badge" id="badge"></div>
    </div>
    <div class="empty" id="empty">Loading&hellip;</div>
    <div class="tip" id="tip"></div>
  </main>

  <aside class="tools" id="tools"></aside>

  <div class="nav">
    <button id="play" title="Play / pause (space)">&#9654;</button>
    <button id="prev" title="Previous frame (&larr;)">&#8249;</button>
    <button id="next" title="Next frame (&rarr;)">&#8250;</button>
    <div class="marks" id="marks"><input type="range" id="scrub" min="0" max="0" value="0"></div>
    <div class="tc" id="tc">--</div>
    <button id="tgTools" title="Toggle tools panel (t)">&#9707;</button>
  </div>
</div>

<script>
"use strict";
const $ = id => document.getElementById(id);
const state = { md:null, params:null, i:0, playing:false, timer:null,
                show:{thirds:true, centroid:true, horizon:true, motion:true} };

/* ---------- loading ---------- */
// fetch() is blocked on file:// in every current browser, so failure here is
// expected rather than exceptional. Say what to do about it instead of erroring.
async function load(){
  try{
    const [md, params] = await Promise.all([
      fetch("../metadata.json").then(r=>r.json()),
      fetch("../parameters.json").then(r=>r.json()).catch(()=>null)
    ]);
    state.md = md; state.params = params;
    boot();
  }catch(e){
    $("empty").innerHTML =
      "<p><b>Cannot read metadata.json.</b></p><p style='margin-top:10px'>"+
      "Browsers block file access from <code>file://</code> pages. Serve this "+
      "directory over HTTP instead:</p><p style='margin-top:10px'>"+
      "<code>cd "+location.pathname.replace(/\/inspect\/[^/]*$/,"")+
      " &amp;&amp; python3 -m http.server</code></p>"+
      "<p style='margin-top:10px'>then open <code>localhost:8000/inspect/</code>.</p>";
  }
}

function boot(){
  const n = state.md.frames.length;
  $("scrub").max = String(Math.max(0, n-1));
  $("stage").hidden = false; $("empty").hidden = true;
  buildSummary(); buildShots(); buildMarks();
  go(0);
}

/* ---------- bench ---------- */
function buildSummary(){
  const s = state.md.summary, r = s.review||{};
  const rows = [
    ["frames", s.frames], ["fps", fmt(s.fps,3)],
    ["duration", fmt(s.duration_seconds,2)+"s"],
    ["shots", s.shots], ["exposure", s.step_label],
    ["effective", fmt(s.effective_fps,1)+" fps"],
    ["holds", pct(s.hold_fraction)], ["keys", s.key_candidates],
    ["loops", s.loops ? "yes" : "no"]
  ];
  let h = rows.map(([k,v])=>`<div class="kv"><span>${k}</span><span>${v}</span></div>`).join("");

  // Exposure detail. The top-level step_label above is the conclusion; this is
  // the evidence, and whether it can be trusted. A reading taken from the
  // container is a measurement; one inferred from pixel differences is a guess
  // about something the decode may already have flattened away, so the two are
  // labelled differently rather than presented as equivalent.
  const e = s.exposure || {};
  if(e.available){
    h += `<div class="kv"><span>tick base</span><span>${fmt(e.tick_fps,0)} fps</span></div>`;
    if(Array.isArray(e.distribution) && e.distribution.length){
      h += `<div class="chips">`+e.distribution.map(d=>
        `<i title="${d.frames} frames ${d.label}">${d.frames}&times;${d.ticks}</i>`).join("")+`</div>`;
    }
    h += `<div class="src ok">exposure read from container</div>`;
    const pi = e.pixel_inference;
    if(pi && pi.agrees === false)
      h += `<div class="note">pixel inference disagrees (says ${step(pi.step)})</div>`;
  } else {
    h += `<div class="src warn">exposure inferred from pixels</div>`;
    if(e.reason) h += `<div class="note">${e.reason}</div>`;
  }

  const flags=[];
  if((r.frames_with_neutral_shadows||0)>0.5) flags.push("neutral shadows");
  if((r.frames_murky||0)>0.5) flags.push("murky palette");
  if((r.frames_clipped||0)>0.25) flags.push("clipped highlights");
  if(flags.length) h += `<div class="note">${flags.join(" · ")}</div>`;
  $("summary").innerHTML = h;
}

const step = n => n===1?"on ones":n===2?"on twos":n===3?"on threes":"on "+n+"s";

function buildShots(){
  // Group frames by shot so the bench is a shot list, which is how a sequence is
  // actually navigated, rather than a flat list of hundreds of frames.
  const byShot = new Map();
  state.md.frames.forEach((f,i)=>{
    const s = f.shot ? f.shot.index : 0;
    if(!byShot.has(s)) byShot.set(s, {first:i, count:0});
    byShot.get(s).count++;
  });
  $("shots").innerHTML = [...byShot.entries()].map(([s,v])=>
    `<div class="shot" data-i="${v.first}" data-shot="${s}">
       <b>Shot ${s+1}</b> <i>${v.count}f @${v.first}</i></div>`).join("");
  $("shots").onclick = e => {
    const el = e.target.closest(".shot");
    if(el) go(+el.dataset.i);
  };
}

function buildMarks(){
  // Transition and key-frame ticks along the scrubber, so structure is visible
  // without scrubbing to find it.
  const n = state.md.frames.length; if(n<2) return;
  const box = $("marks");
  [...box.querySelectorAll(".m,.k")].forEach(e=>e.remove());
  const tr = (state.params && state.params.analysis && state.params.analysis.transitions) || [];
  tr.forEach(t=>{
    const d = document.createElement("div");
    d.className = "m"; d.style.left = (100*t.frame/(n-1))+"%";
    d.title = t.kind+" @"+t.frame; box.appendChild(d);
  });
  state.md.frames.forEach((f,i)=>{
    if(!(f.timing && f.timing.key_candidate)) return;
    const d = document.createElement("div");
    d.className = "k"; d.style.left = (100*i/(n-1))+"%";
    box.appendChild(d);
  });
}

/* ---------- show plane ---------- */
function go(i){
  const n = state.md.frames.length;
  state.i = Math.max(0, Math.min(n-1, i));
  $("scrub").value = String(state.i);
  $("frame").src = "frames/f"+String(state.i).padStart(4,"0")+".jpg";
  const f = state.md.frames[state.i];
  $("tc").textContent = f.timecode+"  ·  "+state.i+"/"+(n-1);
  [...document.querySelectorAll(".shot")].forEach(el=>
    el.classList.toggle("active", +el.dataset.shot === (f.shot?f.shot.index:0)));
  buildBadges(f); buildTools(f); draw(f);
}

function buildBadges(f){
  const t=[], add=(s,c)=>t.push(`<span class="tag ${c||""}">${s}</span>`);
  if(f.timing){
    if(f.timing.hold) add("HOLD");
    if(f.timing.key_candidate) add("KEY","ok");
    if(f.timing.smear) add("SMEAR","warn");
  }
  if(f.camera && f.camera.move && f.camera.move!=="static")
    add(f.camera.move.replace(/_/g," "));
  if(f.exposure){
    add(f.exposure.key.replace("_"," "));
    (f.exposure.flags||[]).forEach(x=>add(x.replace(/_/g," "), "bad"));
  }
  if(f.color){
    add(f.color.harmony);
    if(f.color.murky) add("murky","warn");
    if(f.color.shadow_chroma && f.color.shadow_chroma.verdict==="neutral")
      add("neutral shadows","warn");
  }
  $("badge").innerHTML = t.join("");
}

function draw(f){
  const img=$("frame"), c=$("ov"), g=c.getContext("2d");
  const w=img.clientWidth||img.naturalWidth, h=img.clientHeight||img.naturalHeight;
  if(!w||!h) { img.onload = ()=>draw(f); return; }
  c.width=w; c.height=h; g.clearRect(0,0,w,h);
  g.lineWidth=1;

  if(state.show.thirds){
    g.strokeStyle="rgba(255,255,255,.16)";
    for(let k=1;k<3;k++){
      g.beginPath(); g.moveTo(w*k/3,0); g.lineTo(w*k/3,h); g.stroke();
      g.beginPath(); g.moveTo(0,h*k/3); g.lineTo(w,h*k/3); g.stroke();
    }
  }
  (f.landmarks||[]).forEach(L=>{
    if(L.id==="centroid" && !state.show.centroid) return;
    if(L.id==="horizon"  && !state.show.horizon) return;
    if(L.id==="motion"   && !state.show.motion) return;
    if(L.kind==="hline"){
      g.strokeStyle="#5ac8fa"; g.setLineDash([7,5]);
      g.beginPath(); g.moveTo(0,L.y*h); g.lineTo(w,L.y*h); g.stroke();
      g.setLineDash([]); label(g,L.label,8,L.y*h-6);
    } else if(L.kind==="point"){
      const x=L.x*w, y=L.y*h;
      g.strokeStyle="#ffb340"; g.beginPath(); g.arc(x,y,7,0,7); g.stroke();
      g.beginPath(); g.moveTo(x-11,y); g.lineTo(x+11,y);
      g.moveTo(x,y-11); g.lineTo(x,y+11); g.stroke();
      label(g,L.label,x+13,y-8);
    } else if(L.kind==="vector"){
      const x=L.x*w, y=L.y*h, ex=x+(L.dx||0)*w, ey=y+(L.dy||0)*h;
      g.strokeStyle="#4ade80"; g.lineWidth=2;
      g.beginPath(); g.moveTo(x,y); g.lineTo(ex,ey); g.stroke();
      const a=Math.atan2(ey-y,ex-x);
      g.beginPath(); g.moveTo(ex,ey);
      g.lineTo(ex-9*Math.cos(a-0.4), ey-9*Math.sin(a-0.4));
      g.lineTo(ex-9*Math.cos(a+0.4), ey-9*Math.sin(a+0.4));
      g.closePath(); g.fillStyle="#4ade80"; g.fill();
      g.lineWidth=1; label(g,L.label,ex+8,ey-6);
    }
  });
}
function label(g,t,x,y){
  g.font="11px ui-monospace,monospace";
  const wd=g.measureText(t).width;
  g.fillStyle="rgba(0,0,0,.65)"; g.fillRect(x-3,y-11,wd+6,15);
  g.fillStyle="#fff"; g.fillText(t,x,y);
}

/* landmark tooltips: hit-test in normalised space against the displayed size */
$("hit").addEventListener("mousemove", e=>{
  const f=state.md&&state.md.frames[state.i]; if(!f) return;
  const r=e.currentTarget.getBoundingClientRect();
  const nx=(e.clientX-r.left)/r.width, ny=(e.clientY-r.top)/r.height;
  let hit=null;
  (f.landmarks||[]).forEach(L=>{
    if(L.kind==="hline"){ if(Math.abs(ny-L.y)<0.02) hit=L; }
    else if(Math.hypot(nx-(L.x||0), ny-(L.y||0))<0.035) hit=L;
  });
  const tip=$("tip");
  if(hit){
    tip.innerHTML="<b>"+hit.label+"</b><br>"+(hit.tooltip||"");
    tip.classList.add("on");
    tip.style.left=(e.clientX+14)+"px"; tip.style.top=(e.clientY+14)+"px";
  } else tip.classList.remove("on");
});
$("hit").addEventListener("mouseleave", ()=>$("tip").classList.remove("on"));

/* ---------- tools ---------- */
function buildTools(f){
  const open = new Set([...document.querySelectorAll(".grp[open]")].map(d=>d.dataset.k));
  const g=(k,title,body)=>`<details class="grp" data-k="${k}" ${open.has(k)||open.size===0?"open":""}>
    <summary>${title}</summary><div class="body">${body}</div></details>`;
  const kv=o=>Object.entries(o).map(([k,v])=>
    `<div class="kv"><span>${k}</span><span>${v}</span></div>`).join("");

  let h = "";

  // Fidelity first. When a render looks soft, achieved_error is the number that
  // answers it, so it should not be buried below the per-frame groups. Sequence-
  // level, so it does not change as you scrub -- that is the point.
  const fd = (state.params && state.params.fidelity) || null;
  if(fd){
    const err = fd.achieved_error, target = fd.target_error;
    const met = !(target > 0) || err <= target * 1.05;
    const grid = Array.isArray(fd.essence) ? fd.essence.join("×") : "--";
    const at   = Array.isArray(fd.measured_at) ? fd.measured_at.join("×") : "--";
    let body = `<div class="kv"><span>preset</span><span>${fd.preset||"--"}</span></div>`
      + `<div class="kv"><span>essence grid</span><span>${grid}</span></div>`
      + `<div class="kv"><span>error</span><span class="err ${met?"ok":"warn"}">`
      + `${fmt(err,4)}${target>0?" / "+fmt(target,3):""}</span></div>`
      + `<div class="kv"><span>measured at</span><span>${at}</span></div>`;
    if(!met) body += `<div class="note">target not reached; the source width is the limit</div>`;
    if(fd.measured_at_native === false)
      body += `<div class="note">measured below source resolution, so the error is a lower bound</div>`;

    // Container claims vs what we actually decoded. Shown only when they differ,
    // because a disagreement means one of them is wrong and it matters which.
    const sr = state.params.source_report;
    if(sr && sr.claims_disagree){
      body += `<div class="note">container claims ${sr.claimed.frames} frames, `
            + `decoded ${sr.measured.frames}</div>`;
    }
    h += g("fidelity","Fidelity", body);
  }

  h += g("overlays","Overlays", `<div class="toggles">`+
    Object.keys(state.show).map(k=>
      `<label class="tg"><input type="checkbox" data-show="${k}" ${state.show[k]?"checked":""}> ${k}</label>`
    ).join("")+`</div>`);

  if(f.timing) h += g("timing","Timing", kv({
    hold:f.timing.hold, key:f.timing.key_candidate, smear:f.timing.smear,
    exposure:f.timing.step_label, difference:fmt(f.timing.difference,5)}));

  if(f.camera) h += g("camera","Camera", kv({
    move:f.camera.move, confidence:fmt(f.camera.confidence,3),
    dx:fmt(f.camera.content_dx,5), dy:fmt(f.camera.content_dy,5),
    divergence:fmt(f.camera.divergence,5)}));

  if(f.exposure){
    const e=f.exposure;
    h += g("exposure","Exposure", kv({
      key:e.key, contrast:fmt(e.contrast,3),
      ratio:e.contrast_ratio_valid?fmt(e.contrast_ratio,1)+":1":"n/a (black floor)",
      clipped:pct(e.clipped_highlights), crushed:pct(e.crushed_blacks),
      range:fmt(e.dynamic_range,3)}) + histogram());
  }

  if(f.color){
    const c=f.color, sc=c.shadow_chroma||{};
    h += g("color","Colour", swatches() + kv({
      harmony:c.harmony, hue:c.dominant_hue_name+" ("+fmt(c.dominant_hue,0)+"°)",
      spread:fmt(c.hue_spread,3), temperature:c.temperature,
      saturation:fmt(c.saturation,3), separation:fmt(c.palette_separation,3),
      "shadow chroma":sc.verdict+" · "+sc.hue_name+" · "+fmt(sc.saturation,3),
      "aerial perspective":c.aerial_perspective+" ("+fmt(c.air,3)+")"}) +
      (c.notes||[]).map(n=>`<div class="note">${n}</div>`).join(""));
  }

  if(f.composition){
    const p=f.composition;
    h += g("comp","Composition", kv({
      centroid:fmt(p.centroid[0],3)+", "+fmt(p.centroid[1],3),
      "thirds affinity":fmt(p.thirds_affinity,3),
      horizon:p.has_horizon?fmt(p.horizon_y,3):"none",
      notan:p.notan, "dark mass":pct(p.dark_mass),
      vignette:fmt(p.vignette,3)}) +
      `<div class="bar"><i style="width:${100*(p.thirds_affinity||0)}%"></i></div>`);
  }

  if(f.shot) h += g("shot","Shot", kv({
    index:f.shot.index+1, "frame in shot":f.shot.frame_in_shot,
    length:f.shot.length, position:pct(f.shot.position)}));

  $("tools").innerHTML = h;
  $("tools").querySelectorAll("[data-show]").forEach(cb=>{
    cb.onchange = () => { state.show[cb.dataset.show]=cb.checked;
                          draw(state.md.frames[state.i]); };
  });
}

function swatches(){
  const a=state.params&&state.params.analysis&&state.params.analysis.frames;
  const fr=a&&a[state.i]; const pal=fr&&fr.color&&fr.color.palette;
  if(!pal||!pal.length) return "";
  return `<div class="sw">`+pal.map(p=>{
    const [r,g,b]=p.rgb.map(v=>Math.round(255*v));
    return `<div style="background:rgb(${r},${g},${b})" title="rgb(${r},${g},${b}) · ${pct(p.weight)}"></div>`;
  }).join("")+`</div>`;
}

function histogram(){
  const a=state.params&&state.params.analysis&&state.params.analysis.frames;
  const fr=a&&a[state.i]; const hs=fr&&fr.luma_histogram;
  if(!hs||!hs.length) return "";
  const mx=Math.max(...hs)||1;
  return `<div class="hist">`+hs.map(v=>
    `<i style="height:${Math.max(1,100*v/mx)}%"></i>`).join("")+`</div>`;
}

/* ---------- navigation ---------- */
const fmt=(v,d)=> (v===undefined||v===null||isNaN(v)) ? "--" : Number(v).toFixed(d);
const pct=v=> (v===undefined||v===null||isNaN(v)) ? "--" : (100*v).toFixed(1)+"%";

$("scrub").oninput = e => go(+e.target.value);
$("prev").onclick = ()=>go(state.i-1);
$("next").onclick = ()=>go(state.i+1);
$("play").onclick = togglePlay;
$("tgTools").onclick = ()=>$("app").classList.toggle("collapsed");

function togglePlay(){
  state.playing = !state.playing;
  $("play").innerHTML = state.playing ? "&#10073;&#10073;" : "&#9654;";
  clearInterval(state.timer);
  if(state.playing){
    const fps = (state.md.summary.fps)||24;
    state.timer = setInterval(()=>{
      const n=state.md.frames.length;
      go(state.i+1 >= n ? 0 : state.i+1);   // loop, so a looping clip reads as one
    }, 1000/Math.max(1,Math.min(60,fps)));
  }
}

addEventListener("keydown", e=>{
  if(e.key===" "){ e.preventDefault(); togglePlay(); }
  else if(e.key==="ArrowRight"||e.key===".") go(state.i+1);
  else if(e.key==="ArrowLeft"||e.key===",") go(state.i-1);
  else if(e.key==="Home") go(0);
  else if(e.key==="End") go(state.md.frames.length-1);
  else if(e.key==="t") $("app").classList.toggle("collapsed");
});
addEventListener("resize", ()=>{ if(state.md) draw(state.md.frames[state.i]); });

load();
</script>
</body>
</html>
)INSPECTOR";
}

/// Write the inspector page into `path`.
inline bool write_inspector_html(const std::string &path) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const char *html = inspector_html();
    const size_t n = strlen(html);
    const bool ok = fwrite(html, 1, n, f) == n;
    fclose(f);
    return ok;
}

} // namespace ppm

#endif // PPM_INSPECTOR_HTML_HPP
