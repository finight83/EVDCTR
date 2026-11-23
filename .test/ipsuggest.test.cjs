// Test the static-IP suggestion / validation logic exactly as the pages run it.
// The JS source is taken from EVDIpSuggest.h (the string the board injects), so
// this tests the shipped code, not a copy of it.
const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, '..');
const src = fs.readFileSync(path.join(dir, 'EVDIpSuggest.h'), 'utf8');
const m = src.match(/EVDIP_JS_FN\[\] PROGMEM = R"JS\(([\s\S]*?)\)JS";/);
if (!m) { console.error('EVDIP_JS_FN not found'); process.exit(1); }
const fnSrc = m[1];

function load(preset) {
  const sandbox = { window: { EVD_IP_PRESET: preset } };
  const factory = new Function('window',
    fnSrc + '\nreturn {suggest: evdSuggest, check: evdIpCheck, octets: evdOctets};');
  return factory(sandbox.window);
}

let pass = 0, fail = 0;
function eq(name, got, want) {
  if (JSON.stringify(got) === JSON.stringify(want)) { pass++; }
  else { fail++; console.log('FAIL ' + name + '\n  got  ' + JSON.stringify(got) +
                             '\n  want ' + JSON.stringify(want)); }
}

// --- A /24 lease, the usual home LAN ---------------------------------------
{
  const p = load({live:1, ip:'192.168.100.240', mask:'255.255.255.0',
                  gw:'192.168.100.1', dns:'192.168.100.1'});
  eq('empty address -> lease network, gw .1', p.suggest(''),
     {ip:'', mask:'255.255.255.0', gw:'192.168.100.1', dns:'192.168.100.1',
      usable:true, source:'lease'});
  eq('typed 192.168.100.50 keeps its own address',
     p.suggest('192.168.100.50').ip, '192.168.100.50');
  eq('typed 192.168.100.50 -> gw same subnet', p.suggest('192.168.100.50').gw,
     '192.168.100.1');
  eq('partial 192.168.5 -> network 192.168.5.0, gw .1',
     p.suggest('192.168.5').gw, '192.168.5.1');
  eq('partial 10.0.0.7 -> gw 10.0.0.1', p.suggest('10.0.0.7').gw, '10.0.0.1');
  eq('trailing dot 192.168.100. -> gw 192.168.100.1',
     p.suggest('192.168.100.').gw, '192.168.100.1');
  eq('octet 999 is not accepted', p.octets('192.168.100.999'), null);
  eq('check good config', p.check('192.168.100.50','192.168.100.1','255.255.255.0','192.168.100.1'), '');
  eq('check gateway off-subnet',
     p.check('192.168.100.50','192.168.1.1','255.255.255.0','192.168.1.1'),
     'Gateway is outside the subnet of 192.168.100.50');
  eq('check incomplete ip', p.check('192.168.100','192.168.100.1','255.255.255.0',''),
     'IP address is not complete');
  eq('check ip == gateway', p.check('192.168.100.1','192.168.100.1','255.255.255.0',''),
     'IP address is the gateway address');
  eq('check bad mask', p.check('192.168.100.50','192.168.100.1','255.0.255.0',''),
     'Subnet mask is not valid');
  eq('check dns off-subnet',
     p.check('192.168.100.50','192.168.100.1','255.255.255.0','8.8.8.8'),
     'DNS server is outside the subnet of 192.168.100.50');
  eq('check multicast address',
     p.check('224.0.0.5','224.0.0.1','255.255.255.0',''),
     'IP address must be in the unicast range (1-223)');
}

// --- A /16 lease: the prefix must be respected ------------------------------
{
  const p = load({live:1, ip:'172.16.4.9', mask:'255.255.0.0',
                  gw:'172.16.0.1', dns:'172.16.0.2'});
  eq('/16: typed 172.16.9.9 -> gw 172.16.0.1', p.suggest('172.16.9.9').gw, '172.16.0.1');
  eq('/16: another network 172.20.1.1 -> gw 172.20.0.1', p.suggest('172.20.1.1').gw, '172.20.0.1');
  eq('/16: check passes', p.check('172.16.9.9','172.16.0.1','255.255.0.0','172.16.0.2'), '');
  eq('/16: gateway .0.1 is in subnet', p.check('172.16.9.9','172.16.0.1','255.255.0.0',''), '');
}

// --- No lease, saved values ------------------------------------------------
{
  const p = load({live:0, mask:'255.255.255.0', gw:'10.1.2.1', dns:'10.1.2.53'});
  eq('saved: typed 10.1.2.77 -> gw saved .1', p.suggest('10.1.2.77').gw, '10.1.2.1');
  eq('saved: dns kept', p.suggest('10.1.2.77').dns, '10.1.2.53');
  eq('saved: other net 10.9.9.5 -> gw 10.9.9.1', p.suggest('10.9.9.5').gw, '10.9.9.1');
  eq('saved: source is not lease', p.suggest('10.1.2.77').source, 'saved');
}

// --- Nothing known at all --------------------------------------------------
{
  const p = load({live:0, mask:'255.255.255.0', gw:'192.168.1.1', dns:'192.168.1.1'});
  eq('blank: gw .1', p.suggest('192.168.1.77').gw, '192.168.1.1');
  eq('blank: nothing typed keeps the plain default', p.suggest('').gw, '0.0.0.1');
  eq('blank: typed 192.168.4.22 -> gw 192.168.4.1', p.suggest('192.168.4.22').gw, '192.168.4.1');
}

// --- A /32 or /31 mask: no room for a gateway ------------------------------
{
  const p = load({live:1, ip:'192.168.1.5', mask:'255.255.255.255',
                  gw:'192.168.1.1', dns:'192.168.1.1'});
  eq('/32 is flagged unusable', p.suggest('192.168.1.5').usable, false);
}

// --- The pages themselves --------------------------------------------------
const dash = fs.readFileSync(path.join(dir, 'EVDCTRL.h'), 'utf8');
const setup = fs.readFileSync(path.join(dir, 'EVDNTW.h'), 'utf8');
function eqv(name, got, want) { eq(name, got, want); }
for (const [file, text] of [['EVDCTRL.h', dash], ['EVDNTW.h', setup]]) {
  eqv(file + ': has the static tick box', /type="checkbox"[^>]*n-static|id="static"/.test(text), true);
  eqv(file + ': has an IP field', /id="n-ip"|id="ip"/.test(text), true);
  eqv(file + ': gateway / mask / DNS fields',
      ['gw', 'sn', 'dns'].every(k => text.includes('id="' + k + '"') ||
                                     text.includes('id="n-' + k + '"')), true);
  eqv(file + ': carries the suggestion placeholder', text.includes('{{IPSUGGEST}}'), true);
  eqv(file + ': IP field drives the suggestion',
      /ip'\)\.addEventListener\('input'|#n-ip', 'input'/.test(text), true);
}
// The dashboard's inline script must parse.
const script = dash.match(/<script>([\s\S]*?)<\/script>/);
if (!script) { fail++; console.log('FAIL dashboard script not found'); }
else {
  try { new Function(script[1].replace('{{IPSUGGEST}}', '')); pass++; }
  catch (e) { fail++; console.log('FAIL dashboard script syntax: ' + e.message); }
}
const sscript = setup.match(/<script>([\s\S]*?)<\/script>/);
if (!sscript) { fail++; console.log('FAIL setup script not found'); }
else {
  try { new Function(sscript[1]); pass++; }
  catch (e) { fail++; console.log('FAIL setup script syntax: ' + e.message); }
}

console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
