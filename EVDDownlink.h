#pragma once
// ============================================================================
//  EVDDownlink.h - CTR side of the control plane to EVDSTR
// ============================================================================
//
//  The pin link (EVDLinkSpi.h) carries one thing: an Opus frame per 20 ms,
//  in one direction. Everything else the operator does on the dashboard -
//  create a zone, move a node, mute a zone, push a config - is a *network*
//  job, and on this board the network belongs to EVDSTR. So the dashboard
//  talks to EVDCTR (which it always did, at the same URL), and EVDCTR relays
//  the request to STR and returns STR's answer.
//
//  Relay transport: HTTP. CTR POSTs to STR's own HTTP server on
//  http://<STR_IP>/<path> and streams the response body back. Doing it over
//  WiFi rather than over the pin link is deliberate:
//
//    - The pin link has no return channel. Adding one means a second SPI
//      device, a MISO line, a second handshake and a protocol for
//      multi-frame replies, all to carry a few hundred bytes a minute.
//    - STR already speaks HTTP for its dashboard. A relayed request is the
//      same request, so there is exactly one implementation of zones and
//      players, and no second code path to keep in step.
//    - Control traffic is operator-rate (a few requests a minute), so the
//      cost is nil.
//
//  If STR is unreachable the relay answers 502 with `"downlink":false`, and
//  the dashboard shows the Stream tab as offline instead of hanging.
//
//  Endpoints forwarded (all POST, JSON body passed through untouched):
//    /api/config            volume, mute, mode, quiet, buffer latency
//    /api/audio/encoding    bitrate, complexity, VBR, signal, application
//    /api/audio/encoding/revert
//    /api/audio/encoding/defaults
//    /api/zones/create | /delete | /move | /config | /remove-node
//    /api/source            BT / AUX / USB selection on STR's players
//    /api/cmd               next / prev / bt / aux / usb
//
//  Endpoints read (GET):
//    /api/state             STR's stream state, merged into CTR's /api/state
//    /api/players           the player list, shown on the Stream tab
//    /api/zones             the zone list, shown on the Stream tab
//    /api/audio/encoding    STR's live encoder mirror
//
//  ============================================================================
#include <HTTPClient.h>

// STR's address. Set from the dashboard (Stream tab) and kept in NVS; a blank
// value means "not paired yet" and every relay answers 503 immediately.
String strHost = "";

inline void loadDownlinkSettings() {
  PrefsGuard g("evdctr", true);
  if (!g.ok) return;
  strHost = g.prefs.getString("strhost", "");
}

inline void saveDownlinkSettings() {
  PrefsGuard g("evdctr", false);
  if (!g.ok) return;
  g.prefs.putString("strhost", strHost);
}

// ---- Raw relay -------------------------------------------------------------
// One HTTP exchange with STR. Runs on the AsyncTCP worker (core 0). The
// timeout is short on purpose: the operator is waiting on the dashboard, and
// a slow STR must not tie up the worker that is also serving /api/state.
static const uint16_t DOWNLINK_TIMEOUT_MS = 2500;

inline bool downlinkRequest(const String &method, const String &path,
                            const String &body, String &outBody,
                            int &outCode) {
  outBody = "";
  outCode = 0;
  if (!strHost.length()) { outCode = 503; return false; }
  if (WiFi.status() != WL_CONNECTED && !accessPointActive) {
    outCode = 503; return false;
  }
  if (!lockFor(linkCtrlMutex, 2000)) { outCode = 503; return false; }

  HTTPClient http;
  http.setConnectTimeout(DOWNLINK_TIMEOUT_MS);
  http.setTimeout(DOWNLINK_TIMEOUT_MS);
  http.setReuse(false);
  String url = String("http://") + strHost + path;
  bool began = http.begin(url);
  if (!began) {
    unlockFor(linkCtrlMutex);
    outCode = 502;
    return false;
  }
  bool ok = false;
  if (method == "POST") {
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)body.c_str(), body.length());
    outCode = code;
    if (code > 0) { outBody = http.getString(); ok = (code >= 200 && code < 300); }
  } else {
    int code = http.GET();
    outCode = code;
    if (code > 0) { outBody = http.getString(); ok = (code >= 200 && code < 300); }
  }
  http.end();
  unlockFor(linkCtrlMutex);
  return ok;
}

// ---- Relay handler ---------------------------------------------------------
// Forwards a POST body to STR verbatim and returns STR's own body, so the
// dashboard's existing fetch() calls need no change: they still POST to
// EVDCTR, they still get the JSON shape STR produced.
inline void downlinkForwardPost(AsyncWebServerRequest *request,
                                uint8_t *data, size_t len,
                                size_t index, size_t total,
                                const char *strPath) {
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 1024,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;

  String out; int code = 0;
  bool ok = downlinkRequest("POST", strPath, String(body ? body : ""), out, code);
  if (!out.length()) {
    out = String("{\"ok\":false,\"downlink\":false,\"error\":\"EVDSTR unreachable\"}");
  }
  int send = ok ? code : (code > 0 ? code : 502);
  request->send(send, "application/json", out);
}

// Convenience wrappers used by the route table.
#define DOWNLINK_FORWARD_CTR(path)                                            \
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len,               \
     size_t index, size_t total) {                                            \
    downlinkForwardPost(request, data, len, index, total, path);              \
  }

