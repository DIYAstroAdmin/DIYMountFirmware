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
  header { padding:10px 16px 0; border-bottom:1px solid var(--line); display:flex; flex-wrap:wrap; gap:8px 20px; align-items:flex-end; }
  h1 { margin:0 0 10px; font-size:1.2rem; }
  header .tag { margin:0 0 10px auto; align-self:center; }
  nav { display:flex; gap:2px; }
  nav button { border:none; border-bottom:2px solid transparent; border-radius:0; background:none; padding:8px 14px; color:var(--dim); }
  nav button:active { background:none; color:var(--text); }
  nav button.on { background:none; color:var(--text); border-bottom-color:var(--accent); }
  .tab[hidden] { display:none; }
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
  .msg { min-height:1.3em; color:var(--hot); margin-top:10px; font-size:.9rem; }
  .two { display:grid; grid-template-columns:1fr 1fr; gap:0 10px; }
  .tag { font-size:.8rem; padding:2px 8px; border:1px solid var(--line); border-radius:10px; }
  .bad { color:var(--hot); border-color:var(--hot); }
  .wide { grid-column:1 / -1; }
  .head { display:flex; justify-content:space-between; align-items:baseline; }
  .head button, .head select { padding:2px 10px; font-size:.8rem; }
  .head .tools { display:flex; gap:8px; align-items:center; margin-bottom:10px; }
  .head .tools label { margin:0; }
  .head .tools label.check { display:flex; align-items:center; gap:4px; margin-right:6px; cursor:pointer; }
  .head .tools input[type=checkbox] { width:auto; margin:0; }
  #log { height:calc(100vh - 150px); min-height:200px; overflow-y:auto; background:var(--bg); border:1px solid var(--line); border-radius:6px; padding:6px 8px;
         font-family:ui-monospace,Consolas,monospace; font-size:.8rem; line-height:1.4; white-space:pre-wrap; overflow-wrap:anywhere; }
  #log .debug { color:var(--dim); }
  #log .warn { color:var(--hot); }
  #log .error { color:var(--hot); font-weight:600; }
  table.axes { width:100%; border-collapse:collapse; }
  table.axes th { text-align:left; font-weight:normal; color:var(--dim); font-size:.8rem; padding:4px 6px 4px 0; }
  table.axes td { padding:3px 0 3px 6px; }
  table.axes td select { width:100%; }
  table.axes input[type=checkbox] { width:auto; transform:scale(1.3); }
  table.axes tr.group th { padding-top:12px; text-transform:uppercase; letter-spacing:.08em; font-size:.7rem; }
  .note { font-size:.8rem; color:var(--dim); margin-top:8px; }
  .note.pending { color:var(--hot); }
</style>
</head>
<body>
<header>
  <h1>DIYMount</h1>
  <nav>
    <button data-tab="main">Main</button>
    <button data-tab="setup">Setup</button>
    <button data-tab="log">Log</button>
  </nav>
  <span id="state" class="tag">connecting</span>
</header>
<main id="tab-main" class="tab">
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
    <div class="msg"></div>
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
        <option value="q" selected>1/4 slew rate</option>
        <option value="h">1/2 slew rate</option>
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
    <label for="tmode">Coordinates</label>
    <select id="tmode" style="width:100%">
      <option value="eq">RA / Dec</option>
      <option value="altaz">Alt / Az (tracking unchanged)</option>
    </select>
    <div id="t-eq">
      <label for="tra">Right ascension (h m s)</label>
      <input id="tra" placeholder="05 34 32" autocomplete="off">
      <label for="tdec">Declination (d m s)</label>
      <input id="tdec" placeholder="+22 00 52" autocomplete="off">
    </div>
    <div id="t-altaz" hidden>
      <label for="talt">Altitude (d m s)</label>
      <input id="talt" placeholder="+45 00 00" autocomplete="off">
      <label for="taz">Azimuth (d m s, from north through east)</label>
      <input id="taz" placeholder="180 00 00" autocomplete="off">
    </div>
    <div class="row">
      <button id="goto">Goto</button>
      <button id="sync">Sync</button>
    </div>
    <div class="row">
      <button id="settime">Set time from this device</button>
    </div>
  </section>
</main>

