#pragma once
// ============================================================================
//  EVDWiFi.h - WiFi bring-up, mDNS, discovery, network config, encoder presets
// ============================================================================
//
//  Runs entirely on core 0 (the AsyncTCP/HTTP task and loop()). Nothing here
//  may be called from audioEncode on core 1.
// ============================================================================
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>

WiFiUDP discoveryUdp;

// ---- Settings load/save ----------------------------------------------------
// Owned by EVDOTA.h, which is included later in the chain (it needs the HTTP
// server that EVDRoutes.h owns). Declared here because the WiFi-settings guard
// reads them.
extern volatile bool otaInProgress;
extern volatile bool otaUploadInProgress;

inline void loadWiFiSettings() {
  PrefsGuard g("evdctr", true);
  if (!g.ok) { LOGW("NVS: cannot open evdctr namespace, using defaults"); return; }
  serverName    = g.prefs.getString("srvname", "EVDCTR");
  wifiSsid      = g.prefs.getString("ssid",    "");
  wifiPassword  = g.prefs.getString("pass",    "");
  useStaticIp   = g.prefs.getBool  ("static",  false);
  staticIp      = g.prefs.getString("ip",      "");
  staticGateway = g.prefs.getString("gw",      "");
  staticSubnet  = g.prefs.getString("sn",      "");
  staticDns     = g.prefs.getString("dns",     "");
  masterVolume  = g.prefs.getUChar ("vol",     100);
  masterMuted   = g.prefs.getBool  ("mute",    false);
  playbackModeIdx = g.prefs.getUChar("mode",   MODE_MUSIC);
  quietModeEnabled = g.prefs.getBool("quiet",  false);
  audioBufferMs = g.prefs.getUShort("bufms",   200);
  LOGI("Settings loaded: name=%s ssid=%s mode=%s vol=%u",
       serverName.c_str(), wifiSsid.c_str(), playbackModeCStr(),
       (unsigned)masterVolume);
}

inline void saveWiFiSettings() {
  PrefsGuard g("evdctr", false);
  if (!g.ok) { LOGE("NVS: cannot open evdctr for write"); return; }
  g.prefs.putString("srvname", serverName);
  g.prefs.putString("ssid",    wifiSsid);
  g.prefs.putString("pass",    wifiPassword);
  g.prefs.putBool  ("static",  useStaticIp);
  g.prefs.putString("ip",      staticIp);
  g.prefs.putString("gw",      staticGateway);
  g.prefs.putString("sn",      staticSubnet);
  g.prefs.putString("dns",     staticDns);
  g.prefs.putUChar ("vol",     masterVolume);
  g.prefs.putBool  ("mute",    masterMuted);
  g.prefs.putUChar ("mode",    playbackModeIdx);
  g.prefs.putBool  ("quiet",   quietModeEnabled);
  g.prefs.putUShort("bufms",   audioBufferMs);
  LOGI("Settings saved");
}

// ---- Encoder presets per playback mode -------------------------------------
// Same three-mode model as the unsplit server: switching mode applies a
// stored encoder preset. Only the values that affect the encoder live here;
// buffer latency is a stream-side concern and is forwarded to STR.
struct ModeEncSettings {
  uint32_t bitrate;
  int      complexity;
  bool     vbr;
  bool     vbrConstraint;
  int      signal;
  int      application;
  uint32_t opusRate;
};

inline ModeEncSettings defaultPreset(ModeIdx m) {
  switch (m) {
    case MODE_OFF:   return { 64000, 5,  true, true,  OPUS_SIGNAL_VOICE, OPUS_APPLICATION_VOIP,  OPUS_SAMPLE_RATE_24K };
    case MODE_MOVIE: return { 128000, 8, true, true,  OPUS_SIGNAL_MUSIC, OPUS_APPLICATION_AUDIO, OPUS_SAMPLE_RATE_48K };
    default:         return { 96000, 10, true, true, OPUS_SIGNAL_MUSIC, OPUS_APPLICATION_AUDIO, OPUS_SAMPLE_RATE_48K };
  }
}