// ---- State pull ------------------------------------------------------------
// Merged into /api/state as a "downlink" object. Called from the state
// handler; a failure is reported as downlink:false with the last cached
// values so the Stream tab can show "stale" rather than blank.
inline void refreshDownlinkCache() {
  if (!strHost.length()) return;
  if (!lockFor(linkCtrlMutex, 1000)) return;
  HTTPClient http;
  http.setConnectTimeout(DOWNLINK_TIMEOUT_MS);
  http.setTimeout(DOWNLINK_TIMEOUT_MS);
  bool began = http.begin(String("http://") + strHost + "/api/state");
  if (!began) { unlockFor(linkCtrlMutex); return; }
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, body)) {
      gDownlinkCache.valid      = true;
      gDownlinkCache.updatedMs  = millis();
      gDownlinkCache.subscribers= doc["subscribers"] | 0;
      gDownlinkCache.queueDepth = doc["queueDepth"] | 0;
      gDownlinkCache.queueTarget= doc["queueTarget"] | 0;
      gDownlinkCache.pacerLateMs= doc["pacerLateMs"] | 0;
      gDownlinkCache.framesSent = doc["framesSent"] | 0;
      gDownlinkCache.framesDropped = doc["framesDropped"] | 0;
      gDownlinkCache.wifiRssi   = doc["rssi"] | 0;
      gDownlinkCache.heapFree   = doc["heapFree"] | 0;
      gDownlinkCache.heapMinFree= doc["heapMinFree"] | 0;
      gDownlinkCache.upMs       = doc["upMs"] | 0;
      gDownlinkCache.strIp      = doc["ip"] | "";
      gDownlinkCache.strVersion = doc["version"] | "";
    }
  }
  http.end();
  unlockFor(linkCtrlMutex);
}

// ---- Cache readers for the dashboard --------------------------------------
inline String downlinkPlayersJson() {
  if (!strHost.length()) return "[]";
  String out; int code = 0;
  if (downlinkRequest("GET", "/api/players", "", out, code)) return out;
  return "[]";
}

inline String downlinkZonesJson() {
  if (!strHost.length()) return "[]";
  String out; int code = 0;
  if (downlinkRequest("GET", "/api/zones", "", out, code)) return out;
  return "[]";
}

// ---- Pairing ---------------------------------------------------------------
// The Stream tab posts {"host":"192.168.1.42"} here. We prove the address
// before keeping it: a wrong IP that accepts a TCP connection but answers
// nothing is worse than no address at all.
inline void handleLinkPair(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total) {
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 512,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;
  DynamicJsonDocument in(256);
  if (deserializeJson(in, body, bodyLen) || !in["host"].is<const char*>()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String host = in["host"].as<String>();
  host.trim();
  if (host.startsWith("http://")) host = host.substring(7);
  while (host.endsWith("/")) host.remove(host.length() - 1);

  String out; int code = 0;
  String prev = strHost;
  strHost = host;
  bool ok = downlinkRequest("GET", "/api/state", "", out, code);
  if (!ok) {
    strHost = prev;
    request->send(502, "application/json",
                  "{\"ok\":false,\"error\":\"no EVDSTR answered at that address\"}");
    return;
  }
  saveDownlinkSettings();
  request->send(200, "application/json",
                "{\"ok\":true,\"host\":\"" + jsonEscape(strHost) + "\"}");
}

inline void handleLinkState(AsyncWebServerRequest *request) {
  refreshDownlinkCache();
  String json = String("{\"paired\":") + (strHost.length() ? "true" : "false") +
                ",\"host\":\"" + jsonEscape(strHost) + "\"" +
                ",\"cached\":" + (gDownlinkCache.valid ? "true" : "false") +
                ",\"updatedMs\":" + String((unsigned long)gDownlinkCache.updatedMs) +
                ",\"subscribers\":" + String((unsigned long)gDownlinkCache.subscribers) +
                ",\"queueDepth\":" + String((unsigned long)gDownlinkCache.queueDepth) +
                ",\"queueTarget\":" + String((unsigned long)gDownlinkCache.queueTarget) +
                ",\"pacerLateMs\":" + String((unsigned long)gDownlinkCache.pacerLateMs) +
                ",\"strVersion\":\"" + jsonEscape(gDownlinkCache.strVersion) + "\"" +
                ",\"strIp\":\"" + jsonEscape(gDownlinkCache.strIp) + "\"" +
                ",\"linkSent\":" + String((unsigned long)statLinkSent) +
                ",\"linkDropped\":" + String((unsigned long)statLinkDropped) +
                ",\"linkCsTimeouts\":" + String((unsigned long)statLinkCsTimeouts) +
                ",\"linkSilence\":" + String((unsigned long)statLinkSilence) +
                ",\"linkHealthy\":" + (gLinkHealthy ? "true" : "false") + "}";
  request->send(200, "application/json", json);
}

// ---- STR OTA ---------------------------------------------------------------
// A new STR image is uploaded to CTR (which has the only LittleFS spool) and
// pushed over WiFi to STR's /api/ota/upload. The upload path lives in EVDOTA.h.
