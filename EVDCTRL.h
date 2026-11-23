static const char EVDCTRL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EVDCTR</title>
<style>
  :root{--bg:#050507;--panel:#111116;--panel2:#1a1a21;--line:#32323c;
        --text:#f6f6f8;--muted:#a1a1ad;--accent:#9d7bff;--accenttext:#140b2c;
        --good:#37dba0;--warn:#ffc857;--bad:#ff5d5d}
  *{box-sizing:border-box}
  body{margin:0;font:14px/1.45 system-ui,sans-serif;color:var(--text);
       background:radial-gradient(circle at top right,#2a1b52,transparent 28rem),var(--bg)}
  header{display:flex;flex-wrap:wrap;gap:12px;align-items:center;padding:18px 22px;
         border-bottom:1px solid var(--line)}
  h1{font-size:1.35rem;margin:0;letter-spacing:-.03em}
  .pill{padding:3px 10px;border:1px solid var(--line);border-radius:999px;
        color:var(--muted);font-size:.8rem}
  .pill.ok{color:var(--good);border-color:#1f5b45}
  .pill.bad{color:var(--bad);border-color:#5b1f1f}
  .pill.warn{color:var(--warn);border-color:#5b4a1f}
  nav{display:flex;gap:6px;padding:12px 22px 0}
  nav button{background:var(--panel2);border:1px solid var(--line);color:var(--text);
             padding:8px 14px;border-radius:10px 10px 0 0;font:inherit;cursor:pointer}
  nav button.on{background:var(--panel);border-bottom-color:var(--panel);font-weight:700}
  main{padding:0 22px 40px}
  section{display:none;background:var(--panel);border:1px solid var(--line);
          border-radius:0 14px 14px 14px;padding:20px}
  section.on{display:block}
  .grid{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(230px,1fr))}
  .card{background:var(--panel2);border:1px solid var(--line);border-radius:12px;padding:14px}
  .card h3{margin:0 0 8px;font-size:.85rem;text-transform:uppercase;
           letter-spacing:.08em;color:var(--muted)}
  .kv{display:flex;justify-content:space-between;gap:10px;padding:3px 0}
  .kv span:last-child{font-variant-numeric:tabular-nums}
  label{display:block;margin-top:12px;font-weight:700;font-size:.85rem}
  input[type=range]{width:100%}
  input[type=text],input[type=number],select{width:100%;padding:8px;margin-top:4px;
    border-radius:9px;border:1px solid #343440;background:#09090c;color:var(--text)}
  button.act{margin-top:10px;background:var(--accent);color:var(--accenttext);border:0;
             padding:9px 16px;border-radius:10px;font-weight:800;cursor:pointer}
  button.act.ghost{background:var(--panel2);color:var(--text);border:1px solid var(--line)}
  pre{background:#09090c;border:1px solid var(--line);border-radius:10px;padding:12px;
      max-height:340px;overflow:auto;font-size:.78rem;white-space:pre-wrap}
  .row{display:flex;gap:10px;align-items:center}
  .row>*{flex:1 1 auto}
  .muted{color:var(--muted)}
  .num{font-variant-numeric:tabular-nums}
  .card.wide{grid-column:1/-1}
  .linkrow{display:flex;flex-wrap:wrap;gap:10px}
  .linkbox{flex:1 1 110px;background:#09090c;border:1px solid var(--line);
           border-radius:10px;padding:10px 12px;display:flex;flex-direction:column;gap:2px}
  .linkbox b{font-size:.7rem;text-transform:uppercase;letter-spacing:.08em;
             color:var(--muted);font-weight:600}
  .linkbox span{font-size:1.15rem;font-weight:800}
  .linkbox.good span{color:var(--good)} .linkbox.bad span{color:var(--bad)}
  .linkbox.warn span{color:var(--warn)}
  #spark{width:100%;margin-top:14px;background:#09090c;border:1px solid var(--line);
         border-radius:10px}
  .legend{display:flex;align-items:center;gap:8px;margin-top:8px;font-size:.78rem;
          color:var(--muted)}
  .dot{width:9px;height:9px;border-radius:50%;display:inline-block}
  .dot.good{background:var(--good)} .dot.warn{background:var(--warn)}
  .dot.bad{background:var(--bad)}
  .diag{margin-top:12px;font-size:.85rem;padding:10px 12px;border-radius:10px;
        border:1px solid var(--line);background:#0d0d12}
  .diag.ok{border-color:#1f5b45} .diag.bad{border-color:#5b1f1f}
  .diag.warn{border-color:#5b4a1f}
</style>
</head>
<body>
<header>
  <h1>EVDCTR</h1>
  <span class="pill" id="p-role">controller</span>
  <span class="pill" id="p-ip">-</span>
  <span class="pill" id="p-link">link ?</span>
  <span class="pill" id="p-str">STR ?</span>
  <span class="pill" id="p-ver">-</span>
</header>

<nav>
  <button class="on" data-t="status">Status</button>
  <button data-t="audio">Audio</button>
  <button data-t="stream">Stream</button>
  <button data-t="network">Network</button>
  <button data-t="update">Update</button>
  <button data-t="log">Log</button>
</nav>

<main>
  <section class="on" id="t-status">
    <div class="grid">
      <div class="card"><h3>Capture</h3><div id="cap"></div></div>
      <div class="card"><h3>Encoder (core 1)</h3><div id="enc"></div></div>
      <div class="card"><h3>Pin link to EVDSTR</h3><div id="lnk"></div></div>
      <div class="card"><h3>Stream (EVDSTR)</h3><div id="str"></div></div>
      <div class="card"><h3>Memory</h3><div id="mem"></div></div>
    </div>
  </section>

  <section id="t-audio">
    <div class="grid">
      <div class="card">
        <h3>Master volume</h3>
        <input id="vol" type="range" min="0" max="100" step="1" value="100">
        <div class="row"><span class="muted num" id="volv">100</span>
          <button class="act ghost" id="mute">Mute</button></div>
        <h3 style="margin-top:16px">Playback mode</h3>
        <select id="mode"><option value="music">music</option>
          <option value="movie">movie</option><option value="off">off</option></select>
        <label class="row"><input type="checkbox" id="quiet" style="flex:0 0 auto">
          <span>Quiet mode (high-pass + limiter)</span></label>
      </div>
      <div class="card">
        <h3>Opus encoder</h3>
        <label>Bitrate <span class="muted num" id="brv"></span></label>
        <input id="br" type="range" min="6000" max="192000" step="1000">
        <label>Complexity <span class="muted num" id="cxv"></span></label>
        <input id="cx" type="range" min="0" max="10" step="1">
        <label>Signal</label>
        <select id="sig"><option value="music">music</option>
          <option value="voice">voice</option><option value="auto">auto</option></select>
        <label>Application</label>
        <select id="app"><option value="audio">audio</option>
          <option value="voip">voip</option><option value="lowdelay">lowdelay</option></select>
        <label>Encoder rate</label>
        <select id="rate"><option value="48000">48 kHz</option>
          <option value="24000">24 kHz</option></select>
        <div class="row" style="margin-top:14px">
          <button class="act" id="encsave">Save</button>
          <button class="act ghost" id="encrevert">Revert</button>
          <button class="act ghost" id="encdef">Defaults</button>
        </div>
      </div>
      <div class="card">
        <h3>Input source</h3>
        <div class="row">
          <button class="act ghost" data-src="BT">BT</button>
          <button class="act ghost" data-src="AUX">AUX</button>
          <button class="act ghost" data-src="USB">USB</button>
        </div>
        <div class="row" style="margin-top:10px">
          <button class="act ghost" data-cmd="next">Next</button>
          <button class="act ghost" data-cmd="prev">Prev</button>
        </div>
        <pre id="bt" style="margin-top:12px">BT devices: -</pre>
      </div>
    </div>
  </section>

  <section id="t-stream">
    <!-- Live pin-link monitor. This is the one thing the split added that a
         single board could not show: whether frames are actually crossing
         the four wires, and whether EVDSTR is taking them. -->
    <div class="card wide">
      <h3>Pin link &mdash; live</h3>
      <div class="linkrow">
        <div class="linkbox" id="lb-health"><b>link</b><span id="lv-health">?</span></div>
        <div class="linkbox"><b>frames sent</b><span id="lv-sent" class="num">-</span></div>
        <div class="linkbox"><b>fps</b><span id="lv-fps" class="num">-</span></div>
        <div class="linkbox"><b>last frame</b><span id="lv-age" class="num">-</span></div>
        <div class="linkbox"><b>dropped</b><span id="lv-drop" class="num">-</span></div>
        <div class="linkbox"><b>CS timeouts</b><span id="lv-cs" class="num">-</span></div>
        <div class="linkbox"><b>wire time</b><span id="lv-wire" class="num">-</span></div>
      </div>
      <canvas id="spark" height="72"></canvas>
      <div class="legend"><span class="dot good"></span>frames/s over the last 2 min
        <span class="muted" id="sparknote"></span></div>
      <div class="diag" id="diag"></div>
    </div>

    <div class="grid" style="margin-top:14px">
      <div class="card">
        <h3>EVDSTR address</h3>
        <p class="muted">Audio leaves this board over the pin link. Everything that
           reaches a player is EVDSTR's job, so zone and volume commands are relayed
           to it.</p>
        <input id="strhost" type="text" placeholder="192.168.1.42">
        <div class="row" style="margin-top:10px">
          <button class="act" id="pair">Pair</button>
          <button class="act ghost" id="refresh">Refresh</button>
        </div>
        <div id="pairstat" class="muted" style="margin-top:8px"></div>
      </div>
      <div class="card"><h3>Stream state (from EVDSTR)</h3><div id="strlive"></div></div>
      <div class="card"><h3>Players</h3><pre id="players">-</pre></div>
      <div class="card"><h3>Zones</h3><pre id="zones">-</pre></div>
      <div class="card">
        <h3>Buffer latency</h3>
        <label>Player buffer <span class="muted num" id="bufv"></span> ms</label>
        <input id="buf" type="range" min="20" max="600" step="10">
        <button class="act" id="bufapply">Apply to stream</button>
      </div>
    </div>
  </section>

  <section id="t-network">
    <div class="grid">
      <div class="card"><h3>Network</h3><div id="net"></div>
        <div class="row" style="margin-top:12px">
          <button class="act" id="nettest">Test</button>
          <button class="act ghost" id="restart">Restart</button>
        </div>
        <div id="nettestout" class="muted" style="margin-top:8px"></div>
      </div>
      <div class="card"><h3>Change Wi-Fi</h3>
        <label>Board name</label><input id="n-name" type="text">
        <label>SSID</label><input id="n-ssid" type="text">
        <label>Password</label><input id="n-pass" type="text">
        <button class="act" id="n-save">Save and restart</button>
      </div>
    </div>
  </section>

  <section id="t-update">
    <div class="grid">
      <div class="card"><h3>EVDCTR (this board)</h3>
        <p class="muted">Upload a .bin to the spool, then apply. Audio keeps
           running during the upload.</p>
        <input id="f-ctr" type="file" accept=".bin">
        <div class="row" style="margin-top:10px">
          <button class="act" id="up-ctr">Upload</button>
          <button class="act" id="ap-ctr">Apply and restart</button>
        </div>
      </div>
      <div class="card"><h3>EVDSTR (pushed over WiFi)</h3>
        <p class="muted">The STR image is spooled here and pushed to the paired
           EVDSTR, which flashes and restarts itself.</p>
        <input id="f-str" type="file" accept=".bin">
        <div class="row" style="margin-top:10px">
          <button class="act" id="up-str">Upload</button>
          <button class="act" id="ap-str">Push and apply</button>
        </div>
      </div>
      <div class="card"><h3>State</h3><pre id="otastate">-</pre></div>
    </div>
  </section>

  <section id="t-log"><pre id="log">-</pre></section>
</main>

<script>
const $ = s => document.querySelector(s);
let lastLogSeq = 0;

document.querySelectorAll('nav button').forEach(b => b.onclick = () => {
  document.querySelectorAll('nav button').forEach(x => x.classList.toggle('on', x === b));
  document.querySelectorAll('section').forEach(s =>
    s.classList.toggle('on', s.id === 't-' + b.dataset.t));
});

const kv = (o) => Object.entries(o)
  .map(([k, v]) => `<div class="kv"><span>${k}</span><span>${v}</span></div>`).join('');

async function j(url, opt) {
  const r = await fetch(url, opt);
  const t = await r.text();
  try { return JSON.parse(t); } catch (e) { return { raw: t }; }
}
const post = (url, body) => j(url, { method: 'POST',
  headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) });

async function tick() {
  const s = await j('/api/state');
  if (s.raw) return;
  $('#p-ip').textContent = s.ip + ' (' + s.rssi + ' dBm)';
  $('#p-ver').textContent = 'CTR ' + s.version;
  $('#p-link').textContent = s.linkHealthy ? 'link ok' : 'link DOWN';
  $('#p-link').className = 'pill ' + (s.linkHealthy ? 'ok' : 'bad');
  const d = s.downlink || {};
  $('#p-str').textContent = d.paired
    ? ('STR ' + (d.strIp || d.host) + (s.autoPaired ? ' (auto)' : ''))
    : 'STR searching\u2026';
  $('#p-str').className = 'pill ' + (d.paired && d.cached ? 'ok' : 'warn');

  $('#cap').innerHTML = kv({
    Source: s.source, Mode: s.mode, Volume: s.volume + (s.muted ? ' (muted)' : ''),
    'Source rate': (s.sourceSampleRate / 1000).toFixed(1) + ' kHz',
    DLNA: s.dlnaPlaying ? 'playing' : '-', USB: s.usbStreaming ? 'streaming' : '-',
    Uptime: Math.floor(s.upMs / 1000) + ' s'
  });
  $('#enc').innerHTML = kv({
    Rate: (s.opusSampleRate / 1000) + ' kHz',
    'Frame': s.opusFrameSamples + ' samples',
    Bitrate: Math.round(s.encBitrate / 1000) + ' kbps',
    Complexity: s.encComplexity,
    VBR: s.encVbr ? (s.encVbrConstraint ? 'constrained' : 'on') : 'off',
    Signal: s.encSignal, Application: s.encApplication
  });
  $('#lnk').innerHTML = kv({
    Healthy: s.linkHealthy ? 'yes' : 'no',
    Sent: s.linkSent, Dropped: s.linkDropped, 'CS timeouts': s.linkCsTimeouts
  });
  $('#str').innerHTML = kv({
    Paired: d.paired ? d.host : 'no',
    Subscribers: d.subscribers ?? '-',
    'Queue': (d.queueDepth ?? '-') + ' / ' + (d.queueTarget ?? '-'),
    'Pacer late': (d.pacerLateMs ?? '-') + ' ms',
    Version: d.strVersion || '-'
  });
  updateLink(s, d);
  $('#mem').innerHTML = kv({
    'Heap free': (s.heapFree / 1024).toFixed(1) + ' KB',
    'Heap min': (s.heapMinFree / 1024).toFixed(1) + ' KB',
    'PSRAM free': (s.psramFree / 1024).toFixed(0) + ' KB'
  });

  if (!$('#vol').matches(':active')) { $('#vol').value = s.volume; $('#volv').textContent = s.volume; }
  if (!$('#mode').matches(':active')) $('#mode').value = s.mode;
  $('#quiet').checked = !!s.quiet;

  const st = await j('/api/stats');
  if (!st.raw) {
    $('#log');
    window.__stats = st;
  }
}

// ---- Pin link monitor (Stream tab) ----
// Frames actually crossing the four wires, sampled once per tick. The rate is
// the honest one -- a delta of the encoder's own success counter -- so a link
// that is "up" but slow shows up as 12 fps, not as a green pill.
const SPARK_N = 60;              // 60 samples x 2 s = 2 min
const spark = [];                // frames/s history
let lastSent = null, lastSentAt = 0;

function updateLink(s, d) {
  const sent = s.linkSent || 0;
  const now = performance.now();
  let fps = null;
  if (lastSent !== null && now - lastSentAt > 500) {
    fps = (sent - lastSent) * 1000 / (now - lastSentAt);
    lastSentAt = now;
  }
  if (lastSent === null) lastSentAt = now;
  lastSent = sent;

  const healthy = !!s.linkHealthy;
  const hb = $('#lb-health');
  hb.className = 'linkbox ' + (healthy ? 'good' : 'bad');
  $('#lv-health').textContent = healthy ? 'up' : 'DOWN';
  $('#lv-sent').textContent = sent;
  $('#lv-drop').textContent = s.linkDropped || 0;
  $('#lv-cs').textContent = s.linkCsTimeouts || 0;

  if (fps !== null) {
    spark.push(fps);
    if (spark.length > SPARK_N) spark.shift();
  }
  $('#lv-fps').textContent = fps === null ? '-' : fps.toFixed(1);

  // "last frame" is derived from the sent counter, not from a server clock:
  // the counter stops moving the instant the encoder stops handing frames over.
  $('#lv-age').textContent = healthy ? 'now' : (fps === 0 ? '>' + ((s.linkDropped|0) ? 'gap' : '?') : '?');
  $('#lv-wire').textContent = s.linkLastFrameUs ? (s.linkLastFrameUs / 1000).toFixed(2) + ' ms' : '-';

  drawSpark();

  // One line of diagnosis, in the order a person would check them.
  const diag = $('#diag');
  let cls = 'ok', msg = 'Frames are crossing the link and EVDSTR is clocking them.';
  if (!healthy && (s.linkCsTimeouts || 0) > 0) {
    cls = 'bad';
    msg = 'EVDSTR is not clocking: check that it is powered and flashed, that ' +
          'GPIO11/12/13/14 match on both boards, and that a ground wire runs ' +
          'between them. CS timeouts count frames that were offered and refused.';
  } else if (!healthy && (s.linkDropped || 0) > 0 && !(s.linkCsTimeouts || 0)) {
    cls = 'warn';
    msg = 'Frames are timing out after the transfer started. The link is wired ' +
          'but EVDSTR is not finishing the frame - a long or noisy ribbon, or a ' +
          'slave task being starved.';
  } else if (!d || !d.paired) {
    cls = 'warn';
    msg = 'No EVDSTR paired yet. This board broadcasts a discovery probe every ' +
          '3 s (' + (s.discoveryProbes || 0) + ' sent). Both boards must be on the ' +
          'same LAN with no client isolation between them - guest WiFi blocks ' +
          'this. You can also pair by hand on the Stream tab.';
  } else if (!healthy) {
    cls = 'warn';
    msg = 'No frame has crossed yet. Start audio (a source selected, not muted, ' +
          'mode not off) and watch this line.';
  } else if (d && d.paired && d.cached && (d.subscribers || 0) === 0) {
    cls = 'warn';
    msg = 'The link is healthy but EVDSTR has no players. Check that the players ' +
          'point at EVDSTR, not at this board.';
  }
  diag.className = 'diag ' + cls;
  diag.textContent = msg;

  // The link panel is also fed by /api/link/state, which carries the pin-link
  // counters the Stream tab's monitor needs even when /api/state is trimmed.
  j('/api/link/state').then(l => { if (!l.raw) window.__link = l; });

  if (d) {
    $('#strlive').innerHTML = kv({
      'EVDSTR': d.paired ? (d.strIp || d.host) : 'not paired',
      'Paired by': d.paired ? (s.autoPaired ? 'discovery' : 'typed') : '-',
      'Probes sent': s.discoveryProbes ?? '-',
      'Cached': d.cached ? 'yes' : 'no',
      Subscribers: d.subscribers ?? '-',
      'Queue': (d.queueDepth ?? '-') + ' / ' + (d.queueTarget ?? '-'),
      'Pacer late': (d.pacerLateMs ?? '-') + ' ms',
      Version: d.strVersion || '-'
    });
  }
}

function drawSpark() {
  const c = $('#spark');
  const w = c.clientWidth || 600, h = c.height;
  if (c.width !== w) c.width = w;
  const g = c.getContext('2d');
  g.clearRect(0, 0, w, h);
  // 50 fps reference line
  g.strokeStyle = '#2a2a34'; g.lineWidth = 1;
  g.beginPath(); g.moveTo(0, h * 0.25); g.lineTo(w, h * 0.25); g.stroke();
  if (spark.length < 2) { $('#sparknote').textContent = 'collecting...'; return; }
  $('#sparknote').textContent = 'peak ' + Math.max(...spark).toFixed(0) + ' fps';
  const step = w / (SPARK_N - 1);
  const y = v => h - Math.min(v / 60, 1) * (h - 4) - 2;
  g.strokeStyle = '#37dba0'; g.lineWidth = 2; g.beginPath();
  spark.forEach((v, i) => {
    const x = (SPARK_N - spark.length + i) * step;
    if (i === 0) g.moveTo(x, y(v)); else g.lineTo(x, y(v));
  });
  g.stroke();
  g.fillStyle = 'rgba(55,219,160,.12)';
  g.lineTo(w, h); g.lineTo((SPARK_N - spark.length) * step, h); g.closePath(); g.fill();
}

// ---- Audio tab ----
$('#vol').oninput = e => $('#volv').textContent = e.target.value;
$('#vol').onchange = e => post('/api/config', { masterVolume: +e.target.value });
$('#mute').onclick = async () => {
  const s = await j('/api/state'); post('/api/config', { masterMuted: !s.muted });
};
$('#mode').onchange = e => post('/api/config', { mode: e.target.value });
$('#quiet').onchange = e => post('/api/config', { quietMode: e.target.checked });

async function loadEnc() {
  const e = await j('/api/audio/encoding');
  if (e.raw) return;
  $('#br').value = e.bitrate; $('#brv').textContent = Math.round(e.bitrate / 1000) + ' kbps';
  $('#cx').value = e.complexity; $('#cxv').textContent = e.complexity;
  $('#sig').value = e.signal; $('#app').value = e.application;
  $('#rate').value = String(e.sampleRate);
  $('#buf').value = e.bufferMs; $('#bufv').textContent = e.bufferMs;
}
$('#br').oninput = e => $('#brv').textContent = Math.round(e.target.value / 1000) + ' kbps';
$('#cx').oninput = e => $('#cxv').textContent = e.target.value;
$('#encsave').onclick = () => post('/api/audio/encoding', {
  bitrate: +$('#br').value, complexity: +$('#cx').value, signal: $('#sig').value,
  application: $('#app').value, sampleRate: +$('#rate').value, save: true
});
$('#encrevert').onclick = () => post('/api/audio/encoding/revert').then(loadEnc);
$('#encdef').onclick = () => post('/api/audio/encoding/defaults').then(loadEnc);
document.querySelectorAll('[data-src]').forEach(b => b.onclick =
  () => post('/api/source', { source: b.dataset.src }));
document.querySelectorAll('[data-cmd]').forEach(b => b.onclick =
  () => post('/api/cmd', { cmd: b.dataset.cmd }));

// ---- Stream tab ----
$('#pair').onclick = async () => {
  const r = await post('/api/link/pair', { host: $('#strhost').value.trim() });
  $('#pairstat').textContent = r.ok ? 'Paired with ' + r.host : (r.error || 'failed');
};
$('#refresh').onclick = async () => {
  $('#players').textContent = JSON.stringify(await j('/api/link/players'), null, 1);
  $('#zones').textContent = JSON.stringify(await j('/api/link/zones'), null, 1);
};
$('#buf').oninput = e => $('#bufv').textContent = e.target.value;
$('#bufapply').onclick = () => post('/api/config', { audioBufferMs: +$('#buf').value });

// ---- Network tab ----
async function loadNet() {
  const n = await j('/api/network/state');
  if (n.raw) return;
  $('#net').innerHTML = kv({ Name: n.name, SSID: n.ssid, IP: n.ip,
    'AP mode': n.ap ? 'yes' : 'no', RSSI: n.rssi + ' dBm' });
  $('#n-name').value = n.name; $('#n-ssid').value = n.ssid;
}
$('#nettest').onclick = async () => {
  $('#nettestout').textContent = 'testing...';
  await j('/api/network-test?start=1');
  for (let i = 0; i < 20; i++) {
    await new Promise(r => setTimeout(r, 500));
    const t = await j('/api/network-test');
    if (t.done) { $('#nettestout').textContent = (t.ok ? 'reachable' : 'no route') + ' in ' + t.ms + ' ms'; return; }
  }
  $('#nettestout').textContent = 'timed out';
};
$('#restart').onclick = () => post('/api/restart');
$('#n-save').onclick = () => post('/save', { name: $('#n-name').value,
  ssid: $('#n-ssid').value, password: $('#n-pass').value });

// ---- Update tab ----
async function upload(target, input) {
  const f = input.files[0];
  if (!f) return;
  const fd = new FormData(); fd.append('update', f, f.name);
  $('#otastate').textContent = 'uploading ' + target + '...';
  const r = await fetch('/api/ota/upload?target=' + target, { method: 'POST', body: fd });
  $('#otastate').textContent = await r.text();
}
$('#up-ctr').onclick = () => upload('ctr', $('#f-ctr'));
$('#up-str').onclick = () => upload('str', $('#f-str'));
$('#ap-ctr').onclick = async () => { $('#otastate').textContent = 'applying...';
  await post('/api/ota/apply?target=ctr'); };
$('#ap-str').onclick = async () => { $('#otastate').textContent = 'pushing to EVDSTR...';
  $('#otastate').textContent = JSON.stringify(await post('/api/ota/apply?target=str')); };

// ---- Log tab ----
async function pollLog() {
  const r = await j('/api/log/tail?since=' + lastLogSeq);
  if (!r.raw && r.lines && r.lines.length) {
    lastLogSeq = r.nextSeq;
    const el = $('#log');
    el.textContent += r.lines.map(l => `[${(l.ms/1000).toFixed(1)}] ${l.level} ${l.text}`).join('\n') + '\n';
    el.scrollTop = el.scrollHeight;
  }
}

tick(); loadEnc(); loadNet(); pollLog();
setInterval(tick, 2000);
setInterval(pollLog, 2000);
</script>
</body>
</html>
)HTML";