inline void loadModePreset(ModeIdx m, ModeEncSettings &out) {
  out = defaultPreset(m);
  PrefsGuard g("evdctr", true);
  if (!g.ok) return;
  char k[12];
  snprintf(k, sizeof(k), "m%d_br", (int)m);   out.bitrate      = g.prefs.getUInt (k, out.bitrate);
  snprintf(k, sizeof(k), "m%d_cx", (int)m);   out.complexity   = g.prefs.getUChar(k, (uint8_t)out.complexity);
  snprintf(k, sizeof(k), "m%d_vb", (int)m);   out.vbr          = g.prefs.getBool (k, out.vbr);
  snprintf(k, sizeof(k), "m%d_vc", (int)m);   out.vbrConstraint= g.prefs.getBool (k, out.vbrConstraint);
  snprintf(k, sizeof(k), "m%d_sg", (int)m);   out.signal       = g.prefs.getUChar(k, (uint8_t)out.signal);
  snprintf(k, sizeof(k), "m%d_ap", (int)m);   out.application  = g.prefs.getUChar(k, (uint8_t)out.application);
  snprintf(k, sizeof(k), "m%d_rt", (int)m);   out.opusRate     = g.prefs.getUInt (k, out.opusRate);
}

inline void saveModePreset(ModeIdx m, const ModeEncSettings &s) {
  PrefsGuard g("evdctr", false);
  if (!g.ok) return;
  char k[12];
  snprintf(k, sizeof(k), "m%d_br", (int)m);   g.prefs.putUInt (k, s.bitrate);
  snprintf(k, sizeof(k), "m%d_cx", (int)m);   g.prefs.putUChar(k, (uint8_t)s.complexity);
  snprintf(k, sizeof(k), "m%d_vb", (int)m);   g.prefs.putBool (k, s.vbr);
  snprintf(k, sizeof(k), "m%d_vc", (int)m);   g.prefs.putBool (k, s.vbrConstraint);
  snprintf(k, sizeof(k), "m%d_sg", (int)m);   g.prefs.putUChar(k, (uint8_t)s.signal);
  snprintf(k, sizeof(k), "m%d_ap", (int)m);   g.prefs.putUChar(k, (uint8_t)s.application);
  snprintf(k, sizeof(k), "m%d_rt", (int)m);   g.prefs.putUInt (k, s.opusRate);
}

// The single point that copies a preset into the live encoder variables.
// audioEncodeTask picks the changes up on its next iteration, serialised with
// opus_encode(), which is the only safe way to touch libopus.
inline void applyModeSettingsToLive(ModeIdx m) {
  ModeEncSettings s;
  loadModePreset(m, s);
  g_newBitrate        = (int)s.bitrate;
  g_pendingComplexity = s.complexity;
  g_newVbr            = s.vbr ? 1 : 0;
  g_newVbrConstraint  = s.vbrConstraint ? 1 : 0;
  g_newSignal         = s.signal;
  g_newApplication    = s.application;
  g_encoderSettingsDirty = true;
  if (s.opusRate != g_opusSampleRate) g_pendingOpusSampleRate = s.opusRate;
  LOGI("Mode %s applied: %u bps cx%d rate %u",
       playbackModeCStr(), (unsigned)s.bitrate, s.complexity, (unsigned)s.opusRate);
}

// Capture the live encoder values back into the mode slot before switching.
inline void captureLiveIntoMode(ModeIdx m) {
  ModeEncSettings s;
  s.bitrate       = (uint32_t)g_newBitrate;
  s.complexity    = (g_pendingComplexity >= 0) ? (int)g_pendingComplexity : currentOpusComplexity;
  s.vbr           = g_newVbr != 0;
  s.vbrConstraint = g_newVbrConstraint != 0;
  s.signal        = g_newSignal;
  s.application   = g_newApplication;
  s.opusRate      = g_opusSampleRate;
  saveModePreset(m, s);
}