<main id="tab-setup" class="tab" hidden>
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
    <div class="msg"></div>
  </section>

  <section>
    <h2>Axes</h2>
    <table class="axes">
      <thead><tr><th></th><th>RA</th><th>Dec</th></tr></thead>
      <tbody>
        <tr><th>Motor step angle</th><td><input id="ra_stepAngle" disabled></td><td><input id="dec_stepAngle" disabled></td></tr>
        <tr><th>Microsteps *</th><td><select id="ra_microsteps"></select></td><td><select id="dec_microsteps"></select></td></tr>
        <tr><th>Gear ratio (:1) *</th><td><input id="ra_gearRatio" type="number" step="any"></td><td><input id="dec_gearRatio" type="number" step="any"></td></tr>
        <tr><th>Reverse direction *</th><td><input id="ra_invert" type="checkbox"></td><td><input id="dec_invert" type="checkbox"></td></tr>
        <tr><th>Current (mA RMS)</th><td><input id="ra_current" type="number" step="10"></td><td><input id="dec_current" type="number" step="10"></td></tr>
        <tr><th>Max speed (deg/s)</th><td><input id="ra_maxRate" disabled></td><td><input id="dec_maxRate" disabled></td></tr>
        <tr class="group"><th colspan="3">Homing</th></tr>
        <tr><th>Search direction</th>
          <td><select id="ra_homeDir"><option value="1">Positive</option><option value="-1">Negative</option></select></td>
          <td><select id="dec_homeDir"><option value="1">Positive</option><option value="-1">Negative</option></select></td></tr>
        <tr><th>Switch angle (deg)</th><td><input id="ra_homeAngle" type="number" step="any"></td><td><input id="dec_homeAngle" type="number" step="any"></td></tr>
      </tbody>
    </table>
    <div id="axesnote" class="note">* Applied when the controller restarts. Current and homing apply immediately.</div>
    <div class="row">
      <button id="saveaxes">Save</button>
      <button id="reboot2">Restart</button>
    </div>
    <div class="msg"></div>
  </section>
</main>

<main id="tab-log" class="tab" hidden>
  <section class="wide">
    <div class="head"><h2>Log</h2>
      <div class="tools">
        <label class="check"><input id="log_lx200" type="checkbox"> LX200</label>
        <label class="check"><input id="log_alpaca" type="checkbox"> Alpaca</label>
        <label for="loglevel">Level</label>
        <select id="loglevel">
          <option value="0">Debug</option><option value="1">Info</option>
          <option value="2">Warning</option><option value="3">Error</option>
        </select>
        <button id="clearlog">Clear</button>
      </div>
    </div>
    <div id="log"></div>
  </section>
</main>

<script>
const $ = id => document.getElementById(id);
const CONFIG_FIELDS = ['latitude','longitude','elevation','meridianLimit','guideRate','slewRate','ssid','hostname'];
let status = null, note = '', noteUntil = 0, timeSent = false;

// Messages show in every .msg box, so they are visible whichever tab is open.
const showMsg = text => document.querySelectorAll('.msg').forEach(m => m.textContent = text);
function say(text) { note = text; noteUntil = Date.now() + 5000; showMsg(text); }

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
    if (Date.now() > noteUntil) showMsg(s.message);
    if (!s.timeSet && !timeSent) { timeSent = true; cmd('time', { utc: Date.now() / 1000 }); }
  } catch (e) {
    $('state').textContent = 'offline';
    $('state').classList.add('bad');
  }
}

// Log: fetch the lines added since the last poll and append them. The view follows
// the newest line unless it has been scrolled up.
const LOG_CLASSES = ['debug', 'info', 'warn', 'error'], LOG_KEEP = 500;
let logSeq = 0, logNow = 0;

async function refreshLog() {
  try {
    const j = await (await fetch('/api/log?since=' + logSeq)).json();
    const restarted = j.now < logNow;
    logNow = j.now;
    if (restarted) { logSeq = 0; return; }   // uptime went backwards: fetch everything next time
    logSeq = j.next;
    if (document.activeElement !== $('loglevel')) $('loglevel').value = j.level;
    $('log_lx200').checked = j.lx200;
    $('log_alpaca').checked = j.alpaca;
    if (!j.lines.length) return;
    const box = $('log');
    const follow = box.scrollHeight - box.scrollTop - box.clientHeight < 8;
    for (const e of j.lines) {
      const d = document.createElement('div');
      d.className = LOG_CLASSES[e.l] || 'info';
      d.textContent = new Date(Date.now() - (j.now - e.t)).toTimeString().slice(0, 8) + '  ' + e.m;
      box.appendChild(d);
    }
    while (box.childElementCount > LOG_KEEP) box.firstChild.remove();
    if (follow) box.scrollTop = box.scrollHeight;
  } catch (e) {}
}

const AXES = ['ra', 'dec'];
const AXIS_FIELDS = ['microsteps', 'gearRatio', 'current', 'homeDir', 'homeAngle'];
for (const a of AXES) {
  for (let m = 1; m <= 256; m *= 2) $(a + '_microsteps').add(new Option(m, m));
}

async function loadConfig() {
  try {
    const c = await (await fetch('/api/config')).json();
    CONFIG_FIELDS.forEach(f => $(f).value = c[f]);
    $('slewRate').max = c.maxSlewRate;
    $('ip').value = c.ip + (c.ap ? ' (access point)' : '');
    AXES.forEach((a, i) => {
      const x = c.axes[i];
      AXIS_FIELDS.forEach(f => $(a + '_' + f).value = x[f]);
      $(a + '_invert').checked = x.invert;
      $(a + '_stepAngle').value = x.stepAngle + '°';
      $(a + '_maxRate').value = x.maxRate;
      $(a + '_current').min = c.currentRange[0];
      $(a + '_current').max = c.currentRange[1];
      $(a + '_gearRatio').min = c.gearRange[0];
      $(a + '_gearRatio').max = c.gearRange[1];
    });
    const note = $('axesnote');
    note.classList.toggle('pending', c.restartNeeded);
    note.textContent = c.restartNeeded
      ? 'Saved axis changes are waiting for a restart. The mount position will be reset to home.'
      : '* Applied when the controller restarts. Current and homing apply immediately.';
  } catch (e) { say('Could not load configuration'); }
}

