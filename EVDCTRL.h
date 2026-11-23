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
  /* Static-IP block on the Network tab: a tick box that reveals four fields. */
  label.chk{display:flex;align-items:center;gap:8px;margin-top:14px;cursor:pointer}
  label.chk input{width:auto;margin:0;flex:0 0 auto}
  #n-staticbox{margin-top:2px}
  #n-staticbox.off{display:none}
  .hint{font-size:.78rem;margin:4px 0 0}
  .hint.bad{color:var(--bad)} .hint.ok{color:var(--good)}
  .sugg{float:right;font-weight:400;font-size:.72rem;color:var(--accent);
        text-transform:none;letter-spacing:0;cursor:pointer}
  .sugg:hover{text-decoration:underline}
  button.act.small{margin-top:8px;padding:6px 12px;font-size:.8rem}
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
  /* The error box the script writes into after a failed request. */
  #err{display:none;margin:0 22px 14px;padding:10px 12px;border-radius:10px;
       border:1px solid #5b1f1f;background:#1a0d0d;color:#ffb9b9;font-size:.85rem}
  #err.on{display:block}
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

<div id="err"></div>

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
        <label class="chk"><input type="checkbox" id="n-static">
          <span>Use a static IP</span></label>
        <p class="muted hint" id="n-iphint"></p>
        <div id="n-staticbox">
          <label>IP address</label>
          <input id="n-ip" type="text" inputmode="decimal" autocomplete="off"
                 placeholder="192.168.1.50">
          <div class="muted hint" id="n-ipmsg"></div>
          <label>Gateway <span class="sugg" id="n-gwsugg"></span></label>
          <input id="n-gw" type="text" inputmode="decimal" autocomplete="off">
          <label>Subnet mask <span class="sugg" id="n-snsugg"></span></label>
          <input id="n-sn" type="text" inputmode="decimal" autocomplete="off">
          <label>DNS server <span class="sugg" id="n-dnssugg"></span></label>
          <input id="n-dns" type="text" inputmode="decimal" autocomplete="off">
          <button class="act ghost" id="n-suggest" type="button">Suggest from current network</button>
        </div>
        <button class="act" id="n-save">Save and restart</button>
        <div class="muted hint" id="n-savemsg"></div>
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

{{IPSUGGEST}}

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

// ---- Error surface ---------------------------------------------------------
// Every command used to fire and forget: a 500 from the board, an unreachable
// EVDSTR, or a request cut off by the board restarting left no trace on the
// page. err() puts the reason where the operator is looking, and clears itself
// when the next request succeeds.
let errTimer = null;
function err(e) {
  const box = $('#err');
  if (!box) { console.warn(e); return; }
  box.textContent = (e && e.message) ? e.message : String(e);
  box.className = 'on';
  clearTimeout(errTimer);
  errTimer = setTimeout(() => { box.className = ''; }, 8000);
}

// A fire-and-forget POST that still reports a failure.
//   quiet=true means "a missing EVDSTR is expected here" (the stream may be
//   paired or not); the board answers 200 {applied:"ctr-only"} in that case.
const txt = (url, body, quiet) => post(url, body)
  .then(r => { if (r && r.ok === false && !quiet) err(r.error || 'request failed'); })
  .catch(e => { if (!quiet) err(e); });