// ---- AP fallback -----------------------------------------------------------
inline void startSetupAccessPoint() {
  String apName = sanitizeServerName(serverName);
  if (!apName.length()) apName = "EVDCTR";
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str(), strlen(default_ap_password) ? default_ap_password : nullptr);
  accessPointActive = true;
  LOGW("WiFi: STA failed, AP fallback '%s' at %s", apName.c_str(),
       WiFi.softAPIP().toString().c_str());
}

// ---- STA bring-up ----------------------------------------------------------
inline void setup_wifi_sta() {
  if (!wifiSsid.length()) {
    startSetupAccessPoint();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // sleep on the capture side adds jitter
  if (useStaticIp && staticIp.length()) {
    IPAddress ip, gw, sn, dns;
    if (ip.fromString(staticIp) && gw.fromString(staticGateway) &&
        sn.fromString(staticSubnet)) {
      if (!dns.fromString(staticDns)) dns = gw;
      WiFi.config(ip, gw, sn, dns);
    }
  }
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  const unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
    delay(50);
  }
  if (WiFi.status() != WL_CONNECTED) {
    startSetupAccessPoint();
    return;
  }
  accessPointActive = false;
  LOGI("WiFi: connected to '%s' as %s (rssi %d)", wifiSsid.c_str(),
       WiFi.localIP().toString().c_str(), WiFi.RSSI());

  String host = sanitizeServerName(serverName);
  if (!host.length()) host = "EVDCTR";
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    LOGI("mDNS: http://%s.local/", host.c_str());
  }
}

inline void handleRestart(AsyncWebServerRequest *request) {
  request->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  delay(150);
  ESP.restart();
}

// ---- Network test ----------------------------------------------------------
volatile bool     netTestRunning = false;
volatile bool     netTestDone    = false;
volatile bool     netTestOk      = false;
volatile uint32_t netTestMs      = 0;

inline void networkTestTask(void *) {
  netTestRunning = true;
  netTestOk      = false;
  const unsigned long t0 = millis();
  IPAddress dns = WiFi.gatewayIP();
  WiFiClient c;
  c.setTimeout(3);
  bool ok = dns && c.connect(dns, 80, 2000);
  if (!ok) {
    // A gateway that does not answer on 80 is normal; fall back to a DNS
    // resolution, which is the cheapest proof the uplink works at all.
    IPAddress probe;
    ok = WiFi.hostByName("example.com", probe) == 1;
  }
  if (ok) c.stop();
  netTestMs      = millis() - t0;
  netTestOk      = ok;
  netTestDone    = true;
  netTestRunning = false;
  vTaskDelete(nullptr);
}

inline void handleNetworkTestStatus(AsyncWebServerRequest *request) {
  if (request->hasParam("start")) {
    if (!netTestRunning) {
      netTestDone = false;
      if (xTaskCreatePinnedToCore(networkTestTask, "netTest", 4096, nullptr,
                                  1, nullptr, 0) != pdPASS) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"task create failed\"}");
        return;
      }
    }
    request->send(200, "application/json", "{\"ok\":true,\"running\":true}");
    return;
  }
  String json = String("{\"running\":") + (netTestRunning ? "true" : "false") +
                ",\"done\":"   + (netTestDone ? "true" : "false") +
                ",\"ok\":"     + (netTestOk ? "true" : "false") +
                ",\"ms\":"     + String((unsigned)netTestMs) + "}";
  request->send(200, "application/json", json);
}

// ---- WiFi scan -------------------------------------------------------------
volatile bool scanRunning = false;
String        scanJson    = "[]";

