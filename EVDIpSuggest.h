#pragma once
// ============================================================================
//  EVDIpSuggest.h - "suggested" settings for a static IP address
// ============================================================================
//  One implementation of the rules, shared by the server (which validates what
//  /save stores) and by both pages that show the fields (EVDCTRL.h dashboard and
//  EVDNTW.h setup page, through the EVDIP_JS_* strings below).
//
//  What a suggestion can be based on, in order of how much it can be trusted:
//
//   1. The IPv4 the board holds right now, when it is a real one. A DHCP lease
//      IS the network's own opinion of what is valid here: its prefix, its
//      mask, and (normally) its router and DNS. Nothing beats it.
//   2. The saved gateway / mask / DNS. They survive a failed join and are what
//      the operator last used on this network.
//   3. A classful guess: /24, gateway .1, DNS = gateway. Correct on the home
//      and small-office LANs this board lives on; wrong on /16 or /25+.
//
//  The address being typed only decides the network number: a /24 suggestion is
//  "same first three octets as the address, gateway .1". It never changes the
//  last octet, so the typed address is kept exactly as typed.
//
//  Included from EVDCTR.ino after EVDState.h (it reads staticIp and friends)
//  and after <WiFi.h> (IPAddress, WiFi.localIP()).
// ============================================================================

// ---- Which IPv4 counts as "the board's own" --------------------------------
// WiFi.localIP() is 0.0.0.0 before a join and 192.168.4.1 in AP fallback; the
// softAP address is not evidence of anything about the operator's LAN.
inline bool evdUsableIpv4(const IPAddress &a) {
  return a[0] != 0 && !(a[0] == 192 && a[1] == 168 && a[2] == 4 && a[3] == 1) &&
         !(a[0] == 169 && a[1] == 254);   // link-local: no DHCP answer at all
}

// The address/mask/gateway/DNS to start from, best evidence first.
struct EvdIpContext {
  IPAddress ip, mask, gw, dns;
  bool haveNet;    // ip+mask known: prefix and suggested address are meaningful
  bool haveGw;     // gateway known
  bool haveDns;    // DNS known
};

inline EvdIpContext evdIpContext() {
  EvdIpContext c;
  c.haveNet = c.haveGw = c.haveDns = false;
  c.mask = IPAddress(255, 255, 255, 0);       // classful guess until told better
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask(),
            gw   = WiFi.gatewayIP(), dns = WiFi.dnsIP(0);
  if (evdUsableIpv4(ip) && evdUsableIpv4(mask)) {
    c.ip = ip; c.mask = mask; c.haveNet = true;
    if (evdUsableIpv4(gw))  { c.gw = gw;   c.haveGw  = true; }
    if (evdUsableIpv4(dns)) { c.dns = dns; c.haveDns = true; }
  }
  IPAddress sip, sgw, smask, sdns;
  if (!c.haveNet && sip.fromString(staticIp) && evdUsableIpv4(sip) &&
      smask.fromString(staticSubnet) && evdUsableIpv4(smask)) {
    c.ip = sip; c.mask = smask; c.haveNet = true;
  }
  if (!c.haveGw && sgw.fromString(staticGateway) && evdUsableIpv4(sgw)) {
    c.gw = sgw; c.haveGw = true;
  }
  if (!c.haveDns && sdns.fromString(staticDns) && evdUsableIpv4(sdns)) {
    c.dns = sdns; c.haveDns = true;
  }
  if (!c.haveGw)  c.gw  = IPAddress(192, 168, 1, 1);
  if (!c.haveDns) c.dns = c.gw;
  return c;
}

inline bool evdMaskHasSpareHosts(const IPAddress &m) {
  // /31 and /32 leave no host to put a gateway on.
  return !(m[0] == 255 && m[1] == 255 && m[2] == 255 && m[3] >= 254);
}

// The suggested address: the board's own when it has a real one, else the saved
// static address, else .50 in the saved network, else 192.168.1.50.
inline String evdSuggestedIp() {
  if (evdUsableIpv4(WiFi.localIP()) && evdUsableIpv4(WiFi.subnetMask()))
    return WiFi.localIP().toString();
  IPAddress sip, smask;
  if (sip.fromString(staticIp) && evdUsableIpv4(sip)) return sip.toString();
  if (smask.fromString(staticSubnet) && evdUsableIpv4(smask) &&
      evdUsableIpv4(WiFi.subnetMask())) {
    IPAddress m = WiFi.subnetMask(), base = WiFi.localIP();
    IPAddress s((base[0] & m[0]) | (0x00 & ~m[0]),
                (base[1] & m[1]) | (0x00 & ~m[1]),
                (base[2] & m[2]) | (0x00 & ~m[2]),
                (base[3] & m[3]) | (0x32 & ~m[3]));
    if (s[3] != 0) return s.toString();
  }
  return "192.168.1.50";
}

