// The single-page web interface, served from flash.
#pragma once
#include <Arduino.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MyMount</title>
<style>
  :root { --bg:#120808; --panel:#1e0f0f; --line:#4a1f1f; --text:#e8b0a8; --dim:#9a6860; --accent:#c0392b; --hot:#ff6b5a; }
  * { box-sizing:border-box; }
  body { margin:0; font-family:system-ui,sans-serif; background:var(--bg); color:var(--text); }
  header { padding:12px 16px; border-bottom:1px solid var(--line); display:flex; justify-content:space-between; align-items:baseline; }
  h1 { margin:0; font-size:1.2rem; }
  h2 { margin:0 0 10px; font-size:.85rem; text-transform:uppercase; letter-spacing:.08em; color:var(--dim); }
  main { display:grid; grid-template-columns:repeat(auto-fit,minmax(300px,1fr)); gap:12px; padding:12px; max-width:1100px; margin:auto; }
  section { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:14px; }
  .grid { display:grid; grid-template-columns:auto 1fr; gap:4px 14px; font-variant-numeric:tabular-nums; }
  .grid span:nth-child(odd) { color:var(--dim); }
  .big { font-size:1.25rem; }
  button, select, input { font:inherit; color:var(--text); background:#2a1414; border:1px solid var(--line); border-radius:6px; padding:8px 10px; }
  button { cursor:pointer; touch-action:none; user-select:none; }
  button:active, button.on { background:var(--accent); color:#fff; }
  button.stop { background:var(--accent); color:#fff; font-weight:600; }
  input { width:100%; }
  label { display:block; font-size:.8rem; color:var(--dim); margin:8px 0 3px; }
  .row { display:flex; gap:8px; flex-wrap:wrap; margin-top:10px; }
  .row > * { flex:1; }
  .pad { display:grid; grid-template-columns:repeat(3,64px); grid-template-rows:repeat(3,56px); gap:6px; justify-content:center; }
  .pad button { font-size:1.1rem; }
  #msg { min-height:1.3em; color:var(--hot); margin-top:10px; font-size:.9rem; }
  .two { display:grid; grid-template-columns:1fr 1fr; gap:0 10px; }
  .tag { font-size:.8rem; padding:2px 8px; border:1px solid var(--line); border-radius:10px; }
  .bad { color:var(--hot); border-color:var(--hot); }
</style>
</head>
<body>
<header><h1>MyMount</h1><span id="state" class="tag">connecting</span></header>
<main>
  <section>
    <h2>Position</h2>
    <div class="grid">
      <span>RA</span><span id="ra" class="big">--</span>
      <span>Dec</span><span id="dec" class="big">--</span>
      <span>Hour angle</span><span id="ha">--</span>
      <span>Alt / Az</span><span id="altaz">--</span>
      <span>Sidereal time</span><span id="lst">--</span>
      <span>UTC</span><span id="utc">--</span>
      <span>Pier side</span><span id="pier">--</span>
      <span>Axes</span><span id="axes">--</span>
      <span>Drivers</span><span id="drv">--</span>
      <span>Home switches</span><span id="sw">--</span>
    </div>
    <div id="msg"></div>
  </section>

  <section>
    <h2>Control</h2>
    <div class="pad">
      <span></span><button data-dir="n">N</button><span></span>
      <button data-dir="e">E</button><button class="stop" id="stop">Stop</button><button data-dir="w">W</button>
      <span></span><button data-dir="s">S</button><span></span>
    </div>
    <div class="row">
      <select id="rate">
        <option value="g">Guide rate</option>
        <option value="c">Centre rate</option>
        <option value="m" selected>Find rate</option>
        <option value="s">Slew rate</option>
      </select>
      <select id="trackrate">
        <option value="0">Sidereal</option>
        <option value="1">Lunar</option>
        <option value="2">Solar</option>
      </select>
    </div>
    <div class="row">
      <button id="track">Tracking</button>
      <button id="park">Park</button>
    </div>
    <div class="row">
      <button id="home">Find home</button>
      <button id="sethome">Set home here</button>
    </div>
  </section>

  <section>
    <h2>Goto / Sync</h2>
    <label for="tra">Right ascension (h m s)</label>
    <input id="tra" placeholder="05 34 32" autocomplete="off">
    <label for="tdec">Declination (d m s)</label>
    <input id="tdec" placeholder="+22 00 52" autocomplete="off">
    <div class="row">
      <button id="goto">Goto</button>
      <button id="sync">Sync</button>
    </div>
    <div class="row">
      <button id="settime">Set time from this device</button>
    </div>
  </section>

  <section>
    <h2>Configuration</h2>
    <div class="two">
      <div><label for="latitude">Latitude (deg, N+)</label><input id="latitude" type="number" step="any"></div>
      <div><label for="longitude">Longitude (deg, E+)</label><input id="longitude" type="number" step="any"></div>
      <div><label for="elevation">Elevation (m)</label><input id="elevation" type="number" step="any"></div>
      <div><label for="meridianLimit">Past-meridian limit (deg)</label><input id="meridianLimit" type="number" step="any"></div>
      <div><label for="guideRate">Guide rate (x sidereal)</label><input id="guideRate" type="number" step="any"></div>
      <div><label for="slewRate">Slew rate (deg/s)</label><input id="slewRate" type="number" step="any"></div>
      <div><label for="ssid">WiFi network</label><input id="ssid" autocomplete="off"></div>
      <div><label for="password">WiFi password</label><input id="password" type="password" placeholder="unchanged" autocomplete="new-password"></div>
      <div><label for="hostname">Host name</label><input id="hostname" autocomplete="off"></div>
      <div><label>Address</label><input id="ip" disabled></div>
    </div>
    <div class="row">
      <button id="save">Save</button>
      <button id="reboot">Restart</button>
    </div>
  </section>
</main>

<script>
const $ = id => document.getElementById(id);
const CONFIG_FIELDS = ['latitude','longitude','elevation','meridianLimit','guideRate','slewRate','ssid','hostname'];
let status = null, note = '', noteUntil = 0, timeSent = false;

function say(text) { note = text; noteUntil = Date.now() + 5000; $('msg').textContent = text; }

async function cmd(name, params = {}) {
  try {
    const r = await fetch('/api/cmd', { method:'POST', body:new URLSearchParams({ cmd:name, ...params }) });
    const j = await r.json();
    if (!j.ok) say(j.error || 'Command failed');
    return j.ok;
  } catch (e) { say('No response from mount'); return false; }
}

function sexa(value, signed) {
  const neg = value < 0;
  let t = Math.round(Math.abs(value) * 3600);
  const p = n => String(n).padStart(2, '0');
  const s = p(Math.floor(t / 3600)) + ':' + p(Math.floor(t / 60) % 60) + ':' + p(t % 60);
  return (signed ? (neg ? '-' : '+') : (neg ? '-' : '')) + s;
}

function parseSexa(text) {
  const t = text.trim();
  if (!t) return NaN;
  const parts = t.replace(/^[+-]/, '').split(/[^0-9.]+/).filter(x => x).map(Number);
  if (!parts.length || parts.length > 3 || parts.some(isNaN)) return NaN;
  const v = parts[0] + (parts[1] || 0) / 60 + (parts[2] || 0) / 3600;
  return t.startsWith('-') ? -v : v;
}

async function refresh() {
  try {
    const s = await (await fetch('/api/status')).json();
    status = s;
    $('state').textContent = s.state;
    $('state').classList.remove('bad');
    $('ra').textContent = sexa(s.ra);
    $('dec').textContent = sexa(s.dec, true);
    $('ha').textContent = sexa(s.ha, true);
    $('altaz').textContent = s.alt.toFixed(1) + '° / ' + s.az.toFixed(1) + '°';
    $('lst').textContent = sexa(s.lst);
    $('utc').textContent = new Date(s.utc * 1000).toISOString().slice(0, 19).replace('T', ' ') + (s.timeSet ? '' : ' (not set)');
    $('pier').textContent = s.pier;
    $('axes').textContent = s.axis1.toFixed(2) + '°, ' + s.axis2.toFixed(2) + '°' + (s.homed ? ' (homed)' : '');
    $('drv').textContent = 'RA ' + (s.drvRa ? 'ok' : 'NOT RESPONDING') + ', Dec ' + (s.drvDec ? 'ok' : 'NOT RESPONDING');
    $('sw').textContent = 'RA ' + (s.swRa ? 'active' : 'open') + ', Dec ' + (s.swDec ? 'active' : 'open');
    $('track').classList.toggle('on', s.tracking);
    $('park').textContent = s.atPark ? 'Unpark' : 'Park';
    if (document.activeElement !== $('trackrate')) $('trackrate').value = s.trackRate;
    if (Date.now() > noteUntil) $('msg').textContent = s.message;
    if (!s.timeSet && !timeSent) { timeSent = true; cmd('time', { utc: Date.now() / 1000 }); }
  } catch (e) {
    $('state').textContent = 'offline';
    $('state').classList.add('bad');
  }
}

async function loadConfig() {
  try {
    const c = await (await fetch('/api/config')).json();
    CONFIG_FIELDS.forEach(f => $(f).value = c[f]);
    $('slewRate').max = c.maxSlewRate;
    $('ip').value = c.ip + (c.ap ? ' (access point)' : '');
  } catch (e) { say('Could not load configuration'); }
}

// Direction buttons: move while held. The move is refreshed while the button is down
// and the mount stops by itself if the refreshes stop arriving.
document.querySelectorAll('[data-dir]').forEach(b => {
  let timer = null;
  const send = () => cmd('move', { dir:b.dataset.dir, rate:$('rate').value });
  const start = e => { e.preventDefault(); if (timer) return; b.classList.add('on'); send(); timer = setInterval(send, 400); };
  const end = () => { if (!timer) return; clearInterval(timer); timer = null; b.classList.remove('on'); cmd('stopmove', { dir:b.dataset.dir }); };
  b.addEventListener('pointerdown', start);
  ['pointerup','pointercancel','pointerleave'].forEach(ev => b.addEventListener(ev, end));
});

function target() {
  const ra = parseSexa($('tra').value), dec = parseSexa($('tdec').value);
  if (isNaN(ra) || isNaN(dec) || ra < 0 || ra >= 24 || dec < -90 || dec > 90) { say('Enter a valid RA and Dec'); return null; }
  return { ra, dec };
}

$('stop').onclick = () => cmd('stop');
$('track').onclick = () => status && cmd('track', { on: status.tracking ? 0 : 1 }).then(refresh);
$('trackrate').onchange = () => cmd('trackrate', { rate:$('trackrate').value });
$('park').onclick = () => status && cmd(status.atPark ? 'unpark' : 'park').then(refresh);
$('home').onclick = () => confirm('Search for the home switches now?') && cmd('home');
$('sethome').onclick = () => confirm('Declare the current position to be home (counterweight down, pointing at the pole)?') && cmd('sethome');
$('goto').onclick = () => { const t = target(); if (t) cmd('goto', t); };
$('sync').onclick = () => { const t = target(); if (t) cmd('sync', t).then(ok => ok && say('Synced')); };
$('settime').onclick = () => cmd('time', { utc: Date.now() / 1000 }).then(ok => ok && say('Time set'));
$('reboot').onclick = () => confirm('Restart the controller? The mount position will be reset to home.') && cmd('reboot');
$('save').onclick = async () => {
  const body = new URLSearchParams();
  CONFIG_FIELDS.forEach(f => body.set(f, $(f).value));
  if ($('password').value) body.set('password', $('password').value);
  try {
    const j = await (await fetch('/api/config', { method:'POST', body })).json();
    say(j.reboot ? 'Saved. Restart to apply the network changes.' : 'Saved');
    $('password').value = '';
    loadConfig();
  } catch (e) { say('Save failed'); }
};

loadConfig();
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";