inline void wifiScanTask(void *) {
  scanRunning = true;
  int n = WiFi.scanNetworks(false, true);
  String out = "[";
  for (int i = 0; i < n; ++i) {
    if (i) out += ",";
    out += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" +
           String(WiFi.RSSI(i)) + ",\"secure\":" +
           (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  out += "]";
  scanJson    = out;
  scanRunning = false;
  vTaskDelete(nullptr);
}

inline void handleWifiScanStart(AsyncWebServerRequest *request) {
  if (otaInProgress || otaUploadInProgress) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  if (!scanRunning) {
    if (xTaskCreatePinnedToCore(wifiScanTask, "wifiScan", 4096, nullptr, 1,
                                nullptr, 0) != pdPASS) {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"task create failed\"}");
      return;
    }
  }
  request->send(200, "application/json", "{\"ok\":true}");
}

inline void handleWifiScanStatus(AsyncWebServerRequest *request) {
  String json = String("{\"running\":") + (scanRunning ? "true" : "false") +
                ",\"networks\":" + scanJson + "}";
  request->send(200, "application/json", json);
}

inline void handleNetworkState(AsyncWebServerRequest *request) {
  String json = String("{\"name\":\"") + jsonEscape(serverName) +
                "\",\"ssid\":\"" + jsonEscape(WiFi.SSID()) +
                "\",\"ip\":\""   + WiFi.localIP().toString() +
                "\",\"ap\":\""   + (accessPointActive ? "true" : "false") +
                "\",\"static\":" + (useStaticIp ? "true" : "false") +
                ",\"gw\":\"" + staticGateway + "\",\"sn\":\"" + staticSubnet +
                "\",\"dns\":\"" + staticDns + "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
  request->send(200, "application/json", json);
}

inline void handleSaveNetworkSettings(AsyncWebServerRequest *request,
                                      uint8_t *data, size_t len,
                                      size_t index, size_t total) {
  if (otaInProgress || otaUploadInProgress) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"OTA in progress\"}");
    return;
  }
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 1024,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;
  DynamicJsonDocument in(768);
  if (deserializeJson(in, body, bodyLen)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  if (in["name"].is<const char*>()) {
    String v = sanitizeServerName(in["name"].as<String>());
    if (v.length()) serverName = v;
  }
  if (in["ssid"].is<const char*>())     wifiSsid     = in["ssid"].as<String>();
  if (in["password"].is<const char*>()) wifiPassword = in["password"].as<String>();
  if (in["static"].is<bool>())          useStaticIp  = in["static"].as<bool>();
  if (in["ip"].is<const char*>())       staticIp     = in["ip"].as<String>();
  if (in["gateway"].is<const char*>())  staticGateway= in["gateway"].as<String>();
  if (in["subnet"].is<const char*>())   staticSubnet = in["subnet"].as<String>();
  if (in["dns"].is<const char*>())      staticDns    = in["dns"].as<String>();

  saveWiFiSettings();
  request->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  delay(200);
  ESP.restart();
}

// ---- Discovery responder ---------------------------------------------------
// Answers the same "ENKELVOUDP/1 DISCOVER" probe the old server answered, so
// any tooling that looks for an Enkelvoud box still finds one - except that
// the box it finds is now the *controller*. The reply names both boards.
inline void setupDiscoveryResponder() {
  if (!discoveryUdp.begin(DISCOVERY_PORT)) {
    LOGE("discovery: bind %d failed", DISCOVERY_PORT);
    return;
  }
  LOGI("discovery: listening on %d", DISCOVERY_PORT);
}

inline void serviceDiscovery() {
  int sz = discoveryUdp.parsePacket();
  if (sz <= 0) return;
  char buf[96];
  int n = discoveryUdp.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = '\0';
  if (strncmp(buf, DISCOVERY_MAGIC_REQ, strlen(DISCOVERY_MAGIC_REQ)) != 0) return;

  String reply = String(DISCOVERY_MAGIC_RPY) + " role=ctr name=" +
                 sanitizeServerName(serverName) + " ip=" +
                 WiFi.localIP().toString();
  discoveryUdp.beginPacket(discoveryUdp.remoteIP(), discoveryUdp.remotePort());
  discoveryUdp.write((const uint8_t *)reply.c_str(), reply.length());
  discoveryUdp.endPacket();
}

// ---- Volume potentiometer --------------------------------------------------
inline void serviceVolumePot() {
  static unsigned long last = 0;
  if (millis() - last < 100) return;
  last = millis();
  int raw = analogRead(pinVolumePot);
  int v   = map(raw, 0, 4095, 0, 100);
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  potVolume = (uint8_t)v;
}