// The board turns a server name into an mDNS host: letters and digits only.
const sanitizeHost = v => String(v || '').replace(/[^A-Za-z0-9]/g, '');

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

  // Every number below is `?? '-'`: a firmware that does not publish a field
  // yet must show a dash, not "NaN kHz".
  const kb = n => (typeof n === 'number' ? (n / 1000).toFixed(1) + ' kHz' : '-');
  $('#cap').innerHTML = kv({
    Source: s.source ?? '-', Mode: s.mode ?? '-',
    Volume: (s.volume ?? '-') + (s.muted ? ' (muted)' : ''),
    'Source rate': kb(s.sourceSampleRate),
    DLNA: s.dlnaPlaying ? 'playing' : '-',
    USB: s.usbStreaming ? 'streaming'
         : (s.source === 'USB'
            ? ('waiting for host' + (s.usbError ? ' (' + s.usbError + ')' : ''))
            : '-'),
    'USB host vol': s.usbHostMuted ? 'muted'
                    : (typeof s.usbHostVolumeDb === 'number' ? s.usbHostVolumeDb + ' dB' : '-'),
    Uptime: typeof s.upMs === 'number' ? Math.floor(s.upMs / 1000) + ' s' : '-'
  });
  $('#enc').innerHTML = kv({
    Rate: kb(s.opusSampleRate),
    'Frame': (s.opusFrameSamples ?? '-') + ' samples',
    Bitrate: typeof s.encBitrate === 'number' ? Math.round(s.encBitrate / 1000) + ' kbps' : '-',
    Complexity: s.encComplexity ?? '-',
    VBR: s.encVbr ? (s.encVbrConstraint ? 'constrained' : 'on') : 'off',
    Signal: s.encSignal ?? '-', Application: s.encApplication ?? '-'
  });
  const lastFrameAge = (typeof s.linkLastOkMs === 'number' && typeof s.upMs === 'number')
    ? Math.max(0, Math.round((s.upMs - s.linkLastOkMs) / 100)) / 10 : null;
  $('#lnk').innerHTML = kv({
    Healthy: s.linkHealthy ? 'yes' : 'no',
    Sent: s.linkSent ?? '-', Dropped: s.linkDropped ?? '-',
    'CS timeouts': s.linkCsTimeouts ?? '-',
    'Last frame': lastFrameAge === null ? '-' :
                  (lastFrameAge < 0.2 ? 'now' : lastFrameAge + ' s ago')
  });
  $('#str').innerHTML = kv({
    Paired: d.paired ? (d.host || d.strIp) : 'no',
    Subscribers: d.subscribers ?? '-',
    'Queue': (d.queueDepth ?? '-') + ' / ' + (d.queueTarget ?? '-'),
    'Pacer late': (d.pacerLateMs ?? '-') + ' ms',
    Version: d.strVersion || '-'
  });
  updateLink(s, d);
  const kbi = n => (typeof n === 'number' ? (n / 1024).toFixed(n > 1024 * 100 ? 0 : 1) + ' KB' : '-');
  $('#mem').innerHTML = kv({
    'Heap free': kbi(s.heapFree),
    'Heap min': kbi(s.heapMinFree),
    'PSRAM free': kbi(s.psramFree)
  });

  // Only write a control back when nothing is being dragged in it: a range
  // input that is `:active` is under the finger, and a select that is open on
  // iOS is not :active at all, so check focus as well.
  const idle = sel => { const el = $(sel); return el && !el.matches(':active') &&
                                                        document.activeElement !== el; };
  // A board that has not answered yet sends `s` as {} (j() falls back to
  // {raw: text}); writing undefined into a range input blanks it, so every
  // mirror is guarded on the value actually being a number.
  if (idle('#vol') && typeof s.volume === 'number') {
    $('#vol').value = s.volume; $('#volv').textContent = s.volume;
  }
  if (idle('#mode')) $('#mode').value = s.mode ?? 'music';
  if ($('#quiet')) $('#quiet').checked = !!s.quiet;

  // The volume slider mirrors the stream's volume, but a change made on the
  // Audio tab must reach EVDSTR's players. That relay happens in /api/config,
  // so nothing extra is needed here.
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
// on(): wire a control if it is on the page. Writing `$('#x').onclick` for an
// id that a trimmed dashboard does not carry throws a TypeError that stops the
// rest of this script - silently, because a chunked response has already been
// committed. Losing one control must not cost the other twenty.
const on = (sel, ev, fn) => { const el = $(sel); if (el) el.addEventListener(ev, fn); };
on('#vol', 'input', e => $('#volv').textContent = e.target.value);
on('#vol', 'change', e => txt('/api/config', { masterVolume: +e.target.value }));
// The button reads the live state so it toggles what is actually there, not a
// stale copy; a failed read leaves it doing nothing rather than guessing.
on('#mute', 'click', async () => {
  try {
    const s = await j('/api/state');
    if (s.raw) { err('board did not answer'); return; }
    txt('/api/config', { masterMuted: !s.muted }, true);
  } catch (e) { err(e); }
});
on('#mode', 'change', e => txt('/api/config', { mode: e.target.value }));
// Quiet mode is CTR-only DSP: the field is `quietMode` in the handler's JSON
// and `quiet` in /api/state that tick() reads back.
on('#quiet', 'change', e => txt('/api/config', { quietMode: e.target.checked }));

