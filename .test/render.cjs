// Render the dashboard's Network tab and the AP setup page as the board would
// serve them, with the real injected suggestion script, so the behaviour can be
// checked in a browser instead of only in node.
//   node .test/render.cjs
const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, '..');
const out = path.join(dir, '.test', 'out');
fs.mkdirSync(out, { recursive: true });

const sug = fs.readFileSync(path.join(dir, 'EVDIpSuggest.h'), 'utf8');
const fn = sug.match(/EVDIP_JS_FN\[\] PROGMEM = R"JS\(([\s\S]*?)\)JS";/)[1];

// A board on 192.168.100.240/24 with gateway and DNS at .1 (the screenshot).
const preset = { live: 1, ip: '192.168.100.240', mask: '255.255.255.0',
                 gw: '192.168.100.1', dns: '192.168.100.1' };
const script = '<script>window.EVD_IP_PRESET = ' + JSON.stringify(preset) + ';\n' +
               fn + '</script>\n';

let dash = fs.readFileSync(path.join(dir, 'EVDCTRL.h'), 'utf8')
  .match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
dash = dash.replace('{{IPSUGGEST}}', script);
fs.writeFileSync(path.join(out, 'dashboard.html'), dash);

let setup = fs.readFileSync(path.join(dir, 'EVDNTW.h'), 'utf8')
  .match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
setup = setup.replace('{{NAME}}', 'EVDCTR').replace('{{SSID}}', '')
             .replace('{{PASS}}', '').replace('{{STATIC}}', '')
             .replace('{{STATICCLASS}}', 'hidden')
             .replace('{{IP}}', '').replace('{{GW}}', '')
             .replace('{{SN}}', '').replace('{{DNS}}', '')
             .replace('{{IPSUGGEST}}', script);
fs.writeFileSync(path.join(out, 'setup.html'), setup);
console.log('wrote ' + out);
