#pragma once
// ============================================================================
//  EVDNTW.h - standalone network-setup page served at GET /settings
// ============================================================================
//  Shown when the board is in AP fallback (no STA credentials, or they
//  failed). Self-contained: no dashboard assets, no fetch of the main page.
// ============================================================================

static const char EVDNTW_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EVDCTR Network Setup</title>
<style>
  :root{--bg:#050507;--panel:#111116;--panel2:#1a1a21;--line:#32323c;
        --text:#f6f6f8;--muted:#a1a1ad;--accent:#9d7bff;--accenttext:#140b2c}
  body{font:15px system-ui,sans-serif;margin:0;padding:24px;color:var(--text);
       background:radial-gradient(circle at top right,#2a1b52,transparent 28rem),var(--bg)}
  form{max-width:34rem;margin:5vh auto;padding:26px;border:1px solid var(--line);
       border-radius:20px;background:linear-gradient(145deg,var(--panel2),var(--panel));
       box-shadow:0 24px 60px #0008}
  h1{font-size:2rem;letter-spacing:-.05em;margin:0}
  p{color:var(--muted);line-height:1.5}
  label,input,button{display:block;width:100%;box-sizing:border-box}
  label{margin-top:1rem;font-weight:700}
  input,button{padding:.8rem;font-size:1rem;border-radius:11px}
  input{margin-top:7px;color:var(--text);background:#09090c;border:1px solid #343440}
  input:focus{outline:none;border-color:var(--accent)}
  button{margin-top:1.5rem;border:0;background:var(--accent);color:var(--accenttext);
         font-weight:800;cursor:pointer}
  small{color:var(--muted);display:block;margin-top:6px}
  .row{display:flex;gap:8px}
  .row input{flex:1 1 auto;min-width:0}
  .row button{flex:0 0 auto;width:auto;padding:0 18px;margin-top:7px;background:var(--panel2);
              color:var(--text);border:1px solid var(--line);font-weight:700}
  .chk{display:flex;gap:.6rem;align-items:center;margin-top:1rem}
  .chk input{width:auto;margin:0}
  .hidden{display:none!important}
  #status{margin-top:1rem;color:var(--muted)}

    /* ==================================================================
       CLEAN THEME (palette pass) - matches the PC/server dashboards:
       deep indigo base, hairline surfaces, pill controls.
       ================================================================== */
    :root {
      --bg: #07070f;
      --panel: rgba(255,255,255,0.055);
      --line: rgba(255,255,255,0.10);
      --text: #f4f5fb;
      --muted: #b3b9d1;
      --accent: #9db6ff;
      --accenttext: #0b0a16;
      --good: #4ade80;
      --hair: rgba(255,255,255,0.10);
      --hair-strong: rgba(255,255,255,0.16);
    }
    body { background-color: #07070f; color: var(--text); }
    .card {
      background: var(--panel) !important;
      border: 1px solid var(--line) !important;
      border-radius: 18px !important;
      box-shadow: none !important;
    }
    .pill {
      background: rgba(255,255,255,0.06) !important;
      border: 1px solid var(--line) !important;
      border-radius: 999px !important;
    }
    button, .btn, .tab {
      border-radius: 999px !important;
      border: 1px solid var(--line) !important;
      background: rgba(255,255,255,0.06) !important;
      color: var(--text) !important;
      font-weight: 550 !important;
    }
    button:hover, .btn:hover, .tab:hover { background: rgba(255,255,255,0.12) !important; }
    button.active, .tab.active, .btn.active {
      background: rgba(255,255,255,0.16) !important; color: #fff !important;
    }
    input, select, textarea {
      background: rgba(0,0,0,0.25) !important;
      border: 1px solid var(--line) !important;
      border-radius: 10px !important;
      color: var(--text) !important;
    }

</style>
</head>
<body>
<form id="f">
  <h1>EVDCTR Network Setup</h1>
  <p>Connect the controller to your Wi-Fi. The access point stays up at 192.168.4.1
     until the board joins a network.</p>

  <label for="name">Board name</label>
  <input id="name" maxlength="32" pattern="[A-Za-z0-9]+" value="{{NAME}}">
  <small>Letters and numbers only; becomes the .local address.</small>

  <label for="ssid">Wi-Fi SSID</label>
  <div class="row">
    <input id="ssid" required value="{{SSID}}">
    <button type="button" onclick="scan()">Scan</button>
  </div>

  <label for="pass">Wi-Fi password</label>
  <input id="pass" type="password" value="{{PASS}}">

  <label class="chk"><input id="static" type="checkbox" {{STATIC}}> Use a static IP</label>
  <div id="staticBox" class="{{STATICCLASS}}">
    <label for="ip">IP address</label><input id="ip" value="{{IP}}">
    <label for="gw">Gateway</label><input id="gw" value="{{GW}}">
    <label for="sn">Subnet mask</label><input id="sn" value="{{SN}}">
    <label for="dns">DNS</label><input id="dns" value="{{DNS}}">
  </div>

  <button type="submit">Save and restart</button>
  <div id="status"></div>
</form>
<script>
document.getElementById('static').addEventListener('change', e => {
  document.getElementById('staticBox').className = e.target.checked ? '' : 'hidden';
});
async function scan(){
  const st = document.getElementById('status');
  st.textContent = 'Scanning...';
  await fetch('/api/wifi/scan/start');
  for (let i = 0; i < 40; i++) {
    await new Promise(r => setTimeout(r, 500));
    const r = await (await fetch('/api/wifi/scan/status')).json();
    if (!r.running) {
      if (!r.networks.length) { st.textContent = 'No networks found.'; return; }
      st.textContent = '';
      r.networks.forEach(n => {
        const b = document.createElement('button');
        b.type = 'button';
        b.textContent = n.ssid + '  (' + n.rssi + ' dBm)';
        b.onclick = () => { document.getElementById('ssid').value = n.ssid; st.textContent = ''; };
        st.appendChild(b);
      });
      return;
    }
  }
  st.textContent = 'Scan timed out.';
}
document.getElementById('f').addEventListener('submit', async e => {
  e.preventDefault();
  const st = document.getElementById('status');
  st.textContent = 'Saving...';
  const body = {
    name:     document.getElementById('name').value,
    ssid:     document.getElementById('ssid').value,
    password: document.getElementById('pass').value,
    static:   document.getElementById('static').checked,
    ip:       document.getElementById('ip').value,
    gateway:  document.getElementById('gw').value,
    subnet:   document.getElementById('sn').value,
    dns:      document.getElementById('dns').value
  };
  const r = await (await fetch('/save', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(body)
  })).json();
  st.textContent = r.ok ? 'Saved. Reconnecting...' : 'Failed: ' + (r.error || 'unknown');
});
</script>
</body>
</html>
)HTML";

inline String networkSettingsPage() {
  String page = String(EVDNTW_HTML);
  page.replace("{{NAME}}", serverName);
  page.replace("{{SSID}}", wifiSsid);
  page.replace("{{PASS}}", wifiPassword);
  page.replace("{{STATIC}}", useStaticIp ? "checked" : "");
  page.replace("{{STATICCLASS}}", useStaticIp ? "" : "hidden");
  page.replace("{{IP}}", staticIp);
  page.replace("{{GW}}", staticGateway);
  page.replace("{{SN}}", staticSubnet);
  page.replace("{{DNS}}", staticDns);
  return page;
}