async function loadEnc() {
  const e = await j('/api/audio/encoding');
  if (e.raw) return;
  // Only mirror a control that is not being used: loadEnc() also runs after
  // every save, and overwriting a slider mid-drag jumps it back.
  const focusId = document.activeElement ? document.activeElement.id : '';
  const set = (id, v, out, fmt) => {
    if (v === undefined || v === null || focusId === id) return;
    const el = $('#' + id); if (el) el.value = v;
    if (out) { const o = $('#' + out); if (o) o.textContent = fmt ? fmt(v) : v; }
  };
  set('br', e.bitrate, 'brv', v => Math.round(v / 1000) + ' kbps');
  set('cx', e.complexity, 'cxv');
  set('sig', e.signal); set('app', e.application);
  set('rate', e.sampleRate === undefined ? undefined : String(e.sampleRate));
  set('buf', e.bufferMs, 'bufv', v => v + ' ms');
}
on('#br', 'input', e => $('#brv').textContent = Math.round(e.target.value / 1000) + ' kbps');
on('#cx', 'input', e => $('#cxv').textContent = e.target.value);
// The encoder form sends the whole set, so the selects are always current.
// The buffer slider is NOT part of it: it applies with its own button.
on('#encsave', 'click', () => post('/api/audio/encoding', {
  bitrate: +$('#br').value, complexity: +$('#cx').value, signal: $('#sig').value,
  application: $('#app').value, sampleRate: +$('#rate').value, save: true
}).then(loadEnc).catch(err));
on('#encrevert', 'click', () => post('/api/audio/encoding/revert').then(loadEnc).catch(err));
on('#encdef', 'click', () => post('/api/audio/encoding/defaults').then(loadEnc).catch(err));
// Source and transport buttons go through the CTR -> receiver UART bridge, so
// a 502 here means the receiver board did not answer, which the operator needs
// to see (USB also answers 501 when it is not built into the firmware).
document.querySelectorAll('[data-src]').forEach(b => b.onclick = async () => {
  const r = await post('/api/source', { source: b.dataset.src }).catch(e => ({ error: e.message }));
  if (!r.ok) err(r.error || 'source switch refused');
});
document.querySelectorAll('[data-cmd]').forEach(b => b.onclick = async () => {
  const r = await post('/api/cmd', { cmd: b.dataset.cmd }).catch(e => ({ error: e.message }));
  if (!r.ok) err(r.error || 'command refused');
});

// ---- Stream tab ----
on('#pair', 'click', async () => {
  $('#pairstat').textContent = 'checking...';
  try {
    const r = await post('/api/link/pair', { host: $('#strhost').value.trim() });
    $('#pairstat').textContent = r.ok ? 'Paired with ' + r.host : (r.error || 'failed');
    if (!r.ok) err('pairing failed: ' + (r.error || 'no answer'));
  } catch (e) { err(e); }
});
on('#refresh', 'click', async () => {
  try {
    $('#players').textContent = JSON.stringify(await j('/api/link/players'), null, 1);
    $('#zones').textContent = JSON.stringify(await j('/api/link/zones'), null, 1);
  } catch (e) { err('EVDSTR unreachable: ' + e.message); }
});
on('#buf', 'input', e => $('#bufv').textContent = e.target.value);
on('#bufapply', 'click', () => txt('/api/config', { audioBufferMs: +$('#buf').value }, true));

// ---- Network tab ----
async function loadNet() {
  const n = await j('/api/network/state');
  if (!n || n.raw) { $('#net').innerHTML = kv({ Status: 'unreachable' }); return; }
  const ap = (n.ap === true || n.ap === 'true');
  $('#net').innerHTML = kv({ Name: n.name, SSID: n.ssid || '-', IP: n.ip,
    'AP mode': ap ? 'yes' : 'no', RSSI: n.rssi + ' dBm',
    Static: (n.static === true || n.static === 'true') ? (n.gw || 'yes') : 'no' });
  // Typing in the fields must not be overwritten by the 2 s refresh.
  const focusId = document.activeElement ? document.activeElement.id : '';
  if (focusId !== 'n-name') $('#n-name').value = n.name || '';
  if (focusId !== 'n-ssid') $('#n-ssid').value = n.ssid || '';
  fillStaticFields(n);
}

