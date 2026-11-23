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
  .hint{color:var(--muted);font-size:.8rem;margin:4px 0 0}
  .hint.bad{color:#ff5d5d}
  .hint.ok{color:#37dba0}
  .sugg{float:right;font-weight:400;font-size:.75rem;color:var(--accent);cursor:pointer}
  #status{margin-top:1rem;color:var(--muted)}
  #status button{display:block;margin-top:6px}
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
    <label for="ip">IP address</label>
    <input id="ip" inputmode="decimal" autocomplete="off" placeholder="192.168.1.50" value="{{IP}}">
    <div class="hint" id="ipmsg"></div>
    <label for="gw">Gateway <span class="sugg" id="gwsugg"></span></label>
    <input id="gw" inputmode="decimal" autocomplete="off" value="{{GW}}">
    <label for="sn">Subnet mask <span class="sugg" id="snsugg"></span></label>
    <input id="sn" inputmode="decimal" autocomplete="off" value="{{SN}}">
    <label for="dns">DNS server <span class="sugg" id="dnssugg"></span></label>
    <input id="dns" inputmode="decimal" autocomplete="off" value="{{DNS}}">
    <small>The gateway, mask and DNS follow the address as you type. Tap a
       suggestion to use it; edit any field to override it.</small>
  </div>

  <button type="submit">Save and restart</button>
  <div id="status"></div>
</form>
<script>
// As the address is typed, the gateway, mask and DNS are offered and filled in
// when they are still empty or still hold what was offered last. evdSuggest()
// and evdIpCheck() come from EVDIpSuggest.h, injected below.
var lastOffered = {gw:'', sn:'', dns:''};
var $ = function(id){ return document.getElementById(id); };
function showStatic(){
  $('staticBox').className = $('static').checked ? '' : 'hidden';
}
function fillField(id, key, v){
  if (!v) return;
  var el = $(id);
  if (!el.value.trim() || el.value.trim() === lastOffered[key]) {
    el.value = v; lastOffered[key] = v;
  }
}
function applySuggestion(force){
  var s = evdSuggest($('ip').value.trim());
  $('gw').placeholder = s.gw; $('sn').placeholder = s.mask; $('dns').placeholder = s.dns;
  $('gwsugg').textContent = s.gw; $('snsugg').textContent = s.mask;
  $('dnssugg').textContent = s.dns;
  if (force || !$('gw').value.trim())  fillField('gw',  'gw',  s.gw);
  if (force || !$('sn').value.trim())  fillField('sn',  'sn',  s.mask);
  if (force || !$('dns').value.trim()) fillField('dns', 'dns', s.dns);
  var msg = evdIpCheck($('ip').value.trim(), $('gw').value.trim(),
                       $('sn').value.trim(), $('dns').value.trim());
  var box = $('ipmsg');
  box.textContent = msg || (s.usable ? '' : 'A mask this tight leaves no room for a gateway.');
  box.className = 'hint ' + ((msg || !s.usable) ? 'bad' : 'ok');
  return !msg && s.usable;
}
['gw','sn','dns'].forEach(function(id){
  $(id + 'sugg').onclick = function(){ fillField(id, id, $(id).placeholder); applySuggestion(false); };
});
$('static').addEventListener('change', function(){
  showStatic();
  if ($('static').checked) applySuggestion(true);
});
$('ip').addEventListener('input', function(){ applySuggestion(false); });
['gw','sn','dns'].forEach(function(id){
  $(id).addEventListener('input', function(){ applySuggestion(false); });
});
if ($('static').checked) applySuggestion(false);

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
  const wantStatic = document.getElementById('static').checked;
  if (wantStatic && !applySuggestion(false)) {
    st.textContent = 'Fix the static IP settings first.';
    return;
  }
  st.textContent = 'Saving...';
  const body = {
    name:     document.getElementById('name').value,
    ssid:     document.getElementById('ssid').value,
    password: document.getElementById('pass').value,
    static:   wantStatic,
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
{{IPSUGGEST}}
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
  // Saved values first; the suggestion only fills what was never saved, so the
  // operator is never shown a different address than the one stored.
  page.replace("{{IP}}",  staticIp.length()      ? staticIp      : evdSuggestedIp());
  page.replace("{{GW}}",  staticGateway.length() ? staticGateway : evdSuggestedGateway());
  page.replace("{{SN}}",  staticSubnet.length()  ? staticSubnet  : evdSuggestedMask());
  page.replace("{{DNS}}", staticDns.length()     ? staticDns     : evdSuggestedDns());
  page.replace("{{IPSUGGEST}}", evdIpSuggestScripts(evdUsableIpv4(WiFi.localIP())));
  return page;
}