async function saveConfig(body) {
  try {
    const j = await (await fetch('/api/config', { method:'POST', body })).json();
    say(j.reboot ? 'Saved. Restart to apply all the changes.' : 'Saved');
    loadConfig();
    return true;
  } catch (e) { say('Save failed'); return false; }
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

// Goto/sync target from whichever coordinate set is shown: { cmd suffix, params } or null.
function target() {
  if ($('tmode').value === 'altaz') {
    const alt = parseSexa($('talt').value);
    let az = parseSexa($('taz').value);
    if (az === 360) az = 0;
    if (isNaN(alt) || isNaN(az) || alt < -90 || alt > 90 || az < 0 || az >= 360) { say('Enter a valid altitude and azimuth'); return null; }
    return { suffix: 'altaz', params: { alt, az } };
  }
  const ra = parseSexa($('tra').value), dec = parseSexa($('tdec').value);
  if (isNaN(ra) || isNaN(dec) || ra < 0 || ra >= 24 || dec < -90 || dec > 90) { say('Enter a valid RA and Dec'); return null; }
  return { suffix: '', params: { ra, dec } };
}

$('tmode').onchange = () => {
  const altaz = $('tmode').value === 'altaz';
  $('t-eq').hidden = altaz;
  $('t-altaz').hidden = !altaz;
};

$('stop').onclick = () => cmd('stop');
$('track').onclick = () => status && cmd('track', { on: status.tracking ? 0 : 1 }).then(refresh);
$('trackrate').onchange = () => cmd('trackrate', { rate:$('trackrate').value });
$('park').onclick = () => status && cmd(status.atPark ? 'unpark' : 'park').then(refresh);
$('home').onclick = () => confirm('Search for the home switches now?') && cmd('home');
$('sethome').onclick = () => confirm('Declare the current position to be home (counterweight down, pointing at the pole)?') && cmd('sethome');
$('goto').onclick = () => { const t = target(); if (t) cmd('goto' + t.suffix, t.params); };
$('sync').onclick = () => { const t = target(); if (t) cmd('sync' + t.suffix, t.params).then(ok => ok && say('Synced')); };
$('settime').onclick = () => cmd('time', { utc: Date.now() / 1000 }).then(ok => ok && say('Time set'));
const restart = () => confirm('Restart the controller? The mount position will be reset to home.') && cmd('reboot');
$('reboot').onclick = restart;
$('reboot2').onclick = restart;
$('save').onclick = async () => {
  const body = new URLSearchParams();
  CONFIG_FIELDS.forEach(f => body.set(f, $(f).value));
  if ($('password').value) body.set('password', $('password').value);
  if (await saveConfig(body)) $('password').value = '';
};
$('saveaxes').onclick = () => {
  const body = new URLSearchParams();
  for (const a of AXES) {
    AXIS_FIELDS.forEach(f => body.set(a + '_' + f, $(a + '_' + f).value));
    body.set(a + '_invert', $(a + '_invert').checked ? 1 : 0);
  }
  saveConfig(body);
};

$('clearlog').onclick = () => $('log').replaceChildren();
// The level is applied on the controller: messages below it are never written.
$('loglevel').onchange = async () => { await cmd('loglevel', { level: $('loglevel').value }); $('loglevel').blur(); refreshLog(); };
// LX200 and Alpaca traffic (Info level) is switched on the controller too, not just hidden.
for (const src of ['lx200', 'alpaca']) {
  $('log_' + src).onchange = async e => { await cmd('logsource', { source: src, on: e.target.checked ? 1 : 0 }); refreshLog(); };
}

// Tabs. The open tab is kept in the address (#main, #setup, #log) so a reload stays on it.
const TABS = ['main', 'setup', 'log'];
function showTab(name) {
  if (!TABS.includes(name)) name = 'main';
  TABS.forEach(t => $('tab-' + t).hidden = t !== name);
  document.querySelectorAll('nav button').forEach(b => b.classList.toggle('on', b.dataset.tab === name));
  if (name === 'log') $('log').scrollTop = $('log').scrollHeight;   // lines may have arrived while hidden
  if (name === 'setup') loadConfig();                                // pick up changes made elsewhere
  history.replaceState(null, '', '#' + name);
}
document.querySelectorAll('nav button').forEach(b => b.onclick = () => showTab(b.dataset.tab));
showTab(location.hash.slice(1));

loadConfig();
refresh();
refreshLog();
setInterval(refresh, 1000);
setInterval(refreshLog, 1000);
</script>
</body>
</html>
)HTML";