// ---- Static IP ------------------------------------------------------------
// The tick box reveals the four fields. The IP field drives the rest: as it is
// typed, gateway / subnet / DNS are offered as placeholders and filled in when
// they are empty (or when they are still the suggestion last offered, so the
// operator's own edit is never overwritten). evdSuggest()/evdIpCheck() live in
// EVDIpSuggest.h, which the board injects into this page.
// Loaded once: every later loadNet() would otherwise reset the tick box and the
// fields to the saved values while the operator is still editing them.
let netLoaded = false;
const staticOn = () => $('#n-static').checked;
function suggestNow() { return evdSuggest($('#n-ip').value.trim()); }
function showStatic() {
  $('#n-staticbox').className = staticOn() ? '' : 'off';
}
// Write a value into a field unless the operator has put something of their own
// there. `lastOffered` remembers what we filled in last time.
const lastOffered = { gw: '', sn: '', dns: '' };
function fillField(id, key, v) {
  const el = $(id);
  if (!v) return;
  if (!el.value.trim() || el.value.trim() === lastOffered[key]) {
    el.value = v; lastOffered[key] = v;
  }
}
function applySuggestion(force) {
  const s = suggestNow();
  // The placeholders and the inline suggestions are the authority; they are
  // updated even when a field holds the operator's own value.
  $('#n-gw').placeholder  = s.gw;
  $('#n-sn').placeholder  = s.mask;
  $('#n-dns').placeholder = s.dns;
  $('#n-gwsugg').textContent  = s.gw;
  $('#n-snsugg').textContent  = s.mask;
  $('#n-dnssugg').textContent = s.dns;
  // Fill a field when it is empty, when it already equals the suggestion (so
  // "Suggest from current network" is an idempotent no-op), or when forced.
  const stale = (id, v) => force || !$(id).value.trim() || $(id).value.trim() === v;
  if (stale('#n-gw',  s.gw))   fillField('#n-gw',  'gw',  s.gw);
  if (stale('#n-sn',  s.mask)) fillField('#n-sn',  'sn',  s.mask);
  if (stale('#n-dns', s.dns))  fillField('#n-dns', 'dns', s.dns);
  const msg = evdIpCheck($('#n-ip').value.trim(), $('#n-gw').value.trim(),
                         $('#n-sn').value.trim(), $('#n-dns').value.trim());
  const box = $('#n-ipmsg');
  box.textContent = msg || (s.usable ? '' : 'A mask this tight leaves no room for a gateway.');
  box.className = 'hint ' + ((msg || !s.usable) ? 'bad' : 'ok');
  return !msg && s.usable;
}
// First time the tab sees a board state: put in the saved values, or the
// suggestion when nothing was saved yet. Never while the operator is typing.
function fillStaticFields(n) {
  if (netLoaded) return;
  const focusId = document.activeElement ? document.activeElement.id : '';
  if (focusId.indexOf('n-') === 0 && focusId !== 'n-name' && focusId !== 'n-ssid')
    return;
  netLoaded = true;
  const st = (n.static === true || n.static === 'true');
  $('#n-static').checked = st;
  showStatic();
  if (!st) return;
  const put = (id, v) => { if (v && !$(id).value.trim()) $(id).value = v; };
  put('#n-ip', n.ip_static);
  put('#n-gw', n.gw);
  put('#n-sn', n.sn);
  put('#n-dns', n.dns);
  applySuggestion(false);
}
on('#n-static', 'change', () => {
  showStatic();
  if (staticOn() && !$('#n-ip').value.trim()) {
    $('#n-ip').value = evdSuggest('').ip || '';
    if (!$('#n-ip').value) $('#n-ip').value = (window.EVD_IP_PRESET || {}).ip || '';
  }
  if (staticOn()) applySuggestion(true);
});
on('#n-ip', 'input', () => applySuggestion(false));
['#n-gw', '#n-sn', '#n-dns'].forEach(id =>
  on(id, 'input', () => applySuggestion(false)));
on('#n-suggest', 'click', () => applySuggestion(true));
on('#nettest', 'click', async () => {
  $('#nettestout').textContent = 'testing...';
  await j('/api/network-test?start=1');
  for (let i = 0; i < 20; i++) {
    await new Promise(r => setTimeout(r, 500));
    const t = await j('/api/network-test');
    if (t.done) { $('#nettestout').textContent = (t.ok ? 'reachable' : 'no route') + ' in ' + t.ms + ' ms'; return; }
  }
  $('#nettestout').textContent = 'timed out';
});
on('#restart', 'click', () => txt('/api/restart', {}));
// The handler reads `password`; the AP setup page uses the same name. An empty
// field with an unchanged SSID means "keep the stored password".
on('#n-save', 'click', async () => {
  if (staticOn() && !applySuggestion(false)) {
    $('#n-savemsg').textContent = 'Fix the static IP settings first.';
    $('#n-savemsg').className = 'hint bad';
    return;
  }
  $('#n-savemsg').textContent = 'saving...';
  const body = { name: $('#n-name').value,
    ssid: $('#n-ssid').value, password: $('#n-pass').value };
  if (staticOn()) {
    body.static   = true;
    body.ip       = $('#n-ip').value.trim();
    body.gateway  = $('#n-gw').value.trim();
    body.subnet   = $('#n-sn').value.trim();
    body.dns      = $('#n-dns').value.trim();
  }
  const r = await post('/save', body).catch(e => ({ error: String(e) }));
  if (r && r.ok === false) {
    // The board refuses an unusable static configuration instead of saving it
    // and disappearing; keep the operator on the page with the reason.
    $('#n-savemsg').textContent = r.error || 'save failed';
    $('#n-savemsg').className = 'hint bad';
    return;
  }
  // The board restarts itself 200 ms after answering; say so rather than
  // leaving the page looking as if nothing happened.
  $('#n-savemsg').textContent = 'saved; the board restarts in a moment';
  $('#nettestout').textContent = 'saving... the board restarts in a moment, ' +
    'then reload http://' + (sanitizeHost($('#n-name').value) || 'evdctr') + '.local/';
});