inline String evdSuggestedGateway() { return evdIpContext().gw.toString(); }
inline String evdSuggestedMask()    { return evdIpContext().mask.toString(); }
inline String evdSuggestedDns()     { return evdIpContext().dns.toString(); }

// ---- Validation of what an operator typed ----------------------------------
// A suggestion is only ever an offer; the address has to be usable or the board
// does not come back on the network after the restart.
inline bool evdParseIpv4(const String &s, IPAddress &out) {
  String t = s;
  t.trim();
  if (!t.length() || t.indexOf(' ') >= 0) return false;
  return out.fromString(t);
}

inline bool evdValidMask(const IPAddress &m) {
  uint32_t v = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) |
               ((uint32_t)m[2] << 8) | (uint32_t)m[3];
  if (v == 0 || v == 0xFFFFFFFFu) return false;
  // Ones then zeros, no holes: 255.255.255.0 yes, 255.0.255.0 no.
  uint32_t inv = ~v;
  return (inv & (inv + 1)) == 0;
}

inline bool evdSameNetwork(const IPAddress &a, const IPAddress &b, const IPAddress &m) {
  for (int i = 0; i < 4; ++i)
    if ((a[i] & m[i]) != (b[i] & m[i])) return false;
  return true;
}

// Returns "" when acceptable, else one sentence for the operator.
inline String evdCheckStaticIp(const String &ipS, const String &gwS,
                               const String &maskS, const String &dnsS) {
  IPAddress ip, gw, mask, dns;
  if (!evdParseIpv4(ipS, ip))     return "IP address is not a valid IPv4 address";
  if (ip[0] == 0 || ip[0] == 127 || ip[0] >= 224)
    return "IP address must be in the unicast range (1-223)";
  if (!evdParseIpv4(maskS, mask) || !evdValidMask(mask))
    return "Subnet mask is not valid (e.g. 255.255.255.0)";
  if (!evdParseIpv4(gwS, gw))     return "Gateway is not a valid IPv4 address";
  if (!evdSameNetwork(ip, gw, mask))
    return "Gateway " + gwS + " is outside the subnet of " + ipS;
  if (ip == gw) return "IP address is the gateway address";
  if (dnsS.length()) {
    if (!evdParseIpv4(dnsS, dns)) return "DNS server is not a valid IPv4 address";
    if (!evdSameNetwork(ip, dns, mask))
      return "DNS server " + dnsS + " is outside the subnet of " + ipS;
  }
  return "";
}

