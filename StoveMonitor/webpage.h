#pragma once
// Web interface served by the stove monitor. Fully self-contained (no internet
// needed), so it works on the hotspot during a power cut.

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Stove monitor</title>
<style>
:root{--bg:#e6e9eb;--surface:#fff;--ink:#192026;--muted:#56616a;--line:#c9d0d5;
--cold:#1f5fc4;--good:#1b8547;--high:#f2c318;--hot:#cc2522;--none:#7b848b;
--s-line:#d9601a;--f-line:#5b3fc9;--i-line:#7b848b;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bg:#11161a;--surface:#1a2026;--ink:#e4e8eb;--muted:#9aa5ad;--line:#2c353c;
--s-line:#ff8a3d;--f-line:#a08cff;--i-line:#8b959c;color-scheme:dark}}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.num{font-family:Bahnschrift,"DIN Alternate","DIN Condensed","Roboto Condensed","Arial Narrow",sans-serif;font-variant-numeric:tabular-nums}
header{max-width:980px;margin:auto;padding:1rem 1rem .25rem;display:flex;flex-wrap:wrap;gap:.25rem 1rem;align-items:baseline;justify-content:space-between}
h1{margin:0;font-size:1.3rem;font-weight:700}
#clock{color:var(--muted)}
nav{max-width:980px;margin:auto;padding:0 1rem;display:flex;gap:.25rem;border-bottom:1px solid var(--line);overflow-x:auto}
nav button{font:inherit;background:none;border:0;border-bottom:3px solid transparent;padding:.65rem .9rem;color:var(--muted);cursor:pointer;white-space:nowrap}
nav button[aria-selected=true]{color:var(--ink);border-bottom-color:var(--ink);font-weight:600}
button:focus-visible,input:focus-visible,a:focus-visible{outline:3px solid var(--s-line);outline-offset:2px}
main{max-width:980px;margin:auto;padding:1rem}
.notice{border-radius:8px;padding:.75rem 1rem;margin-bottom:.9rem;display:flex;gap:1rem;align-items:center;justify-content:space-between;flex-wrap:wrap}
.notice.alarm{background:var(--hot);color:#fff}
.notice.fuel{background:var(--surface);border:1px solid var(--line)}
.notice.lost{background:var(--none);color:#fff}
.gauges{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}
.gauge{border-radius:16px;padding:1rem 1.15rem 1.1rem;min-height:12.5rem;color:#fff;background:var(--none);display:flex;flex-direction:column;justify-content:space-between}
.gauge[data-l=cold]{background:var(--cold)}.gauge[data-l=good]{background:var(--good)}
.gauge[data-l=high]{background:var(--high);color:#1a1a1a}.gauge[data-l=hot]{background:var(--hot)}
.gauge .name{font-weight:650;font-size:1.1rem}
.gauge .t{font-size:clamp(4rem,15vw,7.5rem);line-height:.95;font-weight:700;letter-spacing:-.01em}
.gauge .t small{font-size:.32em;font-weight:600;margin-left:.08em;vertical-align:.95em}
.gauge .state{font-size:1.05rem;font-weight:550}
.gauge.alarm{animation:blink 1s steps(2,jump-none) infinite}
@keyframes blink{50%{box-shadow:inset 0 0 0 8px #fff}}
@media (prefers-reduced-motion:reduce){.gauge.alarm{animation:none;box-shadow:inset 0 0 0 8px #fff}}
dl.facts{display:grid;grid-template-columns:repeat(auto-fit,minmax(10.5rem,1fr));gap:1rem 1.5rem;margin:1.4rem 0 0}
.facts dt{color:var(--muted);font-size:.85rem}.facts dd{margin:.1rem 0 0;font-size:1.1rem}
.dot{display:inline-block;width:.7em;height:.7em;border-radius:50%;margin-right:.4em;background:var(--none)}
.dot[data-l=good]{background:var(--good)}.dot[data-l=high]{background:var(--high)}.dot[data-l=hot]{background:var(--hot)}
.panel{background:var(--surface);border:1px solid var(--line);border-radius:10px;padding:1rem}
.legend{display:flex;gap:1.25rem;flex-wrap:wrap;margin:0 0 .5rem;font-size:.95rem}
.legend i{display:inline-block;width:1.4em;height:3px;vertical-align:middle;margin-right:.4em}
#chart{width:100%;height:340px;display:block;touch-action:pan-y}
#readout{min-height:1.5em;color:var(--muted);margin-top:.4rem}
.muted{color:var(--muted)}
ul.files{list-style:none;margin:0;padding:0}
ul.files li{display:flex;justify-content:space-between;gap:1rem;padding:.5rem 0;border-bottom:1px solid var(--line)}
ul.files li:last-child{border-bottom:0}
.linkbtn{font:inherit;background:none;border:0;padding:0;color:var(--ink);text-decoration:underline;cursor:pointer}
a{color:var(--ink)}
.tablewrap{overflow-x:auto;margin-top:1rem}
table{border-collapse:collapse;width:100%;font-size:.95rem}
th,td{text-align:left;padding:.35rem .6rem;border-bottom:1px solid var(--line);white-space:nowrap}
th{font-weight:600;color:var(--muted)}
td.c{color:#fff;font-weight:600}td.c[data-l=cold]{background:var(--cold)}td.c[data-l=good]{background:var(--good)}
td.c[data-l=high]{background:var(--high);color:#1a1a1a}td.c[data-l=hot]{background:var(--hot)}td.c[data-l=unknown]{background:none;color:var(--muted)}
form{display:grid;gap:1rem}
fieldset{border:1px solid var(--line);border-radius:10px;background:var(--surface);padding:.75rem 1rem 1rem;margin:0;display:grid;grid-template-columns:repeat(auto-fit,minmax(14rem,1fr));gap:.9rem 1.25rem}
legend{font-weight:650;padding:0 .35rem}
.field{display:flex;flex-direction:column;gap:.25rem}
.field.check{flex-direction:row;align-items:center;gap:.5rem}
.field small{color:var(--muted)}
input[type=text],input[type=password],input[type=number],input[type=datetime-local],input:not([type]){font:inherit;padding:.5rem .6rem;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink);width:100%}
input[type=checkbox]{width:1.2rem;height:1.2rem}
.actions{display:flex;flex-wrap:wrap;gap:.6rem;align-items:center}
.btn{font:inherit;padding:.55rem 1rem;border-radius:6px;border:1px solid var(--line);background:var(--surface);color:var(--ink);cursor:pointer}
.btn.primary{background:var(--ink);color:var(--bg);border-color:var(--ink);font-weight:600}
.btn.danger{color:var(--hot)}
.notice .btn{background:#fff;color:#1a1a1a;border:0;font-weight:600}
#msg{font-weight:550}
@media (max-width:540px){.gauge{min-height:10rem;padding:.8rem}.gauges{gap:.5rem}}
</style></head>
<body>
<header><h1>Stove monitor</h1><span id="clock" class="num">Connecting…</span></header>
<nav role="tablist">
<button data-tab="now" role="tab" aria-selected="true">Now</button>
<button data-tab="graph" role="tab" aria-selected="false">Last 6 hours</button>
<button data-tab="logs" role="tab" aria-selected="false">Logs</button>
<button data-tab="settings" role="tab" aria-selected="false">Settings</button>
</nav>
<main>
<div id="lost" class="notice lost" hidden>Can’t reach the monitor. Retrying every few seconds.</div>

<section id="now">
<div id="alarm" class="notice alarm" hidden><span id="alarm-text"></span><button class="btn" id="mute">Mute alarm</button></div>
<div id="refuel" class="notice fuel" hidden>The stove is cooling down. It may need more fuel.</div>
<div class="gauges">
<div class="gauge" id="g-stove" data-l="unknown"><div class="name">Stove top</div><div class="t num">--</div><div class="state">Waiting for a reading</div></div>
<div class="gauge" id="g-flue" data-l="unknown"><div class="name">Flue</div><div class="t num">--</div><div class="state">Waiting for a reading</div></div>
</div>
<dl class="facts">
<div><dt>Inside the monitor</dt><dd class="num"><span class="dot" id="int-dot"></span><span id="int">--</span></dd></div>
<div><dt>Stove trend</dt><dd class="num" id="trend">--</dd></div>
<div><dt>Power</dt><dd id="power">--</dd></div>
<div><dt>Battery</dt><dd class="num" id="batt">--</dd></div>
<div><dt>Network</dt><dd id="net">--</dd></div>
<div><dt>Address</dt><dd class="num" id="ip">--</dd></div>
<div><dt>Logging</dt><dd id="sd">--</dd></div>
<div><dt>Clock source</dt><dd id="tsrc">--</dd></div>
</dl>
</section>

<section id="graph" hidden>
<div class="panel">
<div class="legend"><span><i style="background:var(--s-line)"></i>Stove top</span><span><i style="background:var(--f-line)"></i>Flue</span><span><i style="background:var(--i-line);height:2px"></i>Inside the monitor</span></div>
<canvas id="chart"></canvas>
<div id="readout" class="num">Touch or hover over the graph to see readings.</div>
</div>
<p class="muted">One point per minute since the monitor last started. For older readings, open the logs.</p>
</section>

<section id="logs" hidden>
<div class="panel"><ul class="files" id="files"><li class="muted">Loading…</li></ul></div>
<div id="logview"></div>
</section>

<section id="settings" hidden>
<form id="cfg" autocomplete="off">
<fieldset><legend>Set the clock by hand</legend>
<div class="field"><label for="manual-time">Date and time</label><input type="datetime-local" id="manual-time" step="1"></div>
<div class="actions" style="align-self:end"><button type="button" class="btn" id="set-time">Set clock</button><button type="button" class="btn" id="use-device-time">Use this device’s time</button></div>
</fieldset>
<div class="actions" id="cfg-actions">
<button type="submit" class="btn primary">Save settings</button>
<button type="button" class="btn" id="test-alarm">Test alarm</button>
<button type="button" class="btn" id="test-reminder">Test reminder</button>
<button type="button" class="btn" id="defaults">Restore default temperatures</button>
<button type="button" class="btn danger" id="restart">Restart monitor</button>
<span id="msg" role="status"></span>
</div>
</form>
<datalist id="tzlist">
<option value="GMT0BST,M3.5.0/1,M10.5.0">UK</option>
<option value="IST-1GMT0,M10.5.0,M3.5.0/1">Ireland</option>
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option>
<option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
<option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
<option value="UTC0">UTC</option>
</datalist>
</section>
</main>

<script>
const $=(s,r=document)=>r.querySelector(s);
const LEVEL={cold:'Cold',good:'Good',high:'Getting high',hot:'Too hot',unknown:'No reading'};
let cfg=null,hist=null,cursorX=null,histTimer=null;

async function api(url,body){
  const o=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)};
  const r=await fetch(url,o);const t=await r.text();
  if(!r.ok)throw new Error(t||('Error '+r.status));
  try{return JSON.parse(t)}catch(e){return t}
}
function lvl(t,c,w,h){if(t==null||t===''||isNaN(t))return'unknown';t=+t;return t<c?'cold':t<w?'good':t<h?'high':'hot'}

/* Tabs */
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>show(b.dataset.tab));
function show(id){
  document.querySelectorAll('nav button').forEach(b=>b.setAttribute('aria-selected',b.dataset.tab===id));
  document.querySelectorAll('main>section').forEach(s=>s.hidden=s.id!==id);
  history.replaceState(null,'','#'+id);
  clearInterval(histTimer);
  if(id==='graph'){loadHist();histTimer=setInterval(loadHist,60000)}
  if(id==='logs')loadLogs();
  if(id==='settings')fillForm();
}

/* Now */
function gauge(sel,p){
  const g=$(sel);g.dataset.l=p.level;g.classList.toggle('alarm',!!p.alarm);
  $('.t',g).innerHTML=p.t==null?'--':Math.round(p.t)+'<small>°C</small>';
  $('.state',g).textContent=p.fault?p.fault:LEVEL[p.level];
}
function render(s){
  gauge('#g-stove',s.stove);gauge('#g-flue',s.flue);
  $('#clock').textContent=s.time;
  const reasons=[];
  if(s.stove.alarm)reasons.push('the stove top');
  if(s.flue.alarm)reasons.push('the flue');
  if(s.internal.alarm)reasons.push('the inside of the monitor');
  $('#alarm').hidden=!s.alarm;
  $('#alarm-text').textContent=(s.alarmMuted?'Alarm muted. ':'')+'Too hot: '+reasons.join(' and ')+'.';
  $('#mute').hidden=s.alarmMuted;
  $('#refuel').hidden=!s.refuelDue;
  $('#int').textContent=s.internal.t==null?'--':Math.round(s.internal.t)+' °C';
  $('#int-dot').dataset.l=s.internal.level;
  $('#trend').textContent=s.trend==null?'Not enough data yet':(s.trend>0?'+':'')+Math.round(s.trend)+' °C in 5 min';
  $('#power').textContent=s.power==='mains'?(s.charging?'Mains, charging':'Mains'):'On battery';
  $('#batt').textContent=s.battery==null?'Unknown':s.battery+'%';
  $('#net').textContent=s.net.mode==='sta'?'Home Wi-Fi ('+s.net.ssid+')':s.net.mode==='ap'?'Own hotspot ('+s.net.ap+')':'Connecting';
  $('#ip').textContent=s.net.ip+(s.net.mode==='sta'?' or '+s.net.host:'');
  $('#sd').textContent=s.sd?'Saving to SD card':'No SD card';
  $('#tsrc').textContent=s.timeSource;
}
async function poll(){
  try{render(await api('/api/status'));$('#lost').hidden=true}
  catch(e){$('#lost').hidden=false}
}
$('#mute').onclick=async()=>{await api('/api/mute',{});poll()};

/* Graph */
async function loadHist(){try{hist=await api('/api/history');drawChart()}catch(e){}}
function drawChart(){
  const c=$('#chart');if(!hist||!c.offsetParent)return;
  const css=getComputedStyle(document.documentElement),col=n=>css.getPropertyValue(n).trim();
  const dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
  c.width=W*dpr;c.height=H*dpr;const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);
  const now=Date.now(),span=6*3600e3,x0=now-span;
  const L=46,R=12,T=10,B=28,pw=W-L-R,ph=H-T-B;
  const ts=hist.ages.map(a=>now-a*1000);
  const series=[['stove','--s-line',[],2.5],['flue','--f-line',[],2.5],['internal','--i-line',[5,4],1.5]];
  let mx=100;series.forEach(([k])=>hist[k].forEach(v=>{if(v!=null&&v>mx)mx=v}));
  const step=mx>400?100:50;mx=Math.ceil(mx/step)*step;
  const X=t=>L+(t-x0)/span*pw,Y=v=>T+ph-v/mx*ph;
  g.font='12px system-ui,sans-serif';g.fillStyle=col('--muted');g.strokeStyle=col('--line');g.lineWidth=1;
  g.textAlign='right';g.textBaseline='middle';
  for(let v=0;v<=mx;v+=step){const y=Math.round(Y(v))+.5;g.beginPath();g.moveTo(L,y);g.lineTo(W-R,y);g.stroke();g.fillText(v+'°',L-6,y)}
  g.textAlign='center';g.textBaseline='top';
  const h0=new Date(x0);h0.setMinutes(0,0,0);
  for(let t=h0.getTime()+3600e3;t<=now;t+=3600e3){const x=Math.round(X(t))+.5;g.beginPath();g.moveTo(x,T);g.lineTo(x,T+ph);g.stroke();
    g.fillText(new Date(t).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}),x,T+ph+7)}
  if(!ts.length){g.fillText('No readings yet. The first point appears a few seconds after start-up.',L+pw/2,T+ph/2);return}
  series.forEach(([k,cv,dash,lw])=>{g.strokeStyle=col(cv);g.lineWidth=lw;g.setLineDash(dash);g.beginPath();let pen=false;
    hist[k].forEach((v,i)=>{if(v==null||ts[i]<x0){pen=false;return}const x=X(ts[i]),y=Y(v);pen?g.lineTo(x,y):g.moveTo(x,y);pen=true});g.stroke()});
  g.setLineDash([]);
  if(cursorX!=null){
    const t=x0+(cursorX-L)/pw*span;let bi=0;ts.forEach((v,i)=>{if(Math.abs(v-t)<Math.abs(ts[bi]-t))bi=i});
    const x=X(ts[bi]);g.strokeStyle=col('--ink');g.lineWidth=1;g.beginPath();g.moveTo(x,T);g.lineTo(x,T+ph);g.stroke();
    const f=v=>v==null?'--':Math.round(v)+' °C';
    $('#readout').textContent=new Date(ts[bi]).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'})+': stove top '+f(hist.stove[bi])+', flue '+f(hist.flue[bi])+', inside '+f(hist.internal[bi]);
  }
}
const chart=$('#chart');
chart.addEventListener('pointermove',e=>{cursorX=e.offsetX;drawChart()});
chart.addEventListener('pointerdown',e=>{cursorX=e.offsetX;drawChart()});
window.addEventListener('resize',drawChart);

/* Logs */
async function loadLogs(){
  const ul=$('#files');
  try{
    const r=await api('/api/logs');ul.innerHTML='';
    if(!r.sd){ul.innerHTML='<li class="muted">No SD card found. Insert a FAT32-formatted microSD card; the monitor checks again every 30 seconds.</li>';return}
    if(!r.files.length){ul.innerHTML='<li class="muted">No logs yet. The first reading is saved a few seconds after start-up.</li>';return}
    r.files.sort((a,b)=>b.name.localeCompare(a.name)).forEach(f=>{
      const li=document.createElement('li');const b=document.createElement('button');
      b.className='linkbtn num';b.textContent=f.name.replace('.csv','');b.onclick=()=>openLog(f.name);
      const a=document.createElement('a');a.href='/api/download?f='+encodeURIComponent(f.name);a.textContent='Download ('+Math.max(1,Math.round(f.size/1024))+' KB)';
      li.append(b,a);ul.append(li)});
  }catch(e){ul.innerHTML='<li class="muted">Couldn’t load the log list: '+e.message+'</li>'}
}
async function openLog(name){
  const box=$('#logview');box.innerHTML='<p class="muted">Loading '+name+'…</p>';
  if(!cfg)cfg=await api('/api/config');
  const txt=await api('/api/logtail?f='+encodeURIComponent(name)+'&n=300');
  const lines=String(txt).trim().split('\n');const head=lines.shift().split(',');const ix=k=>head.indexOf(k);
  const rows=lines.reverse().map(l=>l.split(','));
  const tbl=document.createElement('table');
  tbl.innerHTML='<thead><tr><th>Time</th><th>Stove top</th><th>Flue</th><th>Inside</th><th>Battery</th><th>Power</th><th>Alarm</th></tr></thead>';
  const tb=document.createElement('tbody');
  rows.forEach(r=>{const tr=document.createElement('tr');
    const cell=(v,cls,l)=>{const td=document.createElement('td');td.textContent=v;if(cls){td.className=cls;td.dataset.l=l}tr.append(td)};
    const s=r[ix('stove_c')],f=r[ix('flue_c')];
    cell(r[ix('datetime')]||('uptime '+r[ix('uptime_s')]+' s'));
    cell(s?Math.round(s)+' °C':'--','c',lvl(s,cfg.stoveCool,cfg.stoveWarn,cfg.stoveHot));
    cell(f?Math.round(f)+' °C':'--','c',lvl(f,cfg.flueCool,cfg.flueWarn,cfg.flueHot));
    const it=r[ix('internal_c')];cell(it?Math.round(it)+' °C':'--');
    const bp=r[ix('battery_pct')];cell(bp>=0?bp+'%':'--');
    cell(r[ix('power')]==='mains'?'Mains':'Battery');cell(r[ix('alarm')]==='1'?'Yes':'');
    tb.append(tr)});
  tbl.append(tb);
  box.innerHTML='<h2 style="font-size:1.1rem;margin:1.25rem 0 0">'+name.replace('.csv','')+'</h2><p class="muted" style="margin:.25rem 0 0">Newest first, last '+rows.length+' readings. Download the file for the full day.</p>';
  const w=document.createElement('div');w.className='tablewrap';w.append(tbl);box.append(w);
}

/* Settings */
const FORM=[
 ['Home Wi-Fi',[['ssid','Network name','text'],['pass','Password','password','Leave blank to keep the saved password.'],
   ['apName','Hotspot name when home Wi-Fi isn’t available','text','Open network at 192.168.4.1'],['hostname','Device name','text','Reach it at name.local on most devices']]],
 ['Clock',[['tz','Time zone','tz','POSIX format. Pick one from the list or type your own.'],['ntp','Internet time server','text']]],
 ['Screen and logging',[['wakeTemp','Turn the screen on when the stove top reaches (°C)','number'],['logInterval','Save a reading every (seconds)','number']]],
 ['Stove top colours (°C)',[['stoveCool','Blue below','number','Green from here'],['stoveWarn','Yellow from','number'],['stoveHot','Red from','number'],['stoveAlarm','Sound the alarm from','number']]],
 ['Flue colours (°C)',[['flueCool','Blue below','number','Green from here'],['flueWarn','Yellow from','number'],['flueHot','Red from','number'],['flueAlarm','Sound the alarm from','number']]],
 ['Inside the monitor (°C)',[['intWarn','Warn from','number'],['intAlarm','Sound the alarm from','number']]],
 ['Over-temperature alarm',[['alarmEnabled','Sound the alarm','check'],['alarmVolume','Volume','range'],['snoozeMin','Mute for (minutes)','number']]],
 ['Add-fuel reminder',[['refuelEnabled','Chirp when the fire is dying down','check'],['refuelTemp','Chirp when the stove top falls below (°C)','number'],['refuelVolume','Volume','range'],['refuelRepeatMin','Repeat every (minutes)','number']]],
 ['Probes',[['ignoreShortFaults','Ignore “shorted to ground” faults (some probes touching the stove need this)','check']]]
];
function buildForm(){
  const form=$('#cfg'),anchor=$('#cfg-actions');
  FORM.forEach(([title,fields])=>{
    const fs=document.createElement('fieldset');const lg=document.createElement('legend');lg.textContent=title;fs.append(lg);
    fields.forEach(([k,label,type,hint])=>{
      const id='f-'+k,w=document.createElement('div');w.className='field'+(type==='check'?' check':'');
      let inp;
      if(type==='check')inp='<input type="checkbox" id="'+id+'" name="'+k+'">';
      else if(type==='range')inp='<input type="range" id="'+id+'" name="'+k+'" min="0" max="255" step="5">';
      else if(type==='tz')inp='<input id="'+id+'" name="'+k+'" list="tzlist" spellcheck="false">';
      else inp='<input type="'+type+'" id="'+id+'" name="'+k+'"'+(type==='number'?' step="any" inputmode="decimal"':'')+' spellcheck="false">';
      w.innerHTML=type==='check'?inp+'<label for="'+id+'">'+label+'</label>':'<label for="'+id+'">'+label+'</label>'+inp+(hint?'<small>'+hint+'</small>':'');
      fs.append(w)});
    form.insertBefore(fs,anchor)});
}
async function fillForm(){
  try{cfg=await api('/api/config')}catch(e){msg('Couldn’t load settings: '+e.message);return}
  FORM.forEach(([,fields])=>fields.forEach(([k,,type])=>{const el=$('#f-'+k);
    if(type==='check')el.checked=!!cfg[k];else if(k==='pass'){el.value='';el.placeholder=cfg.passSet?'Saved':'Not set'}else el.value=cfg[k]}));
  const d=new Date(Date.now()-new Date().getTimezoneOffset()*6e4);$('#manual-time').value=d.toISOString().slice(0,19);
}
function msg(t){$('#msg').textContent=t}
$('#cfg').onsubmit=async e=>{
  e.preventDefault();const body={};
  FORM.forEach(([,fields])=>fields.forEach(([k,,type])=>{const el=$('#f-'+k);
    if(type==='check')body[k]=el.checked;else if(type==='number'||type==='range')body[k]=Number(el.value);
    else if(k==='pass'){if(el.value)body[k]=el.value}else body[k]=el.value.trim()}));
  try{cfg=await api('/api/config',body);$('#f-pass').value='';
    msg(cfg.wifiChanged?'Saved. The monitor is reconnecting to Wi-Fi, so this page may stop updating briefly.':'Saved.')}
  catch(err){msg(err.message)}
};
$('#set-time').onclick=async()=>{const v=$('#manual-time').value;if(!v){msg('Choose a date and time first.');return}
  try{await api('/api/time',{epoch:Math.floor(new Date(v).getTime()/1000)});msg('Clock set.');poll()}catch(e){msg(e.message)}};
$('#use-device-time').onclick=async()=>{try{await api('/api/time',{epoch:Math.floor(Date.now()/1000)});msg('Clock set from this device.');poll()}catch(e){msg(e.message)}};
$('#test-alarm').onclick=()=>api('/api/test',{type:'alarm'}).then(()=>msg('Alarm tested.'));
$('#test-reminder').onclick=()=>api('/api/test',{type:'reminder'}).then(()=>msg('Reminder tested.'));
$('#defaults').onclick=async()=>{if(!confirm('Restore the default temperatures, alarms and logging interval? Wi-Fi and clock settings stay as they are.'))return;
  await api('/api/defaults',{});await fillForm();msg('Default temperatures restored.')};
$('#restart').onclick=async()=>{if(!confirm('Restart the monitor now?'))return;await api('/api/restart',{});msg('Restarting. This page reconnects by itself.')};

buildForm();
show(['now','graph','logs','settings'].includes(location.hash.slice(1))?location.hash.slice(1):'now');
poll();setInterval(poll,3000);
</script>
</body></html>)rawliteral";