// ---- Update tab ----
async function upload(target, input) {
  const f = input && input.files ? input.files[0] : null;
  if (!f) { $('#otastate').textContent = 'choose a .bin first'; return; }
  const fd = new FormData(); fd.append('update', f, f.name);
  $('#otastate').textContent = 'uploading ' + target + ' (' +
    (f.size / 1024).toFixed(0) + ' KB)...';
  try {
    const r = await fetch('/api/ota/upload?target=' + target, { method: 'POST', body: fd });
    $('#otastate').textContent = await r.text();
  } catch (e) {
    // An image for the wrong target, or a dropped link: say so instead of
    // leaving "uploading..." on the screen for ever.
    $('#otastate').textContent = 'upload failed: ' + e;
  }
}
on('#up-ctr', 'click', () => upload('ctr', $('#f-ctr')));
on('#up-str', 'click', () => upload('str', $('#f-str')));
on('#ap-ctr', 'click', async () => {
  $('#otastate').textContent = 'applying... the board restarts when it is done';
  try {
    const r = await post('/api/ota/apply?target=ctr');
    $('#otastate').textContent = JSON.stringify(r);
  } catch (e) {
    // The connection drops the moment the board restarts - that is success,
    // not an error.
    $('#otastate').textContent = 'board restarting (connection closed)';
  }
});
on('#ap-str', 'click', async () => {
  $('#otastate').textContent = 'pushing to EVDSTR... (this can take a minute)';
  try {
    const r = await post('/api/ota/apply?target=str');
    $('#otastate').textContent = JSON.stringify(r);
  } catch (e) { err('STR push failed: ' + e.message); }
});

// ---- Log tab ----
// A full board answers `since=<seq>` with only the new lines, so the page can
// stay open for days. `nextSeq` is what the next poll must ask for; if the
// board restarted, its sequence starts again at 1 and the page would sit there
// waiting for a seq it will never see - so a nextSeq that went backwards
// resets the view instead of freezing it.
async function pollLog() {
  const r = await j('/api/log/tail?since=' + lastLogSeq);
  if (r.raw) return;
  if (typeof r.nextSeq === 'number' && r.nextSeq < lastLogSeq) {
    lastLogSeq = 0; $('#log').textContent = ''; return;
  }
  if (r.lines && r.lines.length) {
    lastLogSeq = r.nextSeq;
    const el = $('#log');
    if (!el) return;
    el.textContent += r.lines.map(l =>
      `[${(l.ms / 1000).toFixed(1)}] ${l.level} ${l.text}`).join('\n') + '\n';
    // Keep the buffer bounded: a board logging at 100 lines/s would otherwise
    // grow the DOM until the tab dies.
    if (el.textContent.length > 200000) el.textContent = el.textContent.slice(-150000);
    el.scrollTop = el.scrollHeight;
  }
}

// Everything above defines its handlers at top level. The pollers are started
// last so the first tick() runs with every control already wired, and the
// promise it returns is caught: an unhandled rejection here (STR unreachable,
// board rebooting after an OTA) used to surface as a frozen dashboard with
// nothing in the console.
function safeStart(fn) { return fn().catch(e => console.warn('startup', e)); }
safeStart(tick);
safeStart(loadEnc);
safeStart(loadNet);
safeStart(pollLog);
setInterval(() => { tick().catch(e => console.warn('tick', e)); }, 2000);
setInterval(() => { pollLog().catch(e => {}); }, 2000);
setInterval(() => { loadNet().catch(e => {}); }, 5000);
</script>
</body>
</html>
)HTML";