// ---- The same rules in the browser -----------------------------------------
// Injected into both pages with evdIpSuggestScripts(). Raw string literals so
// the JS reads exactly as written; plain function declarations and `var`, no
// arrow functions, because the operator may be on an old browser.
static const char EVDIP_JS_FN[] PROGMEM = R"JS(
function evdQ(ip){ return String(ip).split('.'); }
function evdOctets(s){                       // partial input -> [o0,o1,...] or null
  if (typeof s !== 'string') return null;
  var parts = s.split('.');
  if (parts.length > 4) return null;
  var out = [];
  for (var i = 0; i < parts.length; i++) {
    if (i === parts.length - 1 && parts[i] === '') break;   // "192.168.1."
    if (!/^\d{1,3}$/.test(parts[i])) return null;
    var n = +parts[i];
    if (n > 255) return null;
    out.push(n);
  }
  return out.length ? out : null;
}
function evdOnes(mask){
  var mq = evdQ(mask), ones = 0, sawZero = false, ok = mq.length === 4;
  for (var i = 0; ok && i < 4; i++) {
    var n = +mq[i];
    if (!(n >= 0 && n <= 255)) { ok = false; break; }
    for (var b = 7; b >= 0; b--) {
      if (n & (1 << b)) { if (sawZero) ok = false; else ones++; }
      else sawZero = true;
    }
  }
  if (!ok || ones === 0 || ones === 32) return -1;
  return ones;
}
function evdCtx(){
  var pre = window.EVD_IP_PRESET || {};
  var ctx = {mask:'255.255.255.0', gw:'192.168.1.1', dns:'192.168.1.1',
             net:[], haveGw:false, haveDns:false};
  if (pre.mask) ctx.mask = pre.mask;
  if (pre.ip) ctx.net = evdQ(pre.ip);
  if (pre.gw)  { ctx.gw = pre.gw;  ctx.haveGw  = true; }
  if (pre.dns) { ctx.dns = pre.dns; ctx.haveDns = true; }
  if (!ctx.haveGw)  ctx.gw = '192.168.1.1';
  if (!ctx.haveDns) ctx.dns = ctx.gw;
  return ctx;
}
// Suggested settings for a (possibly partial) typed address.
function evdSuggest(typed){
  var ctx = evdCtx(), mask = ctx.mask;
  var typedO = evdOctets(typed), mq = evdQ(mask), ones = evdOnes(mask);
  if (ones < 0) return {ip: typed, mask: mask, gw: ctx.gw, dns: ctx.dns,
                        usable: false, source: 'guess'};
  var net = [], i;
  for (i = 0; i < 4; i++) {
    var m = +mq[i];
    // A part the operator has not typed yet comes from the context's own
    // address, so a half-typed "192.168." still suggests 192.168.1.1 and not
    // 0.0.1.1.
    var src = (typedO && i < typedO.length) ? typedO[i]
            : (ctx.net && i < ctx.net.length ? +ctx.net[i] : 0);
    net.push(src & m);
  }
  // The network number, three octets: the host octet is where the gateway's
  // .1 goes, and only the last mask octet decides that. Anything narrower than
  // /24 (the classful default this is built on) still gets .1 on its own
  // network, and a /32 has no room for one, which `usable` below reports.
  var netStr = String(net[0]) + '.' + String(net[1]) + '.' + String(net[2]);
  var hostBits = 32 - ones;
  var gw = ctx.gw, dns = ctx.dns;
  if (hostBits >= 2) {
    // Gateway is the first host of the network the address is in.
    gw = netStr + '.1';
    if (!ctx.haveDns) dns = gw;            // DNS follows the gateway we suggest
  }
  return {ip: typed, mask: mask, gw: gw, dns: dns, usable: hostBits >= 2,
          source: (window.EVD_IP_PRESET && window.EVD_IP_PRESET.live) ?
                  'lease' : (ctx.haveGw ? 'saved' : 'guess')};
}
// Live validation for the field's hint line. Empty string means fine.
function evdIpCheck(ip, gw, mask, dns){
  var q = evdOctets(ip);
  if (!q || q.length !== 4) return 'IP address is not complete';
  if (q[0] === 0 || q[0] === 127 || q[0] >= 224)
    return 'IP address must be in the unicast range (1-223)';
  if (evdOnes(mask) < 0) return 'Subnet mask is not valid';
  var mq = evdQ(mask), gwq = evdQ(gw), i;
  if (gwq.length !== 4) return 'Gateway is not a valid IPv4 address';
  for (i = 0; i < 4; i++)
    if ((q[i] & mq[i]) !== (+gwq[i] & mq[i]))
      return 'Gateway is outside the subnet of ' + ip;
  if (ip === gw) return 'IP address is the gateway address';
  if (dns) {
    var dq = evdQ(dns);
    if (dq.length !== 4) return 'DNS server is not a valid IPv4 address';
    for (i = 0; i < 4; i++)
      if ((q[i] & mq[i]) !== (+dq[i] & mq[i]))
        return 'DNS server is outside the subnet of ' + ip;
  }
  return '';
}
)JS";

// Script block: the context the page passes in, then the functions.
// `live` is true only when the board is on a real lease right now.
inline String evdIpSuggestScripts(bool live) {
  EvdIpContext c = evdIpContext();
  String js = "<script>\nwindow.EVD_IP_PRESET = {";
  js += live ? "live:1" : "live:0";
  js += ",ip:\"" + evdSuggestedIp() + "\",mask:\"" + c.mask.toString() +
        "\",gw:\"" + c.gw.toString() + "\",dns:\"" + c.dns.toString() + "\"};\n";
  js += String(EVDIP_JS_FN);
  js += "\n</script>\n";
  return js;
}
