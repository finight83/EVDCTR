// Drive the rendered pages the way an operator would: tick the box, type an IP,
// check that gateway / mask / DNS fill in as the address is typed.
//   node .test/dom.test.cjs
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

let pass = 0, fail = 0;
function eq(name, got, want) {
  if (got === want) pass++;
  else { fail++; console.log('FAIL ' + name + '\n  got  ' + JSON.stringify(got) +
                             '\n  want ' + JSON.stringify(want)); }
}
function load(file) {
  const html = fs.readFileSync(path.join(__dirname, 'out', file), 'utf8');
  const dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://192.168.100.240/' });
  const w = dom.window, d = w.document;
  // The pages poll the board; a refused fetch is fine here.
  w.fetch = () => Promise.reject(new Error('offline test'));
  return { w, d, type(el, v) {
    el.value = v;
    el.dispatchEvent(new w.Event('input', { bubbles: true }));
  }, click(el) { el.dispatchEvent(new w.Event('click', { bubbles: true })); } };
}

// ---- dashboard ------------------------------------------------------------
{
  const { w, d, type, click } = load('dashboard.html');
  const ip = d.querySelector('#n-ip'), gw = d.querySelector('#n-gw'),
        sn = d.querySelector('#n-sn'), dns = d.querySelector('#n-dns'),
        box = d.querySelector('#n-staticbox'), chk = d.querySelector('#n-static');
  eq('fields start hidden', box.className.includes('off'), true);
  chk.checked = true; chk.dispatchEvent(new w.Event('change', { bubbles: true }));
  eq('ticking reveals the fields', box.className.includes('off'), false);
  eq('tick fills the suggested IP', ip.value, '192.168.100.240');
  eq('tick fills gateway', gw.value, '192.168.100.1');
  eq('tick fills mask', sn.value, '255.255.255.0');
  eq('tick fills DNS', dns.value, '192.168.100.1');

  // Clear the fields and type a different address character by character.
  [ip, gw, sn, dns].forEach(e => type(e, ''));
  type(ip, '192.168.100.240');       // the screenshot's address, fully typed
  eq('typed address -> gateway', gw.value, '192.168.100.1');
  eq('typed address -> mask', sn.value, '255.255.255.0');
  eq('typed address -> DNS', dns.value, '192.168.100.1');
  eq('no warning on a valid address', d.querySelector('#n-ipmsg').textContent, '');

  // Partial typing must already suggest, not wait for the last octet.
  [gw, sn, dns].forEach(e => type(e, ''));
  type(ip, '10.0.0.7');
  eq('partial-to-full typing follows the network', gw.value, '10.0.0.1');

  // An edited gateway is not overwritten by further typing.
  type(gw, '10.0.0.254');
  type(ip, '10.0.0.7');
  eq('operator edit survives retyping', gw.value, '10.0.0.254');
  type(ip, '10.0.0.8');
  eq('operator edit survives more typing', gw.value, '10.0.0.254');

  // Off-subnet gateway is called out, and save refuses.
  type(gw, '192.168.1.1');
  eq('off-subnet gateway flagged',
     d.querySelector('#n-ipmsg').textContent, 'Gateway is outside the subnet of 10.0.0.8');
  eq('hint turns red', d.querySelector('#n-ipmsg').className, 'hint bad');
  let posted = 0;
  w.fetch = () => { posted++; return Promise.resolve({ ok: true, text: () => Promise.resolve('{"ok":true}') }); };
  click(d.querySelector('#n-save'));
  eq('save refused while invalid', posted, 0);
  eq('save says why', d.querySelector('#n-savemsg').textContent,
     'Fix the static IP settings first.');

  // A valid configuration is posted with all four values.
  type(gw, '10.0.0.1');
  let sent = null;
  w.fetch = (u, o) => { if (u === '/save') sent = JSON.parse(o.body);
    return Promise.resolve({ ok: true, text: () => Promise.resolve('{"ok":true}') }); };
  click(d.querySelector('#n-save'));
  eq('save posts static true', sent && sent.static, true);
  eq('save posts the ip', sent && sent.ip, '10.0.0.8');
  eq('save posts the gateway', sent && sent.gateway, '10.0.0.1');
  eq('save posts the mask', sent && sent.subnet, '255.255.255.0');
  eq('save posts the dns', sent && sent.dns, '10.0.0.1');

  // Unticking hides them and saves static:false.
  chk.checked = false; chk.dispatchEvent(new w.Event('change', { bubbles: true }));
  eq('unticking hides the fields', box.className.includes('off'), true);
  sent = null;
  click(d.querySelector('#n-save'));
  eq('save posts static false', sent && sent.static, false);
}

// ---- AP setup page --------------------------------------------------------
{
  const { w, d, type } = load('setup.html');
  const chk = d.querySelector('#static'), ip = d.querySelector('#ip'),
        gw = d.querySelector('#gw'), sn = d.querySelector('#sn'), dns = d.querySelector('#dns');
  eq('setup: fields start hidden', d.querySelector('#staticBox').className, 'hidden');
  chk.checked = true; chk.dispatchEvent(new w.Event('change', { bubbles: true }));
  eq('setup: ticking reveals the fields', d.querySelector('#staticBox').className, '');
  eq('setup: suggested IP', ip.value, '192.168.100.240');
  eq('setup: suggested gateway', gw.value, '192.168.100.1');
  [ip, gw, sn, dns].forEach(e => type(e, ''));
  type(ip, '192.168.100.240');
  eq('setup: typed address fills gateway', gw.value, '192.168.100.1');
  eq('setup: typed address fills mask', sn.value, '255.255.255.0');
  eq('setup: typed address fills DNS', dns.value, '192.168.100.1');
  eq('setup: valid address has no warning', d.querySelector('#ipmsg').textContent, '');
  type(ip, '192.168.100.1');
  eq('setup: address == gateway flagged', d.querySelector('#ipmsg').textContent,
     'IP address is the gateway address');
  // Clicking a suggestion uses it.
  type(ip, '172.20.5.9');
  d.querySelector('#gwsugg').dispatchEvent(new w.Event('click', { bubbles: true }));
  eq('setup: clicking the suggestion adopts it', gw.value, '172.20.0.1');
}

console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
